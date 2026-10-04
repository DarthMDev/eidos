/*
 * engine/platform/plat_gl3d.c -- the DS 3D engine on the GPU.
 *
 * Draws a G3dSnapshot (engine/g3d/hw3d.h) into a framebuffer at any size,
 * following engine/g3d/raster.c rule for rule: clip-space vertices (the GPU
 * clips and divides), perspective-correct vertex colour and texture
 * coordinates, nearest texel fetch with the repeat, flip and clamp modes,
 * modulate / decal / toon / highlight shading on 6-bit colour and 5-bit alpha,
 * the alpha test, the z or w depth buffer, front / back culling on the
 * projected winding.  The result is premultiplied RGBA (a translucent pixel
 * over the clear plane keeps its own alpha, as the 2D engine expects).
 *
 * Not done on the GPU (the snapshot says so in `unsupported` and the caller
 * shows the CPU frame instead): edge marking, fog, the rear-plane bitmap,
 * shadow polygons.  Anti-aliasing is the resolution itself.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat_gl_int.h"
#include "hw3d.h"
#include "prof.h"

#define FLOATS_PER_VERTEX 9u

static const char *k_vs3 =
    "#version 330 core\n"
    "layout(location = 0) in vec4 a_pos;\n"
    "layout(location = 1) in vec2 a_st;\n"
    "layout(location = 2) in vec3 a_rgb;\n"
    "out vec2 v_st;\n"
    "out vec3 v_rgb;\n"
    "void main() {\n"
    "    v_st = a_st;\n"
    "    v_rgb = a_rgb;\n"
    "    gl_Position = a_pos;\n"
    "}\n";

static const char *k_fs3 =
    "#version 330 core\n"
    "in vec2 v_st;\n"
    "in vec3 v_rgb;\n"
    "out vec4 o;\n"
    "uniform int u_mode;\n"          /* 0 modulate, 1 decal, 2 toon/highlight */
    "uniform int u_alpha5;\n"
    "uniform int u_tex;\n"
    "uniform sampler2D u_texture;\n"
    "uniform ivec2 u_texsize;\n"
    "uniform ivec4 u_wrap;\n"        /* repeat s, repeat t, flip s, flip t */
    "uniform int u_alpharef;\n"
    "uniform int u_toon;\n"
    "uniform int u_highlight;\n"
    "uniform vec3 u_toontab[32];\n"
    "uniform int u_front;\n"
    "uniform int u_back;\n"
    "uniform int u_wbuf;\n"
    "uniform int u_pass;\n"
    "int wrapc(int v, int size, int rep, int flip) {\n"
    "    if (size <= 0) { return 0; }\n"
    "    if (rep == 0) { return clamp(v, 0, size - 1); }\n"
    "    if (flip != 0) {\n"
    "        int k = v & (size * 2 - 1);\n"
    "        if (k >= size) { k = size * 2 - 1 - k; }\n"
    "        return k;\n"
    "    }\n"
    "    return v & (size - 1);\n"
    "}\n"
    "int mod6(int a8, int b8) {\n"
    "    int a6 = a8 >> 2;\n"
    "    int b6 = b8 >> 2;\n"
    "    int r = ((a6 + 1) * (b6 + 1) - 1) >> 6;\n"
    "    r = min(r, 63);\n"
    "    return r * 255 / 63;\n"
    "}\n"
    "int mod5(int a, int b) {\n"
    "    int r = ((a + 1) * (b + 1) - 1) >> 5;\n"
    "    return min(r, 31);\n"
    "}\n"
    "void main() {\n"
    "    if (gl_FrontFacing) { if (u_front == 0) { discard; } }\n"
    "    else { if (u_back == 0) { discard; } }\n"
    "    ivec3 c = clamp(ivec3(floor(v_rgb + 0.001)), ivec3(0), ivec3(255));\n"
    "    ivec3 hl = ivec3(0);\n"
    "    if (u_toon == 1) {\n"
    "        ivec3 e = ivec3(u_toontab[c.r >> 3] + 0.5);\n"
    "        if (u_highlight == 1) { hl = e; c.g = c.r; c.b = c.r; }\n"
    "        else { c = e; }\n"
    "    }\n"
    "    int a5 = u_alpha5;\n"
    "    ivec3 col = c;\n"
    "    if (u_tex == 1) {\n"
    "        int x = wrapc(int(floor(v_st.x / 16.0)), u_texsize.x, u_wrap.x, u_wrap.z);\n"
    "        int y = wrapc(int(floor(v_st.y / 16.0)), u_texsize.y, u_wrap.y, u_wrap.w);\n"
    "        ivec4 tc = ivec4(texelFetch(u_texture, ivec2(x, y), 0) * 255.0 + 0.5);\n"
    "        if (u_mode == 1) {\n"
    "            int ta = tc.a >> 3;\n"
    "            col = (tc.rgb * ta + c * (31 - ta)) / 31;\n"
    "        } else {\n"
    "            col = ivec3(mod6(tc.r, c.r), mod6(tc.g, c.g), mod6(tc.b, c.b));\n"
    "            a5 = mod5(tc.a >> 3, u_alpha5);\n"
    "        }\n"
    "    }\n"
    "    col = min(col + hl, ivec3(255));\n"
    "    if (u_pass == 1 && a5 < 31) { discard; }\n"
    "    if (u_pass == 2 && a5 >= 31) { discard; }\n"
    "    if (a5 == 0 || a5 <= u_alpharef) { discard; }\n"
    "    float a = float(a5) / 31.0;\n"
    "    o = vec4(vec3(col) / 255.0 * a, a);\n"
    "    gl_FragDepth = (u_wbuf == 1) ? clamp((1.0 / gl_FragCoord.w) / 16777216.0, 0.0, 1.0)\n"
    "                                 : gl_FragCoord.z;\n"
    "}\n";

