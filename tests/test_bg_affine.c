/*
 * tests/test_bg_affine.c -- affine BG layers, and the 512-wide screen
 * order flag, against hand-computed pixels.
 *
 * No game data and no oracle: the character data and the map below are
 * built here so that every sample has one right answer that can be worked
 * out on paper.
 *
 * The tileset is three 8bpp tiles:
 *   tile 0  a gradient: texel (r, c) holds r * 8 + c + 1, so 1 .. 64, and
 *           no texel is the transparent index
 *   tile 1  every texel 100
 *   tile 2  every texel 200
 *
 * so a sample says both which tile it landed in and where inside it.
 */

#include <stdio.h>
#include <string.h>

#include "g2d.h"
#include "port_test.h"

#define AFF_TILES 16u          /* the 128x128 affine size */
#define AFF_SIDE  (AFF_TILES * 8u)

static u8 g_tiles[3 * 64];
static u8 g_map[AFF_TILES * AFF_TILES];
static G2dNcgr g_ncgr;

static void build_tiles(void)
{
    u32 i;

    for (i = 0; i < 64u; i++) {
        g_tiles[i] = (u8)(i + 1u);        /* tile 0: 1 .. 64 */
        g_tiles[64 + i] = 100;            /* tile 1 */
        g_tiles[128 + i] = 200;           /* tile 2 */
    }
    memset(&g_ncgr, 0, sizeof(g_ncgr));
    g_ncgr.pixel_fmt = G2D_NCGR_FMT_8BPP;
    g_ncgr.bytes_per_tile = 64;
    g_ncgr.tile_data = g_tiles;
    g_ncgr.tile_data_size = sizeof(g_tiles);
    g_ncgr.tile_num_x = G2D_NCGR_DIM_SENTINEL;
    g_ncgr.tile_num_y = G2D_NCGR_DIM_SENTINEL;
}

/*
 * The map: tile cell (0, 0) is the gradient, (1, 0) is the 100 tile,
 * (0, 1) is the 200 tile, and everything else is the 100 tile.
 */
static void build_map(void)
{
    u32 i;

    for (i = 0; i < sizeof(g_map); i++) {
        g_map[i] = 1;
    }
    g_map[0] = 0;
    g_map[1] = 1;
    g_map[AFF_TILES] = 2;
}

static void layer_init(G2dBgAffineLayer *layer)
{
    memset(layer, 0, sizeof(*layer));
    layer->map = g_map;
    layer->map_size = sizeof(g_map);
    layer->ncgr = &g_ncgr;
    layer->screen_size = G2D_BG_AFF_SIZE_128;
    layer->bg_index = 2;
    layer->enabled = 1;
    g2d_affine_from_srt(G2D_FX_ONE, G2D_FX_ONE, 0, &layer->mtx);
}

/* The index this layer shows at (x, y), or -1 when it shows nothing. */
static int sample(const G2dBgAffineLayer *layer, s32 x, s32 y)
{
    u8 index = 0;
    u32 ci = 0;

    if (!g2d_bg_affine_sample(layer, x, y, &index, &ci)) {
        return -1;
    }
    /* An affine map entry carries no palette row, so the two agree. */
    if (ci != index) {
        return -2;
    }
    return (int)index;
}

/* ------------------------------------------------------------------ */

static int test_sizes(void)
{
    BW_CHECK_EQ("affine size 128", g2d_bg_affine_tiles(G2D_BG_AFF_SIZE_128), 16);
    BW_CHECK_EQ("affine size 256", g2d_bg_affine_tiles(G2D_BG_AFF_SIZE_256), 32);
    BW_CHECK_EQ("affine size 512", g2d_bg_affine_tiles(G2D_BG_AFF_SIZE_512), 64);
    BW_CHECK_EQ("affine size 1024",
                g2d_bg_affine_tiles(G2D_BG_AFF_SIZE_1024), 128);
    BW_CHECK_EQ("affine size bad code", g2d_bg_affine_tiles(9), 0);
    return 0;
}

