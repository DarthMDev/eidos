/*
 * tests/test_bmap.c -- the overworld map mesh (engine/world/bmap_data.c).
 *
 * Builds Route 1 (zone 317) and Accumula Town (zone 397) around the player
 * positions the battle probe measured, checks the mesh is sane (triangles,
 * textures, a ground height under the player, no vertex outside the window),
 * and renders each scene with a small software rasteriser, from above and
 * through the field camera of the zone (a/0/6/0), to PPM files in the
 * directory given as the second argument.  There is no DS frame to compare
 * against: the images are looked at, and compared with kenon's render.
 *
 * usage: test_bmap <data root> [output directory]
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "port.h"
#include "port_test.h"
#include "../engine/world/bmap.h"

typedef struct { int w, h; unsigned char *c; float *z; } Frame;

static Frame frame_new(int w, int h, const unsigned char bg[3])
{
    Frame f;
    int i;

    f.w = w;
    f.h = h;
    f.c = (unsigned char *)malloc((size_t)w * (size_t)h * 3u);
    f.z = (float *)malloc((size_t)w * (size_t)h * sizeof(float));
    for (i = 0; i < w * h; i++) {
        f.c[i * 3] = bg[0]; f.c[i * 3 + 1] = bg[1]; f.c[i * 3 + 2] = bg[2];
        f.z[i] = -1e30f;
    }
    return f;
}

static void frame_save(const Frame *f, const char *path)
{
    FILE *fp = fopen(path, "wb");

    if (fp != NULL) {
        fprintf(fp, "P6\n%d %d\n255\n", f->w, f->h);
        fwrite(f->c, 3u, (size_t)f->w * (size_t)f->h, fp);
        fclose(fp);
    }
}

typedef struct { float x, y, iw, u, v, r, g, b; } SV;

static int wrap(float t, int n, int rep, int flip)
{
    int i = (int)floorf(t);

    if (rep) {
        if (flip) {
            int m = ((i % (2 * n)) + 2 * n) % (2 * n);

            return m >= n ? 2 * n - 1 - m : m;
        }
        return ((i % n) + n) % n;
    }
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}

/* One screen triangle. depth = larger is nearer (iw for a camera, y for top-down). */
static void raster(Frame *f, const SV t[3], const float depth[3], const BmapDraw *d,
                   const BmapTexture *tx, int pass)
{
    float area = (t[1].x - t[0].x) * (t[2].y - t[0].y) - (t[1].y - t[0].y) * (t[2].x - t[0].x);
    int x0, x1, y0, y1, x, y, front;
    float inv;

    if (area == 0.0f) {
        return;
    }
    front = area < 0.0f;
    if ((d->cull == 1 && !front) || (d->cull == 2 && front)) {
        return;
    }
    x0 = (int)floorf(fminf(t[0].x, fminf(t[1].x, t[2].x)));
    x1 = (int)ceilf(fmaxf(t[0].x, fmaxf(t[1].x, t[2].x)));
    y0 = (int)floorf(fminf(t[0].y, fminf(t[1].y, t[2].y)));
    y1 = (int)ceilf(fmaxf(t[0].y, fmaxf(t[1].y, t[2].y)));
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > f->w ? f->w : x1;
    y1 = y1 > f->h ? f->h : y1;
    inv = 1.0f / area;
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = ((t[1].x - px) * (t[2].y - py) - (t[1].y - py) * (t[2].x - px)) * inv;
            float w1 = ((t[2].x - px) * (t[0].y - py) - (t[2].y - py) * (t[0].x - px)) * inv;
            float w2 = 1.0f - w0 - w1, z, q0, q1, q2, nn, rgb[3], a = 1.0f;
            int i = y * f->w + x, k;

            if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) {
                continue;
            }
            z = w0 * depth[0] + w1 * depth[1] + w2 * depth[2];
            if (z < f->z[i]) {
                continue;
            }
            q0 = w0 * t[0].iw; q1 = w1 * t[1].iw; q2 = w2 * t[2].iw;
            nn = 1.0f / (q0 + q1 + q2);
            q0 *= nn; q1 *= nn; q2 *= nn;
            rgb[0] = 255.0f; rgb[1] = 255.0f; rgb[2] = 255.0f;
            if (tx != NULL) {
                float u = (q0 * t[0].u + q1 * t[1].u + q2 * t[2].u) * (float)tx->w;
                float v = (q0 * t[0].v + q1 * t[1].v + q2 * t[2].v) * (float)tx->h;
                const unsigned char *p = tx->rgba +
                    ((size_t)wrap(v, tx->h, tx->repeat_t, tx->flip_t) * (size_t)tx->w +
                     (size_t)wrap(u, tx->w, tx->repeat_s, tx->flip_s)) * 4u;

                if (p[3] == 0) {
                    continue;
                }
                rgb[0] = p[0]; rgb[1] = p[1]; rgb[2] = p[2];
                a = (float)p[3] / 255.0f;
            }
            rgb[0] *= (q0 * t[0].r + q1 * t[1].r + q2 * t[2].r) / 255.0f;
            rgb[1] *= (q0 * t[0].g + q1 * t[1].g + q2 * t[2].g) / 255.0f;
            rgb[2] *= (q0 * t[0].b + q1 * t[1].b + q2 * t[2].b) / 255.0f;
            a *= (float)d->alpha / 31.0f;
            if (pass == 1) {
                for (k = 0; k < 3; k++) {
                    f->c[i * 3 + k] = (unsigned char)(rgb[k] * a + f->c[i * 3 + k] * (1.0f - a));
                }
            } else {
                for (k = 0; k < 3; k++) {
                    f->c[i * 3 + k] = (unsigned char)rgb[k];
                }
                f->z[i] = z;
            }
        }
    }
}

