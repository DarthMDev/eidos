/*
 * sdk/asm/func_02005428.c -- 0x02005428, a long-branch veneer into TWL code.
 *
 * Replaces src/matched/arm9/func_02005428.c (an asm function):
 *
 *     ldr pc, [pc, #-4]
 *     .word 0x027314F0
 *
 * Jumps to 0x027314F0 in the TWL "ltdmain" region (aletheia's
 * twl_sdk/func_027314F0.c is an empty placeholder).  Its caller,
 * func_020053EC.c, is gated on g_020A9E80 like func_02005420.c.
 * Reaching it on the port's NITRO boot is a closure hole.
 */

#include "asmsdk.h"
#include "../port.h"

int func_02005428(void)
{
    bw_panic("func_02005428", 0x027314F0u, "TWL-only veneer reached on a NITRO boot");
    return 0;
}
