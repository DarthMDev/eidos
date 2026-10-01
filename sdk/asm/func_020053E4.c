/*
 * sdk/asm/func_020053E4.c -- 0x020053E4, a long-branch veneer into TWL code.
 *
 * Replaces src/matched/arm9/func_020053E4.c (an asm function):
 *
 *     ldr pc, [pc, #-4]
 *     .word 0x0273154C
 *
 * The eight-byte stub a Thumb caller reaches with blx to jump into the TWL
 * "ltdmain" region at 0x0273154C.  That region is loaded only on a DSi, and
 * its code is not decompiled (aletheia carries its functions as empty
 * placeholders), so there is nothing to forward to.  The one caller,
 * func_02005328.c (the system initialiser), reaches it only when
 * sub_0202EB0C() reports TWL hardware; the port boots as a NITRO console,
 * so reaching this is a closure hole, and it panics with the target.
 */

#include "asmsdk.h"
#include "../port.h"

void func_020053E4(void *p)
{
    (void)p;
    bw_panic("func_020053E4", 0x0273154Cu, "TWL-only veneer reached on a NITRO boot");
}
