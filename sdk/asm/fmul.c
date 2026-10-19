/*
 * sdk/asm/fmul.c -- 0x0209C77C __fmul.
 *
 * Replaces src/matched/arm9/fmul.c, the MSL's single-precision multiply for
 * the ARM (asm).  It takes and returns IEEE 754 single-precision values in
 * core registers, which the matched callers spell as their bit patterns
 * (`u32 func_0209C77C(u32 a, u32 b)` in ov020_func_02187B38.c).
 *
 * The routine rounds to nearest, ties to even.  Here the operands are
 * widened to double: the product of two 24-bit significands fits in a
 * double's 53 bits exactly, so the one rounding to single gives the
 * correctly rounded result.
 */

#include <string.h>

#include "asmsdk.h"

u32 __fmul(u32 a, u32 b)
{
    float fa, fb;
    volatile float r;
    u32 v;

    memcpy(&fa, &a, sizeof fa);
    memcpy(&fb, &b, sizeof fb);
    r = (float)((double)fa * (double)fb);
    memcpy(&v, (const void *)&r, sizeof v);
    return v;
}
