/*
 * sdk/asm/func_0207ADE8.c -- MTX_Copy33To44_ at 0x0207ADE8.
 *
 * Replaces src/matched/arm9_sdk/func_0207ADE8.c (an asm function, libfx).
 * Writes a 3x3 matrix into a 4x4 one: each row gets a fourth word 0, and the last row is (0, 0, 0, 1),
 * as the stm/str sequence of the asm does, word for word; fx32 in
 * 20.12, so 1 is 0x1000.  The words are written as u32 so a negation
 * of 0x80000000 wraps as the asm's `neg` does.
 */

#include "asmsdk.h"

#define ONE 0x1000u

void func_0207ADE8(const void *src, void *dst);

void func_0207ADE8(const void *src, void *dst)
{
    const u32 *s = (const u32 *)src;
    u32 *d = (u32 *)dst;

    u32 r, c;
    for (r = 0; r < 3u; r++) {
        for (c = 0; c < 3u; c++) {
            d[r * 4u + c] = s[r * 3u + c];
        }
        d[r * 4u + 3u] = 0;
    }
    d[12] = 0;
    d[13] = 0;
    d[14] = 0;
    d[15] = ONE;
}
