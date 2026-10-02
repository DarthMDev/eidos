/*
 * sdk/asm/func_02062810.c -- 0x02062810.
 *
 * Replaces src/matched/arm9_sdk/func_02062810.c (an asm function):
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
void func_02062810(void *p);

void func_02062810(void *p)
{
    func_02062BAC((u8 *)p + 8);
}
