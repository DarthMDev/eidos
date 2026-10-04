/*
 * engine/platform/plat_gl.c -- the GPU presenter (see plat_gl.h).
 *
 * No loader library: the GL 3.3 entry points past 1.1 are fetched with
 * SDL_GL_GetProcAddress.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL_opengl.h>

#include "plat_gl.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#define GL_FUNCS(X) \
    X(PFNGLCREATESHADERPROC, glCreateShader) \
    X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
    X(PFNGLUNIFORM1IPROC, glUniform1i) \
    X(PFNGLUNIFORM2FPROC, glUniform2f) \
    X(PFNGLUNIFORM4FPROC, glUniform4f) \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)

struct EidosGl {
    SDL_Window *window;
    SDL_GLContext ctx;
#define X(T, N) T N;
    GL_FUNCS(X)
#undef X
    GLuint prog;
    GLuint vao;
    GLint u_rect, u_tex, u_texsize, u_dstsize;
    GLuint tex[2];
    int tw[2], th[2];
};

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
 * Sharp bilinear: nearest at the centre of each source pixel, a linear blend
 * over about one output pixel at its edge.  Below 1:1 it is plain linear.
 */
static const char *k_fs =
    "#version 330 core\n"
    "uniform sampler2D tex;\n"
    "uniform vec2 texsize;\n"
    "uniform vec2 dstsize;\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "    vec2 t = uv * texsize;\n"
    "    vec2 sc = dstsize / texsize;\n"
    "    vec2 f = fract(t);\n"
    "    vec2 off = clamp((f - 0.5) * sc, -0.5, 0.5);\n"
    "    vec2 p = floor(t) + 0.5 + off;\n"
    "    p = mix(t, p, step(vec2(1.0), sc));\n"
    "    color = vec4(texture(tex, p / texsize).rgb, 1.0);\n"
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

    if (!init) {
        const char *colon;

        init = 1;
        spec = getenv("EIDOS_GL_DUMP");
        if (spec != NULL && (colon = strrchr(spec, ':')) != NULL &&
            (size_t)(colon - spec) < sizeof(path)) {
            memcpy(path, spec, (size_t)(colon - spec));
            path[colon - spec] = '\0';
            at = atoi(colon + 1);
        } else {
            spec = NULL;
        }
    }
    if (spec == NULL || ++frame != at) {
        return;
    }
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
    GLuint vs, fs;
    GLint ok = 0;
    int i;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    g = (EidosGl *)calloc(1, sizeof(*g));
    if (g == NULL) {
        return NULL;
    }
    g->window = window;
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

    vs = compile(g, GL_VERTEX_SHADER, k_vs);
    fs = compile(g, GL_FRAGMENT_SHADER, k_fs);
    if (vs == 0 || fs == 0) {
        eidos_gl_destroy(g);
        return NULL;
    }
    g->prog = g->glCreateProgram();
    g->glAttachShader(g->prog, vs);
    g->glAttachShader(g->prog, fs);
    g->glLinkProgram(g->prog);
    g->glDeleteShader(vs);
    g->glDeleteShader(fs);
    g->glGetProgramiv(g->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];

        g->glGetProgramInfoLog(g->prog, sizeof(log), NULL, log);
        fprintf(stderr, "eidos: GL link: %s\n", log);
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
    if (glGetError() != GL_NO_ERROR) {
        eidos_gl_destroy(g);
        return NULL;
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
        if (g->vao != 0 && g->glDeleteVertexArrays != NULL) {
            g->glDeleteVertexArrays(1, &g->vao);
        }
        if (g->prog != 0 && g->glDeleteProgram != NULL) {
            g->glDeleteProgram(g->prog);
        }
        SDL_GL_DeleteContext(g->ctx);
    }
    free(g);
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

void eidos_gl_present(EidosGl *g, int out_w, int out_h, const SDL_Rect dst[2])
{
    int i;

    if (g == NULL || out_w <= 0 || out_h <= 0) {
        return;
    }
    glViewport(0, 0, out_w, out_h);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    g->glUseProgram(g->prog);
    g->glBindVertexArray(g->vao);
    g->glUniform1i(g->u_tex, 0);
    for (i = 0; i < 2; i++) {
        float x0, y0, x1, y1;

        if (dst[i].w <= 0 || dst[i].h <= 0 || g->tw[i] == 0) {
            continue;
        }
        x0 = 2.f * (float)dst[i].x / (float)out_w - 1.f;
        x1 = 2.f * (float)(dst[i].x + dst[i].w) / (float)out_w - 1.f;
        y1 = 1.f - 2.f * (float)dst[i].y / (float)out_h;
        y0 = 1.f - 2.f * (float)(dst[i].y + dst[i].h) / (float)out_h;
        g->glUniform4f(g->u_rect, x0, y0, x1, y1);
        g->glUniform2f(g->u_texsize, (float)g->tw[i], (float)g->th[i]);
        g->glUniform2f(g->u_dstsize, (float)dst[i].w, (float)dst[i].h);
        g->glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g->tex[i]);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    gl_debug_dump(out_w, out_h);
    SDL_GL_SwapWindow(g->window);
}
