/*
 * tests/test_one_layout.c -- the one-screen layout's geometry and the
 * geometry engine's view scale.
 *
 * No window, no GPU and no game data: the arithmetic the presenter, the
 * mouse and the 3D setup share (engine/platform/geometry.c) and the clip
 * matrix widening (engine/g3d/mtx.c) are checked against numbers worked out
 * by hand.
 *
 * What is checked:
 *
 *   - the world view's factors (hor+): a 16:9 window shows the DS's own
 *     vertical view (ky = 1) and three quarters of the DS horizontal
 *     extent per window width (kx = 0.75), a 4:3 window is the DS view
 *     exactly, an ultra-wide window needs a smaller kx, and a window
 *     narrower than 4:3 keeps the DS's whole width and shows more above
 *     and below instead;
 *   - `zoom` multiplies both factors (0.75 for a slight zoom-out), and is
 *     clamped, so a bad value cannot produce a degenerate matrix;
 *   - the DS rectangle in the wide view: 4:3 to within a pixel, centred,
 *     computed from the rounded factors, so the 2D layers land where the 3D
 *     put the DS screen;
 *   - the window pixel to DS pixel mapping inside that rectangle;
 *   - the fallback viewport: the two screens side by side filling the
 *     window letterboxed, the swap, and a whole-number pixel scale;
 *   - the arrangement the layout gives (16:9, no panel);
 *   - the clip matrix scaling: default 1:1 is the hardware's matrix, a
 *     scale moves clip x and y and leaves z and w, a vertex outside the DS
 *     view falls inside the widened one (what the game's BOX_TEST sees),
 *     and restoring 1:1 restores the matrix.
 */

#include <stdio.h>
#include <string.h>

#include "g3d.h"
#include "hw3d.h"
#include "platform.h"
#include "types.h"

#include "port_test.h"

static int absi(int v)
{
    return v < 0 ? -v : v;
}

static void check_world(const char *name, int w, int h, float zoom, int want_kx,
                        int want_ky)
{
    EidosWorldGeometry g;
    char label[96];

    eidos_world_geometry(w, h, zoom, &g);
    snprintf(label, sizeof label, "%s: kx", name);
    BW_CHECK_EQ(label, g.kx, want_kx);
    snprintf(label, sizeof label, "%s: ky", name);
    BW_CHECK_EQ(label, g.ky, want_ky);
    snprintf(label, sizeof label, "%s: the DS rectangle is 4:3", name);
    BW_CHECK_EQ(label, absi(g.ds.w * 3 - g.ds.h * 4) <= 4, 1);
    snprintf(label, sizeof label, "%s: centred in x", name);
    BW_CHECK_EQ(label, absi(2 * g.ds.x + g.ds.w - w) <= 1, 1);
    snprintf(label, sizeof label, "%s: centred in y", name);
    BW_CHECK_EQ(label, absi(2 * g.ds.y + g.ds.h - h) <= 1, 1);
}

