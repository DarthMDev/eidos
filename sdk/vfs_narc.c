/*
 * sdk/vfs_narc.c -- member-level NARC overrides: the merged archive.
 *
 * Why this exists.  Every asset the game loads is a member of a NARC, and a
 * mod that adds one Pokemon or edits one map should not have to repack and
 * ship a whole archive (and then conflict with every other mod touching the
 * same archive).  So a mod may hold a *directory* at an archive's NitroFS
 * path, and each file in it named `<index>` or `<index>.<ext>` replaces that
 * member, or adds it past the archive's end (sdk/vfs.h has the whole model).
 *
 * Why a synthesised archive rather than a lookup on the read path.  The
 * readers that matter are matched code: func_02048E34, func_02049578 and the
 * rest read the NARC's own header, BTAF and GMIF through FS_SeekFile and
 * FS_ReadFile and do the arithmetic themselves, and the BwArcFile fields
 * (imageBase, fatOffset, count) they trust come from the same bytes.  The
 * one place that can make all of them agree without touching any of them is
 * the bytes: this file builds the NARC the mods describe, laid out exactly
 * the way the cartridge's archives are, and the FS shim (sdk/fs.c) serves it
 * as a memory file whenever the archive's path is opened, by path, by file
 * id, by ROM range or through the archive id table.  Nothing above the FS
 * shim can tell a merged archive from a shipped one.
 *
 * Merged images are cached per path, with a reference count held by the
 * open files reading them (the FS record stores the cache *index*, never a
 * pointer, per the 0x48-byte FSFile rule), and dropped on any change to the
 * roots or the mod list.
 *
 * Framing only: no member's contents are interpreted, and no game bytes are
 * in this file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "vfs.h"
#include "plat_dir.h"

#define FOURCC(a, b, c, d) \
    ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

#define NARC_MAGIC FOURCC('N', 'A', 'R', 'C')
#define BTAF_MAGIC FOURCC('B', 'T', 'A', 'F')
#define BTNF_MAGIC FOURCC('B', 'T', 'N', 'F')
#define GMIF_MAGIC FOURCC('G', 'M', 'I', 'F')

#define NARC_HEADER_SIZE 0x10u
#define NARC_ALIGN       4u
#define NARC_PAD         0xFFu

static void put16(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static void put32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

/* ------------------------------------------------------------------ */
/* Packing                                                             */
/* ------------------------------------------------------------------ */

/* The nameless BTNF every archive of this game but one carries. */
static const u8 MINIMAL_BTNF[16] = {
    'B', 'T', 'N', 'F', 0x10, 0, 0, 0,  /* magic, size                    */
    4, 0, 0, 0,                         /* root: sub-table offset         */
    0, 0, 1, 0                          /* first file id 0, one directory */
};

