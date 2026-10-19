/*
 * sdk/asm/func_0207AE24.c -- 0x0207AE24 MTX_Scale33_.
 *
 * Replaces src/matched/arm9_sdk/func_0207AE24.c (asm): a 3x3 fx32 scale
 * matrix, x, y and z on the diagonal (words 0, 4 and 8) and zero
 * elsewhere.  The asm writes word 0, then word 8, then words 1 to 7 in
 * order; nothing reads the matrix between the stores.
 */

#include "asmsdk.h"

void MTX_Scale33_(s32 *dst, s32 x, s32 y, s32 z)
{
    dst[0] = x;
    dst[1] = 0;
    dst[2] = 0;
    dst[3] = 0;
    dst[4] = y;
    dst[5] = 0;
    dst[6] = 0;
    dst[7] = 0;
    dst[8] = z;
}
