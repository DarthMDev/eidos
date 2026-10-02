/*
 * sdk/asm/GX_BeginLoadClearImage.c -- GX_BeginLoadClearImage at 0x0207FB2C.
 *
 * Replaces src/matched/arm9_sdk/GX_BeginLoadClearImage.c (an asm function):
 * takes the clear-image banks to the LCDC (GX_ResetBankForClearImage,
 * which returns the banks that held it), keeps that mask in the word at
 * 0x02150E2C, and, through a jump table on the mask (0..12), sets the
 * LCDC address the clear image is loaded at, the word at 0x02150E3C
 * (+0x10):
 *
 *     1 (A)        0x067E0000      2 (B), 3 (A+B)   0x06800000
 *     4 (C)        0x06820000      8 (D), 12 (C+D)  0x06840000
 *
 * Any other mask leaves it alone, as the table's plain returns do.
 */

#include "asmsdk.h"

extern u8 g_02150E2C[];
int GX_ResetBankForClearImage(void);

void GX_BeginLoadClearImage(void);

void GX_BeginLoadClearImage(void)
{
    int banks = GX_ResetBankForClearImage();
    u32 addr;

    *(u32 *)g_02150E2C = (u32)banks;
    switch (banks) {
    case 1:  addr = 0x067E0000u; break;
    case 2:
    case 3:  addr = 0x06800000u; break;
    case 4:  addr = 0x06820000u; break;
    case 8:
    case 12: addr = 0x06840000u; break;
    default: return;
    }
    *(u32 *)(g_02150E2C + 0x10) = addr;
}

/* The callers' address spelling. */
void func_0207FB2C(void);

void func_0207FB2C(void)
{
    GX_BeginLoadClearImage();
}
