/*
 * sdk/asm/func_0206295C.c -- 0x0206295C.
 *
 * Replaces src/matched/arm9_sdk/func_0206295C.c (an asm function):
 *
 *     ldr ip, =func_02062BAC
 *     add r0, r0, #8
 *     bx  ip
 *
 * func_02062BAC on the table 8 bytes into the object (`void
 * func_02062BAC(struct Tbl *t)`, matched C).
 */

#include "asmsdk.h"

void func_02062BAC(void *t);
void func_0206295C(void *p);

void func_0206295C(void *p)
{
    func_02062BAC((u8 *)p + 8);
}
