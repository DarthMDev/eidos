/*
 * sdk/asm/func_02075DC8.c -- CARD_Enable at 0x02075DC8.
 *
 * Replaces src/matched/arm9_sdk/func_02075DC8.c (TwlSDK card_api.o, an asm
 * function):
 *
 *     ldr r1, =g_0215019C; str r0, [r1]; bx lr
 *
 * void CARD_Enable(BOOL enable): stores the argument in the CARD enable
 * flag g_0215019C.  No matched caller uses the SDK name, so only the
 * address spelling is defined.
 */

#include "asmsdk.h"

extern BOOL g_0215019C;

void func_02075DC8(BOOL enable)
{
    g_0215019C = enable;
}