static int test_identity(void)
{
    G2dBgAffineLayer layer;

    layer_init(&layer);

    /* Identity: screen pixel = texture texel. */
    BW_CHECK_EQ("identity (0,0)", sample(&layer, 0, 0), 1);
    BW_CHECK_EQ("identity (3,5)", sample(&layer, 3, 5), 5 * 8 + 3 + 1);
    BW_CHECK_EQ("identity (7,7)", sample(&layer, 7, 7), 64);
    BW_CHECK_EQ("identity tile (1,0)", sample(&layer, 8, 0), 100);
    BW_CHECK_EQ("identity tile (0,1)", sample(&layer, 0, 8), 200);

    /* BGxX is 20.8: eight whole pixels is 8 * 256. */
    layer.ref_x = 8 * 256;
    BW_CHECK_EQ("ref_x = 8 pixels", sample(&layer, 0, 0), 100);
    layer.ref_x = 0;
    layer.ref_y = 8 * 256;
    BW_CHECK_EQ("ref_y = 8 pixels", sample(&layer, 0, 0), 200);

    /*
     * A fractional reference point is truncated towards minus infinity,
     * not towards zero: -1/256 of a pixel samples texel -1, which is off
     * a non-wrapping map.
     */
    layer.ref_y = 0;
    layer.ref_x = -1;
    BW_CHECK_EQ("ref_x = -1/256 pixel, no wrap", sample(&layer, 0, 0), -1);
    return 0;
}

static int test_scale_and_rotation(void)
{
    G2dBgAffineLayer layer;

    /* A 2x scale: PA = PD = 128, so two screen pixels per texel. */
    layer_init(&layer);
    g2d_affine_from_srt(2 * G2D_FX_ONE, 2 * G2D_FX_ONE, 0, &layer.mtx);
    BW_CHECK_EQ("2x PA", layer.mtx.pa, 128);
    BW_CHECK_EQ("2x (0,0)", sample(&layer, 0, 0), 1);
    BW_CHECK_EQ("2x (1,1)", sample(&layer, 1, 1), 1);
    BW_CHECK_EQ("2x (2,2)", sample(&layer, 2, 2), 1 * 8 + 1 + 1);
    BW_CHECK_EQ("2x (3,3)", sample(&layer, 3, 3), 1 * 8 + 1 + 1);
    /* Sixteen screen pixels now cover the first tile row. */
    BW_CHECK_EQ("2x (15,0)", sample(&layer, 15, 0), 7 + 1);
    BW_CHECK_EQ("2x (16,0)", sample(&layer, 16, 0), 100);

    /*
     * A quarter turn: PA = 0, PB = 256, PC = -256, PD = 0, so tx = y and
     * ty = -x.  Screen y walks the texture's x axis, and any positive
     * screen x lands at a negative texture y.
     */
    layer_init(&layer);
    g2d_affine_from_srt(G2D_FX_ONE, G2D_FX_ONE, 0x4000, &layer.mtx);
    BW_CHECK_EQ("90 PB", layer.mtx.pb, 256);
    BW_CHECK_EQ("90 PC", layer.mtx.pc, -256);
    BW_CHECK_EQ("90 (0,0)", sample(&layer, 0, 0), 1);
    BW_CHECK_EQ("90 (0,3)", sample(&layer, 0, 3), 0 * 8 + 3 + 1);
    BW_CHECK_EQ("90 (0,8)", sample(&layer, 0, 8), 100);
    BW_CHECK_EQ("90 (1,0), no wrap", sample(&layer, 1, 0), -1);

    /* With wraparound the same pixel comes off the bottom of the map. */
    layer.wrap = 1;
    BW_CHECK_EQ("90 (1,0), wrapped", sample(&layer, 1, 0), 100);
    return 0;
}

