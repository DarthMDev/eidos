/*
 * sdk/asm/func_027312B4.c -- OSi_InitArenaEx_ltdmain at 0x027312B4.
 *
 * Replaces src/matched/twl/func_027312B4.c, an empty `asm` placeholder
 * (EXACT*): the TWL "ltdmain" region is loaded only on a DSi and aletheia
 * does not decompile it, so the matched file has no body to transliterate.
 * Its caller, OS_InitArena (func_020864FC.c), calls it only when
 * OS_IsRunOnTwl() is true; the port boots as a NITRO console, so reaching
 * it is a closure hole and it panics.
 */

#include "asmsdk.h"
#include "../port.h"

void func_027312B4(void)
{
    bw_panic("OSi_InitArenaEx_ltdmain", 0x027312B4u, "TWL-only code reached on a NITRO boot");
}

void OSi_InitArenaEx_ltdmain(void)
{
    func_027312B4();
}
