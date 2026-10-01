/*
 * sdk/asm/func_02034F74.c -- 0x02034F74.
 *
 * Replaces src/matched/arm9/func_02034F74.c (an asm function):
 *
 *     ldrb r2, [r0, #0x38]; orr r1, r2, r1; strb r1, [r0, #0x38]
 *
 * ORs `mask` into the byte at +0x38 of the object (only the low byte of
 * the result is stored).  Its one caller is func_02034F84
 * (sdk/asm/func_02034F84.c), which passes the object the global at
 * 0x021469BC points to.
 */

#include "asmsdk.h"

void func_02034F74(void *obj, u32 mask)
{
    u8 *p = (u8 *)obj + 0x38;

    *p = (u8)(*p | mask);
}
