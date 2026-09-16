/*
 * tests/test_view.c -- the demo, headless, and its screenshot checked.
 *
 * ctest runs this with SDL_VIDEODRIVER=dummy and SDL_AUDIODRIVER=dummy in the
 * environment (CMakeLists.txt sets them on the test), so the whole platform
 * layer runs on a machine with no display and no sound card: the window, the
 * textures, the frame pacer and SDL_SaveBMP all work under the dummy drivers,
 * and what comes out is a file this test can read.
 *
 *     ./test_view <eidos_view executable> <data root> <output BMP>
 *
 * What is checked:
 *
 *   - the demo runs a few frames and exits 0,
 *   - the BMP it wrote is 24-bit, bottom-up, and exactly 256 x 384: the two
 *     DS screens stacked, at 1x,
 *   - two screenshots, one after the first frame and one after the last,
 *     differ on the bottom screen and are identical on the top.  That is
 *     the check that game code ran: the bottom screen's pose is advanced
 *     along its NANR timeline by a task on the game's own task-callback
 *     list, and the frame loop's step is the matched runner
 *     (src/matched/sub_020310C4.c) walking that list.  The top screen has
 *     no task and must not move, which is what makes the bottom screen's
 *     difference mean something rather than being "the demo redrew".
 *   - neither half is blank.  "Blank" is not just "all black": a compositor
 *     that ran and drew nothing produces a screen of one flat colour, the
 *     backdrop.  So each half has to carry several distinct colours and a
 *     few hundred pixels that are not its own most common one.  The
 *     thresholds are deliberately low, because the bottom screen is one
 *     48x52 sprite on a backdrop: about 2500 pixels of 49152, of which the
 *     opaque ones are what "it drew something" means here.  What the picture
 *     *is* is not this test's business.
 *
 *   - a second run with --map 120 puts one a/0/0/8 map model on the top
 *     screen through engine/g3d, and that screen has to be non-blank and
 *     to differ from the 2D run's top screen.  What the geometry *is* is
 *     tests/test_g3d.c's business, against aletheia's own OBJ export.
 *
 * This is a smoke test of the platform layer, not a pixel oracle: what the
 * picture *is* is checked by test_bg and test_g2d against the Python
 * renderers, pixel for pixel.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"

#define SHOT_W 256u
#define SHOT_H 384u

/*
 * Twelve frames rather than five: the first screenshot is taken after frame
 * 1, so the two shots are eleven NANR frames apart, which is well inside
 * every sequence in species 25's rig (the shortest is eighteen frames) and
 * comfortably past the point where two adjacent frames could be the same
 * drawing.
 */
#define DEMO_FRAMES "12"

/* How many pixels of the bottom screen have to move between the two shots
 * before the animation counts as having happened.  One pose is about 2500
 * pixels; the threshold is a small fraction of that. */
#define MOVED_PIXELS 200u

typedef struct Bmp {
    u32 width, height;
    u32 bits;
    u8 *pixels;      /* top-down RGB triples, width*height*3 */
} Bmp;

/*
 * A 24-bit bottom-up BMP, which is what SDL_SaveBMP writes for an RGB24
 * surface.  Parsed by hand: the port has no image library and is not about
 * to grow one for a test.
 */