static u8 c5to8(u32 v)
{
    v &= 31u;
    return (u8)((v << 3) | (v >> 2));
}

int eidos_gl3d_init(EidosGl *g)
{
    g->p3d = eidos_gl_compile_program(g, k_vs3, k_fs3);
    if (g->p3d == 0) {
        return -1;
    }
#define LOC(field, name) g->field = g->glGetUniformLocation(g->p3d, name)
    LOC(u_mode, "u_mode");
    LOC(u_alpha5, "u_alpha5");
    LOC(u_tex3, "u_tex");
    LOC(u_texture, "u_texture");
    LOC(u_texsize3, "u_texsize");
    LOC(u_wrap, "u_wrap");
    LOC(u_alpharef, "u_alpharef");
    LOC(u_toon, "u_toon");
    LOC(u_highlight, "u_highlight");
    LOC(u_toontab, "u_toontab");
    LOC(u_front, "u_front");
    LOC(u_back, "u_back");
    LOC(u_wbuf, "u_wbuf");
    LOC(u_pass, "u_pass");
#undef LOC
    g->glGenFramebuffers(1, &g->fb[0].fbo);
    g->glGenFramebuffers(1, &g->fb[1].fbo);
    g->glGenVertexArrays(1, &g->vao3);
    g->glGenBuffers(1, &g->vbo3);
    g->have3d = 1;
    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

void eidos_gl3d_destroy(EidosGl *g)
{
    unsigned i;

    for (i = 0; i < 2; i++) {
        GlFb *f = &g->fb[i];

        if (f->fbo != 0) {
            g->glDeleteFramebuffers(1, &f->fbo);
        }
        if (f->depth != 0) {
            g->glDeleteRenderbuffers(1, &f->depth);
        }
        if (f->color != 0) {
            glDeleteTextures(1, &f->color);
        }
        if (f->ms_fbo != 0) {
            g->glDeleteFramebuffers(1, &f->ms_fbo);
            g->glDeleteRenderbuffers(1, &f->ms_color);
            g->glDeleteRenderbuffers(1, &f->ms_depth);
        }
    }
    if (g->vbo3 != 0) {
        g->glDeleteBuffers(1, &g->vbo3);
    }
    if (g->vao3 != 0) {
        g->glDeleteVertexArrays(1, &g->vao3);
    }
    if (g->p3d != 0) {
        g->glDeleteProgram(g->p3d);
    }
    for (i = 0; i < g->n_gtex; i++) {
        glDeleteTextures(1, &g->gtex[i]);
    }
    free(g->gtex);
    free(g->vtx);
}

static int fbo_resize(EidosGl *g, GlFb *f, int w, int h)
{
    if (f->w == w && f->h == h && f->color != 0) {
        return 0;
    }
    if (f->color == 0) {
        glGenTextures(1, &f->color);
        g->glGenRenderbuffers(1, &f->depth);
    }
    glBindTexture(GL_TEXTURE_2D, f->color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g->glBindRenderbuffer(GL_RENDERBUFFER, f->depth);
    g->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    g->glBindFramebuffer(GL_FRAMEBUFFER, f->fbo);
    g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, f->color, 0);
    g->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_RENDERBUFFER, f->depth);
    if (g->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return -1;
    }
    f->w = w;
    f->h = h;
    return 0;
}

