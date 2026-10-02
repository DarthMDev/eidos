/*
 * sdk/asm/MI_Copy48B.c -- MI_Copy48B at 0x02082B7C.
 *
 * Replaces src/matched/arm9_sdk/MI_Copy48B.c (an asm function): four
 * `ldmia r0!, {r2, r3, r12}; stmia r1!, {r2, r3, r12}` pairs, 48 bytes
 * copied a word at a time from src to dst.
 */

#include "asmsdk.h"

void MI_Copy48B(const void *src, void *dst);

void MI_Copy48B(const void *src, void *dst)
{
    const u32 *s = (const u32 *)src;
    u32 *d = (u32 *)dst;
    u32 i;

    for (i = 0; i < 12u; i++) {
        d[i] = s[i];
    }
}
