/*
 * sdk/asm/CTRDGi_CallbackForSetPhi.c -- CTRDGi_CallbackForSetPhi at
 * 0x02066DFC.
 *
 * Replaces src/matched/arm9_sdk/CTRDGi_CallbackForSetPhi.c (an asm
 * function):
 *
 *     ldr r0, =g_02148B2C
 *     mov r1, #0
 *     str r1, [r0, #4]
 *     bx  lr
 *
 * Clears the second word of the cartridge state at 0x02148B2C (the PHI
 * request the ARM7 has answered).
 */

#include "asmsdk.h"

extern u8 g_02148B2C[];

void CTRDGi_CallbackForSetPhi(void);

void CTRDGi_CallbackForSetPhi(void)
{
    *(u32 *)(g_02148B2C + 4) = 0;
}

/* The callers' address spelling. */
void func_02066DFC(void);

void func_02066DFC(void)
{
    CTRDGi_CallbackForSetPhi();
}