static int test_world(void)
{
    EidosWorldGeometry g;

    printf("the world view\n");
    /* 16:9: kx = (4/3) / (16/9) = 0.75 = 3072; the DS rectangle is 960 x 720
     * in the middle of 1280 x 720. */
    check_world("1280x720", 1280, 720, 1.0f, 3072, 4096);
    eidos_world_geometry(1280, 720, 1.0f, &g);
    BW_CHECK_EQ("1280x720: ds x", g.ds.x, 160);
    BW_CHECK_EQ("1280x720: ds y", g.ds.y, 0);
    BW_CHECK_EQ("1280x720: ds w", g.ds.w, 960);
    BW_CHECK_EQ("1280x720: ds h", g.ds.h, 720);
    check_world("1920x1080", 1920, 1080, 1.0f, 3072, 4096);
    eidos_world_geometry(1920, 1080, 1.0f, &g);
    BW_CHECK_EQ("1920x1080: ds w", g.ds.w, 1440);
    BW_CHECK_EQ("1920x1080: ds x", g.ds.x, 240);

    /* a 4:3 window is the DS view and nothing else */
    check_world("1024x768", 1024, 768, 1.0f, 4096, 4096);
    eidos_world_geometry(1024, 768, 1.0f, &g);
    BW_CHECK_EQ("1024x768: ds is the window w", g.ds.w, 1024);
    BW_CHECK_EQ("1024x768: ds is the window h", g.ds.h, 768);
    BW_CHECK_EQ("1024x768: ds x", g.ds.x, 0);

    /* ultra-wide: 21:9 (2560x1080): kx = (4/3) / (64/27) = 0.5625 = 2304 */
    check_world("2560x1080", 2560, 1080, 1.0f, 2304, 4096);
    /* 32:9: kx = (4/3) / (32/9) = 0.375 = 1536 */
    check_world("3840x1080", 3840, 1080, 1.0f, 1536, 4096);

    /* narrower than 4:3: the whole DS width stays, more above and below:
     * 800x800, a = 1, ky = 0.75 */
    check_world("800x800", 800, 800, 1.0f, 4096, 3072);
    eidos_world_geometry(800, 800, 1.0f, &g);
    BW_CHECK_EQ("800x800: ds w", g.ds.w, 800);
    BW_CHECK_EQ("800x800: ds h", g.ds.h, 600);
    BW_CHECK_EQ("800x800: ds y", g.ds.y, 100);

    /* zoom: a slight zoom-out shows more in both directions */
    check_world("1280x720 zoom 0.75", 1280, 720, 0.75f, 2304, 3072);
    eidos_world_geometry(1280, 720, 0.75f, &g);
    BW_CHECK_EQ("zoom 0.75: ds w", g.ds.w, 720);
    BW_CHECK_EQ("zoom 0.75: ds h", g.ds.h, 540);
    BW_CHECK_EQ("zoom 0.75: ds x", g.ds.x, 280);
    BW_CHECK_EQ("zoom 0.75: ds y", g.ds.y, 90);
    check_world("1280x720 zoom 0.5", 1280, 720, 0.5f, 1536, 2048);

    /* bad zoom: unset is 1, tiny and huge are clamped */
    check_world("zoom 0 is 1", 1280, 720, 0.0f, 3072, 4096);
    check_world("zoom 0.01 is 0.25", 1280, 720, 0.01f, 768, 1024);
    check_world("zoom 9 is 2", 1280, 720, 9.0f, 6144, 8192);

    /* nothing to show */
    eidos_world_geometry(0, 0, 1.0f, &g);
    BW_CHECK_EQ("an empty window: kx", g.kx, 4096);
    BW_CHECK_EQ("an empty window: ds w", g.ds.w, 0);

    /* the window to DS mapping: the middle of the rectangle is the middle of
     * the DS screen, the corners are the corners, outside clamps */
    {
        int x, y, in;

        eidos_world_geometry(1280, 720, 1.0f, &g);
        in = eidos_world_to_ds(&g, 640, 360, &x, &y);
        BW_CHECK_EQ("the centre is inside", in, 1);
        BW_CHECK_EQ("the centre is DS x 128", x, 128);
        BW_CHECK_EQ("the centre is DS y 96", y, 96);
        in = eidos_world_to_ds(&g, 160, 0, &x, &y);
        BW_CHECK_EQ("the top-left corner is inside", in, 1);
        BW_CHECK_EQ("... DS x 0", x, 0);
        BW_CHECK_EQ("... DS y 0", y, 0);
        in = eidos_world_to_ds(&g, 1119, 719, &x, &y);
        BW_CHECK_EQ("the bottom-right corner is inside", in, 1);
        BW_CHECK_EQ("... DS x 255", x, 255);
        BW_CHECK_EQ("... DS y 191", y, 191);
        in = eidos_world_to_ds(&g, 20, 300, &x, &y);
        BW_CHECK_EQ("left of the rectangle is outside", in, 0);
        BW_CHECK_EQ("... and clamps to DS x 0", x, 0);
        in = eidos_world_to_ds(&g, 1200, 300, &x, &y);
        BW_CHECK_EQ("right of the rectangle is outside", in, 0);
        BW_CHECK_EQ("... and clamps to DS x 255", x, 255);
    }
    return 0;
}

