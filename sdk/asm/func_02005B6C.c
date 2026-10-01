/*
 * sdk/asm/func_02005B6C.c -- 0x02005B6C, a long-branch veneer into TWL code.
 *
 * Replaces src/matched/arm9/func_02005B6C.c (an asm function):
 *
 *     ldr pc, [pc, #-4]
 *     .word 0x02704364
 *
 * Jumps into the TWL region at 0x02704364 (not loaded on a NITRO console,
 * not decompiled).  Its caller, func_02005B24.c, calls it only when
 * OS_IsRunOnTwl() returns 1.  Reaching it on the port's NITRO boot is a
 * closure hole.
 */

#include "asmsdk.h"
#include "../port.h"

void func_02005B6C(void)
{
    bw_panic("func_02005B6C", 0x02704364u, "TWL-only veneer reached on a NITRO boot");
}
