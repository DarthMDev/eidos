/*
 * sdk/asm/MIi_CpuCopy32.c -- MIi_CpuCopy32 at 0x02082A90.
 *
 * Replaces src/matched/arm9_sdk/MIi_CpuCopy32.c (an asm function).
 * void MIi_CpuCopy32(const void *srcp, void *destp, u32 size):
 *
 *       add   ip, r1, r2      ; end = dest + size
 *   1:  cmp   r1, ip          ; signed
 *       ldmltia r0!, {r2}
 *       stmltia r1!, {r2}
 *       blt   1b
 *
 * A forward word copy while the destination is below its end: a size that
 * is not a multiple of 4 is rounded up, a size with bit 31 set copies
 * nothing (the signed compare, see MIi_CpuClear32.c), and an overlapping
 * call sees each word loaded before it is stored.
 */

#include "asmsdk.h"

void MIi_CpuCopy32(const void *srcp, void *destp, u32 size)
{
    const u8 *s = (const u8 *)srcp;
    u8 *d = (u8 *)destp;
    u32 off;

    for (off = 0; (s32)off < (s32)size; off += 4) {
        u32 v = bw_rd32(s + off);

        bw_wr32(d + off, v);
    }
}
