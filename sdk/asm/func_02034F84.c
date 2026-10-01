/*
 * sdk/asm/func_02034F84.c -- 0x02034F84.
 *
 * Replaces src/matched/arm9_sdk/func_02034F84.c (an asm tail call):
 *
 *     ldr r2, =0x021469BC; mov r1, r0; ldr r0, [r2]; b func_02034F74
 *
 * func_02034F74(*(void **)0x021469BC, mask).  The asm names the global by
 * its literal address where its siblings (func_0203516C.c, func_020351D0.c)
 * use the symbol; both reach one object, and the host uses the symbol for
 * all three (the global holds a pointer, so its width is the host's).
 */

#include "asmsdk.h"

extern void *g_021469BC;

void func_02034F84(u32 mask)
{
    func_02034F74(g_021469BC, mask);
}
