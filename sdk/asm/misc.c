/*
 * sdk/asm/misc.c -- 0x0209C054 _ll_mul and 0x0209C078 _ll_shl.
 *
 * Replaces src/matched/arm9/misc.c, two helpers of the ARM runtime library
 * (mwcc `asm` bodies):
 *
 *   _ll_mul  64 x 64 -> 64 multiply: umull of the low words, then the two
 *            cross products added into the high word (mla, mla).  The
 *            result is the low 64 bits of the product, the same for signed
 *            and unsigned operands; ov231's CARD_SPI_RunCommand calls it as
 *            `u64 _ull_mul(u64, u64)`.
 *   _ll_shl  64-bit shift left by r2 & 0x3F: a count of 32 or more moves
 *            the low word into the high word, shifted by the rest.
 *
 * A 64-bit argument in r0:r1 (low, high) is a u64 here; the host passes
 * and returns it whole.
 */

#include "asmsdk.h"

u64 _ll_mul(u64 a, u64 b)
{
    return a * b;
}

u64 _ll_shl(u64 a, u32 n)
{
    n &= 0x3F;
    return a << n;
}
