/*
 * tests/test_compose3d.c -- a 3D layer inside the 2D per-pixel compositor,
 * synthetically.
 *
 * No SDL, no game data, no oracle: a 4x4 3D buffer of one flat colour and a
 * one-tile affine BG stand in for the two layers, and every expected pixel
 * is computed on paper.  The point is the *stack*, not the picture: the 3D
 * output is an ordinary layer, so a 2D BG can sit in front of or behind it,
 * a window can hide it, and a 3D pixel can blend with what is beneath.
 *
 * What is checked:
 *
 *   - 3D over the backdrop: every covered pixel is the 3D colour;
 *   - a 3D buffer smaller than the screen leaves the uncovered pixels to
 *     whatever is behind (here the backdrop);
 *   - a 3D layer disabled contributes nothing;
 *   - priority: a 2D BG at a lower priority number draws in front of the 3D
 *     layer, and the 3D layer draws in front of a BG at a higher number;
 *   - a window that does not include the 3D layer's bit hides it, revealing
 *     the backdrop;
 *   - the 3D layer's own blend: an opaque 3D pixel over a second target is
 *     unchanged (EVA/EVB are not used), a translucent one is mixed by its
 *     alpha whatever the effect and first-target bits say, by hand in the
 *     6-bit stage; with no second target beneath it takes the ordinary
 *     rules as BG0;
 *   - alpha 0 in the 3D buffer is transparent, to the backdrop and to a BG;
 *   - BG0HOFS scrolls the layer with a 512-pixel period.
 */

#include <stdio.h>
#include <string.h>

#include "g2d.h"
#include "port_test.h"

#define W 4u
#define H 4u

