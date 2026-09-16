/*
 * engine/g2d/bg_affine.c -- rotation/scaling BG layers.
 *
 * The affine counterpart of bg.c's text layer, sharing every piece of
 * arithmetic with the OBJ affine path in affine.c: the same 8.8
 * PA/PB/PC/PD, the same floor-truncating shift, the same discarded
 * fraction.  What differs is the reference point (a 20.8 BGxX/BGxY rather
 * than a canvas centre), the map entry (one byte, no flip and no palette
 * row) and the wraparound flag.
 *
 * Provenance is stated in g2d.h's affine BG section.  The one rule worth
 * repeating here: an affine map is a plain tiles_wide * tiles_high array.
 * The block layout func_02040EE0 spells out for the 512-pixel text sizes
 * has no affine counterpart, because an affine map is never wider than
 * one block in the sense that layout means.
 */

#include <string.h>

#include "g2d.h"

u32 g2d_bg_affine_tiles(u32 size_code)
{
    switch (size_code) {
    case G2D_BG_AFF_SIZE_128:  return 16u;
    case G2D_BG_AFF_SIZE_256:  return 32u;
    case G2D_BG_AFF_SIZE_512:  return 64u;
    case G2D_BG_AFF_SIZE_1024: return 128u;
    default:                   return 0u;
    }
}

/*
 * BGxX and BGxY are 28-bit signed on hardware.  A reference point that
 * overflows that wraps rather than running away, so the engine wraps it
 * too: anything else would diverge from the hardware exactly where a
 * scrolling affine layer has been running for a long time.
 */
static s32 sign_extend_28(s32 v)
{
    u32 u = (u32)v & 0x0FFFFFFFu;

    if (u & 0x08000000u) {
        u |= 0xF0000000u;
    }
    return (s32)u;
}

/* The hardware's shift: 8.8 to whole pixels, fraction discarded. */
static s32 fx8_floor(s64 v)
{
    return (s32)((v >= 0) ? (v >> 8) : -(((-v) + 255) >> 8));
}

int g2d_bg_affine_sample(const G2dBgAffineLayer *layer, s32 sx, s32 sy,
                         u8 *out_index, u32 *out_color_index)
{
    u32 tiles, side;
    s32 tx, ty;
    u32 entry_idx;
    u8 tile[64];
    u8 index;

    if (layer == NULL || layer->map == NULL || layer->ncgr == NULL) {
        return 0;
    }
    tiles = g2d_bg_affine_tiles(layer->screen_size);
    if (tiles == 0) {
        return 0;
    }
    side = tiles * 8u;

    tx = fx8_floor((s64)sign_extend_28(layer->ref_x)
                   + (s64)layer->mtx.pa * sx + (s64)layer->mtx.pb * sy);
    ty = fx8_floor((s64)sign_extend_28(layer->ref_y)
                   + (s64)layer->mtx.pc * sx + (s64)layer->mtx.pd * sy);

    if (layer->wrap) {
        /* Every affine map side is a power of two, which is why the
         * hardware can wrap with a mask rather than a modulo. */
        tx = (s32)((u32)tx & (side - 1u));
        ty = (s32)((u32)ty & (side - 1u));
    } else if (tx < 0 || ty < 0 || (u32)tx >= side || (u32)ty >= side) {
        return 0;
    }

    entry_idx = ((u32)ty / 8u) * tiles + ((u32)tx / 8u);
    if (entry_idx >= layer->map_size) {
        return 0;
    }

    g2d_bg_tile(layer->ncgr, layer->map[entry_idx], 0, 0, tile);
    index = tile[((u32)ty % 8u) * 8u + ((u32)tx % 8u)];

    if (out_index != NULL) {
        *out_index = index;
    }
    if (out_color_index != NULL) {
        /* An affine map entry carries no palette row: 8bpp character data
         * indexes the whole 256-colour palette directly. */
        *out_color_index = index;
    }
    return 1;
}

void g2d_bg_affine_render(const G2dBgAffineLayer *layer, u32 width, u32 height,
                          u8 *rgba)
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
            if (!g2d_bg_affine_sample(layer, (s32)x, (s32)y, &index, &ci)) {
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
