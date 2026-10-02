/*
 * sdk/asm/MTX_RotX33_.c -- MTX_RotX33_ at 0x0207AF00.
 *
 * Replaces src/matched/arm9_sdk/MTX_RotX33_.c (an asm function, libfx).
 * Writes the 3x3 rotation about X: rows (1, 0, 0), (0, cos, sin), (0, -sin, cos),
 * as the stm/str sequence of the asm does, word for word; fx32 in
 * 20.12, so 1 is 0x1000.  The words are written as u32 so a negation
 * of 0x80000000 wraps as the asm's `neg` does.
 */

#include "asmsdk.h"

#define ONE 0x1000u

void MTX_RotX33_(void *dst, fx32 sinVal, fx32 cosVal);

void MTX_RotX33_(void *dst, fx32 sinVal, fx32 cosVal)
{
    u32 *m = (u32 *)dst;

    m[0] = ONE;
    m[1] = 0;
    m[2] = 0;
    m[3] = 0;
    m[4] = (u32)cosVal;
    m[5] = (u32)sinVal;
    m[6] = 0;
    m[7] = 0u - (u32)sinVal;
    m[8] = (u32)cosVal;
}
