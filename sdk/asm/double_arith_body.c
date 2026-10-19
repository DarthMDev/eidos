/*
 * sdk/asm/double_arith_body.c -- 0x0209C5E0, the MSL's double square root.
 *
 * Replaces src/matched/arm9/double_arith_body.c.  aletheia's runtime table
 * names the routine _double_arith_body, but the body is a square root: it
 * halves the exponent (`movs ip, ip, asr #1`) and forms the root of the
 * significand one bit per step by shift and subtract, and the overlays that
 * call it name it _dsqrt (`double func_0209C5E0(double x)` in
 * ov021_func_021C73B4.c).  Both spellings are defined here; the result is
 * the correctly rounded square root, as IEEE 754 requires.
 */

#include <math.h>

#include "asmsdk.h"

double _double_arith_body(double x)
{
    volatile double r = sqrt(x);

    return r;
}

double _dsqrt(double x)
{
    return _double_arith_body(x);
}
