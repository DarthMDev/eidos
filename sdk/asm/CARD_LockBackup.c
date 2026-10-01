/*
 * sdk/asm/CARD_LockBackup.c -- CARD_LockBackup at 0x02075E9C.
 *
 * Replaces src/matched/arm9_sdk/CARD_LockBackup.c (TwlSDK card_api.o, an asm
 * tail call):
 *
 *     ldr ip, =func_02075ED4; mov r1, #2; bx ip
 *
 * void CARD_LockBackup(u16 lock_id) is CARDi_LockResource(lock_id,
 * CARD_TARGET_BACKUP): r0 passes through, r1 = 2.  func_02075ED4's matched
 * C is void (s32 owner, CARDTarget target); the enum is copied from it
 * below.  The matched callers (func_02033534.c and others) use
 * func_02075E9C, which forwards.
 */

#include "asmsdk.h"

typedef enum CARDTarget
{
    CARD_TARGET_NONE,
    CARD_TARGET_ROM,
    CARD_TARGET_BACKUP
} CARDTarget;

extern void func_02075ED4(s32 owner, CARDTarget target);

void CARD_LockBackup(u16 lock_id)
{
    func_02075ED4((s32)lock_id, CARD_TARGET_BACKUP);
}

void func_02075E9C(u16 lock_id)
{
    CARD_LockBackup(lock_id);
}
