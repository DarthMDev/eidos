/*
 * tests/test_g3d.c -- the 3D engine, in two halves.
 *
 *     ./test_g3d <data root> <oracle directory> [<bmp path>]
 *
 * (a) GEOMETRY, against an oracle.  For each model the oracle exported,
 * the C engine opens the player's own a/0/0/8, finds the BMD0 inside the
 * sub-file, locates the model's shape dictionary, decodes every shape's
 * own display list through engine/g3d/gx.c with an identity matrix
 * stack, and compares the result against the OBJ aletheia's
 * tools/assets/map_shapes.py wrote from the same bytes: every vertex
 * position, every texture coordinate, every face's vertex indices, and
 * the per-shape counts.
 *
 * The two decoders share no code and no language.  map_shapes.py and
 * nsbmd.py were written in the decompilation project against the
 * archives' bytes; engine/g3d/ here against the same attested structures
 * and the public GX command table.
 *
 * THE TOLERANCE.  The OBJ writes each coordinate with "%.6f", so the
 * oracle's own number is the exact value rounded to six decimals and
 * carries at most 5e-7 of representation error.  The engine's number is
 * exact: a vertex command's payload is a whole number of 1/4096, 1/64 or
 * 1/512 units and stays in fx32.  The comparison is therefore
 * |obj - engine/4096| <= 1e-6, with the largest deviation actually seen
 * reported every run.  Texture coordinates are compared after undoing
 * the OBJ's own normalisation: the oracle's manifest records the
 * (width, height) each shape's `vt` lines were divided by and that its
 * v axis was flipped, so what is compared is the sixteenth-of-a-texel
 * number the TEXCOORD command really carried.
 *
 * (b) RASTER, with no oracle, and this test does not pretend otherwise.
 * There is no DS frame in this repository to compare a picture against,
 * so the raster half checks two different things:
 *
 *   - a render of the house map from a fixed camera, written as a BMP
 *     into the build directory, with assertions on the frame's *content*
 *     rather than on its pixels: a minimum number of distinct colours, a
 *     minimum fraction of the frame covered by geometry, and that the
 *     depth buffer was written where the colour buffer was;
 *   - synthetic cases whose every expected pixel is computed on paper
 *     from the rules in engine/g3d/raster.c's header: one flat triangle
 *     (coverage at six hand-checked pixels, the two culling bits, and
 *     the exact flat colour) and one textured quad (a 2x2 texture over
 *     64x48 pixels, four hand-computed texel lookups and the exact
 *     modulate-blend result of each).
 *
 * Nothing in either half is evidence that a frame matches the DS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g3d.h"
#include "port.h"
#include "port_test.h"

#define MAX_MODELS  8
#define MAX_SHAPES  128
#define MAX_VERTS   65536
#define MAX_FACES   16384

/* The oracle writes 6 decimals; the engine's value is exact. */
#define POS_TOL 1e-6
#define UV_TOL  1e-6

/* ------------------------------------------------------------------ */
/* The oracle's manifest and OBJ                                       */
/* ------------------------------------------------------------------ */

typedef struct ShapeSpec {
    u32  index;
    char texture[32];
    u32  tw, th;
    u32  n_verts, n_faces;
} ShapeSpec;

typedef struct ModelSpec {
    char      name[32];
    char      archive[32];
    u32       index;
    u32       n_shapes;
    ShapeSpec shapes[MAX_SHAPES];
    u32       n_specs;
} ModelSpec;

typedef struct ObjFace {
    u32 v[4];
    u32 count;
} ObjFace;

typedef struct ObjShape {
    u32 index;
    u32 first_v, n_v;
    u32 first_f, n_f;
} ObjShape;

typedef struct Obj {
    double  *vx, *vy, *vz;
    double  *tu, *tv;
    u32      n_v;
    ObjFace *faces;
    u32      n_f;
    ObjShape shapes[MAX_SHAPES];
    u32      n_shapes;
} Obj;

static void obj_free(Obj *o)
{
    free(o->vx); free(o->vy); free(o->vz);
    free(o->tu); free(o->tv);
    free(o->faces);
    memset(o, 0, sizeof(*o));
}

