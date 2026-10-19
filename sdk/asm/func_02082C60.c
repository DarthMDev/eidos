/*
 * sdk/asm/func_02082C60.c -- 0x02082C60 MI_CpuFill.
 *
 * Replaces src/matched/arm9_sdk/func_02082C60.c (an mwcc `asm` body):
 * void MI_CpuFill(void *dst, u8 value, u32 size).  Size 0 returns; up to 8
 * bytes are stored one by one; above that the byte is spread over a word,
 * the destination aligned with a byte, a halfword and a word store, then
 * filled 32 bytes at a time, by words, and the tail by bytes.  Every byte in
 * [dst, dst + size) gets the value.
 */

#include "asmsdk.h"

void MI_CpuFill(void *dst, u8 value, u32 size)
{
    u8 *d = (u8 *)dst;

    while (size-- > 0) {
        *d++ = value;
    }
}
