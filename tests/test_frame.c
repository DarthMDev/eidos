/*
 * tests/test_frame.c -- the frame skeleton, synthetically.
 *
 * No SDL, no game data, no oracle: the frame loop is the DS's convention
 * about *when* things happen, and every expectation here is computed from
 * the two constants the convention is built on (the 33513982 Hz system clock
 * and the 560190-clock frame).
 *
 * What is checked:
 *
 *   - the callback order is step, vblank, render, and the counters advance
 *     at the vblank between them, so a step function sees the number of the
 *     frame it is building and a render function sees the next one,
 *   - the commit convention: a value the step function writes reaches the
 *     render function only through the vblank callback,
 *   - the OS tick advances by 8752 ticks a frame with the 62/64 remainder
 *     carried, so 32 frames are exactly 32 * 560190 / 64 = 280095 ticks,
 *   - a second of frames is a second of ticks, to the tick,
 *   - the tick/millisecond conversions round-trip.
 */

#include <stdio.h>
#include <string.h>

#include "frame.h"
#include "port_test.h"

typedef struct Trace {
    char order[64];
    u32  n;
    u64  step_frame, vblank_frame, render_frame;
    int  pending, live, seen_by_render;
} Trace;

static void note(Trace *t, char c)
{
    if (t->n + 1u < sizeof(t->order)) {
        t->order[t->n++] = c;
    }
}

static EidosFrameLoop *g_loop;

static void on_step(void *user)
{
    Trace *t = (Trace *)user;
    note(t, 's');
    t->step_frame = eidos_frame_count(g_loop);
    t->pending = 42;          /* written before the commit */
    t->seen_by_render = -1;
}

static void on_vblank(void *user)
{
    Trace *t = (Trace *)user;
    note(t, 'v');
    t->vblank_frame = eidos_frame_count(g_loop);
    t->live = t->pending;     /* the commit */
}

static void on_render(void *user, u8 *top, u8 *bottom)
{
    Trace *t = (Trace *)user;
    note(t, 'r');
    t->render_frame = eidos_frame_count(g_loop);
    t->seen_by_render = t->live;
    if (top != NULL) {
        top[0] = 1;
    }
    if (bottom != NULL) {
        bottom[0] = 2;
    }
}

int main(void)
{
    EidosFrameLoop loop;
    EidosFrameHooks hooks;
    Trace t;
    u8 top[4], bottom[4];
    u64 ticks_32, ticks_60;
    u32 i;

    memset(&t, 0, sizeof(t));
    memset(top, 0, sizeof(top));
    memset(bottom, 0, sizeof(bottom));
    memset(&hooks, 0, sizeof(hooks));
    hooks.step = on_step;
    hooks.vblank = on_vblank;
    hooks.render = on_render;
    hooks.user = &t;

    eidos_frame_init(&loop, &hooks, top, bottom);
    g_loop = &loop;

    printf("callback order and the commit\n");
    eidos_frame_run_one(&loop);
    t.order[t.n] = '\0';
    BW_CHECK_EQ("three callbacks ran", t.n, 3);
    BW_CHECK_EQ("order is step, vblank, render",
                strcmp(t.order, "svr"), 0);
    BW_CHECK_EQ("step sees frame 0", (long)t.step_frame, 0);
    BW_CHECK_EQ("vblank sees frame 0", (long)t.vblank_frame, 0);
    BW_CHECK_EQ("render sees frame 1", (long)t.render_frame, 1);
    BW_CHECK_EQ("the render callback sees the committed value",
                t.seen_by_render, 42);
    BW_CHECK_EQ("the frame buffers reached the render callback",
                top[0] + bottom[0], 3);

    printf("counters\n");
    BW_CHECK_EQ("one frame counted", (long)eidos_frame_count(&loop), 1);
    BW_CHECK_EQ("one frame of ticks",
                (long)eidos_frame_tick(&loop), EIDOS_FRAME_CLOCKS / 64u);

    /*
     * 32 frames: 32 * 560190 = 17926080 clocks, which is exactly 280095
     * ticks.  A counter that dropped the 62/64 remainder would be 31 ticks
     * short here, which is the whole point of carrying it.
     */
    for (i = 1; i < 32u; i++) {
        eidos_frame_run_one(&loop);
    }
    ticks_32 = eidos_frame_tick(&loop);
    printf("  32 frames: %lu ticks\n", (unsigned long)ticks_32);
    BW_CHECK_EQ("32 frames are exact in ticks",
                (long)ticks_32, (long)(32u * EIDOS_FRAME_CLOCKS / 64u));
    BW_CHECK_EQ("32 frames counted", (long)eidos_frame_count(&loop), 32);

    /* A second of frames (59.8261 of them) is a second of ticks to within
     * one frame's worth, which is what "the tick is the same clock" means. */
    for (i = 32u; i < 60u; i++) {
        eidos_frame_run_one(&loop);
    }
    ticks_60 = eidos_frame_tick(&loop);
    printf("  60 frames: %lu ticks, one second is %u\n",
           (unsigned long)ticks_60, EIDOS_TICKS_PER_SECOND);
    BW_CHECK_EQ("60 frames are 60 * 560190 / 64 ticks",
                (long)ticks_60, (long)(60u * EIDOS_FRAME_CLOCKS / 64u));
    BW_REQUIRE("60 frames is a little over a second of ticks",
               ticks_60 > (u64)EIDOS_TICKS_PER_SECOND
               && ticks_60 - (u64)EIDOS_TICKS_PER_SECOND < 10000u);

    printf("the current loop, which is what OS_GetTick reads\n");
    BW_CHECK_EQ("no loop is current by default",
                eidos_frame_current() == NULL, 1);
    eidos_frame_set_current(&loop);
    BW_CHECK_EQ("the loop is current", eidos_frame_current() == &loop, 1);
    BW_CHECK_EQ("the current loop's tick",
                (long)eidos_frame_tick(eidos_frame_current()), (long)ticks_60);
    eidos_frame_set_current(NULL);

    printf("tick and millisecond conversions\n");
    BW_CHECK_EQ("a second of ticks is 1000 ms",
                (long)eidos_ticks_to_ms((u64)EIDOS_TICKS_PER_SECOND), 1000);
    BW_CHECK_EQ("1000 ms is a second of ticks",
                (long)eidos_ms_to_ticks(1000), (long)EIDOS_TICKS_PER_SECOND);
    BW_CHECK_EQ("no ticks is no time", (long)eidos_ticks_to_ms(0), 0);

    printf("a loop with no callbacks still counts\n");
    eidos_frame_init(&loop, NULL, NULL, NULL);
    eidos_frame_run_one(&loop);
    BW_CHECK_EQ("one frame", (long)eidos_frame_count(&loop), 1);
    eidos_frame_run_one(NULL);   /* must not crash */

    BW_REPORT("test_frame");
}
