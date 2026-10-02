/*
 * sdk/asm/GX_SetBankForLCDC.c -- GX_SetBankForLCDC at 0x0208068C.
 *
 * Replaces src/matched/arm9_sdk/GX_SetBankForLCDC.c (an asm function):
 *
 *     ldr r2, =g_02150E4C          ; the GX state's LCDC bank mask
 *     mov r1, r0, lsl #16
 *     ldrh r3, [r2]
 *     ldr ip, =func_0207FD34       ; GX_VRAMCNT_SetLCDC_
 *     orr r1, r3, r1, lsr #16
 *     strh r1, [r2]
 *     bx  ip                       ; r0 still the bank mask
 *
 * Adds the banks to the LCDC mask (the halfword at 0x02150E4C), then maps
 * them to the LCDC (sdk/boot/gx.c's GX_VRAMCNT_SetLCDC_).
 */

#include "asmsdk.h"

extern u16 g_02150E4C;
void GX_VRAMCNT_SetLCDC_(int lcdc);

void GX_SetBankForLCDC(int bank);

void GX_SetBankForLCDC(int bank)
{
    g_02150E4C = (u16)(g_02150E4C | (u16)bank);
    GX_VRAMCNT_SetLCDC_(bank);
}

/* The callers' address spelling. */
void func_0208068C(int bank);

void func_0208068C(int bank)
{
    GX_SetBankForLCDC(bank);
}
