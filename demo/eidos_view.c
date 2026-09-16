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
 *           through g2d_rig_load and g2d_compose_pose, animated along its
 *           own NANR timeline.
 *
 * **The frame loop's step is game code now.**  The step function is
 * src/matched/sub_020310C4.c, one pass of the game's task-callback list
 * (aletheia's tcbl.c), over a list built by src/matched/sub_0203101C.c on
 * the matched heap layer.  The demo registers exactly one task on it, and
 * that task is what advances the animation clock and the sprite's position
 * each frame.  So the picture moves because matched game code ran: the
 * runner walked its list, found the node, fetched its work area through the
 * matched work accessor's call site and dispatched.  sdk/tcbl.h says which
 * functions in that path are matched and which the port supplies.
 *
 * The step/vblank split is not decoration.  The task writes the next
 * frame's scroll, sprite position and animation time into `pending`; the
 * vblank callback copies them into `live`; the render callback reads only
 * `live`.  That is what OAM and the BG registers do on hardware.
 *
 *     eidos_view [--data-root PATH] [--scale N] [--side-by-side] [--gap N]
 *                [--species N] [--map N] [--frames N] [--shot PATH]
 *                [--shot-first PATH] [--no-audio]
 *
 * Escape quits; F12 writes a screenshot.  With --frames the run ends after
 * that many frames, which together with SDL_VIDEODRIVER=dummy is how
 * tests/test_view.c checks the picture with no display attached;
 * --shot-first writes a second screenshot right after the first frame, so
 * the test can compare two frames and see that the bottom screen moved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "frame.h"
#include "g2d.h"
#include "g3d.h"
#include "platform.h"
#include "port.h"
#include "tcbl.h"

#ifndef EIDOS_DEFAULT_DATA_ROOT
#define EIDOS_DEFAULT_DATA_ROOT ""
#endif

#define TITLE_ARCHIVE "titledemo.narc"
#define TITLE_NSCR    0u
#define TITLE_NCGR    1u
#define TITLE_NCLR    2u

#define RIG_ARCHIVE    "a/0/0/4"
#define RIG_BLOCK_SIZE 20u

/* The game's task list: which heap it comes from, how deep its pool is, and
 * the priority the one task sits at.  The pool is deeper than the demo needs
 * so that the list is a list and not a special case of one. */
#define VIEW_HEAP           1u
#define VIEW_TASK_SLOTS     8u
#define VIEW_ANIM_PRIORITY 64u
#define VIEW_CAM_PRIORITY  32u

/*
 * --map N: the top screen becomes the 3D layer.
 *
 * `a/0/0/8` is the overworld map, building and dungeon model archive
 * aletheia's asset lane identified, and `a/0/1/4` the shared texture
 * archive every one of those models draws from.  engine/g3d decodes the
 * model's shapes into one geometry buffer once, and every frame the
 * camera task orbits an eye around it and the renderer transforms and
 * rasterises through the engine's own fixed-point pipeline.
 *
 * The 3D frame reaches the screen through g3d_compose_over(), which puts
 * it *behind* an ordinary 2D frame: every pixel where the 2D frame would
 * show its backdrop shows the 3D one instead.  That is the arrangement a
 * DS game uses when BG0 carries the 3D output at the lowest priority.
 * What it is not is a general BG0-is-3D hook inside g2d's own per-pixel
 * stack; README.md's engine/g3d section says what that still needs.
 */
#define MAP_ARCHIVE "a/0/0/8"
#define MAP_TEXTURES "a/0/1/4"
#define MAP_FOVY   ((u16)(40u * 65536u / 360u))
#define MAP_NEAR   (FX32_ONE / 2)
#define MAP_FAR    (64 * FX32_ONE)

#define SCREEN_W EIDOS_DS_SCREEN_WIDTH
#define SCREEN_H EIDOS_DS_SCREEN_HEIGHT
#define SCREEN_BYTES ((size_t)SCREEN_W * SCREEN_H * 4u)

/* The hardware state the render callback is allowed to read.  A step
 * function may only write `pending`; the vblank callback commits. */
typedef struct DisplayRegs {
    s32 bg_hofs, bg_vofs;   /* BG0HOFS / BG0VOFS of the top screen */
    s32 obj_x, obj_y;       /* the bottom screen's single OAM entry */
    u32 anim_t;             /* the NANR timeline position of the pose */
    /* the 3D camera, committed at the vblank like everything else */
    u16 cam_yaw;            /* a 16-bit binary angle around the model */
    u16 cam_pitch;          /* likewise, clamped away from the poles   */
    fx32 cam_dist;
} DisplayRegs;

/*
 * The task's work area, which lives inside the node the game's own list
 * handed out and reaches the callback through the matched runner's call to
 * the work accessor.  Sixteen bytes on this host; the list is built with
 * room for it.
 */
typedef struct ViewTask {
    struct View *view;
    u32          t;       /* NANR frames elapsed */
    u32          speed;   /* how many per DS frame */
} ViewTask;

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
    u32        pose_t;      /* the timeline position `pose` was composed at */

    /* top screen, in --map mode */
    int            map_on;
    s32            map_index;
    G3dModel       map;
    int            map_loaded;
    G3dTexArchive *map_tex;
    G3dTarget      map_target;
    int            map_target_ok;

    /* the game's own scheduler, and the tasks on it */
    EidosTcblList *tasks;
    EidosTcblNode *anim_node;
    EidosTcblNode *cam_node;

    /* the platform, so the camera task can read the keypad */
    EidosPlatform *plat;

    DisplayRegs pending, live;
    u64 phase;
} View;

