/*
 * tests/test_gpu3d.c -- the GPU 3D rasteriser against the software one.
 *
 * Both draw the same clip-space polygons (engine/platform/plat_gl3d.c and
 * engine/g3d/raster.c).  At 256x192 the two must agree except where the
 * fill rule puts an edge pixel on one side or the other, so the test counts
 * pixels that differ by more than a small tolerance and allows a thin
 * fraction of them (the polygon edges).  The scenes cover vertex colour
 * (perspective correct), a repeating / flipping texture with alpha, decal,
 * toon and highlight shading, translucency over a clear plane of alpha 0,
 * the depth test in either draw order, culling and the alpha test.
 *
 * It needs a real window system (a hidden OpenGL 3.3 window); with none, or
 * under SDL_VIDEODRIVER=dummy, it reports a skip and passes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "g3d.h"
#include "hw3d.h"
#include "plat_gl.h"
#include "port.h"
#include "port_test.h"

#define W 256
#define H 192

static G3dVertex g_verts[64];
static u32 g_idx[64];
static G3dPoly g_polys[16];
static s32 g_ptex[16];
static G3dSnapTex g_tex[3];
static u32 g_nv, g_ni, g_np;

static void scene_reset(void)
{
    g_nv = g_ni = g_np = 0;
}

static G3dVertex mkv(s32 x, s32 y, s32 z, s32 w, s32 s, s32 t, u8 r, u8 g, u8 b)
{
    G3dVertex v;

    memset(&v, 0, sizeof(v));
    v.x = x; v.y = y; v.z = z; v.w = w;
    v.s = s; v.t = t;
    v.r = r; v.g = g; v.b = b;
    return v;
}

static void add_poly(const G3dVertex *v, u32 n, u32 attr, u32 teximage,
                     s32 tex)
{
    u32 i;

    g_polys[g_np].first = g_ni;
    g_polys[g_np].count = n;
    g_polys[g_np].attr = attr;
    g_polys[g_np].teximage = teximage;
    g_polys[g_np].pltt_base = 0;
    g_ptex[g_np] = tex;
    for (i = 0; i < n; i++) {
        g_verts[g_nv] = v[i];
        g_idx[g_ni++] = g_nv++;
    }
    g_np++;
}

#define ATTR(alpha, id, front, back) \
    ((u32)(alpha) << 16 | (u32)(id) << 24 | ((front) ? 0x80u : 0u) | ((back) ? 0x40u : 0u))

static u32 teximage(int rs, int rt, int fs, int ft)
{
    return ((u32)rs << 16) | ((u32)rt << 17) | ((u32)fs << 18) | ((u32)ft << 19)
           | (7u << 26);
}

static G3dSnapshot make_snap(void)
{
    G3dSnapshot s;
    u32 i;

    memset(&s, 0, sizeof(s));
    s.verts = g_verts; s.n_verts = g_nv;
    s.idx = g_idx; s.n_idx = g_ni;
    s.polys = g_polys; s.n_polys = g_np;
    s.poly_tex = g_ptex;
    s.tex = g_tex; s.n_tex = 3;
    s.disp3dcnt = 1u;                      /* texturing on */
    s.clear_depth = 0x7FFFu;
    for (i = 0; i < 32u; i++) {            /* a toon ramp */
        s.toon[i * 3u + 0] = (u8)(i * 8u);
        s.toon[i * 3u + 1] = (u8)(255u - i * 8u);
        s.toon[i * 3u + 2] = (u8)(64u + i * 4u);
    }
    return s;
}

