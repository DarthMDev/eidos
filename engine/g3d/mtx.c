/*
 * engine/g3d/mtx.c -- the geometry engine's fixed point and matrix stack.
 *
 * Provenance (see g3d.h's header for the full rule):
 *
 *   - fx32 (1.19.12) and FX_MUL / FX_DIV are sdk/types.h's, which is the
 *     format src/matched/unit_02048AD0.c's VecFx32 is three of.  Every
 *     product below keeps the 64-bit intermediate and shifts right
 *     arithmetically, so a negative result truncates towards minus
 *     infinity, which is what the DS runtime helper's smull/asr pair
 *     does.  Portability rule 3: no fixed-point value is routed through
 *     a float here, and this file includes no math.h.
 *   - the matrix modes, the stack depths (one projection entry, one
 *     texture entry, 31 usable position/vector entries with the 32nd the
 *     overflow slot), the sticky over/underflow flag, and the rule that
 *     MTX_MULT_* multiplies the parameter on the left of the current
 *     matrix are public NDS hardware documentation.
 *   - the row-vector convention (v' = v * M, translation in row 3) is
 *     the same documentation's, and is why a 4x3 matrix is the first
 *     three rows plus a translation row.
 *   - the sine and cosine are engine/g2d/affine.c's 4096-entry fx32
 *     table, built by evaluating the series in 2.30 fixed point.  The 3D
 *     engine adds no second trigonometry implementation to the port.
 */

#include <string.h>

#include "g2d.h"
#include "g3d.h"

/* ------------------------------------------------------------------ */
/* Matrices                                                            */
/* ------------------------------------------------------------------ */

void g3d_mtx_identity(G3dMtx *out)
{
    memset(out->m, 0, sizeof(out->m));
    out->m[0] = out->m[5] = out->m[10] = out->m[15] = FX32_ONE;
}

void g3d_mtx_mul(G3dMtx *out, const G3dMtx *a, const G3dMtx *b)
{
    G3dMtx r;
    u32 i, j, k;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            s64 acc = 0;
            for (k = 0; k < 4; k++) {
                acc += (s64)a->m[i * 4 + k] * (s64)b->m[k * 4 + j];
            }
            r.m[i * 4 + j] = (fx32)(acc >> FX32_SHIFT);
        }
    }
    *out = r;
}

void g3d_mtx_xform(const G3dMtx *m, fx32 x, fx32 y, fx32 z,
                   fx32 *ox, fx32 *oy, fx32 *oz, fx32 *ow)
{
    s64 rx, ry, rz, rw;

    rx = (s64)x * m->m[0] + (s64)y * m->m[4] + (s64)z * m->m[8];
    ry = (s64)x * m->m[1] + (s64)y * m->m[5] + (s64)z * m->m[9];
    rz = (s64)x * m->m[2] + (s64)y * m->m[6] + (s64)z * m->m[10];
    rw = (s64)x * m->m[3] + (s64)y * m->m[7] + (s64)z * m->m[11];

    *ox = (fx32)((rx >> FX32_SHIFT) + m->m[12]);
    *oy = (fx32)((ry >> FX32_SHIFT) + m->m[13]);
    *oz = (fx32)((rz >> FX32_SHIFT) + m->m[14]);
    *ow = (fx32)((rw >> FX32_SHIFT) + m->m[15]);
}

void g3d_mtx_translate(G3dMtx *out, fx32 x, fx32 y, fx32 z)
{
    g3d_mtx_identity(out);
    out->m[12] = x;
    out->m[13] = y;
    out->m[14] = z;
}

void g3d_mtx_scale(G3dMtx *out, fx32 x, fx32 y, fx32 z)
{
    g3d_mtx_identity(out);
    out->m[0] = x;
    out->m[5] = y;
    out->m[10] = z;
}

/*
 * MTX_FRUSTUM, in the row-vector convention: the perspective divide's
 * w comes out of column 3, so the -1 sits at m[11] rather than at m[14].
 */
void g3d_mtx_frustum(G3dMtx *out, fx32 t, fx32 b, fx32 l, fx32 r,
                     fx32 n, fx32 f)
{
    fx32 dx = r - l, dy = t - b, dz = f - n;

    memset(out->m, 0, sizeof(out->m));
    if (dx == 0 || dy == 0 || dz == 0) {
        g3d_mtx_identity(out);
        return;
    }
    out->m[0]  = FX_DIV(2 * n, dx);
    out->m[5]  = FX_DIV(2 * n, dy);
    out->m[8]  = FX_DIV(r + l, dx);
    out->m[9]  = FX_DIV(t + b, dy);
    out->m[10] = -FX_DIV(f + n, dz);
    out->m[11] = -FX32_ONE;
    out->m[14] = -FX_DIV(2 * FX_MUL(f, n), dz);
}

