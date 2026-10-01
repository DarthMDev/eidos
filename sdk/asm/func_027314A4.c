/*
 * sdk/asm/func_027314A4.c -- 0x027314A4, TWL "ltdmain" code.
 *
 * Replaces src/matched/twl/func_027314A4.c, an empty `asm` placeholder
 * (EXACT*): the TWL ltdmain region is loaded only on a DSi and aletheia
 * does not decompile it, so the matched file has no body to transliterate,
 * and no matched caller names it.  The port boots as a NITRO console, so
 * reaching it is a closure hole and it panics.
 */

#include "asmsdk.h"
#include "../port.h"

void func_027314A4(void)
{
    bw_panic("func_027314A4", 0x027314A4u, "TWL-only code reached on a NITRO boot");
}
