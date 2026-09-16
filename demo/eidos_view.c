/*
 * demo/eidos_view.c -- the first visible output of the port.
 *
 * Two screens, drawn by engine/g2d/ out of the player's own extracted ROM,
 * presented by engine/platform/ through the frame loop in engine/frame/:
 *
 *   top     titledemo.narc member 0, the 256x256 8bpp title screen the asset
 *           lane identified (the leftover Diamond/Pearl logo), over the
 *           NCGR/NCLR pair background.py's coverage rule paired with it
 *           (members 1 and 2; tests/test_bg.c checks that same triple
 *           against the Python renderer pixel for pixel).
 *   bottom  one composed rig pose, species 25 by default, out of a/0/0/4
 *           through g2d_rig_load and g2d_compose_pose.
 *
 * Nothing here is game logic: no matched function runs in this loop yet.
 * What it proves is that the three layers meet -- the 2D engine's output
 * reaches a window at the DS's own frame rate, and the DS's commit-at-vblank
 * convention is the shape the loop already has.
 *
 * The step/vblank split is not decoration.  The step function writes the
 * next frame's scroll and sprite position into `pending`; the vblank
 * callback copies them into `live`; the render callback reads only `live`.
 * That is what OAM and the BG registers do on hardware, and doing it this
 * way from the first frame is what keeps the matched code that eventually
 * replaces the step function from having to be rearranged around it.
 *
 *     eidos_view [--data-root PATH] [--scale N] [--side-by-side] [--gap N]
 *                [--species N] [--frames N] [--shot PATH] [--no-audio]
 *
 * Escape quits; F12 writes a screenshot.  With --frames the run ends after
 * that many frames, which together with SDL_VIDEODRIVER=dummy is how
 * tests/test_view.c checks the picture with no display attached.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame.h"
#include "g2d.h"
#include "platform.h"
#include "port.h"

#ifndef EIDOS_DEFAULT_DATA_ROOT
#define EIDOS_DEFAULT_DATA_ROOT ""
#endif

#define TITLE_ARCHIVE "titledemo.narc"
#define TITLE_NSCR    0u
#define TITLE_NCGR    1u
#define TITLE_NCLR    2u

#define RIG_ARCHIVE    "a/0/0/4"
#define RIG_BLOCK_SIZE 20u

#define SCREEN_W EIDOS_DS_SCREEN_WIDTH
#define SCREEN_H EIDOS_DS_SCREEN_HEIGHT
#define SCREEN_BYTES ((size_t)SCREEN_W * SCREEN_H * 4u)

/* The hardware state the render callback is allowed to read.  A step
 * function may only write `pending`; the vblank callback commits. */
typedef struct DisplayRegs {
    s32 bg_hofs, bg_vofs;   /* BG0HOFS / BG0VOFS of the top screen */
    s32 obj_x, obj_y;       /* the bottom screen's single OAM entry */
} DisplayRegs;

typedef struct View {
    /* top screen */
    BwArcFile  title_arc;
    int        title_open;
    u8        *nscr_buf, *ncgr_buf, *nclr_buf;
    G2dNscr    nscr;
    G2dNcgr    ncgr;
    G2dPalette pal;

    /* bottom screen */
    BwArcFile  rig_arc;
    int        rig_open;
    G2dRig     rig;
    G2dPose    pose;
    int        pose_ok;

    DisplayRegs pending, live;
    u64 phase;
} View;

static void view_free(View *v)
{
    if (v->pose_ok) {
        g2d_pose_free(&v->pose);
    }
    if (v->rig_open) {
        g2d_rig_free(&v->rig);
        bw_arc_close(&v->rig_arc);
    }
    g2d_nscr_free(&v->nscr);
    g2d_ncgr_free(&v->ncgr);
    g2d_palette_free(&v->pal);
    free(v->nscr_buf);
    free(v->ncgr_buf);
    free(v->nclr_buf);
    if (v->title_open) {
        bw_arc_close(&v->title_arc);
    }
    memset(v, 0, sizeof(*v));
}

