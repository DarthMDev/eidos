/*
 * sdk/arm7/snd_seq.h -- the ARM7 driver's sequencer: 16 players, a shared
 * pool of 32 tracks, 16 extended channels allocated by priority over the
 * hardware (sdk/arm7/snd_hw.h), envelopes, LFOs, sweeps and the SSEQ
 * interpreter in real time.
 *
 * The shapes are the SDK's own, read from the DWARF of libsnd.TWL.HYB.a's
 * snd_work.o with aletheia's tools/sdkobj.py: struct SNDWork is
 * channel[16] (SNDExChannel, 84 bytes), player[16] (SNDPlayer, 36 bytes),
 * track[32] (SNDTrack, 64 bytes, a pool the players allocate from) and
 * alarm[8].  The player and track parameters are kept as byte images at the
 * DWARF offsets, so PLAYER_PARAM and TRACK_PARAM are the plain stores the
 * ARM9 senders (SNDi_SetPlayerParam, SNDi_SetTrackParam) mean them to be and
 * SND_ReadDriverInfo gets the same bytes back.  The arithmetic tables are
 * snd_util.o's (bw_snd_calc_timer and friends below).  What the ARM7 does
 * with them is public NDS sound driver documentation; snd_seq.c names each
 * rule where it applies.
 *
 * Every function here runs under sdk/arm7/snd_driver.c's lock.
 */

#ifndef BW_PORT_ARM7_SND_SEQ_H
#define BW_PORT_ARM7_SND_SEQ_H

#include <stddef.h>
#include <stdint.h>

#include "audio.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BW_SNDSEQ_PLAYERS   16
#define BW_SNDSEQ_TRACKS    32      /* SNDWork.track[32], shared          */
#define BW_SNDSEQ_CHANNELS  16

/* One driver frame: 64 * 2728 bus cycles (33513982 Hz), about 5.21 ms. */
#define BW_SNDSEQ_FRAME_CYCLES (64u * 2728u)

/* SND_SEQ_MUTE_* (snd_interface.o DWARF).                                 */
enum {
    BW_SNDSEQ_MUTE_OFF = 0,
    BW_SNDSEQ_MUTE_NO_STOP = 1,
    BW_SNDSEQ_MUTE_RELEASE = 2,
    BW_SNDSEQ_MUTE_STOP = 3
};

/* SNDPlayer / SNDTrack field offsets (snd_work.o DWARF).                  */
#define BW_SNDPL_PRIO         0x04
#define BW_SNDPL_VOLUME       0x05
#define BW_SNDPL_EXT_FADER    0x06
#define BW_SNDPL_TRACKS       0x08
#define BW_SNDPL_TEMPO        0x18
#define BW_SNDPL_TEMPO_RATIO  0x1A
#define BW_SNDPL_TEMPO_COUNTER 0x1C
#define BW_SNDPL_SIZE         0x24

#define BW_SNDTR_PAN_RANGE    0x01
#define BW_SNDTR_PRG          0x02
#define BW_SNDTR_VOLUME       0x04
#define BW_SNDTR_VOLUME2      0x05
#define BW_SNDTR_PITCH_BEND   0x06
#define BW_SNDTR_BEND_RANGE   0x07
#define BW_SNDTR_PAN          0x08
#define BW_SNDTR_EXT_PAN      0x09
#define BW_SNDTR_EXT_FADER    0x0A
#define BW_SNDTR_EXT_PITCH    0x0C
#define BW_SNDTR_ATTACK       0x0E
#define BW_SNDTR_DECAY        0x0F
#define BW_SNDTR_SUSTAIN      0x10
#define BW_SNDTR_RELEASE      0x11
#define BW_SNDTR_PRIO         0x12
#define BW_SNDTR_TRANSPOSE    0x13
#define BW_SNDTR_PORTA_KEY    0x14
#define BW_SNDTR_PORTA_TIME   0x15
#define BW_SNDTR_SWEEP_PITCH  0x16
#define BW_SNDTR_MOD_TARGET   0x18
#define BW_SNDTR_MOD_SPEED    0x19
#define BW_SNDTR_MOD_DEPTH    0x1A
#define BW_SNDTR_MOD_RANGE    0x1B
#define BW_SNDTR_MOD_DELAY    0x1C
#define BW_SNDTR_CHANNEL_MASK 0x1E
#define BW_SNDTR_PARAMS       0x20  /* the byte image covers 0x00-0x1F  */
#define BW_SNDTR_SIZE         0x40