/*
 * DISP3DCNT bit 4 turns on the hardware's edge anti-aliasing.  The GPU's
 * equivalent is multisampling: render into a 4-sample target and resolve it
 * into the frame's texture.  Not worth its memory at a large size, where the
 * resolution does the same job.
 */
#define MSAA_SAMPLES 4
#define MSAA_MAX_PIXELS (2048 * 1536)

static int msaa_target(EidosGl *g, GlFb *f, int w, int h)
{
    if ((long)w * (long)h > MSAA_MAX_PIXELS) {
        return 0;
    }
    if (f->ms_fbo == 0) {
        g->glGenFramebuffers(1, &f->ms_fbo);
        g->glGenRenderbuffers(1, &f->ms_color);
        g->glGenRenderbuffers(1, &f->ms_depth);
    }
    if (f->ms_w != w || f->ms_h != h) {
        g->glBindRenderbuffer(GL_RENDERBUFFER, f->ms_color);
        g->glRenderbufferStorageMultisample(GL_RENDERBUFFER, MSAA_SAMPLES, GL_RGBA8, w, h);
        g->glBindRenderbuffer(GL_RENDERBUFFER, f->ms_depth);
        g->glRenderbufferStorageMultisample(GL_RENDERBUFFER, MSAA_SAMPLES, GL_DEPTH_COMPONENT24, w, h);
        g->glBindFramebuffer(GL_FRAMEBUFFER, f->ms_fbo);
        g->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, f->ms_color);
        g->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, f->ms_depth);
        f->ms_w = w;
        f->ms_h = h;
    }
    g->glBindFramebuffer(GL_FRAMEBUFFER, f->ms_fbo);
    if (g->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        f->ms_w = 0;
        return 0;
    }
    return 1;
}

#define ATTR_DEFAULT (((u32)1 << 6) | ((u32)1 << 7) | ((u32)31 << 16))

/* Render the snapshot at w x h into the framebuffer; the colour texture is
 * then eidos_gl_3d_texture(g).  Returns 0, or -1 if the GPU path cannot
 * draw this frame. */
