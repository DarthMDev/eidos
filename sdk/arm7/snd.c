/*
 * sdk/arm7/snd.c -- the ARM7's sound driver, PXI tag 7: the transport.
 *
 * Why a stand-in: sdk/arm7/arm7.h.  The protocol, from the matched ARM9
 * side (src/matched/arm9_sdk/, snd_command.o):
 *
 *   - the ARM9 builds a list of SNDCommands (SND_AllocCommand,
 *     SND_PushCommand) out of sCommandArray, and SND_FlushCommand sends the
 *     address of the list's first command as one FIFO word (a non-zero
 *     word); RequestCommandProc (func_02089ED8.c) sends 0 to ask the ARM7
 *     to process what it has now;
 *   - the ARM7 answers through shared memory, not through the FIFO: after
 *     each list it advances SNDi_SharedWork->finishCommandTag, and
 *     SND_RecvCommandReply (func_02089978.c) reclaims one list each time
 *     that counter has moved past its own;
 *   - the only words the ARM7 sends back on this tag are sequence alarms,
 *     which PxiFifoCallback (func_02089E50.c) hands to SNDi_CallAlarmHandler
 *     as alarmNo | id << 8 (func_0208A010.c).
 *
 * This file is the wire end of that: it finds the list a FIFO word names,
 * hands every command in it to the installed driver (sdk/arm7/snd.h,
 * "BwArm7SndDriver"), and counts the list complete.  It executes each list
 * as soon as the word arrives, which is a legal ARM7 schedule (the ARM9
 * only ever waits for completion, never for its absence).  A list it cannot
 * find is still counted complete, so the ARM9 never waits on a list the
 * host cannot read; bw_arm7_snd_unresolved() says how many there were.
 *
 * The driver that plays the commands, sdk/arm7/snd_driver.c, depends on
 * engine/audio and the full address space; this file depends on neither,
 * so the PXI test links it with test doubles.  The eidos library builds
 * this file with BW_ARM7_SND_DRIVER=1, which installs that driver at ARM7
 * power-on when no other has been installed.
 */

#include <stddef.h>
#include <stdint.h>

#include "arm7.h"
#include "dsmem.h"
#include "pxi.h"
#include "snd.h"

/* A list longer than the pool is a cycle, not a list.                     */
#define MAX_LIST_LENGTH 4096u

/* The PXIFifoMessage data field is 26 bits (sdk/pxi.h).                   */
#define FIFO_DATA_MASK 0x03FFFFFFu

static struct {
    volatile u32 *finish_tag;
    u32 lists;
    u32 unresolved;
    u8 *pool;
    u32 pool_count;
    size_t pool_stride;
    int pool_bound;
} g_snd;

/* Installed drivers outlive an ARM7 reset: they are configuration.        */
static const BwArm7SndDriver *g_drv;

void bw_arm7_snd_set_driver(const BwArm7SndDriver *drv)
{
    g_drv = drv;
}

const BwArm7SndDriver *bw_arm7_snd_driver(void)
{
    return g_drv;
}

void bw_arm7_snd_bind_pool(void *commands, u32 count, size_t stride)
{
    g_snd.pool = (u8 *)commands;
    g_snd.pool_count = commands ? count : 0;
    g_snd.pool_stride = stride ? stride : sizeof(BwSndCommandWire);
    g_snd.pool_bound = commands != NULL;
}

u32 bw_arm7_snd_unresolved(void)
{
    return g_snd.unresolved;
}

/* The pool element whose host address has the FIFO word's 26 bits.       */
static BwSndCommandWire *find_list(u32 data)
{
    u8 *base;
    u32 count, i;
    size_t stride;

    if (g_snd.pool_bound) {
        base = g_snd.pool;
        count = g_snd.pool_count;
        stride = g_snd.pool_stride;
    } else {
        base = (u8 *)bw_ds_try_ptr(BW_SND_COMMAND_ARRAY_DS);
        count = BW_SND_COMMAND_NUM;
        stride = sizeof(BwSndCommandWire);
    }
    if (base == NULL)
        return NULL;
    for (i = 0; i < count; i++) {
        uintptr_t a = (uintptr_t)(base + (size_t)i * stride);
        if (((u32)a & FIFO_DATA_MASK) == (data & FIFO_DATA_MASK))
            return (BwSndCommandWire *)(void *)(base + (size_t)i * stride);
    }
    return NULL;
}

static void run_list(const BwSndCommandWire *c)
{
    u32 n = 0;
    BwSndCommand cmd;
    int k;

    for (; c != NULL && n < MAX_LIST_LENGTH; c = c->next, n++) {
        cmd.id = (u32)c->id;
        for (k = 0; k < 4; k++)
            cmd.arg[k] = (u64)c->arg[k];
        g_drv->command(&cmd);
    }
}

static void snd_callback(int tag, u32 data, int err)
{
    (void)tag;
    if (err)
        return;
    if (data != 0) {
        BwSndCommandWire *list = find_list(data);
        if (list == NULL)
            g_snd.unresolved++;
        else if (g_drv != NULL && g_drv->command != NULL)
            run_list(list);
        g_snd.lists++;
        if (g_drv != NULL && g_drv->list_done != NULL)
            g_drv->list_done(g_snd.lists);
    }
    if (g_snd.finish_tag)
        *g_snd.finish_tag = g_snd.lists;
}

void bw_arm7_snd_init(void)
{
    g_snd.lists = 0;
    g_snd.unresolved = 0;
#if defined(BW_ARM7_SND_DRIVER) && BW_ARM7_SND_DRIVER
    if (g_drv == NULL)
        bw_snd_driver_attach();
#endif
    if (g_drv != NULL && g_drv->reset != NULL)
        g_drv->reset();
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

void bw_arm7_snd_step(void)
{
    if (g_drv != NULL && g_drv->step != NULL)
        g_drv->step();
}
