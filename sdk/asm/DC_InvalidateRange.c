/*
 * sdk/asm/DC_InvalidateRange.c -- DC_InvalidateRange at 0x020862B8.
 *
 * Replaces src/matched/arm9_sdk/DC_InvalidateRange.c (TwlSDK os_cache.o, an
 * asm function):
 *
 *     add r1, r1, r0               end = start + nBytes
 *     bic r0, r0, #0x1f            align start down to a cache line
 *   loop:
 *     mcr p15, 0, r0, c7, c6, 1    invalidate the data cache line at r0
 *     add r0, r0, #0x20; cmp r0, r1; blt loop
 *     bx  lr
 *
 * void DC_InvalidateRange(void *startAddr, u32 nBytes).  The only effect is
 * on the ARM946E-S data cache (discarding lines so later reads see what a
 * DMA or the ARM7 wrote).  The host's memory is coherent and nothing caches
 * the emulated address space, so the function does nothing.  The matched
 * callers that use the address spelling (most declare it (u32, u32)) get
 * func_020862B8, a forwarder.
 */

#include "asmsdk.h"

void DC_InvalidateRange(void *startAddr, u32 nBytes)
{
    (void)startAddr;
    (void)nBytes;
}

void func_020862B8(void *startAddr, u32 nBytes)
{
    DC_InvalidateRange(startAddr, nBytes);
}
