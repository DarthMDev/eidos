/*
 * sdk/asm/func_02005454.c -- 0x02005454, a long-branch veneer into TWL code.
 *
 * Replaces src/matched/arm9/func_02005454.c (an asm function):
 *
 *     ldr pc, [pc, #-4]
 *     .word 0x027315C0
 *
 * Jumps into the TWL "ltdmain" region (not loaded on a NITRO console, not
 * decompiled).  Its caller is func_02005440.c, the callback func_02005328.c
 * installs only on TWL hardware.  Reaching it on the port's NITRO boot is a
 * closure hole.
 */

#include "asmsdk.h"
#include "../port.h"

void func_02005454(int v)
{
    (void)v;
    bw_panic("func_02005454", 0x027315C0u, "TWL-only veneer reached on a NITRO boot");
}