static int obj_load(Obj *o, const char *path)
{
    FILE *fh = fopen(path, "r");
    char line[512];
    u32 n_vt = 0;

    memset(o, 0, sizeof(*o));
    if (fh == NULL) {
        return -1;
    }
    o->vx = (double *)malloc(MAX_VERTS * sizeof(double));
    o->vy = (double *)malloc(MAX_VERTS * sizeof(double));
    o->vz = (double *)malloc(MAX_VERTS * sizeof(double));
    o->tu = (double *)malloc(MAX_VERTS * sizeof(double));
    o->tv = (double *)malloc(MAX_VERTS * sizeof(double));
    o->faces = (ObjFace *)malloc(MAX_FACES * sizeof(ObjFace));
    if (o->vx == NULL || o->vy == NULL || o->vz == NULL || o->tu == NULL ||
        o->tv == NULL || o->faces == NULL) {
        fclose(fh);
        obj_free(o);
        return -1;
    }

    while (fgets(line, sizeof(line), fh) != NULL) {
        if (strncmp(line, "g shape", 7) == 0) {
            if (o->n_shapes >= MAX_SHAPES) {
                break;
            }
            if (o->n_shapes > 0) {
                ObjShape *prev = &o->shapes[o->n_shapes - 1];
                prev->n_v = o->n_v - prev->first_v;
                prev->n_f = o->n_f - prev->first_f;
            }
            o->shapes[o->n_shapes].index = (u32)strtoul(line + 7, NULL, 10);
            o->shapes[o->n_shapes].first_v = o->n_v;
            o->shapes[o->n_shapes].first_f = o->n_f;
            o->n_shapes++;
        } else if (line[0] == 'v' && line[1] == ' ') {
            if (o->n_v < MAX_VERTS) {
                sscanf(line + 2, "%lf %lf %lf", &o->vx[o->n_v],
                       &o->vy[o->n_v], &o->vz[o->n_v]);
                o->n_v++;
            }
        } else if (line[0] == 'v' && line[1] == 't') {
            /* The exporter writes a shape's `v` lines and then its `vt`
             * lines, one per vertex, in the same order, so the n-th `vt`
             * of the file belongs to the n-th `v` of the file. */
            if (n_vt < MAX_VERTS) {
                sscanf(line + 3, "%lf %lf", &o->tu[n_vt], &o->tv[n_vt]);
                n_vt++;
            }
        } else if (line[0] == 'f' && line[1] == ' ') {
            char *p = line + 2;
            ObjFace f;
            memset(&f, 0, sizeof(f));
            while (f.count < 4) {
                char *end;
                unsigned long v = strtoul(p, &end, 10);
                if (end == p) {
                    break;
                }
                f.v[f.count++] = (u32)(v - 1u);
                p = end;
                while (*p != '\0' && *p != ' ') {
                    p++;   /* skip the /vt part */
                }
            }
            if (f.count >= 3 && o->n_f < MAX_FACES) {
                o->faces[o->n_f++] = f;
            }
        }
    }
    if (o->n_shapes > 0) {
        ObjShape *prev = &o->shapes[o->n_shapes - 1];
        prev->n_v = o->n_v - prev->first_v;
        prev->n_f = o->n_f - prev->first_f;
    }
    fclose(fh);
    return 0;
}

static int read_manifest(ModelSpec *models, u32 max, const char *dir,
                         u32 *fmt_ids, u32 *fmt_counts, u32 *n_fmts)
{
    char path[1024];
    FILE *fh;
    char line[512];
    int n = 0;

    snprintf(path, sizeof(path), "%s/models.txt", dir);
    fh = fopen(path, "r");
    if (fh == NULL) {
        return -1;
    }
    *n_fmts = 0;
    while (fgets(line, sizeof(line), fh) != NULL) {
        if (line[0] == '#') {
            continue;
        }
        if (strncmp(line, "model ", 6) == 0 && (u32)n < max) {
            ModelSpec *m = &models[n++];
            memset(m, 0, sizeof(*m));
            sscanf(line + 6, "%31s %31s %u %u", m->name, m->archive,
                   &m->index, &m->n_shapes);
        } else if (strncmp(line, "shape ", 6) == 0 && n > 0) {
            ModelSpec *m = &models[n - 1];
            if (m->n_specs < MAX_SHAPES) {
                ShapeSpec *s = &m->shapes[m->n_specs++];
                memset(s, 0, sizeof(*s));
                sscanf(line + 6, "%u %31s %u %u %u %u", &s->index,
                       s->texture, &s->tw, &s->th, &s->n_verts, &s->n_faces);
            }
        } else if (strncmp(line, "format ", 7) == 0 && *n_fmts < 8u) {
            char name[32];
            u32 id, count, sizes;
            if (sscanf(line + 7, "%u %31s %u %u", &id, name, &count,
                       &sizes) == 4) {
                fmt_ids[*n_fmts] = id;
                fmt_counts[*n_fmts] = count;
                (*n_fmts)++;
            }
        }
    }
    fclose(fh);
    return n;
}

/* ------------------------------------------------------------------ */
/* (a) the geometry comparison                                         */
/* ------------------------------------------------------------------ */

static double fx_to_d(fx32 v)
{
    return (double)v / 4096.0;
}

static double dabs(double v)
{
    return v < 0.0 ? -v : v;
}

typedef struct GeomStats {
    u32    verts, faces;
    u32    pos_bad, uv_bad, face_bad;
    double pos_max, uv_max;
} GeomStats;

