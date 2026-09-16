/*
 * engine/g3d/raster.c -- the software rasteriser.
 *
 * READ THIS BEFORE BELIEVING A PIXEL.  Everything above this file in
 * engine/g3d/ is checked against an oracle: the containers, the
 * dictionaries, the display-list interpreter and the vertex pipeline are
 * compared vertex for vertex against the OBJ aletheia's own asset tools
 * export.  This file has no oracle.  There is no DS frame in this
 * repository to compare a rendered picture against, and **nothing here
 * claims pixel-exactness with the hardware**.  What is below is one of
 * three things, and each rule says which:
 *
 *   [HW]  documented NDS hardware behaviour: the near-plane clip, the
 *         perspective divide, the 256x192 viewport, POLYGON_ATTR's
 *         culling bits and its 5-bit alpha, TEXIMAGE_PARAM's repeat,
 *         flip and clamp wrapping, and the modulate and decal texture
 *         blend formulas, which are defined on 6-bit colour channels and
 *         5-bit alpha rather than on the 8 bits a host buffer holds.
 *   [AP]  an approximation, named as one: the fill rule, the
 *         subpixel resolution, the per-span interpolation, the depth
 *         buffer's units, and the toon and shadow polygon modes.
 *   [PO]  a decision that belongs to the port rather than to either.
 *
 * The three approximations worth knowing about:
 *
 *   [AP] **Texture coordinates are interpolated affinely along a span**,
 *        not divided per pixel.  The brief this was built to asks for
 *        that, and for the near-axis-aligned geometry a map chunk is
 *        made of it is not visible; it is still an approximation, and
 *        the place a future comparison against a real DS frame would
 *        first show a difference is a steeply oblique floor polygon.
 *   [AP] **The fill rule is a top-left rule at pixel centres**, in 20.4
 *        fixed point.  The DS's own edge inclusion is not documented
 *        precisely enough anywhere public to transcribe, so this is a
 *        conventional rule chosen to be watertight (no pixel drawn
 *        twice, no gap between shared edges) rather than to be the
 *        hardware's.
 *   [AP] **Depth is z/w mapped linearly onto a 23-bit range.**  The DS
 *        has both a z-buffer and a w-buffer mode selected per frame;
 *        this is the z one, and the exact quantisation is this engine's.
 *
 * [PO] A polygon whose POLYGON_ATTR word is zero is drawn double-sided
 * and opaque rather than culled.  That is not a hardware rule: it is
 * what a per-shape decode needs, because a shape's display list is
 * entered at a heuristically located offset (see gx.c and aletheia's
 * docs/ASSETS.md) which is usually *past* the model's own POLYGON_ATTR
 * command, so the attribute word really was never seen rather than
 * really being zero.  Culling everything in that case would render an
 * empty screen and call it correct.
 */

#include <stdlib.h>
#include <string.h>

#include "g2d.h"
#include "g3d.h"

#define SUBPIX 4          /* screen coordinates are 20.4 fixed point */
#define SUB_ONE (1 << SUBPIX)
#define SUB_HALF (SUB_ONE / 2)
#define DEPTH_MAX 0x7FFFFF

#define ATTR_DEFAULT (((u32)1 << 6) | ((u32)1 << 7) | ((u32)31 << 16))

/* ------------------------------------------------------------------ */
/* The target                                                          */
/* ------------------------------------------------------------------ */

int g3d_target_init(G3dTarget *t, u32 width, u32 height)
{
    memset(t, 0, sizeof(*t));
    if (width == 0 || height == 0 || width > 4096u || height > 4096u) {
        return -1;
    }
    t->rgba = (u8 *)calloc((size_t)width * height, 4);
    t->depth = (s32 *)malloc((size_t)width * height * sizeof(s32));
    if (t->rgba == NULL || t->depth == NULL) {
        g3d_target_free(t);
        return -1;
    }
    t->width = width;
    t->height = height;
    g3d_target_clear(t, 0, 0, 0, 255);
    return 0;
}