/* The bank a sequence plays with: the SBNK and the SWAR in each of its
 * four wave-archive slots (empty blob: none).                             */
typedef struct {
    BwBlob sbnk;
    BwBlob arc[4];
} BwSndSeqBank;

/* ------------------------------------------------------------------ */
/* snd_util.o arithmetic                                               */
/* ------------------------------------------------------------------ */

/* SND_CalcTimer: `base_timer` scaled by 2^(-pitch / 768), pitch in 1/64
 * semitones, clamped to 0x10-0xFFFF (the listing of the ARM9 copy).       */
u16 bw_snd_calc_timer(int base_timer, int pitch);
/* SND_CalcChannelVolume (func_0208A24C.c): volume | shift << 8.           */
u16 bw_snd_calc_channel_volume(int db);
/* SNDi_DecibelSquareTable[i], i 0-127, tenths of a decibel.               */
int bw_snd_decibel_square(int i);
/* The quarter-wave LFO sine, index 0-127 over one cycle, -127..127.       */
int bw_snd_sin_idx(int i);
/* The SBNK envelope bytes as rates.                                       */
int bw_snd_attack_rate(int attack);
int bw_snd_fall_rate(int decay_or_release);
/* Sums over the generated tables, for the test that holds them to the
 * SDK's: pitch, decibel-square, volume.                                   */
void bw_snd_table_sums(s64 sum[3], s64 weighted[3]);

/* ------------------------------------------------------------------ */
/* The sequencer                                                       */
/* ------------------------------------------------------------------ */

void bw_sndseq_reset(void);
/* One driver frame: every player's ticks, then every channel's envelope,
 * LFO and sweep, written to the hardware.                                 */
void bw_sndseq_frame(void);

/* PREPARE_SEQ: `seq` is the sequence payload from seqBase, `offset` where
 * track 0 starts.  The player is left prepared, not started.             */
void bw_sndseq_prepare(int player, BwBlob seq, u32 offset, const BwSndSeqBank *bank);
void bw_sndseq_start(int player);
void bw_sndseq_stop(int player);
void bw_sndseq_pause(int player, int pause);
void bw_sndseq_skip(int player, u32 ticks);
void bw_sndseq_player_param(int player, u32 offset, u32 value, u32 size);
void bw_sndseq_track_param(int player, u32 tracks, u32 offset, u32 value, u32 size);
void bw_sndseq_mute(int player, u32 tracks, int mode);
void bw_sndseq_allocatable(int player, u32 tracks, u32 channels);
/* Variables 0-15 are the player's own, 16-31 the global ones (SSEQ). */
void bw_sndseq_set_var(int player, int var, s16 value);
s16  bw_sndseq_var(int player, int var);

/* Channels the ARM9 locked are never allocated; locking drops the notes
 * playing on them.  STOP_UNLOCKED_CHANNEL drops the unlocked ones.        */
void bw_sndseq_lock(u32 channels);
void bw_sndseq_unlock(u32 channels);
u32  bw_sndseq_locked(void);
void bw_sndseq_stop_unlocked(u32 channels);
/* Seq, bank or wave data in [lo, hi) is about to be reused: stop what
 * reads it.  kind 0 sequence, 1 bank, 2 wave.                             */
void bw_sndseq_invalidate(int kind, uintptr_t lo, uintptr_t hi);

/* Inspection.                                                             */
typedef struct {
    int active, prepared, paused;
    int loops;                  /* jumped back or repeated a loop          */
    u64 tick;                   /* ticks played since the start            */
    u8  reg[BW_SNDPL_SIZE];     /* the SNDPlayer image                     */
    u32 track_mask;             /* track numbers allocated                 */
    u32 channel_mask;           /* channels its notes hold                 */
    const u8 *seq;              /* what it reads, for invalidation         */
    BwSndSeqBank bank;
} BwSndSeqPlayerInfo;
int  bw_sndseq_player_info(int player, BwSndSeqPlayerInfo *out);
u32  bw_sndseq_status(void);            /* bit n: player n active         */
u32  bw_sndseq_channel_status(void);    /* bit n: channel n held by a note */
u32  bw_sndseq_dropped_notes(void);     /* note-ons that found no channel  */

/* SNDWork in the DS layout for SND_ReadDriverInfo, `work_address` being
 * the DS address the ARM9 reads it at (the channel and track links are
 * written relative to it).  `work` must hold 0xF80 bytes (the alarms
 * are the driver's).                                                      */
void bw_sndseq_write_work(u8 *work, u32 work_address);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_ARM7_SND_SEQ_H */