static int view_load(View *v, int species)
{
    u32 n_size = 0, g_size = 0, p_size = 0;

    memset(v, 0, sizeof(*v));

    if (bw_arc_open(&v->title_arc, TITLE_ARCHIVE) != 0) {
        fprintf(stderr, "eidos_view: cannot open %s under %s\n",
                TITLE_ARCHIVE, bw_data_root());
        return -1;
    }
    v->title_open = 1;
    v->nscr_buf = g2d_arc_member(&v->title_arc, TITLE_NSCR, &n_size);
    v->ncgr_buf = g2d_arc_member(&v->title_arc, TITLE_NCGR, &g_size);
    v->nclr_buf = g2d_arc_member(&v->title_arc, TITLE_NCLR, &p_size);
    if (v->nscr_buf == NULL || v->ncgr_buf == NULL || v->nclr_buf == NULL
        || g2d_nscr_parse(&v->nscr, v->nscr_buf, n_size) != 0
        || g2d_ncgr_parse(&v->ncgr, v->ncgr_buf, g_size) != 0
        || g2d_palette_append_nclr(&v->pal, v->nclr_buf, p_size) != 0) {
        fprintf(stderr, "eidos_view: %s members %u/%u/%u did not parse\n",
                TITLE_ARCHIVE, TITLE_NSCR, TITLE_NCGR, TITLE_NCLR);
        view_free(v);
        return -1;
    }

    if (bw_arc_open(&v->rig_arc, RIG_ARCHIVE) != 0) {
        fprintf(stderr, "eidos_view: cannot open %s\n", RIG_ARCHIVE);
        view_free(v);
        return -1;
    }
    v->rig_open = 1;
    if (g2d_rig_load(&v->rig, &v->rig_arc, (u32)species * RIG_BLOCK_SIZE,
                     RIG_BLOCK_SIZE) != 0) {
        fprintf(stderr, "eidos_view: rig block for species %d did not load\n",
                species);
        view_free(v);
        return -1;
    }
    if (v->rig.nmcr.num_multi_cell == 0
        || g2d_compose_pose(&v->rig.ncgr, &v->rig.ncer,
                            v->rig.has_nanr ? &v->rig.nanr : NULL,
                            &v->rig.nmcr.groups[0], 0, v->rig.palette.count,
                            &v->pose) != 0) {
        fprintf(stderr, "eidos_view: species %d has no composable pose\n",
                species);
        view_free(v);
        return -1;
    }
    v->pose_ok = 1;

    printf("top:    %s#%u, %ux%u, %u-colour palette\n", TITLE_ARCHIVE,
           TITLE_NSCR, v->nscr.width, v->nscr.height, v->pal.count);
    printf("bottom: %s species %d, pose %ux%u\n", RIG_ARCHIVE, species,
           v->pose.width, v->pose.height);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The three frame callbacks                                           */
/* ------------------------------------------------------------------ */

/*
 * One frame of "logic".  It writes nothing the renderer reads: the values go
 * into `pending` and reach the picture only through the vblank commit.
 *
 * The bob is the engine's own fixed-point sine table rather than a float, so
 * the demo needs no libm and the motion is in the same arithmetic the affine
 * path uses: the angle is a 16-bit binary angle, one turn every 128 frames.
 */
static void view_step(void *user)
{
    View *v = (View *)user;
    s32 s;

    v->phase++;
    s = g2d_sin_fx((u16)((v->phase * 512u) & 0xFFFFu));

    v->pending.bg_hofs = 0;
    /* The screen resource is taller than the display (256 against 192):
     * scroll to the middle of it rather than showing the top. */
    v->pending.bg_vofs = v->nscr.height > SCREEN_H
                         ? (s32)((v->nscr.height - SCREEN_H) / 2u) : 0;
    v->pending.obj_x = ((s32)SCREEN_W - (s32)v->pose.width) / 2;
    v->pending.obj_y = ((s32)SCREEN_H - (s32)v->pose.height) / 2
                       + (s * 6) / G2D_FX_ONE;
}

/* Vblank: the one place OAM and the BG registers may be written. */
static void view_vblank(void *user)
{
    View *v = (View *)user;
    v->live = v->pending;
}

static void view_render(void *user, u8 *top, u8 *bottom)
{
    View *v = (View *)user;
    G2dBgLayer layer;
    G2dObjEntry obj;
    G2dFrameDesc desc;

    memset(&layer, 0, sizeof(layer));
    layer.nscr = &v->nscr;
    layer.ncgr = &v->ncgr;
    layer.pal = &v->pal;
    layer.screen_size = G2D_BG_SIZE_FLAT;
    layer.priority = 0;
    layer.hofs = v->live.bg_hofs;
    layer.vofs = v->live.bg_vofs;
    layer.wrap = 1;
    layer.enabled = 1;

    memset(&desc, 0, sizeof(desc));
    desc.bgs = &layer;
    desc.n_bgs = 1;
    desc.backdrop_pal = &v->pal;
    g2d_frame_compose_desc(&desc, SCREEN_W, SCREEN_H, top);

    memset(&obj, 0, sizeof(obj));
    obj.pose = &v->pose;
    obj.pal = &v->rig.palette;
    obj.x = v->live.obj_x;
    obj.y = v->live.obj_y;
    obj.priority = 0;

    memset(&desc, 0, sizeof(desc));
    desc.objs = &obj;
    desc.n_objs = 1;
    desc.backdrop_pal = &v->rig.palette;
    g2d_frame_compose_desc(&desc, SCREEN_W, SCREEN_H, bottom);
}

/* ------------------------------------------------------------------ */

static void usage(void)
{
    printf("eidos_view [--data-root PATH] [--scale N] [--side-by-side]\n"
           "           [--gap N] [--species N] [--frames N] [--shot PATH]\n"
           "           [--no-audio]\n"
           "\n"
           "Escape quits, F12 writes a screenshot.\n");
}

int main(int argc, char **argv)
{
    EidosPlatformConfig cfg;
    EidosPlatform *plat = NULL;
    EidosFrameHooks hooks;
    EidosFrameLoop loop;
    View view;
    const char *data_root = NULL;
    const char *shot = NULL;
    long frames = 0;
    int species = 25;
    int audio = 1;
    u8 *top, *bottom;
    int i;
    int rc = 0;

    memset(&cfg, 0, sizeof(cfg));
    cfg.title = "eidos";
    cfg.scale = 2;
    cfg.layout = EIDOS_LAYOUT_STACKED;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data-root") == 0 && i + 1 < argc) {
            data_root = argv[++i];
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            cfg.scale = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--side-by-side") == 0) {
            cfg.layout = EIDOS_LAYOUT_SIDE_BY_SIDE;
        } else if (strcmp(argv[i], "--gap") == 0 && i + 1 < argc) {
            cfg.gap = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--species") == 0 && i + 1 < argc) {
            species = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shot = argv[++i];
        } else if (strcmp(argv[i], "--no-audio") == 0) {
            audio = 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "eidos_view: unknown argument %s\n", argv[i]);
            usage();
            return 2;
        }
    }
    cfg.audio = audio;

    if (data_root == NULL) {
        data_root = getenv("BW_DATA_ROOT");
    }
    if (data_root == NULL || data_root[0] == '\0') {
        data_root = EIDOS_DEFAULT_DATA_ROOT;
    }
    if (data_root[0] == '\0') {
        fprintf(stderr, "eidos_view: no data root; pass --data-root PATH\n");
        return 2;
    }
    bw_set_data_root(data_root);

    if (view_load(&view, species) != 0) {
        return 1;
    }

    top = (u8 *)calloc(SCREEN_BYTES, 1);
    bottom = (u8 *)calloc(SCREEN_BYTES, 1);
    if (top == NULL || bottom == NULL) {
        free(top);
        free(bottom);
        view_free(&view);
        return 1;
    }

    if (eidos_platform_init(&plat, &cfg) != 0) {
        free(top);
        free(bottom);
        view_free(&view);
        return 1;
    }
    printf("window: %d x %d at scale %d, frame period %llu ns "
           "(%s), audio %s\n",
           cfg.layout == EIDOS_LAYOUT_SIDE_BY_SIDE
               ? (int)SCREEN_W * 2 + cfg.gap : (int)SCREEN_W,
           cfg.layout == EIDOS_LAYOUT_SIDE_BY_SIDE
               ? (int)SCREEN_H : (int)SCREEN_H * 2 + cfg.gap,
           cfg.scale, (unsigned long long)eidos_frame_period_ns(),
           cfg.layout == EIDOS_LAYOUT_SIDE_BY_SIDE ? "side by side" : "stacked",
           eidos_platform_audio_open(plat) ? "open" : "silent");

    memset(&hooks, 0, sizeof(hooks));
    hooks.step = view_step;
    hooks.vblank = view_vblank;
    hooks.render = view_render;
    hooks.user = &view;
    eidos_frame_init(&loop, &hooks, top, bottom);
    eidos_frame_set_current(&loop);

    while (eidos_platform_pump(plat)) {
        eidos_frame_run_one(&loop);
        eidos_platform_present(plat, top, bottom);

        if (eidos_platform_take_screenshot_request(plat)) {
            const char *path = shot != NULL ? shot : "eidos_view.bmp";
            if (eidos_platform_screenshot(plat, path) == 0) {
                printf("screenshot: %s\n", path);
            }
        }
        if (frames > 0 && (long)eidos_frame_count(&loop) >= frames) {
            break;
        }
        eidos_platform_wait_frame(plat);
    }

    if (shot != NULL) {
        u32 sw = 0, sh = 0;
        eidos_platform_shot_size(plat, &sw, &sh);
        if (eidos_platform_screenshot(plat, shot) != 0) {
            rc = 1;
        } else {
            printf("screenshot: %s (%u x %u)\n", shot, sw, sh);
        }
    }

    printf("ran %llu frames, tick %llu (%llu ms of game time), "
           "%u resynchronisations, %llu audio frames\n",
           (unsigned long long)eidos_frame_count(&loop),
           (unsigned long long)eidos_frame_tick(&loop),
           (unsigned long long)eidos_ticks_to_ms(eidos_frame_tick(&loop)),
           eidos_platform_late_frames(plat),
           (unsigned long long)eidos_platform_audio_frames(plat));

    eidos_frame_set_current(NULL);
    eidos_platform_quit(plat);
    free(top);
    free(bottom);
    view_free(&view);
    return rc;
}