void g3d_target_free(G3dTarget *t)
{
    if (t == NULL) {
        return;
    }
    free(t->rgba);
    free(t->depth);
    memset(t, 0, sizeof(*t));
}

void g3d_target_clear(G3dTarget *t, u8 r, u8 g, u8 b, u8 a)
{
    u32 i, n = t->width * t->height;

    for (i = 0; i < n; i++) {
        t->rgba[i * 4 + 0] = r;
        t->rgba[i * 4 + 1] = g;
        t->rgba[i * 4 + 2] = b;
        t->rgba[i * 4 + 3] = a;
        t->depth[i] = G3D_DEPTH_FAR;
    }
}

/* ------------------------------------------------------------------ */
/* Clipping                                                            */
/* ------------------------------------------------------------------ */

/* A vertex carried through the clip and the interpolation.  Positions
 * are clip space until the divide; s and t stay in the sixteenths of a
 * texel the TEXCOORD command carried. */
typedef struct ClipV {
    fx32 x, y, z, w;
    s32  s, t;
    s32  r, g, b;
} ClipV;

#define CLIP_MAX 12

static void clip_lerp(ClipV *o, const ClipV *a, const ClipV *b, s64 num,
                      s64 den)
{
    /* o = a + (b - a) * num / den, with the division done once per field
     * in 64 bits so a large clip-space coordinate cannot overflow. */
    if (den == 0) {
        *o = *a;
        return;
    }
    o->x = (fx32)(a->x + ((s64)(b->x - a->x) * num) / den);
    o->y = (fx32)(a->y + ((s64)(b->y - a->y) * num) / den);
    o->z = (fx32)(a->z + ((s64)(b->z - a->z) * num) / den);
    o->w = (fx32)(a->w + ((s64)(b->w - a->w) * num) / den);
    o->s = (s32)(a->s + ((s64)(b->s - a->s) * num) / den);
    o->t = (s32)(a->t + ((s64)(b->t - a->t) * num) / den);
    o->r = (s32)(a->r + ((s64)(b->r - a->r) * num) / den);
    o->g = (s32)(a->g + ((s64)(b->g - a->g) * num) / den);
    o->b = (s32)(a->b + ((s64)(b->b - a->b) * num) / den);
}

/*
 * [HW] The near-plane clip.  A polygon is clipped against w >= WMIN and
 * z >= -w before the divide; the far plane and the four side planes are
 * left to the scanline loop's own screen clamp, which is the guard band
 * a hardware rasteriser has anyway.
 */
#define WMIN 16   /* 1/256 in fx32: small, but never a divide by zero */

static u32 clip_plane(ClipV *out, const ClipV *in, u32 n, int near_plane)
{
    u32 i, m = 0;

    for (i = 0; i < n; i++) {
        const ClipV *a = &in[i];
        const ClipV *b = &in[(i + 1u) % n];
        s64 da = near_plane ? (s64)a->z + a->w : (s64)a->w - WMIN;
        s64 db = near_plane ? (s64)b->z + b->w : (s64)b->w - WMIN;
        int ia = da >= 0, ib = db >= 0;

        if (ia && m < CLIP_MAX) {
            out[m++] = *a;
        }
        if (ia != ib && m < CLIP_MAX) {
            clip_lerp(&out[m++], a, b, da, da - db);
        }
    }
    return m;
}

/* ------------------------------------------------------------------ */
/* Texture sampling                                                    */
/* ------------------------------------------------------------------ */

/*
 * [HW] TEXIMAGE_PARAM's wrapping.  With repeat clear the coordinate is
 * clamped to the edge; with repeat set it wraps by masking, and with
 * flip additionally set every other repetition is mirrored, which is why
 * the mask is over twice the size.  Every texture dimension is a power
 * of two, which is what makes both a mask rather than a modulo.
 */
static u32 wrap_coord(s32 v, u32 size, u32 repeat, u32 flip)
{
    u32 k;

    if (size == 0) {
        return 0;
    }
    if (!repeat) {
        if (v < 0) {
            return 0;
        }
        if ((u32)v >= size) {
            return size - 1u;
        }
        return (u32)v;
    }
    if (flip) {
        k = (u32)v & (size * 2u - 1u);
        if (k >= size) {
            k = size * 2u - 1u - k;
        }
        return k;
    }
    return (u32)v & (size - 1u);
}

