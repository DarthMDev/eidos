/*
 * sdk/asm/s32_div_f.c -- the mwccarm runtime's 32-bit division.
 *
 * Replaces src/matched/arm9/s32_div_f.c, the unit holding _s32_div_f
 * (0x0209C0A4), _div_by_zero_check (0x0209C2B0, the entry the game calls
 * _u32_div_f) and _u32_div_not_0_f (0x0209C2B8, a label inside it).
 *
 * Code the host compiles never calls these: a host `/` is the host's own.
 * They exist for the matched files that call the helper by name
 * (`extern int _s32_div_f(int, int)`, `func_0209C0A4(...)`,
 * `_u32_div_f(n, d)`), which mwcc's division emits as a call.
 *
 * The asm, in the order the result depends on it:
 *
 *   _s32_div_f        ip = (a ^ b) & 0x80000000, plus 1 if a < 0; a and b
 *                     made non-negative with rsblt (so 0x80000000 stays
 *                     0x80000000 and is divided as the unsigned value).
 *                     b == 0: quotient = |a|, remainder 0, then the signs
 *                     below, so a / 0 returns a and remainder 0.
 *                     |a| < |b| (unsigned): quotient 0, remainder |a|.
 *                     Otherwise the unsigned kernel.  Finally the quotient
 *                     is negated if the operand signs differed and the
 *                     remainder if a was negative (wrapping, so
 *                     0x80000000 / -1 is 0x80000000).
 *   _div_by_zero_check  b == 0: return with r0 = a, r1 = 0.
 *   _u32_div_not_0_f  a < b: quotient 0, remainder a.  Otherwise the kernel.
 *
 * The kernel: r2 = 28 minus 16, 8 and 4 as the signed compares `cmp b,
 * (a >> 4) >> 12` (and so on) allow, which skips quotient bits known to be
 * zero; a is shifted left by r2, r1 = -b, `adds r0, r0, r0` puts a's top
 * bit in C, and a computed jump runs blocks r2..31 of 32 identical
 *
 *     adcs  r3, r1, r3, lsl #1   ; r3 = 2*r3 + C - b, C = no borrow
 *     sublo r3, r3, r1           ; restore if it went negative
 *     adcs  r0, r0, r0           ; shift the quotient bit in
 *
 * leaving the quotient in r0 and the remainder in r3 (moved to r1).
 *
 * For b != 0 that is exact unsigned division, which C computes directly.
 * The one input C cannot reproduce is _u32_div_not_0_f called with b == 0,
 * where the kernel runs on a zero divisor; that case runs the kernel itself
 * (bw_rt_div_kernel below), instruction for instruction.  tests/test_asmsdk.c
 * checks the kernel against C division and both entries on edge inputs.
 */

#include "asmsdk.h"

/*
 * The unrolled restoring-division kernel exactly as the asm runs it, on an
 * unsigned dividend `n` and divisor `d` (with n >= d on every real path).
 * Exported for the test, which checks it against C division.
 */
u32 bw_rt_div_kernel(u32 n, u32 d, u32 *rem)
{
    u32 r0 = n;
    u32 r1 = d;
    u32 r2 = 28;
    u32 r3 = r0 >> 4;
    u32 c;
    u32 blk;

    if ((s32)r1 <= (s32)(r3 >> 12)) {   /* cmp r1, r3, lsr #12; suble/movle */
        r2 -= 16;
        r3 >>= 16;
    }
    if ((s32)r1 <= (s32)(r3 >> 4)) {    /* cmp r1, r3, lsr #4 */
        r2 -= 8;
        r3 >>= 8;
    }
    if ((s32)r1 <= (s32)r3) {           /* cmp r1, r3 */
        r2 -= 4;
        r3 >>= 4;
    }
    r0 <<= r2;
    r1 = 0u - r1;                       /* rsb r1, r1, #0 */
    c = r0 >> 31;                       /* adds r0, r0, r0 */
    r0 <<= 1;
    for (blk = r2; blk < 32; blk++) {
        u32 sh = r3 << 1;
        u32 sum = r1 + sh;
        u32 carry = (sum < r1) ? 1u : 0u;
        u32 sum2 = sum + c;

        if (sum2 < sum) {
            carry = 1;
        }
        r3 = sum2;                      /* adcs r3, r1, r3, lsl #1 */
        c = carry;
        if (c == 0) {
            r3 -= r1;                   /* sublo r3, r3, r1 */
        }
        carry = r0 >> 31;               /* adcs r0, r0, r0 */
        r0 = (r0 << 1) + c;
        c = carry;
    }
    *rem = r3;
    return r0;
}

u32 bw_rt_u32_divmod_not_0(u32 a, u32 b, u32 *rem)
{
    if (a < b) {
        *rem = a;
        return 0;
    }
    if (b == 0) {
        return bw_rt_div_kernel(a, b, rem);
    }
    *rem = a % b;
    return a / b;
}

u32 bw_rt_u32_divmod(u32 a, u32 b, u32 *rem)
{
    if (b == 0) {
        *rem = 0;
        return a;
    }
    return bw_rt_u32_divmod_not_0(a, b, rem);
}

s32 bw_rt_s32_divmod(s32 a, s32 b, s32 *rem)
{
    u32 sign = ((u32)a ^ (u32)b) & 0x80000000u;
    u32 n = (u32)a;
    u32 d = (u32)b;
    u32 q;
    u32 r;

    if (a < 0) {
        n = 0u - n;
        sign += 1;
    }
    if (b < 0) {
        d = 0u - d;
    }
    if (d == 0) {
        q = n;
        r = 0;
    } else if (n < d) {
        q = 0;
        r = n;
    } else {
        q = n / d;
        r = n % d;
    }
    if ((sign & 0x80000000u) != 0) {
        q = 0u - q;
    }
    if ((sign & 1u) != 0) {
        r = 0u - r;
    }
    *rem = (s32)r;
    return (s32)q;
}

s32 _s32_div_f(s32 a, s32 b)
{
    s32 r;

    return bw_rt_s32_divmod(a, b, &r);
}

s32 func_0209C0A4(s32 a, s32 b)
{
    return _s32_div_f(a, b);
}

u32 _div_by_zero_check(u32 a, u32 b)
{
    u32 r;

    return bw_rt_u32_divmod(a, b, &r);
}

u32 _u32_div_f(u32 a, u32 b)
{
    return _div_by_zero_check(a, b);
}

u32 _u32_div_not_0_f(u32 a, u32 b)
{
    u32 r;

    return bw_rt_u32_divmod_not_0(a, b, &r);
}
