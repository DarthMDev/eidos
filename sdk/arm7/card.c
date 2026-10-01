/*
 * sdk/arm7/card.c -- the ARM7's Slot-1 card module, PXI tag 14.
 *
 * Why a stand-in: sdk/arm7/arm7.h.
 *
 * On tag 14 the ARM7 tells the ARM9 about the card slot:
 * CARDi_PulledOutCallback (src/matched/arm9_sdk/func_02077410.c) reads
 * command 0x11 as "the card was pulled out" and 2 as "the slot was reset",
 * and terminates on anything else.  The host's card cannot be pulled out,
 * so the ARM7 never sends either.  Words the ARM9 sends on this tag
 * (func_02077568.c) are taken and need no reply.
 *
 * The ARM7 still registers the tag, as its firmware does, so
 * PXI_IsCallbackReady(14, 1) is true.
 */

#include "arm7.h"
#include "pxi.h"

static void card_callback(int tag, u32 data, int err)
{
    (void)tag;
    (void)data;
    (void)err;
}

void bw_arm7_card_init(void)
{
    bw_pxi_arm7_set_callback(BW_PXI_TAG_CARD, card_callback);
}
