/*
 * sdk/asm/MTX_Rot22_.c -- MTX_Rot22_ at 0x0207AD64.
 *
 * Replaces src/matched/arm9_sdk/MTX_Rot22_.c (a Thumb asm function).
 * void MTX_Rot22_(MtxFx22 *dst, fx32 sinVal, fx32 cosVal):
 *
 *     str r2, [r0]        ; _00 = cos
 *     str r1, [r0, #4]    ; _01 = sin
 *     neg r1, r1
 *     str r1, [r0, #8]    ; _10 = -sin
 *     str r2, [r0, #0xc]  ; _11 = cos
 *
 * `neg` wraps: -0x80000000 is 0x80000000, done here in unsigned arithmetic
 * so the host has no signed overflow.  The callers pass their own 2x2
 * struct of four fx32 (struct Mtx22); the layout is the same.
 */

#include "asmsdk.h"

void MTX_Rot22_(BwAsmMtxFx22 *dst, fx32 sinVal, fx32 cosVal)
{
    dst->_00 = cosVal;
    dst->_01 = sinVal;
    dst->_10 = (fx32)(0u - (u32)sinVal);
    dst->_11 = cosVal;
}