static void view_free(View *v)
{
    if (v->tasks != NULL) {
        bw_tcbl_free(v->tasks);
        v->tasks = NULL;
        v->anim_node = NULL;
        v->cam_node = NULL;
    }
    if (v->map_target_ok) {
        g3d_target_free(&v->map_target);
    }
    if (v->map_loaded) {
        g3d_model_free(&v->map);
    }
    if (v->map_tex != NULL) {
        g3d_texarchive_close(v->map_tex);
        v->map_tex = NULL;
    }
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
    v->pose_t = 0;

    printf("top:    %s#%u, %ux%u, %u-colour palette\n", TITLE_ARCHIVE,
           TITLE_NSCR, v->nscr.width, v->nscr.height, v->pal.count);
    printf("bottom: %s species %d, pose %ux%u, %u NANR sequences\n",
           RIG_ARCHIVE, species, v->pose.width, v->pose.height,
           v->rig.has_nanr ? v->rig.nanr.num_sequences : 0u);
    return 0;
}

/*
 * --map N: decode one a/0/0/8 model and index the shared texture archive.
 * Both are done once, here, and not per frame: the display lists are
 * interpreted into a geometry buffer in model space, and every frame after
 * this only transforms and rasterises it.
 */
static int view_load_map(View *v, s32 index)
{
    u32 total_polys = 0, textured = 0, i;

    v->map_tex = g3d_texarchive_open(MAP_TEXTURES);
    if (v->map_tex == NULL) {
        fprintf(stderr, "eidos_view: cannot index %s\n", MAP_TEXTURES);
        return -1;
    }
    if (g3d_model_load(&v->map, MAP_ARCHIVE, (u32)index, v->map_tex) != 0) {
        fprintf(stderr, "eidos_view: %s#%ld has no decodable model\n",
                MAP_ARCHIVE, (long)index);
        return -1;
    }
    v->map_loaded = 1;
    if (g3d_target_init(&v->map_target, SCREEN_W, SCREEN_H) != 0) {
        return -1;
    }
    v->map_target_ok = 1;
    v->map_on = 1;
    v->map_index = index;

    for (i = 0; i < v->map.n_shapes; i++) {
        total_polys += v->map.shapes[i].n_polys;
        if (v->map.shapes[i].texture[0] != '\0') {
            textured++;
        }
    }
    printf("top:    %s#%ld, model '%s', %u shapes (%u textured), "
           "%u vertices, %u polygons, %u texture names indexed\n",
           MAP_ARCHIVE, (long)index, v->map.name, v->map.n_shapes, textured,
           v->map.geom.n_verts, total_polys,
           g3d_texarchive_names(v->map_tex));
    return 0;
}

