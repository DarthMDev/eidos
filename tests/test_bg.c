/*
 * tests/test_bg.c -- the BG half of the 2D engine against a pixel oracle.
 *
 * For each screen named in the oracle manifest, the C engine opens the
 * player's own archive, reads the NSCR sub-file and the NCGR/NCLR pair
 * the oracle chose, renders the screen through engine/g2d/bg.c and
 * compares it pixel for pixel against the dump aletheia's
 * tools/assets/background.py produced from the same three sub-files.
 * A mismatch prints the screen, the first differing pixel and both
 * colours.
 *
 *     ./test_bg <data root> <oracle directory>
 *
 * The two renderers share no code and no language.  background.py was
 * written in the decompilation project against the archives' bytes;
 * engine/g2d/bg.c here against the same attested formats and against
 * src/matched/func_02040EE0.c for the screen layout.  The pairing
 * heuristic is NOT under test: it is a property of how the archives are
 * laid out rather than of the hardware, so the oracle records its choice
 * in screens.txt and both sides render the same three sub-files.
 *
 * After the oracle comparison the test checks four things the oracle
 * cannot see, because background.py renders one layer on its own and has
 * no scroll, no priority and no backdrop: the transcription of
 * func_02040EE0's screen-entry layout, scroll wraparound, the
 * priority/transparency order of the frame compositor, and that an OBJ
 * wins a priority tie against a BG.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g2d.h"
#include "port.h"
#include "port_test.h"

#define MAX_SCREENS 32

typedef struct ScreenSpec {
    char name[64];
    char archive[64];
    u32  index, ncgr, nclr;
    u32  width, height, bpp, pal_colors, distinct;
} ScreenSpec;

typedef struct Loaded {
    G2dNscr    nscr;
    G2dNcgr    ncgr;
    G2dPalette pal;
    u8        *nscr_buf;
    u8        *ncgr_buf;
    u8        *nclr_buf;
} Loaded;

static void loaded_free(Loaded *l)
{
    g2d_nscr_free(&l->nscr);
    g2d_ncgr_free(&l->ncgr);
    g2d_palette_free(&l->pal);
    free(l->nscr_buf);
    free(l->ncgr_buf);
    free(l->nclr_buf);
    memset(l, 0, sizeof(*l));
}

static int load_screen(Loaded *l, BwArcFile *arc, const ScreenSpec *s)
{
    u32 n_size = 0, g_size = 0, p_size = 0;

    memset(l, 0, sizeof(*l));
    l->nscr_buf = g2d_arc_member(arc, s->index, &n_size);
    l->ncgr_buf = g2d_arc_member(arc, s->ncgr, &g_size);
    l->nclr_buf = g2d_arc_member(arc, s->nclr, &p_size);
    if (l->nscr_buf == NULL || l->ncgr_buf == NULL || l->nclr_buf == NULL) {
        loaded_free(l);
        return -1;
    }
    if (g2d_nscr_parse(&l->nscr, l->nscr_buf, n_size) != 0
        || g2d_ncgr_parse(&l->ncgr, l->ncgr_buf, g_size) != 0
        || g2d_palette_append_nclr(&l->pal, l->nclr_buf, p_size) != 0) {
        loaded_free(l);
        return -1;
    }
    return 0;
}

static void flat_layer(G2dBgLayer *layer, const Loaded *l)
{
    memset(layer, 0, sizeof(*layer));
    layer->nscr = &l->nscr;
    layer->ncgr = &l->ncgr;
    layer->pal = &l->pal;
    layer->screen_size = G2D_BG_SIZE_FLAT;
    layer->enabled = 1;
}

/* The oracle dump's framing: 'RGBA', u32 width, u32 height, then pixels. */
static u8 *oracle_load(const char *dir, const char *name, u32 *w, u32 *h)
{
    char path[1024];
    FILE *fp;
    u8 head[12];
    size_t need;
    u8 *buf;

    if (snprintf(path, sizeof(path), "%s/%s.rgba", dir, name)
        >= (int)sizeof(path)) {
        return NULL;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        printf("  cannot open oracle dump %s\n", path);
        return NULL;
    }
    if (fread(head, 1, 12, fp) != 12 || memcmp(head, "RGBA", 4) != 0) {
        fclose(fp);
        return NULL;
    }
    *w = bw_rd32(head + 4);
    *h = bw_rd32(head + 8);
    need = (size_t)*w * *h * 4u;
    buf = (u8 *)malloc(need);
    if (buf == NULL || fread(buf, 1, need, fp) != need) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    return buf;
}

