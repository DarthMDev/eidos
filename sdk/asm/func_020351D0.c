/*
 * sdk/asm/func_020351D0.c -- 0x020351D0.
 *
 * Replaces src/matched/arm9_sdk/func_020351D0.c (an asm tail call):
 *
 *     ldr r2, =g_021469BC; mov r1, r0; ldr r0, [r2]; b func_020351BC
 *
 * sub_020351BC(g_021469BC, mask) and its result: whether any of the mask's
 * bits are set in byte +0x39 of the object (matched C; func_020351BC is
 * its alias).
 */

#include "asmsdk.h"

extern void *g_021469BC;
extern u32 sub_020351BC(void *p, u32 mask);

u32 func_020351D0(u32 mask)
{
    return sub_020351BC(g_021469BC, mask);
}
