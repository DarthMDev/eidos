/*
 * sdk/asm/func_02084D98.c -- OS_UnLockCartridge at 0x02084D98.
 *
 * Replaces src/matched/arm9/func_02084D98.c (a hand-written SDK stub):
 *
 *     ldr r1, =func_02084D78; bx r1
 *
 * A tail call with the arguments untouched: the result is func_02084D78's
 * (OS_UnlockCartridge's common body, s32 (u16 lockID), matched C).  The
 * caller (CTRDGi_UnlockByProcessor.c) uses the SDK name.
 */

#include "asmsdk.h"

extern s32 func_02084D78(u16 lockID);

s32 func_02084D98(u16 lockID)
{
    return func_02084D78(lockID);
}

s32 OS_UnLockCartridge(u16 lockID)
{
    return func_02084D78(lockID);
}
