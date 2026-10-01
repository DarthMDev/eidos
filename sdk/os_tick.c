/*
 * sdk/os_tick.c -- OS: the SDK's tick counter (os_tick.o), onto the frame
 * loop.
 *
 * aletheia's src/matched/func_02021444.c (palanm.c) stamps its module state
 * block with `OS_GetTick()`'s full u64, stored as one 4-aligned long long.
 * That fixes both the width and the unit:
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
 *
 * OS_InitTick (func_02086FE8, EXACT in aletheia) starts TM0 with the /64
 * prescaler and counts its overflows in an interrupt (OSi_CountUpTick).
 * That timer is what the frame loop's counter stands for, so here the
 * initialiser only records that the tick is in use: there is no TM0 to
 * start and no overflow interrupt to install.  sdk/os_alarm.c's alarms run
 * on this same clock.
 */

#include "frame.h"
#include "os.h"
#include "types.h"

static u16 g_use_tick;                /* OSi_UseTick */

void OS_InitTick(void)
{
    g_use_tick = 1;
}

int OS_IsTickAvailable(void)
{
    return g_use_tick;
}

u64 OS_GetTick(void)
{
    return eidos_frame_tick(eidos_frame_current());
}

u32 OS_GetTickLo(void)
{
    return (u32)OS_GetTick();
}

void bw_os_tick_reset(void)
{
    g_use_tick = 0;
}

/* Address spellings (sdk/aliases.c says why a host link needs them). */
void func_02086FE8(void) { OS_InitTick(); }
int  func_02087058(void) { return OS_IsTickAvailable(); }
u64  func_020870D0(void) { return OS_GetTick(); }
u32  func_02087170(void) { return OS_GetTickLo(); }