static int test_fallback(void)
{
    EidosViewport vp;
    EidosScreenPlacement pl;

    printf("the fallback viewport\n");
    /* 1280x720: 512 x 192 content fits the width: 1280 x 480, centred
     * vertically (y = 120); each screen is 640 x 480 */
    eidos_one_fallback_viewport(0, 0, 1280, 720, &vp);
    BW_CHECK_EQ("1280x720: content w", vp.content.w, 1280);
    BW_CHECK_EQ("1280x720: content h", vp.content.h, 480);
    BW_CHECK_EQ("1280x720: content y", vp.content.y, 120);
    BW_CHECK_EQ("1280x720: top x", vp.screen[0].x, 0);
    BW_CHECK_EQ("1280x720: top w", vp.screen[0].w, 640);
    BW_CHECK_EQ("1280x720: top h", vp.screen[0].h, 480);
    BW_CHECK_EQ("1280x720: top y", vp.screen[0].y, 120);
    BW_CHECK_EQ("1280x720: bottom x", vp.screen[1].x, 640);
    BW_CHECK_EQ("1280x720: bottom w", vp.screen[1].w, 640);

    /* swapped: the bottom screen is on the left */
    eidos_one_fallback_viewport(1, 0, 1280, 720, &vp);
    BW_CHECK_EQ("swapped: bottom x", vp.screen[1].x, 0);
    BW_CHECK_EQ("swapped: top x", vp.screen[0].x, 640);

    /* 1920x1080: 1920 x 720 at y = 180 */
    eidos_one_fallback_viewport(0, 0, 1920, 1080, &vp);
    BW_CHECK_EQ("1920x1080: content h", vp.content.h, 720);
    BW_CHECK_EQ("1920x1080: content y", vp.content.y, 180);
    BW_CHECK_EQ("1920x1080: top w", vp.screen[0].w, 960);

    /* a tall window is letterboxed above and below: 600x900 gives 600 x 225 */
    eidos_one_fallback_viewport(0, 0, 600, 900, &vp);
    BW_CHECK_EQ("600x900: content w", vp.content.w, 600);
    BW_CHECK_EQ("600x900: content h", vp.content.h, 225);
    BW_CHECK_EQ("600x900: content y", vp.content.y, 337);

    /* a whole-number pixel scale: 2x in 1280x720 is 1024 x 384, centred */
    eidos_one_fallback_viewport(0, 2, 1280, 720, &vp);
    BW_CHECK_EQ("2x: content w", vp.content.w, 1024);
    BW_CHECK_EQ("2x: content h", vp.content.h, 384);
    BW_CHECK_EQ("2x: content x", vp.content.x, 128);
    BW_CHECK_EQ("2x: content y", vp.content.y, 168);
    BW_CHECK_EQ("2x: top w", vp.screen[0].w, 512);

    /* the stylus: the bottom screen's middle is the middle of the DS pixels */
    {
        int x, y, in;

        eidos_one_fallback_viewport(0, 0, 1280, 720, &vp);
        eidos_one_fallback_place(0, &pl);
        in = eidos_viewport_to_touch(&pl, &vp, 960, 360, &x, &y);
        BW_CHECK_EQ("the bottom screen's centre is on it", in, 1);
        BW_CHECK_EQ("... DS x 128", x, 128);
        BW_CHECK_EQ("... DS y 96", y, 96);
        in = eidos_viewport_to_touch(&pl, &vp, 100, 360, &x, &y);
        BW_CHECK_EQ("the top screen is not the stylus's", in, 0);
    }

    printf("the arrangement\n");
    eidos_layout_place(EIDOS_LAYOUT_ONE, 0, 0, &pl);
    BW_CHECK_EQ("the one-screen arrangement is 16:9: w", pl.width, 512);
    BW_CHECK_EQ("... h", pl.height, 288);
    BW_CHECK_EQ("... top screen", pl.screen[0].x, 0);
    BW_CHECK_EQ("... bottom screen", pl.screen[1].x, 256);
    return 0;
}

