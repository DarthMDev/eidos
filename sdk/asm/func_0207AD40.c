/*
 * sdk/asm/func_0207AD40.c -- 0x0207AD40 MTX_Identity22_.
 *
 * Replaces src/matched/arm9_sdk/func_0207AD40.c (an mwcc `asm` body):
 * void MTX_Identity22_(MtxFx22 *m).  Stores 0x1000, 0, 0, 0x1000: the 2x2
 * identity in fx32.
 */

#include "asmsdk.h"

void MTX_Identity22_(void *m)
{
    s32 *p = (s32 *)m;

    p[0] = 0x1000;
    p[1] = 0;
    p[2] = 0;
    p[3] = 0x1000;
}
