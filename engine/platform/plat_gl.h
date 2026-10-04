/*
 * engine/platform/plat_gl.h -- the GPU presenter.
 *
 * The two DS screens are uploaded as RGBA textures (any size, so the 2D can
 * stay 256x192 while a 3D layer is larger) and drawn by one fragment shader
 * to the window at its own resolution.  The shader keeps pixels sharp at any
 * scale, with soft edges only where the ratio is fractional.  The CPU only
 * uploads; all scaling happens on the GPU.  OpenGL 3.3 core (the context SDL
 * creates on Windows, Linux and macOS).  If it cannot be created the
 * platform layer falls back to SDL's renderer.
 */
#ifndef EIDOS_PLAT_GL_H
#define EIDOS_PLAT_GL_H

#include <SDL.h>

typedef struct EidosGl EidosGl;

/* The window must have been created with SDL_WINDOW_OPENGL.  NULL on any
 * failure (the caller then recreates the window without the flag). */
EidosGl *eidos_gl_create(SDL_Window *window);
void eidos_gl_destroy(EidosGl *gl);

/* Replace screen `i`'s image: w x h RGBA bytes. */
void eidos_gl_upload(EidosGl *gl, int i, const unsigned char *rgba, int w,
                     int h);

/* Clear and draw both screens into destination rectangles (window pixels,
 * y down) of an output out_w x out_h, then swap. */
void eidos_gl_present(EidosGl *gl, int out_w, int out_h, const SDL_Rect dst[2]);

/* Draw both screens as present does, but read the window's pixels instead of
 * swapping: rgb is out_w x out_h x 3 bytes, top-down.  0 on success. */
int eidos_gl_grab(EidosGl *gl, int out_w, int out_h, const SDL_Rect dst[2],
                  unsigned char *rgb);

/* The GPU 3D rasteriser (plat_gl3d.c).  has3d is false when the optional part
 * failed to build; then the platform keeps drawing screens only. */
struct G3dSnapshot;
int eidos_gl_has3d(const EidosGl *gl);
/* Draw the snapshot at w x h; 0 on success. */
int eidos_gl_render3d(EidosGl *gl, const struct G3dSnapshot *snap, int w, int h);
/* Make screen `screen` (0 top, 1 bottom) show the last rendered 3D frame over
 * `behind`, the screen at DS resolution with its 3D layer off; bmode 0/1/2 and
 * bfac 0..1 are the engine's master brightness.  screen < 0 turns it off. */
void eidos_gl_set_3d(EidosGl *gl, int screen, const unsigned char *behind,
                     int w, int h, int bmode, float bfac, int dilate);

/* Test aid: the last 3D frame as top-down, non-premultiplied RGBA, w x h as
 * rendered (the size given to eidos_gl_render3d).  0 on success. */
int eidos_gl_read3d(EidosGl *gl, unsigned char *out, int w, int h);

/* The 3D frame at DS size (256x192 top-down RGBA, what the CPU rasteriser
 * would have drawn), for the 2D compositor and the display capture.  0 new
 * data, 1 unchanged since the last call, -1 failure. */
int eidos_gl_read3d_1x(EidosGl *gl, const struct G3dSnapshot *snap,
                       unsigned char *out, int async);

#endif