void g3d_mtx_perspective(G3dMtx *out, u16 fovy, fx32 aspect, fx32 n, fx32 f)
{
    /* tan(fovy/2) out of the shared table, in fx32. */
    u16 half = (u16)(fovy / 2);
    fx32 s = (fx32)g2d_sin_fx(half);
    fx32 c = (fx32)g2d_cos_fx(half);
    fx32 t, b, l, r;

    if (c == 0) {
        g3d_mtx_identity(out);
        return;
    }
    t = FX_MUL(n, FX_DIV(s, c));
    b = -t;
    r = FX_MUL(t, aspect);
    l = -r;
    g3d_mtx_frustum(out, t, b, l, r, n, f);
}

/* Integer square root of a 64-bit value, by bit pairs: no math.h. */
static u64 g3d_isqrt64(u64 v)
{
    u64 rem = 0, root = 0;
    int i;

    for (i = 0; i < 32; i++) {
        root <<= 1;
        rem = (rem << 2) | (v >> 62);
        v <<= 2;
        if (root < rem) {
            rem -= root + 1;
            root += 2;
        }
    }
    return root >> 1;
}

static fx32 g3d_len(fx32 x, fx32 y, fx32 z)
{
    s64 sq = ((s64)x * x + (s64)y * y + (s64)z * z) >> FX32_SHIFT;

    if (sq <= 0) {
        return 0;
    }
    return (fx32)g3d_isqrt64((u64)sq << FX32_SHIFT);
}

static void g3d_norm(fx32 *x, fx32 *y, fx32 *z)
{
    fx32 len = g3d_len(*x, *y, *z);

    if (len == 0) {
        return;
    }
    *x = FX_DIV(*x, len);
    *y = FX_DIV(*y, len);
    *z = FX_DIV(*z, len);
}

static void g3d_cross(fx32 ax, fx32 ay, fx32 az, fx32 bx, fx32 by, fx32 bz,
                      fx32 *ox, fx32 *oy, fx32 *oz)
{
    *ox = FX_MUL(ay, bz) - FX_MUL(az, by);
    *oy = FX_MUL(az, bx) - FX_MUL(ax, bz);
    *oz = FX_MUL(ax, by) - FX_MUL(ay, bx);
}

void g3d_mtx_lookat(G3dMtx *out, const G3dVec *eye, const G3dVec *up,
                    const G3dVec *at)
{
    fx32 zx = eye->x - at->x, zy = eye->y - at->y, zz = eye->z - at->z;
    fx32 xx, xy, xz, yx, yy, yz;

    g3d_norm(&zx, &zy, &zz);
    g3d_cross(up->x, up->y, up->z, zx, zy, zz, &xx, &xy, &xz);
    g3d_norm(&xx, &xy, &xz);
    g3d_cross(zx, zy, zz, xx, xy, xz, &yx, &yy, &yz);

    out->m[0] = xx;  out->m[1] = yx;  out->m[2]  = zx;  out->m[3]  = 0;
    out->m[4] = xy;  out->m[5] = yy;  out->m[6]  = zy;  out->m[7]  = 0;
    out->m[8] = xz;  out->m[9] = yz;  out->m[10] = zz;  out->m[11] = 0;
    out->m[12] = -(FX_MUL(eye->x, xx) + FX_MUL(eye->y, xy) + FX_MUL(eye->z, xz));
    out->m[13] = -(FX_MUL(eye->x, yx) + FX_MUL(eye->y, yy) + FX_MUL(eye->z, yz));
    out->m[14] = -(FX_MUL(eye->x, zx) + FX_MUL(eye->y, zy) + FX_MUL(eye->z, zz));
    out->m[15] = FX32_ONE;
}

/* ------------------------------------------------------------------ */
/* The stack                                                           */
/* ------------------------------------------------------------------ */

void g3d_mtx_stack_init(G3dMtxStack *st)
{
    memset(st, 0, sizeof(*st));
    g3d_mtx_identity(&st->proj);
    g3d_mtx_identity(&st->pos);
    g3d_mtx_identity(&st->vec);
    g3d_mtx_identity(&st->tex);
    g3d_mtx_identity(&st->proj_save);
    g3d_mtx_identity(&st->tex_save);
    g3d_mtx_identity(&st->clip);
    st->mode = G3D_MTX_MODE_POSITION;
    st->clip_dirty = 1;
}

void g3d_mtx_stack_set_mode(G3dMtxStack *st, u32 mode)
{
    st->mode = mode & 3u;
}

