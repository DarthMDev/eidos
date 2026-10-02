/*
 * sdk/asm/func_0207B598.c -- MTX_Identity43_ at 0x0207B598.
 *
 * Replaces src/matched/arm9_sdk/func_0207B598.c (an asm function, libfx).
 * Writes the 4x3 identity: (1, 0, 0), (0, 1, 0), (0, 0, 1), (0, 0, 0),
 * as the stm/str sequence of the asm does, word for word; fx32 in
 * 20.12, so 1 is 0x1000.  The words are written as u32 so a negation
 * of 0x80000000 wraps as the asm's `neg` does.
 */

#include "asmsdk.h"

#define ONE 0x1000u

void func_0207B598(void *dst);

void func_0207B598(void *dst)
{
    u32 *m = (u32 *)dst;
    u32 i;

    for (i = 0; i < 12u; i++) {
        m[i] = (i == 0u || i == 4u || i == 8u) ? ONE : 0u;
    }
}
