/*
 * sdk/asm/func_0207AD58.c -- MTX_Scale22_ at 0x0207AD58.
 *
 * Replaces src/matched/arm9_sdk/func_0207AD58.c (an asm function, libfx).
 * Writes the 2x2 scale: (x, 0), (0, y),
 * as the stm/str sequence of the asm does, word for word; fx32 in
 * 20.12, so 1 is 0x1000.  The words are written as u32 so a negation
 * of 0x80000000 wraps as the asm's `neg` does.
 */

#include "asmsdk.h"

#define ONE 0x1000u

void func_0207AD58(void *dst, fx32 x, fx32 y);

void func_0207AD58(void *dst, fx32 x, fx32 y)
{
    u32 *m = (u32 *)dst;

    m[0] = (u32)x;
    m[1] = 0;
    m[2] = 0;
    m[3] = (u32)y;
}
