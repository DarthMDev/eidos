/*
 * sdk/asm/func_0272F8A8.c -- 0x0272F8A8, TWL "ltdmain" code.
 *
 * Replaces src/matched/twl/func_0272F8A8.c, an empty `asm` placeholder
 * (EXACT*): the TWL ltdmain region is loaded only on a DSi and aletheia
 * does not decompile it, so the matched file has no body to transliterate.
 * Its callers, func_0272FB28.c and func_0272FB78.c (themselves TWL ltdmain
 * code), declare it with nine arguments; that signature is kept.  The port
 * boots as a NITRO console, so reaching it is a closure hole and it panics.
 */

#include "asmsdk.h"
#include "../port.h"

void func_0272F8A8(int a0, int a1, int a2, int a3, int a4, int a5, void *a6, int a7, unsigned int a8)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    (void)a7;
    (void)a8;
    bw_panic("func_0272F8A8", 0x0272F8A8u, "TWL-only code reached on a NITRO boot");
}
