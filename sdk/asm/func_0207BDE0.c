/*
 * sdk/asm/func_0207BDE0.c -- MTX_Copy44To43_ at 0x0207BDE0.
 *
 * Replaces src/matched/arm9_sdk/func_0207BDE0.c (an asm function, libfx).
 * Writes a 4x4 matrix into a 4x3 one: the fourth word of each row dropped,
 * as the stm/str sequence of the asm does, word for word; fx32 in
 * 20.12, so 1 is 0x1000.  The words are written as u32 so a negation
 * of 0x80000000 wraps as the asm's `neg` does.
 */

#include "asmsdk.h"

#define ONE 0x1000u

void func_0207BDE0(const void *src, void *dst);

void func_0207BDE0(const void *src, void *dst)
{
    const u32 *s = (const u32 *)src;
    u32 *d = (u32 *)dst;
    u32 r, c;

    for (r = 0; r < 4u; r++) {
        for (c = 0; c < 3u; c++) {
            d[r * 3u + c] = s[r * 4u + c];
        }
    }
}
