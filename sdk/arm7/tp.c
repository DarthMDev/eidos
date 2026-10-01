/*
 * sdk/arm7/tp.c -- the ARM7's touch-panel module, PXI tag 6.
 *
 * Why a stand-in: sdk/arm7/arm7.h.  The protocol is the ARM9 side's, as
 * aletheia matches it (libspi tp.o):
 *
 *   TP_Init (func_0208A924.c) spins on PXI_IsCallbackReady(6, ARM7) before
 *   it installs its receiver, so the responder has to exist at power-on.
 *
 *   Requests are SPI-style words: bit 25 starts a packet, bit 24 ends it,
 *   bits 8-14 carry the command, the low byte (and bits 16-23, an index
 *   into the packet) the data:
 *     0x03000000                     sample once (command 0)
 *     0x02000100 | frequence,
 *     0x01010000 | vcount            start auto sampling (command 1)
 *     0x03000200                     stop auto sampling (command 2)
 *
 *   Replies (TPi_TpCallback.c): `command << 8 | result`, with bit 24 set
 *   for a command's completion (result 0 is success); command 0x10, with
 *   no end bit, is one auto-sampling result.  Before either the ARM7
 *   leaves the raw sample in shared memory at 0x02FFFFAA as two halfwords:
 *   x in bits 0-11, y in 12-23, touch in 24, validity in 25-26.
 *
 * The panel itself is the host's: bw_arm7_tp_set_touch says whether the
 * stylus is down and where (in the panel's raw 12-bit units).  Nothing
 * touches it by default.  Auto sampling delivers `frequence` samples per
 * frame; the port's frame driver calls bw_arm7_tp_frame() once a frame for
 * that (demo/eidos_boot.c), since the ARM7 stand-in has no clock of its own.
 */

#include "arm7.h"
#include "dsmem.h"
#include "pxi.h"

#define TP_RAW_POINT     0x02FFFFAAu
#define TP_START_BIT     0x02000000u
#define TP_END_BIT       0x01000000u
#define TP_CMD_SAMPLE    0u
#define TP_CMD_AUTO_ON   1u
#define TP_CMD_AUTO_OFF  2u
#define TP_CMD_AUTO_DATA 0x10u

static struct {
    u32 packet[2];       /* the words of the packet being received          */
    u32 words;
    int auto_on;
    u32 frequence;       /* samples per frame while auto sampling            */
    int down;
    u32 x;
    u32 y;
} g_tp;

static void tp_write_raw(void)
{
    volatile u16 *raw = (volatile u16 *)bw_ds_ptr(TP_RAW_POINT);
    u32 v = (g_tp.x & 0xFFFu) | ((g_tp.y & 0xFFFu) << 12);

    if (g_tp.down) {
        v |= 1u << 24;          /* touch; validity 0, both axes valid */
    }
    raw[0] = (u16)v;
    raw[1] = (u16)(v >> 16);
}

static void tp_reply(u32 command, u32 result)
{
    (void)bw_pxi_arm7_send(BW_PXI_TAG_TOUCHPANEL,
                           TP_END_BIT | (command << 8) | (result & 0xFFu), 0);
}

static void tp_command(void)
{
    u32 command = (g_tp.packet[0] >> 8) & 0x7Fu;

    switch (command) {
    case TP_CMD_SAMPLE:
        tp_write_raw();
        tp_reply(command, 0);
        break;
    case TP_CMD_AUTO_ON:
        g_tp.frequence = g_tp.packet[0] & 0xFFu;
        if (g_tp.frequence == 0) {
            g_tp.frequence = 1;
        }
        g_tp.auto_on = 1;
        tp_reply(command, 0);
        break;
    case TP_CMD_AUTO_OFF:
        g_tp.auto_on = 0;
        tp_reply(command, 0);
        break;
    default:
        tp_reply(command, 1);   /* TP_RESULT_INVALID: the ARM9 stops */
        break;
    }
}

static void tp_callback(int tag, u32 data, int err)
{
    (void)tag;
    if (err) {
        return;
    }
    if (data & TP_START_BIT) {
        g_tp.words = 0;
    }
    if (g_tp.words < 2) {
        g_tp.packet[g_tp.words++] = data;
    }
    if (data & TP_END_BIT) {
        tp_command();
        g_tp.words = 0;
    }
}

void bw_arm7_tp_init(void)
{
    g_tp.words = 0;
    g_tp.auto_on = 0;
    g_tp.frequence = 0;
    g_tp.down = 0;
    g_tp.x = 0;
    g_tp.y = 0;
    tp_write_raw();
    bw_pxi_arm7_set_callback(BW_PXI_TAG_TOUCHPANEL, tp_callback);
}

void bw_arm7_tp_set_touch(int down, u32 x, u32 y)
{
    g_tp.down = down != 0;
    g_tp.x = x;
    g_tp.y = y;
}

void bw_arm7_tp_frame(void)
{
    u32 i;

    if (!g_tp.auto_on) {
        return;
    }
    for (i = 0; i < g_tp.frequence; i++) {
        tp_write_raw();
        (void)bw_pxi_arm7_send(BW_PXI_TAG_TOUCHPANEL, TP_CMD_AUTO_DATA << 8, 0);
    }
}