/* The software reference for the same polygons. */
static void soft_render_sz(const G3dSnapshot *s, u8 *rgba, u32 sw, u32 sh)
{
    G3dTarget t;
    G3dTexture tx[3];
    u32 i, k;

    g3d_target_init(&t, sw, sh);
    g3d_target_clear(&t, 0, 0, 0, 0);
    t.alpha_ref = s->alpha_ref;
    t.toon = s->toon;
    t.highlight = s->highlight;
    t.wbuffer = s->wbuffer;
    for (i = 0; i < s->n_tex; i++) {
        tx[i].width = s->tex[i].w;
        tx[i].height = s->tex[i].h;
        tx[i].format = 7;
        tx[i].rgba = s->tex[i].rgba;
    }
    for (i = 0; i < s->n_polys; i++) {
        G3dVertex v[4];
        const G3dPoly *p = &s->polys[i];

        for (k = 0; k < p->count; k++) {
            v[k] = s->verts[s->idx[p->first + k]];
        }
        g3d_raster_polygon(&t, v, p->count, p->attr, p->teximage,
                           s->poly_tex[i] >= 0 ? &tx[s->poly_tex[i]] : NULL);
    }
    memcpy(rgba, t.rgba, (size_t)sw * sh * 4u);
    g3d_target_free(&t);
}

static void soft_render(const G3dSnapshot *s, u8 *rgba)
{
    soft_render_sz(s, rgba, W, H);
}

/* Pixels where the two frames differ by more than `tol` in any channel. */
static u32 diff_count(const u8 *a, const u8 *b, int tol, u32 *covered)
{
    u32 i, bad = 0, cov = 0;

    for (i = 0; i < (u32)W * H; i++) {
        int c, d = 0;

        if (a[i * 4u + 3u] != 0 || b[i * 4u + 3u] != 0) {
            cov++;
        }
        for (c = 0; c < 4; c++) {
            int e = (int)a[i * 4u + (u32)c] - (int)b[i * 4u + (u32)c];

            if (e < 0) {
                e = -e;
            }
            if (e > d) {
                d = e;
            }
        }
        if (d > tol) {
            bad++;
        }
    }
    *covered = cov;
    return bad;
}

static void check_scene(const char *name, const G3dSnapshot *s, EidosGl *gl,
                        int tol, double max_frac)
{
    static u8 soft[W * H * 4], gpu[W * H * 4];
    u32 bad, cov;
    char msg[96];

    soft_render(s, soft);
    if (eidos_gl_render3d(gl, s, W, H) != 0 || eidos_gl_read3d(gl, gpu, W, H) != 0) {
        BW_CHECK_EQ(name, 0, 1);
        return;
    }
    bad = diff_count(soft, gpu, tol, &cov);
    if (getenv("GPU3D_DUMP") != NULL) {
        char path[256];
        FILE *f;
        u32 k, which;

        for (which = 0; which < 2u; which++) {
            snprintf(path, sizeof(path), "%s_%s_%s.ppm", getenv("GPU3D_DUMP"),
                     which ? "gpu" : "soft", name);
            for (k = 0; path[k] != 0; k++) {
                if (path[k] == ' ' || path[k] == ',' || path[k] == ':') {
                    path[k] = '_';
                }
            }
            f = fopen(path, "wb");
            if (f == NULL) {
                printf("  cannot write %s\n", path);
            }
            if (f != NULL) {
                const u8 *src = which ? gpu : soft;

                fprintf(f, "P6 %d %d 255 ", W, H);
                for (k = 0; k < (u32)W * H; k++) {
                    fwrite(src + k * 4u, 1, 3, f);
                }
                fclose(f);
            }
        }
    }
    snprintf(msg, sizeof(msg), "%s: %u of %u covered pixels differ", name, bad, cov);
    printf("  %s\n", msg);
    BW_CHECK_EQ(name, cov > 100u, 1);
    BW_CHECK_EQ(name, (double)bad <= max_frac * (double)cov, 1);
}


/* ------------------------------------------------------------------ */
/* The presenter and the 3D composite, read back from the window        */
/* ------------------------------------------------------------------ */

static u8 g_img[W * H * 4];
static u8 g_behind[W * H * 4];

/* A window of the given size; the drawable size may differ on a scaled
 * display, so the tests ask for it and skip what does not apply. */
static int window_pixels(SDL_Window *win, int w, int h, int *dw, int *dh)
{
    SDL_SetWindowSize(win, w, h);
    SDL_GL_GetDrawableSize(win, dw, dh);
    return *dw > 0 && *dh > 0;
}

