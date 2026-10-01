/*
 * sdk/asm/func_0272EA74.c -- 0x0272EA74, TWL "ltdmain" code.
 *
 * Replaces src/matched/twl/func_0272EA74.c, an empty `asm` placeholder
 * (EXACT*): the TWL ltdmain region is loaded only on a DSi and aletheia
 * does not decompile it, so the matched file has no body to transliterate,
 * and no matched caller names it.  The port boots as a NITRO console, so
 * reaching it is a closure hole and it panics.
 */

#include "asmsdk.h"
#include "../port.h"

void func_0272EA74(void)
{
    bw_panic("func_0272EA74", 0x0272EA74u, "TWL-only code reached on a NITRO boot");
}
