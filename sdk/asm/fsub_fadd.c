/*
 * sdk/asm/fsub_fadd.c -- 0x0209C95C __fadd and 0x0209CB8C __fsub.
 *
 * Replaces src/matched/arm9/fsub_fadd.c, the MSL's single-precision add and
 * subtract for the ARM (one asm function: __fsub flips the sign of its
 * second operand and branches into __fadd's body).  Both take and return
 * IEEE 754 single-precision values in core registers, which the matched
 * callers spell as their bit patterns (`u32 func_0209CB8C(u32 a, u32 b)` in
 * ov020_func_02187B38.c, `int` in the ov021 callers).
 *
 * The routine rounds to nearest, ties to even, as IEEE 754 does.  Here the
 * operands are widened to double, added, and the sum rounded once to
 * single: a double holds at least 2 * 24 + 2 bits of significand, so that
 * rounding gives the correctly rounded single result for every pair of
 * finite operands.
 */

#include <string.h>

#include "asmsdk.h"

static float bits_to_float(u32 v)
{
    float f;

    memcpy(&f, &v, sizeof f);
    return f;
}

static u32 float_to_bits(float f)
{
    u32 v;

    memcpy(&v, &f, sizeof v);
    return v;
}

u32 __fadd(u32 a, u32 b)
{
    volatile float r = (float)((double)bits_to_float(a) + (double)bits_to_float(b));

    return float_to_bits(r);
}

u32 __fsub(u32 a, u32 b)
{
    volatile float r = (float)((double)bits_to_float(a) - (double)bits_to_float(b));

    return float_to_bits(r);
}
