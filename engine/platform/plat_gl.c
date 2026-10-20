/*
 * engine/platform/plat_gl.c -- the GPU presenter (see plat_gl.h).
 *
 * No loader library: the GL 3.3 entry points past 1.1 are fetched with
 * SDL_GL_GetProcAddress.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat_gl_int.h"

static const char *k_vs =
    "#version 330 core\n"
    "uniform vec4 rect;\n"          /* x0, y0, x1, y1 in NDC */
    "out vec2 uv;\n"
    "void main() {\n"
    "    vec2 c = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
    "    uv = vec2(c.x, 1.0 - c.y);\n"
    "    gl_Position = vec4(mix(rect.xy, rect.zw, c), 0.0, 1.0);\n"
    "}\n";

/*
 * Sharp bilinear: a texel stays its own colour except across the seam to its
 * neighbour, where the blend is one output pixel wide whatever the scale.  Let
 * t be the texture coordinate in texels, `seam` the nearest texel boundary and
 * sc the output pixels per texel: sample at seam + clamp((t - seam) * sc, -0.5,
 * 0.5).  Linear filtering at a seam is an even mix of the two texels, at half a
 * texel from it the pure texel.  At a whole-number scale every output pixel
 * centre is exactly half an output pixel from a seam, so it is pure nearest.
 * Below 1:1 it is plain linear.
 */
#define SHARP_FN \
    "vec3 sharp(sampler2D s, vec2 uv, vec2 texsize, vec2 dstsize) {\n" \
    "    vec2 t = uv * texsize;\n" \
    "    vec2 sc = dstsize / texsize;\n" \
    "    vec2 seam = floor(t + 0.5);\n" \
    "    vec2 p = seam + clamp((t - seam) * sc, -0.5, 0.5);\n" \
    "    p = mix(t, p, step(vec2(1.0), sc));\n" \
    "    return texture(s, p / texsize).rgb;\n" \
    "}\n"

static const char *k_fs =
    "#version 330 core\n"
    "uniform sampler2D tex;\n"
    "uniform vec2 texsize;\n"
    "uniform vec2 dstsize;\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    SHARP_FN
    "void main() {\n"
    "    color = vec4(sharp(tex, uv, texsize, dstsize), 1.0);\n"
    "}\n";

/*
 * A screen whose 3D layer the GPU drew.  `tex` is the screen composed with
 * the software 3D, `behind` the same screen with the 3D layer off, both at DS
 * resolution; `g3d` is the GPU's premultiplied 3D frame at any size.  Where
 * the two small images differ (dilated by one pixel, because the GPU's edges
 * land between the DS pixels) the 3D shows through the GPU frame:
 *   result = P + (1 - A) * behind
 * with the master brightness folded in exactly:
 *   up   (1 - f) P + f A + (1 - A) behind
 *   down (1 - f) P + (1 - A) behind
 * Everywhere else the screen shows as it is.  A 2D layer the game blends
 * over the 3D (the battle's message box: BG1 at full strength over 7/16 of
 * the 3D) is kept too: `zero` is the screen composed with an opaque black 3D
 * layer, so at a pixel where it differs from `tex` it is what the 2D adds,
 * and the difference over the sentinel colour `sent` (the 3D layer `tex`
 * was composed with) is how much of the 3D shows:
 *   result = zero + w * (the 3D as above),  w = (tex - zero) / sent
 * A pixel the dilation adds, where the two agree, shows the 3D as before.
 */
