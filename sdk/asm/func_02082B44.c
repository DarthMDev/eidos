/*
 * sdk/asm/func_02082B44.c -- 0x02082B44 MI_Copy32B.
 *
 * Replaces src/matched/arm9_sdk/func_02082B44.c (an mwcc `asm` body):
 * void MI_Copy32B(const void *src, void *dst).  Eight words, copied with
 * ldmia/stmia in groups of three, three and two.
 */

#include "asmsdk.h"

void MI_Copy32B(const void *src, void *dst)
{
    const u32 *s = (const u32 *)src;
    u32 *d = (u32 *)dst;
    u32 i;

    for (i = 0; i < 8; i++) {
        d[i] = s[i];
    }
}
