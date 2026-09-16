/*
 * engine/g2d/affine.c -- the DS OBJ/BG rotation and scaling arithmetic.
 *
 * Three things live here, and the first is the reason the other two are
 * integer-only:
 *
 *   1. a fixed-point sine table, 4096 entries over one turn, fx32 valued
 *      (4096 == 1.0), indexed by a 16-bit binary angle shifted right by
 *      four.  That is the index convention the NNS fixed-point helpers
 *      use and the one a NANR frame's rotZ (1/65536 of a turn) feeds
 *      directly.  The table is built once, by evaluating the sine series
 *      in 2.30 fixed point with 64-bit intermediates: no float, no libm,
 *      and no table literal to get wrong.  Every entry agrees with
 *      round(sin(2*pi*i/4096) * 4096) for all 4096 indices.
 *
 *   2. G2dAffine, the four 8.8 fixed-point parameters PA/PB/PC/PD the DS
 *      OBJ engine holds per affine slot (and the BG engine holds as
 *      BGxPA..BGxPD), computed from a frame's (sx, sy, rotZ).
 *
 *   3. the per-pixel resample itself: for every pixel of the output
 *      canvas, offset from the reference point, run the offset through
 *      the 2x2 matrix, shift right eight, and take the source texel at
 *      that offset from the source reference point.  Nearest neighbour is
 *      not a choice here: the matrix maps to a whole texel because the
 *      fractional bits are discarded, which is what the hardware does.
 *
 * **Why the parameters are the inverse transform.**  The DS does not
 * forward-map source pixels onto the screen; it walks output pixels and
 * asks each one which texel it shows.  So PA..PD hold S^-1 R(-theta) when
 * the intended picture is R(theta) S applied to the art:
 *
 *     PA =  cos(theta) * 4096 / sx        PB =  sin(theta) * 4096 / sx
 *     PC = -sin(theta) * 4096 / sy        PD =  cos(theta) * 4096 / sy
 *
 * each then in 8.8, so a value of 256 is 1.0.  A frame with sx = sy =
 * 4096 and rotZ = 0 gives (256, 0, 0, 256), the identity, and a frame
 * with sx = 8192 gives PA = 128: the output steps half a texel per pixel,
 * which is a sprite drawn at double width.
 *
 * **Where the reference point is.**  For an OBJ the hardware's reference
 * point is the centre of the drawn canvas, and the source reference point
 * is the centre of the object's own WxH texel block.  With attr0 bits 8-9
 * == 3 (the double-size affine mode) the drawn canvas is 2W x 2H instead
 * of W x H, still centred on the same point, which is the whole purpose
 * of the mode: a rotated 64x64 object needs 91 pixels of diagonal and
 * would otherwise be clipped to its own box.  aletheia's docs/ASSETS.md
 * records the same offset from the other end of the pipe: func_02066E30
 * shifts a double-size entry's translation by half its own width and
 * height before shifting it left 12 into the geometry engine's translate
 * register, which is exactly the displacement of a 2Wx2H canvas whose
 * centre must stay put while its top-left moves.
 *
 * **Fixed point, and the divergence from rig.py.**  aletheia's
 * tools/assets/rig.py inverse-maps in double precision with math.cos /
 * math.sin and rounds the source coordinate to nearest.  This engine
 * follows the hardware: a 12-bit sine table, an 8.8 matrix, and a
 * floor-truncating shift, so the source coordinate it lands on can be one
 * texel away from rig.py's on pixels near a texel boundary.  Where the
 * two differ the engine is the reference, because the hardware is: see
 * README.md.  rig.py is not modified.
 *
 * Nothing here is derived from any other decompilation, port or emulator;
 * the parameter semantics are public NDS hardware documentation and the
 * double-size reference point is corroborated by the matched code cited
 * above.
 */

#include <stdlib.h>
#include <string.h>

#include "g2d.h"

/* ------------------------------------------------------------------ */
/* The fixed-point sine table                                          */
/* ------------------------------------------------------------------ */

#define SIN_TABLE_SIZE 4096
#define SIN_QUARTER    1024

/* pi/2 in 2.30 fixed point: round(pi/2 * 2^30). */
#define PI_OVER_2_Q30  1686629713

static s16 g_sin_table[SIN_TABLE_SIZE];
static int g_sin_ready;

/* (a * b) >> 30 for two 2.30 values, with a 64-bit intermediate. */
static s64 q30_mul(s64 a, s64 b)
{
    return (a * b) >> 30;
}

