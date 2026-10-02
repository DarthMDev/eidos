/*
 * sdk/asm/MTX_Scale43_.c -- MTX_Scale43_ at 0x0207B5F4.
 *
 * Replaces src/matched/arm9_sdk/MTX_Scale43_.c (an asm function, libfx).
 * Writes the 4x3 scale: diagonal (x, y, z), every other word 0,
 * as the stm/str sequence of the asm does, word for word; fx32 in
 * 20.12, so 1 is 0x1000.  The words are written as u32 so a negation
 * of 0x80000000 wraps as the asm's `neg` does.
 */

#include "asmsdk.h"

#define ONE 0x1000u

void MTX_Scale43_(void *dst, fx32 x, fx32 y, fx32 z);

void MTX_Scale43_(void *dst, fx32 x, fx32 y, fx32 z)
{
    u32 *m = (u32 *)dst;
    u32 i;

    for (i = 0; i < 12u; i++) {
        m[i] = 0;
    }
    m[0] = (u32)x;
    m[4] = (u32)y;
    m[8] = (u32)z;
}