int bw_narc_pack(const u8 *const *data, const u32 *sizes, u32 count,
                 const u8 *btnf, u32 btnf_size, u8 **out, u32 *out_size)
{
    u64 gmif_data = 0;
    u64 total;
    u32 btaf_size;
    u32 i;
    u32 at;
    u32 pos;
    u8 *img;

    *out = NULL;
    *out_size = 0;
    if (count > BW_NARC_MAX_MEMBERS || (count != 0 && sizes == NULL)) {
        return -1;
    }
    if (btnf == NULL) {
        btnf = MINIMAL_BTNF;
        btnf_size = sizeof(MINIMAL_BTNF);
    }
    if (btnf_size < 8 || (btnf_size % NARC_ALIGN) != 0 ||
        bw_rd32(btnf) != BTNF_MAGIC || bw_rd32(btnf + 4) != btnf_size) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (sizes[i] != 0 && (data == NULL || data[i] == NULL)) {
            return -1;
        }
        gmif_data += ((u64)sizes[i] + NARC_ALIGN - 1) & ~(u64)(NARC_ALIGN - 1);
    }
    btaf_size = 12u + 8u * count;
    total = (u64)NARC_HEADER_SIZE + btaf_size + btnf_size + 8u + gmif_data;
    if (total > 0xFFFFFFFFu) {
        return -1;
    }
    img = (u8 *)malloc((size_t)total);
    if (img == NULL) {
        return -1;
    }

    put32(img + 0x00, NARC_MAGIC);
    put16(img + 0x04, 0xFFFEu);           /* byte order mark          */
    put16(img + 0x06, 0x0100u);           /* version                  */
    put32(img + 0x08, (u32)total);
    put16(img + 0x0C, NARC_HEADER_SIZE);
    put16(img + 0x0E, 3);                 /* chunk count              */

    at = NARC_HEADER_SIZE;
    put32(img + at + 0, BTAF_MAGIC);
    put32(img + at + 4, btaf_size);
    put16(img + at + 8, count);
    put16(img + at + 10, 0);
    pos = 0;
    for (i = 0; i < count; i++) {
        put32(img + at + 12 + i * 8, pos);
        put32(img + at + 16 + i * 8, pos + sizes[i]);
        pos += (sizes[i] + NARC_ALIGN - 1) & ~(NARC_ALIGN - 1);
    }
    at += btaf_size;

    memcpy(img + at, btnf, btnf_size);
    at += btnf_size;

    put32(img + at + 0, GMIF_MAGIC);
    put32(img + at + 4, (u32)(8u + gmif_data));
    at += 8;
    for (i = 0; i < count; i++) {
        u32 padded = (sizes[i] + NARC_ALIGN - 1) & ~(NARC_ALIGN - 1);

        if (sizes[i] != 0) {
            memcpy(img + at, data[i], sizes[i]);
        }
        memset(img + at + sizes[i], NARC_PAD, padded - sizes[i]);
        at += padded;
    }

    *out = img;
    *out_size = (u32)total;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Reading the base archive                                            */
/* ------------------------------------------------------------------ */

typedef struct Narc {
    const u8 *img;
    u32 size;
    u32 count;
    u32 fat;        /* offset of BTAF                     */
    u32 btnf;       /* offset of BTNF, or 0 when absent   */
    u32 btnf_size;
    u32 gmif;       /* offset of GMIF                     */
    u32 gmif_size;
} Narc;

static int parse_narc(const u8 *img, u32 size, Narc *n)
{
    u32 pos;
    int have_fat = 0;
    int have_img = 0;
    u32 i;

    memset(n, 0, sizeof(*n));
    if (size < NARC_HEADER_SIZE || bw_rd32(img) != NARC_MAGIC) {
        return -1;
    }
    n->img = img;
    n->size = size;
    pos = bw_rd16(img + 12);
    while (pos <= size && size - pos >= 8) {
        u32 magic = bw_rd32(img + pos);
        u32 csize = bw_rd32(img + pos + 4);

        if (csize < 8 || csize > size - pos) {
            break;
        }
        if (magic == BTAF_MAGIC && csize >= 12) {
            n->fat = pos;
            n->count = bw_rd16(img + pos + 8);
            if ((u64)12 + (u64)8 * n->count > csize) {
                return -1;
            }
            have_fat = 1;
        } else if (magic == BTNF_MAGIC) {
            n->btnf = pos;
            n->btnf_size = csize;
        } else if (magic == GMIF_MAGIC) {
            n->gmif = pos;
            n->gmif_size = csize;
            have_img = 1;
        }
        pos += csize;
    }
    if (!have_fat || !have_img) {
        return -1;
    }
    for (i = 0; i < n->count; i++) {
        u32 start = bw_rd32(img + n->fat + 12 + i * 8);
        u32 end = bw_rd32(img + n->fat + 16 + i * 8);

        if (end < start || end > n->gmif_size - 8) {
            return -1;
        }
    }
    return 0;
}

static u8 *read_whole(const char *path, u32 *size_out)
{
    FILE *fp = fopen(path, "rb");
    long end;
    u8 *buf;

    *size_out = 0;
    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (end = ftell(fp)) < 0 ||
        (unsigned long)end > 0xFFFFFFFFul || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = (u8 *)malloc(end == 0 ? 1 : (size_t)end);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    if (end != 0 && fread(buf, 1, (size_t)end, fp) != (size_t)end) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *size_out = (u32)end;
    return buf;
}

