/*
 * sdk/asm/ArrayDeleteAt.c -- 0x02005D90 (layout name ArrayDeleteAt).
 *
 * Replaces src/matched/arm9_sdk/ArrayDeleteAt.c (a Thumb asm function):
 *
 *     push {r3, r4, r5, lr}
 *     r5 = r0; r4 = r1
 *     bl func_02005F90          ; (r0, r1) as received
 *     r0 = r5; r1 = r4
 *     bl func_02005D34          ; (r0, r1) again
 *     pop {r3, r4, r5, pc}      ; r0 = func_02005D34's result
 *
 * Two calls with the same two arguments, returning the second's result.
 * The name is the symbol table's (a DWC darray name with no relation to
 * this game function); its caller (unit_02005F38.c) uses sub_02005D90.
 * func_02005F90 is not matched yet (closure.py lists it open).
 */

#include "asmsdk.h"

extern void  func_02005F90(u32 a, u32 b);
extern void *func_02005D34(u32 a0, u32 a1);

void *ArrayDeleteAt(u32 a, u32 b)
{
    func_02005F90(a, b);
    return func_02005D34(a, b);
}

void *sub_02005D90(u32 a, u32 b)
{
    return ArrayDeleteAt(a, b);
}
