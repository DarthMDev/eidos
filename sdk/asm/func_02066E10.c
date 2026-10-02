/*
 * sdk/asm/func_02066E10.c -- 0x02066E10.
 *
 * Replaces src/matched/arm9_sdk/func_02066E10.c (an asm function):
 *
 *     ldr r1, =g_020A8218
 *     str r0, [r1]
 *     bx  lr
 *
 * Stores its argument in the word at 0x020A8218.
 */

#include "asmsdk.h"

extern u32 g_020A8218;

void func_02066E10(u32 value);

void func_02066E10(u32 value)
{
    g_020A8218 = value;
}