static const char *k_fs3 =
    "#version 330 core\n"
    "uniform sampler2D tex;\n"
    "uniform sampler2D behind;\n"
    "uniform sampler2D g3d;\n"
    "uniform vec2 texsize;\n"
    "uniform vec2 dstsize;\n"
    "uniform int bmode;\n"
    "uniform int dil;\n"
    "uniform float bfac;\n"
    "uniform sampler2D zero;\n"
    "uniform int hasz;\n"
    "uniform vec3 sent;\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    SHARP_FN
    "void main() {\n"
    "    ivec2 sz = ivec2(texsize);\n"
    "    ivec2 ip = ivec2(floor(uv * texsize));\n"
    "    bool m = false;\n"
    "    int rad = (dil != 0) ? 1 : 0;\n"
    "    for (int dy = -rad; dy <= rad; dy++) {\n"
    "        for (int dx = -rad; dx <= rad; dx++) {\n"
    "            ivec2 q = clamp(ip + ivec2(dx, dy), ivec2(0), sz - 1);\n"
    "            if (texelFetch(tex, q, 0).rgb != texelFetch(behind, q, 0).rgb) { m = true; }\n"
    "        }\n"
    "    }\n"
    "    if (!m) {\n"
    "        color = vec4(sharp(tex, uv, texsize, dstsize), 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec4 g = texture(g3d, vec2(uv.x, 1.0 - uv.y));\n"
    "    vec3 P = g.rgb;\n"
    "    float A = g.a;\n"
    "    if (bmode == 1) { P = (1.0 - bfac) * P + bfac * A; }\n"
    "    else if (bmode == 2) { P = (1.0 - bfac) * P; }\n"
    "    vec3 b = sharp(behind, uv, texsize, dstsize);\n"
    "    vec3 d = P + (1.0 - A) * b;\n"
    "    if (hasz != 0) {\n"
    "        ivec2 c = clamp(ip, ivec2(0), sz - 1);\n"
    "        vec3 T = texelFetch(tex, c, 0).rgb;\n"
    "        vec3 X = texelFetch(zero, c, 0).rgb;\n"
    "        if (T != X) {\n"
    "            float w = clamp((T.g - X.g) / sent.g, 0.0, 1.0);\n"
    "            d = min(vec3(1.0), sharp(zero, uv, texsize, dstsize) + w * d);\n"
    "        }\n"
    "    }\n"
    "    color = vec4(d, 1.0);\n"
    "}\n";

/*
 * The display capture at the 3D's resolution.  mode 0 blends two frames,
 * (eva A + evb B) / 16 a channel, as DISPCAPCNT does; mode 1 shows a frame
 * with the master brightness.  A is always a GPU frame (row 0 at the
 * bottom); B is one too unless bflip says it is an uploaded picture.
 */
static const char *k_fsc =
    "#version 330 core\n"
    "uniform sampler2D A;\n"
    "uniform sampler2D B;\n"
    "uniform float eva;\n"
    "uniform float evb;\n"
    "uniform int bflip;\n"
    "uniform int mode;\n"
    "uniform int bmode;\n"
    "uniform float bfac;\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "    vec2 q = vec2(uv.x, 1.0 - uv.y);\n"
    "    vec3 a = texture(A, q).rgb;\n"
    "    if (mode == 0) {\n"
    "        vec3 b = texture(B, bflip != 0 ? uv : q).rgb;\n"
    "        color = vec4(min(vec3(1.0), a * eva + b * evb), 1.0);\n"
    "        return;\n"
    "    }\n"
    "    if (bmode == 1) { a = a + (1.0 - a) * bfac; }\n"
    "    else if (bmode == 2) { a = a * (1.0 - bfac); }\n"
    "    color = vec4(a, 1.0);\n"
    "}\n";

static GLuint compile(EidosGl *g, GLenum type, const char *src)
{
    GLuint s = g->glCreateShader(type);
    GLint ok = 0;

    g->glShaderSource(s, 1, &src, NULL);
    g->glCompileShader(s);
    g->glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];

        g->glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "eidos: GL shader: %s\n", log);
        g->glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint eidos_gl_compile_program(EidosGl *g, const char *vsrc, const char *fsrc)
{
    GLuint vs = compile(g, GL_VERTEX_SHADER, vsrc);
    GLuint fs = compile(g, GL_FRAGMENT_SHADER, fsrc);
    GLuint prog;
    GLint ok = 0;

    if (vs == 0 || fs == 0) {
        return 0;
    }
    prog = g->glCreateProgram();
    g->glAttachShader(prog, vs);
    g->glAttachShader(prog, fs);
    g->glLinkProgram(prog);
    g->glDeleteShader(vs);
    g->glDeleteShader(fs);
    g->glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];

        g->glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "eidos: GL link: %s\n", log);
        g->glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