/* ------------------------------------------------------------------ */
/* Collecting the mods' members                                        */
/* ------------------------------------------------------------------ */

typedef struct Override {
    u32   index;
    u32   mod;      /* position in the load order */
    char *path;
} Override;

typedef struct Collect {
    Override   *list;
    u32         count;
    u32         cap;
    u32         mod;
    const char *dir;
    int         error;
} Collect;

/*
 * `<digits>` or `<digits>.<ext>` with no further dot: 12, 0012, 12.bin,
 * 12.nclr.  Anything else (12.bin.bak, readme.txt, .DS_Store) is not a
 * member and is ignored.  Returns 1 for a member name, 0 for another name,
 * -1 for a member name whose index the format cannot hold.
 */
static int member_index(const char *name, u32 *index)
{
    const char *p = name;
    u32 v = 0;

    if (*p < '0' || *p > '9') {
        return 0;
    }
    while (*p >= '0' && *p <= '9') {
        if (v < BW_NARC_MAX_MEMBERS) {
            v = v * 10u + (u32)(*p - '0');
        }
        p++;
    }
    if (*p == '.') {
        if (p[1] == '\0' || strchr(p + 1, '.') != NULL) {
            return 0;
        }
    } else if (*p != '\0') {
        return 0;
    }
    if (v >= BW_NARC_MAX_MEMBERS) {
        return -1;
    }
    *index = v;
    return 1;
}

static int collect_one(void *ctx, const char *name)
{
    Collect *c = (Collect *)ctx;
    u32 index;
    int kind = member_index(name, &index);
    size_t need;
    char *path;

    if (kind == 0) {
        return 0;
    }
    if (kind < 0) {
        fprintf(stderr, "vfs: %s/%s: member index past the NARC limit of %u\n",
                c->dir, name, BW_NARC_MAX_MEMBERS);
        c->error = 1;
        return 1;
    }
    need = strlen(c->dir) + 1 + strlen(name) + 1;
    path = (char *)malloc(need);
    if (path == NULL) {
        c->error = 1;
        return 1;
    }
    sprintf(path, "%s/%s", c->dir, name);
    if (bw_plat_path_kind(path) != BW_PATH_FILE) {
        free(path);      /* a subdirectory named like a member: not one */
        return 0;
    }
    if (c->count == c->cap) {
        u32 cap = c->cap == 0 ? 16 : c->cap * 2;
        Override *grown = (Override *)realloc(c->list, cap * sizeof(*grown));

        if (grown == NULL) {
            free(path);
            c->error = 1;
            return 1;
        }
        c->list = grown;
        c->cap = cap;
    }
    c->list[c->count].index = index;
    c->list[c->count].mod = c->mod;
    c->list[c->count].path = path;
    c->count++;
    return 0;
}

static int cmp_override(const void *a, const void *b)
{
    const Override *x = (const Override *)a;
    const Override *y = (const Override *)b;

    if (x->index != y->index) {
        return x->index < y->index ? -1 : 1;
    }
    if (x->mod != y->mod) {
        return x->mod < y->mod ? -1 : 1;
    }
    return 0;
}

static void free_collect(Collect *c)
{
    u32 i;

    for (i = 0; i < c->count; i++) {
        free(c->list[i].path);
    }
    free(c->list);
    memset(c, 0, sizeof(*c));
}

/*
 * Gather the member files of every mod later than the file-granular winner
 * `layer`, then reduce them to one per index, the latest mod's.  Two files
 * for one index in the same mod is an error: neither is "later".
 * Returns 0 (c->count may be 0) or -1.
 */
