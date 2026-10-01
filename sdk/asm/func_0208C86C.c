/*
 * sdk/asm/func_0208C86C.c -- 0x0208C86C.
 *
 * Replaces src/matched/arm9_sdk/func_0208C86C.c (an asm tail call):
 *
 *     mov r1, r0; ldr r0, =g_0215301C; b func_0208C788
 *
 * func_0208C788(&g_0215301C, info): appends a PM callback record to the
 * list whose head is the global at 0x0215301C (matched C).
 */

#include "asmsdk.h"

extern u8 g_0215301C[];
extern void func_0208C788(void *listp, void *info);

void func_0208C86C(void *info)
{
    func_0208C788(g_0215301C, info);
}
