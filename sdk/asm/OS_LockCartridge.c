/*
 * sdk/asm/OS_LockCartridge.c -- OS_TryLockCartridge at 0x02084DA4.
 *
 * Replaces src/matched/arm9_sdk/OS_LockCartridge.c (asm; named after the
 * neighbouring OS_LockCartridge, but 0x02084DA4 is OS_TryLockCartridge in
 * layout/symbols_sdk.txt and the manifest):
 *
 *     ldr r1, =0x02FFFFE8          the cartridge lock word
 *     ldr r2, =func_02084DC4       OSi_AllocateCartridgeBus
 *     ldr ip, =func_02084CFC       OSi_DoTryLockByWord
 *     mov r3, #1                   disable FIQ while locked
 *     bx  ip
 *
 * A tail call of OSi_DoTryLockByWord with the lock id untouched, as
 * sdk/asm/OS_LockCard.c does for the card lock (which says why the SDK
 * signature is used for the call).
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
extern void func_02084DC4(void);

s32 func_02084DA4(u32 lockID)
{
    return func_02084CFC((u16)lockID, (OSLockWord *)bw_ds_ptr(0x02FFFFE8u),
                         func_02084DC4, 1);
}

s32 OS_TryLockCartridge(u16 lockID)
{
    return func_02084DA4(lockID);
}