static int collect(const char *rel, int layer, Collect *c)
{
    char dir[1200];
    u32 n = bw_mod_count();
    u32 i;
    u32 kept;

    memset(c, 0, sizeof(*c));
    for (i = (u32)(layer + 1); i < n; i++) {
        const char *root = bw_mod_root_at(i);

        if (strlen(root) + 1 + strlen(rel) + 1 > sizeof(dir)) {
            continue;
        }
        sprintf(dir, "%s/%s", root, rel);
        if (bw_plat_path_kind(dir) != BW_PATH_DIR) {
            continue;
        }
        c->mod = i;
        c->dir = dir;
        (void)bw_plat_list_dir(dir, collect_one, c);
        if (c->error) {
            free_collect(c);
            return -1;
        }
    }
    if (c->count == 0) {
        return 0;
    }
    qsort(c->list, c->count, sizeof(*c->list), cmp_override);
    kept = 0;
    for (i = 0; i < c->count; i++) {
        if (i + 1 < c->count && c->list[i + 1].index == c->list[i].index) {
            if (c->list[i + 1].mod == c->list[i].mod) {
                fprintf(stderr, "vfs: %s and %s name the same member of %s\n",
                        c->list[i].path, c->list[i + 1].path, rel);
                free_collect(c);
                return -1;
            }
            free(c->list[i].path);        /* a later mod has this index */
            continue;
        }
        c->list[kept++] = c->list[i];
    }
    c->count = kept;
    return 0;
}

int bw_vfs_member_overrides(const char *rel)
{
    Collect c;
    int layer = bw_vfs_file_layer(rel);
    int n;

    if (layer == BW_LAYER_NONE || bw_mod_count() == 0) {
        return 0;
    }
    if (collect(rel, layer, &c) != 0) {
        return -1;
    }
    n = (int)c.count;
    free_collect(&c);
    return n;
}

/* ------------------------------------------------------------------ */
/* Merging                                                             */
/* ------------------------------------------------------------------ */

int bw_vfs_build_merged(const char *rel, u8 **out, u32 *out_size)
{
    char base_path[1200];
    Collect c;
    Narc narc;
    int layer;
    u8 *base = NULL;
    u32 base_size;
    const u8 **data = NULL;
    u32 *sizes = NULL;
    u8 **owned = NULL;
    u32 count;
    u32 i;
    int rc = -1;

    *out = NULL;
    *out_size = 0;
    if (bw_mod_count() == 0) {
        return 0;
    }
    /*
     * Members apply on top of a file: with no layer holding one, a mod
     * directory at this path is just a directory (a/0/1 holds the archives
     * a/0/1/0..9, whose names look like member indices).
     */
    layer = bw_vfs_file_layer(rel);
    if (layer == BW_LAYER_NONE) {
        return 0;
    }
    if (collect(rel, layer, &c) != 0) {
        return -1;
    }
    if (c.count == 0) {
        return 0;
    }

    if (bw_data_path(base_path, sizeof(base_path), rel) != 0 ||
        (base = read_whole(base_path, &base_size)) == NULL) {
        fprintf(stderr, "vfs: %s: cannot read the archive the members apply to\n", rel);
        goto done;
    }
    if (parse_narc(base, base_size, &narc) != 0) {
        fprintf(stderr, "vfs: %s: member overrides given, but %s is not a NARC\n",
                rel, base_path);
        goto done;
    }

    count = narc.count;
    if (c.list[c.count - 1].index + 1 > count) {
        count = c.list[c.count - 1].index + 1;
    }
    data = (const u8 **)calloc(count == 0 ? 1 : count, sizeof(*data));
    sizes = (u32 *)calloc(count == 0 ? 1 : count, sizeof(*sizes));
    owned = (u8 **)calloc(c.count, sizeof(*owned));
    if (data == NULL || sizes == NULL || owned == NULL) {
        goto done;
    }
    for (i = 0; i < narc.count; i++) {
        u32 start = bw_rd32(base + narc.fat + 12 + i * 8);
        u32 end = bw_rd32(base + narc.fat + 16 + i * 8);

        data[i] = base + narc.gmif + 8 + start;
        sizes[i] = end - start;
    }
    /* Indices past the base count that no mod fills stay empty members. */
    for (i = 0; i < c.count; i++) {
        u32 sz;

        owned[i] = read_whole(c.list[i].path, &sz);
        if (owned[i] == NULL) {
            fprintf(stderr, "vfs: %s: cannot read member file\n", c.list[i].path);
            goto done;
        }
        data[c.list[i].index] = owned[i];
        sizes[c.list[i].index] = sz;
    }
    if (bw_narc_pack(data, sizes, count,
                     narc.btnf != 0 ? base + narc.btnf : NULL, narc.btnf_size,
                     out, out_size) != 0) {
        fprintf(stderr, "vfs: %s: the merged archive does not fit the NARC format\n", rel);
        goto done;
    }
    rc = 1;

done:
    if (owned != NULL) {
        for (i = 0; i < c.count; i++) {
            free(owned[i]);
        }
    }
    free(owned);
    free(sizes);
    free((void *)data);
    free(base);
    free_collect(&c);
    return rc;
}

