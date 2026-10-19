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

/*
 * Within one frame the counter would stand still, and a busy wait on it
 * would never end: ov231's CARD_SPI_RunCommand spins until 60 microseconds
 * (about 31 ticks) have passed between two SPI commands.  So each read in
 * the same frame moves on by one tick past the frame's count, never as far
 * as the next frame's.  The count stays deterministic (it depends on the
 * reads the game makes, not on the host clock), and the first read of a
 * frame still returns the frame's own count.
 */
/* The whole ticks one frame advances (engine/frame/frame.h: 8752 and a
 * remainder). */
#define EIDOS_TICKS_PER_FRAME_MIN 8752u

static u64 g_tick_base;               /* the frame count the reads refer to */
static u32 g_tick_sub;                /* reads past it in this frame        */

u64 OS_GetTick(void)
{
    u64 base = eidos_frame_tick(eidos_frame_current());

    if (base != g_tick_base) {
        g_tick_base = base;
        g_tick_sub = 0;
    } else if (base != 0 && g_tick_sub < EIDOS_TICKS_PER_FRAME_MIN - 1) {
        g_tick_sub++;
    }
    return base + g_tick_sub;
}

u32 OS_GetTickLo(void)
{
    return (u32)OS_GetTick();
}

void bw_os_tick_reset(void)
{
    g_use_tick = 0;
    g_tick_base = 0;
    g_tick_sub = 0;
}

/* Address spellings (sdk/aliases.c says why a host link needs them). */
void func_02086FE8(void) { OS_InitTick(); }
int  func_02087058(void) { return OS_IsTickAvailable(); }
u64  func_020870D0(void) { return OS_GetTick(); }
u32  func_02087170(void) { return OS_GetTickLo(); }