/*
 * sin(pi/2 * j / 1024) as fx32, for j in 0..1024, by the alternating
 * series x - x^3/3! + x^5/5! - x^7/7! + x^9/9! evaluated in 2.30.  Every
 * term is computed as a positive magnitude so the divisions truncate the
 * same way on every conforming implementation.  Nine terms is far more
 * than the 1/4096 output needs; the series is nowhere near its error
 * bound on [0, pi/2].
 */
static s16 sin_quarter_fx(u32 j)
{
    s64 x = ((s64)PI_OVER_2_Q30 * (s64)j) / SIN_QUARTER;
    s64 x2 = q30_mul(x, x);
    s64 term = x;
    s64 acc = x;

    term = q30_mul(term, x2) / 6;    acc -= term;
    term = q30_mul(term, x2) / 20;   acc += term;
    term = q30_mul(term, x2) / 42;   acc -= term;
    term = q30_mul(term, x2) / 72;   acc += term;

    /* 2.30 to fx32 (1.19.12), rounded to nearest. */
    return (s16)((acc + (1 << 17)) >> 18);
}

static void sin_table_init(void)
{
    u32 i;

    for (i = 0; i < SIN_TABLE_SIZE; i++) {
        u32 quadrant = i / SIN_QUARTER;
        u32 j = i % SIN_QUARTER;
        s16 v;

        switch (quadrant) {
        case 0:  v = sin_quarter_fx(j); break;
        case 1:  v = sin_quarter_fx(SIN_QUARTER - j); break;
        case 2:  v = (s16)(-sin_quarter_fx(j)); break;
        default: v = (s16)(-sin_quarter_fx(SIN_QUARTER - j)); break;
        }
        g_sin_table[i] = v;
    }
    g_sin_ready = 1;
}

s32 g2d_sin_fx(u16 angle)
{
    if (!g_sin_ready) {
        sin_table_init();
    }
    return g_sin_table[(angle >> 4) & (SIN_TABLE_SIZE - 1)];
}

s32 g2d_cos_fx(u16 angle)
{
    /* cos(a) = sin(a + quarter turn); the quarter turn is 0x4000. */
    return g2d_sin_fx((u16)(angle + 0x4000u));
}

/* ------------------------------------------------------------------ */
/* The 8.8 parameters                                                  */
/* ------------------------------------------------------------------ */

int g2d_affine_is_identity(s32 sx, s32 sy, u16 rot_z)
{
    return sx == G2D_FX_ONE && sy == G2D_FX_ONE && rot_z == 0;
}

void g2d_affine_from_srt(s32 sx, s32 sy, u16 rot_z, G2dAffine *out)
{
    s32 c = g2d_cos_fx(rot_z);
    s32 s = g2d_sin_fx(rot_z);

    /*
     * A zero scale has no inverse and no hardware meaning (the OBJ would
     * be infinitely wide); treat it as 1.0, the same tolerance rig.py
     * applies with its `sx = sx or 4096`.
     */
    if (sx == 0) {
        sx = G2D_FX_ONE;
    }
    if (sy == 0) {
        sy = G2D_FX_ONE;
    }

    out->pa = (s16)(((s64)c * 256) / sx);
    out->pb = (s16)(((s64)s * 256) / sx);
    out->pc = (s16)(((s64)-s * 256) / sy);
    out->pd = (s16)(((s64)c * 256) / sy);
}

/*
 * The hardware's shift: the product of an 8.8 parameter and an integer
 * pixel offset is 8.8, and the fractional bits are dropped rather than
 * rounded.  Spelled as an explicit floor so a negative offset behaves the
 * same whatever the host makes of `>>` on a negative value.
 */
static s32 fx8_floor(s64 v)
{
    return (s32)((v >= 0) ? (v >> 8) : -(((-v) + 255) >> 8));
}

void g2d_affine_map(const G2dAffine *m, s32 dx, s32 dy, s32 *sx, s32 *sy)
{
    *sx = fx8_floor((s64)m->pa * dx + (s64)m->pb * dy);
    *sy = fx8_floor((s64)m->pc * dx + (s64)m->pd * dy);
}

/* ------------------------------------------------------------------ */
/* One OBJ through the matrix                                          */
/* ------------------------------------------------------------------ */

void g2d_obj_affine_size(u32 w, u32 h, int double_size, u32 *out_w, u32 *out_h)
{
    *out_w = double_size ? w * 2u : w;
    *out_h = double_size ? h * 2u : h;
}

