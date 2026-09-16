/*
 * tests/test_window.c -- windows and the colour special effects against
 * hand-computed pixels.
 *
 * No game data and no oracle.  The frame is two 8bpp text BG layers over
 * a backdrop, each layer painted in one flat colour, so every expected
 * pixel below is a colour worked out in the 5 bits per channel the DS
 * has and then expanded the one way the palette is expanded.
 *
 *   backdrop   palette index 0, 5-bit ( 2,  2,  2)
 *   BG0        palette index 1, 5-bit (20, 10,  0), priority 0
 *   BG1        palette index 2, 5-bit ( 4,  8, 16), priority 1
 */

#include <stdio.h>
#include <string.h>

#include "g2d.h"
#include "port_test.h"

/* The palette's own expansion of a 5-bit channel. */
static u8 c8(u32 c5)
{
    return (u8)(c5 * 255u / 31u);
}

static G2dColor g_colors[3];
static G2dPalette g_pal;

static u8 g_tiles[2 * 64];
static G2dNcgr g_ncgr;

static u16 g_scr0[32 * 24];
static u16 g_scr1[32 * 24];
static G2dNscr g_nscr0, g_nscr1;
static G2dBgLayer g_bgs[2];

static void build(void)
{
    u32 i;

    g_colors[0].r = c8(2);  g_colors[0].g = c8(2);  g_colors[0].b = c8(2);
    g_colors[1].r = c8(20); g_colors[1].g = c8(10); g_colors[1].b = c8(0);
    g_colors[2].r = c8(4);  g_colors[2].g = c8(8);  g_colors[2].b = c8(16);
    g_pal.colors = g_colors;
    g_pal.count = 3;

    for (i = 0; i < 64u; i++) {
        g_tiles[i] = 1;        /* tile 0: palette index 1 everywhere */
        g_tiles[64 + i] = 2;   /* tile 1: palette index 2 everywhere */
    }
    memset(&g_ncgr, 0, sizeof(g_ncgr));
    g_ncgr.pixel_fmt = G2D_NCGR_FMT_8BPP;
    g_ncgr.bytes_per_tile = 64;
    g_ncgr.tile_data = g_tiles;
    g_ncgr.tile_data_size = sizeof(g_tiles);
    g_ncgr.tile_num_x = G2D_NCGR_DIM_SENTINEL;
    g_ncgr.tile_num_y = G2D_NCGR_DIM_SENTINEL;

    for (i = 0; i < 32u * 24u; i++) {
        g_scr0[i] = 0;   /* BG0 is all tile 0 */
        g_scr1[i] = 1;   /* BG1 is all tile 1 */
    }
    memset(&g_nscr0, 0, sizeof(g_nscr0));
    g_nscr0.width = 256; g_nscr0.height = 192;
    g_nscr0.tiles_wide = 32; g_nscr0.tiles_high = 24;
    g_nscr0.entries = g_scr0; g_nscr0.count = 32u * 24u;
    g_nscr1 = g_nscr0;
    g_nscr1.entries = g_scr1;

    memset(g_bgs, 0, sizeof(g_bgs));
    g_bgs[0].nscr = &g_nscr0;
    g_bgs[0].ncgr = &g_ncgr;
    g_bgs[0].pal = &g_pal;
    g_bgs[0].screen_size = G2D_BG_SIZE_FLAT;
    g_bgs[0].priority = 0;
    g_bgs[0].wrap = 1;
    g_bgs[0].enabled = 1;
    g_bgs[1] = g_bgs[0];
    g_bgs[1].nscr = &g_nscr1;
    g_bgs[1].priority = 1;
}

static void desc_init(G2dFrameDesc *desc)
{
    memset(desc, 0, sizeof(*desc));
    desc->bgs = g_bgs;
    desc->n_bgs = 2;
    desc->backdrop_pal = &g_pal;
}

