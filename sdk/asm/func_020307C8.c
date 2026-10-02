/*
 * sdk/asm/func_020307C8.c -- 0x020307C8.
 *
 * Replaces src/matched/arm9_sdk/func_020307C8.c (an asm function):
 *
 *     stmfd   sp!, {r3, lr}
 *     bl      func_02030238        ; all three arguments passed through
 *     cmp     r0, #0
 *     ldmnefd sp!, {r3, pc}
 *     bl      func_02088234        ; OS_Terminate
 *     ldmfd   sp!, {r3, pc}
 *
 * Resize a heap block in place (func_02030238, `u32 func_02030238(void *p,
 * u32 heapId, s32 size)`, matched C) and stop the console when it fails.
 * The callers pass (block, heap id, size), e.g. func_02046440.
 */

#include "asmsdk.h"

extern u32  func_02030238(void *p, u32 heapId, s32 size);
extern void OS_Terminate(void);

void func_020307C8(void *p, u32 heapId, s32 size);

void func_020307C8(void *p, u32 heapId, s32 size)
{
    if (func_02030238(p, heapId, size) != 0) {
        return;
    }
    OS_Terminate();
}
