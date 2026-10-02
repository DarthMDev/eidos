/*
 * sdk/asm/func_02087EA0.c -- 0x02087EA0.
 *
 * Replaces src/matched/arm9/func_02087EA0.c (an asm function):
 *
 *     clz r0, r0
 *     bx  lr
 *
 * The count of leading zero bits of a word, 32 for zero (ARM's CLZ), as
 * the VRAM exclusion bookkeeping (OSi_UnlockVram) uses it to find the
 * highest bank bit.  Portable C, no compiler builtin.
 */

#include "asmsdk.h"

int func_02087EA0(unsigned int x);

int func_02087EA0(unsigned int x)
{
    int n = 0;

    if (x == 0u) {
        return 32;
    }
    while ((x & 0x80000000u) == 0u) {
        x <<= 1;
        n++;
    }
    return n;
}