static void compare_model(const ModelSpec *spec, const Obj *obj,
                          const G3dModel *model, GeomStats *st)
{
    u32 i;

    for (i = 0; i < obj->n_shapes; i++) {
        const ObjShape *os = &obj->shapes[i];
        const G3dShape *gs;
        const ShapeSpec *ss = NULL;
        double tw = 1.0, th = 1.0;
        u32 k;

        if (os->index >= model->n_shapes) {
            st->face_bad++;
            continue;
        }
        gs = &model->shapes[os->index];
        for (k = 0; k < spec->n_specs; k++) {
            if (spec->shapes[k].index == os->index) {
                ss = &spec->shapes[k];
                break;
            }
        }
        if (ss != NULL) {
            tw = ss->tw ? (double)ss->tw : 1.0;
            th = ss->th ? (double)ss->th : 1.0;
        }

        if (gs->n_verts != os->n_v || gs->n_polys != os->n_f) {
            printf("  FAIL shape %u: engine %u verts / %u faces, "
                   "oracle %u / %u\n", os->index, gs->n_verts, gs->n_polys,
                   os->n_v, os->n_f);
            st->face_bad++;
            continue;
        }

        for (k = 0; k < os->n_v; k++) {
            const G3dVertex *v = &model->geom.verts[gs->first_vert + k];
            u32 oi = os->first_v + k;
            double dx = dabs(obj->vx[oi] - fx_to_d(v->ox));
            double dy = dabs(obj->vy[oi] - fx_to_d(v->oy));
            double dz = dabs(obj->vz[oi] - fx_to_d(v->oz));
            double du = dabs(obj->tu[oi] - ((double)v->s / 16.0) / tw);
            double dv = dabs(obj->tv[oi] -
                             (1.0 - ((double)v->t / 16.0) / th));

            st->verts++;
            if (dx > st->pos_max) { st->pos_max = dx; }
            if (dy > st->pos_max) { st->pos_max = dy; }
            if (dz > st->pos_max) { st->pos_max = dz; }
            if (du > st->uv_max) { st->uv_max = du; }
            if (dv > st->uv_max) { st->uv_max = dv; }
            if (dx > POS_TOL || dy > POS_TOL || dz > POS_TOL) {
                if (st->pos_bad == 0) {
                    printf("  FAIL shape %u vertex %u: engine "
                           "(%.6f, %.6f, %.6f), oracle (%.6f, %.6f, %.6f)\n",
                           os->index, k, fx_to_d(v->ox), fx_to_d(v->oy),
                           fx_to_d(v->oz), obj->vx[oi], obj->vy[oi],
                           obj->vz[oi]);
                }
                st->pos_bad++;
            }
            if (du > UV_TOL || dv > UV_TOL) {
                if (st->uv_bad == 0) {
                    printf("  FAIL shape %u texcoord %u: engine "
                           "(%.6f, %.6f), oracle (%.6f, %.6f)\n",
                           os->index, k, ((double)v->s / 16.0) / tw,
                           1.0 - ((double)v->t / 16.0) / th,
                           obj->tu[oi], obj->tv[oi]);
                }
                st->uv_bad++;
            }
        }

        for (k = 0; k < os->n_f; k++) {
            const ObjFace *of = &obj->faces[os->first_f + k];
            const G3dPoly *gp = &model->geom.polys[gs->first_poly + k];
            u32 j;

            st->faces++;
            if (gp->count != of->count) {
                st->face_bad++;
                continue;
            }
            for (j = 0; j < gp->count; j++) {
                u32 ge = model->geom.idx[gp->first + j] - gs->first_vert;
                u32 oe = of->v[j] - os->first_v;
                if (ge != oe) {
                    if (st->face_bad == 0) {
                        printf("  FAIL shape %u face %u slot %u: engine %u, "
                               "oracle %u\n", os->index, k, j, ge, oe);
                    }
                    st->face_bad++;
                    break;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* BMP output                                                          */
/* ------------------------------------------------------------------ */

static int write_bmp(const char *path, const u8 *rgba, u32 w, u32 h)
{
    FILE *fh = fopen(path, "wb");
    u32 row = (w * 3u + 3u) & ~3u;
    u32 size = 54u + row * h;
    u8 hdr[54];
    u8 *line;
    u32 y, x;

    if (fh == NULL) {
        return -1;
    }
    line = (u8 *)calloc(row, 1);
    if (line == NULL) {
        fclose(fh);
        return -1;
    }
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (u8)(size); hdr[3] = (u8)(size >> 8);
    hdr[4] = (u8)(size >> 16); hdr[5] = (u8)(size >> 24);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = (u8)(w); hdr[19] = (u8)(w >> 8);
    hdr[20] = (u8)(w >> 16); hdr[21] = (u8)(w >> 24);
    hdr[22] = (u8)(h); hdr[23] = (u8)(h >> 8);
    hdr[24] = (u8)(h >> 16); hdr[25] = (u8)(h >> 24);
    hdr[26] = 1;
    hdr[28] = 24;
    fwrite(hdr, 1, sizeof(hdr), fh);
    for (y = 0; y < h; y++) {
        const u8 *src = rgba + (size_t)(h - 1u - y) * w * 4u;
        memset(line, 0, row);
        for (x = 0; x < w; x++) {
            line[x * 3 + 0] = src[x * 4 + 2];   /* B */
            line[x * 3 + 1] = src[x * 4 + 1];   /* G */
            line[x * 3 + 2] = src[x * 4 + 0];   /* R */
        }
        fwrite(line, 1, row, fh);
    }
    free(line);
    fclose(fh);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Synthetic raster cases                                              */
/* ------------------------------------------------------------------ */

/*
 * A vertex placed at an exact screen pixel coordinate.  With w = 1.0 the
 * viewport transform is sx = x * 128 + 128 and sy = 96 - y * 96, so a
 * pixel position px maps back to clip x = (px - 128) / 128 and a pixel
 * position py to clip y = (96 - py) / 96.  Every py used below is a
 * multiple of 3, which makes (96 - py) * 4096 / 96 a whole fx32.
 */
static G3dVertex vtx_px(double px, double py, s32 s, s32 t,
                        u8 r, u8 g, u8 b)
{
    G3dVertex v;

    memset(&v, 0, sizeof(v));
    v.w = FX32_ONE;
    v.x = (fx32)((px - 128.0) * 4096.0 / 128.0);
    v.y = (fx32)((96.0 - py) * 4096.0 / 96.0);
    v.z = 0;
    v.s = s;
    v.t = t;
    v.r = r; v.g = g; v.b = b;
    return v;
}

static void px_of(const G3dTarget *t, u32 x, u32 y, u8 out[3])
{
    const u8 *p = t->rgba + ((size_t)y * t->width + x) * 4u;

    out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
}

static int px_is(const G3dTarget *t, u32 x, u32 y, u8 r, u8 g, u8 b)
{
    u8 c[3];

    px_of(t, x, y, c);
    return c[0] == r && c[1] == g && c[2] == b;
}

#define ATTR_BOTH  (((u32)1 << 6) | ((u32)1 << 7) | ((u32)31 << 16))
#define ATTR_FRONT_ONLY (((u32)1 << 7) | ((u32)31 << 16))
#define ATTR_BACK_ONLY  (((u32)1 << 6) | ((u32)31 << 16))

static void synthetic_triangle(void)
{
    G3dTarget t;
    G3dVertex v[3];

    if (g3d_target_init(&t, G3D_SCREEN_W, G3D_SCREEN_H) != 0) {
        printf("  FAIL cannot allocate a render target\n");
        bw_failures++;
        return;
    }

    /*
     * The right triangle (32, 24) - (96, 24) - (32, 72) in pixels.  Its
     * hypotenuse runs from (96, 24) to (32, 72), i.e. y = 96 - 0.75x, and
     * the interior is x >= 32, y >= 24, y <= 96 - 0.75x.  A pixel is
     * covered when its centre (x + 0.5, y + 0.5) satisfies all three,
     * which is engine/g3d/raster.c's top-left rule at pixel centres.
     */
    v[0] = vtx_px(32, 24, 0, 0, 200, 100, 50);
    v[1] = vtx_px(96, 24, 0, 0, 200, 100, 50);
    v[2] = vtx_px(32, 72, 0, 0, 200, 100, 50);

    g3d_target_clear(&t, 0, 0, 0, 255);
    g3d_raster_polygon(&t, v, 3u, ATTR_BOTH, 0, NULL);

    /* (40.5, 30.5): 96 - 0.75 * 40.5 = 65.625 >= 30.5, inside. */
    BW_CHECK_EQ("triangle: (40, 30) is inside", px_is(&t, 40, 30, 200, 100, 50), 1);
    /* (20.5, 30.5): left of x = 32, outside. */
    BW_CHECK_EQ("triangle: (20, 30) is outside", px_is(&t, 20, 30, 0, 0, 0), 1);
    /* (40.5, 20.5): above y = 24, outside. */
    BW_CHECK_EQ("triangle: (40, 20) is outside", px_is(&t, 40, 20, 0, 0, 0), 1);
    /* (90.5, 40.5): 96 - 0.75 * 90.5 = 28.125 < 40.5, past the hypotenuse. */
    BW_CHECK_EQ("triangle: (90, 40) is outside", px_is(&t, 90, 40, 0, 0, 0), 1);
    /* (33.5, 70.5): 96 - 0.75 * 33.5 = 70.875 >= 70.5, the last row in. */
    BW_CHECK_EQ("triangle: (33, 70) is the last row in",
                px_is(&t, 33, 70, 200, 100, 50), 1);
    /* (33.5, 71.5): 71.5 > 70.875, one row past the hypotenuse. */
    BW_CHECK_EQ("triangle: (33, 71) is one row out",
                px_is(&t, 33, 71, 0, 0, 0), 1);
    /* An untextured polygon takes the vertex colour unchanged: no
     * modulate runs, so the exact bytes come through. */
    BW_CHECK_EQ("triangle: the flat colour is exact",
                px_is(&t, 40, 30, 200, 100, 50), 1);
    /* The depth buffer was written where the colour buffer was, and not
     * where it was not. */
    BW_CHECK_EQ("triangle: depth written inside",
                t.depth[30u * G3D_SCREEN_W + 40u] != G3D_DEPTH_FAR, 1);
    BW_CHECK_EQ("triangle: depth untouched outside",
                t.depth[30u * G3D_SCREEN_W + 20u], G3D_DEPTH_FAR);

    /*
     * Culling.  The projected signed area is
     * (96-32)*(72-24) - (32-32)*(24-24) > 0, and screen y runs downwards,
     * so this winding is the *back* face.
     */
    g3d_target_clear(&t, 0, 0, 0, 255);
    g3d_raster_polygon(&t, v, 3u, ATTR_FRONT_ONLY, 0, NULL);
    BW_CHECK_EQ("cull: a back face with only FRONT set is dropped",
                px_is(&t, 40, 30, 0, 0, 0), 1);
    g3d_target_clear(&t, 0, 0, 0, 255);
    g3d_raster_polygon(&t, v, 3u, ATTR_BACK_ONLY, 0, NULL);
    BW_CHECK_EQ("cull: the same face with BACK set is drawn",
                px_is(&t, 40, 30, 200, 100, 50), 1);

    /* Alpha 0 is the wireframe mode and is not drawn. */
    g3d_target_clear(&t, 0, 0, 0, 255);
    g3d_raster_polygon(&t, v, 3u, ((u32)1 << 6) | ((u32)1 << 7), 0, NULL);
    BW_CHECK_EQ("alpha 0 (wireframe) draws nothing",
                px_is(&t, 40, 30, 0, 0, 0), 1);

    g3d_target_free(&t);
}

static void synthetic_textured_quad(void)
{
    G3dTarget t;
    G3dTexture tex;
    G3dVertex v[4];
    static const u8 texels[4][4] = {
        { 252, 0,   0,   255 },   /* (0, 0) */
        { 0,   252, 0,   255 },   /* (1, 0) */
        { 0,   0,   252, 255 },   /* (0, 1) */
        { 252, 252, 0,   255 },   /* (1, 1) */
    };
    u32 i;

    if (g3d_target_init(&t, G3D_SCREEN_W, G3D_SCREEN_H) != 0) {
        printf("  FAIL cannot allocate a render target\n");
        bw_failures++;
        return;
    }
    memset(&tex, 0, sizeof(tex));
    tex.width = 2; tex.height = 2;
    tex.format = G3D_TEXFMT_PAL16;
    tex.rgba = (u8 *)malloc(16);
    if (tex.rgba == NULL) {
        g3d_target_free(&t);
        return;
    }
    for (i = 0; i < 4u; i++) {
        memcpy(tex.rgba + i * 4u, texels[i], 4);
    }

    /*
     * A 64x48 pixel quad from (32, 24) to (96, 72), carrying texture
     * coordinates 0 to 2 texels, i.e. 0 to 32 in the sixteenths of a
     * texel TEXCOORD uses.  Interpolation across the span is affine, so
     * the pixel at screen x = 64.5 sits at (64.5 - 32) / 64 = 0.5078 of
     * the way across and samples s = 16.25 sixteenths, which is texel
     * column 1; x = 40.5 samples s = 4.25, which is column 0.  The same
     * arithmetic in y puts the split at screen y = 48.
     *
     * The blend is modulate against a white vertex colour, which is the
     * identity on the 6-bit channel: ((a6 + 1) * 64 - 1) >> 6 == a6, and
     * a texel channel of 252 is a6 = 63, which expands back to 255.
     */
    v[0] = vtx_px(32, 24, 0,  0,  255, 255, 255);
    v[1] = vtx_px(96, 24, 32, 0,  255, 255, 255);
    v[2] = vtx_px(96, 72, 32, 32, 255, 255, 255);
    v[3] = vtx_px(32, 72, 0,  32, 255, 255, 255);

    g3d_target_clear(&t, 0, 0, 0, 255);
    g3d_raster_polygon(&t, v, 4u, ATTR_BOTH, 0, &tex);

    BW_CHECK_EQ("quad: (40, 30) samples texel (0, 0)",
                px_is(&t, 40, 30, 255, 0, 0), 1);
    BW_CHECK_EQ("quad: (80, 30) samples texel (1, 0)",
                px_is(&t, 80, 30, 0, 255, 0), 1);
    BW_CHECK_EQ("quad: (40, 60) samples texel (0, 1)",
                px_is(&t, 40, 60, 0, 0, 255), 1);
    BW_CHECK_EQ("quad: (80, 60) samples texel (1, 1)",
                px_is(&t, 80, 60, 255, 255, 0), 1);
    BW_CHECK_EQ("quad: just outside stays background",
                px_is(&t, 20, 30, 0, 0, 0), 1);

    /* Modulate against a half-bright vertex colour: a6 = 63 for the
     * texel and b6 = 31 for a vertex channel of 124, so the result is
     * ((64 * 32) - 1) >> 6 = 31, which expands to 31 * 255 / 63 = 125. */
    for (i = 0; i < 4u; i++) {
        v[i].r = 124; v[i].g = 124; v[i].b = 124;
    }
    g3d_target_clear(&t, 0, 0, 0, 255);
    g3d_raster_polygon(&t, v, 4u, ATTR_BOTH, 0, &tex);
    BW_CHECK_EQ("quad: modulate by a 6-bit 31 gives 125",
                px_is(&t, 40, 30, 125, 0, 0), 1);

    /* Texture wrapping: with repeat set in TEXIMAGE_PARAM bits 16-17 a
     * coordinate of 2 texels wraps back to column 0 rather than clamping
     * to column 1. */
    for (i = 0; i < 4u; i++) {
        v[i].r = v[i].g = v[i].b = 255;
    }
    v[0].s = 32; v[0].t = 0;
    v[1].s = 64; v[1].t = 0;
    v[2].s = 64; v[2].t = 32;
    v[3].s = 32; v[3].t = 32;
    g3d_target_clear(&t, 0, 0, 0, 255);
    g3d_raster_polygon(&t, v, 4u, ATTR_BOTH,
                       ((u32)1 << 16) | ((u32)1 << 17), &tex);
    BW_CHECK_EQ("wrap: s = 2 texels repeats back to column 0",
                px_is(&t, 40, 30, 255, 0, 0), 1);

    free(tex.rgba);
    g3d_target_free(&t);
}

static void synthetic_matrices(void)
{
    G3dMtxStack st;
    G3dMtx a, b;
    fx32 x, y, z, w;

    g3d_mtx_stack_init(&st);
    BW_CHECK_EQ("matrix: the stack starts in position mode",
                st.mode, G3D_MTX_MODE_POSITION);

    g3d_mtx_xform(g3d_mtx_stack_clip(&st), FX32_ONE, 2 * FX32_ONE,
                  3 * FX32_ONE, &x, &y, &z, &w);
    BW_CHECK_EQ("matrix: identity leaves x", x, FX32_ONE);
    BW_CHECK_EQ("matrix: identity leaves y", y, 2 * FX32_ONE);
    BW_CHECK_EQ("matrix: identity leaves z", z, 3 * FX32_ONE);
    BW_CHECK_EQ("matrix: identity gives w = 1", w, FX32_ONE);

    /* MTX_MULT puts the parameter on the LEFT of the current matrix, so
     * a translate issued after a scale is applied to the vertex first. */
    g3d_mtx_scale(&a, 2 * FX32_ONE, 2 * FX32_ONE, 2 * FX32_ONE);
    g3d_mtx_translate(&b, FX32_ONE, 0, 0);
    g3d_mtx_stack_load_identity(&st);
    g3d_mtx_stack_mult(&st, &a);          /* current = scale */
    g3d_mtx_stack_mult(&st, &b);          /* current = translate * scale */
    g3d_mtx_xform(g3d_mtx_stack_clip(&st), FX32_ONE, 0, 0, &x, &y, &z, &w);
    BW_CHECK_EQ("matrix: translate then scale gives (1 + 1) * 2",
                x, 4 * FX32_ONE);

    /* push, change, pop restores. */
    g3d_mtx_stack_push(&st);
    g3d_mtx_stack_load_identity(&st);
    g3d_mtx_xform(g3d_mtx_stack_clip(&st), FX32_ONE, 0, 0, &x, &y, &z, &w);
    BW_CHECK_EQ("matrix: the pushed-over matrix is identity", x, FX32_ONE);
    g3d_mtx_stack_pop(&st, 1);
    g3d_mtx_xform(g3d_mtx_stack_clip(&st), FX32_ONE, 0, 0, &x, &y, &z, &w);
    BW_CHECK_EQ("matrix: pop restores the pushed matrix", x, 4 * FX32_ONE);
    BW_CHECK_EQ("matrix: no stack error so far", st.error, 0);

    g3d_mtx_stack_pop(&st, 1);
    BW_CHECK_EQ("matrix: popping an empty stack sets the error flag",
                st.error, 1);

    /* Fixed-point truncation: FX_MUL keeps the 64-bit intermediate and
     * shifts arithmetically, so -1 * (1/4096) stays -1 rather than 0. */
    BW_CHECK_EQ("fx: FX_MUL(-4096, 1) truncates towards minus infinity",
                FX_MUL(-FX32_ONE, 1), -1);
}

static void synthetic_texture_formats(void)
{
    G3dTexture t;
    /* A 16-colour palette whose entry 1 is pure red and entry 2 pure
     * blue in BGR555, and a 2x2 texture of indices 0, 1, 2, 1 packed two
     * to a byte, low nibble first. */
    static const u8 pal[8] = {
        0x00, 0x00,   /* 0: black                     */
        0x1F, 0x00,   /* 1: r = 31                    */
        0x00, 0x7C,   /* 2: b = 31                    */
        0xE0, 0x03,   /* 3: g = 31                    */
    };
    static const u8 texel[2] = { 0x10, 0x12 };

    BW_CHECK_EQ("texture: 16-colour 8x8 is 32 bytes",
                g3d_texture_byte_size(G3D_TEXFMT_PAL16, 8, 8), 32);
    BW_CHECK_EQ("texture: 256-colour 8x8 is 64 bytes",
                g3d_texture_byte_size(G3D_TEXFMT_PAL256, 8, 8), 64);
    BW_CHECK_EQ("texture: direct 8x8 is 128 bytes",
                g3d_texture_byte_size(G3D_TEXFMT_DIRECT, 8, 8), 128);
    BW_CHECK_EQ("texture: 4x4-compressed 8x8 is 24 bytes",
                g3d_texture_byte_size(G3D_TEXFMT_COMP, 8, 8), 24);

    BW_CHECK_EQ("texture: a 2x2 16-colour decodes",
                g3d_texture_decode(&t, G3D_TEXFMT_PAL16, 2, 2, texel, 2,
                                   pal, sizeof(pal), 1), 0);
    if (t.rgba != NULL) {
        BW_CHECK_EQ("texture: index 0 is transparent under bit 29",
                    t.rgba[3], 0);
        BW_CHECK_EQ("texture: index 1 expands to r = 255", t.rgba[4], 255);
        BW_CHECK_EQ("texture: index 1 is opaque", t.rgba[7], 255);
        BW_CHECK_EQ("texture: index 2 expands to b = 255", t.rgba[10], 255);
        g3d_texture_free(&t);
    }

    BW_CHECK_EQ("texture: an A3I5 byte carries a 3-bit alpha",
                g3d_texture_decode(&t, G3D_TEXFMT_A3I5, 1, 1,
                                   (const u8 *)"\xE1", 1, pal, sizeof(pal),
                                   0), 0);
    if (t.rgba != NULL) {
        /* 0xE1 = alpha 7, index 1: full alpha, palette entry 1. */
        BW_CHECK_EQ("texture: A3I5 alpha 7 is 255", t.rgba[3], 255);
        BW_CHECK_EQ("texture: A3I5 index 1 is red", t.rgba[0], 255);
        g3d_texture_free(&t);
    }
}

/* ------------------------------------------------------------------ */
/* (b) the map render                                                  */
/* ------------------------------------------------------------------ */

static void render_map(const G3dModel *model, G3dTexArchive *ar,
                       const char *bmp_path)
{
    G3dTarget t;
    G3dMtx view, proj;
    G3dVec eye, up, at;
    u32 i, n, covered = 0, textured = 0, distinct = 0, depth_written = 0;
    u8 *seen;

    if (g3d_target_init(&t, G3D_SCREEN_W, G3D_SCREEN_H) != 0) {
        printf("  FAIL cannot allocate a render target\n");
        bw_failures++;
        return;
    }
    /* A fixed camera, so the picture is the same every run.  The model's
     * own coordinates run to about +-4 units on x and z, so an eye six
     * units up and nine back looking at the middle frames the whole of
     * it. */
    eye.x = 0;                 eye.y = 6 * FX32_ONE;   eye.z = 9 * FX32_ONE;
    at.x  = 0;                 at.y  = FX32_ONE / 2;   at.z  = 0;
    up.x  = 0;                 up.y  = FX32_ONE;       up.z  = 0;
    g3d_mtx_lookat(&view, &eye, &up, &at);
    /* 40 degrees of vertical field of view, as a 16-bit binary angle. */
    g3d_mtx_perspective(&proj, (u16)(40u * 65536u / 360u),
                        (fx32)(4 * FX32_ONE / 3), FX32_ONE / 2,
                        64 * FX32_ONE);

    g3d_target_clear(&t, 0, 0, 40, 255);
    g3d_render_model(&t, model, &view, &proj, ar);

    n = t.width * t.height;
    seen = (u8 *)calloc(1u << 24, 1);
    for (i = 0; i < n; i++) {
        const u8 *p = t.rgba + i * 4u;
        u32 key = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];

        if (!(p[0] == 0 && p[1] == 0 && p[2] == 40)) {
            covered++;
            if (seen != NULL && !seen[key]) {
                seen[key] = 1;
                distinct++;
            }
        }
        if (t.depth[i] != G3D_DEPTH_FAR) {
            depth_written++;
        }
    }
    free(seen);

    /* "Textured" here means a pixel whose three channels are not all
     * equal: a flat-shaded or untextured surface comes out grey, and a
     * decoded texture almost never does. */
    for (i = 0; i < n; i++) {
        const u8 *p = t.rgba + i * 4u;
        if (!(p[0] == 0 && p[1] == 0 && p[2] == 40) &&
            !(p[0] == p[1] && p[1] == p[2])) {
            textured++;
        }
    }

    printf("  house render: %u of %u pixels covered (%u per mille), "
           "%u distinct colours, %u textured, depth written on %u\n",
           covered, n, covered * 1000u / n, distinct, textured,
           depth_written);

    if (bmp_path != NULL && write_bmp(bmp_path, t.rgba, t.width, t.height) == 0) {
        printf("  wrote %s\n", bmp_path);
    }

    BW_CHECK_EQ("render: at least a quarter of the frame is covered",
                covered * 4u >= n, 1);
    BW_CHECK_EQ("render: at least 64 distinct colours", distinct >= 64u, 1);
    BW_CHECK_EQ("render: at least a tenth of the frame is textured",
                textured * 10u >= n, 1);
    BW_CHECK_EQ("render: the depth buffer was written where colour was",
                depth_written, covered);

    g3d_target_free(&t);
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    ModelSpec models[MAX_MODELS];
    const char *oracle_dir;
    const char *bmp_path = NULL;
    u32 fmt_ids[8], fmt_counts[8], n_fmts = 0;
    int n, i;
    G3dTexArchive *ar;
    G3dModel house;
    int have_house = 0;

    BW_TEST_SETUP(argc, argv);
    oracle_dir = (argc > 2 && argv[2][0] != '\0') ? argv[2]
                                                  : getenv("G3D_ORACLE_DIR");
    if (oracle_dir == NULL || oracle_dir[0] == '\0') {
        printf("usage: test_g3d <data root> <oracle directory> [bmp]\n");
        return 1;
    }
    if (argc > 3 && argv[3][0] != '\0') {
        bmp_path = argv[3];
    }
    printf("oracle: %s\n", oracle_dir);

    memset(models, 0, sizeof(models));
    n = read_manifest(models, MAX_MODELS, oracle_dir, fmt_ids, fmt_counts,
                      &n_fmts);
    BW_REQUIRE("the oracle manifest names at least two models", n >= 2);

    printf("texture formats present in a/0/1/4, per the oracle's survey:\n");
    for (i = 0; (u32)i < n_fmts; i++) {
        printf("  format %u: %u textures\n", fmt_ids[i], fmt_counts[i]);
    }
    BW_CHECK_EQ("the survey found at least three formats", n_fmts >= 3u, 1);

    printf("indexing a/0/1/4 (texture name scan)...\n");
    ar = g3d_texarchive_open("a/0/1/4");
    BW_REQUIRE("the texture archive opens", ar != NULL);
    printf("  %u texture names indexed\n", g3d_texarchive_names(ar));
    BW_CHECK_EQ("at least a thousand texture names are indexed",
                g3d_texarchive_names(ar) >= 1000u, 1);

    for (i = 0; i < n; i++) {
        ModelSpec *spec = &models[i];
        char path[1024];
        Obj obj;
        G3dModel model;
        GeomStats st;

        printf("%s (%s#%u, %u shapes):\n", spec->name, spec->archive,
               spec->index, spec->n_shapes);

        snprintf(path, sizeof(path), "%s/%s.obj", oracle_dir, spec->name);
        bw_checks++;
        if (obj_load(&obj, path) != 0) {
            printf("  FAIL cannot read %s\n", path);
            bw_failures++;
            continue;
        }
        bw_checks++;
        if (g3d_model_load(&model, spec->archive, spec->index, ar) != 0) {
            printf("  FAIL the engine could not load the model\n");
            bw_failures++;
            obj_free(&obj);
            continue;
        }
        BW_CHECK_EQ("the model's name matches the oracle's",
                    strcmp(model.name, spec->name), 0);
        BW_CHECK_EQ("the shape count matches the oracle's",
                    model.n_shapes, spec->n_shapes);

        memset(&st, 0, sizeof(st));
        compare_model(spec, &obj, &model, &st);
        printf("  %u vertices, %u faces compared; worst position "
               "deviation %.3e, worst texcoord deviation %.3e\n",
               st.verts, st.faces, st.pos_max, st.uv_max);
        BW_CHECK_EQ("every vertex position is within tolerance",
                    st.pos_bad, 0);
        BW_CHECK_EQ("every texture coordinate is within tolerance",
                    st.uv_bad, 0);
        BW_CHECK_EQ("every face's vertex indices match", st.face_bad, 0);
        BW_CHECK_EQ("the comparison saw at least 2000 vertices",
                    st.verts >= 2000u, 1);

        /* The per-shape texture link: every shape the oracle named a
         * texture for must name the same one here. */
        {
            u32 k, named = 0, agree = 0;
            for (k = 0; k < spec->n_specs && k < model.n_shapes; k++) {
                const ShapeSpec *ss = &spec->shapes[k];
                if (ss->texture[0] == '-' && ss->texture[1] == '\0') {
                    continue;
                }
                named++;
                if (strcmp(ss->texture, model.shapes[ss->index].texture) == 0) {
                    agree++;
                }
            }
            printf("  %u of %u shapes' texture names agree with the oracle\n",
                   agree, named);
            BW_CHECK_EQ("every named shape resolves to the oracle's texture",
                        agree, named);
        }

        if (!have_house && strcmp(spec->name, "pl_00_00") == 0) {
            house = model;
            have_house = 1;
        } else {
            g3d_model_free(&model);
        }
        obj_free(&obj);
    }

    printf("raster (no oracle: no DS frame exists here to compare against)\n");
    if (have_house) {
        render_map(&house, ar, bmp_path);
        g3d_model_free(&house);
    } else {
        printf("  FAIL the house model was not loaded\n");
        bw_failures++;
    }
    synthetic_triangle();
    synthetic_textured_quad();
    synthetic_matrices();
    synthetic_texture_formats();

    g3d_texarchive_close(ar);
    BW_REPORT("test_g3d");
}
