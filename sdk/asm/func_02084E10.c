/*
 * sdk/asm/func_02084E10.c -- OS_UnlockCard at 0x02084E10.
 *
 * Replaces src/matched/arm9_sdk/func_02084E10.c (TwlSDK os_spinlock.o, an
 * asm tail call):
 *
 *     ldr r1, =0x02FFFFE0          the card lock word (HW_CARD_LOCK_BUF)
 *     ldr r2, =func_02084E98       OSi_FreeCardBus
 *     ldr ip, =func_02084CEC       OS_UnlockByWord
 *     bx  ip
 *
 * s32 OS_UnlockCard(u16 lockID) is OS_UnlockByWord(lockID,
 * (OSLockWord *)HW_CARD_LOCK_BUF, OSi_FreeCardBus).
 *
 * The callee: 0x02084CEC is OS_UnlockByWord in layout/symbols_sdk.txt, and
 * its body is the tail call OSi_DoUnlockByWord(r0, r1, r2, FALSE), that is
 * func_02084C78(lockID, lockp, ctrlFuncp, 0).  aletheia's
 * func_02084CEC.c matches those bytes under a different SDK name
 * (FSi_HasEnoughSpaceToCreateUserFile, three pointer-and-size parameters),
 * so its C prototype cannot carry a u16 lock id and a function pointer on
 * the host.  This file therefore calls func_02084C78 (OSi_DoUnlockByWord,
 * matched C with the real prototype) with FALSE, which is exactly what
 * func_02084CEC does with the registers it is handed.
 *
 * The lock word is reached through the address space; OSi_FreeCardBus is
 * matched C, void (void).  The caller (card_unlockrom.c, CARD_UnlockRom)
 * declares int func_02084E10(unsigned int id); the lock id is a u16 in the
 * SDK, so the host narrows it as OSi_DoUnlockByWord's u16 parameter does.
 */

#include "asmsdk.h"
#include "../dsmem.h"

typedef struct OSLockWord {
    volatile u32 lockFlag;
    volatile u16 ownerID;
    volatile u16 extension;
} OSLockWord;

extern s32 func_02084C78(u16 lockID, OSLockWord *lockp, void (*ctrlFuncp)(void), BOOL disableFIQ);
extern void func_02084E98(void);

s32 func_02084E10(u32 lockID)
{
    return func_02084C78((u16)lockID, (OSLockWord *)bw_ds_ptr(0x02FFFFE0u), func_02084E98, 0);
}
