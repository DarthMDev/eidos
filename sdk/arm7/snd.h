/*
 * sdk/arm7/snd.h -- the ARM7's sound driver, as the ARM9's SND library sees
 * it: command lists over PXI tag 7, answers in shared memory.
 *
 * Two files, split so the transport has no dependency on the engine:
 *
 *   snd.c         the PXI responder.  It turns a FIFO word into the command
 *                 list the ARM9 built, hands each command to the installed
 *                 driver, and advances finishCommandTag after every list.
 *                 With no driver it only counts (what test_pxi links).
 *   snd_driver.c  the driver: executes the commands on engine/audio and
 *                 keeps SNDSharedWork current (player status, channel
 *                 status, tick counters), fires the SND alarms back to the
 *                 ARM9, and fills SNDDriverInfo on request.
 *
 * Every layout here is the ARM9 SDK's own, read from the DWARF of
 * libsnd.TWL.HYB.a (snd_command.o, snd_work.o, snd_bank.o) with aletheia's
 * tools/sdkobj.py, and from the matched ARM9 senders (src/matched/arm9_sdk/,
 * named where used).  Nothing is taken from ARM7 code.
 */

#ifndef BW_PORT_ARM7_SND_H
#define BW_PORT_ARM7_SND_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The wire                                                            */
/* ------------------------------------------------------------------ */

/* enum SNDCommandID (snd_command.o DWARF).                                */
enum {
    BW_SND_CMD_START_SEQ             = 0,
    BW_SND_CMD_STOP_SEQ              = 1,
    BW_SND_CMD_PREPARE_SEQ           = 2,
    BW_SND_CMD_START_PREPARED_SEQ    = 3,
    BW_SND_CMD_PAUSE_SEQ             = 4,
    BW_SND_CMD_SKIP_SEQ              = 5,
    BW_SND_CMD_PLAYER_PARAM          = 6,
    BW_SND_CMD_TRACK_PARAM           = 7,
    BW_SND_CMD_MUTE_TRACK            = 8,
    BW_SND_CMD_ALLOCATABLE_CHANNEL   = 9,
    BW_SND_CMD_PLAYER_LOCAL_VAR      = 10,
    BW_SND_CMD_PLAYER_GLOBAL_VAR     = 11,
    BW_SND_CMD_START_TIMER           = 12,
    BW_SND_CMD_STOP_TIMER            = 13,
    BW_SND_CMD_SETUP_CHANNEL_PCM     = 14,
    BW_SND_CMD_SETUP_CHANNEL_PSG     = 15,
    BW_SND_CMD_SETUP_CHANNEL_NOISE   = 16,
    BW_SND_CMD_SETUP_CAPTURE         = 17,
    BW_SND_CMD_SETUP_ALARM           = 18,
    BW_SND_CMD_CHANNEL_TIMER         = 19,
    BW_SND_CMD_CHANNEL_VOLUME        = 20,
    BW_SND_CMD_CHANNEL_PAN           = 21,
    BW_SND_CMD_SURROUND_DECAY        = 22,
    BW_SND_CMD_MASTER_VOLUME         = 23,
    BW_SND_CMD_MASTER_PAN            = 24,
    BW_SND_CMD_OUTPUT_SELECTOR       = 25,
    BW_SND_CMD_LOCK_CHANNEL          = 26,
    BW_SND_CMD_UNLOCK_CHANNEL        = 27,
    BW_SND_CMD_STOP_UNLOCKED_CHANNEL = 28,
    BW_SND_CMD_SHARED_WORK           = 29,
    BW_SND_CMD_INVALIDATE_SEQ        = 30,
    BW_SND_CMD_INVALIDATE_BANK       = 31,
    BW_SND_CMD_INVALIDATE_WAVE       = 32,
    BW_SND_CMD_READ_DRIVER_INFO      = 33,
    BW_SND_CMD_COUNT
};

/*
 * struct SNDCommand as the matched ARM9 files declare it (SND_FlushCommand.c,
 * func_02089B00.c, func_02089804.c): a next pointer, the id, four argument
 * words in the files' own `u32`, which they typedef as unsigned long.  The
 * port declares it the same way so that whatever the host's widths, the
 * stride and the field offsets are the ones the matched code wrote with:
 * 24 bytes on the DS and on every ILP32 host.  Pointer arguments are
 * `(u32)pointer` casts in the matched senders, so they carry a whole host
 * pointer exactly where pointers are 32 bits wide or unsigned long is 64
 * (sdk/arm7/snd_driver.c, "Addresses in arguments").
 */
typedef struct BwSndCommandWire {
    struct BwSndCommandWire *next;
    int id;
    unsigned long arg[4];
} BwSndCommandWire;

/* One command as the transport hands it to a driver.                      */
typedef struct BwSndCommand {
    u32 id;
    u64 arg[4];
} BwSndCommand;

/* ------------------------------------------------------------------ */
/* The transport (snd.c)                                               */
/* ------------------------------------------------------------------ */

/*
 * A driver.  Every hook may be NULL.  `reset` runs at ARM7 power-on
 * (bw_arm7_snd_init); `command` runs once per command, in list order, on the
 * thread that delivered the FIFO word; `list_done` after each whole list
 * with the new completion count; `step` from bw_arm7_step(), the game
 * thread's service point, which is where a driver publishes state the
 * ARM9 polls and sends the words it owes the ARM9.  The driver stays
 * installed across ARM7 resets.
 */
