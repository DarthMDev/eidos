/*
 * sdk/asm/func_02084F10.c -- OS_ReleaseLockID at 0x02084F10.
 *
 * Replaces src/matched/arm9_sdk/func_02084F10.c (an asm function).
 * void OS_ReleaseLockID(u16 lockID):
 *
 *     ldr   r3, =0x02FFFFB0
 *     cmp   r0, #0x60
 *     addpl r3, r3, #4        ; id >= 0x60: the second word
 *     subpl r0, r0, #0x60
 *     submi r0, r0, #0x40
 *     mov   r1, #0x80000000
 *     mov   r1, r1, lsr r0    ; register shift: bottom byte of r0
 *     ldr r2, [r3]; orr r2, r2, r1; str r2, [r3]
 *
 * Sets the id's free bit back (see func_02084EB8.c for the layout).  The
 * shift amount is the bottom byte of r0 and an amount of 32 or more gives
 * 0, so an id below 0x40 (r0 negative, bottom byte 0xC0 or more) changes
 * nothing, and an id of 0x80 or above wraps through the byte: 0x160 and
 * 0x260 release bit 31 of the second word.  The word is always rewritten.
 */

#include "asmsdk.h"
#include "../dsmem.h"

void OS_ReleaseLockID(u16 lockID)
{
    u32 addr = 0x02FFFFB0u;
    u32 r0 = lockID;
    u32 amount;
    u32 mask;
    volatile u32 *word;

    if (lockID >= 0x60) {
        addr += 4;
        r0 -= 0x60;
    } else {
        r0 -= 0x40;
    }
    amount = r0 & 0xFFu;
    mask = (amount < 32) ? (0x80000000u >> amount) : 0u;
    word = (volatile u32 *)bw_ds_ptr(addr);
    word[0] = word[0] | mask;
}

void func_02084F10(u16 lockID)
{
    OS_ReleaseLockID(lockID);
}