/* Exact-multiple output is pure nearest; a larger fractional scale keeps most
 * pixels their own colour (the blend is one output pixel at each seam). */
static void test_presenter(EidosGl *gl, SDL_Window *win)
{
    static const int sizes[][2] = { { 512, 384 }, { 768, 576 }, { 896, 672 } };
    u32 i;
    int dw, dh, k;

    for (i = 0; i < (u32)(W * H); i++) {
        u32 x = i % W, y = i / W;

        g_img[i * 4u + 0] = (u8)(x * 37u + y * 11u);
        g_img[i * 4u + 1] = (u8)(x * 7u + y * 53u);
        g_img[i * 4u + 2] = (u8)(x ^ y);
        g_img[i * 4u + 3] = 255;
    }
    eidos_gl_upload(gl, 0, g_img, W, H);
    for (k = 0; k < 3; k++) {
        SDL_Rect dst[2];
        u8 *rgb;
        u32 same = 0, x, y;

        if (!window_pixels(win, sizes[k][0], sizes[k][1], &dw, &dh)) {
            continue;
        }
        memset(dst, 0, sizeof(dst));
        dst[0].w = dw;
        dst[0].h = dh;
        rgb = (u8 *)malloc((size_t)dw * (size_t)dh * 3u);
        if (rgb == NULL || eidos_gl_grab(gl, dw, dh, dst, rgb) != 0) {
            BW_CHECK_EQ("presenter grab", 0, 1);
            free(rgb);
            continue;
        }
        for (y = 0; y < (u32)dh; y++) {
            for (x = 0; x < (u32)dw; x++) {
                u32 sx = (u32)((double)(x + 0.5) * W / dw);
                u32 sy = (u32)((double)(y + 0.5) * H / dh);
                const u8 *e = g_img + ((size_t)sy * W + sx) * 4u;
                const u8 *g = rgb + ((size_t)y * (size_t)dw + x) * 3u;

                if (e[0] == g[0] && e[1] == g[1] && e[2] == g[2]) {
                    same++;
                }
            }
        }
        printf("  presenter %dx%d: %.1f%% of pixels pure\n", dw, dh,
               100.0 * same / ((double)dw * dh));
        if (dw % W == 0 && dh % H == 0 && dw / W == dh / H) {
            BW_CHECK_EQ("presenter whole multiple is nearest",
                        same == (u32)(dw * dh), 1);
        } else {
            BW_CHECK_EQ("presenter fractional scale keeps pixels sharp",
                        (double)same >= 0.6 * (double)dw * dh, 1);
        }
        free(rgb);
    }
}

/* The GPU 3D layer under the 2D layers: inside the 3D layer's coverage the
 * picture is the GPU frame over what is behind it, everywhere else it is the
 * screen as composed (a 2D overlay stays on top). */
