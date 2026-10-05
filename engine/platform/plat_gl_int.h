/*
 * engine/platform/plat_gl_int.h -- the GPU presenter's private state, shared
 * by plat_gl.c (screens) and plat_gl3d.c (the 3D rasteriser).
 */
#ifndef EIDOS_PLAT_GL_INT_H
#define EIDOS_PLAT_GL_INT_H

#include <SDL.h>
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
    X(PFNGLUNIFORM1FPROC, glUniform1f) \
    X(PFNGLUNIFORM2FPROC, glUniform2f) \
    X(PFNGLUNIFORM2IPROC, glUniform2i) \
    X(PFNGLUNIFORM4FPROC, glUniform4f) \
    X(PFNGLUNIFORM4IPROC, glUniform4i) \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv) \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers) \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer) \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) \
    X(PFNGLBINDBUFFERPROC, glBindBuffer) \
    X(PFNGLBUFFERDATAPROC, glBufferData) \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLMAPBUFFERPROC, glMapBuffer) \
    X(PFNGLUNMAPBUFFERPROC, glUnmapBuffer) \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample) \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer) \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)

typedef struct GlFb {
    GLuint fbo, color, depth;
    int w, h;
    GLuint ms_fbo, ms_color, ms_depth;   /* the multisampled target (anti-aliasing) */
    int ms_w, ms_h;
} GlFb;

struct EidosGl {
    SDL_Window *window;
    SDL_GLContext ctx;
#define X(T, N) T N;
    GL_FUNCS(X)
#undef X

    /* the screen presenter */
    GLuint prog;
    GLuint vao;
    GLint u_rect, u_tex, u_texsize, u_dstsize;
    GLuint tex[2];
    int tw[2], th[2];

    /* the 3D composite (a screen whose 3D layer the GPU draws) */
    GLuint prog3;
    GLint u3_rect, u3_tex, u3_behind, u3_g3d, u3_texsize, u3_dstsize,
          u3_bmode, u3_bfac, u3_dil;
    int s3d;                       /* screen index, -1 none                */
    GLuint behind_tex;
    int behind_w, behind_h;
    int bmode;                     /* master brightness: 0 off, 1 up, 2 down */
    float bfac;                    /* 0..1                                  */
    int dilate;                    /* mask dilated by a pixel (1) or exact (0) */

    /* the high-resolution display capture (eidos_gl_capture) */
    GLuint progc;
    GLint uc_rect, uc_a, uc_b, uc_eva, uc_evb, uc_bflip, uc_mode, uc_bmode,
          uc_bfac;
    GLuint cap_fbo, capa_tex, cap_tex[2], cap_src[3];
    int cap_w, cap_h, cap_cur, cap_valid;
    int cap_screen;                /* screen that shows the capture, -1 none */
    int cap_bmode;
    float cap_bfac;

    /* the 3D rasteriser (plat_gl3d.c) */
    int have3d;
    GLuint p3d;
    GLint u_mode, u_alpha5, u_tex3, u_texture, u_texsize3, u_wrap, u_alpharef,
          u_toon, u_highlight, u_toontab, u_front, u_back, u_wbuf, u_pass;
    GlFb fb[2];                    /* 0: the display size, 1: DS size (readback) */
    unsigned read_serial;          /* snapshot serial last read back         */
    GLuint pbo;                    /* async readback target                  */
    int pbo_pending;               /* a readback is in flight in the pbo     */
    int read_valid;
    GLuint vbo3, vao3;
    GLuint *gtex;
    unsigned n_gtex, cap_gtex;
    float *vtx;
    size_t cap_vtx;
};

/* plat_gl3d.c */
int eidos_gl3d_init(EidosGl *g);
void eidos_gl3d_destroy(EidosGl *g);
GLuint eidos_gl_compile_program(EidosGl *g, const char *vs, const char *fs);

#endif