static int test_scale(void)
{
    G3dMtxStack st;
    const G3dMtx *c;
    G3dMtx before;
    fx32 x, y, z, w;
    s32 kx, ky;

    printf("the clip matrix scale\n");
    g3d_hw_view_scale(&kx, &ky);
    BW_CHECK_EQ("the default scale is 1 in x", kx, FX32_ONE);
    BW_CHECK_EQ("... and in y", ky, FX32_ONE);

    g3d_mtx_stack_init(&st);
    g3d_mtx_stack_set_mode(&st, G3D_MTX_MODE_POSITION);
    g3d_mtx_stack_load_identity(&st);
    c = g3d_mtx_stack_clip(&st);
    before = *c;
    /* a vertex a quarter outside the DS view in x and y */
    g3d_mtx_xform(c, FX32_ONE * 5 / 4, FX32_ONE * 5 / 4, FX32_ONE / 2,
                  &x, &y, &z, &w);
    BW_CHECK_EQ("1:1: x is outside the view", x > w, 1);
    BW_CHECK_EQ("1:1: y is outside the view", y > w, 1);

    /* widen to 16:9: x by 0.75 (the game would now keep this vertex) */
    g3d_hw_set_view_scale(3072, 4096);
    c = g3d_mtx_stack_clip(&st);
    g3d_mtx_xform(c, FX32_ONE * 5 / 4, FX32_ONE * 5 / 4, FX32_ONE / 2,
                  &x, &y, &z, &w);
    BW_CHECK_EQ("0.75: x is scaled", x, FX32_ONE * 5 / 4 * 3 / 4);
    BW_CHECK_EQ("0.75: x is inside the view", x <= w, 1);
    BW_CHECK_EQ("0.75: y is untouched", y, FX32_ONE * 5 / 4);
    BW_CHECK_EQ("0.75: z is untouched", z, FX32_ONE / 2);
    BW_CHECK_EQ("0.75: w is untouched", w, FX32_ONE);
    g3d_hw_view_scale(&kx, &ky);
    BW_CHECK_EQ("the scale reads back: kx", kx, 3072);
    BW_CHECK_EQ("... ky", ky, 4096);

    /* a zoom-out as well: y by 0.5 */
    g3d_hw_set_view_scale(2048, 2048);
    c = g3d_mtx_stack_clip(&st);
    g3d_mtx_xform(c, FX32_ONE, FX32_ONE, 0, &x, &y, &z, &w);
    BW_CHECK_EQ("0.5: x", x, FX32_ONE / 2);
    BW_CHECK_EQ("0.5: y", y, FX32_ONE / 2);

    /* a bad factor is the identity, never a collapsed matrix */
    g3d_hw_set_view_scale(0, -5);
    g3d_hw_view_scale(&kx, &ky);
    BW_CHECK_EQ("a zero factor is 1: kx", kx, FX32_ONE);
    BW_CHECK_EQ("a negative factor is 1: ky", ky, FX32_ONE);

    /* back to 1:1: the matrix is the hardware's again */
    g3d_hw_set_view_scale(FX32_ONE, FX32_ONE);
    c = g3d_mtx_stack_clip(&st);
    BW_CHECK_EQ("1:1 again: the matrix is as it was",
                memcmp(c->m, before.m, sizeof(before.m)), 0);
    return 0;
}

int main(void)
{
    if (test_world() != 0 || test_fallback() != 0 || test_scale() != 0) {
        return 1;
    }
    BW_REPORT("test_one_layout");
}