/*
 * Debugging aid: EIDOS_GL_DUMP=path.bmp:N writes the window's pixels (what
 * the GPU actually drew, before the swap) at the Nth presented frame.
 */
static void gl_debug_dump(int w, int h)
{
    static int frame;
    static const char *spec;
    static int init;
    static char path[512];
    static int at;
    static int done;

    if (!init) {
        const char *colon;

        init = 1;
        spec = getenv("EIDOS_GL_DUMP");
        if (spec != NULL && (colon = strrchr(spec, ':')) != NULL &&
            (size_t)(colon - spec) < sizeof(path)) {
            memcpy(path, spec, (size_t)(colon - spec));
            path[colon - spec] = 0;
            at = atoi(colon + 1);
        } else {
            spec = NULL;
        }
    }
    if (spec == NULL || ++frame < at || done) {
        return;
    }
    done = 1;
    {
        size_t row = ((size_t)w * 3u + 3u) & ~(size_t)3u;
        unsigned char *px = (unsigned char *)malloc(row * (size_t)h);
        unsigned char *rgb = (unsigned char *)malloc((size_t)w * (size_t)h * 3u);
        FILE *f;
        int x, y;

        if (px == NULL || rgb == NULL) {
            free(px);
            free(rgb);
            return;
        }
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
        for (y = 0; y < h; y++) {          /* GL rows run bottom-up: so does BMP */
            for (x = 0; x < w; x++) {
                px[(size_t)y * row + (size_t)x * 3u + 0] = rgb[((size_t)y * w + x) * 3u + 2];
                px[(size_t)y * row + (size_t)x * 3u + 1] = rgb[((size_t)y * w + x) * 3u + 1];
                px[(size_t)y * row + (size_t)x * 3u + 2] = rgb[((size_t)y * w + x) * 3u + 0];
            }
        }
        f = fopen(path, "wb");
        if (f != NULL) {
            unsigned char hd[54] = { 'B', 'M' };
            unsigned long sz = 54ul + (unsigned long)(row * (size_t)h);

            hd[2] = (unsigned char)sz; hd[3] = (unsigned char)(sz >> 8);
            hd[4] = (unsigned char)(sz >> 16); hd[5] = (unsigned char)(sz >> 24);
            hd[10] = 54; hd[14] = 40;
            hd[18] = (unsigned char)w; hd[19] = (unsigned char)(w >> 8);
            hd[20] = (unsigned char)(w >> 16);
            hd[22] = (unsigned char)h; hd[23] = (unsigned char)(h >> 8);
            hd[24] = (unsigned char)(h >> 16);
            hd[26] = 1; hd[28] = 24;
            fwrite(hd, 1, 54, f);
            fwrite(px, 1, row * (size_t)h, f);
            fclose(f);
        }
        free(px);
        free(rgb);
    }
}

