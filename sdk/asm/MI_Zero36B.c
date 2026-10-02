/*
 * sdk/asm/MI_Zero36B.c -- MI_Zero36B at 0x020834A8.
 *
 * Replaces src/matched/arm9_sdk/MI_Zero36B.c (an asm function): three
 * `stmia r0!, {r1, r2, r3}` of zero registers, 36 bytes cleared a word at a
 * time.
 */

#include "asmsdk.h"

void MI_Zero36B(void *dst);

void MI_Zero36B(void *dst)
{
    u32 *d = (u32 *)dst;
    u32 i;

    for (i = 0; i < 9u; i++) {
        d[i] = 0;
    }
}