static int render_into(EidosGl *g, GlFb *fb, const G3dSnapshot *s, int w, int h)
{
    unsigned i, nv = 0;
    size_t need;
    u32 cc, alpha;
    u8 cr, cg, cb;
    int bound_tex = -2;
    int prev_depth_eq = -1, prev_mask = -1;
    GLfloat toon[96];
    unsigned long long t_up, t_dr;
    int use_ms;

    if (g == NULL || !g->have3d || s == NULL || w < 1 || h < 1) {
        return -1;
    }
    if (fbo_resize(g, fb, w, h) != 0) {
        return -1;
    }

    /* textures: one GL texture per snapshot texture */
    while (g->n_gtex < s->n_tex) {
        if (g->n_gtex == g->cap_gtex) {
            unsigned c = g->cap_gtex ? g->cap_gtex * 2u : 16u;
            GLuint *n = (GLuint *)realloc(g->gtex, (size_t)c * sizeof(*n));

            if (n == NULL) {
                return -1;
            }
            g->gtex = n;
            g->cap_gtex = c;
        }
        glGenTextures(1, &g->gtex[g->n_gtex]);
        glBindTexture(GL_TEXTURE_2D, g->gtex[g->n_gtex]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g->n_gtex++;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    t_up = eidos_prof_begin();
    for (i = 0; i < s->n_tex; i++) {
        glBindTexture(GL_TEXTURE_2D, g->gtex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)s->tex[i].w,
                     (GLsizei)s->tex[i].h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     s->tex[i].rgba);
    }

    eidos_prof_end(EIDOS_PROF_GLUP, t_up);
    t_dr = eidos_prof_begin();
    /* vertices: each polygon as a fan of triangles */
    need = (size_t)s->n_polys * 6u * FLOATS_PER_VERTEX;
    if (need > g->cap_vtx) {
        float *n = (float *)realloc(g->vtx, need * sizeof(float));

        if (n == NULL) {
            return -1;
        }
        g->vtx = n;
        g->cap_vtx = need;
    }
    for (i = 0; i < s->n_polys; i++) {
        const G3dPoly *p = &s->polys[i];
        static const unsigned fan[6] = { 0, 1, 2, 0, 2, 3 };
        unsigned tri, k, ntri;

        if (p->count < 3u || p->count > 4u) {
            continue;
        }
        ntri = p->count - 2u;
        for (tri = 0; tri < ntri * 3u; tri++) {
            const G3dVertex *v = &s->verts[s->idx[p->first + fan[tri]]];
            float *o = g->vtx + (size_t)nv * FLOATS_PER_VERTEX;

            o[0] = (float)v->x; o[1] = (float)v->y;
            o[2] = (float)v->z; o[3] = (float)v->w;
            o[4] = (float)v->s; o[5] = (float)v->t;
            o[6] = (float)v->r; o[7] = (float)v->g; o[8] = (float)v->b;
            nv++;
            (void)k;
        }
    }

    use_ms = (s->disp3dcnt & 0x10u) != 0 && msaa_target(g, fb, w, h);
    if (!use_ms) {
        g->glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    }
    glViewport(0, 0, w, h);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    /* the clear plane: premultiplied colour, alpha as set */
    cc = s->clear_color;
    alpha = ((cc >> 16) & 0x1Fu) * 255u / 31u;
    cr = c5to8(cc);
    cg = c5to8(cc >> 5);
    cb = c5to8(cc >> 10);
    glClearColor((float)cr / 255.f * (float)alpha / 255.f,
                 (float)cg / 255.f * (float)alpha / 255.f,
                 (float)cb / 255.f * (float)alpha / 255.f,
                 (float)alpha / 255.f);
    glClearDepth(s->wbuffer ? (double)(s->clear_depth & 0x7FFFu) / 32768.0
                            : ((s->clear_depth & 0x7FFFu) == 0x7FFFu
                               ? 1.0
                               : (double)(s->clear_depth & 0x7FFFu) / 32767.0));
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (nv > 0) {
        unsigned drawn = 0, first = 0;

        g->glBindVertexArray(g->vao3);
        g->glBindBuffer(GL_ARRAY_BUFFER, g->vbo3);
        g->glBufferData(GL_ARRAY_BUFFER,
                        (GLsizeiptr)((size_t)nv * FLOATS_PER_VERTEX * sizeof(float)),
                        g->vtx, GL_STREAM_DRAW);
        g->glEnableVertexAttribArray(0);
        g->glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE,
                                 FLOATS_PER_VERTEX * sizeof(float), (void *)0);
        g->glEnableVertexAttribArray(1);
        g->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                                 FLOATS_PER_VERTEX * sizeof(float),
                                 (void *)(4 * sizeof(float)));
        g->glEnableVertexAttribArray(2);
        g->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE,
                                 FLOATS_PER_VERTEX * sizeof(float),
                                 (void *)(6 * sizeof(float)));

        g->glUseProgram(g->p3d);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        g->glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                               GL_ONE_MINUS_SRC_ALPHA);
        for (i = 0; i < 96u; i++) {
            toon[i] = (float)s->toon[i];
        }
        g->glUniform3fv(g->u_toontab, 32, toon);
        g->glUniform1i(g->u_texture, 0);
        g->glActiveTexture(GL_TEXTURE0);
        g->glUniform1i(g->u_alpharef, (GLint)s->alpha_ref);
        g->glUniform1i(g->u_highlight, (GLint)s->highlight);
        g->glUniform1i(g->u_wbuf, (GLint)s->wbuffer);

        for (i = 0; i < s->n_polys; i++) {
            const G3dPoly *p = &s->polys[i];
            u32 attr = p->attr, mode, a5, ti;
            u32 verts;
            s32 slot = s->poly_tex[i];
            int deq, mask;

            if (p->count < 3u || p->count > 4u) {
                continue;
            }
            verts = (p->count - 2u) * 3u;
            if (attr == 0) {
                attr = ATTR_DEFAULT;
            }
            mode = G3D_ATTR_MODE(attr);
            a5 = G3D_ATTR_ALPHA(attr);
            if (a5 == 0 || mode == G3D_POLY_MODE_SHADOW) {
                first += verts;
                continue;
            }
            ti = p->teximage;
            deq = G3D_ATTR_DEPTH_EQ(attr) != 0;
            if (deq != prev_depth_eq) {
                glDepthFunc(deq ? GL_LEQUAL : GL_LESS);
                prev_depth_eq = deq;
            }
            mask = (a5 >= 31u) || G3D_ATTR_XLU_DEPTH(attr);
            if (mask != prev_mask) {
                glDepthMask(mask ? GL_TRUE : GL_FALSE);
                prev_mask = mask;
            }
            g->glUniform1i(g->u_mode, (GLint)(mode == G3D_POLY_MODE_DECAL ? 1
                                      : mode == G3D_POLY_MODE_TOON ? 2 : 0));
            g->glUniform1i(g->u_toon, mode == G3D_POLY_MODE_TOON ? 1 : 0);
            g->glUniform1i(g->u_alpha5, (GLint)a5);
            g->glUniform1i(g->u_front, G3D_ATTR_FRONT(attr) ? 1 : 0);
            g->glUniform1i(g->u_back, G3D_ATTR_BACK(attr) ? 1 : 0);
            if (slot >= 0 && (s->disp3dcnt & 1u)) {
                if (bound_tex != slot) {
                    glBindTexture(GL_TEXTURE_2D, g->gtex[slot]);
                    bound_tex = slot;
                }
                g->glUniform1i(g->u_tex3, 1);
                g->glUniform2i(g->u_texsize3, (GLint)s->tex[slot].w,
                               (GLint)s->tex[slot].h);
                g->glUniform4i(g->u_wrap, (GLint)G3D_TEX_REPEAT_S(ti),
                               (GLint)G3D_TEX_REPEAT_T(ti),
                               (GLint)G3D_TEX_FLIP_S(ti),
                               (GLint)G3D_TEX_FLIP_T(ti));
            } else {
                g->glUniform1i(g->u_tex3, 0);
            }
            if (a5 >= 31u && !G3D_ATTR_XLU_DEPTH(attr) && slot >= 0
                && (s->disp3dcnt & 1u) && s->tex[slot].partial) {
                /* An opaque polygon whose texture has partly transparent
                 * texels: the hardware decides per pixel from the final alpha,
                 * so the opaque texels write depth and the translucent ones
                 * do not.  One draw cannot do both: two passes can. */
                glDepthMask(GL_TRUE);
                g->glUniform1i(g->u_pass, 1);
                glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)verts);
                glDepthMask(GL_FALSE);
                g->glUniform1i(g->u_pass, 2);
                glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)verts);
                g->glUniform1i(g->u_pass, 0);
                prev_mask = 0;
            } else {
                glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)verts);
            }
            first += verts;
            drawn++;
        }
        (void)drawn;
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    if (use_ms) {
        /* resolve the samples into the frame's texture */
        g->glBindFramebuffer(GL_READ_FRAMEBUFFER, fb->ms_fbo);
        g->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb->fbo);
        g->glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    eidos_prof_end(EIDOS_PROF_GLDRAW, t_dr);
    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

