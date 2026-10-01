/*
 * sdk/asm/OS_LockCard.c -- OS_TryLockCard at 0x02084E38.
 *
 * Replaces src/matched/arm9_sdk/OS_LockCard.c (asm; the file is named after
 * the neighbouring OS_LockCard, but the address is OS_TryLockCard's in
 * layout/symbols_sdk.txt and the manifest):
 *
 *     ldr r1, =0x02FFFFE0          the card lock word (HW_CARD_LOCK_BUF)
 *     ldr r2, =func_02084E54       OSi_AllocateCardBus
 *     ldr ip, =func_02084D68       OS_TryLockByWord
 *     bx  ip
 *
 * OS_TryLockByWord (0x02084D68) is OSi_DoTryLockByWord(id, lock, ctrl,
 * FALSE) (0x02084CFC).  aletheia matches both under FS names with
 * (struct *, char *, u32[, int]) prototypes that fit the bytes but not a
 * host call carrying a lock id and a function pointer, so this calls
 * OSi_DoTryLockByWord directly with its SDK signature, the same effect
 * (sdk/asm/func_02084E10.c does the same for the unlock).  The lock word is
 * reached by its DS address.  Defined under its address spelling and its
 * SDK name; the caller (cardi_lockbuscondition.c) says func_02084E38.
 */

#include "asmsdk.h"
#include "../dsmem.h"

typedef struct OSLockWord {
    volatile u32 lockFlag;
    volatile u16 ownerID;
    volatile u16 extension;
} OSLockWord;

extern s32 func_02084CFC(u16 lockID, OSLockWord *lockp, void (*ctrlFuncp)(void),
                         BOOL disableFIQ);
extern void func_02084E54(void);

s32 func_02084E38(u32 lockID)
{
    return func_02084CFC((u16)lockID, (OSLockWord *)bw_ds_ptr(0x02FFFFE0u),
                         func_02084E54, 0);
}

s32 OS_TryLockCard(u16 lockID)
{
    return func_02084E38(lockID);
}
