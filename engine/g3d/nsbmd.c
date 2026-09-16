/*
 * engine/g3d/nsbmd.c -- NSBMD / NSBTX containers, resource dictionaries,
 * TEX0 texture and palette records, and the MDL0 shape dictionary.
 *
 * Provenance: every structure below is one aletheia's asset lane cracked
 * and recorded in docs/ASSETS.md, and this is an independent C reader of
 * the same bytes, not a translation of the Python:
 *
 *   - the outer G3D container (magic, BOM, version, file size, header
 *     size, block count, then one u32 offset per block);
 *   - the resource dictionary: a u8 revision, a u8 count, a u16 total
 *     size, a u16 that is always 8 and a u16 that is always
 *     8 + (count + 1) * 4, then (count + 1) four-byte patricia nodes,
 *     then a {u16 entry_size, u16 names_offset} sub-header, then
 *     count * entry_size record bytes, then count sixteen-byte names;
 *   - TEX0's 0x3C-byte header, of which two fields are confirmed: the
 *     texture data offset at +0x14 and the palette data offset at +0x24,
 *     with the texture region running between them;
 *   - the 8-byte texture record (u16 offset in 8-byte units, u16 holding
 *     the upper half of TEXIMAGE_PARAM, u8 width, u8, u16) and the
 *     4-byte palette record (u16 offset in 8-byte units, u16 flag);
 *   - the MDL0 shape dictionary, whose 4-byte records are offsets *from
 *     the dictionary's own start* to 16-byte descriptors
 *     {u16 itemTag, u16 size, u32 flag, u32 dlOffset, u32 dlSize} whose
 *     dlOffset is instead relative to the byte after the dictionary --
 *     two different bases in one structure, which is the part of this
 *     format that is easiest to get wrong;
 *   - the render-command bytecode: byte pairs (id, opcode) with opcode 4
 *     binding a material and opcode 5 drawing a shape with the material
 *     last bound.
 *
 * Two things are located by a self-consistency scan rather than by a
 * cracked pointer, and both gaps are aletheia's rather than this
 * engine's: which sub-dictionaries a model's raw data holds (found by
 * the dictionary signature above, because a model header's own offset
 * fields do not resolve under one consistent base), and which
 * dictionary is the material dictionary (scored by how many of its
 * names, with any trailing _<digits> stripped, are real texture names in
 * a/0/1/4).  Both are cross-checks against real data rather than
 * guesses, and both are documented in docs/ASSETS.md.
 */

#include <stdlib.h>
#include <string.h>

#include "g3d.h"

/* ------------------------------------------------------------------ */
/* Resource dictionaries                                               */
/* ------------------------------------------------------------------ */

int g3d_dict_parse(G3dResDict *out, const u8 *buf, u32 size, u32 off)
{
    u32 rev, count, dsize, unk4, unk6, sub, names_off;

    memset(out, 0, sizeof(*out));
    if (off + 8u > size) {
        return -1;
    }
    rev   = buf[off];
    count = buf[off + 1];
    dsize = bw_rd16(buf + off + 2);
    unk4  = bw_rd16(buf + off + 4);
    unk6  = bw_rd16(buf + off + 6);
    if (rev != 0 || unk4 != 8u || unk6 != 8u + (count + 1u) * 4u) {
        return -1;
    }
    if (dsize < 8u + count * 16u || off + dsize > size) {
        return -1;
    }
    names_off = off + dsize - count * 16u;

    out->base  = buf + off;
    out->size  = dsize;
    out->count = count;
    out->names = buf + names_off;

    sub = off + 8u + (count + 1u) * 4u;
    if (count == 0 || sub + 4u > names_off) {
        out->entry_size = 0;
        out->records = NULL;
        return 0;
    }
    out->entry_size = bw_rd16(buf + sub);
    out->records = buf + sub + 4u;
    if (out->entry_size == 0 ||
        sub + 4u + count * out->entry_size > names_off) {
        out->entry_size = 0;
        out->records = NULL;
    }
    return 0;
}