static int compare_screen(const ScreenSpec *s, const Loaded *l,
                          const char *oracle_dir)
{
    G2dBgLayer layer;
    u32 ow = 0, oh = 0;
    u8 *oracle;
    u8 *rgba;
    u32 i;
    int failed = 0;

    oracle = oracle_load(oracle_dir, s->name, &ow, &oh);
    if (oracle == NULL) {
        printf("  FAIL oracle dump missing or malformed\n");
        return 1;
    }
    if (l->nscr.width != ow || l->nscr.height != oh) {
        printf("  FAIL screen %ux%u, oracle %ux%u\n",
               l->nscr.width, l->nscr.height, ow, oh);
        free(oracle);
        return 1;
    }

    flat_layer(&layer, l);
    rgba = (u8 *)malloc((size_t)ow * oh * 4u);
    if (rgba == NULL) {
        free(oracle);
        return 1;
    }
    g2d_bg_render(&layer, ow, oh, rgba);

    for (i = 0; i < ow * oh; i++) {
        if (memcmp(rgba + i * 4, oracle + i * 4, 4) != 0) {
            printf("  FAIL %s first differing pixel at (%u, %u): "
                   "engine RGBA %u,%u,%u,%u  oracle RGBA %u,%u,%u,%u\n",
                   s->name, i % ow, i / ow,
                   rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2], rgba[i * 4 + 3],
                   oracle[i * 4], oracle[i * 4 + 1], oracle[i * 4 + 2],
                   oracle[i * 4 + 3]);
            failed = 1;
            break;
        }
    }
    if (!failed) {
        printf("  match: %ux%u pixels identical (%ubpp, %u distinct colours)\n",
               ow, oh, s->bpp, s->distinct);
    }
    free(rgba);
    free(oracle);
    return failed;
}

/* ------------------------------------------------------------------ */
/* What the oracle cannot see                                          */
/* ------------------------------------------------------------------ */

/*
 * src/matched/func_02040EE0.c, spot-checked at the boundaries that
 * distinguish the four text screen sizes from a row-major map.
 */
static void check_screen_layout(void)
{
    printf("screen-entry layout (func_02040EE0):\n");
    BW_CHECK_EQ("256x256 (5,3)", g2d_bg_screen_index(5, 3, G2D_BG_SIZE_256x256), 5 + 3 * 32);
    BW_CHECK_EQ("256x512 (5,40)", g2d_bg_screen_index(5, 40, G2D_BG_SIZE_256x512), 5 + 40 * 32);
    BW_CHECK_EQ("512x256 (31,1)", g2d_bg_screen_index(31, 1, G2D_BG_SIZE_512x256), 63);
    BW_CHECK_EQ("512x256 (32,0)", g2d_bg_screen_index(32, 0, G2D_BG_SIZE_512x256), 0x400);
    BW_CHECK_EQ("512x256 (32,1)", g2d_bg_screen_index(32, 1, G2D_BG_SIZE_512x256), 0x400 + 32);
    BW_CHECK_EQ("512x512 (0,32)", g2d_bg_screen_index(0, 32, G2D_BG_SIZE_512x512), 0x800);
    BW_CHECK_EQ("512x512 (32,32)", g2d_bg_screen_index(32, 32, G2D_BG_SIZE_512x512), 0xC00);
    BW_CHECK_EQ("128x128 (5,3)", g2d_bg_screen_index(5, 3, G2D_BG_SIZE_128x128), 5 + 3 * 16);
}

