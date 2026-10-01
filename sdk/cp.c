/*
 * sdk/cp.c -- the divider and square-root unit, computed on access.
 *
 * Why a shim: sdk/cp.h.  The SDK's CP_ inlines are register traffic, so the
 * matched code that uses them runs as written (docs/boot.md, layer 1); what
 * the port supplies is the hardware behind the registers, the way
 * sdk/dsmem.c supplies VRAMCNT's bank mapping.
 *
 * The rules, from GBATEK "DS Maths":
 *
 *   DIVCNT bits 0-1 select the operand widths: 0 = 32/32, 1 = 64/32,
 *   2 = 64/64, 3 = as 1.  The operands are signed; a 32-bit operand is the
 *   low word of its 64-bit register, sign-extended.  DIV_RESULT and
 *   DIV_REM are both 64-bit, C's truncating quotient and its remainder.
 *   DIVCNT bit 14 is set when the whole 64-bit DIV_DENOM is zero.  Division
 *   by zero gives REM = NUMER and RESULT = -1 for a non-negative numerator,
 *   +1 for a negative one; in 32/32 mode the upper word of RESULT is then
 *   inverted.  The one quotient that does not fit, INT64_MIN / -1 in the
 *   64-bit modes, gives INT64_MIN with a zero remainder (C leaves it
 *   undefined, so it is spelled out).  In 32/32 mode INT32_MIN / -1 is
 *   +0x80000000 as a 64-bit value, which the 64-bit arithmetic gives as is.
 *
 *   SQRTCNT bit 0 selects a 32-bit (0) or 64-bit (1) unsigned SQRT_PARAM;
 *   SQRT_RESULT is the 32-bit floor of the square root.
 *
 * When.  The access hook runs on every translation of an address in the
 * unit's window, before the matched load or store through it lands.  A
 * translation of DIVCNT or of the result registers recomputes the division
 * from DIV_NUMER, DIV_DENOM and the mode as they stand; SQRTCNT and
 * SQRT_RESULT likewise for the root.  CP_SetDiv32_32's stores therefore
 * land first, CP_WaitDiv's poll of DIVCNT computes, and CP_GetDivResult32
 * computes again from the same operands, so every read sees the answer to
 * the last operands stored.  Busy (bit 15) is never set, so CP_WaitDiv's
 * loop exits at once.  Nothing is done at bw_io_sync: a store the game made
 * to the result registers is left alone until the game next reads one.
 */

#include "cp.h"
#include "dsmem.h"

#define DIV_WINDOW_LO   BW_REG_DIVCNT
#define CP_WINDOW_HI    0x040002C0u

static u64 rd64(u32 addr)
{
    return (u64)bw_io_read32(addr) | ((u64)bw_io_read32(addr + 4u) << 32);
}

static void wr64(u32 addr, u64 v)
{
    bw_io_write32(addr, (u32)v);
    bw_io_write32(addr + 4u, (u32)(v >> 32));
}

static s64 sext32(u64 v)
{
    u32 lo = (u32)v;

    return (lo & 0x80000000u) ? (s64)lo - (s64)0x100000000ll : (s64)lo;
}

static void divide(void)
{
    u16 cnt = bw_io_read16(BW_REG_DIVCNT);
    u32 mode = cnt & 3u;
    u64 rn = rd64(BW_REG_DIV_NUMER);
    u64 rd = rd64(BW_REG_DIV_DENOM);
    s64 n, d, q, r;

    n = mode == 0 ? sext32(rn) : (s64)rn;
    d = mode == 2 ? (s64)rd : sext32(rd);

    if (d == 0) {
        q = n < 0 ? 1 : -1;
        r = n;
        if (mode == 0)
            q = (s64)((u64)q ^ 0xFFFFFFFF00000000ull);
    } else if (d == -1 && n == -(s64)0x7FFFFFFFFFFFFFFFll - 1) {
        q = n;                              /* only reachable in mode 1/2  */
        r = 0;
    } else {
        q = n / d;
        r = n % d;
    }
    wr64(BW_REG_DIV_RESULT, (u64)q);
    wr64(BW_REG_DIV_REM, (u64)r);
    cnt = (u16)(rd == 0 ? (cnt | 0x4000u) : (cnt & ~0x4000u));
    bw_io_write16(BW_REG_DIVCNT, cnt);
}

/* floor(sqrt(v)) for a 64-bit v, bit by bit: exact, no floating point.     */
static u32 isqrt64(u64 v)
{
    u64 root = 0;
    u64 bit = (u64)1 << 62;

    while (bit > v)
        bit >>= 2;
    while (bit != 0) {
        if (v >= root + bit) {
            v -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (u32)root;
}

static void square_root(void)
{
    u64 param = rd64(BW_REG_SQRT_PARAM);

    if ((bw_io_read16(BW_REG_SQRTCNT) & 1u) == 0)
        param &= 0xFFFFFFFFull;
    bw_io_write32(BW_REG_SQRT_RESULT, isqrt64(param));
}

static void access_hook(u32 addr, int sync)
{
    if (sync)
        return;
    if ((addr >= BW_REG_DIVCNT && addr < BW_REG_DIVCNT + 4u) ||
        (addr >= BW_REG_DIV_RESULT && addr < BW_REG_SQRTCNT))
        divide();
    else if (addr >= BW_REG_SQRTCNT && addr < BW_REG_SQRT_PARAM)
        square_root();
}

void bw_cp_update(void)
{
    divide();
    square_root();
}

void bw_cp_reset(void)
{
    bw_io_access_hook(DIV_WINDOW_LO, CP_WINDOW_HI, access_hook);
}
