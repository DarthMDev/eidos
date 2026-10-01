/*
 * sdk/asm/func_02005420.c -- 0x02005420, a long-branch veneer into TWL code.
 *
 * Replaces src/matched/arm9/func_02005420.c (an asm function):
 *
 *     ldr pc, [pc, #-4]
 *     .word 0x027315D0
 *
 * Jumps into the TWL "ltdmain" region (not loaded on a NITRO console, not
 * decompiled).  The caller, func_020053EC.c, runs it only after
 * g_020A9E80 is set, which only func_02005440 does, from the TWL branch of
 * func_02005328.c.  Reaching it on the port's NITRO boot is a closure hole.
 */

#include "asmsdk.h"
#include "../port.h"

int func_02005420(void)
{
    bw_panic("func_02005420", 0x027315D0u, "TWL-only veneer reached on a NITRO boot");
    return 0;
}