/*
 * Recompose the pose at a new point on its NANR timeline.  The compositor
 * sizes the canvas to the frame, so the pose is rebuilt rather than
 * repainted; a rig pose is a few thousand pixels and this is once a frame.
 */
static void view_set_pose_time(View *v, u32 t)
{
    G2dPose next;

    if (!v->pose_ok || t == v->pose_t) {
        return;
    }
    if (g2d_compose_pose(&v->rig.ncgr, &v->rig.ncer,
                         v->rig.has_nanr ? &v->rig.nanr : NULL,
                         &v->rig.nmcr.groups[0], t, v->rig.palette.count,
                         &next) != 0) {
        return;   /* keep the frame we have rather than blanking the screen */
    }
    g2d_pose_free(&v->pose);
    v->pose = next;
    v->pose_t = t;
}

/* ------------------------------------------------------------------ */
/* The one task on the game's own list                                 */
/* ------------------------------------------------------------------ */

/*
 * This is what the matched runner calls.  Both arguments come from matched
 * code: `node` is the node the runner is holding, and `work` is what the
 * work accessor answered for it at the runner's own call site
 * (src/matched/sub_020310C4.c, `n->func(n, sub_02030FB4(n))`).
 *
 * It may write `pending` and nothing else: the picture is built from `live`,
 * and the vblank callback is the only thing that moves one to the other.
 *
 * The bob is the engine's own fixed-point sine table rather than a float, so
 * the demo needs no libm and the motion is in the same arithmetic the affine
 * path uses: the angle is a 16-bit binary angle, one turn every 128 frames.
 */
static void view_anim_task(EidosTcblNode *node, void *work)
{
    ViewTask *w = (ViewTask *)work;
    View *v;
    s32 s;

    (void)node;
    if (w == NULL || w->view == NULL) {
        return;
    }
    v = w->view;

    w->t += w->speed;
    v->phase++;
    s = g2d_sin_fx((u16)((v->phase * 512u) & 0xFFFFu));

    v->pending.anim_t = w->t;
    v->pending.bg_hofs = 0;
    /* The screen resource is taller than the display (256 against 192):
     * scroll to the middle of it rather than showing the top. */
    v->pending.bg_vofs = v->nscr.height > SCREEN_H
                         ? (s32)((v->nscr.height - SCREEN_H) / 2u) : 0;
    v->pending.obj_x = ((s32)SCREEN_W - (s32)v->pose.width) / 2;
    v->pending.obj_y = ((s32)SCREEN_H - (s32)v->pose.height) / 2
                       + (s * 6) / G2D_FX_ONE;
}

/*
 * The camera, as a second task on the same list, at a higher priority than
 * the animation so the matched runner reaches it first.  It writes the
 * pending register block exactly as the animation task does: the eye is
 * committed at the vblank and the renderer never sees a half-updated one.
 *
 * The arrows orbit, X and Y (or L and R) move in and out, and START puts
 * the camera back where it started.  The angles are 16-bit binary angles,
 * the same unit engine/g2d's sine table is indexed by.
 */
#define CAM_YAW_STEP   512u
#define CAM_PITCH_STEP 256u
#define CAM_PITCH_MIN  1024u   /* about 5.6 degrees above the horizon */
#define CAM_PITCH_MAX  15360u  /* about 84 degrees, short of straight down */
#define CAM_DIST_MIN   (2 * FX32_ONE)
#define CAM_DIST_MAX   (40 * FX32_ONE)
#define CAM_DIST_STEP  (FX32_ONE / 4)
#define CAM_YAW_HOME   0u
#define CAM_PITCH_HOME 6000u
#define CAM_DIST_HOME  (11 * FX32_ONE)