EidosGl *eidos_gl_create(SDL_Window *window)
{
    EidosGl *g;
    int i;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    g = (EidosGl *)calloc(1, sizeof(*g));
    if (g == NULL) {
        return NULL;
    }
    g->window = window;
    g->s3d = -1;
    g->ctx = SDL_GL_CreateContext(window);
    if (g->ctx == NULL) {
        free(g);
        return NULL;
    }
#define X(T, N) \
    g->N = (T)SDL_GL_GetProcAddress(#N); \
    if (g->N == NULL) { eidos_gl_destroy(g); return NULL; }
    GL_FUNCS(X)
#undef X
    /* The pacer is the timebase, never the display's refresh. */
    SDL_GL_SetSwapInterval(0);

    g->cap_screen = -1;
    g->prog = eidos_gl_compile_program(g, k_vs, k_fs);
    if (g->prog == 0) {
        eidos_gl_destroy(g);
        return NULL;
    }
    g->u_rect = g->glGetUniformLocation(g->prog, "rect");
    g->u_tex = g->glGetUniformLocation(g->prog, "tex");
    g->u_texsize = g->glGetUniformLocation(g->prog, "texsize");
    g->u_dstsize = g->glGetUniformLocation(g->prog, "dstsize");
    g->glGenVertexArrays(1, &g->vao);
    glGenTextures(2, g->tex);
    for (i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, g->tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glGenTextures(1, &g->behind_tex);
    glBindTexture(GL_TEXTURE_2D, g->behind_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &g->zero_tex);
    glBindTexture(GL_TEXTURE_2D, g->zero_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    if (glGetError() != GL_NO_ERROR) {
        eidos_gl_destroy(g);
        return NULL;
    }

    /* The 3D path is optional: a failure here leaves a working presenter. */
    g->prog3 = eidos_gl_compile_program(g, k_vs, k_fs3);
    if (g->prog3 != 0) {
        g->u3_rect = g->glGetUniformLocation(g->prog3, "rect");
        g->u3_tex = g->glGetUniformLocation(g->prog3, "tex");
        g->u3_behind = g->glGetUniformLocation(g->prog3, "behind");
        g->u3_g3d = g->glGetUniformLocation(g->prog3, "g3d");
        g->u3_texsize = g->glGetUniformLocation(g->prog3, "texsize");
        g->u3_dstsize = g->glGetUniformLocation(g->prog3, "dstsize");
        g->u3_bmode = g->glGetUniformLocation(g->prog3, "bmode");
        g->u3_bfac = g->glGetUniformLocation(g->prog3, "bfac");
        g->u3_dil = g->glGetUniformLocation(g->prog3, "dil");
        g->u3_zero = g->glGetUniformLocation(g->prog3, "zero");
        g->u3_hasz = g->glGetUniformLocation(g->prog3, "hasz");
        g->u3_sent = g->glGetUniformLocation(g->prog3, "sent");
        if (eidos_gl3d_init(g) != 0) {
            g->have3d = 0;
        }
        g->progc = eidos_gl_compile_program(g, k_vs, k_fsc);
        if (g->progc != 0) {
            g->uc_rect = g->glGetUniformLocation(g->progc, "rect");
            g->uc_a = g->glGetUniformLocation(g->progc, "A");
            g->uc_b = g->glGetUniformLocation(g->progc, "B");
            g->uc_eva = g->glGetUniformLocation(g->progc, "eva");
            g->uc_evb = g->glGetUniformLocation(g->progc, "evb");
            g->uc_bflip = g->glGetUniformLocation(g->progc, "bflip");
            g->uc_mode = g->glGetUniformLocation(g->progc, "mode");
            g->uc_bmode = g->glGetUniformLocation(g->progc, "bmode");
            g->uc_bfac = g->glGetUniformLocation(g->progc, "bfac");
        }
    }
    return g;
}

void eidos_gl_destroy(EidosGl *g)
{
    if (g == NULL) {
        return;
    }
    if (g->ctx != NULL) {
        SDL_GL_MakeCurrent(g->window, g->ctx);
        if (g->tex[0] != 0) {
            glDeleteTextures(2, g->tex);
        }
        if (g->zero_tex != 0) {
            glDeleteTextures(1, &g->zero_tex);
        }
        if (g->behind_tex != 0) {
            glDeleteTextures(1, &g->behind_tex);
        }
        if (g->have3d || g->p3d != 0) {
            eidos_gl3d_destroy(g);
        }
        if (g->vao != 0 && g->glDeleteVertexArrays != NULL) {
            g->glDeleteVertexArrays(1, &g->vao);
        }
        if (g->prog != 0 && g->glDeleteProgram != NULL) {
            g->glDeleteProgram(g->prog);
        }
        if (g->prog3 != 0 && g->glDeleteProgram != NULL) {
            g->glDeleteProgram(g->prog3);
        }
        SDL_GL_DeleteContext(g->ctx);
    }
    free(g);
}

int eidos_gl_has3d(const EidosGl *g)
{
    return g != NULL && g->have3d;
}

void eidos_gl_upload(EidosGl *g, int i, const unsigned char *rgba, int w,
                     int h)
{
    if (g == NULL || rgba == NULL || w <= 0 || h <= 0) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, g->tex[i]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (g->tw[i] != w || g->th[i] != h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, rgba);
        g->tw[i] = w;
        g->th[i] = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA,
                        GL_UNSIGNED_BYTE, rgba);
    }
}

/* The sentinel colour in the shader matches demo/eidos_boot.c's
 * boot_sentinel: the opaque 3D layer the 2D screen is composed with. */
void eidos_gl_set_3d_zero(EidosGl *g, const unsigned char *zero, int w, int h)
{
    if (g == NULL) {
        return;
    }
    g->has_zero = 0;
    if (zero == NULL || g->zero_tex == 0 || w != 256 || h != 192) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, g->zero_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, zero);
    g->has_zero = 1;
}