/*
 * PUSH.  In projection and texture mode the "stack" is one slot deep and
 * the pointer the hardware keeps for it is a single bit; this engine
 * keeps the saved matrix and does not model the bit, because no display
 * list in the corpus depends on a projection-stack overflow.  In the two
 * position modes the vector matrix rides with the position matrix, which
 * is the whole difference between modes 1 and 2.
 */
void g3d_mtx_stack_push(G3dMtxStack *st)
{
    switch (st->mode) {
    case G3D_MTX_MODE_PROJECTION:
        st->proj_save = st->proj;
        break;
    case G3D_MTX_MODE_TEXTURE:
        st->tex_save = st->tex;
        break;
    default:
        if (st->sp >= G3D_POS_STACK_DEPTH - 1) {
            st->error = 1;
        }
        if (st->sp < G3D_POS_STACK_DEPTH) {
            st->pos_stack[st->sp] = st->pos;
            st->vec_stack[st->sp] = st->vec;
            st->sp++;
        } else {
            st->error = 1;
        }
        break;
    }
}

void g3d_mtx_stack_pop(G3dMtxStack *st, s32 count)
{
    switch (st->mode) {
    case G3D_MTX_MODE_PROJECTION:
        st->proj = st->proj_save;
        st->clip_dirty = 1;
        break;
    case G3D_MTX_MODE_TEXTURE:
        st->tex = st->tex_save;
        break;
    default:
        /* The command's parameter is a signed 6-bit count.  A negative
         * one moves the pointer the other way on hardware; no display
         * list in the corpus uses that, so it is refused loudly rather
         * than guessed at, and so is an underflow. */
        if (count <= 0 || (u32)count > st->sp) {
            st->error = 1;
            return;
        }
        st->sp -= (u32)count;
        st->pos = st->pos_stack[st->sp];
        st->vec = st->vec_stack[st->sp];
        st->clip_dirty = 1;
        break;
    }
}

void g3d_mtx_stack_store(G3dMtxStack *st, u32 slot)
{
    switch (st->mode) {
    case G3D_MTX_MODE_PROJECTION: st->proj_save = st->proj; break;
    case G3D_MTX_MODE_TEXTURE:    st->tex_save = st->tex;   break;
    default:
        if (slot >= G3D_POS_STACK_DEPTH) {
            st->error = 1;
            return;
        }
        st->pos_stack[slot] = st->pos;
        st->vec_stack[slot] = st->vec;
        break;
    }
}

void g3d_mtx_stack_restore(G3dMtxStack *st, u32 slot)
{
    switch (st->mode) {
    case G3D_MTX_MODE_PROJECTION:
        st->proj = st->proj_save;
        st->clip_dirty = 1;
        break;
    case G3D_MTX_MODE_TEXTURE:
        st->tex = st->tex_save;
        break;
    default:
        if (slot >= G3D_POS_STACK_DEPTH) {
            st->error = 1;
            return;
        }
        st->pos = st->pos_stack[slot];
        st->vec = st->vec_stack[slot];
        st->clip_dirty = 1;
        break;
    }
}

void g3d_mtx_stack_load_identity(G3dMtxStack *st)
{
    G3dMtx id;

    g3d_mtx_identity(&id);
    g3d_mtx_stack_load(st, &id);
}

void g3d_mtx_stack_load(G3dMtxStack *st, const G3dMtx *m)
{
    switch (st->mode) {
    case G3D_MTX_MODE_PROJECTION:
        st->proj = *m;
        st->clip_dirty = 1;
        break;
    case G3D_MTX_MODE_TEXTURE:
        st->tex = *m;
        break;
    case G3D_MTX_MODE_POSVEC:
        st->pos = *m;
        st->vec = *m;
        st->clip_dirty = 1;
        break;
    default:
        st->pos = *m;
        st->clip_dirty = 1;
        break;
    }
}

void g3d_mtx_stack_mult(G3dMtxStack *st, const G3dMtx *m)
{
    switch (st->mode) {
    case G3D_MTX_MODE_PROJECTION:
        g3d_mtx_mul(&st->proj, m, &st->proj);
        st->clip_dirty = 1;
        break;
    case G3D_MTX_MODE_TEXTURE:
        g3d_mtx_mul(&st->tex, m, &st->tex);
        break;
    case G3D_MTX_MODE_POSVEC:
        g3d_mtx_mul(&st->pos, m, &st->pos);
        g3d_mtx_mul(&st->vec, m, &st->vec);
        st->clip_dirty = 1;
        break;
    default:
        g3d_mtx_mul(&st->pos, m, &st->pos);
        st->clip_dirty = 1;
        break;
    }
}

const G3dMtx *g3d_mtx_stack_clip(G3dMtxStack *st)
{
    if (st->clip_dirty) {
        g3d_mtx_mul(&st->clip, &st->pos, &st->proj);
        st->clip_dirty = 0;
    }
    return &st->clip;
}