typedef struct {
    int top;                      /* 1 = looking straight down */
    float eye[3], right[3], up[3], fwd[3];
    float focal, cx, cy, near, scale, ox, oz;
} View;

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void cross3(const float *a, const float *b, float *o)
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

static void norm3(float *v)
{
    float l = sqrtf(dot3(v, v));

    if (l > 1e-9f) {
        v[0] /= l; v[1] /= l; v[2] /= l;
    }
}

typedef struct { float q[3]; float u, v, r, g, b; } QV;

static void draw_scene(Frame *f, const BmapScene *s, const View *vw)
{
    int pass;
    unsigned di, k;

    for (pass = 0; pass < 2; pass++) {
        for (di = 0; di < s->nd; di++) {
            const BmapDraw *d = &s->d[di];
            const BmapTexture *tx = d->tex >= 0 ? &s->t[d->tex] : NULL;
            unsigned ti;

            if ((int)d->translucent != pass) {
                continue;
            }
            for (ti = 0; ti + 3u <= d->count; ti += 3u) {
                QV in[3], out[4];
                int n = 0, i;
                SV sv[4];
                float dep[4];

                for (k = 0; k < 3u; k++) {
                    const BmapVertex *v = &s->v[d->first + ti + k];
                    float p[3] = { v->x, v->y, v->z };

                    if (d->billboard && !vw->top) {
                        /* turn about the pivot: x right, y up (full only), z back */
                        float dl[3] = { p[0] - d->pivot[0], p[1] - d->pivot[1], p[2] - d->pivot[2] };
                        float back[3] = { -vw->fwd[0], d->billboard == 1 ? -vw->fwd[1] : 0.0f, -vw->fwd[2] };
                        float rt[3], upv[3] = { 0, 1, 0 };
                        int c;

                        norm3(back);
                        if (d->billboard == 1) {
                            memcpy(rt, vw->right, sizeof rt);
                            memcpy(upv, vw->up, sizeof upv);
                        } else {
                            cross3(upv, back, rt);
                        }
                        for (c = 0; c < 3; c++) {
                            p[c] = d->pivot[c] + dl[0] * rt[c] + dl[1] * upv[c] + dl[2] * back[c];
                        }
                    }
                    if (vw->top) {
                        in[k].q[0] = (p[0] - vw->ox) * vw->scale + vw->cx;
                        in[k].q[1] = (p[2] - vw->oz) * vw->scale + vw->cy;
                        in[k].q[2] = p[1];
                    } else {
                        float dd[3] = { p[0] - vw->eye[0], p[1] - vw->eye[1], p[2] - vw->eye[2] };

                        in[k].q[0] = dot3(dd, vw->right);
                        in[k].q[1] = dot3(dd, vw->up);
                        in[k].q[2] = dot3(dd, vw->fwd);
                    }
                    in[k].u = v->u; in[k].v = v->v;
                    in[k].r = v->r; in[k].g = v->g; in[k].b = v->b;
                }
                if (vw->top) {
                    for (i = 0; i < 3; i++) {
                        out[i] = in[i];
                    }
                    n = 3;
                } else {
                    for (i = 0; i < 3; i++) {
                        const QV *a = &in[i], *b = &in[(i + 1) % 3];
                        int ain = a->q[2] >= vw->near, bin = b->q[2] >= vw->near;

                        if (ain) {
                            out[n++] = *a;
                        }
                        if (ain != bin) {
                            float t = (vw->near - a->q[2]) / (b->q[2] - a->q[2]);
                            QV m;

                            m.q[0] = a->q[0] + (b->q[0] - a->q[0]) * t;
                            m.q[1] = a->q[1] + (b->q[1] - a->q[1]) * t;
                            m.q[2] = vw->near;
                            m.u = a->u + (b->u - a->u) * t;
                            m.v = a->v + (b->v - a->v) * t;
                            m.r = a->r + (b->r - a->r) * t;
                            m.g = a->g + (b->g - a->g) * t;
                            m.b = a->b + (b->b - a->b) * t;
                            out[n++] = m;
                        }
                    }
                }
                if (n < 3) {
                    continue;
                }
                for (i = 0; i < n; i++) {
                    if (vw->top) {
                        sv[i].x = out[i].q[0];
                        sv[i].y = out[i].q[1];
                        sv[i].iw = 1.0f;
                        dep[i] = out[i].q[2];
                    } else {
                        float iw = 1.0f / out[i].q[2];

                        sv[i].x = vw->cx + vw->focal * out[i].q[0] * iw;
                        sv[i].y = vw->cy - vw->focal * out[i].q[1] * iw;
                        sv[i].iw = iw;
                        dep[i] = iw;
                    }
                    sv[i].u = out[i].u; sv[i].v = out[i].v;
                    sv[i].r = out[i].r; sv[i].g = out[i].g; sv[i].b = out[i].b;
                }
                for (i = 1; i + 1 < n; i++) {
                    SV t3[3];
                    float d3[3];

                    t3[0] = sv[0]; t3[1] = sv[i]; t3[2] = sv[i + 1];
                    d3[0] = dep[0]; d3[1] = dep[i]; d3[2] = dep[i + 1];
                    raster(f, t3, d3, d, tx, pass);
                }
            }
        }
    }
}