/* A wrapping layer scrolled by its own map width samples identically. */
static void check_scroll(const Loaded *l)
{
    G2dBgLayer a, b;
    u32 x, y;
    int same = 1, shifted_ok = 1;
    u32 mw = (u32)l->nscr.tiles_wide * 8u;

    printf("scroll:\n");
    flat_layer(&a, l);
    a.wrap = 1;
    b = a;
    b.hofs = (s32)mw;
    for (y = 0; y < l->nscr.height && same; y++) {
        for (x = 0; x < mw; x++) {
            u8 ia = 0, ib = 0;
            u32 ca = 0, cb = 0;

            if (g2d_bg_sample(&a, (s32)x, (s32)y, &ia, &ca)
                != g2d_bg_sample(&b, (s32)x, (s32)y, &ib, &cb)
                || ia != ib || ca != cb) {
                same = 0;
                break;
            }
        }
    }
    BW_CHECK_EQ("a full-map scroll wraps to itself", same, 1);

    /* A one-tile scroll moves the picture by exactly one tile. */
    b = a;
    b.hofs = 8;
    for (y = 0; y < 64u && shifted_ok; y++) {
        for (x = 0; x < mw - 8u; x++) {
            u8 ia = 0, ib = 0;
            u32 ca = 0, cb = 0;

            g2d_bg_sample(&a, (s32)x + 8, (s32)y, &ia, &ca);
            g2d_bg_sample(&b, (s32)x, (s32)y, &ib, &cb);
            if (ia != ib || ca != cb) {
                shifted_ok = 0;
                break;
            }
        }
    }
    BW_CHECK_EQ("an 8px scroll shifts by one tile", shifted_ok, 1);

    /* Off a non-wrapping map nothing is sampled. */
    b = a;
    b.wrap = 0;
    b.hofs = -(s32)(mw + 8u);
    BW_CHECK_EQ("off a non-wrapping map: no pixel",
                g2d_bg_sample(&b, 0, 0, NULL, NULL), 0);
}

/*
 * The frame compositor's own invariant, checked against g2d_bg_sample
 * rather than against the compositor's internals: the front-most layer
 * whose palette index is non-zero decides the pixel, an OBJ wins a
 * priority tie against a BG, and where nothing covers it the backdrop
 * (BG palette colour 0) shows.
 */
static void check_compose(const Loaded *front, const Loaded *back)
{
    G2dBgLayer bgs[2];
    G2dObjEntry obj;
    G2dPose pose;
    u8 *frame;
    u32 x, y;
    int ok = 1;
    int obj_ok = 1;
    const u32 w = G2D_SCREEN_WIDTH, h = G2D_SCREEN_HEIGHT;

    printf("frame compositor:\n");
    flat_layer(&bgs[0], front);
    bgs[0].priority = 1;
    flat_layer(&bgs[1], back);
    bgs[1].priority = 2;

    frame = (u8 *)malloc((size_t)w * h * 4u);
    if (frame == NULL) {
        return;
    }
    g2d_frame_compose(bgs, 2, NULL, 0, &front->pal, w, h, frame);

    for (y = 0; y < h && ok; y++) {
        for (x = 0; x < w; x++) {
            const u8 *px = frame + ((size_t)y * w + x) * 4u;
            u8 i0 = 0, i1 = 0;
            u32 c0 = 0, c1 = 0;
            const G2dPalette *pal = &front->pal;
            u32 want = 0;

            if (g2d_bg_sample(&bgs[0], (s32)x, (s32)y, &i0, &c0) && i0 != 0) {
                want = c0;
            } else if (g2d_bg_sample(&bgs[1], (s32)x, (s32)y, &i1, &c1)
                       && i1 != 0) {
                want = c1;
                pal = &back->pal;
            } else {
                want = 0;   /* the backdrop */
            }
            if (want >= pal->count) {
                continue;   /* magenta on both sides, nothing to compare */
            }
            if (px[0] != pal->colors[want].r || px[1] != pal->colors[want].g
                || px[2] != pal->colors[want].b || px[3] != 255) {
                printf("  FAIL composite pixel (%u, %u)\n", x, y);
                ok = 0;
                break;
            }
        }
    }
    BW_CHECK_EQ("two BGs and a backdrop composite by priority", ok, 1);

    /* One 4x4 OBJ of palette index 1, at the same priority as BG0. */
    memset(&pose, 0, sizeof(pose));
    pose.width = 4;
    pose.height = 4;
    pose.indices = (u8 *)malloc(16);
    if (pose.indices != NULL) {
        memset(pose.indices, 1, 16);
        memset(&obj, 0, sizeof(obj));
        obj.pose = &pose;
        obj.pal = &front->pal;
        obj.x = 20;
        obj.y = 20;
        obj.priority = 1;
        g2d_frame_compose(bgs, 2, &obj, 1, &front->pal, w, h, frame);
        if (front->pal.count > 1) {
            for (y = 20; y < 24; y++) {
                for (x = 20; x < 24; x++) {
                    const u8 *px = frame + ((size_t)y * w + x) * 4u;

                    if (px[0] != front->pal.colors[1].r
                        || px[1] != front->pal.colors[1].g
                        || px[2] != front->pal.colors[1].b) {
                        obj_ok = 0;
                    }
                }
            }
        }
        BW_CHECK_EQ("an OBJ wins a priority tie against a BG", obj_ok, 1);
        free(pose.indices);
    }
    free(frame);
}

