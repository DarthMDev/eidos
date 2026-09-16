/*
 * engine/frame/frame.c -- the vblank-driven frame loop and the OS tick.
 *
 * See frame.h for the DS convention this implements and for where the tick
 * unit comes from.  No SDL, no game data, standard library only.
 */

#include <string.h>

#include "frame.h"

/*
 * One frame is 560190 system clocks and one tick is 64 of them: 8752 whole
 * ticks with 62 clocks left over.  Written as a division and a modulo of the
 * two constants in frame.h rather than as two literals, so the relationship
 * stays visible.  The remainder is 62/64 of a tick per frame, which is a
 * whole tick every 32 frames -- half a second of drift per hour if it were
 * dropped.
 */
#define TICKS_PER_FRAME (EIDOS_FRAME_CLOCKS / 64u)
#define TICK_REMAINDER  (EIDOS_FRAME_CLOCKS % 64u)

static EidosFrameLoop *g_current;

void eidos_frame_init(EidosFrameLoop *loop, const EidosFrameHooks *hooks,
                      u8 *top, u8 *bottom)
{
    if (loop == NULL) {
        return;
    }
    memset(loop, 0, sizeof(*loop));
    if (hooks != NULL) {
        loop->hooks = *hooks;
    }
    loop->top = top;
    loop->bottom = bottom;
}

void eidos_frame_run_one(EidosFrameLoop *loop)
{
    if (loop == NULL) {
        return;
    }

    if (loop->hooks.step != NULL) {
        loop->hooks.step(loop->hooks.user);
    }

    /*
     * Vblank: the commit, and the counters.  The frame and the tick advance
     * here rather than at the top of the iteration because on hardware it is
     * the vblank interrupt that the OS counts, and a step function that asks
     * for the frame number must see the number of the frame it is building,
     * not of the one already shown.
     */
    if (loop->hooks.vblank != NULL) {
        loop->hooks.vblank(loop->hooks.user);
    }
    loop->frame++;
    loop->tick += TICKS_PER_FRAME;
    loop->tick_rem += TICK_REMAINDER;
    if (loop->tick_rem >= 64u) {
        loop->tick_rem -= 64u;
        loop->tick += 1;
    }

    if (loop->hooks.render != NULL) {
        loop->hooks.render(loop->hooks.user, loop->top, loop->bottom);
    }
}

u64 eidos_frame_count(const EidosFrameLoop *loop)
{
    return loop == NULL ? 0 : loop->frame;
}

u64 eidos_frame_tick(const EidosFrameLoop *loop)
{
    return loop == NULL ? 0 : loop->tick;
}

void eidos_frame_set_current(EidosFrameLoop *loop)
{
    g_current = loop;
}

EidosFrameLoop *eidos_frame_current(void)
{
    return g_current;
}

u64 eidos_ticks_to_ms(u64 ticks)
{
    return ticks * 1000ull / (u64)EIDOS_TICKS_PER_SECOND;
}

u64 eidos_ms_to_ticks(u64 ms)
{
    return ms * (u64)EIDOS_TICKS_PER_SECOND / 1000ull;
}
