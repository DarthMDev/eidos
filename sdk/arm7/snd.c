/*
 * sdk/arm7/snd.c -- the ARM7's sound driver, PXI tag 7, as a stub that
 * completes every command list at once.
 *
 * Why a stand-in: sdk/arm7/arm7.h.  The port plays audio with its own
 * engine (engine/audio/), so this is only the handshake the ARM9's
 * snd_command.o needs, from the matched ARM9 side
 * (src/matched/arm9_sdk/):
 *
 *   - SND_FlushCommand sends the address of a command list (a non-zero
 *     word); RequestCommandProc (func_02089ED8.c) sends 0 to ask the ARM7
 *     to process what it has now;
 *   - the ARM7 answers through shared memory, not through the FIFO: after
 *     each list it advances SNDi_SharedWork->finishCommandTag, and
 *     SND_RecvCommandReply (func_02089978.c) reclaims one list each time
 *     that counter has moved past its own.
 *
 * So the stand-in counts the lists it has taken and, when bound, writes the
 * count to the finish tag.  The address of the shared work reaches the real
 * ARM7 inside the first command list (SND_CommandInit's command 0x1D), which
 * a host cannot follow through a 26-bit FIFO word; bw_arm7_snd_bind is the
 * port's way to hand it over instead.  The ARM7's own words on this tag
 * (sequence alarms, SNDi_CallAlarmHandler) are never sent.
 */

#include <stddef.h>

#include "arm7.h"
#include "pxi.h"

static struct {
    volatile u32 *finish_tag;
    u32 lists;
} g_snd;

static void snd_callback(int tag, u32 data, int err)
{
    (void)tag;
    if (err)
        return;
    if (data != 0)
        g_snd.lists++;
    if (g_snd.finish_tag)
        *g_snd.finish_tag = g_snd.lists;
}

void bw_arm7_snd_init(void)
{
    g_snd.lists = 0;
    bw_pxi_arm7_set_callback(BW_PXI_TAG_SOUND, snd_callback);
}

void bw_arm7_snd_bind(volatile u32 *finish_command_tag)
{
    g_snd.finish_tag = finish_command_tag;
    if (g_snd.finish_tag)
        *g_snd.finish_tag = g_snd.lists;
}

u32 bw_arm7_snd_lists(void)
{
    return g_snd.lists;
}