int eidos_gl_render3d(EidosGl *g, const G3dSnapshot *s, int w, int h)
{
    return render_into(g, &g->fb[0], s, w, h);
}

static void unpremultiply(const unsigned char *raw, unsigned char *out,
                          int w, int h)
{
    int x, y;

    for (y = 0; y < h; y++) {
        const unsigned char *s = raw + (size_t)(h - 1 - y) * (size_t)w * 4u;
        unsigned char *d = out + (size_t)y * (size_t)w * 4u;

        for (x = 0; x < w; x++) {
            unsigned a = s[x * 4 + 3];

            if (a == 255u) {
                d[x * 4 + 0] = s[x * 4 + 0];
                d[x * 4 + 1] = s[x * 4 + 1];
                d[x * 4 + 2] = s[x * 4 + 2];
                d[x * 4 + 3] = 255;
            } else if (a == 0) {
                d[x * 4 + 0] = d[x * 4 + 1] = d[x * 4 + 2] = d[x * 4 + 3] = 0;
            } else {
                unsigned c;

                for (c = 0; c < 3; c++) {
                    unsigned v = (s[x * 4 + c] * 255u + a / 2u) / a;

                    d[x * 4 + c] = (unsigned char)(v > 255u ? 255u : v);
                }
                d[x * 4 + 3] = (unsigned char)a;
            }
        }
    }
}

