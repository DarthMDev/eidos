/*
 * sdk/asm/func_020862F0.c -- DC_FlushRange at 0x020862F0.
 *
 * Replaces src/matched/arm9_sdk/func_020862F0.c (TwlSDK os_cache.o, an asm
 * function):
 *
 *     mov r12, #0
 *     add r1, r1, r0               end = start + nBytes
 *     bic r0, r0, #0x1f            align start down to a cache line
 *   line:
 *     mcr p15, 0, r12, c7, c10, 4  drain the write buffer
 *     mcr p15, 0, r0, c7, c14, 1   clean and invalidate the line at r0
 *     add r0, r0, #0x20; cmp r0, r1; blt line
 *     bx  lr
 *
 * void DC_FlushRange(const void *startAddr, u32 nBytes).  Only the ARM9
 * data cache and write buffer are touched; on the coherent host there is
 * nothing to write back or discard, so the function does nothing.  Matched
 * callers use both spellings (func_020862F0 in game code, DC_FlushRange in
 * SDK code); one forwards to the other.
 */

#include "asmsdk.h"

void DC_FlushRange(const void *startAddr, u32 nBytes)
{
    (void)startAddr;
    (void)nBytes;
}

void func_020862F0(const void *startAddr, u32 nBytes)
{
    DC_FlushRange(startAddr, nBytes);
}
