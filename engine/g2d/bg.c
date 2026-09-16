/*
 * engine/g2d/bg.c -- text-mode BG layers and the frame compositor.
 *
 * The NSCR (screen / tile map) parser, the per-layer rasteriser with
 * scroll and screen-size handling, and the compositor that stacks up to
 * four BG layers and an OBJ list by priority into one RGBA frame.
 *
 * Provenance is stated in g2d.h's NSCR section.  The two rules worth
 * repeating at the point where they are implemented:
 *
 *   - The screen-entry index for a given (tile x, tile y) is NOT row
 *     major for a 512-pixel-wide or 512-pixel-tall hardware map.
 *     src/matched/func_02040EE0.c is the game's own helper and lays out
 *     every size: 512x256 puts the second 32x32 map after the first, and
 *     512x512 lays four out in a 2x2 grid, 0x400 entries apart.  That is
 *     g2d_bg_screen_index below, transcribed from it.
 *
 *   - An NSCR resource on disk is NOT in that order.  Its entry array is
 *     row major at its own declared width, which is the only reading
 *     available for the many screens whose declared size is not a
 *     hardware size at all (a/1/4/3#22 is 512x192 and a/0/3/3#29 is
 *     448x192).  The two readings coincide for every size except
 *     512x256 and 512x512; G2D_BG_SIZE_FLAT selects the row-major one
 *     and is what the oracle comparison uses.  See README.md.
 *
 * Every read goes through bw_rd16/bw_rd32 (portability rule 4).
 */

#include <stdlib.h>
#include <string.h>

#include "g2d.h"

#define FOURCC(a, b, c, d) \
    ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

/* ------------------------------------------------------------------ */
/* NSCR (RCSN) -- screen                                               */
/* ------------------------------------------------------------------ */

/*
 * The SCRN (on-disk "NRCS") body is 12 bytes then the entry array:
 *   0x00 u16 width_px, 0x02 u16 height_px, 0x04 u32 unknown,
 *   0x08 u32 data_size, 0x0C .. data_size bytes of u16 entries.
 * data_size is exactly width/8 * height/8 * 2 for every NSCR sub-file
 * aletheia's asset lane scanned, and no NSCR sub-file in any covered
 * archive is compressed.
 */
int g2d_nscr_parse(G2dNscr *out, const u8 *data, u32 size)
{
    u32 pos, hsize, bmagic, bsize, body, body_size;
    u32 data_size;
    u32 i;

    memset(out, 0, sizeof(*out));
    if (size < 16 || bw_rd32(data) != FOURCC('R', 'C', 'S', 'N')) {
        return -1;
    }
    hsize = bw_rd16(data + 12);
    pos = hsize;
    if (pos + 8 > size) {
        return -1;
    }
    bmagic = bw_rd32(data + pos);
    bsize = bw_rd32(data + pos + 4);
    if (bmagic != FOURCC('N', 'R', 'C', 'S') || bsize < 8 || bsize > size - pos) {
        return -1;
    }
    body = pos + 8;
    body_size = bsize - 8;
    if (body_size < 12) {
        return -1;
    }
    out->width = bw_rd16(data + body + 0);
    out->height = bw_rd16(data + body + 2);
    out->unk = bw_rd32(data + body + 4);
    data_size = bw_rd32(data + body + 8);
    if (data_size > body_size - 12) {
        data_size = body_size - 12;
    }
    out->tiles_wide = (u16)(out->width / 8u);
    out->tiles_high = (u16)(out->height / 8u);
    out->count = data_size / 2u;
    out->entries = (u16 *)calloc(out->count ? out->count : 1, sizeof(u16));
    if (out->entries == NULL) {
        return -1;
    }
    for (i = 0; i < out->count; i++) {
        out->entries[i] = bw_rd16(data + body + 12 + i * 2u);
    }
    return 0;
}

void g2d_nscr_free(G2dNscr *nscr)
{
    free(nscr->entries);
    nscr->entries = NULL;
    nscr->count = 0;
}

/* ------------------------------------------------------------------ */
/* Archive members                                                     */
/* ------------------------------------------------------------------ */

u8 *g2d_arc_member(BwArcFile *arc, u32 index, u32 *out_size)
{
    long raw_size = bw_arc_member_size(arc, index);
    u8 *raw;
    u8 *dec;
    u32 dec_size = 0;

    *out_size = 0;
    if (raw_size <= 0) {
        return NULL;
    }
    raw = (u8 *)malloc((size_t)raw_size);
    if (raw == NULL) {
        return NULL;
    }
    if (bw_arc_read_member(arc, index, raw, (u32)raw_size) != raw_size) {
        free(raw);
        return NULL;
    }
    dec = g2d_decompress(raw, (u32)raw_size, &dec_size);
    if (dec != NULL) {
        free(raw);
        *out_size = dec_size;
        return dec;
    }
    *out_size = (u32)raw_size;
    return raw;
}

/* ------------------------------------------------------------------ */
/* Character data, addressed the way a screen entry addresses it       */
/* ------------------------------------------------------------------ */