void eidos_gl_set_3d(EidosGl *g, int screen, const unsigned char *behind,
                     int w, int h, int bmode, float bfac, int dilate)
{
    if (g == NULL) {
        return;
    }
    if (screen < 0 || behind == NULL || w <= 0 || h <= 0) {
        g->s3d = -1;
        return;
    }
    glBindTexture(GL_TEXTURE_2D, g->behind_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (g->behind_w != w || g->behind_h != h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, behind);
        g->behind_w = w;
        g->behind_h = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA,
                        GL_UNSIGNED_BYTE, behind);
    }
    g->s3d = screen;
    g->bmode = bmode;
    g->bfac = bfac;
    g->dilate = dilate;
}

static void cap_tex_setup(GLuint t, int w, int h, const unsigned char *px)
{
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/*
 * One frame of the display capture, kept at the 3D's resolution.  The game
 * captures the main screen into a VRAM bank every frame and shows that bank
 * (a feedback blend: the title's motion trail); the DS-size capture still
 * runs on the CPU for the game's memory, and this is the same thing on the
 * GPU for the picture.  `a2d` is the graphics screen with the sentinel 3D
 * layer and `behind` the same with the 3D off (both 256 x 192 RGBA);
 * `snap` the 3D frame; blend != 0 mixes it with what was captured before,
 * eva and evb in sixteenths.  `b1x`, when not NULL, replaces the earlier
 * capture (the bank's content, 256 x 192 RGBA): the first frame, or after
 * the game wrote the bank itself.
 */
int eidos_gl_capture(EidosGl *g, const unsigned char *a2d,
                     const unsigned char *behind,
                     const struct G3dSnapshot *snap, int scale, int blend,
                     int eva, int evb, const unsigned char *b1x)
{
    int w = 256 * scale, h = 192 * scale, next, i;

    if (g == NULL || !g->have3d || g->progc == 0 || g->prog3 == 0
        || a2d == NULL || behind == NULL || snap == NULL || scale < 1) {
        return -1;
    }
    if (eidos_gl_render3d(g, snap, w, h) != 0) {
        return -1;
    }
    if (g->cap_fbo == 0) {
        g->glGenFramebuffers(1, &g->cap_fbo);
        glGenTextures(1, &g->capa_tex);
        glGenTextures(2, g->cap_tex);
        glGenTextures(3, g->cap_src);
        for (i = 0; i < 3; i++) {
            cap_tex_setup(g->cap_src[i], 256, 192, NULL);
        }
    }
    if (g->cap_w != w || g->cap_h != h) {
        cap_tex_setup(g->capa_tex, w, h, NULL);
        cap_tex_setup(g->cap_tex[0], w, h, NULL);
        cap_tex_setup(g->cap_tex[1], w, h, NULL);
        g->cap_w = w;
        g->cap_h = h;
        g->cap_valid = 0;
    }
    if (blend && !g->cap_valid && b1x == NULL) {
        return -1;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, g->cap_src[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, a2d);
    glBindTexture(GL_TEXTURE_2D, g->cap_src[1]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, behind);
    if (b1x != NULL) {
        glBindTexture(GL_TEXTURE_2D, g->cap_src[2]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, b1x);
    }

    /* source A: the screen with the GPU's 3D layer, at the 3D's size */
    g->glBindFramebuffer(GL_FRAMEBUFFER, g->cap_fbo);
    g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                              g->capa_tex, 0);
    if (g->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return -1;
    }
    glViewport(0, 0, w, h);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    g->glBindVertexArray(g->vao);
    g->glUseProgram(g->prog3);
    g->glUniform1i(g->u3_tex, 0);
    g->glUniform1i(g->u3_behind, 1);
    g->glUniform1i(g->u3_g3d, 2);
    g->glUniform4f(g->u3_rect, -1.f, -1.f, 1.f, 1.f);
    g->glUniform2f(g->u3_texsize, 256.f, 192.f);
    g->glUniform2f(g->u3_dstsize, (float)w, (float)h);
    g->glUniform1i(g->u3_bmode, 0);
    g->glUniform1f(g->u3_bfac, 0.f);
    g->glUniform1i(g->u3_dil, 1);
    g->glUniform1i(g->u3_hasz, 0);
    g->glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g->cap_src[1]);
    g->glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g->fb[0].color);
    g->glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->cap_src[0]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* the capture: A alone, or mixed with the capture before it */
    next = g->cap_cur ^ 1;
    g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                              g->cap_tex[next], 0);
    g->glUseProgram(g->progc);
    g->glUniform4f(g->uc_rect, -1.f, -1.f, 1.f, 1.f);
    g->glUniform1i(g->uc_a, 0);
    g->glUniform1i(g->uc_b, 1);
    g->glUniform1i(g->uc_mode, 0);
    g->glUniform1f(g->uc_eva, blend ? (float)eva / 16.f : 1.f);
    g->glUniform1f(g->uc_evb, blend ? (float)evb / 16.f : 0.f);
    g->glUniform1i(g->uc_bflip, b1x != NULL ? 1 : 0);
    g->glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, b1x != NULL ? g->cap_src[2] : g->cap_tex[g->cap_cur]);
    g->glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g->capa_tex);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g->cap_cur = next;
    g->cap_valid = 1;
    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

