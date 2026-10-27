/*
 * demo/bmap_npc_dump.c -- a check for engine/world/bmap_npc.c: lists the NPCs
 * of a zone and writes each picture as a 24-bit BMP on a green ground.
 *
 *   bmap_npc_dump <data root> <zone> <out dir>
 */

#include <stdio.h>
#include <stdlib.h>

#include <math.h>
#include <string.h>

#include "bmap_npc.h"
#include "port.h"

static void put32(FILE *f, unsigned v)
{
    fputc((int)(v & 255u), f);
    fputc((int)((v >> 8) & 255u), f);
    fputc((int)((v >> 16) & 255u), f);
    fputc((int)((v >> 24) & 255u), f);
}

static void write_bmp(const char *path, const BmapNpc *n)
{
    FILE *f = fopen(path, "wb");
    int x, y, pad = (4 - (n->w * 3) % 4) % 4;
    unsigned size = 54u + (unsigned)((n->w * 3 + pad) * n->h);

    if (f == NULL) {
        return;
    }
    fputs("BM", f);
    put32(f, size);
    put32(f, 0u);
    put32(f, 54u);
    put32(f, 40u);
    put32(f, (unsigned)n->w);
    put32(f, (unsigned)n->h);
    put32(f, 0x00180001u);
    put32(f, 0u);
    put32(f, size - 54u);
    put32(f, 2835u);
    put32(f, 2835u);
    put32(f, 0u);
    put32(f, 0u);
    for (y = n->h - 1; y >= 0; y--) {
        for (x = 0; x < n->w; x++) {
            const unsigned char *p = n->rgba + ((size_t)y * (size_t)n->w + (size_t)x) * 4u;
            unsigned a = p[3];
            static const unsigned char bg[3] = { 90, 140, 90 };

            fputc((int)((p[2] * a + bg[2] * (255u - a)) / 255u), f);
            fputc((int)((p[1] * a + bg[1] * (255u - a)) / 255u), f);
            fputc((int)((p[0] * a + bg[0] * (255u - a)) / 255u), f);
        }
        for (x = 0; x < pad; x++) {
            fputc(0, f);
        }
    }
    fclose(f);
}

/* A front view (looking along -z, x right, y up) of the models added to a
 * scene, 8 pixels a map unit, as a BMP; also prints what was added. */
