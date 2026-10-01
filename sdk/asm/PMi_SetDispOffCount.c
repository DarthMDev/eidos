/*
 * sdk/asm/PMi_SetDispOffCount.c -- PMi_SetDispOffCount at 0x0208C944.
 *
 * Replaces src/matched/arm9_sdk/PMi_SetDispOffCount.c (TwlSDK pm.o, an asm
 * function):
 *
 *     ldr r1, =0x02FFFC3C     OS_GetVBlankCount()'s system-area word
 *     ldr r0, =g_02153000     pm.o .bss
 *     ldr r1, [r1]
 *     str r1, [r0, #0x10]     dispOffCount = the V-blank count
 *     bx  lr
 *
 * void PMi_SetDispOffCount(void).  +0x10 is the dispOffCount field of the
 * PmBss struct the matched pm.o files declare (u16 isInit, a callback-list
 * pointer, sleepEndFlag, lcdCount, dispOffCount, ...).  The host writes the
 * field through that same prefix, so it lands where the matched C reads it
 * whatever the pointer width (+0x10 on a 32-bit host, as on the DS).  The
 * V-blank count is read through the address space.
 */

#include "asmsdk.h"
#include "../dsmem.h"

typedef struct BwPmBssHead {
    u16 isInit;
    void *postSleepCallbackList;
    volatile BOOL sleepEndFlag;
    u32 lcdCount;
    u32 dispOffCount;
} BwPmBssHead;

extern BwPmBssHead g_02153000;

void PMi_SetDispOffCount(void)
{
    g_02153000.dispOffCount = *(const volatile u32 *)bw_ds_ptr(0x02FFFC3Cu);
}