void g2d_bg_tile(const G2dNcgr *ncgr, u32 tile_index, int hflip, int vflip,
                 u8 out[64])
{
    u8 tmp[64];
    u32 off, i;

    memset(out, 0, 64);
    if (ncgr == NULL || ncgr->tile_data == NULL || ncgr->bytes_per_tile == 0) {
        return;
    }
    off = tile_index * ncgr->bytes_per_tile;
    if (off > ncgr->tile_data_size
        || ncgr->bytes_per_tile > ncgr->tile_data_size - off) {
        return;   /* past the end of this NCGR's data: blank */
    }
    if (ncgr->bytes_per_tile == 32u) {
        /* 4bpp, low nibble first (the left pixel of the pair). */
        for (i = 0; i < 32u; i++) {
            u8 b = ncgr->tile_data[off + i];

            tmp[i * 2u] = (u8)(b & 0x0F);
            tmp[i * 2u + 1u] = (u8)((b >> 4) & 0x0F);
        }
    } else {
        memcpy(tmp, ncgr->tile_data + off, 64);
    }

    for (i = 0; i < 64u; i++) {
        u32 lx = i % 8u;
        u32 ly = i / 8u;

        if (hflip) {
            lx = 7u - lx;
        }
        if (vflip) {
            ly = 7u - ly;
        }
        out[i] = tmp[ly * 8u + lx];
    }
}

/* ------------------------------------------------------------------ */
/* Screen layout                                                       */
/* ------------------------------------------------------------------ */

/*
 * src/matched/func_02040EE0.c, transcribed.  The arithmetic there is u16
 * throughout and case 4 truncates the scaled map index to sixteen bits
 * before adding the in-map offset; that truncation is kept, because a map
 * index large enough to wrap is exactly what an out-of-range (x, y) would
 * produce and the game's own helper would wrap it.
 */
u32 g2d_bg_screen_index(int x, int y, u32 size_code)
{
    u16 map, off;

    switch (size_code) {
    case G2D_BG_SIZE_128x128:
        return (u16)(x + y * 16);
    case G2D_BG_SIZE_256x256:
    case G2D_BG_SIZE_256x512:
        return (u16)(x + y * 32);
    case G2D_BG_SIZE_512x256:
        return (u16)((x & 0x1f) + (y + (x & ~0x1f)) * 32);
    case G2D_BG_SIZE_512x512:
        map = (u16)((y >> 5) * 2 + (x >> 5));
        off = (u16)((x & 0x1f) + (y & 0x1f) * 32);
        map = (u16)(map * 0x400);
        return (u16)(off + map);
    default:
        return 0;
    }
}

void g2d_bg_map_tiles(u32 size_code, u32 *tiles_wide, u32 *tiles_high)
{
    u32 w = 0, h = 0;

    switch (size_code) {
    case G2D_BG_SIZE_128x128: w = 16; h = 16; break;
    case G2D_BG_SIZE_256x256: w = 32; h = 32; break;
    case G2D_BG_SIZE_256x512: w = 32; h = 64; break;
    case G2D_BG_SIZE_512x256: w = 64; h = 32; break;
    case G2D_BG_SIZE_512x512: w = 64; h = 64; break;
    default: break;
    }
    *tiles_wide = w;
    *tiles_high = h;
}

/* ------------------------------------------------------------------ */
/* Layer sampling                                                      */
/* ------------------------------------------------------------------ */

static void layer_map_tiles(const G2dBgLayer *layer, u32 *tw, u32 *th)
{
    if (layer->screen_size == G2D_BG_SIZE_FLAT) {
        *tw = layer->nscr->tiles_wide;
        *th = layer->nscr->tiles_high;
        return;
    }
    g2d_bg_map_tiles(layer->screen_size, tw, th);
}

int g2d_bg_sample(const G2dBgLayer *layer, s32 sx, s32 sy,
                  u8 *out_index, u32 *out_color_index)
{
    u32 tw = 0, th = 0;
    s32 px, py;
    u32 tx, ty, entry_idx;
    u16 entry;
    u8 tile[64];
    u8 index;
    u32 base;

    if (layer == NULL || layer->nscr == NULL || layer->ncgr == NULL) {
        return 0;
    }
    layer_map_tiles(layer, &tw, &th);
    if (tw == 0 || th == 0) {
        return 0;
    }

    px = sx + layer->hofs;
    py = sy + layer->vofs;
    if (layer->wrap) {
        s32 mw = (s32)(tw * 8u);
        s32 mh = (s32)(th * 8u);

        px %= mw;
        if (px < 0) {
            px += mw;
        }
        py %= mh;
        if (py < 0) {
            py += mh;
        }
    } else if (px < 0 || py < 0
               || px >= (s32)(tw * 8u) || py >= (s32)(th * 8u)) {
        return 0;
    }

    tx = (u32)px / 8u;
    ty = (u32)py / 8u;
    if (layer->screen_size == G2D_BG_SIZE_FLAT) {
        entry_idx = ty * tw + tx;
    } else {
        entry_idx = g2d_bg_screen_index((int)tx, (int)ty, layer->screen_size);
    }
    if (entry_idx >= layer->nscr->count) {
        return 0;
    }
    entry = layer->nscr->entries[entry_idx];

    g2d_bg_tile(layer->ncgr, G2D_SCR_TILE(entry), G2D_SCR_HFLIP(entry),
                G2D_SCR_VFLIP(entry), tile);
    index = tile[((u32)py % 8u) * 8u + ((u32)px % 8u)];

    /*
     * 4bpp selects one 16-colour bank with the entry's palette row; 8bpp
     * ignores that field and indexes the whole 256-colour palette.  (An
     * extended-palette 8bpp layer, BGxCNT bit 13, would select a 256-entry
     * slot with the same field; no BG resource in the archives the asset
     * lane covers uses one, and this engine does not implement it.)
     */
    base = (layer->ncgr->bytes_per_tile == 32u)
         ? (u32)G2D_SCR_PAL(entry) * 16u : 0u;

    if (out_index != NULL) {
        *out_index = index;
    }
    if (out_color_index != NULL) {
        *out_color_index = base + index;
    }
    return 1;
}

