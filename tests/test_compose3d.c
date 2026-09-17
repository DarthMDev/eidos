/*
 * tests/test_compose3d.c -- a 3D layer inside the 2D per-pixel compositor,
 * synthetically.
 *
 * No SDL, no game data, no oracle: a 4x4 3D buffer of one flat colour and a
 * one-tile affine BG stand in for the two layers, and every expected pixel
 * is computed on paper (or, for the blend, by calling the same blend helper
 * the compositor calls).  The point is the *stack*, not the picture: the 3D
 * output is an ordinary layer, so a 2D BG can sit in front of or behind it,
 * a window can hide it, and a colour effect can blend with a 3D pixel.
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
 *   - alpha blending where the topmost pixel is the 3D layer and the one
 *     beneath is the backdrop produces exactly g2d_blend_alpha's result; a
 *     first target the 3D layer is not part of does not blend.
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
    u8         expect[3];
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

    printf("alpha blending with a 3D pixel\n");
    expect[0] = 10; expect[1] = 20; expect[2] = 30;
    blend.effect = G2D_BLEND_ALPHA;
    blend.first = G2D_LAYER_BG0;
    blend.second = G2D_LAYER_BD;
    blend.eva = 8; blend.evb = 8; blend.evy = 0;
    desc.blend = &blend;
    g2d_frame_compose_desc(&desc, W, H, out);
    g2d_blend_alpha(expect, (const u8[3]){ 1, 2, 3 }, 8, 8, expect);
    BW_CHECK_EQ("blend red", out[0], expect[0]);
    BW_CHECK_EQ("blend green", out[1], expect[1]);
    BW_CHECK_EQ("blend blue", out[2], expect[2]);

    printf("a first target the 3D layer is not part of\n");
    blend.first = G2D_LAYER_BG1;   /* the 3D layer is BG0 */
    g2d_frame_compose_desc(&desc, W, H, out);
    BW_CHECK_EQ("no blend red", out[0], 10);
    BW_CHECK_EQ("no blend green", out[1], 20);
    desc.blend = NULL;

    BW_REPORT("test_compose3d");
}
