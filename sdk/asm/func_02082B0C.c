/*
 * sdk/asm/func_02082B0C.c -- MIi_CpuCopyFast at 0x02082B0C.
 *
 * Replaces src/matched/arm9_sdk/func_02082B0C.c (an asm function):
 * (src, dst, size), eight-word ldm/stm blocks while a whole block fits
 * below dst + size, then single words while dst is below dst + size.  So
 * the copy is word by word, and a size that is not a multiple of four
 * still copies its last partial word whole, as the word loop does.
 */

#include "asmsdk.h"

void func_02082B0C(const void *src, void *dst, u32 size);

void func_02082B0C(const void *src, void *dst, u32 size)
{
    const u32 *s = (const u32 *)src;
    u32 *d = (u32 *)dst;
    u32 i;

    for (i = 0; i < (size + 3u) / 4u; i++) {
        d[i] = s[i];
    }
}
