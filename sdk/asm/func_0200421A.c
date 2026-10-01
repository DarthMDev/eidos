/*
 * sdk/asm/func_0200421A.c -- SVC_CpuSet at 0x0200421A (Thumb).
 *
 * Replaces src/matched/arm9/func_0200421A.c (an asm function):
 *
 *     swi 0xb                 BIOS CpuSet(r0 = src, r1 = dst, r2 = control)
 *     bx  lr
 *
 * void func_0200421A(const void *srcp, void *destp, u32 dmaCntData).  The
 * BIOS call, as the NDS ARM9 BIOS defines it:
 *
 *   bits 0-20  number of units to transfer
 *   bit 24     fill: the unit at src is read once and stored to every
 *              destination unit (otherwise src advances with dst)
 *   bit 26     units are 32-bit words (otherwise 16-bit halfwords)
 *
 * Units go one at a time, low address first, which is the BIOS's order
 * when the ranges overlap.  The BIOS also forces both addresses down to the
 * unit's alignment; host pointers into the address space keep the DS
 * alignment, and every matched caller passes aligned addresses, so the host
 * leaves them as they are.  Each unit is moved with memcpy, so host
 * alignment and aliasing rules hold for any object.  The one matched caller
 * (func_02090F18.c) passes a word to fill 1 word: 0x05000001.
 */

#include <string.h>

#include "asmsdk.h"

void func_0200421A(const void *srcp, void *destp, u32 dmaCntData)
{
    const u8 *src = (const u8 *)srcp;
    u8 *dst = (u8 *)destp;
    u32 count = dmaCntData & 0x001FFFFFu;
    size_t unit = (dmaCntData & (1u << 26)) != 0 ? 4u : 2u;
    u32 i;

    if ((dmaCntData & (1u << 24)) != 0) {
        u8 fill[4];

        memcpy(fill, src, unit);
        for (i = 0; i < count; i++) {
            memcpy(dst, fill, unit);
            dst += unit;
        }
    } else {
        for (i = 0; i < count; i++) {
            memmove(dst, src, unit);
            src += unit;
            dst += unit;
        }
    }
}