static void models_view(BmapScene *s, unsigned first_v, unsigned first_d, const char *path,
                        const BmapNpc *n)
{
    enum { W = 320, H = 320 };
    static unsigned char img[H][W][3];
    static float zb[H][W];
    unsigned di, k;
    FILE *f;
    int x, y;

    memset(img, 90, sizeof img);
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            zb[y][x] = -1e30f;
        }
    }
    for (di = first_d; di < s->nd; di++) {
        const BmapDraw *dr = &s->d[di];

        if (dr->first < first_v) {
            continue;
        }
        for (k = dr->first; k + 3 <= dr->first + dr->count; k += 3) {
            const BmapVertex *v = &s->v[k];
            float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
            float px[3], py[3];
            int i, x0, x1, y0, y1;

            for (i = 0; i < 3; i++) {
                px[i] = W / 2 + (v[i].x - n->x) * 8.0f;
                py[i] = H - 20 - (v[i].y - n->y) * 8.0f;
                if (px[i] < minx) minx = px[i];
                if (px[i] > maxx) maxx = px[i];
                if (py[i] < miny) miny = py[i];
                if (py[i] > maxy) maxy = py[i];
            }
            x0 = (int)fmaxf(0, floorf(minx)); x1 = (int)fminf(W - 1, ceilf(maxx));
            y0 = (int)fmaxf(0, floorf(miny)); y1 = (int)fminf(H - 1, ceilf(maxy));
            for (y = y0; y <= y1; y++) {
                for (x = x0; x <= x1; x++) {
                    float d0 = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
                    float w1 = ((x - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (y - py[0])) / d0;
                    float w2 = ((px[1] - px[0]) * (y - py[0]) - (x - px[0]) * (py[1] - py[0])) / d0;
                    float w0 = 1.0f - w1 - w2, z, u, vv;
                    unsigned char c[4] = { 255, 255, 255, 255 };

                    if (d0 == 0.0f || w0 < 0 || w1 < 0 || w2 < 0) {
                        continue;
                    }
                    z = w0 * v[0].z + w1 * v[1].z + w2 * v[2].z;
                    if (z < zb[y][x]) {
                        continue;
                    }
                    if (dr->tex >= 0) {
                        const BmapTexture *t = &s->t[dr->tex];
                        int tx, ty;

                        u = w0 * v[0].u + w1 * v[1].u + w2 * v[2].u;
                        vv = w0 * v[0].v + w1 * v[1].v + w2 * v[2].v;
                        u -= floorf(u);
                        vv -= floorf(vv);
                        tx = (int)(u * t->w) % t->w;
                        ty = (int)(vv * t->h) % t->h;
                        memcpy(c, t->rgba + ((size_t)ty * (size_t)t->w + (size_t)tx) * 4u, 4);
                    }
                    if (c[3] < 128) {
                        continue;
                    }
                    zb[y][x] = z;
                    img[y][x][0] = (unsigned char)(c[0] * (w0 * v[0].r + w1 * v[1].r + w2 * v[2].r) / 255.0f);
                    img[y][x][1] = (unsigned char)(c[1] * (w0 * v[0].g + w1 * v[1].g + w2 * v[2].g) / 255.0f);
                    img[y][x][2] = (unsigned char)(c[2] * (w0 * v[0].b + w1 * v[1].b + w2 * v[2].b) / 255.0f);
                }
            }
        }
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        return;
    }
    fputs("BM", f);
    put32(f, 54u + W * H * 3u);
    put32(f, 0u);
    put32(f, 54u);
    put32(f, 40u);
    put32(f, W);
    put32(f, H);
    put32(f, 0x00180001u);
    put32(f, 0u);
    put32(f, W * H * 3u);
    put32(f, 2835u);
    put32(f, 2835u);
    put32(f, 0u);
    put32(f, 0u);
    for (y = H - 1; y >= 0; y--) {
        for (x = 0; x < W; x++) {
            fputc(img[y][x][2], f);
            fputc(img[y][x][1], f);
            fputc(img[y][x][0], f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    BmapNpc *npcs;
    int count, i;
    unsigned zone;

    if (argc < 4) {
        fprintf(stderr, "usage: bmap_npc_dump <data root> <zone> <out dir>\n");
        return 2;
    }
    bw_set_data_root(argv[1]);
    zone = (unsigned)atoi(argv[2]);
    if (bmap_npc_load(zone, &npcs, &count) != 0) {
        fprintf(stderr, "zone %u: load failed\n", zone);
        return 1;
    }
    for (i = 0; i < count; i++) {
        const BmapNpc *n = &npcs[i];
        char path[600];

        printf("%2d code %4u dir %d trainer %d at (%.1f %.1f %.1f) pic %dx%d world %.0fx%.0f\n",
               i, n->obj_code, n->dir, n->is_trainer, n->x, n->y, n->z, n->w, n->h,
               n->world_w, n->world_h);
        if (n->rgba != NULL) {
            snprintf(path, sizeof path, "%s/z%u_%02d.bmp", argv[3], zone, i);
            write_bmp(path, n);
        }
    }
    if (argc > 4) {
        /* the 3D objects: build the map at the first one, add the models, view them */
        BmapScene s;

        for (i = 0; i < count && npcs[i].model_file < 0; i++) {
        }
        if (i < count && bmap_build(&s, zone, npcs[i].x, npcs[i].z, 0, 12, 0) == 0) {
            unsigned v0 = s.nv, d0 = s.nd, t0 = s.nt;
            char path[600];
            int bad = bmap_npc_add_models(&s, npcs, count);

            printf("models: %d failed, +%u vertices, +%u draws, +%u textures\n", bad,
                   s.nv - v0, s.nd - d0, s.nt - t0);
            snprintf(path, sizeof path, "%s/z%u_model.bmp", argv[3], zone);
            models_view(&s, v0, 0, path, &npcs[i]);
            bmap_free(&s);
        }
    }
    {
        /* the player: hero 1, heroine 4 */
        unsigned code;

        for (code = 1; code <= 4; code += 3) {
            BmapNpc h;

            memset(&h, 0, sizeof h);
            if (bmap_npc_picture(code, 1, &h.rgba, &h.w, &h.h, &h.world_w, &h.world_h) == 0) {
                char path[600];

                snprintf(path, sizeof path, "%s/player_%u.bmp", argv[3], code);
                write_bmp(path, &h);
                free(h.rgba);
            } else {
                printf("player code %u: no picture\n", code);
            }
        }
    }
    bmap_npc_free(npcs, count);
    return 0;
}
