/*
 * tests/test_affine.c -- the OBJ affine path against hand-computed pixels.
 *
 * No oracle and no game data: every expected value below is worked out on
 * paper from the hardware rule engine/g2d/affine.c implements, so this
 * test is evidence about the arithmetic itself rather than agreement with
 * another implementation of it.
 *
 * The source block used throughout is a 4x4 whose texel (r, c) holds
 * 4r + c + 1, so every texel is distinguishable and a wrong sample is a
 * wrong number rather than a wrong-looking picture:
 *
 *      1  2  3  4
 *      5  6  7  8
 *      9 10 11 12
 *     13 14 15 16
 */

#include <stdio.h>
#include <string.h>

#include "g2d.h"
#include "port_test.h"

static u8 g_src4[16] = {
     1,  2,  3,  4,
     5,  6,  7,  8,
     9, 10, 11, 12,
    13, 14, 15, 16
};

static int check_raster(const char *label, const u8 *got, const u8 *want,
                        u32 w, u32 h)
{
    u32 i;

    bw_checks++;
    for (i = 0; i < w * h; i++) {
        if (got[i] != want[i]) {
            bw_failures++;
            printf("  FAIL %-40s first difference at (%u, %u): "
                   "got %u, want %u\n",
                   label, i % w, i / w, got[i], want[i]);
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    G2dAffine m;
    u8 out[64];

    /* ---- the fixed-point sine table --------------------------------- */
    /*
     * A 16-bit binary angle: 0x4000 is a quarter turn.  The table holds
     * round(sin(a) * 4096), so the quarter-turn entries are exact and the
     * eighth-turn one is round(4096 / sqrt(2)) = 2896.
     */
    BW_CHECK_EQ("sin(0)", g2d_sin_fx(0x0000), 0);
    BW_CHECK_EQ("sin(1/8 turn)", g2d_sin_fx(0x2000), 2896);
    BW_CHECK_EQ("sin(1/4 turn)", g2d_sin_fx(0x4000), 4096);
    BW_CHECK_EQ("sin(1/2 turn)", g2d_sin_fx(0x8000), 0);
    BW_CHECK_EQ("sin(3/4 turn)", g2d_sin_fx(0xC000), -4096);
    BW_CHECK_EQ("cos(0)", g2d_cos_fx(0x0000), 4096);
    BW_CHECK_EQ("cos(1/4 turn)", g2d_cos_fx(0x4000), 0);
    BW_CHECK_EQ("cos(1/2 turn)", g2d_cos_fx(0x8000), -4096);

    /* ---- the 8.8 parameters ----------------------------------------- */
    g2d_affine_from_srt(G2D_FX_ONE, G2D_FX_ONE, 0, &m);
    BW_CHECK_EQ("identity PA", m.pa, 256);
    BW_CHECK_EQ("identity PB", m.pb, 0);
    BW_CHECK_EQ("identity PC", m.pc, 0);
    BW_CHECK_EQ("identity PD", m.pd, 256);
    BW_CHECK_EQ("identity is identity",
                g2d_affine_is_identity(G2D_FX_ONE, G2D_FX_ONE, 0), 1);

    /*
     * A 2x scale is PA = PD = 128: the inverse map steps half a texel per
     * output pixel, which draws the art twice as large.
     */
    g2d_affine_from_srt(2 * G2D_FX_ONE, 2 * G2D_FX_ONE, 0, &m);
    BW_CHECK_EQ("2x scale PA", m.pa, 128);
    BW_CHECK_EQ("2x scale PD", m.pd, 128);
    BW_CHECK_EQ("2x scale is not identity",
                g2d_affine_is_identity(2 * G2D_FX_ONE, 2 * G2D_FX_ONE, 0), 0);

    /* A quarter turn at unit scale: (0, 256, -256, 0). */
    g2d_affine_from_srt(G2D_FX_ONE, G2D_FX_ONE, 0x4000, &m);
    BW_CHECK_EQ("90 degree PA", m.pa, 0);
    BW_CHECK_EQ("90 degree PB", m.pb, 256);
    BW_CHECK_EQ("90 degree PC", m.pc, -256);
    BW_CHECK_EQ("90 degree PD", m.pd, 0);

    /* ---- a quarter turn of the 4x4, single size ---------------------- */
    /*
     * Reference point (2, 2) in a 4x4 canvas, source reference (2, 2).
     * dx = ox - 2, dy = oy - 2; tx = dy, ty = -dx; so the sampled texel
     * is src[4 - ox][oy], and column ox = 0 asks for row 4, which does not
     * exist.  A rotation about the centre of an even-sized block loses a
     * column: the hardware's reference point is a texel corner, not the
     * block's true centre, and the single-size canvas cannot hold the
     * overhang.  That lost column is the reason double size exists.
     */
    {
        static const u8 want[16] = {
            0, 13,  9, 5,
            0, 14, 10, 6,
            0, 15, 11, 7,
            0, 16, 12, 8
        };
        u32 ow, oh;

        g2d_affine_from_srt(G2D_FX_ONE, G2D_FX_ONE, 0x4000, &m);
        g2d_obj_affine_size(4, 4, 0, &ow, &oh);
        BW_CHECK_EQ("single-size canvas width", ow, 4);
        BW_CHECK_EQ("single-size canvas height", oh, 4);
        BW_REQUIRE("render 90 single size",
                   g2d_obj_affine_render(g_src4, 4, 4, &m, 0, out) == 0);
        check_raster("90 degrees, single size", out, want, 4, 4);
    }

    /* ---- the same quarter turn, double size -------------------------- */
    /*
     * The canvas is 8x8 with the reference point at (4, 4) and the source
     * reference still at (2, 2): tx = dy + 2 = oy - 2, ty = -dx + 2 =
     * 6 - ox.  Every one of the sixteen texels now lands somewhere on the
     * canvas, in the block ox 3..6, oy 2..5, and nothing is clipped.
     */
    {
        static const u8 want[64] = {
            0, 0, 0,  0,  0,  0, 0, 0,
            0, 0, 0,  0,  0,  0, 0, 0,
            0, 0, 0, 13,  9,  5, 1, 0,
            0, 0, 0, 14, 10,  6, 2, 0,
            0, 0, 0, 15, 11,  7, 3, 0,
            0, 0, 0, 16, 12,  8, 4, 0,
            0, 0, 0,  0,  0,  0, 0, 0,
            0, 0, 0,  0,  0,  0, 0, 0
        };
        u32 ow, oh;

        g2d_affine_from_srt(G2D_FX_ONE, G2D_FX_ONE, 0x4000, &m);
        g2d_obj_affine_size(4, 4, 1, &ow, &oh);
        BW_CHECK_EQ("double-size canvas width", ow, 8);
        BW_CHECK_EQ("double-size canvas height", oh, 8);
        BW_REQUIRE("render 90 double size",
                   g2d_obj_affine_render(g_src4, 4, 4, &m, 1, out) == 0);
        check_raster("90 degrees, double size", out, want, 8, 8);
    }

    /* ---- a 2x scale, double size ------------------------------------- */
    /*
     * PA = PD = 128, so tx = floor(dx / 2) + 2 with dx = ox - 4: the
     * column map over ox 0..7 is 0,0,1,1,2,2,3,3 and the row map is the
     * same.  Every texel is doubled and the whole 8x8 canvas is covered,
     * which is also the check that the shift floors rather than truncating
     * towards zero: ox = 1 gives floor(-3 / 2) = -2, column 0, not -1.
     */
    {
        static const u8 want[64] = {
             1,  1,  2,  2,  3,  3,  4,  4,
             1,  1,  2,  2,  3,  3,  4,  4,
             5,  5,  6,  6,  7,  7,  8,  8,
             5,  5,  6,  6,  7,  7,  8,  8,
             9,  9, 10, 10, 11, 11, 12, 12,
             9,  9, 10, 10, 11, 11, 12, 12,
            13, 13, 14, 14, 15, 15, 16, 16,
            13, 13, 14, 14, 15, 15, 16, 16
        };

        g2d_affine_from_srt(2 * G2D_FX_ONE, 2 * G2D_FX_ONE, 0, &m);
        BW_REQUIRE("render 2x double size",
                   g2d_obj_affine_render(g_src4, 4, 4, &m, 1, out) == 0);
        check_raster("2x scale, double size", out, want, 8, 8);
    }

    /* ---- the cell-level resample ------------------------------------- */
    /*
     * Identity: the output is the source with the two-pixel truncation
     * margin on every side, and the origin moves by that margin.
     */
    {
        G2dRaster r;

        BW_REQUIRE("resample identity",
                   g2d_affine_resample(g_src4, 4, 4, 0, 0,
                                       G2D_FX_ONE, G2D_FX_ONE, 0, &r) == 0);
        BW_CHECK_EQ("resample identity width", r.w, 8);
        BW_CHECK_EQ("resample identity height", r.h, 8);
        BW_CHECK_EQ("resample identity origin x", r.ox, 2);
        BW_CHECK_EQ("resample identity origin y", r.oy, 2);
        BW_CHECK_EQ("resample identity (0,0)", r.px[2 * 8 + 2], 1);
        BW_CHECK_EQ("resample identity (3,3)", r.px[5 * 8 + 5], 16);
        BW_CHECK_EQ("resample identity margin", r.px[0], 0);
        g2d_raster_free(&r);
    }

    /*
     * A quarter turn of a 2x1 raster whose origin is its top-left corner.
     * The forward transform of the corners (0,0), (2,0), (0,1), (2,1) is
     * (0,0), (0,2), (-1,0), (-1,2); with the two-pixel margin the canvas
     * is x in [-3, 2), y in [-2, 4), so 5x6 with the origin at (3, 2).
     * The inverse map sends local (0, 0) to texel (0, 0) and local (0, 1)
     * to texel (1, 0), and nothing else lands inside the source.
     */
    {
        static const u8 src2[2] = { 1, 2 };
        G2dRaster r;
        u32 i, nonzero = 0;

        BW_REQUIRE("resample 90",
                   g2d_affine_resample(src2, 2, 1, 0, 0,
                                       G2D_FX_ONE, G2D_FX_ONE, 0x4000, &r) == 0);
        BW_CHECK_EQ("resample 90 width", r.w, 5);
        BW_CHECK_EQ("resample 90 height", r.h, 6);
        BW_CHECK_EQ("resample 90 origin x", r.ox, 3);
        BW_CHECK_EQ("resample 90 origin y", r.oy, 2);
        BW_CHECK_EQ("resample 90 at (3,2)", r.px[2 * 5 + 3], 1);
        BW_CHECK_EQ("resample 90 at (3,3)", r.px[3 * 5 + 3], 2);
        for (i = 0; i < r.w * r.h; i++) {
            if (r.px[i] != 0) {
                nonzero++;
            }
        }
        BW_CHECK_EQ("resample 90 covered texels", nonzero, 2);
        g2d_raster_free(&r);
    }

    BW_REPORT("test_affine");
}
