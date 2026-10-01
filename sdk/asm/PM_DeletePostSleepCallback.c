/*
 * sdk/asm/PM_DeletePostSleepCallback.c -- PM_DeletePostSleepCallback at 0x0208C884.
 *
 * Replaces src/matched/arm9_sdk/PM_DeletePostSleepCallback.c (TwlSDK pm.o,
 * an asm tail call):
 *
 *     mov r1, r0; ldr r0, =g_02153004; ldr ip, =func_0208C788; bx ip
 *
 * void PM_DeletePostSleepCallback(PMSleepCallbackInfo *info) is
 * PMi_DeleteList(&postSleepCallbackList, info).  func_0208C788 is matched
 * C, void (PMiGenCallbackInfo **listp, PMiGenCallbackInfo *info).
 * g_02153004 is the list head (a pointer, so host width); the insert side,
 * func_0208C828.c, names the same symbol, so both ends agree on the object.
 * The game caller (func_02073B3C.c) declares the address spelling with a
 * void * argument, which forwards.
 */

#include "asmsdk.h"

typedef struct PMiGenCallbackInfo PMiGenCallbackInfo;

extern PMiGenCallbackInfo *g_02153004;
extern void func_0208C788(PMiGenCallbackInfo **listp, PMiGenCallbackInfo *info);

void PM_DeletePostSleepCallback(PMiGenCallbackInfo *info)
{
    func_0208C788(&g_02153004, info);
}

void func_0208C884(void *info)
{
    PM_DeletePostSleepCallback((PMiGenCallbackInfo *)info);
}
