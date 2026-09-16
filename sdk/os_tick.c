/*
 * sdk/os_tick.c -- OS: the SDK's tick counter, onto the frame loop.
 *
 * aletheia's src/matched/func_02021444.c (palanm.c) stamps its module state
 * block with `OS_GetTick()`'s full u64, stored as one 4-aligned long long.
 * That is the only thing the current closure asks of the OS clock, and it
 * fixes both the width and the unit:
 *
 *   - u64, returned in r0:r1 on the DS.
 *   - the SDK tick, the system clock divided by 64, 523655.96875 Hz.  Not
 *     milliseconds and not 64 Hz; engine/frame/frame.h carries the
 *     arithmetic and the reason.
 *
 * The count comes from the frame loop rather than from the host clock, so a
 * tick difference the game measures is a whole number of frames' worth
 * whatever the host was doing, and a headless run that steps frames without
 * sleeping advances the game's clock exactly as a paced one does.  With no
 * loop made current (a unit test that never starts one) the counter is zero
 * and stays zero, which is a defensible reading of "no time has passed" and
 * is visibly wrong rather than subtly wrong.
 */

#include "frame.h"
#include "types.h"

u64 OS_GetTick(void)
{
    return eidos_frame_tick(eidos_frame_current());
}

u32 OS_GetTickLo(void)
{
    return (u32)OS_GetTick();
}
