/*
 * sdk/gx.c -- the one GX name the heap layer's failure arm reaches.
 *
 * src/matched/sub_02030734.c calls 0x02030524 with no arguments on the
 * out-of-memory path, immediately before the reporter and OS_Terminate.
 * aletheia's layout/symbols_sdk.json offers GX_GetBankForBGExtPltt for that
 * address; the matched file's own header records that the offer is a
 * signature-matcher guess with 37 siblings and that it does not fit the role
 * played here, which is why the matched source spells the callee
 * `sub_02030524` and declares it `void sub_02030524(void)`.  The port must
 * define the name the matched file names, so `sub_02030524` is what is
 * defined below; the SDK spelling is provided beside it for whatever later
 * closure asks for it by name.
 *
 * The value the DS would report.  GX_GetBankForBGExtPltt answers which VRAM
 * banks are currently mapped to the main engine's BG extended-palette slots,
 * as a bit mask over the bank letters.  A bank is in that mask only after the
 * game has assigned one with GX_SetBankForBGExtPltt.  eidos's own 2D engine
 * implements no extended-palette path at all: engine/g2d/bg.c takes a
 * layer's palette from its NCLR and BGxCNT bit 13 is carried on the layer
 * struct without being acted on (README.md, "BG layers").  So the
 * configuration this port actually runs is "no bank assigned to BG extended
 * palettes", and the honest answer to the query is the empty mask, 0.  If a
 * later milestone gives the engine extended palettes, this is the one place
 * that has to learn the mask the game's own GX_SetBankForBGExtPltt calls
 * install, and those calls are not decompiled yet.
 */

#include "port.h"

u32 GX_GetBankForBGExtPltt(void)
{
    return 0u;
}

void sub_02030524(void)
{
    /*
     * Reached only from sub_02030734's failure arm, one call before
     * OS_Terminate.  Whatever the DS function is, its result is discarded
     * there (the matched source declares it void and ignores it), so the
     * port has nothing to compute: the call survives in the original only
     * for its side effect, and the port has no such side effect to have.
     */
}
