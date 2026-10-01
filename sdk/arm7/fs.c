/*
 * sdk/arm7/fs.c -- the ARM7's side of the Slot-1 backup commands, PXI tag
 * 11 (the SDK's PXI_FIFO_TAG_FS, which libcard uses for its requests).
 *
 * Why a stand-in: sdk/arm7/arm7.h.  The protocol, from CARDi_Request
 * (src/matched/arm9_sdk/func_020763F0.c) and CARDi_OnFifoRecv
 * (func_020763BC.c):
 *
 *   - the ARM9 waits for PXI_IsCallbackReady(11, 1), then sends the request
 *     type as one word with the error bit SET (libcard uses the bit as its
 *     own "this is a request" flag);
 *   - for request 0 (initialise) a second word follows, also with the bit
 *     set: the address of the shared command block, cardi_arg;
 *   - the ARM9 sleeps until a word on tag 11 with the error bit set comes
 *     back (CARDi_OnFifoRecv ignores the data), then reads the command
 *     block's result field, 0 = success.
 *
 * The stand-in acknowledges every request and leaves the command block
 * untouched, so its result stays at the 0 CARDi_InitCommand cleared it to.
 * That is enough for the boot, which only initialises the module.  Real
 * backup (the save file) needs the command block, and the address in the
 * second word is a host pointer squeezed into 26 bits, which does not
 * survive the trip on a 64-bit host; the follow-up is for the port to bind
 * cardi_arg by name, as sdk/arm7/snd.c binds the sound shared work.
 */

#include "arm7.h"
#include "pxi.h"

static struct {
    u32 requests;
    u32 last;
    int expect_arg;
} g_fs;

static void fs_callback(int tag, u32 data, int err)
{
    (void)tag;
    (void)err;
    if (g_fs.expect_arg) {
        g_fs.expect_arg = 0;            /* the command block's address      */
    } else {
        g_fs.requests++;
        g_fs.last = data;
        if (data == 0) {
            g_fs.expect_arg = 1;
            return;
        }
    }
    (void)bw_pxi_arm7_send(BW_PXI_TAG_FS, g_fs.last, 1);
}

void bw_arm7_fs_init(void)
{
    g_fs.requests = 0;
    g_fs.last = 0;
    g_fs.expect_arg = 0;
    bw_pxi_arm7_set_callback(BW_PXI_TAG_FS, fs_callback);
}

u32 bw_arm7_fs_requests(void)
{
    return g_fs.requests;
}

u32 bw_arm7_fs_last_request(void)
{
    return g_fs.last;
}
