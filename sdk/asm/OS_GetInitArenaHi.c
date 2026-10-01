/*
 * sdk/asm/OS_GetInitArenaHi.c -- OS_GetInitArenaHi at 0x02086594.
 *
 * Replaces src/matched/arm9_sdk/OS_GetInitArenaHi.c (an asm function: a
 * jump table on `addls pc, pc, id, lsl #2` with its constants in a literal
 * pool).  void *OS_GetInitArenaHi(OSArenaId id).
 *
 * The pool, decoded case by case (id compared unsigned, above 6 is 0):
 *
 *   0 MAIN         OS_IsRunOnTwl() ? 0x02F80000 : 0x023E0000
 *   1 MAINEX_PRIV  0 (the default branch)
 *   2 MAINEX       as OS_GetInitArenaLo.c, with 0x0DF00000 (TWL) and
 *                  0x02700000 (NITRO)
 *   3 ITCM         0x02000000
 *   4 DTCM         the SDK's system-stack floor, from the pool words
 *                  HW_DTCM 0x02FE0000, SDK_SYS_STACKSIZE 0x1800 (signed),
 *                  SDK_IRQ_STACKSIZE 0x800 and the arena start 0x02FE00C0:
 *                    irqLo = HW_DTCM + 0x3F80 - IRQ size
 *                    sys == 0:  max(HW_DTCM, arena start) (unsigned)
 *                    sys <  0:  arena start - sys
 *                    sys >  0:  irqLo - sys            = 0x02FE1F80
 *   5 SHARED       0x02FFF680
 *   6 WRAM_MAIN    0x03040000
 *
 * The return value goes through the address space as OS_GetInitArenaLo.c
 * explains (NULL where no region backs the address).
 */

#include <stddef.h>

#include "asmsdk.h"
#include "../dsmem.h"

extern int OS_IsRunOnTwl(void);
extern u32 OS_GetConsoleType(void);

static u32 bw_dtcm_arena_hi(void)
{
    const u32 dtcm = 0x02FE0000u;
    const s32 sys_stack = 0x1800;
    const u32 irq_stack = 0x800u;
    const u32 arena_start = 0x02FE00C0u;
    u32 irq_lo = dtcm + 0x3F80u - irq_stack;

    if (sys_stack == 0) {
        return dtcm < arena_start ? arena_start : dtcm;
    }
    if (sys_stack < 0) {
        return arena_start - (u32)sys_stack;
    }
    return irq_lo - (u32)sys_stack;
}

u32 bw_os_init_arena_hi_addr(u32 id)
{
    u32 console;

    switch (id) {
    case 0:
        return OS_IsRunOnTwl() ? 0x02F80000u : 0x023E0000u;
    case 2:
        if (*(const volatile u32 *)bw_ds_ptr(0x02151268u) == 0) {
            return 0;
        }
        console = OS_GetConsoleType() & 0xFu;
        if (OS_IsRunOnTwl()) {
            return console != 4 ? 0x0DF00000u : 0u;
        }
        return console != 1 ? 0x02700000u : 0u;
    case 3:
        return 0x02000000u;
    case 4:
        return bw_dtcm_arena_hi();
    case 5:
        return 0x02FFF680u;
    case 6:
        return 0x03040000u;
    default:
        return 0;
    }
}

void *OS_GetInitArenaHi(u32 id)
{
    u32 addr = bw_os_init_arena_hi_addr(id);

    return addr != 0 ? bw_ds_try_ptr(addr) : NULL;
}

void *func_02086594(u32 id)
{
    return OS_GetInitArenaHi(id);
}
