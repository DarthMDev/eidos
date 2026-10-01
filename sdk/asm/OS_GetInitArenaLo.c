/*
 * sdk/asm/OS_GetInitArenaLo.c -- OS_GetInitArenaLo at 0x02086694.
 *
 * Replaces src/matched/arm9_sdk/OS_GetInitArenaLo.c (an asm function: a
 * jump table on `addls pc, pc, id, lsl #2` with its constants in a literal
 * pool).  void *OS_GetInitArenaLo(OSArenaId id).
 *
 * The pool, decoded case by case (id compared unsigned, above 6 is 0):
 *
 *   0 MAIN         OS_IsRunOnTwl() ? 0x02735B40 : 0x02213D20
 *   1 MAINEX_PRIV  0 (the default branch)
 *   2 MAINEX       0 unless the word at g_02151264 + 4 (the SDK's
 *                  "main-ex arena enabled" flag, 0x02151268) is non-zero;
 *                  then with c = OS_GetConsoleType() & 0xF:
 *                  TWL: c != 4 ? 0x0D000000 : 0;  NITRO: c != 1 ? 0x02400060 : 0
 *   3 ITCM         0x01FF8820
 *   4 DTCM         0x02FE00C0
 *   5 SHARED       0x02FFF000
 *   6 WRAM_MAIN    0x03040000
 *
 * The calls happen in the asm's order (case 2 reads the flag first, then
 * the console type, then OS_IsRunOnTwl).  bw_os_init_arena_lo_addr returns
 * the DS address the asm returns.  OS_GetInitArenaLo returns the host
 * address standing for it (bw_ds_try_ptr): the asm returns a literal
 * address, which the port spells through the address space as it does
 * every literal; an address no region backs (ITCM, the TWL main-ex bank)
 * comes back NULL rather than as a wild pointer.  The caller is OS_InitArena
 * (func_020864FC.c), which hands the value to OS_SetArenaLo; the port's
 * arena (sdk/os_arena.c) serves allocations from its own region whatever
 * these bounds say.
 */

#include <stddef.h>

#include "asmsdk.h"
#include "../dsmem.h"

extern int OS_IsRunOnTwl(void);
extern u32 OS_GetConsoleType(void);

u32 bw_os_init_arena_lo_addr(u32 id)
{
    u32 console;

    switch (id) {
    case 0:
        return OS_IsRunOnTwl() ? 0x02735B40u : 0x02213D20u;
    case 2:
        if (*(const volatile u32 *)bw_ds_ptr(0x02151268u) == 0) {
            return 0;
        }
        console = OS_GetConsoleType() & 0xFu;
        if (OS_IsRunOnTwl()) {
            return console != 4 ? 0x0D000000u : 0u;
        }
        return console != 1 ? 0x02400060u : 0u;
    case 3:
        return 0x01FF8820u;
    case 4:
        return 0x02FE00C0u;
    case 5:
        return 0x02FFF000u;
    case 6:
        return 0x03040000u;
    default:
        return 0;
    }
}

void *OS_GetInitArenaLo(u32 id)
{
    u32 addr = bw_os_init_arena_lo_addr(id);

    return addr != 0 ? bw_ds_try_ptr(addr) : NULL;
}

void *func_02086694(u32 id)
{
    return OS_GetInitArenaLo(id);
}