/* One pixel of a composed 4x4 frame. */
static void compose_pixel(const G2dFrameDesc *desc, u32 x, u32 y, u8 out[3])
{
    static u8 rgba[4 * 4 * 4];

    g2d_frame_compose_desc(desc, 4, 4, rgba);
    out[0] = rgba[(y * 4u + x) * 4u + 0];
    out[1] = rgba[(y * 4u + x) * 4u + 1];
    out[2] = rgba[(y * 4u + x) * 4u + 2];
}

static int check_rgb(const char *label, const u8 got[3],
                     u32 r5, u32 g5, u32 b5)
{
    bw_checks++;
    if (got[0] != c8(r5) || got[1] != c8(g5) || got[2] != c8(b5)) {
        bw_failures++;
        printf("  FAIL %-40s got %u,%u,%u  want %u,%u,%u"
               " (5-bit %u,%u,%u)\n",
               label, got[0], got[1], got[2],
               c8(r5), c8(g5), c8(b5), r5, g5, b5);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */

static int test_window_mask(void)
{
    G2dWindows w;
    static u8 objmask[4 * 4];

    BW_CHECK_EQ("no window system", g2d_window_mask(NULL, 0, 0), G2D_LAYER_ALL);

    memset(&w, 0, sizeof(w));
    w.outside_layers = G2D_LAYER_BG0;
    BW_CHECK_EQ("no window enabled is everything",
                g2d_window_mask(&w, 0, 0), G2D_LAYER_ALL);

    w.win0.enabled = 1;
    w.win0.x1 = 1; w.win0.x2 = 3;
    w.win0.y1 = 1; w.win0.y2 = 3;
    w.win0.layers = G2D_LAYER_BG1 | G2D_LAYER_EFFECT;
    BW_CHECK_EQ("inside win0", g2d_window_mask(&w, 1, 1), w.win0.layers);
    BW_CHECK_EQ("inside win0, last pixel",
                g2d_window_mask(&w, 2, 2), w.win0.layers);
    BW_CHECK_EQ("outside win0, right edge is exclusive",
                g2d_window_mask(&w, 3, 1), G2D_LAYER_BG0);
    BW_CHECK_EQ("outside win0, above", g2d_window_mask(&w, 1, 0), G2D_LAYER_BG0);

    /* WIN1 loses to WIN0 where they overlap and wins outside it. */
    w.win1.enabled = 1;
    w.win1.x1 = 0; w.win1.x2 = 4;
    w.win1.y1 = 0; w.win1.y2 = 4;
    w.win1.layers = G2D_LAYER_BG2;
    BW_CHECK_EQ("win0 outranks win1", g2d_window_mask(&w, 1, 1), w.win0.layers);
    BW_CHECK_EQ("win1 where win0 does not reach",
                g2d_window_mask(&w, 3, 3), w.win1.layers);

    /* The OBJ window loses to both and wins over the outside region. */
    w.win1.enabled = 0;
    memset(objmask, 0, sizeof(objmask));
    objmask[3 * 4 + 3] = 1;
    w.obj_enabled = 1;
    w.obj_mask = objmask;
    w.mask_width = 4;
    w.mask_height = 4;
    w.obj_layers = G2D_LAYER_BG3;
    BW_CHECK_EQ("inside the OBJ window", g2d_window_mask(&w, 3, 3),
                G2D_LAYER_BG3);
    BW_CHECK_EQ("win0 outranks the OBJ window",
                g2d_window_mask(&w, 1, 1), w.win0.layers);
    BW_CHECK_EQ("outside every window", g2d_window_mask(&w, 0, 3),
                G2D_LAYER_BG0);

    /*
     * A right edge before the left edge, or past the screen, reads as the
     * screen edge: X2 is eight bits on hardware and cannot say 256, so a
     * full-width window is written with X2 = 0 as often as with 255.
     */
    memset(&w, 0, sizeof(w));
    w.win0.enabled = 1;
    w.win0.x1 = 10; w.win0.x2 = 0;
    w.win0.y1 = 0;  w.win0.y2 = G2D_SCREEN_HEIGHT;
    w.win0.layers = G2D_LAYER_BG1;
    w.outside_layers = G2D_LAYER_BG0;
    BW_CHECK_EQ("x2 before x1 means the right edge",
                g2d_window_mask(&w, 200, 0), G2D_LAYER_BG1);
    BW_CHECK_EQ("and x1 still excludes", g2d_window_mask(&w, 9, 0),
                G2D_LAYER_BG0);
    return 0;
}

static int test_layer_masking(void)
{
    G2dFrameDesc desc;
    G2dWindows w;
    u8 px[3];

    desc_init(&desc);

    /* No window: BG0 is on top. */
    compose_pixel(&desc, 1, 1, px);
    check_rgb("no window, BG0 on top", px, 20, 10, 0);

    memset(&w, 0, sizeof(w));
    w.win0.enabled = 1;
    w.win0.x1 = 1; w.win0.x2 = 3;
    w.win0.y1 = 1; w.win0.y2 = 3;
    w.win0.layers = G2D_LAYER_BG1;          /* BG0 hidden inside */
    w.outside_layers = G2D_LAYER_ALL;
    desc.windows = &w;

    compose_pixel(&desc, 1, 1, px);
    check_rgb("inside the window, BG0 hidden", px, 4, 8, 16);
    compose_pixel(&desc, 0, 0, px);
    check_rgb("outside the window, BG0 shows", px, 20, 10, 0);

    /* Hide both layers inside: the backdrop shows. */
    w.win0.layers = 0;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("both layers hidden, backdrop shows", px, 2, 2, 2);
    return 0;
}

static int test_alpha(void)
{
    G2dFrameDesc desc;
    G2dBlend blend;
    G2dWindows w;
    u8 px[3];

    desc_init(&desc);
    memset(&blend, 0, sizeof(blend));
    blend.effect = G2D_BLEND_ALPHA;
    blend.first = G2D_LAYER_BG0;
    blend.second = G2D_LAYER_BG1;
    blend.eva = 8;
    blend.evb = 8;
    desc.blend = &blend;

    /*
     * (20 * 8 + 4 * 8) >> 4 = 12, (10 * 8 + 8 * 8) >> 4 = 9,
     * (0 * 8 + 16 * 8) >> 4 = 8.
     */
    compose_pixel(&desc, 1, 1, px);
    check_rgb("BG0 over BG1, half and half", px, 12, 9, 8);

    /* EVA alone, at full weight, is the first target unchanged. */
    blend.eva = 16;
    blend.evb = 0;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("eva 16, evb 0", px, 20, 10, 0);

    /* (20 * 4 + 4 * 12) >> 4 = 8, (10 * 4 + 8 * 12) >> 4 = 8,
     * (0 + 16 * 12) >> 4 = 12. */
    blend.eva = 4;
    blend.evb = 12;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("eva 4, evb 12", px, 8, 8, 12);

    /*
     * The second target has to be the layer *directly beneath* the first.
     * BG2 is not in this frame at all, so nothing blends.
     */
    blend.eva = 8;
    blend.evb = 8;
    blend.second = G2D_LAYER_BG2;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("second target absent, no blend", px, 20, 10, 0);

    /* The backdrop is beneath BG1, not beneath BG0: still no blend. */
    blend.second = G2D_LAYER_BD;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("backdrop is two layers down, no blend", px, 20, 10, 0);

    /*
     * Hide BG1 with a window and the backdrop *becomes* the layer
     * directly beneath BG0, so the same blend now runs:
     * (20 * 8 + 2 * 8) >> 4 = 11, (10 * 8 + 2 * 8) >> 4 = 6,
     * (0 * 8 + 2 * 8) >> 4 = 1.
     */
    memset(&w, 0, sizeof(w));
    w.win0.enabled = 1;
    w.win0.x1 = 0; w.win0.x2 = 4;
    w.win0.y1 = 0; w.win0.y2 = 4;
    w.win0.layers = G2D_LAYER_BG0 | G2D_LAYER_EFFECT;
    w.outside_layers = G2D_LAYER_ALL;
    desc.windows = &w;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("BG1 hidden, the backdrop is beneath BG0", px, 11, 6, 1);

    /* The window's effect bit gates the whole thing. */
    w.win0.layers = G2D_LAYER_BG0;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("effect bit clear, no blend", px, 20, 10, 0);

    /* A first target that is not the topmost pixel does not blend. */
    desc.windows = NULL;
    blend.first = G2D_LAYER_BG1;
    blend.second = G2D_LAYER_BD;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("first target is not topmost, no blend", px, 20, 10, 0);
    return 0;
}

static int test_brightness(void)
{
    G2dFrameDesc desc;
    G2dBlend blend;
    u8 px[3];

    desc_init(&desc);
    memset(&blend, 0, sizeof(blend));
    blend.first = G2D_LAYER_BG0;
    blend.evy = 8;
    desc.blend = &blend;

    /*
     * Up: a + ((31 - a) * 8 >> 4).  20 -> 20 + 5 = 25, 10 -> 10 + 10 = 20,
     * 0 -> 0 + 15 = 15.
     */
    blend.effect = G2D_BLEND_BRIGHT_UP;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("brightness up, evy 8", px, 25, 20, 15);

    /* Down: a - ((a * 8) >> 4).  20 -> 10, 10 -> 5, 0 -> 0. */
    blend.effect = G2D_BLEND_BRIGHT_DOWN;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("brightness down, evy 8", px, 10, 5, 0);

    /* Full strength: white, then black. */
    blend.evy = 16;
    blend.effect = G2D_BLEND_BRIGHT_UP;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("brightness up, evy 16", px, 31, 31, 31);
    blend.effect = G2D_BLEND_BRIGHT_DOWN;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("brightness down, evy 16", px, 0, 0, 0);

    /* evy 0 changes nothing. */
    blend.evy = 0;
    blend.effect = G2D_BLEND_BRIGHT_UP;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("brightness up, evy 0", px, 20, 10, 0);

    /* The backdrop can be the first target too. */
    blend.first = G2D_LAYER_BD;
    blend.evy = 16;
    blend.effect = G2D_BLEND_BRIGHT_DOWN;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("backdrop is not topmost here", px, 20, 10, 0);

    g_bgs[0].enabled = 0;
    g_bgs[1].enabled = 0;
    compose_pixel(&desc, 1, 1, px);
    check_rgb("backdrop darkened to black", px, 0, 0, 0);
    g_bgs[0].enabled = 1;
    g_bgs[1].enabled = 1;
    return 0;
}

static int test_blend_primitives(void)
{
    u8 a[3], b[3], out[3];

    /* Saturation: two full-brightness channels at full weight clamp. */
    a[0] = c8(31); a[1] = c8(31); a[2] = c8(31);
    b[0] = c8(31); b[1] = c8(31); b[2] = c8(31);
    g2d_blend_alpha(a, b, 16, 16, out);
    check_rgb("alpha saturates at 31", out, 31, 31, 31);

    /* A coefficient above 16 reads as 16. */
    a[0] = c8(10); a[1] = c8(10); a[2] = c8(10);
    b[0] = c8(0);  b[1] = c8(0);  b[2] = c8(0);
    g2d_blend_alpha(a, b, 31, 0, out);
    check_rgb("eva above 16 reads as 16", out, 10, 10, 10);

    /* The 8-bit fold back to 5 bits is exact for every palette colour. */
    {
        u32 c5;

        for (c5 = 0; c5 < 32u; c5++) {
            a[0] = c8(c5); a[1] = c8(c5); a[2] = c8(c5);
            g2d_blend_brightness(a, 0, 1, out);
            BW_CHECK_EQ("5-bit fold round trip", out[0], c8(c5));
        }
    }
    return 0;
}

int main(void)
{
    build();

    if (test_window_mask() || test_layer_masking() || test_alpha()
        || test_brightness() || test_blend_primitives()) {
        return 1;
    }

    BW_REPORT("test_window");
}