static void sample_texture(const G3dTexture *tex, u32 teximage, s32 s16th,
                           s32 t16th, u8 out[4])
{
    s32 s = s16th >> SUBPIX;   /* sixteenths of a texel to whole texels */
    s32 t = t16th >> SUBPIX;
    u32 x = wrap_coord(s, tex->width, G3D_TEX_REPEAT_S(teximage),
                       G3D_TEX_FLIP_S(teximage));
    u32 y = wrap_coord(t, tex->height, G3D_TEX_REPEAT_T(teximage),
                       G3D_TEX_FLIP_T(teximage));
    const u8 *p = tex->rgba + ((size_t)y * tex->width + x) * 4u;

    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    out[3] = p[3];
}

/* [HW] The modulate blend is defined on 6-bit colour channels and 5-bit
 * alpha: ((a + 1) * (b + 1) - 1) >> bits.  Doing it on the 8-bit
 * expansion instead rounds differently on essentially every pixel, which
 * is the same reason engine/g2d/window.c blends in 5 bits. */
static u8 modulate6(u8 a8, u8 b8)
{
    u32 a6 = (u32)a8 >> 2, b6 = (u32)b8 >> 2;
    u32 r6 = ((a6 + 1u) * (b6 + 1u) - 1u) >> 6;

    if (r6 > 63u) {
        r6 = 63u;
    }
    return (u8)(r6 * 255u / 63u);
}

static u32 modulate5(u32 a5, u32 b5)
{
    u32 r = ((a5 + 1u) * (b5 + 1u) - 1u) >> 5;

    return r > 31u ? 31u : r;
}

/* ------------------------------------------------------------------ */
/* One pixel                                                           */
/* ------------------------------------------------------------------ */

static void put_pixel(G3dTarget *t, u32 x, u32 y, s32 depth,
                      const u8 col[3], u32 alpha5)
{
    size_t o = ((size_t)y * t->width + x);
    u8 *px = t->rgba + o * 4u;

    if (alpha5 == 0) {
        return;
    }
    if (depth > t->depth[o]) {
        return;
    }
    if (alpha5 >= 31u) {
        px[0] = col[0];
        px[1] = col[1];
        px[2] = col[2];
        px[3] = 255;
        t->depth[o] = depth;
        return;
    }
    /* [HW] translucent blend in 5-bit alpha; [AP] the depth buffer is
     * not written, which is the DS's default for translucent polygons
     * and is what keeps a translucent surface from hiding what is
     * behind it. */
    px[0] = (u8)((col[0] * alpha5 + px[0] * (31u - alpha5)) / 31u);
    px[1] = (u8)((col[1] * alpha5 + px[1] * (31u - alpha5)) / 31u);
    px[2] = (u8)((col[2] * alpha5 + px[2] * (31u - alpha5)) / 31u);
    px[3] = 255;
}

/* ------------------------------------------------------------------ */
/* The polygon                                                         */
/* ------------------------------------------------------------------ */

/* A vertex after the divide: screen position in 20.4, depth, and the
 * attributes interpolated across the face. */
typedef struct ScreenV {
    s32 x, y;      /* 20.4 */
    s32 depth;
    s32 s, t;      /* sixteenths of a texel */
    s32 r, g, b;
} ScreenV;

static void interp(ScreenV *o, const ScreenV *a, const ScreenV *b,
                   s32 num, s32 den)
{
    if (den == 0) {
        *o = *a;
        return;
    }
    o->x     = (s32)(a->x     + ((s64)(b->x     - a->x)     * num) / den);
    o->y     = (s32)(a->y     + ((s64)(b->y     - a->y)     * num) / den);
    o->depth = (s32)(a->depth + ((s64)(b->depth - a->depth) * num) / den);
    o->s     = (s32)(a->s     + ((s64)(b->s     - a->s)     * num) / den);
    o->t     = (s32)(a->t     + ((s64)(b->t     - a->t)     * num) / den);
    o->r     = (s32)(a->r     + ((s64)(b->r     - a->r)     * num) / den);
    o->g     = (s32)(a->g     + ((s64)(b->g     - a->g)     * num) / den);
    o->b     = (s32)(a->b     + ((s64)(b->b     - a->b)     * num) / den);
}

