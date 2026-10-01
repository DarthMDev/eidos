/*
 * sdk/asm/func_02730468.c -- 0x02730468, a TWL-only function.
 *
 * Replaces src/matched/twl/func_02730468.c, an empty asm placeholder for
 * code in the DSi-only ARM9i block, which a NITRO console never loads.
 * MI_InitWramManager (sdk/asm/MI_InitWramManager.c) calls it only when
 * OS_IsRunOnTwl() says the console is a DSi; reaching it on the NITRO boot
 * is a port bug, reported as such.
 */

#include "asmsdk.h"
#include "../port.h"

void func_02730468(void)
{
    bw_panic("func_02730468", 0x02730468u, "TWL-only code reached on a NITRO boot");
}