/* The screen that shows the capture this frame (-1: none), with the master
 * brightness (0 off, 1 up, 2 down; fac 0..1). */
void eidos_gl_show_capture(EidosGl *g, int screen, int bmode, float bfac)
{
    if (g == NULL) {
        return;
    }
    g->cap_screen = (screen >= 0 && g->cap_valid) ? screen : -1;
    g->cap_bmode = bmode;
    g->cap_bfac = bfac;
}

static void draw_frame(EidosGl *g, int out_w, int out_h, const SDL_Rect dst[2])
{
    int i;

    if (g == NULL || out_w <= 0 || out_h <= 0) {
        return;
    }
    glViewport(0, 0, out_w, out_h);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    g->glBindVertexArray(g->vao);
    for (i = 0; i < 2; i++) {
        float x0, y0, x1, y1;
        int is3d = (i == g->s3d && g->prog3 != 0 && g->fb[0].color != 0 &&
                    g->behind_w == g->tw[i] && g->behind_h == g->th[i]);

        if (dst[i].w <= 0 || dst[i].h <= 0 || g->tw[i] == 0) {
            continue;
        }
        x0 = 2.f * (float)dst[i].x / (float)out_w - 1.f;
        x1 = 2.f * (float)(dst[i].x + dst[i].w) / (float)out_w - 1.f;
        y1 = 1.f - 2.f * (float)dst[i].y / (float)out_h;
        y0 = 1.f - 2.f * (float)(dst[i].y + dst[i].h) / (float)out_h;
        if (i == g->cap_screen && g->cap_valid && g->progc != 0) {
            g->glUseProgram(g->progc);
            g->glUniform4f(g->uc_rect, x0, y0, x1, y1);
            g->glUniform1i(g->uc_a, 0);
            g->glUniform1i(g->uc_b, 0);
            g->glUniform1i(g->uc_mode, 1);
            g->glUniform1i(g->uc_bmode, g->cap_bmode);
            g->glUniform1f(g->uc_bfac, g->cap_bfac);
            glBindTexture(GL_TEXTURE_2D, g->cap_tex[g->cap_cur]);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            continue;
        }
        if (is3d) {
            g->glUseProgram(g->prog3);
            g->glUniform1i(g->u3_tex, 0);
            g->glUniform1i(g->u3_behind, 1);
            g->glUniform1i(g->u3_g3d, 2);
            g->glUniform4f(g->u3_rect, x0, y0, x1, y1);
            g->glUniform2f(g->u3_texsize, (float)g->tw[i], (float)g->th[i]);
            g->glUniform2f(g->u3_dstsize, (float)dst[i].w, (float)dst[i].h);
            g->glUniform1i(g->u3_bmode, g->bmode);
            g->glUniform1f(g->u3_bfac, g->bfac);
            g->glUniform1i(g->u3_dil, g->dilate);
            {
                static const GLfloat sent[3] = { 84.f / 255.f, 168.f / 255.f, 52.f / 255.f };

                g->glUniform1i(g->u3_zero, 3);
                g->glUniform1i(g->u3_hasz, g->has_zero);
                g->glUniform3fv(g->u3_sent, 1, sent);
            }
            g->glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, g->zero_tex);
            g->glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, g->behind_tex);
            g->glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, g->fb[0].color);
            g->glActiveTexture(GL_TEXTURE0);
        } else {
            g->glUseProgram(g->prog);
            g->glUniform1i(g->u_tex, 0);
            g->glUniform4f(g->u_rect, x0, y0, x1, y1);
            g->glUniform2f(g->u_texsize, (float)g->tw[i], (float)g->th[i]);
            g->glUniform2f(g->u_dstsize, (float)dst[i].w, (float)dst[i].h);
        }
        glBindTexture(GL_TEXTURE_2D, g->tex[i]);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