typedef struct BwArm7SndDriver {
    void (*reset)(void);
    void (*command)(const BwSndCommand *cmd);
    void (*list_done)(u32 lists);
    void (*step)(void);
} BwArm7SndDriver;

void bw_arm7_snd_set_driver(const BwArm7SndDriver *drv);
const BwArm7SndDriver *bw_arm7_snd_driver(void);

/*
 * The command pool the ARM9 builds its lists in.  A FIFO word carries only
 * 26 data bits of the list's address (PXIFifoMessage, pxi_fifo.o's DWARF),
 * which on a host is the low 26 bits of a host pointer; the list is the
 * pool element whose address has those low bits.  Every list the matched
 * SND code sends is made of elements of snd_command.o's sCommandArray (256
 * SNDCommands, SND_CommandInit's free-list build), which the game's data
 * definitions register at its DS address, 0x02151740 (SND_CommandInit's
 * relocations); that is the default pool when none is bound.  `stride` 0
 * means sizeof(BwSndCommandWire).  NULL unbinds.
 */
#define BW_SND_COMMAND_ARRAY_DS   0x02151740u
#define BW_SND_COMMAND_NUM        256u
void bw_arm7_snd_bind_pool(void *commands, u32 count, size_t stride);

/* Lists the transport could not find in the pool (completed unexecuted).  */
u32 bw_arm7_snd_unresolved(void);

/* The game thread's service point; bw_arm7_step() calls it.               */
void bw_arm7_snd_step(void);

/* ------------------------------------------------------------------ */
/* The driver (snd_driver.c)                                           */
/* ------------------------------------------------------------------ */

/* How much of a sequence the driver prepares (engine/audio's max_seconds):
 * the note list and the level probe stop there, and a looping sequence
 * restarts from its top at that point or at its own end, whichever is first. */
#define BW_SND_SEQ_MAX_SECONDS 180.0

/* Install the engine/audio driver into the transport (idempotent).  The
 * eidos library does this at ARM7 power-on unless a port installed its own
 * driver first; tests call it directly. */
void bw_snd_driver_attach(void);
const BwArm7SndDriver *bw_snd_driver_ops(void);

/*
 * Run the driver's clock by `frames` output frames at BW_AUDIO_RATE and,
 * when `out` is non-NULL, write that many interleaved stereo s16 frames of
 * every playing sequence mixed.  This is the whole of what the platform's
 * audio callback calls; a host with no audio device calls it with NULL
 * from its frame loop instead, so sequences still end, tick counters still
 * move and alarms still fire.  Never allocates and never blocks for longer
 * than a command takes to execute.
 */
void bw_snd_render(s16 *out, u32 frames);

/*
 * How the driver turns a pointer-valued argument (seqBase, a bank, a wave
 * archive, the shared work, an invalidated range) into host memory.
 * `value` is the argument as the ARM9 wrote it; the result is the host
 * address, or NULL, and *avail the bytes known to be readable from there
 * (0 when unknown, in which case the driver reads only what the file's own
 * header says is there).  NULL restores the default (snd_driver.c,
 * "Addresses in arguments"); a port with its own address model, or a mod
 * that serves sound data from somewhere else, installs its own.
 */
typedef void *(*BwSndResolver)(u64 value, size_t *avail);
void bw_snd_set_resolver(BwSndResolver r);
void *bw_snd_resolve(u64 value, size_t *avail);

/* Bind the SNDSharedWork the driver writes (the SHARED_WORK command does
 * this from the ARM9's own pointer; NULL unbinds). */
void bw_snd_bind_shared(void *shared_work);

/* What the driver has seen and done, for tests and diagnostics.           */
typedef struct BwSndStats {
    u32 commands;               /* commands executed                       */
    u32 by_id[BW_SND_CMD_COUNT];
    u32 ignored;                /* recognised, deliberately not modelled   */
    u32 bad_address;            /* a pointer argument that did not resolve */
    u32 bad_sequence;           /* a sequence the engine refused           */
    u32 alarms_fired;
    u32 alarms_dropped;         /* more firings between two steps than fit */
    u64 frames;                 /* driver clock, in BW_AUDIO_RATE frames   */
} BwSndStats;
void bw_snd_stats(BwSndStats *out);

/* A player's state as the driver holds it (0-15).                         */
typedef struct BwSndPlayerState {
    int    active, prepared, paused;
    int    loops;               /* restarts at its end (a looping SSEQ)    */
    u64    tick;
    s16    player_fader;        /* SNDPlayer.extFader, 1/10 dB             */
    u16    tempo_ratio;         /* SNDPlayer.tempo_ratio, 256 = 1.0        */
    u8     volume;              /* SNDPlayer.volume                        */
    u8     prio;                /* SNDPlayer.prio                          */
    double gain;                /* what the mix applies                    */
} BwSndPlayerState;
int bw_snd_player_state(int player, BwSndPlayerState *out);

/* 0-127, the MASTER_VOLUME command; 127 at power-on.                       */
int bw_snd_master_volume(void);
/* Channels the ARM9 has locked (LOCK_CHANNEL / UNLOCK_CHANNEL).           */
u32 bw_snd_locked_channels(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_ARM7_SND_H */
