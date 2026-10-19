/*
 * sdk/asm/fdiv.c -- 0x0209CE10 __fdiv.
 *
 * Replaces src/matched/arm9/fdiv.c, the MSL's single-precision divide for
 * the ARM (asm).  It takes and returns IEEE 754 single-precision values in
 * core registers, which the matched callers spell as their bit patterns
 * (`u32 func_0209CE10(u32 a, u32 b)` in ov020_func_02187B38.c).
 *
 * The routine rounds to nearest, ties to even.  Here the quotient is formed
 * in double and rounded once to single: a format with at least 2 * 24 + 2
 * significand bits makes that the correctly rounded single quotient.
 */

#include <string.h>

#include "asmsdk.h"

u32 __fdiv(u32 a, u32 b)
{
    float fa, fb;
    volatile float r;
    u32 v;

    memcpy(&fa, &a, sizeof fa);
    memcpy(&fb, &b, sizeof fb);
    r = (float)((double)fa / (double)fb);
    memcpy(&v, (const void *)&r, sizeof v);
    return v;
}