void eidos_gl_present(EidosGl *g, int out_w, int out_h, const SDL_Rect dst[2])
{
    if (g == NULL || out_w <= 0 || out_h <= 0) {
        return;
    }
    draw_frame(g, out_w, out_h, dst);
    gl_debug_dump(out_w, out_h);
    SDL_GL_SwapWindow(g->window);
}

int eidos_gl_grab(EidosGl *g, int out_w, int out_h, const SDL_Rect dst[2],
                  unsigned char *rgb)
{
    unsigned char *raw;
    int y;

    if (g == NULL || rgb == NULL || out_w <= 0 || out_h <= 0) {
        return -1;
    }
    raw = (unsigned char *)malloc((size_t)out_w * (size_t)out_h * 3u);
    if (raw == NULL) {
        return -1;
    }
    draw_frame(g, out_w, out_h, dst);      /* not swapped: only read */
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, out_w, out_h, GL_RGB, GL_UNSIGNED_BYTE, raw);
    for (y = 0; y < out_h; y++) {          /* GL rows run bottom-up */
        memcpy(rgb + (size_t)y * (size_t)out_w * 3u,
               raw + (size_t)(out_h - 1 - y) * (size_t)out_w * 3u,
               (size_t)out_w * 3u);
    }
    free(raw);
    return glGetError() == GL_NO_ERROR ? 0 : -1;
}
