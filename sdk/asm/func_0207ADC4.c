/*
 * sdk/asm/func_0207ADC4.c -- MTX_Identity33_ at 0x0207ADC4.
 *
 * Replaces src/matched/arm9_sdk/func_0207ADC4.c (an asm function, libfx).
 * Writes the 3x3 identity,
 * as the stm/str sequence of the asm does, word for word; fx32 in
 * 20.12, so 1 is 0x1000.  The words are written as u32 so a negation
 * of 0x80000000 wraps as the asm's `neg` does.
 */

#include "asmsdk.h"

#define ONE 0x1000u

void func_0207ADC4(void *dst);

void func_0207ADC4(void *dst)
{
    u32 *m = (u32 *)dst;
    u32 i;

    for (i = 0; i < 9u; i++) {
        m[i] = (i % 4u == 0u) ? ONE : 0u;
    }
}
