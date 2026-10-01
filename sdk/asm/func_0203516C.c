/*
 * sdk/asm/func_0203516C.c -- 0x0203516C.
 *
 * Replaces src/matched/arm9_sdk/func_0203516C.c (an asm tail call):
 *
 *     ldr r2, =g_021469BC; mov r1, r0; ldr r0, [r2]; b func_0203515C
 *
 * sub_0203515C(g_021469BC, mask): ORs the mask into byte +0x39 of the
 * object the global points to (matched C; func_0203515C is its alias).
 */

#include "asmsdk.h"

extern void *g_021469BC;
extern void sub_0203515C(void *p, u8 mask);

void func_0203516C(u32 mask)
{
    sub_0203515C(g_021469BC, (u8)mask);
}