int g2d_obj_affine_render(const u8 *src, u32 w, u32 h, const G2dAffine *m,
                          int double_size, u8 *out)
{
    u32 ow, oh, ox, oy;
    s32 half_ow, half_oh, half_w, half_h;

    if (src == NULL || out == NULL || w == 0 || h == 0) {
        return -1;
    }
    g2d_obj_affine_size(w, h, double_size, &ow, &oh);
    half_ow = (s32)(ow / 2u);
    half_oh = (s32)(oh / 2u);
    half_w = (s32)(w / 2u);
    half_h = (s32)(h / 2u);

    memset(out, 0, (size_t)ow * oh);
    for (oy = 0; oy < oh; oy++) {
        for (ox = 0; ox < ow; ox++) {
            s32 dx = (s32)ox - half_ow;
            s32 dy = (s32)oy - half_oh;
            s32 tx, ty;

            g2d_affine_map(m, dx, dy, &tx, &ty);
            tx += half_w;
            ty += half_h;
            if (tx < 0 || ty < 0 || (u32)tx >= w || (u32)ty >= h) {
                continue;
            }
            out[(size_t)oy * ow + ox] = src[(size_t)ty * w + tx];
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* One cell raster through the matrix, about the cell origin            */
/* ------------------------------------------------------------------ */

/*
 * The forward transform, scale then rotate, in the same fixed point: used
 * only to size the output canvas, never to place a pixel.  The per-pixel
 * work is the inverse map above.
 */
static void srt_forward(s32 x, s32 y, s32 sx, s32 sy, s32 c, s32 s,
                        s32 *out_x, s32 *out_y)
{
    s64 fx = ((s64)x * sx) / G2D_FX_ONE;
    s64 fy = ((s64)y * sy) / G2D_FX_ONE;

    *out_x = (s32)((fx * c - fy * s) / G2D_FX_ONE);
    *out_y = (s32)((fx * s + fy * c) / G2D_FX_ONE);
}

int g2d_affine_resample(const u8 *src, u32 sw, u32 sh, s32 src_ox, s32 src_oy,
                        s32 sx, s32 sy, u16 rot_z, G2dRaster *out)
{
    G2dAffine m;
    s32 c = g2d_cos_fx(rot_z);
    s32 s = g2d_sin_fx(rot_z);
    s32 lx0 = -src_ox;
    s32 ly0 = -src_oy;
    s32 lx1 = lx0 + (s32)sw;
    s32 ly1 = ly0 + (s32)sh;
    s32 cx[4], cy[4];
    s32 minx, maxx, miny, maxy;
    const s32 margin = 2;   /* truncation slop at the output edge */
    u32 ow, oh, ox, oy;
    int i;

    memset(out, 0, sizeof(*out));
    if (src == NULL || sw == 0 || sh == 0) {
        return -1;
    }
    g2d_affine_from_srt(sx, sy, rot_z, &m);

    srt_forward(lx0, ly0, sx, sy, c, s, &cx[0], &cy[0]);
    srt_forward(lx1, ly0, sx, sy, c, s, &cx[1], &cy[1]);
    srt_forward(lx0, ly1, sx, sy, c, s, &cx[2], &cy[2]);
    srt_forward(lx1, ly1, sx, sy, c, s, &cx[3], &cy[3]);
    minx = maxx = cx[0];
    miny = maxy = cy[0];
    for (i = 1; i < 4; i++) {
        if (cx[i] < minx) { minx = cx[i]; }
        if (cx[i] > maxx) { maxx = cx[i]; }
        if (cy[i] < miny) { miny = cy[i]; }
        if (cy[i] > maxy) { maxy = cy[i]; }
    }
    minx -= margin;
    miny -= margin;
    maxx += margin;
    maxy += margin;

    ow = (u32)(maxx - minx);
    oh = (u32)(maxy - miny);
    if (ow == 0) { ow = 1; }
    if (oh == 0) { oh = 1; }

    out->px = (u8 *)calloc((size_t)ow * oh, 1);
    if (out->px == NULL) {
        return -1;
    }
    out->w = ow;
    out->h = oh;
    out->ox = -minx;
    out->oy = -miny;

    for (oy = 0; oy < oh; oy++) {
        for (ox = 0; ox < ow; ox++) {
            s32 lx = minx + (s32)ox;
            s32 ly = miny + (s32)oy;
            s32 tx, ty;

            g2d_affine_map(&m, lx, ly, &tx, &ty);
            tx += src_ox;
            ty += src_oy;
            if (tx < 0 || ty < 0 || (u32)tx >= sw || (u32)ty >= sh) {
                continue;
            }
            out->px[(size_t)oy * ow + ox] = src[(size_t)ty * sw + tx];
        }
    }
    return 0;
}

void g2d_raster_free(G2dRaster *r)
{
    free(r->px);
    r->px = NULL;
    r->w = 0;
    r->h = 0;
}
