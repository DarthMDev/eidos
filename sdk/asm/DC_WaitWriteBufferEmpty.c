/*
 * sdk/asm/DC_WaitWriteBufferEmpty.c -- DC_WaitWriteBufferEmpty at 0x02086314.
 *
 * Replaces src/matched/arm9_sdk/DC_WaitWriteBufferEmpty.c (TwlSDK os_cache.o,
 * an asm function):
 *
 *     mov r0, #0
 *     mcr p15, 0, r0, c7, c10, 4   drain the write buffer
 *     bx  lr
 *
 * void DC_WaitWriteBufferEmpty(void).  It stalls until the ARM9's write
 * buffer has reached memory.  The host has no such buffer between the game
 * and the emulated address space, so the function does nothing.
 */

#include "asmsdk.h"

void DC_WaitWriteBufferEmpty(void)
{
}
