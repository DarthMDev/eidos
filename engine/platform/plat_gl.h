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

#endif