static void check_composite(const char *name, const G3dSnapshot *s, EidosGl *gl,
                            SDL_Window *win)
{
    static u8 soft[512 * 384 * 4];
    SDL_Rect dst[2];
    u8 *rgb;
    int dw, dh, mismatch = 0, x, y;
    u32 i, covered = 0;

    if (!window_pixels(win, 512, 384, &dw, &dh) || dw != 512 || dh != 384) {
        printf("  %s: skipped (drawable %dx%d)\n", name, dw, dh);
        return;
    }
    /* behind: a gradient with an opaque 2D overlay at x >= 200; the screen
     * with the 3D layer on is the sentinel colour except under the overlay */
    for (i = 0; i < (u32)(W * H); i++) {
        u32 px = i % W, py = i / W;
        u8 *b = g_behind + i * 4u, *im = g_img + i * 4u;

        b[0] = 20; b[1] = (u8)(30u + py / 2u); b[2] = (u8)(120u + px / 4u); b[3] = 255;
        if (px >= 200u) {
            b[0] = 220; b[1] = 20; b[2] = 200;
        }
        im[0] = 84; im[1] = 168; im[2] = 52; im[3] = 255;   /* the sentinel */
        if (px >= 200u) {
            im[0] = 220; im[1] = 20; im[2] = 200;
        }
    }
    eidos_gl_upload(gl, 0, g_img, W, H);
    memset(dst, 0, sizeof(dst));
    dst[0].w = dw;
    dst[0].h = dh;
    BW_CHECK_EQ("composite render", eidos_gl_render3d(gl, s, dw, dh), 0);
    eidos_gl_set_3d(gl, 0, g_behind, W, H, 0, 0.f, 0);
    rgb = (u8 *)malloc((size_t)dw * (size_t)dh * 3u);
    if (rgb == NULL || eidos_gl_grab(gl, dw, dh, dst, rgb) != 0) {
        BW_CHECK_EQ("composite grab", 0, 1);
        free(rgb);
        return;
    }
    soft_render_sz(s, soft, (u32)dw, (u32)dh);
    for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
            const u8 *sf = soft + ((size_t)y * (size_t)dw + (size_t)x) * 4u;
            const u8 *b = g_behind + ((size_t)(y / 2) * W + (size_t)(x / 2)) * 4u;
            const u8 *g = rgb + ((size_t)y * (size_t)dw + (size_t)x) * 3u;
            double a = sf[3] / 255.0;
            int c, bad = 0;

            if (x / 2 >= 200) {
                a = 0.0;                        /* the overlay stays on top */
            }
            if (a > 0.0) {
                covered++;
            }
            for (c = 0; c < 3; c++) {
                int ex = (int)(sf[c] * a + b[c] * (1.0 - a) + 0.5);

                if (abs(ex - (int)g[c]) > 10) {
                    bad = 1;
                }
            }
            mismatch += bad;
        }
    }
    printf("  %s: %d of %u covered pixels differ\n", name, mismatch, covered);
    BW_CHECK_EQ(name, covered > 1000u, 1);
    BW_CHECK_EQ(name, (double)mismatch <= 0.03 * (double)covered, 1);
    free(rgb);
}

