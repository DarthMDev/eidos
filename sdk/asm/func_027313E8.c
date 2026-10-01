/*
 * sdk/asm/func_027313E8.c -- PMi_PreSleepForNdma at 0x027313E8.
 *
 * Replaces src/matched/twl/func_027313E8.c, an empty `asm` placeholder
 * (EXACT*) for TWL "ltdmain" code aletheia does not decompile.  Its caller,
 * PMi_PreSleepForDma.c, calls it only when OS_IsRunOnTwl() is true.  On the
 * port's NITRO boot reaching it is a closure hole and it panics.
 */

#include "asmsdk.h"
#include "../port.h"

void func_027313E8(u32 dmaNo)
{
    (void)dmaNo;
    bw_panic("PMi_PreSleepForNdma", 0x027313E8u, "TWL-only code reached on a NITRO boot");
}

void PMi_PreSleepForNdma(u32 dmaNo)
{
    func_027313E8(dmaNo);
}
