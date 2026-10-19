/*
 * sdk/asm/func_02084E2C.c -- 0x02084E2C OS_UnLockCard.
 *
 * Replaces src/matched/arm9_sdk/func_02084E2C.c, an mwcc `asm` body that is
 * a tail jump:
 *
 *     ldr r1, =OS_UnlockCard
 *     bx  r1
 *
 * The SDK's older spelling of the card unlock, forwarding its lock id in r0
 * to OS_UnlockCard (0x02084E10, matched C).  ov231's CARD_SPI_RunCommand
 * calls it after its SPI exchange.
 */

#include "asmsdk.h"

extern s32 OS_UnlockCard(u16 lockID);

s32 OS_UnLockCard(u16 lockID)
{
    return OS_UnlockCard(lockID);
}
