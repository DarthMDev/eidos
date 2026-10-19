/*
 * sdk/asm/converters.c -- the MSL runtime's nine integer/float/double
 * converters.
 *
 * Replaces src/matched/arm9/converters.c (asm bodies):
 *
 *   0x0209B750 _s32_to_double   0x0209BDBC _s32_to_float   0x0209C494 _f2s
 *   0x0209B790 _u32_to_double   0x0209BE04 _u32_to_float   0x0209C4C8 _f2u
 *   0x0209BD38 _f2d                                        0x0209C508 _d2s
 *                                                          0x0209C554 _d2u
 *
 * Each body is the asm's integer arithmetic on the IEEE 754 bit patterns,
 * so the results (rounding, saturation, NaN and infinity cases) are the
 * DS's to the bit.  A float travels as its bits in a u32 and a double as
 * its bits in a u64 (low word r0, high word r1 on the DS).
 *
 * The callers do not agree on the C types.  On the 32-bit x86 host an
 * argument of float or double has the same stack layout as a u32 or u64,
 * so the parameters work for every caller; a return value does not (eax or
 * eax:edx for integers, the x87 stack for float and double).  The return
 * types follow the callers the matched C has:
 *
 *   _s32_to_float, _u32_to_float   u32 bits (ov020, ov021 spell them
 *                                  func_0209BDBC/func_0209BE04 returning int)
 *   _f2d                           u64 bits (nan() takes the low word as int)
 *   _s32_to_double, _u32_to_double double (func_0202B134 and friends)
 *   _d2u                           u64: the bias 0x43E (1023 + 63) makes it a
 *                                  double to unsigned 64-bit conversion
 *
 * A float or double result elsewhere in game C is computed by the host
 * compiler and never calls these.
 */

#include <string.h>

#include "asmsdk.h"

typedef unsigned long long bw_u64;

static unsigned clz32(u32 x)
{
    unsigned n = 0;

    if (x == 0)
        return 32;
    while (!(x & 0x80000000u)) {
        x <<= 1;
        n++;
    }
    return n;
}

/* ARM register-specified LSR: an amount of 32 or more gives 0. */
static u32 lsr32(u32 x, u32 s)
{
    return s >= 32 ? 0 : x >> s;
}

/* Round the 8 bits the float mantissa drops: to nearest, ties to even. */
static u32 round_float(u32 r, u32 low)
{
    if (low == 0 || !(low & 0x80))
        return r;
    if ((low & 0x7f) != 0 || (r & 1))
        r++;
    return r;
}

u32 _s32_to_float(u32 x)
{
    u32 sign = x & 0x80000000u;
    unsigned n;
    u32 m;

    if (sign)
        x = 0u - x;
    if (x == 0)
        return x;
    n = clz32(x);
    m = x << n;
    return round_float(sign | ((m << 1) >> 9) | ((0x9eu - n) << 23), m & 0xff);
}

u32 _u32_to_float(u32 x)
{
    u32 e = 0x9e;
    u32 m = x;

    if (x == 0)
        return 0;
    if (!(x & 0x80000000u)) {
        unsigned n = clz32(x);
        m = x << n;
        e -= n;
    }
    return round_float(((m << 1) >> 9) | (e << 23), m & 0xff);
}

s32 _f2s(u32 x)
{
    s32 s = 0x9e - (s32)((x & 0x7fffffffu) >> 23);
    u32 r;

    if (s <= 0)
        return (s32)(~(u32)((s32)x >> 31) + 0x80000000u);
    r = lsr32((x << 8) | 0x80000000u, (u32)s);
    return (s32)x < 0 ? -(s32)r : (s32)r;
}

u32 _f2u(u32 x)
{
    s32 s;

    if (x & 0x80000000u)
        return 0xff000000u >= (x << 1) ? 0 : 0xffffffffu;
    s = 0x9e - (s32)(x >> 23);
    if (s < 0)
        return 0xffffffffu;
    return lsr32((x << 8) | 0x80000000u, (u32)s);
}

s32 _d2s(bw_u64 d)
{
    u32 lo = (u32)d, hi = (u32)(d >> 32);
    s32 s = 0x41e - (s32)((hi & 0x7fffffffu) >> 20);
    u32 r;

    if (s <= 0)
        return (s32)(~(u32)((s32)hi >> 31) + 0x80000000u);
    if (s >= 32)
        return 0;
    r = ((hi << 11) | 0x80000000u | (lo >> 21)) >> s;
    return (s32)hi < 0 ? -(s32)r : (s32)r;
}

bw_u64 _d2u(bw_u64 d)
{
    u32 lo = (u32)d, hi = (u32)(d >> 32);
    s32 s;
    bw_u64 m;

    if (hi & 0x80000000u) {
        /* cmn hi, #0x100000; cmpeq lo, #0; bhi: a NaN gives all ones,
         * every other negative value 0. */
        if (hi > 0xfff00000u || (hi == 0xfff00000u && lo != 0))
            return ~(bw_u64)0;
        return 0;
    }
    s = 0x43e - (s32)(hi >> 20);
    if (s < 0)
        return ~(bw_u64)0;
    if (s >= 64)
        return 0;
    m = ((bw_u64)((hi << 11) | 0x80000000u | (lo >> 21)) << 32) | (bw_u64)(lo << 11);
    return m >> s;
}

static double double_of(u32 lo, u32 hi)
{
    bw_u64 v = ((bw_u64)hi << 32) | lo;
    double f;

    memcpy(&f, &v, sizeof f);
    return f;
}

double _s32_to_double(s32 v)
{
    u32 x = (u32)v;
    u32 sign = x & 0x80000000u;
    unsigned n;
    u32 m;

    if (sign)
        x = 0u - x;
    if (x == 0)
        return double_of(0, 0);
    n = clz32(x);
    m = x << n;
    return double_of(m << 21, sign | ((m << 1) >> 12) | ((0x41eu - n) << 20));
}

double _u32_to_double(u32 x)
{
    u32 e = 0x41e;
    u32 m = x;

    if (x == 0)
        return double_of(0, 0);
    if (!(x & 0x80000000u)) {
        unsigned n = clz32(x);
        m = x << n;
        e -= n;
    }
    return double_of(m << 21, ((m << 1) >> 12) | (e << 20));
}

bw_u64 _f2d(u32 x)
{
    u32 sign = x & 0x80000000u;
    u32 e = (x >> 23) & 0xff;
    u32 m = x << 9;

    if (e == 0) {
        unsigned n;

        if (m == 0)
            return (bw_u64)sign << 32;
        m >>= 1;
        n = clz32(m);
        m <<= n;
        e = 1u - n;
        m += m;
    } else if (e == 0xff) {
        if (m != 0)
            return 0x7fffffffffffffffull;
        return (bw_u64)(0x7ff00000u | sign) << 32;
    }
    e += 0x380;
    return ((bw_u64)(sign | (m >> 12) | (e << 20)) << 32) | (bw_u64)(m << 20);
}
