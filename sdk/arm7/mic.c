/*
 * sdk/arm7/mic.c -- the ARM7's microphone, PXI tag 9: present, silent, and
 * unable to stream.
 *
 * Why a stand-in: sdk/arm7/arm7.h.  The game's boot calls MIC_Init
 * (arm9/func_02006CBC.c -> arm9_sdk/func_0208B24C.c), which spins until the
 * ARM7 has registered this tag, so the tag has to be served even though the
 * port has no microphone yet.
 *
 * The protocol is the SPI library's (sdk/arm7/nvram.c describes the
 * framing): bit 25 starts a command, bit 24 ends it, the command byte is in
 * bits 8-15 of the first word.  MicCommonCallback (func_0208B548.c) reads
 * a reply's command from bits 8-14 and its result from bits 0-7 (0 success,
 * 1 invalid command, 2 illegal parameter, 3 illegal status, 4 busy), and
 * for the one-shot command 0x40 takes the sample from 0x02FFFF94
 * (HW_MIC_SAMPLING_DATA).  The commands the ARM9 sends (mic.o): 0x40
 * one-shot sample, 0x41 start auto sampling (MicStartAutoSampling,
 * func_0208B650.c: a buffer, a size and a period), 0x42 stop it, 0x43
 * adjust it, and the limited-sampling commands after it.
 *
 * What the port answers until it has an input path for a microphone:
 *   - one-shot: success, and silence in HW_MIC_SAMPLING_DATA, which is the
 *     midpoint for the unsigned sample types and 0 for the signed ones (the
 *     type is bits 0-2 of the first word: 0 8-bit, 1 12-bit, 2 signed 8,
 *     3 signed 12, 5 12-bit unfiltered, 7 signed 12 unfiltered);
 *   - start or adjust auto or limited sampling: "illegal status" (3), the
 *     SDK's answer when the ARM7 cannot start sampling, which reaches the
 *     caller as MIC_RESULT_ILLEGAL_STATUS: no sampling runs, no buffer is
 *     written, the full callback never fires;
 *   - stop: success (nothing is running, and the stop leaves it so);
 *   - anything else: invalid command (1).
 * Every command is answered, so no caller waits for a reply that never
 * comes.
 */

#include <string.h>

#include "arm7.h"
#include "dsmem.h"
#include "pxi.h"

#define SPI_START   0x02000000u
#define SPI_END     0x01000000u

#define MIC_SAMPLING_DATA   0x02FFFF94u

enum {
    MIC_ONE_SAMPLING = 0x40,
    MIC_AUTO_START = 0x41,
    MIC_AUTO_STOP = 0x42,
    MIC_AUTO_ADJUST = 0x43,
    MIC_LIMITED_START = 0x44,
    MIC_LIMITED_STOP = 0x45,
    MIC_LIMITED_ADJUST = 0x46
};

enum { RES_SUCCESS = 0, RES_INVALID_COMMAND = 1, RES_ILLEGAL_STATUS = 3 };

static struct {
    int in_command;
    u32 command;
    u32 first;
    u32 commands;
} g_mic;

static int run(void)
{
    switch (g_mic.command) {
    case MIC_ONE_SAMPLING: {
        /* Silence: the midpoint of an unsigned sample, 0 for a signed one. */
        u32 type = g_mic.first & 7u;
        u16 v = 0;

        if (type == 0)
            v = 0x80;
        else if (type == 1 || type == 4)
            v = 0x800;
        memcpy(bw_ds_ptr(MIC_SAMPLING_DATA), &v, 2);
        return RES_SUCCESS;
    }
    case MIC_AUTO_START:
    case MIC_AUTO_ADJUST:
    case MIC_LIMITED_START:
    case MIC_LIMITED_ADJUST:
        return RES_ILLEGAL_STATUS;
    case MIC_AUTO_STOP:
    case MIC_LIMITED_STOP:
        return RES_SUCCESS;
    default:
        return RES_INVALID_COMMAND;
    }
}

static void mic_callback(int tag, u32 data, int err)
{
    (void)tag;
    if (err)
        return;
    if (data & SPI_START) {
        g_mic.in_command = 1;
        g_mic.command = (data >> 8) & 0xFFu;
        g_mic.first = data & 0xFFu;
    }
    if (!(data & SPI_END) || !g_mic.in_command)
        return;
    g_mic.in_command = 0;
    g_mic.commands++;
    (void)bw_pxi_arm7_send(BW_PXI_TAG_MIC,
                           SPI_START | SPI_END | ((g_mic.command | 0x80u) << 8)
                           | (u32)run(), 0);
}

void bw_arm7_mic_init(void)
{
    g_mic.in_command = 0;
    g_mic.commands = 0;
    bw_pxi_arm7_set_callback(BW_PXI_TAG_MIC, mic_callback);
}

u32 bw_arm7_mic_commands(void)
{
    return g_mic.commands;
}
