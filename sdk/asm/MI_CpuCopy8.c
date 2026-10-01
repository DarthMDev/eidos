/*
 * sdk/asm/MI_CpuCopy8.c -- MI_CpuCopy8 at 0x02082D44.
 *
 * Replaces src/matched/arm9_sdk/MI_CpuCopy8.c (TwlSDK mi_memory.o, an asm
 * function in the vendor source).  void MI_CpuCopy8(const void *srcp,
 * void *dstp, u32 size).
 *
 * The asm never stores a single byte: VRAM and palette RAM ignore byte
 * writes, so every byte it changes is written inside a halfword (or word)
 * store, with the neighbour byte read and written back unchanged.  On the
 * host that read-modify-write is invisible except through the order of
 * loads and stores, which decides the result of an overlapping copy; this
 * file keeps that order unit by unit and stores only the bytes the asm
 * changes.  The phases:
 *
 *   size 0              return.
 *   dst odd             one byte: ldrh [dst-1] (kept), the source byte
 *                       (ldrh at src or src-1 by parity), strh [dst-1].
 *                       dst is even from here.
 *   (src ^ dst) & 1     src odd: halfword loads from src rounded down, the
 *                       spare byte carried in r3, one halfword store per
 *                       load.  Each load happens before the store of the
 *                       pair below it, and reads one byte ahead of what that
 *                       store needs (ldrh can over-read the last source
 *                       byte, which this file does not do: that byte is
 *                       never stored).  An odd tail stores the carried byte.
 *   (src ^ dst) & 2     both even, not word-compatible: halfword copy of
 *                       size & ~1, then the last byte.
 *   otherwise           one halfword to word-align dst (if size >= 2),
 *                       words for size & ~3, a halfword, the last byte.
 *   last byte           ldrh [dst], ldrh [src], keep dst's high byte, store.
 *
 * The copy is forward in every phase; an overlapping call with dst above src
 * repeats the pattern the units define, exactly as on the DS.  Alignment
 * means the host address's, which matches the DS address's because every
 * host block standing for DS memory is at least word aligned.
 */

#include <stdint.h>

#include "asmsdk.h"

static void copy_unit(const u8 *s, u8 *d, u32 width)
{
    u8 tmp[4];
    u32 i;

    /* One ldr/ldrh: every byte of the unit is read before any is stored. */
    for (i = 0; i < width; i++) {
        tmp[i] = s[i];
    }
    for (i = 0; i < width; i++) {
        d[i] = tmp[i];
    }
}

void MI_CpuCopy8(const void *srcp, void *dstp, u32 size)
{
    const u8 *s = (const u8 *)srcp;
    u8 *d = (u8 *)dstp;
    u32 n = size;
    u32 chunk;

    if (n == 0) {
        return;
    }
    if (((uintptr_t)d & 1u) != 0) {
        d[0] = s[0];
        s++;
        d++;
        if (--n == 0) {
            return;
        }
    }

    if ((((uintptr_t)s ^ (uintptr_t)d) & 1u) != 0) {
        /* bic r0, #1; ldrh: r3 = the byte at s (the odd half). */
        u8 carry = s[0];

        s++;
        while (n >= 2) {
            /* ldrh ip, [r0], #2 reads s[0], s[1]; s[1] is only needed when
             * another pair or the odd tail follows. */
            u8 lo = s[0];
            u8 hi = (n > 2) ? s[1] : 0;

            d[0] = carry;
            d[1] = lo;
            carry = hi;
            s += 2;
            d += 2;
            n -= 2;
        }
        if ((n & 1u) != 0) {
            d[0] = carry;
        }
        return;
    }

    if ((((uintptr_t)s ^ (uintptr_t)d) & 2u) != 0) {
        chunk = n & ~1u;
        n -= chunk;
        for (; chunk != 0; chunk -= 2) {
            copy_unit(s, d, 2);
            s += 2;
            d += 2;
        }
    } else if (n >= 2) {
        if (((uintptr_t)d & 2u) != 0) {
            copy_unit(s, d, 2);
            s += 2;
            d += 2;
            n -= 2;
            if (n == 0) {
                return;
            }
        }
        chunk = n & ~3u;
        n -= chunk;
        for (; chunk != 0; chunk -= 4) {
            copy_unit(s, d, 4);
            s += 4;
            d += 4;
        }
        if ((n & 2u) != 0) {
            copy_unit(s, d, 2);
            s += 2;
            d += 2;
        }
    }

    if ((n & 1u) != 0) {
        d[0] = s[0];
    }
}

void func_02082D44(const void *srcp, void *dstp, u32 size)
{
    MI_CpuCopy8(srcp, dstp, size);
}

void sub_02082D44(const void *srcp, void *dstp, u32 size)
{
    MI_CpuCopy8(srcp, dstp, size);
}
