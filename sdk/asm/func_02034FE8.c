/*
 * sdk/asm/func_02034FE8.c -- 0x02034FE8.
 *
 * Replaces src/matched/arm9_sdk/func_02034FE8.c (an asm function):
 *
 *     ldr r2, =0x021469BC
 *     mov r1, r0
 *     ldr r0, [r2]
 *     ldr ip, =func_02034FD4
 *     bx  ip
 *
 * func_02034FD4(the system context, mask): the context is the word at
 * 0x021469BC, read through the DS address space.
 */

#include "asmsdk.h"
#include "../dsmem.h"

int func_02034FD4(const void *ctx, u32 mask);
int func_02034FE8(u32 mask);

int func_02034FE8(u32 mask)
{
    void *ctx = *(void **)bw_ds_ptr(0x021469BCu);

    return func_02034FD4(ctx, mask);
}