int main(void)
{
    G2dColor   bdc[1];
    G2dPalette backdrop;
    G2dColor   bgc[8];
    G2dPalette bgpal;
    G2dNcgr    ncgr;
    G2dBgAffineLayer abg;
    u8         tile[64];
    u8         map[256];
    u8         rgb3d[W * H * 4];
    u8         out[W * H * 4];
    G2dLayer3d l3;
    G2dWindows win;
    G2dBlend   blend;
    G2dFrameDesc desc;
    u32        i;

    /* One flat 3D frame, one flat affine BG, one backdrop colour. */
    for (i = 0; i < W * H; i++) {
        rgb3d[i * 4u + 0u] = 10;
        rgb3d[i * 4u + 1u] = 20;
        rgb3d[i * 4u + 2u] = 30;
        rgb3d[i * 4u + 3u] = 255;
    }
    bdc[0].r = 1; bdc[0].g = 2; bdc[0].b = 3;
    backdrop.colors = bdc; backdrop.count = 1;

    memset(tile, 7, sizeof(tile));
    memset(map, 0, sizeof(map));
    for (i = 0; i < 8u; i++) {
        bgc[i].r = (u8)i; bgc[i].g = (u8)i; bgc[i].b = (u8)i;
    }
    bgc[7].r = 70; bgc[7].g = 80; bgc[7].b = 90;
    bgpal.colors = bgc; bgpal.count = 8;

    memset(&ncgr, 0, sizeof(ncgr));
    ncgr.tile_data = tile;
    ncgr.tile_data_size = sizeof(tile);
    ncgr.bytes_per_tile = 64;

    memset(&abg, 0, sizeof(abg));
    abg.map = map; abg.map_size = sizeof(map);
    abg.ncgr = &ncgr; abg.pal = &bgpal;
    abg.screen_size = G2D_BG_AFF_SIZE_128;
    abg.bg_index = 2; abg.priority = 0; abg.wrap = 1;
    abg.mtx.pa = 256; abg.mtx.pd = 256;   /* identity, 8.8 */
    abg.enabled = 1;

    memset(&l3, 0, sizeof(l3));
    l3.rgba = rgb3d; l3.width = W; l3.height = H;
    l3.priority = 0; l3.bg_index = 0; l3.enabled = 1;

    memset(&desc, 0, sizeof(desc));
    desc.backdrop_pal = &backdrop;
    desc.g3d = &l3;

    printf("3D over the backdrop\n");
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("3D red", out[0], 10);
    BW_CHECK_EQ("3D green", out[1], 20);
    BW_CHECK_EQ("3D blue", out[2], 30);
    BW_CHECK_EQ("opaque", out[3], 255);

    printf("a 3D buffer smaller than the screen\n");
    l3.width = 2; l3.height = 2;
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("inside the buffer is 3D", out[0], 10);
    BW_CHECK_EQ("outside the buffer is the backdrop",
                (out[(0u * W + 3u) * 4u] == 1
                 && out[(0u * W + 3u) * 4u + 1u] == 2), 1);
    l3.width = W; l3.height = H;

    printf("the 3D layer disabled\n");
    l3.enabled = 0;
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("backdrop red", out[0], 1);
    BW_CHECK_EQ("backdrop green", out[1], 2);
    l3.enabled = 1;

    printf("priority against a 2D BG\n");
    desc.abgs = &abg; desc.n_abgs = 1;
    abg.priority = 0; l3.priority = 1;
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("a BG in front of 3D wins", out[0], 70);
    BW_CHECK_EQ("and its green survives", out[1], 80);
    abg.priority = 1; l3.priority = 0;
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("3D in front of a BG wins", out[0], 10);
    desc.abgs = NULL; desc.n_abgs = 0;

    printf("a window hiding the 3D layer's bit\n");
    memset(&win, 0, sizeof(win));
    win.win0.enabled = 1;
    win.win0.x1 = 0; win.win0.x2 = W;
    win.win0.y1 = 0; win.win0.y2 = H;
    win.win0.layers = G2D_LAYER_OBJ;   /* no BG0: the 3D layer is hidden */
    desc.windows = &win;
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("the backdrop shows through", out[0], 1);
    BW_CHECK_EQ("and it is the backdrop's green", out[1], 2);
    desc.windows = NULL;

    /*
     * The 3D layer blends by its own per-pixel alpha, never by EVA/EVB:
     * an opaque 3D pixel (alpha 31) over a second target is
     * (3D * 32 + 2D * 0) / 32, the 3D pixel itself.
     */
    printf("an opaque 3D pixel over a second target\n");
    blend.effect = G2D_BLEND_ALPHA;
    blend.first = G2D_LAYER_BG0;
    blend.second = G2D_LAYER_BD;
    blend.eva = 8; blend.evb = 8; blend.evy = 0;
    desc.blend = &blend;
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("opaque 3D red", out[0], 10);
    BW_CHECK_EQ("opaque 3D green", out[1], 20);
    BW_CHECK_EQ("opaque 3D blue", out[2], 30);

    /*
     * A translucent 3D pixel, alpha 15 (8-bit 123 folds to 15), over the
     * backdrop as second target, by hand in the 6-bit stage:
     *   3D:  252, 0, 0  -> 6-bit 62, 0, 0
     *   2D:  backdrop set to the 5-bit colour (0, 0, 31) -> 0, 0, 63
     *   (62 * 16 + 0 * 16) / 32 = 31,  (0 * 16 + 63 * 16) / 32 = 31
     *   31 * 255 / 63 = 125 on red and blue, green 0.
     * The effect field is brightness, and the first target is BG1: the
     * 3D blend ignores both.
     */
    printf("a translucent 3D pixel blends by its own alpha\n");
    rgb3d[0] = 252; rgb3d[1] = 0; rgb3d[2] = 0; rgb3d[3] = 123;
    bdc[0].r = 0; bdc[0].g = 0; bdc[0].b = 255;
    blend.effect = G2D_BLEND_BRIGHT_UP;
    blend.first = G2D_LAYER_BG1;
    blend.evy = 16;
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("3D blend red", out[0], 125);
    BW_CHECK_EQ("3D blend green", out[1], 0);
    BW_CHECK_EQ("3D blend blue", out[2], 125);

    printf("no second target beneath: ordinary rules for BG0\n");
    blend.second = G2D_LAYER_BG1;
    blend.first = G2D_LAYER_BG0;
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("brightness up to white", out[0], 255);
    BW_CHECK_EQ("brightness up to white, green", out[1], 255);
    blend.first = G2D_LAYER_BG1;
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("translucent 3D shown as is", out[0], 252);
    BW_CHECK_EQ("translucent 3D shown as is, blue", out[2], 0);
    desc.blend = NULL;

    printf("alpha 0 in the 3D frame is transparent\n");
    rgb3d[3] = 0;
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("the backdrop shows", out[2], 255);
    BW_CHECK_EQ("the backdrop shows, red", out[0], 0);
    desc.abgs = &abg; desc.n_abgs = 1;
    abg.priority = 1; l3.priority = 0;
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("a BG behind the 3D layer shows", out[0], 70);
    desc.abgs = NULL; desc.n_abgs = 0;
    rgb3d[0] = 10; rgb3d[1] = 20; rgb3d[2] = 30; rgb3d[3] = 255;

    printf("BG0HOFS scrolls the 3D layer\n");
    rgb3d[2u * 4u + 0u] = 99;
    l3.hofs = 2;
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("column 2 shows at x 0", out[0], 99);
    BW_CHECK_EQ("x 2 shows column 4: past the buffer, backdrop",
                out[2u * 4u + 2u], 255);
    l3.hofs = 511;
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("hofs 511: x 0 is column 511, transparent", out[2], 255);
    BW_CHECK_EQ("hofs 511: x 3 is column 2", out[3u * 4u], 99);
    l3.hofs = 0;

    BW_REPORT("test_compose3d");
}
