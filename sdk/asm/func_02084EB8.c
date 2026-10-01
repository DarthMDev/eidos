/*
 * sdk/asm/func_02084EB8.c -- OS_GetLockID at 0x02084EB8.
 *
 * Replaces src/matched/arm9_sdk/func_02084EB8.c (an asm function: it needs
 * clz).  s32 OS_GetLockID(void).
 *
 * The ARM9's lock ids live in two 32-bit flag words in shared main RAM at
 * 0x02FFFFB0 (ids 0x40-0x5F) and 0x02FFFFB4 (ids 0x60-0x7F); a set bit is
 * a free id, the most significant bit the lowest id.  The asm:
 *
 *     word0: clz; if not 32: id = 0x40 + clz, clear that bit in word0
 *     word1: clz; if 32: return -3 (OS_LOCK_ID_ERROR, 0xFFFFFFFD)
 *            else id = 0x60 + clz, clear that bit in word1
 *
 * The clearing store re-reads its word (ldr, bic, str), kept here.  The
 * words are reached through the DS address space, as the asm's literal
 * addresses are, so the ARM7 stand-in and anything else that reads the
 * shared area sees the same bits.
 */

#include "asmsdk.h"
#include "../dsmem.h"

static u32 bw_clz32(u32 v)
{
    u32 n = 0;

    if (v == 0) {
        return 32;
    }
    while ((v & 0x80000000u) == 0) {
        v <<= 1;
        n++;
    }
    return n;
}

s32 OS_GetLockID(void)
{
    volatile u32 *word = (volatile u32 *)bw_ds_ptr(0x02FFFFB0u);
    u32 base = 0x40;
    u32 n = bw_clz32(word[0]);

    if (n == 32) {
        word = (volatile u32 *)bw_ds_ptr(0x02FFFFB4u);
        n = bw_clz32(word[0]);
        if (n == 32) {
            return (s32)0xFFFFFFFDu;
        }
        base = 0x60;
    }
    word[0] = word[0] & ~(0x80000000u >> n);
    return (s32)(base + n);
}

s32 func_02084EB8(void)
{
    return OS_GetLockID();
}
