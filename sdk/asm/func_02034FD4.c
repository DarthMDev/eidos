/*
 * sdk/asm/func_02034FD4.c -- 0x02034FD4.
 *
 * Replaces src/matched/arm9/func_02034FD4.c (an asm function):
 *
 *     ldrb  r0, [r0, #0x38]
 *     tst   r0, r1
 *     movne r0, #1
 *     moveq r0, #0
 *     bx    lr
 *
 * 1 if the byte at +0x38 of the context has any bit of `mask` set.
 */

#include "asmsdk.h"

int func_02034FD4(const void *ctx, u32 mask);

int func_02034FD4(const void *ctx, u32 mask)
{
    return (((const u8 *)ctx)[0x38] & mask) != 0u;
}
