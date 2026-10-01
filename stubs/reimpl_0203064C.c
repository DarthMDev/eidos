/*
 * sub_0203064C -- create a child heap or stop.  REIMPLEMENTATION of
 * aletheia's EXACT src/matched/arm9/sub_0203064C.c, for one reason: the
 * matched C forwards a third argument through a register.
 *
 * The matched source is
 *
 *     void sub_0203064C(void *a, u32 b)
 *     {
 *         if (sub_0202FDC4(a, b) != 0) return;
 *         sub_020306A0(sub_02030524(), a, b);
 *     }
 *
 * and every caller passes three arguments (sub_0200545C.c: `(1, 7,
 * 0x25000)`, parent id, child id, size; func_02010BB8.c, func_02011D60.c,
 * func_02011D9C.c likewise), while sub_0202FDC4 (src/matched/arm9/
 * sub_0202FDC4.c) takes three: (parent, child, size).  On the DS the size
 * arrives in r2 and nothing between the entry and the call to 0x0202FDC4
 * writes r2, so the size reaches the child-heap constructor untouched.  A
 * host compiler passes what the C says: two arguments, and the callee reads
 * an indeterminate third.  So this file says the third argument out loud;
 * everything else is the matched C: the same guard, the same failure arm
 * (the manager's error code to 0x020306A0, which stops the console for any
 * code but 0).  cmake/boot.cmake lists the matched file as replaced.
 */

#include "stubs.h"

int  sub_0202FDC4(u32 src, u32 dst, u32 param);
u16  sub_02030524(void);
void func_020306A0(int n);
void sub_0203064C(u32 parent, u32 child, u32 size);

void sub_0203064C(u32 parent, u32 child, u32 size)
{
    if (sub_0202FDC4(parent, child, size) != 0) {
        return;
    }
    func_020306A0((int)sub_02030524());
}
