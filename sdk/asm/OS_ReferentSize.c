/*
 * sdk/asm/OS_ReferentSize.c -- OS_ReferentSize at 0x02086C54.
 *
 * Replaces src/matched/arm9_sdk/OS_ReferentSize.c (an asm function):
 *
 *     stmfd sp!, {r4, lr}
 *     mov   r4, r1             ; the block
 *     bl    OS_DisableInterrupts
 *     bl    OS_RestoreInterrupts   ; with what the first returned
 *     ldr   r0, [r4, #-0x18]
 *     sub   r0, r0, #0x20
 *     ldmfd sp!, {r4, pc}
 *
 * (heap, block): the block's size, read from its header 0x18 bytes before
 * it, less the 0x20-byte header, between a disable and a restore of the
 * interrupts.
 */

#include "asmsdk.h"

u32  OS_DisableInterrupts(void);
u32  OS_RestoreInterrupts(u32 state);

u32 OS_ReferentSize(void *heap, void *block);

u32 OS_ReferentSize(void *heap, void *block)
{
    (void)heap;
    (void)OS_RestoreInterrupts(OS_DisableInterrupts());
    return *(const u32 *)((const u8 *)block - 0x18) - 0x20u;
}

/* The callers' address spelling. */
u32 func_02086C54(void *heap, void *block);

u32 func_02086C54(void *heap, void *block)
{
    return OS_ReferentSize(heap, block);
}
