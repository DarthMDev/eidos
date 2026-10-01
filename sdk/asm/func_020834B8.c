/*
 * sdk/asm/func_020834B8.c -- MI_SwapWord at 0x020834B8.
 *
 * Replaces src/matched/arm9/func_020834B8.c (an asm function):
 *
 *     swp r0, r0, [r1]        atomically: old = *ptr; *ptr = val; r0 = old
 *     bx  lr
 *
 * u32 MI_SwapWord(u32 setData, volatile u32 *destp).  `swp` makes the
 * read and the write one bus transaction.  The port runs one game thread at
 * a time (the OS layer switches threads only at its own calls), so nothing
 * can come between the two, and a plain read then write is the same
 * operation.  The matched file spells it int func_020834B8(int val,
 * int *ptr); SDK code (the spin lock) calls MI_SwapWord.  Both are defined
 * as the same swap, each on its own pointer type, so neither converts an
 * int * to a u32 * (the object behind it keeps its declared type).
 */

#include "asmsdk.h"

u32 MI_SwapWord(u32 setData, volatile u32 *destp)
{
    u32 old = *destp;

    *destp = setData;
    return old;
}

int func_020834B8(int val, int *ptr)
{
    volatile int *p = ptr;
    int old = *p;

    *p = val;
    return old;
}