void g3d_dict_name(const G3dResDict *d, u32 i, char out[17])
{
    u32 j;

    out[0] = '\0';
    if (i >= d->count) {
        return;
    }
    for (j = 0; j < 16u; j++) {
        char c = (char)d->names[i * 16u + j];
        out[j] = c;
        if (c == '\0') {
            return;
        }
    }
    out[16] = '\0';
}

const u8 *g3d_dict_record(const G3dResDict *d, u32 i)
{
    if (d->records == NULL || i >= d->count) {
        return NULL;
    }
    return d->records + i * d->entry_size;
}

/* Every name is non-empty printable ASCII: the signature that makes the
 * dictionary scan specific enough to use on undivided model data. */
static int dict_names_plausible(const G3dResDict *d)
{
    u32 i, j;

    for (i = 0; i < d->count; i++) {
        const u8 *p = d->names + i * 16u;
        if (p[0] == 0) {
            return 0;
        }
        for (j = 0; j < 16u && p[j] != 0; j++) {
            if (p[j] < 32 || p[j] >= 127) {
                return 0;
            }
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The outer container                                                 */
/* ------------------------------------------------------------------ */

const u8 *g3d_container_block(const u8 *data, u32 size, const char magic[4],
                              u32 *block_size)
{
    u32 header_size, nblk, i, file_size;

    if (size < 0x10u) {
        return NULL;
    }
    file_size   = bw_rd32(data + 8);
    header_size = bw_rd16(data + 0x0C);
    nblk        = bw_rd16(data + 0x0E);
    if (file_size > size) {
        file_size = size;
    }
    if (header_size + nblk * 4u > size || nblk == 0 || nblk > 8u) {
        return NULL;
    }
    for (i = 0; i < nblk; i++) {
        u32 off = bw_rd32(data + header_size + i * 4u);
        if (off + 8u > file_size) {
            continue;
        }
        if (memcmp(data + off, magic, 4) == 0) {
            u32 bsize = bw_rd32(data + off + 4);
            if (bsize == 0 || off + bsize > file_size) {
                bsize = file_size - off;
            }
            if (block_size != NULL) {
                *block_size = bsize;
            }
            return data + off;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* TEX0                                                                */
/* ------------------------------------------------------------------ */

#define TEX0_HEADER_SIZE 0x3Cu

int g3d_tex0_parse(G3dTex0 *out, const u8 *block, u32 size)
{
    u32 bsize, tex_off, pal_off, pal_dict_off;

    memset(out, 0, sizeof(*out));
    if (size < TEX0_HEADER_SIZE || memcmp(block, "TEX0", 4) != 0) {
        return -1;
    }
    bsize = bw_rd32(block + 4);
    if (bsize == 0 || bsize > size) {
        bsize = size;
    }
    tex_off = bw_rd32(block + 0x14);
    pal_off = bw_rd32(block + 0x24);
    if (tex_off >= bsize || pal_off > bsize || pal_off < tex_off) {
        return -1;
    }
    if (g3d_dict_parse(&out->tex_dict, block, bsize, TEX0_HEADER_SIZE) != 0) {
        return -1;
    }
    pal_dict_off = TEX0_HEADER_SIZE + out->tex_dict.size;
    if (g3d_dict_parse(&out->pal_dict, block, bsize, pal_dict_off) != 0) {
        return -1;
    }
    out->base = block;
    out->size = bsize;
    out->texel = block + tex_off;
    out->texel_bytes = pal_off - tex_off;
    out->pal = block + pal_off;
    out->pal_bytes = bsize - pal_off;
    return 0;
}

int g3d_tex0_texture(const G3dTex0 *t, u32 i, G3dTexRecord *out)
{
    const u8 *rec = g3d_dict_record(&t->tex_dict, i);
    u32 params;

    memset(out, 0, sizeof(*out));
    if (rec == NULL || t->tex_dict.entry_size != 8u) {
        return -1;
    }
    g3d_dict_name(&t->tex_dict, i, out->name);
    out->offset = (u32)bw_rd16(rec) * 8u;
    params = bw_rd16(rec + 2);
    out->params = params;
    out->width  = 8u << ((params >> 4) & 7u);
    out->height = 8u << ((params >> 7) & 7u);
    out->format = (params >> 10) & 7u;
    out->color0 = (params >> 13) & 1u;
    return 0;
}

int g3d_tex0_palette(const G3dTex0 *t, u32 i, G3dPalRecord *out)
{
    const u8 *rec = g3d_dict_record(&t->pal_dict, i);

    memset(out, 0, sizeof(*out));
    if (rec == NULL || t->pal_dict.entry_size != 4u) {
        return -1;
    }
    g3d_dict_name(&t->pal_dict, i, out->name);
    out->offset = (u32)bw_rd16(rec) * 8u;
    return 0;
}

/* How many colours a format's index range can reach: also how many
 * halfwords of the palette region a texture of that format needs, which
 * is how the palette's own length is recovered.  The record does not
 * carry a length, and the gap to the next distinct offset is not a
 * boundary either (real dictionaries have two names at one offset). */
static u32 format_colors(u32 format)
{
    switch (format) {
    case G3D_TEXFMT_A3I5:   return 32u;
    case G3D_TEXFMT_PAL4:   return 4u;
    case G3D_TEXFMT_PAL16:  return 16u;
    case G3D_TEXFMT_PAL256: return 256u;
    case G3D_TEXFMT_A5I3:   return 8u;
    default:                return 256u;
    }
}

static u32 count_valid(const G3dTexture *t)
{
    u32 i, valid = 0;

    for (i = 0; i < t->width * t->height; i++) {
        /* The decoder paints an out-of-range index magenta at full
         * alpha; that exact colour is the signal a candidate palette is
         * too short for this texture's indices. */
        if (!(t->rgba[i * 4] == 255 && t->rgba[i * 4 + 1] == 0 &&
              t->rgba[i * 4 + 2] == 255 && t->rgba[i * 4 + 3] == 255)) {
            valid++;
        }
    }
    return valid;
}

int g3d_tex0_decode(const G3dTex0 *t, u32 i, G3dTexture *out)
{
    G3dTexRecord tr;
    G3dPalRecord pr;
    u32 bytes, need, j;
    char want[20];
    int best_j = -1;
    u32 best_valid = 0, best_fit = 0;

    memset(out, 0, sizeof(*out));
    if (g3d_tex0_texture(t, i, &tr) != 0) {
        return -1;
    }
    bytes = g3d_texture_byte_size(tr.format, tr.width, tr.height);
    if (tr.offset + bytes > t->texel_bytes) {
        return -1;
    }
    if (tr.format == G3D_TEXFMT_DIRECT) {
        return g3d_texture_decode(out, tr.format, tr.width, tr.height,
                                  t->texel + tr.offset, bytes, NULL, 0, 0);
    }
    need = format_colors(tr.format) * 2u;

    /* 1. the "<name>_pl" convention, a real link where it exists. */
    if (strlen(tr.name) + 3u < sizeof(want)) {
        strcpy(want, tr.name);
        strcat(want, "_pl");
        for (j = 0; j < t->pal_dict.count; j++) {
            if (g3d_tex0_palette(t, j, &pr) == 0 &&
                strcmp(pr.name, want) == 0 && pr.offset < t->pal_bytes) {
                u32 avail = t->pal_bytes - pr.offset;
                return g3d_texture_decode(out, tr.format, tr.width, tr.height,
                                          t->texel + tr.offset, bytes,
                                          t->pal + pr.offset,
                                          avail < need ? avail : need,
                                          (int)tr.color0);
            }
        }
    }

    /* 2. the palette whose indices all come out in range, tie-broken by
     *    how closely the format's index range matches it.  This is the
     *    stand-in for a material record nothing in this project has
     *    cracked; it is validated per candidate, not guessed. */
    for (j = 0; j < t->pal_dict.count; j++) {
        G3dTexture cand;
        u32 avail, use, valid, fit;

        if (g3d_tex0_palette(t, j, &pr) != 0 || pr.offset >= t->pal_bytes) {
            continue;
        }
        avail = t->pal_bytes - pr.offset;
        use = avail < need ? avail : need;
        if (g3d_texture_decode(&cand, tr.format, tr.width, tr.height,
                               t->texel + tr.offset, bytes,
                               t->pal + pr.offset, use, (int)tr.color0) != 0) {
            continue;
        }
        valid = count_valid(&cand);
        fit = (use / 2u > format_colors(tr.format))
                  ? use / 2u - format_colors(tr.format)
                  : format_colors(tr.format) - use / 2u;
        g3d_texture_free(&cand);
        if (best_j < 0 || valid > best_valid ||
            (valid == best_valid && fit < best_fit)) {
            best_j = (int)j;
            best_valid = valid;
            best_fit = fit;
        }
    }
    if (best_j < 0) {
        return g3d_texture_decode(out, tr.format, tr.width, tr.height,
                                  t->texel + tr.offset, bytes, NULL, 0,
                                  (int)tr.color0);
    }
    (void)g3d_tex0_palette(t, (u32)best_j, &pr);
    {
        u32 avail = t->pal_bytes - pr.offset;
        return g3d_texture_decode(out, tr.format, tr.width, tr.height,
                                  t->texel + tr.offset, bytes,
                                  t->pal + pr.offset,
                                  avail < need ? avail : need,
                                  (int)tr.color0);
    }
}

/* ------------------------------------------------------------------ */
/* The a/0/1/4 texture archive                                         */
/* ------------------------------------------------------------------ */

typedef struct TexName {
    char name[17];
    u32  subfile;
} TexName;

typedef struct TexCache {
    char name[17];
    G3dTexture tex;
} TexCache;

struct G3dTexArchive {
    BwArcFile arc;
    int       open;
    TexName  *names; u32 n_names, cap_names;
    TexCache *cache; u32 n_cache, cap_cache;
};

static int texarchive_add(G3dTexArchive *ar, const char *name, u32 subfile)
{
    u32 i;

    for (i = 0; i < ar->n_names; i++) {
        if (strcmp(ar->names[i].name, name) == 0) {
            return 0;   /* first one wins, as the asset lane's index does */
        }
    }
    if (ar->n_names == ar->cap_names) {
        u32 cap = ar->cap_names ? ar->cap_names * 2u : 256u;
        TexName *p = (TexName *)realloc(ar->names, cap * sizeof(*p));
        if (p == NULL) {
            return -1;
        }
        ar->names = p;
        ar->cap_names = cap;
    }
    memset(&ar->names[ar->n_names], 0, sizeof(TexName));
    strncpy(ar->names[ar->n_names].name, name, 16);
    ar->names[ar->n_names].subfile = subfile;
    ar->n_names++;
    return 0;
}

G3dTexArchive *g3d_texarchive_open(const char *rel_path)
{
    G3dTexArchive *ar = (G3dTexArchive *)calloc(1, sizeof(*ar));
    u32 i;
    u8 *buf = NULL;
    u32 buf_cap = 0;

    if (ar == NULL) {
        return NULL;
    }
    if (bw_arc_open(&ar->arc, rel_path) != 0) {
        free(ar);
        return NULL;
    }
    ar->open = 1;

    for (i = 0; i < ar->arc.count; i++) {
        long size = bw_arc_member_size(&ar->arc, i);
        const u8 *block;
        u32 bsize, j;
        G3dTex0 tex0;

        if (size <= 16) {
            continue;
        }
        if ((u32)size > buf_cap) {
            u8 *p = (u8 *)realloc(buf, (u32)size);
            if (p == NULL) {
                break;
            }
            buf = p;
            buf_cap = (u32)size;
        }
        if (bw_arc_read_member(&ar->arc, i, buf, buf_cap) != size) {
            continue;
        }
        if (memcmp(buf, "BTX0", 4) != 0 && memcmp(buf, "BMD0", 4) != 0) {
            continue;
        }
        block = g3d_container_block(buf, (u32)size, "TEX0", &bsize);
        if (block == NULL || g3d_tex0_parse(&tex0, block, bsize) != 0) {
            continue;
        }
        for (j = 0; j < tex0.tex_dict.count; j++) {
            char name[17];
            g3d_dict_name(&tex0.tex_dict, j, name);
            if (name[0] != '\0') {
                (void)texarchive_add(ar, name, i);
            }
        }
    }
    free(buf);
    return ar;
}

void g3d_texarchive_close(G3dTexArchive *ar)
{
    u32 i;

    if (ar == NULL) {
        return;
    }
    for (i = 0; i < ar->n_cache; i++) {
        g3d_texture_free(&ar->cache[i].tex);
    }
    free(ar->cache);
    free(ar->names);
    if (ar->open) {
        bw_arc_close(&ar->arc);
    }
    free(ar);
}

u32 g3d_texarchive_names(const G3dTexArchive *ar)
{
    return ar == NULL ? 0u : ar->n_names;
}

static int texarchive_find(const G3dTexArchive *ar, const char *name,
                           u32 *subfile)
{
    u32 i;

    for (i = 0; i < ar->n_names; i++) {
        if (strcmp(ar->names[i].name, name) == 0) {
            *subfile = ar->names[i].subfile;
            return 0;
        }
    }
    return -1;
}

const G3dTexture *g3d_texarchive_get(G3dTexArchive *ar, const char *name)
{
    u32 subfile, i, size32;
    long size;
    u8 *buf;
    const u8 *block;
    u32 bsize;
    G3dTex0 tex0;
    const G3dTexture *found = NULL;

    if (ar == NULL || name == NULL || name[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < ar->n_cache; i++) {
        if (strcmp(ar->cache[i].name, name) == 0) {
            return ar->cache[i].tex.rgba != NULL ? &ar->cache[i].tex : NULL;
        }
    }
    if (texarchive_find(ar, name, &subfile) != 0) {
        return NULL;
    }
    size = bw_arc_member_size(&ar->arc, subfile);
    if (size <= 16) {
        return NULL;
    }
    size32 = (u32)size;
    buf = (u8 *)malloc(size32);
    if (buf == NULL) {
        return NULL;
    }
    if (bw_arc_read_member(&ar->arc, subfile, buf, size32) != size ||
        (block = g3d_container_block(buf, size32, "TEX0", &bsize)) == NULL ||
        g3d_tex0_parse(&tex0, block, bsize) != 0) {
        free(buf);
        return NULL;
    }

    /* Decode every texture of the sub-file into the cache: a map model
     * reuses several textures out of the same one, and the palette
     * resolution above is the expensive part. */
    for (i = 0; i < tex0.tex_dict.count; i++) {
        char tname[17];
        G3dTexture tex;
        u32 k;
        int known = 0;

        g3d_dict_name(&tex0.tex_dict, i, tname);
        for (k = 0; k < ar->n_cache; k++) {
            if (strcmp(ar->cache[k].name, tname) == 0) {
                known = 1;
                break;
            }
        }
        if (known) {
            continue;
        }
        if (g3d_tex0_decode(&tex0, i, &tex) != 0) {
            memset(&tex, 0, sizeof(tex));
        }
        if (ar->n_cache == ar->cap_cache) {
            u32 cap = ar->cap_cache ? ar->cap_cache * 2u : 64u;
            TexCache *p = (TexCache *)realloc(ar->cache, cap * sizeof(*p));
            if (p == NULL) {
                g3d_texture_free(&tex);
                break;
            }
            ar->cache = p;
            ar->cap_cache = cap;
        }
        memset(&ar->cache[ar->n_cache], 0, sizeof(TexCache));
        strncpy(ar->cache[ar->n_cache].name, tname, 16);
        ar->cache[ar->n_cache].tex = tex;
        ar->n_cache++;
    }
    free(buf);

    for (i = 0; i < ar->n_cache; i++) {
        if (strcmp(ar->cache[i].name, name) == 0) {
            found = ar->cache[i].tex.rgba != NULL ? &ar->cache[i].tex : NULL;
            break;
        }
    }
    return found;
}

/* ------------------------------------------------------------------ */
/* MDL0: the dictionary scan, the shape dictionary, the render commands */
/* ------------------------------------------------------------------ */

#define G3D_MAX_DICTS 64u

static u32 find_resdicts(const u8 *buf, u32 size, u32 *offs, u32 max)
{
    u32 off = 0, n = 0;

    while (off + 8u <= size && n < max) {
        G3dResDict d;
        u32 count = buf[off + 1];

        if (buf[off] == 0 && count >= 1u && count <= 200u &&
            g3d_dict_parse(&d, buf, size, off) == 0 &&
            dict_names_plausible(&d)) {
            offs[n++] = off;
            off += d.size > 8u ? d.size : 8u;
            continue;
        }
        off += 4u;   /* dictionary headers are only ever 4-byte aligned */
    }
    return n;
}

#define SHAPE_DESC_SIZE 16u

static int shape_dict_at(const u8 *buf, u32 size, u32 off, G3dResDict *out)
{
    G3dResDict d;
    u32 dl_base, i, ok = 0;

    if (g3d_dict_parse(&d, buf, size, off) != 0 || d.count == 0 ||
        d.entry_size != 4u || d.records == NULL) {
        return -1;
    }
    dl_base = off + d.size;
    for (i = 0; i < d.count; i++) {
        u32 v = bw_rd32(d.records + i * 4u);
        u32 addr = off + v;
        u32 tag, dsize, dl_off, dl_size;

        if (addr + SHAPE_DESC_SIZE > size) {
            continue;
        }
        tag     = bw_rd16(buf + addr);
        dsize   = bw_rd16(buf + addr + 2);
        dl_off  = bw_rd32(buf + addr + 8);
        dl_size = bw_rd32(buf + addr + 12);
        if (tag == 0 && dsize == 0x10u &&
            dl_base + dl_off + dl_size <= size) {
            ok++;
        }
    }
    if (ok != d.count) {
        return -1;
    }
    *out = d;
    return 0;
}

/*
 * The render-command bytecode: the longest run of (id, opcode) byte pairs
 * in which every opcode is 4 (bind material id) or 5 (draw shape id with
 * the material last bound).  Fills `shape_to_mat` (indexed by shape id,
 * 0xFF meaning "not named by the bytecode").
 */
static u32 find_render_commands(const u8 *buf, u32 size, u8 *shape_to_mat,
                                u32 max_shapes)
{
    u32 i = 0, best_pairs = 0, best_off = 0;

    while (i + 1u < size) {
        u32 j = i, pairs = 0;

        while (j + 1u < size && (buf[j + 1] == 4u || buf[j + 1] == 5u)) {
            pairs++;
            j += 2u;
        }
        if (pairs > best_pairs) {
            best_pairs = pairs;
            best_off = i;
        }
        i = pairs ? j : i + 1u;
    }
    memset(shape_to_mat, 0xFF, max_shapes);
    if (best_pairs < 8u) {
        return 0;
    }
    {
        u32 j = best_off, cur = 0xFFu, k;

        for (k = 0; k < best_pairs; k++) {
            u8 v = buf[j], op = buf[j + 1];
            if (op == 4u) {
                cur = v;
            } else if (v < max_shapes) {
                shape_to_mat[v] = (u8)cur;
            }
            j += 2u;
        }
    }
    return best_pairs;
}

/* "pk_l_mado_4" -> "pk_l_mado"; a material name is the texture's own,
 * with a trailing _<digits> when the model uses a second variant of it.
 * Which of the two spellings a first use takes is model-specific, so
 * both are tried against the archive. */
static void bare_texture_name(const char *in, char out[17])
{
    size_t len = strlen(in), i = len;

    strncpy(out, in, 16);
    out[16] = '\0';
    while (i > 0 && in[i - 1] >= '0' && in[i - 1] <= '9') {
        i--;
    }
    if (i > 1u && i < len && in[i - 1] == '_') {
        out[i - 1] = '\0';
    }
}

static int material_dict_of(const u8 *buf, u32 size, const u32 *dict_offs,
                            u32 n_dicts, u32 shape_off, u32 max_mat,
                            const G3dTexArchive *ar, G3dResDict *out)
{
    u32 i, j;
    int best = -1;
    u32 best_score = 0;

    for (i = 0; i < n_dicts; i++) {
        G3dResDict d;
        u32 score = 0, all_pl = 1;

        if (dict_offs[i] == shape_off) {
            continue;
        }
        if (g3d_dict_parse(&d, buf, size, dict_offs[i]) != 0 || d.count == 0) {
            continue;
        }
        if (d.count <= max_mat) {
            continue;   /* too small to be indexed by the highest id used */
        }
        for (j = 0; j < d.count; j++) {
            char name[17], bare[17];
            u32 sub;

            g3d_dict_name(&d, j, name);
            {
                size_t l = strlen(name);
                if (l < 3u || strcmp(name + l - 3u, "_pl") != 0) {
                    all_pl = 0;
                }
            }
            if (ar == NULL) {
                continue;
            }
            bare_texture_name(name, bare);
            if (texarchive_find(ar, name, &sub) == 0 ||
                texarchive_find(ar, bare, &sub) == 0) {
                score++;
            }
        }
        if (all_pl) {
            continue;   /* a palette-name dictionary */
        }
        if (ar == NULL) {
            /* Without the archive to score against, the largest
             * non-palette dictionary that covers every material id the
             * bytecode uses is the best available answer, and the test
             * that needs a real answer passes the archive. */
            score = d.count;
        }
        if (best < 0 || score > best_score) {
            best = (int)i;
            best_score = score;
            *out = d;
        }
    }
    return best < 0 ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* Loading one map model                                               */
/* ------------------------------------------------------------------ */

void g3d_model_free(G3dModel *m)
{
    if (m == NULL) {
        return;
    }
    g3d_geom_free(&m->geom);
    free(m->shapes);
    free(m->raw);
    memset(m, 0, sizeof(*m));
}

int g3d_model_load(G3dModel *out, const char *rel_path, u32 index,
                   const G3dTexArchive *ar)
{
    BwArcFile arc;
    long size;
    u8 *raw = NULL;
    u32 raw_size, i, bmd_off;
    const u8 *mdl_block;
    u32 mdl_bsize;
    G3dResDict model_dict, shape_rd, mat_rd;
    const u8 *tail;
    u32 tail_size;
    u32 dict_offs[G3D_MAX_DICTS], n_dicts;
    int shape_found = -1;
    u32 shape_off = 0, dl_base;
    u8 shape_to_mat[256];
    u32 max_mat = 0;
    int have_mat = 0;

    memset(out, 0, sizeof(*out));
    g3d_geom_init(&out->geom);

    if (bw_arc_open(&arc, rel_path) != 0) {
        return -1;
    }
    size = bw_arc_member_size(&arc, index);
    if (size <= 32) {
        bw_arc_close(&arc);
        return -1;
    }
    raw_size = (u32)size;
    raw = (u8 *)malloc(raw_size);
    if (raw == NULL || bw_arc_read_member(&arc, index, raw, raw_size) != size) {
        free(raw);
        bw_arc_close(&arc);
        return -1;
    }
    bw_arc_close(&arc);

    /* An a/0/0/8 sub-file is header || BMD0 || per-tile grid || tail. */
    bmd_off = raw_size;
    for (i = 0; i + 4u <= raw_size; i++) {
        if (memcmp(raw + i, "BMD0", 4) == 0) {
            bmd_off = i;
            break;
        }
    }
    if (bmd_off >= raw_size) {
        free(raw);
        return -1;
    }
    out->raw = raw;
    out->raw_size = raw_size;

    mdl_block = g3d_container_block(raw + bmd_off, raw_size - bmd_off,
                                    "MDL0", &mdl_bsize);
    if (mdl_block == NULL ||
        g3d_dict_parse(&model_dict, mdl_block, mdl_bsize, 8u) != 0) {
        g3d_model_free(out);
        return -1;
    }
    g3d_dict_name(&model_dict, 0, out->name);

    tail = mdl_block + 8u + model_dict.size;
    tail_size = mdl_bsize - (8u + model_dict.size);
    out->mdl = tail;
    out->mdl_size = tail_size;

    n_dicts = find_resdicts(tail, tail_size, dict_offs, G3D_MAX_DICTS);
    for (i = 0; i < n_dicts; i++) {
        G3dResDict d;
        if (shape_dict_at(tail, tail_size, dict_offs[i], &d) == 0) {
            if (shape_found < 0 || d.count > shape_rd.count) {
                shape_rd = d;
                shape_off = dict_offs[i];
                shape_found = (int)i;
            }
        }
    }
    if (shape_found < 0) {
        g3d_model_free(out);
        return -1;
    }
    dl_base = shape_off + shape_rd.size;

    (void)find_render_commands(tail, tail_size, shape_to_mat,
                               (u32)sizeof(shape_to_mat));
    for (i = 0; i < shape_rd.count && i < sizeof(shape_to_mat); i++) {
        if (shape_to_mat[i] != 0xFFu && shape_to_mat[i] > max_mat) {
            max_mat = shape_to_mat[i];
        }
    }
    have_mat = material_dict_of(tail, tail_size, dict_offs, n_dicts,
                                shape_off, max_mat, ar, &mat_rd) == 0;

    out->shapes = (G3dShape *)calloc(shape_rd.count, sizeof(G3dShape));
    if (out->shapes == NULL) {
        g3d_model_free(out);
        return -1;
    }
    out->n_shapes = shape_rd.count;

    for (i = 0; i < shape_rd.count; i++) {
        G3dShape *sh = &out->shapes[i];
        u32 v = bw_rd32(shape_rd.records + i * 4u);
        u32 addr = shape_off + v;
        G3dGx gx;
        u32 start, soft, hard;

        sh->index = i;
        sh->material = 0xFFFFFFFFu;
        sh->first_poly = out->geom.n_polys;
        sh->first_vert = out->geom.n_verts;
        if (addr + SHAPE_DESC_SIZE > tail_size) {
            continue;
        }
        sh->dl_offset = bw_rd32(tail + addr + 8);
        sh->dl_size   = bw_rd32(tail + addr + 12);

        if (i < sizeof(shape_to_mat) && shape_to_mat[i] != 0xFFu) {
            sh->material = shape_to_mat[i];
            if (have_mat && sh->material < mat_rd.count) {
                char mname[17];
                g3d_dict_name(&mat_rd, sh->material, mname);
                bare_texture_name(mname, sh->texture);
            }
        }

        /* Each shape's declared range opens with a short non-command
         * preamble this project has not identified (aletheia's
         * docs/ASSETS.md records the same gap), so the stream's start is
         * located inside the shape's own bounded range, and the soft
         * limit lets a batch whose END_VTXS spills a few bytes past
         * dlSize close rather than dropping that batch. */
        if (dl_base + sh->dl_offset + sh->dl_size > tail_size) {
            continue;
        }
        start = g3d_gx_find_stream(tail, dl_base + sh->dl_offset + sh->dl_size,
                                   dl_base + sh->dl_offset, 4u);
        if (start >= dl_base + sh->dl_offset + sh->dl_size) {
            continue;
        }
        soft = dl_base + sh->dl_offset + sh->dl_size;
        hard = soft + 384u;
        if (hard > tail_size) {
            hard = tail_size;
        }
        g3d_gx_init(&gx, &out->geom);
        (void)g3d_gx_run(&gx, tail, tail_size, start, soft, hard);

        sh->n_polys = out->geom.n_polys - sh->first_poly;
        sh->n_verts = out->geom.n_verts - sh->first_vert;
    }
    return 0;
}