static int bmp_load(Bmp *out, const char *path)
{
    FILE *fp;
    u8 head[54];
    long size;
    u8 *raw = NULL;
    u32 offset, y, x, pitch;

    memset(out, 0, sizeof(*out));
    fp = fopen(path, "rb");
    if (fp == NULL) {
        printf("  cannot open %s\n", path);
        return -1;
    }
    if (fread(head, 1, sizeof(head), fp) != sizeof(head)
        || head[0] != 'B' || head[1] != 'M') {
        printf("  %s is not a BMP\n", path);
        fclose(fp);
        return -1;
    }
    offset = bw_rd32(head + 10);
    out->width = bw_rd32(head + 18);
    out->height = bw_rd32(head + 22);
    out->bits = bw_rd16(head + 28);
    if (out->bits != 24) {
        printf("  %s is %u bits per pixel, expected 24\n", path, out->bits);
        fclose(fp);
        return -1;
    }

    pitch = (out->width * 3u + 3u) & ~3u;
    size = (long)pitch * (long)out->height;
    raw = (u8 *)malloc((size_t)size);
    if (raw == NULL || fseek(fp, (long)offset, SEEK_SET) != 0
        || fread(raw, 1, (size_t)size, fp) != (size_t)size) {
        printf("  %s is truncated\n", path);
        free(raw);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    out->pixels = (u8 *)malloc((size_t)out->width * out->height * 3u);
    if (out->pixels == NULL) {
        free(raw);
        return -1;
    }
    /* BMP rows run bottom to top and the channels are stored B, G, R. */
    for (y = 0; y < out->height; y++) {
        const u8 *src = raw + (size_t)(out->height - 1u - y) * pitch;
        u8 *dst = out->pixels + (size_t)y * out->width * 3u;
        for (x = 0; x < out->width; x++) {
            dst[x * 3 + 0] = src[x * 3 + 2];
            dst[x * 3 + 1] = src[x * 3 + 1];
            dst[x * 3 + 2] = src[x * 3 + 0];
        }
    }
    free(raw);
    return 0;
}

/*
 * Count distinct colours in one half of the image and how many pixels differ
 * from the commonest one.  A small open-addressed table, because a 24-bit
 * histogram would be 16 MB for a question that stops mattering at 16.
 */
#define SLOTS 4096u

static int half_is_blank(const Bmp *bmp, u32 y0, u32 rows, const char *name)
{
    u32 keys[SLOTS];
    u32 counts[SLOTS];
    u32 used = 0, best = 0, total = rows * bmp->width;
    u32 x, y, i;

    memset(counts, 0, sizeof(counts));
    for (i = 0; i < SLOTS; i++) {
        keys[i] = 0xFFFFFFFFu;
    }

    for (y = y0; y < y0 + rows; y++) {
        for (x = 0; x < bmp->width; x++) {
            const u8 *p = bmp->pixels + ((size_t)y * bmp->width + x) * 3u;
            u32 key = ((u32)p[0] << 16) | ((u32)p[1] << 8) | (u32)p[2];
            u32 h = (key * 2654435761u) % SLOTS;
            while (keys[h] != 0xFFFFFFFFu && keys[h] != key) {
                h = (h + 1u) % SLOTS;
            }
            if (keys[h] == 0xFFFFFFFFu) {
                if (used + 1u >= SLOTS) {
                    /* Far past the threshold already; stop counting. */
                    used = SLOTS;
                    goto done;
                }
                keys[h] = key;
                used++;
            }
            counts[h]++;
            if (counts[h] > best) {
                best = counts[h];
            }
        }
    }
done:
    printf("  %s: %u distinct colours, %u of %u pixels not the commonest\n",
           name, used, total - best, total);
    if (used < 8u) {
        printf("  FAIL %s carries fewer than 8 colours\n", name);
        return 1;
    }
    if (total - best < 500u) {
        printf("  FAIL %s has fewer than 500 pixels off its backdrop\n", name);
        return 1;
    }
    return 0;
}

/*
 * How many pixels differ between the same rectangle of two images.  -1 if
 * the two are not the same size, which is a failure in itself.
 */
static long half_diff(const Bmp *a, const Bmp *b, u32 y0, u32 rows)
{
    long n = 0;
    u32 x, y;

    if (a->width != b->width || a->height != b->height) {
        return -1;
    }
    for (y = y0; y < y0 + rows; y++) {
        for (x = 0; x < a->width; x++) {
            size_t o = ((size_t)y * a->width + x) * 3u;
            if (a->pixels[o] != b->pixels[o]
                || a->pixels[o + 1] != b->pixels[o + 1]
                || a->pixels[o + 2] != b->pixels[o + 2]) {
                n++;
            }
        }
    }
    return n;
}

static int run_demo(const char *exe, const char *data_root, const char *shot,
                    const char *shot_first, const char *extra)
{
    char cmd[2048];
    int rc;

    if (extra == NULL) {
        extra = "";
    }
#ifdef _WIN32
    /* cmd.exe strips the outermost pair of quotes off a command line that
     * starts with one, so a quoted program path needs the whole line quoted
     * again. */
    rc = snprintf(cmd, sizeof(cmd),
                  "\"\"%s\" --data-root \"%s\" --frames " DEMO_FRAMES
                  " --shot \"%s\" --shot-first \"%s\" --no-audio %s\"",
                  exe, data_root, shot, shot_first, extra);
#else
    rc = snprintf(cmd, sizeof(cmd),
                  "\"%s\" --data-root \"%s\" --frames " DEMO_FRAMES
                  " --shot \"%s\" --shot-first \"%s\" --no-audio %s",
                  exe, data_root, shot, shot_first, extra);
#endif
    if (rc < 0 || rc >= (int)sizeof(cmd)) {
        printf("  command line does not fit\n");
        return -1;
    }
    printf("running: %s\n", cmd);
    rc = system(cmd);
    if (rc != 0) {
        printf("  FAIL the demo exited with %d\n", rc);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    Bmp bmp;
    Bmp first;
    char shot_first[1024];
    const char *exe, *data_root, *shot;
    int have_first = 0;
    int failed = 0;
    int n;

    if (argc < 4) {
        printf("usage: %s <eidos_view> <data root> <output.bmp>\n", argv[0]);
        return 2;
    }
    exe = argv[1];
    data_root = argv[2];
    shot = argv[3];

    n = snprintf(shot_first, sizeof(shot_first), "%s.first.bmp", shot);
    if (n < 0 || n >= (int)sizeof(shot_first)) {
        printf("  output path does not fit\n");
        return 1;
    }

    remove(shot);
    remove(shot_first);
    if (run_demo(exe, data_root, shot, shot_first, NULL) != 0) {
        return 1;
    }
    if (bmp_load(&bmp, shot) != 0) {
        return 1;
    }
    have_first = bmp_load(&first, shot_first) == 0;
    if (!have_first) {
        printf("  FAIL the demo did not write the first-frame screenshot\n");
        failed = 1;
    }

    printf("screenshot %s: %u x %u, %u bpp\n", shot, bmp.width, bmp.height,
           bmp.bits);
    if (bmp.width != SHOT_W || bmp.height != SHOT_H) {
        printf("  FAIL expected %u x %u, the two DS screens stacked at 1x\n",
               SHOT_W, SHOT_H);
        failed = 1;
    } else {
        failed |= half_is_blank(&bmp, 0, SHOT_H / 2u, "top screen");
        failed |= half_is_blank(&bmp, SHOT_H / 2u, SHOT_H / 2u,
                                "bottom screen");
    }

    /*
     * The two frames.  The bottom screen has to have moved, because a task
     * on the game's own list advanced the pose along its NANR timeline and
     * the matched runner is what called that task.  The top screen has no
     * task and has to be identical, which is what rules out "the demo
     * simply redrew everything differently".
     */
    if (have_first) {
        long top = half_diff(&first, &bmp, 0, SHOT_H / 2u);
        long bottom = half_diff(&first, &bmp, SHOT_H / 2u, SHOT_H / 2u);

        printf("frame 1 against frame " DEMO_FRAMES
               ": top %ld pixels differ, bottom %ld\n", top, bottom);
        if (top != 0) {
            printf("  FAIL the top screen has no task and must not move\n");
            failed = 1;
        }
        if (bottom < (long)MOVED_PIXELS) {
            printf("  FAIL the bottom screen moved by fewer than %u pixels; "
                   "the task on the matched list did not animate it\n",
                   MOVED_PIXELS);
            failed = 1;
        }
        free(first.pixels);
    }

    /*
     * The 3D layer.  A second run with --map 120 puts one a/0/0/8 map
     * model on the top screen through engine/g3d: the same display lists
     * tests/test_g3d.c compares against aletheia's OBJ, transformed by the
     * engine's own matrix stack and rasterised at 256x192.  What is checked
     * here is that it reached the screen at all, which is the platform
     * layer's business; what the geometry *is* is test_g3d's.
     */
    {
        Bmp map_bmp;
        char map_shot[1024];
        char map_first[1024];

        n = snprintf(map_shot, sizeof(map_shot), "%s.map.bmp", shot);
        if (n > 0 && n < (int)sizeof(map_shot)) {
            n = snprintf(map_first, sizeof(map_first), "%s.map.first.bmp",
                         shot);
        }
        if (n <= 0 || n >= (int)sizeof(map_first)) {
            printf("  FAIL the --map output path does not fit\n");
            failed = 1;
        } else {
            remove(map_shot);
            remove(map_first);
            if (run_demo(exe, data_root, map_shot, map_first,
                         "--map 120") != 0) {
                failed = 1;
            } else if (bmp_load(&map_bmp, map_shot) != 0) {
                failed = 1;
            } else {
                printf("screenshot %s: %u x %u, %u bpp (--map 120)\n",
                       map_shot, map_bmp.width, map_bmp.height, map_bmp.bits);
                if (map_bmp.width != SHOT_W || map_bmp.height != SHOT_H) {
                    printf("  FAIL the --map run's screenshot is the wrong "
                           "size\n");
                    failed = 1;
                } else {
                    long moved;

                    failed |= half_is_blank(&map_bmp, 0, SHOT_H / 2u,
                                            "top screen (3D)");
                    moved = half_diff(&bmp, &map_bmp, 0, SHOT_H / 2u);
                    printf("3D top screen differs from the 2D one on %ld "
                           "pixels\n", moved);
                    if (moved < (long)MOVED_PIXELS) {
                        printf("  FAIL --map did not replace the top "
                               "screen with the 3D layer\n");
                        failed = 1;
                    }
                }
                free(map_bmp.pixels);
            }
        }
    }

    free(bmp.pixels);
    if (failed) {
        printf("test_view: FAIL\n");
        return 1;
    }
    printf("test_view: ok\n");
    return 0;
}