static void view_camera_task(EidosTcblNode *node, void *work)
{
    ViewTask *w = (ViewTask *)work;
    View *v;
    u32 buttons;
    s32 pitch;

    (void)node;
    if (w == NULL || w->view == NULL) {
        return;
    }
    v = w->view;
    buttons = v->plat != NULL ? eidos_platform_buttons(v->plat) : 0u;

    if (buttons & EIDOS_PAD_LEFT) {
        v->pending.cam_yaw = (u16)(v->pending.cam_yaw - CAM_YAW_STEP);
    }
    if (buttons & EIDOS_PAD_RIGHT) {
        v->pending.cam_yaw = (u16)(v->pending.cam_yaw + CAM_YAW_STEP);
    }
    pitch = (s32)v->pending.cam_pitch;
    if (buttons & EIDOS_PAD_UP) {
        pitch += (s32)CAM_PITCH_STEP;
    }
    if (buttons & EIDOS_PAD_DOWN) {
        pitch -= (s32)CAM_PITCH_STEP;
    }
    if (pitch < (s32)CAM_PITCH_MIN) { pitch = (s32)CAM_PITCH_MIN; }
    if (pitch > (s32)CAM_PITCH_MAX) { pitch = (s32)CAM_PITCH_MAX; }
    v->pending.cam_pitch = (u16)pitch;

    if (buttons & (EIDOS_PAD_X | EIDOS_PAD_L)) {
        v->pending.cam_dist -= CAM_DIST_STEP;
    }
    if (buttons & (EIDOS_PAD_Y | EIDOS_PAD_R)) {
        v->pending.cam_dist += CAM_DIST_STEP;
    }
    if (v->pending.cam_dist < CAM_DIST_MIN) {
        v->pending.cam_dist = CAM_DIST_MIN;
    }
    if (v->pending.cam_dist > CAM_DIST_MAX) {
        v->pending.cam_dist = CAM_DIST_MAX;
    }
    if (buttons & EIDOS_PAD_START) {
        v->pending.cam_yaw = CAM_YAW_HOME;
        v->pending.cam_pitch = CAM_PITCH_HOME;
        v->pending.cam_dist = CAM_DIST_HOME;
    }
}

/*
 * Build the game's task-callback list and put the animation task on it.
 * Everything below the two bw_tcbl_ calls is matched: the constructor, the
 * free-list build, the heap the pool comes from.
 */
static int view_start_tasks(View *v, u32 speed)
{
    ViewTask *w;

    bw_heap_init(BW_HEAP_COUNT);

    v->tasks = bw_tcbl_create(VIEW_HEAP, (u16)VIEW_HEAP, VIEW_TASK_SLOTS,
                              (u32)sizeof(ViewTask));
    if (v->tasks == NULL) {
        fprintf(stderr, "eidos_view: the matched tcbl constructor failed\n");
        return -1;
    }

    v->anim_node = bw_tcbl_register(v->tasks, view_anim_task,
                                    (u32)sizeof(ViewTask), VIEW_ANIM_PRIORITY);
    if (v->anim_node == NULL) {
        fprintf(stderr, "eidos_view: could not register the animation task\n");
        return -1;
    }

    w = (ViewTask *)bw_tcbl_work(v->anim_node);
    memset(w, 0, sizeof(*w));
    w->view = v;
    w->t = 0;
    w->speed = speed;

    if (v->map_on) {
        v->pending.cam_yaw = CAM_YAW_HOME;
        v->pending.cam_pitch = CAM_PITCH_HOME;
        v->pending.cam_dist = CAM_DIST_HOME;
        v->live = v->pending;
        v->cam_node = bw_tcbl_register(v->tasks, view_camera_task,
                                       (u32)sizeof(ViewTask),
                                       VIEW_CAM_PRIORITY);
        if (v->cam_node == NULL) {
            fprintf(stderr, "eidos_view: could not register the camera task\n");
            return -1;
        }
        w = (ViewTask *)bw_tcbl_work(v->cam_node);
        memset(w, 0, sizeof(*w));
        w->view = v;
    }

    printf("tasks:  %u slots of %u work bytes on heap %u, "
           "animation task at priority %u%s\n",
           (unsigned)v->tasks->count, (unsigned)bw_tcbl_work_size(v->tasks),
           (unsigned)VIEW_HEAP, (unsigned)VIEW_ANIM_PRIORITY,
           v->map_on ? ", camera task at priority 32" : "");
    return 0;
}