/* ------------------------------------------------------------------ */

static int read_manifest(ScreenSpec *out, int max, const char *dir)
{
    char path[1024];
    char line[512];
    FILE *fp;
    int n = 0;

    if (snprintf(path, sizeof(path), "%s/screens.txt", dir) >= (int)sizeof(path)) {
        return 0;
    }
    fp = fopen(path, "r");
    if (fp == NULL) {
        printf("cannot open the oracle manifest %s\n", path);
        return 0;
    }
    while (n < max && fgets(line, sizeof(line), fp) != NULL) {
        ScreenSpec *s = &out[n];

        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        if (sscanf(line, "%63s %63s %u %u %u %u %u %u %u %u",
                   s->name, s->archive, &s->index, &s->ncgr, &s->nclr,
                   &s->width, &s->height, &s->bpp, &s->pal_colors,
                   &s->distinct) == 10) {
            n++;
        }
    }
    fclose(fp);
    return n;
}

int main(int argc, char **argv)
{
    ScreenSpec screens[MAX_SCREENS];
    Loaded loaded[MAX_SCREENS];
    const char *oracle_dir;
    int n, i;
    int n_loaded = 0;

    BW_TEST_SETUP(argc, argv);

    oracle_dir = (argc > 2 && argv[2][0] != '\0') ? argv[2] : getenv("BG_ORACLE_DIR");
    if (oracle_dir == NULL || oracle_dir[0] == '\0') {
        printf("usage: test_bg <data root> <oracle directory>\n");
        return 1;
    }
    printf("oracle: %s\n", oracle_dir);

    memset(screens, 0, sizeof(screens));
    memset(loaded, 0, sizeof(loaded));
    n = read_manifest(screens, MAX_SCREENS, oracle_dir);
    BW_REQUIRE("the oracle manifest names at least six screens", n >= 6);

    for (i = 0; i < n; i++) {
        BwArcFile arc;

        printf("%s (%s#%u, NCGR#%u, NCLR#%u):\n", screens[i].name,
               screens[i].archive, screens[i].index, screens[i].ncgr,
               screens[i].nclr);
        bw_checks++;
        if (bw_arc_open(&arc, screens[i].archive) != 0) {
            printf("  FAIL cannot open %s under the data root\n",
                   screens[i].archive);
            bw_failures++;
            continue;
        }
        if (load_screen(&loaded[n_loaded], &arc, &screens[i]) != 0) {
            printf("  FAIL the NSCR/NCGR/NCLR trio did not parse\n");
            bw_failures++;
            bw_arc_close(&arc);
            continue;
        }
        bw_arc_close(&arc);
        if (compare_screen(&screens[i], &loaded[n_loaded], oracle_dir) != 0) {
            bw_failures++;
        }
        n_loaded++;
    }

    check_screen_layout();
    if (n_loaded > 0) {
        check_scroll(&loaded[0]);
    }
    if (n_loaded > 1) {
        check_compose(&loaded[0], &loaded[1]);
    }
    for (i = 0; i < n_loaded; i++) {
        loaded_free(&loaded[i]);
    }

    BW_REPORT("test_bg");
}