void g3d_raster_polygon(G3dTarget *t, const G3dVertex *v, u32 n,
                        u32 attr, u32 teximage, const G3dTexture *tex)
{
    ClipV in[CLIP_MAX], tmp[CLIP_MAX];
    ScreenV sv[CLIP_MAX];
    u32 i, m;
    u32 mode, alpha5;
    s64 area;
    int front;
    s32 ymin, ymax, y;

    if (n < 3u || n > 4u) {
        return;
    }
    if (attr == 0) {
        attr = ATTR_DEFAULT;   /* [PO] see this file's header */
    }
    mode = G3D_ATTR_MODE(attr);
    alpha5 = G3D_ATTR_ALPHA(attr);
    if (alpha5 == 0) {
        /* [HW] alpha 0 is the wireframe mode; this engine does not draw
         * wireframes, and a polygon asking for one is skipped rather
         * than quietly filled. */
        return;
    }
    if (mode == G3D_POLY_MODE_SHADOW) {
        /* [AP] shadow volumes need the stencil-like shadow polygon
         * machinery and a second pass; nothing in the corpus this engine
         * has been pointed at uses them, and drawing them as ordinary
         * polygons would put black slabs in the picture. */
        return;
    }

    for (i = 0; i < n; i++) {
        in[i].x = v[i].x; in[i].y = v[i].y; in[i].z = v[i].z; in[i].w = v[i].w;
        in[i].s = v[i].s; in[i].t = v[i].t;
        in[i].r = v[i].r; in[i].g = v[i].g; in[i].b = v[i].b;
    }
    m = clip_plane(tmp, in, n, 0);
    if (m < 3u) {
        return;
    }
    m = clip_plane(in, tmp, m, 1);
    if (m < 3u) {
        return;
    }

    /* [HW] the perspective divide and the viewport transform. */
    for (i = 0; i < m; i++) {
        s64 w = in[i].w;
        s64 hx = (s64)t->width * SUB_ONE / 2;
        s64 hy = (s64)t->height * SUB_ONE / 2;
        s64 dz;

        if (w < WMIN) {
            w = WMIN;
        }
        sv[i].x = (s32)(((s64)in[i].x * hx) / w + hx);
        sv[i].y = (s32)(hy - ((s64)in[i].y * hy) / w);
        dz = (((s64)in[i].z + w) * DEPTH_MAX) / (2 * w);
        if (dz < 0) {
            dz = 0;
        }
        if (dz > DEPTH_MAX) {
            dz = DEPTH_MAX;
        }
        sv[i].depth = (s32)dz;
        sv[i].s = in[i].s;
        sv[i].t = in[i].t;
        sv[i].r = in[i].r;
        sv[i].g = in[i].g;
        sv[i].b = in[i].b;
    }

    /* [HW] culling, on the projected winding.  Screen y runs downwards,
     * so a negative signed area is the front face. */
    area = (s64)(sv[1].x - sv[0].x) * (sv[2].y - sv[0].y) -
           (s64)(sv[2].x - sv[0].x) * (sv[1].y - sv[0].y);
    if (area == 0) {
        return;
    }
    front = area < 0;
    if (front && !G3D_ATTR_FRONT(attr)) {
        return;
    }
    if (!front && !G3D_ATTR_BACK(attr)) {
        return;
    }

    ymin = sv[0].y;
    ymax = sv[0].y;
    for (i = 1; i < m; i++) {
        if (sv[i].y < ymin) { ymin = sv[i].y; }
        if (sv[i].y > ymax) { ymax = sv[i].y; }
    }
    {
        s32 y0 = (ymin - SUB_HALF + SUB_ONE - 1) >> SUBPIX;
        s32 y1 = (ymax - SUB_HALF - 1) >> SUBPIX;

        if (y0 < 0) { y0 = 0; }
        if (y1 >= (s32)t->height) { y1 = (s32)t->height - 1; }

        for (y = y0; y <= y1; y++) {
            s32 yc = (y << SUBPIX) + SUB_HALF;   /* the pixel centre */
            ScreenV cross[2];
            u32 ncross = 0;
            ScreenV *L, *R;
            s32 x0, x1, x;
            s32 span;

            for (i = 0; i < m && ncross < 2u; i++) {
                const ScreenV *a = &sv[i];
                const ScreenV *b = &sv[(i + 1u) % m];
                int hit;

                /* [AP] the top-left rule: an edge covers a scanline on
                 * [y_top, y_bottom), so two faces sharing an edge cover
                 * every scanline exactly once between them. */
                if (a->y <= b->y) {
                    hit = (yc >= a->y && yc < b->y);
                } else {
                    hit = (yc >= b->y && yc < a->y);
                }
                if (!hit) {
                    continue;
                }
                interp(&cross[ncross++], a, b, yc - a->y, b->y - a->y);
            }
            if (ncross < 2u) {
                continue;
            }
            if (cross[0].x <= cross[1].x) {
                L = &cross[0]; R = &cross[1];
            } else {
                L = &cross[1]; R = &cross[0];
            }
            span = R->x - L->x;
            if (span <= 0) {
                continue;
            }
            x0 = (L->x - SUB_HALF + SUB_ONE - 1) >> SUBPIX;
            x1 = (R->x - SUB_HALF - 1) >> SUBPIX;
            if (x0 < 0) { x0 = 0; }
            if (x1 >= (s32)t->width) { x1 = (s32)t->width - 1; }

            for (x = x0; x <= x1; x++) {
                s32 xc = (x << SUBPIX) + SUB_HALF;
                ScreenV p;
                u8 col[3];
                u32 a5 = alpha5;

                /* [AP] affine interpolation across the span: no
                 * per-pixel perspective divide.  See this file's header. */
                interp(&p, L, R, xc - L->x, span);

                if (p.r < 0) { p.r = 0; } if (p.r > 255) { p.r = 255; }
                if (p.g < 0) { p.g = 0; } if (p.g > 255) { p.g = 255; }
                if (p.b < 0) { p.b = 0; } if (p.b > 255) { p.b = 255; }

                if (tex != NULL && tex->rgba != NULL) {
                    u8 tc[4];
                    sample_texture(tex, teximage, p.s, p.t, tc);
                    switch (mode) {
                    case G3D_POLY_MODE_DECAL: {
                        /* [HW] decal: the texture's own alpha selects
                         * between the texture and the vertex colour. */
                        u32 ta = (u32)tc[3] >> 3;
                        col[0] = (u8)((tc[0] * ta + p.r * (31u - ta)) / 31u);
                        col[1] = (u8)((tc[1] * ta + p.g * (31u - ta)) / 31u);
                        col[2] = (u8)((tc[2] * ta + p.b * (31u - ta)) / 31u);
                        break;
                    }
                    default:
                        /* [HW] modulate; [AP] the toon and highlight
                         * mode falls back to it, because the toon table
                         * is a register nothing in this port loads and
                         * inventing a ramp would be a guess. */
                        col[0] = modulate6(tc[0], (u8)p.r);
                        col[1] = modulate6(tc[1], (u8)p.g);
                        col[2] = modulate6(tc[2], (u8)p.b);
                        a5 = modulate5((u32)tc[3] >> 3, alpha5);
                        break;
                    }
                } else {
                    col[0] = (u8)p.r;
                    col[1] = (u8)p.g;
                    col[2] = (u8)p.b;
                }
                put_pixel(t, (u32)x, (u32)y, p.depth, col, a5);
            }
        }
    }
}

