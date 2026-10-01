/*
 * sdk/asm/func_0273131C.c -- OSi_SearchTitleIdFromList at 0x0273131C.
 *
 * Replaces src/matched/twl/func_0273131C.c, an empty `asm` placeholder
 * (EXACT*) for TWL "ltdmain" code aletheia does not decompile.  Its caller
 * (func_020883D4.c) calls it only with a non-zero previous title id from
 * the deliver argument, which a NITRO boot does not carry (sdk/os_argument.c
 * hands back none).  Reaching it is a closure hole and it panics.
 */

#include "asmsdk.h"
#include "../port.h"

u64 func_0273131C(u64 titleId, u64 mask)
{
    (void)titleId;
    (void)mask;
    bw_panic("OSi_SearchTitleIdFromList", 0x0273131Cu, "TWL-only code reached on a NITRO boot");
    return 0;
}

u64 OSi_SearchTitleIdFromList(u64 titleId, u64 mask)
{
    return func_0273131C(titleId, mask);
}