/* The zone's field camera (a/0/6/0), as kenon's render_camera reads it. */
static int g_wide;                   /* a high, far camera that shows the horizon */

static int field_view(unsigned zone, const BmapScene *s, float px, float pz, int w, int h, View *vw)
{
    BwArcFile a;
    unsigned char hdr[48], rec[0x2C];
    long n;
    unsigned setting;
    int s32[0x2C / 4], i;
    float distance, pitch, yaw, fov, target[3], dirv[3], pc;
    unsigned char *hb;
    BwArcFile z;

    if (bw_arc_open(&z, "a/0/1/2") != 0) {
        return -1;
    }
    n = bw_arc_member_size(&z, 0);
    hb = (unsigned char *)malloc((size_t)n);
    bw_arc_read_member(&z, 0, hb, (u32)n);
    memcpy(hdr, hb + zone * 48u, 48u);
    free(hb);
    bw_arc_close(&z);
    setting = (unsigned)(hdr[0x1C] | (hdr[0x1D] << 8)) >> 9;
    if (bw_arc_open(&a, "a/0/6/0") != 0) {
        return -1;
    }
    n = bw_arc_member_size(&a, 0);
    hb = (unsigned char *)malloc((size_t)n);
    bw_arc_read_member(&a, 0, hb, (u32)n);
    memcpy(rec, hb + setting * 0x2Cu, 0x2C);
    free(hb);
    bw_arc_close(&a);
    for (i = 0; i < 0x2C / 4; i++) {
        s32[i] = (int)(rec[i * 4] | (rec[i * 4 + 1] << 8) | (rec[i * 4 + 2] << 16) |
                       ((unsigned)rec[i * 4 + 3] << 24));
    }
    distance = (float)s32[0];
    pitch = (float)(unsigned short)s32[1] / 65536.0f * 6.2831853f;
    yaw = (float)(unsigned short)s32[2] / 65536.0f * 6.2831853f;
    fov = (float)(rec[0x12] | (rec[0x13] << 8)) / 65536.0f * 6.2831853f;
    if (g_wide) {
        distance = 1100.0f;
        pitch = 0.30f;
        fov = 0.42f;
    }
    vw->near = (float)s32[5] / 4096.0f;
    if (vw->near < 0.5f) {
        vw->near = 0.5f;
    }
    if (s32[7] != 0) {
        target[0] = px + (float)s32[8] / 4096.0f;
        target[1] = bmap_ground_y(s, px, pz) + (float)s32[9] / 4096.0f;
        target[2] = pz + (float)s32[10] / 4096.0f;
    } else {
        target[0] = (float)s32[8] / 4096.0f;
        target[1] = (float)s32[9] / 4096.0f;
        target[2] = (float)s32[10] / 4096.0f;
    }
    pc = fabsf(cosf(pitch));
    if (pc < 0.05f) {
        pc = 0.05f;
    }
    dirv[0] = sinf(yaw) * pc;
    dirv[1] = sinf(pitch);
    dirv[2] = cosf(yaw) * pc;
    norm3(dirv);
    for (i = 0; i < 3; i++) {
        vw->eye[i] = target[i] + dirv[i] * distance;
        vw->fwd[i] = target[i] - vw->eye[i];
    }
    norm3(vw->fwd);
    {
        float upw[3] = { 0, 1, 0 };

        cross3(vw->fwd, upw, vw->right);
        norm3(vw->right);
        cross3(vw->right, vw->fwd, vw->up);
    }
    vw->top = 0;
    vw->focal = (float)h / 2.0f / tanf(fov > 0.01f ? fov : 0.01f);
    vw->cx = (float)w / 2.0f;
    vw->cy = (float)h / 2.0f;
    printf("  camera setting %u: distance %.1f pitch %.1f yaw %.1f fov %.1f near %.2f\n", setting,
           distance, pitch * 57.2958f, yaw * 57.2958f, fov * 57.2958f, vw->near);
    return 0;
}