static int test_wraparound(void)
{
    G2dBgAffineLayer layer;

    layer_init(&layer);

    /* One map past the right edge: nothing without the flag. */
    BW_CHECK_EQ("off the right, no wrap",
                sample(&layer, (s32)AFF_SIDE, 0), -1);
    BW_CHECK_EQ("off the bottom, no wrap",
                sample(&layer, 0, (s32)AFF_SIDE), -1);
    BW_CHECK_EQ("negative, no wrap", sample(&layer, -1, 0), -1);

    layer.wrap = 1;
    BW_CHECK_EQ("off the right, wrapped",
                sample(&layer, (s32)AFF_SIDE, 0), 1);
    BW_CHECK_EQ("off the bottom, wrapped",
                sample(&layer, 0, (s32)AFF_SIDE), 1);
    /* Texel x = -1 wraps to 127, tile cell (15, 0), which is the 100 tile. */
    BW_CHECK_EQ("negative x, wrapped", sample(&layer, -1, 0), 100);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The 512-wide text screen order, both readings                        */
/* ------------------------------------------------------------------ */

/*
 * A 512x256 text screen is the size where the two readings of the entry
 * array differ.  Tile (32, 0) is entry 32 read row-major at the map's own
 * 64-tile width, and entry 1024 in the VRAM order func_02040EE0 attests
 * (the second 32x32 map follows the first).  Both entries are filled with
 * a different tile here, so one sample tells the two readings apart.
 */
static u16 g_scr[64 * 32];
static G2dNscr g_nscr;

static int test_screen_order(void)
{
    G2dBgLayer layer;
    u8 index = 0;
    u32 ci = 0;

    memset(g_scr, 0, sizeof(g_scr));
    g_scr[32] = 1;      /* the row-major reading of tile (32, 0)  */
    g_scr[1024] = 2;    /* the VRAM reading of the same tile      */

    memset(&g_nscr, 0, sizeof(g_nscr));
    g_nscr.width = 512;
    g_nscr.height = 256;
    g_nscr.tiles_wide = 64;
    g_nscr.tiles_high = 32;
    g_nscr.entries = g_scr;
    g_nscr.count = 64u * 32u;

    memset(&layer, 0, sizeof(layer));
    layer.nscr = &g_nscr;
    layer.ncgr = &g_ncgr;
    layer.screen_size = G2D_BG_SIZE_512x256;
    layer.wrap = 1;
    layer.enabled = 1;

    /* A zeroed layer gets the attested order. */
    BW_CHECK_EQ("default order is the VRAM order",
                layer.screen_order, G2D_BG_ORDER_VRAM);
    BW_REQUIRE("VRAM sample", g2d_bg_sample(&layer, 256, 0, &index, &ci) == 1);
    BW_CHECK_EQ("VRAM order reads entry 1024", index, 200);

    layer.screen_order = G2D_BG_ORDER_ROW_MAJOR;
    BW_REQUIRE("row-major sample",
               g2d_bg_sample(&layer, 256, 0, &index, &ci) == 1);
    BW_CHECK_EQ("row-major order reads entry 32", index, 100);

    /* The two readings coincide everywhere inside the first 32x32 map. */
    layer.screen_order = G2D_BG_ORDER_VRAM;
    BW_REQUIRE("VRAM sample inside map 0",
               g2d_bg_sample(&layer, 8, 8, &index, &ci) == 1);
    BW_CHECK_EQ("VRAM order, tile (1,1)", index, 1);
    layer.screen_order = G2D_BG_ORDER_ROW_MAJOR;
    BW_REQUIRE("row-major sample inside map 0",
               g2d_bg_sample(&layer, 8, 8, &index, &ci) == 1);
    BW_CHECK_EQ("row-major order, tile (1,1)", index, 1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The affine layer in the frame compositor                             */
/* ------------------------------------------------------------------ */

static int test_frame(void)
{
    G2dBgAffineLayer aff;
    G2dFrameDesc desc;
    G2dFrameSample stack[4];
    u32 n;

    layer_init(&aff);
    aff.priority = 1;

    memset(&desc, 0, sizeof(desc));
    desc.abgs = &aff;
    desc.n_abgs = 1;

    n = g2d_frame_stack(&desc, 0, 0, stack, 4);
    BW_CHECK_EQ("affine layer contributes", n, 1);
    BW_CHECK_EQ("affine layer is a BG", stack[0].is_obj, 0);
    BW_CHECK_EQ("affine layer number", stack[0].layer, 2);
    BW_CHECK_EQ("affine layer colour", stack[0].color_index, 1);

    /* Disabled, and off a non-wrapping map, it contributes nothing. */
    aff.enabled = 0;
    BW_CHECK_EQ("disabled affine layer",
                g2d_frame_stack(&desc, 0, 0, stack, 4), 0);
    aff.enabled = 1;
    BW_CHECK_EQ("affine layer off its map",
                g2d_frame_stack(&desc, (s32)AFF_SIDE, 0, stack, 4), 0);
    return 0;
}

int main(void)
{
    build_tiles();
    build_map();

    if (test_sizes() || test_identity() || test_scale_and_rotation()
        || test_wraparound() || test_screen_order() || test_frame()) {
        return 1;
    }

    BW_REPORT("test_bg_affine");
}