void g3d_raster_geom(G3dTarget *t, const G3dGeom *g,
                     G3dTexLookup lookup, void *user)
{
    u32 i;

    for (i = 0; i < g->n_polys; i++) {
        const G3dPoly *poly = &g->polys[i];
        G3dVertex v[4];
        u32 k;

        if (poly->count < 3u || poly->count > 4u) {
            continue;
        }
        for (k = 0; k < poly->count; k++) {
            v[k] = g->verts[g->idx[poly->first + k]];
        }
        g3d_raster_polygon(t, v, poly->count, poly->attr, poly->teximage,
                           lookup ? lookup(user, i) : NULL);
    }
}

/* ------------------------------------------------------------------ */
/* One whole model                                                     */
/* ------------------------------------------------------------------ */

typedef struct ModelTexUser {
    const G3dTexture **per_poly;
} ModelTexUser;

static const G3dTexture *model_tex(void *user, u32 poly)
{
    return ((ModelTexUser *)user)->per_poly[poly];
}

void g3d_render_model(G3dTarget *t, const G3dModel *m,
                      const G3dMtx *view, const G3dMtx *proj,
                      G3dTexArchive *ar)
{
    G3dMtx clip;
    G3dGeom xf;
    const G3dTexture **per_poly;
    ModelTexUser user;
    u32 i, s;

    if (m->geom.n_verts == 0 || m->geom.n_polys == 0) {
        return;
    }
    g3d_mtx_mul(&clip, view, proj);

    xf = m->geom;
    xf.verts = (G3dVertex *)malloc((size_t)m->geom.n_verts * sizeof(G3dVertex));
    per_poly = (const G3dTexture **)calloc(m->geom.n_polys,
                                           sizeof(*per_poly));
    if (xf.verts == NULL || per_poly == NULL) {
        free(xf.verts);
        free(per_poly);
        return;
    }
    for (i = 0; i < m->geom.n_verts; i++) {
        xf.verts[i] = m->geom.verts[i];
        g3d_mtx_xform(&clip, xf.verts[i].ox, xf.verts[i].oy, xf.verts[i].oz,
                      &xf.verts[i].x, &xf.verts[i].y, &xf.verts[i].z,
                      &xf.verts[i].w);
    }
    for (s = 0; s < m->n_shapes; s++) {
        const G3dShape *sh = &m->shapes[s];
        const G3dTexture *tex = (ar != NULL && sh->texture[0] != '\0')
                                    ? g3d_texarchive_get(ar, sh->texture)
                                    : NULL;
        for (i = 0; i < sh->n_polys; i++) {
            if (sh->first_poly + i < m->geom.n_polys) {
                per_poly[sh->first_poly + i] = tex;
            }
        }
    }
    user.per_poly = per_poly;
    g3d_raster_geom(t, &xf, model_tex, &user);

    free(xf.verts);
    free(per_poly);
}

/* ------------------------------------------------------------------ */
/* Meeting the 2D compositor                                           */
/* ------------------------------------------------------------------ */

void g3d_compose_over(const void *frame_desc, const G3dTarget *t,
                      u32 width, u32 height, u8 *rgba)
{
    const G2dFrameDesc *desc = (const G2dFrameDesc *)frame_desc;
    u32 x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            u8 *out = rgba + ((size_t)y * width + x) * 4u;
            G2dFrameSample top;
            u32 n = g2d_frame_stack(desc, (s32)x, (s32)y, &top, 1u);

            if (n == 0) {
                /* the 2D frame shows its backdrop here, so the 3D layer
                 * behind it shows instead */
                if (x < t->width && y < t->height) {
                    const u8 *src = t->rgba + ((size_t)y * t->width + x) * 4u;
                    out[0] = src[0];
                    out[1] = src[1];
                    out[2] = src[2];
                    out[3] = 255;
                } else {
                    out[0] = out[1] = out[2] = 0;
                    out[3] = 255;
                }
            } else {
                G2dColor c = top.pal->colors[top.color_index];
                out[0] = c.r;
                out[1] = c.g;
                out[2] = c.b;
                out[3] = 255;
            }
        }
    }
}
