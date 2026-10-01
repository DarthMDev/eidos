/*
 * sdk/asm/DWCi_Language.c -- DWCi_Language at 0x020461E4.
 *
 * Replaces src/matched/arm9_sdk/DWCi_Language.c (TwlDWC libdwcutil main.o,
 * an asm function):
 *
 *     ldr r0, =g_02146A54; ldrb r0, [r0]; bx lr
 *
 * Returns the language byte at g_02146A54, zero-extended.  The matched
 * callers (ov016) declare it int DWCi_Language(void) and index tables with
 * it; one game caller declares the address spelling as u8 (void), which
 * forwards.
 */

#include "asmsdk.h"

extern u8 g_02146A54;

int DWCi_Language(void)
{
    return (int)g_02146A54;
}

u8 func_020461E4(void)
{
    return (u8)DWCi_Language();
}
