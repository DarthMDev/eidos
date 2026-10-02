/*
 * sdk/asm/CARD_SetPulledOutCallback.c -- CARD_SetPulledOutCallback at
 * 0x02066DEC.
 *
 * Replaces src/matched/arm9_sdk/CARD_SetPulledOutCallback.c (an asm
 * function):
 *
 *     ldr r1, =g_02148B2C
 *     str r0, [r1, #8]
 *     bx  lr
 *
 * The card-removal callback is the third word of the cartridge state at
 * 0x02148B2C.
 */

#include "asmsdk.h"

extern u8 g_02148B2C[];

void CARD_SetPulledOutCallback(void (*callback)(void));

void CARD_SetPulledOutCallback(void (*callback)(void))
{
    *(void (**)(void))(g_02148B2C + 8) = callback;
}

/* The callers' address spelling. */
void func_02066DEC(void (*callback)(void));

void func_02066DEC(void (*callback)(void))
{
    CARD_SetPulledOutCallback(callback);
}
