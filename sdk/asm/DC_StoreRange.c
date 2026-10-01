/*
 * sdk/asm/DC_StoreRange.c -- DC_StoreRange at 0x020862D4.
 *
 * Replaces src/matched/arm9_sdk/DC_StoreRange.c (TwlSDK os_cache.o, an asm
 * function):
 *
 *     add r1, r1, r0               end = start + nBytes
 *     bic r0, r0, #0x1f            align start down to a cache line
 *   loop:
 *     mcr p15, 0, r0, c7, c10, 1   clean (write back) the data cache line
 *     add r0, r0, #0x20; cmp r0, r1; blt loop
 *     bx  lr
 *
 * void DC_StoreRange(const void *startAddr, u32 nBytes).  Writing dirty
 * lines back to RAM matters only to a DMA or the ARM7 reading main memory
 * behind the ARM9 cache; on the coherent host every store is already
 * visible, so the function does nothing.  func_020862D4 (the spelling some
 * matched callers declare) forwards.
 */

#include "asmsdk.h"

void DC_StoreRange(const void *startAddr, u32 nBytes)
{
    (void)startAddr;
    (void)nBytes;
}

void func_020862D4(const void *startAddr, u32 nBytes)
{
    DC_StoreRange(startAddr, nBytes);
}