/* ------------------------------------------------------------------ */
/* The three frame callbacks                                           */
/* ------------------------------------------------------------------ */

/*
 * One frame of logic, and it is game code: one pass of the task-callback
 * list, src/matched/sub_020310C4.c.  The loop does not know what is on the
 * list; the list decides.
 */
static void view_step(void *user)
{
    View *v = (View *)user;

    bw_tcbl_run(v->tasks);
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

    /* The committed animation time is the one the picture is built from. */
    view_set_pose_time(v, v->live.anim_t);

    if (v->map_on) {
        /*
         * The top screen is the 3D layer.  The eye orbits the model on the
         * committed yaw, pitch and distance; the model matrix is identity,
         * so the geometry buffer's own model space is world space, which is
         * the same space tests/test_g3d.c compares against the oracle's OBJ.
         */
        G3dMtx view_m, proj;
        G3dVec eye, at, up;
        s32 cp = g2d_cos_fx(v->live.cam_pitch);
        s32 sp = g2d_sin_fx(v->live.cam_pitch);
        s32 cy = g2d_cos_fx(v->live.cam_yaw);
        s32 sy = g2d_sin_fx(v->live.cam_yaw);
        fx32 horiz = FX_MUL(v->live.cam_dist, cp);

        at.x = 0; at.y = FX32_ONE / 2; at.z = 0;
        up.x = 0; up.y = FX32_ONE;     up.z = 0;
        eye.x = at.x + FX_MUL(horiz, sy);
        eye.y = at.y + FX_MUL(v->live.cam_dist, sp);
        eye.z = at.z + FX_MUL(horiz, cy);
        g3d_mtx_lookat(&view_m, &eye, &up, &at);
        g3d_mtx_perspective(&proj, MAP_FOVY, (fx32)(4 * FX32_ONE / 3),
                            MAP_NEAR, MAP_FAR);

        g3d_target_clear(&v->map_target, 0, 0, 40, 255);
        g3d_render_model(&v->map_target, &v->map, &view_m, &proj, v->map_tex);

        /* Through the compositor, not straight into the buffer: the 2D
         * frame is empty here, so every pixel is its backdrop and the 3D
         * layer behind it shows.  A real BG0 goes in `desc` beside it. */
        memset(&desc, 0, sizeof(desc));
        desc.backdrop_pal = &v->pal;
        g3d_compose_over(&desc, &v->map_target, SCREEN_W, SCREEN_H, top);
        goto bottom_screen;
    }

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

bottom_screen:
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
/* Music                                                              */
/* ------------------------------------------------------------------ */
/*
 * The platform layer's audio callback is a plain function pointer over a
 * 32768 Hz stereo signed-16-bit buffer, which is exactly what
 * bw_audio_player_mix() fills, so the whole of the wiring is this thunk and
 * one eidos_platform_set_audio() call.  The callback runs on SDL's audio
 * thread and must not allocate or block: the player prepared every note and
 * decoded every wave it needs when it was created, and mixing touches
 * nothing but its own sixteen voices and a stack block buffer.
 *
 * SEQ 1062 is SEQ_BGM_GYM, one of the three sequences tests/test_audio.c
 * checks sample for sample against aletheia's sseqsynth.py.  The title
 * music replaces it here once the boot path names its SEQ id; until then
 * this is a real sequence out of the player's own ROM rather than a
 * placeholder tone.
 */
#define VIEW_MUSIC_SEQ     1062
#define VIEW_MUSIC_SECONDS 60.0

static void view_audio(void *user, s16 *out, u32 frames)
{
    bw_audio_player_mix((BwAudioPlayer *)user, out, frames);
}

/* ------------------------------------------------------------------ */

static void usage(void)
{
    printf("eidos_view [--data-root PATH] [--scale N] [--side-by-side]\n"
           "           [--gap N] [--species N] [--frames N] [--shot PATH]\n"
           "           [--shot-first PATH] [--anim-speed N] [--no-audio]\n"
           "           [--music [SEQ]]\n"
           "\n"
           "Escape quits, F12 writes a screenshot.  --shot-first writes one\n"
           "after the first frame, so two frames can be compared.\n"
           "--music plays a sequence out of the SDAT through engine/audio;\n"
           "with no argument it plays SEQ %d, SEQ_BGM_GYM.\n", VIEW_MUSIC_SEQ);
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
    const char *shot_first = NULL;
    long frames = 0;
    int species = 25;
    long map_index = -1;
    int anim_speed = 1;
    int audio = 1;
    long music_seq = -1;
    BwSdat music_sdat;
    BwAudioPlayer *music = NULL;
    int music_sdat_open = 0;
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
        } else if (strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            map_index = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--species") == 0 && i + 1 < argc) {
            species = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shot = argv[++i];
        } else if (strcmp(argv[i], "--shot-first") == 0 && i + 1 < argc) {
            shot_first = argv[++i];
        } else if (strcmp(argv[i], "--anim-speed") == 0 && i + 1 < argc) {
            anim_speed = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-audio") == 0) {
            audio = 0;
        } else if (strcmp(argv[i], "--music") == 0) {
            music_seq = VIEW_MUSIC_SEQ;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
                music_seq = strtol(argv[++i], NULL, 10);
            }
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
    if (map_index >= 0 && view_load_map(&view, (s32)map_index) != 0) {
        view_free(&view);
        return 1;
    }
    if (view_start_tasks(&view, (u32)(anim_speed > 0 ? anim_speed : 1)) != 0) {
        view_free(&view);
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
    view.plat = plat;
    printf("window: %d x %d at scale %d, frame period %llu ns "
           "(%s), audio %s\n",
           cfg.layout == EIDOS_LAYOUT_SIDE_BY_SIDE
               ? (int)SCREEN_W * 2 + cfg.gap : (int)SCREEN_W,
           cfg.layout == EIDOS_LAYOUT_SIDE_BY_SIDE
               ? (int)SCREEN_H : (int)SCREEN_H * 2 + cfg.gap,
           cfg.scale, (unsigned long long)eidos_frame_period_ns(),
           cfg.layout == EIDOS_LAYOUT_SIDE_BY_SIDE ? "side by side" : "stacked",
           eidos_platform_audio_open(plat) ? "open" : "silent");

    if (music_seq >= 0) {
        char sdat_path[1024];
        sprintf(sdat_path, "%s/wb_sound_data.sdat", data_root);
        if (bw_sdat_load(&music_sdat, sdat_path) == 0) {
            music_sdat_open = 1;
            music = bw_audio_player_create(&music_sdat, (u32)music_seq,
                                           VIEW_MUSIC_SECONDS, 1);
        }
        if (music != NULL) {
            const char *name = bw_sdat_name(&music_sdat, BW_SDAT_SEQ, (u32)music_seq);
            eidos_platform_set_audio(plat, view_audio, music);
            printf("music: SEQ %ld (%s), looping\n", music_seq,
                   name != NULL ? name : "unnamed");
        } else {
            fprintf(stderr, "eidos_view: cannot play SEQ %ld; running silent\n",
                    music_seq);
        }
    }

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

        if (shot_first != NULL && eidos_frame_count(&loop) == 1) {
            if (eidos_platform_screenshot(plat, shot_first) == 0) {
                printf("screenshot: %s (frame 1, animation t %u)\n",
                       shot_first, (unsigned)view.pose_t);
            } else {
                rc = 1;
            }
        }
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
            printf("screenshot: %s (%u x %u, animation t %u)\n", shot, sw, sh,
                   (unsigned)view.pose_t);
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
    if (music != NULL) {
        printf("music: %llu frames mixed, %u channel overflows\n",
               (unsigned long long)bw_audio_player_frames(music),
               bw_audio_player_voice_overflows(music));
    }
    /* Unhook the callback before the player is freed: the audio device is
     * still running until eidos_platform_quit() closes it. */
    eidos_platform_set_audio(plat, NULL, NULL);
    eidos_platform_quit(plat);
    bw_audio_player_free(music);
    if (music_sdat_open) bw_sdat_close(&music_sdat);
    free(top);
    free(bottom);
    view_free(&view);
    return rc;
}