static int read_fb(EidosGl *g, const GlFb *fb, unsigned char *out, int w, int h)
{
    unsigned char *raw;
    unsigned long long t_rd;

    raw = (unsigned char *)malloc((size_t)w * (size_t)h * 4u);
    if (raw == NULL) {
        return -1;
    }
    t_rd = eidos_prof_begin();
    g->glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, raw);
    g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    eidos_prof_end(EIDOS_PROF_GLREAD, t_rd);
    t_rd = eidos_prof_begin();
    unpremultiply(raw, out, w, h);
    eidos_prof_end(EIDOS_PROF_UNPREM, t_rd);
    free(raw);
    return 0;
}

/* Start reading `fb` into the pack buffer without waiting for the GPU. */
static int read_fb_start(EidosGl *g, const GlFb *fb, int w, int h)
{
    if (g->pbo == 0) {
        g->glGenBuffers(1, &g->pbo);
    }
    g->glBindBuffer(GL_PIXEL_PACK_BUFFER, g->pbo);
    g->glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)((size_t)w * (size_t)h * 4u),
                    NULL, GL_STREAM_READ);
    g->glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void *)0);
    g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    g->pbo_pending = 1;
    return 0;
}

/* Collect the read started earlier (one frame ago). */
static int read_fb_finish(EidosGl *g, unsigned char *out, int w, int h)
{
    const unsigned char *raw;
    int rc = -1;

    if (!g->pbo_pending) {
        return 1;
    }
    g->glBindBuffer(GL_PIXEL_PACK_BUFFER, g->pbo);
    raw = (const unsigned char *)g->glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    if (raw != NULL) {
        unsigned long long t0 = eidos_prof_begin();

        unpremultiply(raw, out, w, h);
        eidos_prof_end(EIDOS_PROF_UNPREM, t0);
        g->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        rc = 0;
    }
    g->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    g->pbo_pending = 0;
    return rc;
}

int eidos_gl_read3d(EidosGl *g, unsigned char *out, int w, int h)
{
    if (g == NULL || out == NULL || g->fb[0].w != w || g->fb[0].h != h) {
        return -1;
    }
    return read_fb(g, &g->fb[0], out, w, h);
}

/* The frame at DS size, top-down non-premultiplied RGBA: what the software
 * rasteriser's frame is, for the 2D compositor and the display capture.
 * 1 = unchanged since the last call (out is not touched).  With `async` the
 * GPU is not waited for: the call returns the frame read one call ago (the
 * first call returns 1), so the picture lags a frame but the CPU never
 * stalls. */
int eidos_gl_read3d_1x(EidosGl *g, const G3dSnapshot *s, unsigned char *out,
                       int async)
{
    int got = 1;

    if (g == NULL || !g->have3d || s == NULL || out == NULL) {
        return -1;
    }
    if (g->read_valid && s->serial == g->read_serial) {
        return 1;
    }
    if (render_into(g, &g->fb[1], s, 256, 192) != 0) {
        g->read_valid = 0;
        return -1;
    }
    if (async) {
        got = read_fb_finish(g, out, 256, 192);
        read_fb_start(g, &g->fb[1], 256, 192);
    } else {
        g->pbo_pending = 0;
        if (read_fb(g, &g->fb[1], out, 256, 192) != 0) {
            g->read_valid = 0;
            return -1;
        }
        got = 0;
    }
    g->read_serial = s->serial;
    g->read_valid = 1;
    return got;
}
