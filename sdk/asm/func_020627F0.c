/*
 * sdk/asm/func_020627F0.c -- 0x020627F0.
 *
 * Replaces src/matched/arm9/func_020627F0.c (an asm function):
 *
 *     ldrh  r1, [r0, #2]
 *     tst   r1, #1
 *     ldrh  r1, [r0]
 *     ldrne r0, [r0, #4]
 *     addne r0, r0, r1, lsl #4
 *     ldreq r0, [r0, #4]
 *     addeq r0, r0, r1, lsl #3
 *     bx    lr
 *
 * The current entry of a table: the base at +4 plus the index at +0 times
 * 16 when bit 0 of the flags at +2 is set, times 8 otherwise.
 */

#include "asmsdk.h"

void *func_020627F0(void *ctx);

void *func_020627F0(void *ctx)
{
    const u8 *c = (const u8 *)ctx;
    u32 index = *(const u16 *)c;
    u8 *base = *(u8 *const *)(c + 4);

    return base + index * ((*(const u16 *)(c + 2) & 1u) ? 16u : 8u);
}