void g2d_bg_render(const G2dBgLayer *layer, u32 width, u32 height, u8 *rgba)
{
    u32 x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            u8 *out = rgba + ((size_t)y * width + x) * 4u;
            u8 index;
            u32 ci;

            out[0] = 0;
            out[1] = 0;
            out[2] = 0;
            out[3] = 255;
            if (!g2d_bg_sample(layer, (s32)x, (s32)y, &index, &ci)) {
                continue;
            }
            if (layer->pal != NULL && ci < layer->pal->count) {
                out[0] = layer->pal->colors[ci].r;
                out[1] = layer->pal->colors[ci].g;
                out[2] = layer->pal->colors[ci].b;
            } else {
                out[0] = 255;
                out[1] = 0;
                out[2] = 255;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The frame compositor                                                */
/* ------------------------------------------------------------------ */

static void put_color(u8 *px, const G2dPalette *pal, u32 ci)
{
    if (pal != NULL && ci < pal->count) {
        px[0] = pal->colors[ci].r;
        px[1] = pal->colors[ci].g;
        px[2] = pal->colors[ci].b;
    } else {
        px[0] = 255;
        px[1] = 0;
        px[2] = 255;
    }
    px[3] = 255;
}

static void draw_bg(const G2dBgLayer *layer, u32 width, u32 height, u8 *rgba)
{
    u32 x, y;

    if (!layer->enabled || layer->nscr == NULL || layer->ncgr == NULL) {
        return;
    }
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            u8 index;
            u32 ci;

            if (!g2d_bg_sample(layer, (s32)x, (s32)y, &index, &ci)) {
                continue;
            }
            if (index == 0) {
                continue;   /* transparent: the layer behind shows through */
            }
            put_color(rgba + ((size_t)y * width + x) * 4u, layer->pal, ci);
        }
    }
}

static void draw_obj(const G2dObjEntry *obj, u32 width, u32 height, u8 *rgba)
{
    u32 px, py;

    if (obj->pose == NULL || obj->pose->indices == NULL) {
        return;
    }
    for (py = 0; py < obj->pose->height; py++) {
        s32 sy = obj->y + (s32)py;

        if (sy < 0 || sy >= (s32)height) {
            continue;
        }
        for (px = 0; px < obj->pose->width; px++) {
            s32 sx = obj->x + (s32)px;
            u8 index;

            if (sx < 0 || sx >= (s32)width) {
                continue;
            }
            index = obj->pose->indices[(size_t)py * obj->pose->width + px];
            if (index == 0) {
                continue;
            }
            put_color(rgba + ((size_t)sy * width + sx) * 4u, obj->pal, index);
        }
    }
}

void g2d_frame_compose(const G2dBgLayer *bgs, u32 n_bgs,
                       const G2dObjEntry *objs, u32 n_objs,
                       const G2dPalette *backdrop_pal,
                       u32 width, u32 height, u8 *rgba)
{
    u32 i;
    int prio;

    /* The backdrop: colour 0 of the BG palette, opaque, everywhere. */
    for (i = 0; i < width * height; i++) {
        put_color(rgba + (size_t)i * 4u, backdrop_pal, 0);
    }

    for (prio = 3; prio >= 0; prio--) {
        u32 n;

        /* BGs of this priority, back to front: BG3 first, BG0 last. */
        for (n = n_bgs; n > 0; n--) {
            const G2dBgLayer *layer = &bgs[n - 1];

            if ((u32)prio == layer->priority) {
                draw_bg(layer, width, height, rgba);
            }
        }
        /* Then the OBJs of this priority, so an OBJ wins the tie against
         * a BG of equal priority.  Reverse list order keeps the OBJ
         * half's rule that the lowest OAM slot draws in front. */
        for (n = n_objs; n > 0; n--) {
            if (objs[n - 1].priority == (u32)prio) {
                draw_obj(&objs[n - 1], width, height, rgba);
            }
        }
    }
}
