/*
 * sdk/arm7/ctrdg.c -- the ARM7's Slot-2 cartridge module, PXI tag 13.
 *
 * Why a stand-in: sdk/arm7/arm7.h.  Black is a DSi-enhanced card and has
 * no Slot-2 accessory; the stand-in is a console with nothing in Slot 2,
 * which is also every DSi.
 *
 * Shared memory.  The ARM7 firmware probes Slot 2 at power-on and leaves
 * the result in the module-info block at 0x02FFFC30.  The ARM9 reads its
 * first halfword, the module id: CTRDG_IsExistedAtInit
 * (src/matched/arm9_sdk/CTRDG_IsExistedAtInit.c) answers "no cartridge"
 * exactly when it is 0xFFFF, and CTRDG_IsExisting asks that first.  So
 * power-on writes 0xFFFF there and leaves the rest of the block zero.
 *
 * PXI.  The game initialises the module with CTRDG_DummyInit
 * (func_020911A0.c): CTRDGi_InitCommon, a callback that does nothing
 * (CTRDGi_DummyCallback, func_020911D4.c), and one word, 1, sent with
 * CTRDGi_SendtoPxi.  Nothing on the ARM9 waits for or reads an answer, so
 * the ARM7 takes the word and sends none.
 */

#include "arm7.h"
#include "dsmem.h"
#include "pxi.h"

#define CTRDG_MODULE_INFO   0x02FFFC30u

static u32 g_words;

static void ctrdg_callback(int tag, u32 data, int err)
{
    (void)tag;
    (void)data;
    if (!err)
        g_words++;
}

void bw_arm7_ctrdg_init(void)
{
    g_words = 0;
    *(volatile u16 *)bw_ds_ptr(CTRDG_MODULE_INFO) = 0xFFFF;
    bw_pxi_arm7_set_callback(BW_PXI_TAG_CTRDG, ctrdg_callback);
}

u32 bw_arm7_ctrdg_words(void)
{
    return g_words;
}