int main(void)
{
    SDL_Window *win;
    EidosGl *gl;
    G3dVertex tri[3], quad[4];
    G3dSnapshot s;
    u8 *checker, *half, *partial;
    u32 i;

    setvbuf(stdout, NULL, _IONBF, 0);   /* a crash must not eat the log */
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("test_gpu3d: SKIP (no video: %s)\n", SDL_GetError());
        return 0;
    }
    win = SDL_CreateWindow("test_gpu3d", 0, 0, W, H,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    printf("  window %p\n", (void *)win);
    gl = win != NULL ? eidos_gl_create(win) : NULL;
    printf("  gl %p\n", (void *)gl);
    if (gl == NULL || !eidos_gl_has3d(gl)) {
        printf("test_gpu3d: SKIP (no OpenGL 3.3 here)\n");
        if (gl != NULL) {
            eidos_gl_destroy(gl);
        }
        if (win != NULL) {
            SDL_DestroyWindow(win);
        }
        SDL_Quit();
        return 0;
    }

    /* textures: an 8x8 checker (opaque) and an 8x4 with a transparent half */
    checker = (u8 *)malloc(8 * 8 * 4);
    half = (u8 *)malloc(8 * 4 * 4);
    for (i = 0; i < 64u; i++) {
        int on = (int)(((i % 8u) / 2u + (i / 8u) / 2u) & 1u);

        checker[i * 4u + 0] = on ? 255 : 40;
        checker[i * 4u + 1] = on ? 200 : 90;
        checker[i * 4u + 2] = on ? 30 : 220;
        checker[i * 4u + 3] = 255;
    }
    for (i = 0; i < 32u; i++) {
        half[i * 4u + 0] = (u8)(i * 7u);
        half[i * 4u + 1] = 180;
        half[i * 4u + 2] = (u8)(255u - i * 7u);
        half[i * 4u + 3] = (i % 8u) < 4u ? 255 : 0;
    }
    g_tex[0].w = 8; g_tex[0].h = 8; g_tex[0].rgba = checker;
    g_tex[1].w = 8; g_tex[1].h = 4; g_tex[1].rgba = half;
    partial = (u8 *)malloc(8 * 4 * 4);
    for (i = 0; i < 32u; i++) {
        partial[i * 4u + 0] = 250;
        partial[i * 4u + 1] = (u8)(40u + i * 5u);
        partial[i * 4u + 2] = 40;
        partial[i * 4u + 3] = (i % 8u) < 4u ? 128 : 255;   /* 5-bit alpha 16, 31 */
    }
    g_tex[2].w = 8; g_tex[2].h = 4; g_tex[2].rgba = partial; g_tex[2].partial = 1;

    /* 1. gouraud triangle, perspective (w varies) */
    scene_reset();
    tri[0] = mkv(-3000, -2500, 0, 4096, 0, 0, 255, 0, 0);
    tri[1] = mkv(3200, -2000, 0, 8192, 0, 0, 0, 255, 0);
    tri[2] = mkv(200, 3000, 0, 4096, 0, 0, 0, 0, 255);
    add_poly(tri, 3, ATTR(31, 1, 1, 1), 0, -1);
    s = make_snap();
    check_scene("gouraud triangle", &s, gl, 6, 0.06);

    /* 2. textured modulate quad, repeat and flip */
    scene_reset();
    quad[0] = mkv(-3500, -3000, 0, 4096, 0, 0, 255, 255, 255);
    quad[1] = mkv(3500, -3000, 0, 4096, 8 * 16 * 3, 0, 255, 255, 255);
    quad[2] = mkv(3500, 3000, 0, 4096, 8 * 16 * 3, 8 * 16 * 3, 255, 255, 255);
    quad[3] = mkv(-3500, 3000, 0, 4096, 0, 8 * 16 * 3, 255, 255, 255);
    {
        u32 m;

        for (m = 0; m < 16u; m++) {
            char nm[64];

            scene_reset();
            add_poly(quad, 4, ATTR(31, 2, 1, 1),
                     teximage((int)(m & 1u), (int)((m >> 1) & 1u),
                              (int)((m >> 2) & 1u), (int)((m >> 3) & 1u)), 0);
            s = make_snap();
            snprintf(nm, sizeof(nm), "wrap rs%u rt%u fs%u ft%u", m & 1u,
                     (m >> 1) & 1u, (m >> 2) & 1u, (m >> 3) & 1u);
            check_scene(nm, &s, gl, 8, 0.03);
        }
    }

    /* 2b. a quad whose vertex colours are not planar: the GPU draws it as two
     * triangles, the software rasteriser interpolates along the edges, so the
     * gradient differs a little (tolerance); the shape and texture agree. */
    scene_reset();
    quad[1].r = 220; quad[1].g = 220; quad[1].b = 220;
    quad[3].r = 200; quad[3].g = 200;
    add_poly(quad, 4, ATTR(31, 2, 1, 1), teximage(1, 1, 1, 0), 0);
    s = make_snap();
    check_scene("textured quad, non-planar colours", &s, gl, 40, 0.01);

    /* 3. decal with a half-transparent texture, clamp */
    scene_reset();
    quad[0] = mkv(-2500, -2500, 0, 4096, 0, 0, 255, 60, 60);
    quad[1] = mkv(2500, -2500, 0, 4096, 8 * 16, 0, 255, 60, 60);
    quad[2] = mkv(2500, 2500, 0, 4096, 8 * 16, 4 * 16, 255, 60, 60);
    quad[3] = mkv(-2500, 2500, 0, 4096, 0, 4 * 16, 255, 60, 60);
    add_poly(quad, 4, ATTR(31, 3, 1, 1) | (1u << 4), teximage(0, 0, 0, 0), 1);
    s = make_snap();
    check_scene("decal, half transparent texture", &s, gl, 8, 0.03);

    /* 4. translucent triangle over an opaque one and over the clear plane */
    scene_reset();
    tri[0] = mkv(-3000, -2500, 0, 4096, 0, 0, 255, 255, 0);
    tri[1] = mkv(1500, -2500, 0, 4096, 0, 0, 255, 255, 0);
    tri[2] = mkv(-3000, 2000, 0, 4096, 0, 0, 255, 255, 0);
    add_poly(tri, 3, ATTR(31, 4, 1, 1), 0, -1);
    tri[0] = mkv(-1000, -3000, 0, 4096, 0, 0, 0, 200, 255);
    tri[1] = mkv(3500, -1000, 0, 4096, 0, 0, 0, 200, 255);
    tri[2] = mkv(-500, 3000, 0, 4096, 0, 0, 0, 200, 255);
    add_poly(tri, 3, ATTR(14, 5, 1, 1), 0, -1);
    s = make_snap();
    check_scene("translucent over opaque and clear", &s, gl, 8, 0.04);

    /* 5. depth test, both draw orders */
    for (i = 0; i < 2u; i++) {
        scene_reset();
        quad[0] = mkv(-3000, -3000, 0, 4096, 0, 0, 255, 0, 0);
        quad[1] = mkv(2000, -3000, 0, 4096, 0, 0, 255, 0, 0);
        quad[2] = mkv(2000, 2000, 0, 4096, 0, 0, 255, 0, 0);
        quad[3] = mkv(-3000, 2000, 0, 4096, 0, 0, 255, 0, 0);
        if (i == 1u) {
            add_poly(quad, 4, ATTR(31, 6, 1, 1), 0, -1);
        }
        {
            G3dVertex far[4];

            far[0] = mkv(-1000, -1000, 2000, 4096, 0, 0, 0, 255, 0);
            far[1] = mkv(3500, -1000, 2000, 4096, 0, 0, 0, 255, 0);
            far[2] = mkv(3500, 3000, 2000, 4096, 0, 0, 0, 255, 0);
            far[3] = mkv(-1000, 3000, 2000, 4096, 0, 0, 0, 255, 0);
            add_poly(far, 4, ATTR(31, 7, 1, 1), 0, -1);
        }
        if (i == 0u) {
            add_poly(quad, 4, ATTR(31, 6, 1, 1), 0, -1);
        }
        s = make_snap();
        check_scene(i == 0u ? "depth: near drawn last" : "depth: near drawn first",
                    &s, gl, 4, 0.02);
    }

    /* 6. toon and highlight shading */
    for (i = 0; i < 2u; i++) {
        scene_reset();
        tri[0] = mkv(-3000, -2500, 0, 4096, 0, 0, 20, 0, 0);
        tri[1] = mkv(3200, -2000, 0, 4096, 0, 0, 250, 0, 0);
        tri[2] = mkv(200, 3000, 0, 4096, 0, 0, 120, 0, 0);
        add_poly(tri, 3, ATTR(31, 8, 1, 1) | (2u << 4), 0, -1);
        s = make_snap();
        s.highlight = i;
        check_scene(i == 0u ? "toon" : "highlight", &s, gl, 8, 0.04);
    }

    /* 7. culling: a back face with back culled draws nothing; the alpha test */
    scene_reset();
    tri[0] = mkv(-3000, -2500, 0, 4096, 0, 0, 255, 0, 255);
    tri[1] = mkv(200, 3000, 0, 4096, 0, 0, 255, 0, 255);
    tri[2] = mkv(3200, -2000, 0, 4096, 0, 0, 255, 0, 255);
    add_poly(tri, 3, ATTR(31, 9, 1, 0), 0, -1);
    s = make_snap();
    {
        static u8 gpu[W * H * 4];
        u32 any = 0;

        BW_CHECK_EQ("cull render", eidos_gl_render3d(gl, &s, W, H), 0);
        BW_CHECK_EQ("cull read", eidos_gl_read3d(gl, gpu, W, H), 0);
        for (i = 0; i < (u32)W * H; i++) {
            any |= gpu[i * 4u + 3u];
        }
        BW_CHECK_EQ("culled face draws nothing", any, 0);
    }
    scene_reset();
    quad[0] = mkv(-3500, -3000, 0, 4096, 0, 0, 255, 255, 255);
    quad[1] = mkv(3500, -3000, 0, 4096, 8 * 16, 0, 255, 255, 255);
    quad[2] = mkv(3500, 3000, 0, 4096, 8 * 16, 4 * 16, 255, 255, 255);
    quad[3] = mkv(-3500, 3000, 0, 4096, 0, 4 * 16, 255, 255, 255);
    add_poly(quad, 4, ATTR(31, 10, 1, 1), teximage(0, 0, 0, 0), 1);
    s = make_snap();
    s.alpha_ref = 8;
    check_scene("alpha test", &s, gl, 8, 0.03);

    /* depth: an opaque polygon with partly transparent texels writes depth only
     * where the final alpha is opaque (two passes on the GPU) */
    scene_reset();
    quad[0] = mkv(-3000, -3000, 3000, 4096, 0, 0, 40, 255, 40);
    quad[1] = mkv(3000, -3000, 3000, 4096, 0, 0, 40, 255, 40);
    quad[2] = mkv(3000, 3000, 3000, 4096, 0, 0, 40, 255, 40);
    quad[3] = mkv(-3000, 3000, 3000, 4096, 0, 0, 40, 255, 40);
    add_poly(quad, 4, ATTR(31, 11, 1, 1), 0, -1);               /* far, green */
    quad[0] = mkv(-3000, -3000, -1000, 4096, 0, 0, 255, 255, 255);
    quad[1] = mkv(3000, -3000, -1000, 4096, 8 * 16, 0, 255, 255, 255);
    quad[2] = mkv(3000, 3000, -1000, 4096, 8 * 16, 4 * 16, 255, 255, 255);
    quad[3] = mkv(-3000, 3000, -1000, 4096, 0, 4 * 16, 255, 255, 255);
    add_poly(quad, 4, ATTR(31, 12, 1, 1), teximage(0, 0, 0, 0), 2);   /* near */
    quad[0] = mkv(-3000, -3000, 1000, 4096, 0, 0, 250, 20, 20);
    quad[1] = mkv(3000, -3000, 1000, 4096, 0, 0, 250, 20, 20);
    quad[2] = mkv(3000, 3000, 1000, 4096, 0, 0, 250, 20, 20);
    quad[3] = mkv(-3000, 3000, 1000, 4096, 0, 0, 250, 20, 20);
    add_poly(quad, 4, ATTR(31, 13, 1, 1), 0, -1);               /* middle, red */
    s = make_snap();
    check_scene("partly transparent texels and depth", &s, gl, 8, 0.03);

    test_presenter(gl, win);

    scene_reset();
    tri[0] = mkv(-3000, -2500, 0, 4096, 0, 0, 255, 0, 0);
    tri[1] = mkv(3200, -2000, 0, 8192, 0, 0, 0, 255, 0);
    tri[2] = mkv(200, 3000, 0, 4096, 0, 0, 0, 0, 255);
    add_poly(tri, 3, ATTR(31, 1, 1, 1), 0, -1);
    s = make_snap();
    check_composite("composite: opaque triangle", &s, gl, win);
    scene_reset();
    tri[0] = mkv(-3000, -2500, 0, 4096, 0, 0, 255, 255, 0);
    tri[1] = mkv(1500, -2500, 0, 4096, 0, 0, 255, 255, 0);
    tri[2] = mkv(-3000, 2000, 0, 4096, 0, 0, 255, 255, 0);
    add_poly(tri, 3, ATTR(31, 4, 1, 1), 0, -1);
    tri[0] = mkv(-1000, -3000, 0, 4096, 0, 0, 0, 200, 255);
    tri[1] = mkv(3500, -1000, 0, 4096, 0, 0, 0, 200, 255);
    tri[2] = mkv(-500, 3000, 0, 4096, 0, 0, 0, 200, 255);
    add_poly(tri, 3, ATTR(14, 5, 1, 1), 0, -1);
    s = make_snap();
    check_composite("composite: translucent over opaque", &s, gl, win);

    free(checker);
    free(half);
    free(partial);
    eidos_gl_destroy(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    BW_REPORT("test_gpu3d");
}
