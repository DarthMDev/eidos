/*
 * sdk/asm/MI_CpuCopy.c -- 0x02082EA4 MI_CpuCopy.
 *
 * Replaces src/matched/arm9_sdk/MI_CpuCopy.c (an mwcc `asm` body):
 * void MI_CpuCopy(const void *src, void *dst, u32 size).  Up to 8 bytes it
 * copies byte by byte through a computed jump; above that it aligns the
 * source with one or two byte copies, then copies by words with shifts for
 * each destination alignment, and finishes the tail in bytes.  Every path
 * copies forward, one source byte to one destination byte in order, so for
 * memory that does not overlap the result is a plain forward copy.
 */

#include "asmsdk.h"

void MI_CpuCopy(const void *src, void *dst, u32 size)
{
    const u8 *s = (const u8 *)src;
    u8 *d = (u8 *)dst;

    while (size-- > 0) {
        *d++ = *s++;
    }
}
