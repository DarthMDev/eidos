/*
 * sdk/asm/func_0203129C.c -- 0x0203129C.
 *
 * Replaces src/matched/arm9_sdk/func_0203129C.c (an asm function):
 *
 *     stmfd sp!, {r3, lr}
 *     bl func_02031354          ; argument passed through
 *     ldr r1, =g_02146990
 *     str r0, [r1]
 *     ldmfd sp!, {r3, pc}
 *
 * Stores func_02031354's result (void *func_02031354(int), matched C) in
 * the global pointer at 0x02146990.
 */

#include "asmsdk.h"

extern void *func_02031354(int a0);
extern void *g_02146990;

void func_0203129C(u32 a)
{
    g_02146990 = func_02031354((int)a);
}
