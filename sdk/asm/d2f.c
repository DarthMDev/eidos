/*
 * sdk/asm/d2f.c -- 0x0209B64C _d2f.
 *
 * Replaces src/matched/arm9/d2f.c, the MSL's double to single conversion
 * for the ARM (asm).  The one caller that names it,
 * ov021_func_021C73B4.c, declares `float func_0209B64C(double d)`, so the
 * host types are float and double; the conversion rounds to nearest, ties
 * to even, as the routine does.
 */

#include "asmsdk.h"

float _d2f(double d)
{
    volatile float r = (float)d;

    return r;
}