static int run_case(const char *name, unsigned zone, float px, float pz, int season, int hour,
                    int minute, const char *dir)
{
    BmapScene s;
    int rc;
    unsigned i, tris = 0;
    float minx = 1e30f, maxx = -1e30f, gy;
    char path[512];
    Frame fr;
    View vw;
    unsigned char sky[3];

    {
        clock_t t0 = clock();

        rc = bmap_build(&s, zone, px, pz, season, hour, minute);
        printf("  build time %.0f ms\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    }
    printf("%s: bmap_build rc=%d\n", name, rc);
    BW_REQUIRE("builds", rc == 0);
    for (i = 0; i < s.nd; i++) {
        tris += s.d[i].count / 3u;
    }
    for (i = 0; i < s.nv; i++) {
        minx = s.v[i].x < minx ? s.v[i].x : minx;
        maxx = s.v[i].x > maxx ? s.v[i].x : maxx;
    }
    gy = bmap_ground_y(&s, px, pz);
    printf("  %u vertices, %u draws, %u textures, %u triangles, x %.0f..%.0f, ground y %.2f, "
           "clear %u,%u,%u\n", s.nv, s.nd, s.nt, tris, minx, maxx, gy, s.clear[0], s.clear[1],
           s.clear[2]);
    if (getenv("BMAP_DEBUG") != NULL) {
        float dx, dz, miny = 1e30f, maxy = -1e30f;

        for (i = 0; i < s.nv; i++) {
            miny = s.v[i].y < miny ? s.v[i].y : miny;
            maxy = s.v[i].y > maxy ? s.v[i].y : maxy;
        }
        printf("  vertex y %.2f..%.2f\n", miny, maxy);
        for (dz = -160.0f; dz <= 160.0f; dz += 32.0f) {
            for (dx = -240.0f; dx <= 240.0f; dx += 24.0f) {
                printf("%6.1f", bmap_ground_y(&s, px + dx, pz + dz));
            }
            printf("\n");
        }
    }
    printf("  fog %s start %.1f end %.1f colour %u,%u,%u\n", s.fog_on ? "on" : "off", s.fog_start,
           s.fog_end, s.fog[0], s.fog[1], s.fog[2]);
    BW_REQUIRE("has triangles", tris > 1000u);
    BW_REQUIRE("has textures", s.nt > 0u);
    BW_REQUIRE("ground under the player", fabsf(gy) < 4096.0f);
    BW_REQUIRE("window width", maxx - minx > 900.0f);
    sky[0] = s.clear[0]; sky[1] = s.clear[1]; sky[2] = s.clear[2];

    fr = frame_new(1024, 1024, sky);
    memset(&vw, 0, sizeof vw);
    vw.top = 1;
    vw.scale = 0.8f;
    vw.ox = px - 640.0f;
    vw.oz = pz - 640.0f;
    vw.cx = vw.cy = 0.0f;
    draw_scene(&fr, &s, &vw);
    snprintf(path, sizeof path, "%s/bmap_%s_top.ppm", dir, name);
    frame_save(&fr, path);
    free(fr.c); free(fr.z);

    fr = frame_new(768, 576, sky);
    if (field_view(zone, &s, px, pz, 768, 576, &vw) == 0) {
        draw_scene(&fr, &s, &vw);
    }
    snprintf(path, sizeof path, "%s/bmap_%s_field.ppm", dir, name);
    frame_save(&fr, path);
    free(fr.c); free(fr.z);
    g_wide = 1;
    fr = frame_new(1024, 576, sky);
    if (field_view(zone, &s, px, pz, 1024, 576, &vw) == 0) {
        draw_scene(&fr, &s, &vw);
    }
    snprintf(path, sizeof path, "%s/bmap_%s_wide.ppm", dir, name);
    frame_save(&fr, path);
    free(fr.c); free(fr.z);
    g_wide = 0;
    fr = frame_new(1024, 1024, sky);
    memset(&vw, 0, sizeof vw);
    vw.top = 1;
    vw.scale = 0.4f;
    vw.ox = px - 1280.0f;
    vw.oz = pz - 1280.0f;
    draw_scene(&fr, &s, &vw);
    snprintf(path, sizeof path, "%s/bmap_%s_topwide.ppm", dir, name);
    frame_save(&fr, path);
    free(fr.c); free(fr.z);
    bmap_free(&s);
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 2 ? argv[2] : ".";

    BW_TEST_SETUP(argc, argv);
    if (!bw_data_exists("a/0/1/3")) {
        printf("no game data: skipped\n");
        return 0;
    }
    if (getenv("BMAP_AT") != NULL) {
        /* BMAP_AT="name zone x z season hour minute": one extra case */
        char name[64];
        unsigned zone;
        float x, z;
        int season, hour, minute;

        if (sscanf(getenv("BMAP_AT"), "%63s %u %f %f %d %d %d", name, &zone, &x, &z, &season,
                   &hour, &minute) == 7) {
            return run_case(name, zone, x, z, season, hour, minute, dir);
        }
    }
    if (run_case("route1", 317u, 12664.0f, 11000.0f, 1, 12, 0, dir) != 0 ||
        run_case("route1_dusk", 317u, 12664.0f, 11000.0f, 1, 19, 0, dir) != 0 ||
        run_case("accumula", 397u, 12664.0f, 10584.0f, 1, 12, 0, dir) != 0 ||
        run_case("accumula_dusk", 397u, 12664.0f, 10584.0f, 1, 19, 34, dir) != 0) {
        return 1;
    }
    BW_REPORT("test_bmap");
}
