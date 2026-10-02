/*
 * sdk/asm/func_0207B5C0.c -- MTX_Copy43To44_ at 0x0207B5C0.
 *
 * Replaces src/matched/arm9_sdk/func_0207B5C0.c (an asm function, libfx).
 * Writes a 4x3 matrix into a 4x4 one: each row gets a fourth word 0, the last row 1 (0x1000),
 * as the stm/str sequence of the asm does, word for word; fx32 in
 * 20.12, so 1 is 0x1000.  The words are written as u32 so a negation
 * of 0x80000000 wraps as the asm's `neg` does.
 */

#include "asmsdk.h"

#define ONE 0x1000u

void func_0207B5C0(const void *src, void *dst);

void func_0207B5C0(const void *src, void *dst)
{
    const u32 *s = (const u32 *)src;
    u32 *d = (u32 *)dst;

    u32 r, c;
    for (r = 0; r < 4u; r++) {
        for (c = 0; c < 3u; c++) {
            d[r * 4u + c] = s[r * 3u + c];
        }
        d[r * 4u + 3u] = (r == 3u) ? ONE : 0u;
    }
}
