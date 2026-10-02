/*
 * func_020173D0 -- set up the shared record at g_0214652C, then a field of
 * g_02146270.  REIMPLEMENTATION of aletheia's EXACT
 * src/matched/arm9/func_020173D0.c, for an argument it forwards in r0.
 *
 * The matched source is
 *
 *     void func_020173D0(void)
 *     {
 *         func_0201AD88();
 *         *(unsigned short *)(g_02146270 + 2) = 0x28b;
 *     }
 *
 * and func_0201AD88 is sub_0201AD88(u32 heap) (src/matched/arm9/
 * unit_0201AD88.c), which allocates from that heap.  On the DS the caller's
 * r0 reaches it untouched (aletheia's asm/arm9/main.s, 0x020173D0: `push
 * {r3, lr}; bl func_0201AD88`), and the one caller, the boot sequence
 * func_0200523C, passes 0.  A host passes nothing, the allocator saw an
 * indeterminate heap id and failed.  This file names the argument.
 * cmake/boot.cmake lists the matched file as replaced.
 *
 * symbols:
 *   g_02146270      0x02146270  data
 */

#include "stubs.h"

void sub_0201AD88(u32 heap);
void func_020173D0(u32 heap);

extern unsigned char g_02146270[];

void func_020173D0(u32 heap)
{
    sub_0201AD88(heap);
    *(unsigned short *)(g_02146270 + 2) = 0x28b;
}