/* ------------------------------------------------------------------ */
/* The cache the FS shim reads                                         */
/* ------------------------------------------------------------------ */

typedef struct Merged {
    char *rel;     /* NULL: a free entry                         */
    u8   *data;
    u32   size;
    u32   refs;    /* open files reading it                       */
    int   stale;   /* dropped by a flush, freed at the last close */
} Merged;

static Merged *g_merged;
static u32     g_merged_cap;

static void free_entry(Merged *m)
{
    free(m->rel);
    free(m->data);
    memset(m, 0, sizeof(*m));
}

void bw_vfs_flush(void)
{
    u32 i;

    for (i = 0; i < g_merged_cap; i++) {
        if (g_merged[i].rel == NULL) {
            continue;
        }
        if (g_merged[i].refs == 0) {
            free_entry(&g_merged[i]);
        } else {
            g_merged[i].stale = 1;
        }
    }
}

static int new_entry(u32 *index)
{
    u32 i;
    u32 cap;
    Merged *grown;

    for (i = 0; i < g_merged_cap; i++) {
        if (g_merged[i].rel == NULL) {
            *index = i;
            return 0;
        }
    }
    cap = g_merged_cap == 0 ? 8 : g_merged_cap * 2;
    grown = (Merged *)realloc(g_merged, cap * sizeof(*grown));
    if (grown == NULL) {
        return -1;
    }
    memset(grown + g_merged_cap, 0, (cap - g_merged_cap) * sizeof(*grown));
    *index = g_merged_cap;
    g_merged = grown;
    g_merged_cap = cap;
    return 0;
}

int bw_vfs_acquire(const char *rel, const u8 **data, u32 *size, u32 *handle)
{
    u32 i;
    u8 *img;
    u32 img_size;
    char *key;
    int rc;

    *data = NULL;
    *size = 0;
    *handle = 0;
    if (rel == NULL || bw_mod_count() == 0) {
        return 0;
    }
    for (i = 0; i < g_merged_cap; i++) {
        Merged *m = &g_merged[i];

        if (m->rel != NULL && !m->stale && strcmp(m->rel, rel) == 0) {
            m->refs++;
            *data = m->data;
            *size = m->size;
            *handle = i + 1;
            return 1;
        }
    }
    rc = bw_vfs_build_merged(rel, &img, &img_size);
    if (rc <= 0) {
        return rc;
    }
    key = (char *)malloc(strlen(rel) + 1);
    if (key == NULL || new_entry(&i) != 0) {
        free(key);
        free(img);
        return -1;
    }
    strcpy(key, rel);
    g_merged[i].rel = key;
    g_merged[i].data = img;
    g_merged[i].size = img_size;
    g_merged[i].refs = 1;
    g_merged[i].stale = 0;
    *data = img;
    *size = img_size;
    *handle = i + 1;
    return 1;
}

void bw_vfs_release(u32 handle)
{
    Merged *m;

    if (handle == 0 || handle > g_merged_cap) {
        return;
    }
    m = &g_merged[handle - 1];
    if (m->rel == NULL || m->refs == 0) {
        return;
    }
    m->refs--;
    if (m->refs == 0 && m->stale) {
        free_entry(m);
    }
}
