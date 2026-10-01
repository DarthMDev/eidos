/*
 * sdk/arm7/snd_driver.c -- the ARM7's sound driver, the part that plays:
 * SND commands executed on engine/audio, and the shared work the ARM9 polls.
 *
 * Why a stand-in: sdk/arm7/arm7.h.  sdk/arm7/snd.c hands this file one
 * command at a time (sdk/arm7/snd.h).  What each command means is read off
 * the matched ARM9 senders (src/matched/arm9_sdk/, snd_interface.o) and the
 * SDK's DWARF (enum SNDCommandID, struct SNDPlayer, SNDTrack, SNDWork,
 * SNDDriverInfo, SNDSharedWork, SNDBankData, SNDWaveArc), nothing from ARM7
 * code:
 *
 *   PREPARE_SEQ (player, seqBase, seqOffset, bank)   func_02089350.c
 *   START_SEQ   (the same four)                      enum only, = prepare+start
 *   START_PREPARED_SEQ (player), STOP_SEQ (player)   func_02089330.c
 *   PAUSE_SEQ   (player, flag)                       func_02089398.c
 *   PLAYER_PARAM (player, offset, data, size)        func_020897B4.c: a store
 *               into the ARM7's SNDPlayer; SND_SetPlayerVolume is offset 6
 *               (extFader), SND_SetPlayerTempoRatio 0x1A, the channel
 *               priority 4 (the asm veneers of the same name)
 *   TRACK_PARAM (player | size << 24, tracks, offset, data)  func_020897DC.c:
 *               the same into each SNDTrack; volume 0xA (extFader), pitch
 *               0xC, pan 9, modulation depth 0x1A and speed 0x19
 *   MUTE_TRACK  (player, tracks, mode)               func_02089580.c
 *   SETUP_ALARM (alarmNo, tick, period, id)          func_02089540.c
 *   START_TIMER / STOP_TIMER (channels, captures, alarms, flags)
 *   MASTER_VOLUME (volume)                           func_0208970C.c
 *   LOCK_CHANNEL / UNLOCK_CHANNEL (channels, flags)  func_020895D8.c / F8
 *   SHARED_WORK (SNDSharedWork *)                    SND_CommandInit
 *   INVALIDATE_SEQ/BANK/WAVE (start, end)            func_020896AC.c
 *   READ_DRIVER_INFO (SNDDriverInfo *)               func_02089794.c
 *
 * ------------------------------------------------------------------------
 * The sequencer
 * ------------------------------------------------------------------------
 * A prepared sequence is an engine/audio player over a BwAudioSeqSource
 * whose bytes are the game's own: seqBase and seqOffset as the ARM9 sent
 * them (offsets measured from seqBase, the reading engine/audio/audio.h
 * documents for the command path), the SBNK the bank argument points at,
 * and the four SWARs its waveArcLink[] names (SND_AssignWaveArc,
 * func_0208A2BC.c, links them).  Nothing is copied, so a mod that loads a
 * new sequence, bank or wave archive into the game's memory plays without
 * anything here knowing about it, and there is no table of sequences at
 * all.  The source's `volume` is 0 (unity): the ARM9 sends the SEQ record's
 * volume as a player fader instead.
 *
 * Levels.  extFader is in tenths of a decibel: SND_CalcChannelVolume
 * (func_0208A24C.c) clamps a level to -723..0 and steps its hardware shift
 * at -60, -120 and -240, i.e. one halving (6 dB) per 60.  So a fader f is
 * an amplitude of 10^(f / 200); 0 is exactly 1.0 and is not multiplied in,
 * so a sequence nobody turned down is bit-identical to engine/audio driven
 * directly.  MASTER_VOLUME is linear, 127 = 1.0, likewise skipped at 127.
 *
 * ------------------------------------------------------------------------
 * The shared work
 * ------------------------------------------------------------------------
 * SNDSharedWork is the matched code's own declaration (func_0208A1D0.c),
 * reproduced below so both sides agree on every host.  The driver keeps
 * playerStatus (bit n while player n is active), channelStatus (the engine
 * voices sounding, OR-ed over players: the engine gives each player its own
 * sixteen, where the DS shares sixteen between all of them) and each
 * player's tickCounter (sequence ticks played, 48 to the quarter).  It
 * writes them at bw_arm7_step (the game thread's service point) and after
 * every command list, so the ARM9 sees a START before it sees the list
 * complete, and a finished sequence after the next step.
 *
 * ------------------------------------------------------------------------
 * Time
 * ------------------------------------------------------------------------
 * The driver's clock is the output: bw_snd_render() advances it by the
 * frames it mixes, at BW_AUDIO_RATE.  Sequences end, ticks move and alarms
 * fire on that clock, so a stream's refill alarm stays in step with the
 * samples actually played.  An alarm's tick and period are OS ticks, the
 * bus clock / 64 (sdk/os_tick.c), converted exactly from frames.  A firing
 * is the word alarmNo | id << 8 on tag 7 (func_0208A010.c), sent at the
 * next step on the game thread.
 *
 * ------------------------------------------------------------------------
 * Addresses in arguments
 * ------------------------------------------------------------------------
 * The matched senders store pointers as `(u32)pointer`, where their u32 is
 * unsigned long.  So an argument carries the whole host pointer wherever
 * unsigned long is at least as wide as a pointer: every ILP32 host (the
 * reference configuration, where the game's heap and therefore its sound
 * data exist) and LP64 Unix.  On LLP64 (64-bit Windows) the cast drops the
 * top half and nothing can recover it, so there an argument is read as a DS
 * address instead, which is what a port-side sender can supply.  How many
 * bytes are readable is known when the address lies in sdk/dsmem.h's space
 * (probed through bw_ds_addr_of); otherwise the driver reads what the
 * file's own header says the game loaded (SSEQ, SBNK, SWAR all carry their
 * size at +8).  bw_snd_set_resolver() replaces the whole rule.
 *
 * ------------------------------------------------------------------------
 * Not modelled (counted in BwSndStats.ignored)
 * ------------------------------------------------------------------------
 * SKIP_SEQ, ALLOCATABLE_CHANNEL, the sequence variables (no matched ARM9
 * sender in this game shows their argument shape), direct channel and
 * capture control (SETUP_CHANNEL_*, SETUP_CAPTURE, CHANNEL_TIMER/VOLUME/PAN,
 * SURROUND_DECAY, MASTER_PAN, OUTPUT_SELECTOR, STOP_UNLOCKED_CHANNEL, the
 * channel and capture bits of START/STOP_TIMER).  The tempo ratio and the
 * track pitch, pan and modulation parameters are stored where the ARM9
 * wrote them and reported in SNDDriverInfo, but the engine renders at the
 * sequence's own tempo and does not bend notes.  Track volume and mute
 * apply to notes that start after the command.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "arm7.h"
#include "audio.h"
#include "dsmem.h"
#include "plat_thread.h"
#include "pxi.h"
#include "snd.h"

#define PLAYERS 16
#define TRACKS  16
#define ALARMS  8

/* Firings held for the next step.  The ARM9 drains them at every step, a
 * few per frame for a stream; a burst beyond this is counted and dropped. */
#define ALARM_QUEUE 256

/* struct SNDPlayer (snd_work.o DWARF), the bytes PLAYER_PARAM addresses.  */
#define PL_PRIO         0x04
#define PL_VOLUME       0x05
#define PL_EXT_FADER    0x06
#define PL_TRACKS       0x08
#define PL_TEMPO        0x18
#define PL_TEMPO_RATIO  0x1A
#define PL_SIZE         0x24

/* struct SNDTrack (snd_work.o DWARF).                                     */
#define TR_VOLUME       0x04
#define TR_VOLUME2      0x05
#define TR_BEND_RANGE   0x07
#define TR_EXT_FADER    0x0A
#define TR_PRIO         0x12
#define TR_SIZE         0x40

/* struct SNDDriverInfo / SNDWork (snd_work.o DWARF), DS layout.           */
#define DI_PLAYER       0x0540
#define DI_TRACK        0x0780
#define DI_TRACK_COUNT  32
#define DI_CHCTRL       0x1180
#define DI_WORK_ADDRESS 0x11C0
#define DI_LOCKED       0x11C4
#define DI_SIZE         0x11E0

/* The SDK's fader floor (SND_CalcChannelVolume, func_0208A24C.c).         */
#define FADER_MIN       (-723)

/* The most of a header-less sequence the driver will read.               */
#define SEQ_FALLBACK_MAX ((size_t)256 * 1024)

/* OS ticks per BW_AUDIO_RATE frame, as a ratio: (bus / 64) / 32768.       */
#define TICK_NUM        33513982u
#define TICK_DEN        (64u * 32768u)

/* SNDSharedWork as the matched code declares it (func_0208A1D0.c).        */
typedef struct {
    unsigned long finishCommandTag;
    unsigned long playerStatus;
    unsigned short channelStatus;
    unsigned short captureStatus;
    unsigned long padding[5];
    struct {
        short variable[16];
        unsigned long tickCounter;
    } player[16];
    short globalVariable[16];
} SharedWork;

/* 640 bytes, the DWARF's size, wherever unsigned long is four bytes.     */
typedef char bw_shared_work_size[(sizeof(unsigned long) != 4 || sizeof(SharedWork) == 640) ? 1 : -1];

typedef struct {
    BwAudioPlayer   *ap;
    BwAudioSeqSource src;
    int   active, prepared, paused, done;
    u8    param[PL_SIZE];
    u8    track[TRACKS][TR_SIZE];
    u8    mute[TRACKS];
    u64   tick;
    u32   voices;
    double gain;
} Player;

typedef struct {
    int armed;
    u8  id;
    u64 tick, period;   /* as SETUP_ALARM gave them, in OS ticks */
    u64 fire_at;        /* driver clock, OS ticks                */
} Alarm;

static struct {
    BwMutex      *mtx;
    Player        pl[PLAYERS];
    Alarm         alarm[ALARMS];
    u32           fired[ALARM_QUEUE];
    u32           n_fired;
    int           master_volume;
    u32           locked;
    SharedWork   *shared;
    BwSndResolver resolver;
    BwSndStats    stats;
} g;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static void lock(void)   { if (g.mtx) bw_mutex_lock(g.mtx); }
static void unlock(void) { if (g.mtx) bw_mutex_unlock(g.mtx); }

static double fader_gain(int f)
{
    if (f >= 0)
        return 1.0;
    if (f < FADER_MIN)
        f = FADER_MIN;
    return pow(10.0, (double)f / 200.0);
}

static s16 rd_s16(const u8 *p) { return (s16)bw_rd16(p); }

static void store_le(u8 *dst, u32 v, u32 size)
{
    u32 i;
    for (i = 0; i < size; i++)
        dst[i] = (u8)(v >> (8 * i));
}

static void player_defaults(Player *p)
{
    int t;
    memset(p->param, 0, sizeof(p->param));
    p->param[PL_PRIO] = 64;
    p->param[PL_VOLUME] = 127;
    memset(p->param + PL_TRACKS, 0xFF, 16);
    store_le(p->param + PL_TEMPO, 120, 2);
    store_le(p->param + PL_TEMPO_RATIO, 256, 2);
    for (t = 0; t < TRACKS; t++) {
        memset(p->track[t], 0, TR_SIZE);
        p->track[t][TR_VOLUME] = 127;
        p->track[t][TR_VOLUME2] = 127;
        p->track[t][TR_BEND_RANGE] = 2;
        p->track[t][TR_PRIO] = 64;
        p->mute[t] = 0;
    }
    p->gain = 1.0;
}

/* Apply the stored parameters to the engine player (under the lock).      */
static void player_apply(Player *p)
{
    int t;
    p->gain = fader_gain(rd_s16(p->param + PL_EXT_FADER));
    if (p->ap == NULL)
        return;
    for (t = 0; t < TRACKS; t++) {
        double tg = p->mute[t] ? 0.0 : fader_gain(rd_s16(p->track[t] + TR_EXT_FADER));
        bw_audio_player_set_track_gain(p->ap, t, tg);
    }
}

/* ------------------------------------------------------------------ */
/* Addresses                                                           */
/* ------------------------------------------------------------------ */

/* How far host memory from `h` keeps mapping to consecutive DS addresses. */
static size_t ds_extent(const u8 *h, u32 ds)
{
    size_t lo = 1, hi = (size_t)1 << 24;
    uintptr_t base = (uintptr_t)h;

    if (bw_ds_addr_of((const void *)(base + hi - 1)) == ds + (u32)(hi - 1))
        return hi;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (bw_ds_addr_of((const void *)(base + mid - 1)) == ds + (u32)(mid - 1))
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

static void *default_resolve(u64 value, size_t *avail)
{
    u8 *h;
    u32 ds;

    *avail = 0;
    if (value == 0)
        return NULL;
    if (sizeof(unsigned long) >= sizeof(void *)) {
        if ((u64)(uintptr_t)value != value)
            return NULL;
        h = (u8 *)(uintptr_t)value;
    } else {
        if (value > 0xFFFFFFFFu)
            return NULL;
        h = (u8 *)bw_ds_try_ptr((u32)value);
        if (h == NULL)
            return NULL;
    }
    ds = bw_ds_addr_of(h);
    if (ds != 0)
        *avail = ds_extent(h, ds);
    return h;
}

void bw_snd_set_resolver(BwSndResolver r)
{
    g.resolver = r;
}

void *bw_snd_resolve(u64 value, size_t *avail)
{
    size_t dummy;
    if (avail == NULL)
        avail = &dummy;
    return g.resolver ? g.resolver(value, avail) : default_resolve(value, avail);
}

/* Whether the `n` bytes before `h` can be read: inside the DS space they
 * must map contiguously; outside it they are the game's own memory, where
 * seqBase sits 0x1C into the SSEQ file the game loaded. */
static int readable_before(const u8 *h, u32 n)
{
    u32 ds = bw_ds_addr_of(h);
    if (ds == 0)
        return 1;
    return ds >= n && bw_ds_addr_of((const void *)((uintptr_t)h - n)) == ds - n;
}

/* A file with the NitroSDK header (magic, ..., u32 size at +8).            */
static int file_blob(u64 value, const char *magic, size_t min_size, BwBlob *out)
{
    size_t avail;
    const u8 *h = (const u8 *)bw_snd_resolve(value, &avail);
    u32 size;

    out->data = NULL;
    out->size = 0;
    if (h == NULL)
        return -1;
    if (avail != 0 && avail < min_size)
        return -1;
    if (memcmp(h, magic, 4) != 0)
        return -1;
    size = bw_rd32(h + 8);
    if (size < min_size)
        return -1;
    if (avail != 0 && size > avail)
        size = (u32)avail;
    out->data = h;
    out->size = size;
    return 0;
}

/* The bytes a seqBase names: the SSEQ the game loaded, when its header is
 * just before (seqBase = file + 0x1C, the u32 at +0x18 being that 0x1C),
 * else as far as the address space says is mapped. */
static int seq_blob(u64 value, BwBlob *out)
{
    size_t avail;
    const u8 *h = (const u8 *)bw_snd_resolve(value, &avail);

    out->data = NULL;
    out->size = 0;
    if (h == NULL)
        return -1;
    if (readable_before(h, 0x1C)) {
        const u8 *f = h - 0x1C;
        if (memcmp(f, "SSEQ", 4) == 0 && bw_rd32(f + 0x18) == 0x1C
            && bw_rd32(f + 8) > 0x1C) {
            u32 size = bw_rd32(f + 8) - 0x1C;
            if (avail != 0 && size > avail)
                size = (u32)avail;
            out->data = h;
            out->size = size;
            return 0;
        }
    }
    /* No header (a sequence inside an archive, or bytes a port placed):
     * as far as the address space maps, capped, and committed, since a
     * translation is what commits main RAM (sdk/dsmem.c). */
    if (avail == 0)
        return -1;
    if (avail > SEQ_FALLBACK_MAX)
        avail = SEQ_FALLBACK_MAX;
    {
        u32 ds = bw_ds_addr_of(h), off;
        if (ds != 0)
            for (off = 0; off < (u32)avail; off += 0x8000u)
                (void)bw_ds_try_ptr(ds + off);
    }
    out->data = h;
    out->size = avail;
    return 0;
}

static int build_source(const BwSndCommand *c, BwAudioSeqSource *src)
{
    int i;

    memset(src, 0, sizeof(*src));
    if (seq_blob(c->arg[1], &src->seq) != 0)
        return -1;
    if (c->arg[2] >= src->seq.size)
        return -1;
    src->seq_start = (u32)c->arg[2];
    if (c->arg[3] != 0) {
        if (file_blob(c->arg[3], "SBNK", 0x3C, &src->bank) != 0)
            return -1;
        for (i = 0; i < 4; i++) {
            /* waveArcLink[i].waveArc, a DS-width pointer slot. */
            u32 arc = bw_rd32(src->bank.data + 0x18 + 8 * i);
            if (arc != 0)
                (void)file_blob(arc, "SWAR", 0x3C, &src->wave_arc[i]);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Players                                                             */
/* ------------------------------------------------------------------ */

static int player_no(u64 v)
{
    return v < PLAYERS ? (int)v : -1;
}

/* Detach the engine player under the lock; the caller frees it outside. */
static BwAudioPlayer *player_stop(Player *p)
{
    BwAudioPlayer *old = p->ap;
    p->ap = NULL;
    p->active = p->prepared = p->paused = p->done = 0;
    p->voices = 0;
    return old;
}

static void cmd_prepare(const BwSndCommand *c, int start)
{
    int n = player_no(c->arg[0]);
    BwAudioSeqSource src;
    BwAudioPlayer *ap, *old;
    Player *p;

    if (n < 0)
        return;
    if (build_source(c, &src) != 0) {
        g.stats.bad_address++;
        lock();
        old = player_stop(&g.pl[n]);
        unlock();
        bw_audio_player_free(old);
        return;
    }
    /* The slow part (decode, the level probe) runs outside the lock, so
     * the audio thread keeps mixing the other players meanwhile. */
    ap = bw_audio_player_create_source(&src, BW_SND_SEQ_MAX_SECONDS, 0);
    if (ap == NULL)
        g.stats.bad_sequence++;
    else
        bw_audio_player_set_loop(ap, bw_audio_player_seq_loops(ap));

    lock();
    p = &g.pl[n];
    old = player_stop(p);
    player_defaults(p);
    p->src = src;
    p->ap = ap;
    p->prepared = ap != NULL;
    p->active = start && ap != NULL;
    p->tick = 0;
    player_apply(p);
    unlock();
    bw_audio_player_free(old);
}

static void cmd_player(const BwSndCommand *c)
{
    int n = player_no(c->arg[0]);
    BwAudioPlayer *old = NULL;
    Player *p;

    if (n < 0)
        return;
    lock();
    p = &g.pl[n];
    switch (c->id) {
    case BW_SND_CMD_START_PREPARED_SEQ:
        if (p->prepared && p->ap != NULL)
            p->active = 1;
        break;
    case BW_SND_CMD_STOP_SEQ:
        old = player_stop(p);
        break;
    case BW_SND_CMD_PAUSE_SEQ:
        p->paused = c->arg[1] != 0;
        break;
    case BW_SND_CMD_PLAYER_PARAM: {
        u32 off = (u32)c->arg[1], size = (u32)c->arg[3];
        if ((size == 1 || size == 2 || size == 4) && off + size <= PL_SIZE) {
            store_le(p->param + off, (u32)c->arg[2], size);
            player_apply(p);
        }
        break;
    }
    case BW_SND_CMD_MUTE_TRACK: {
        int t;
        for (t = 0; t < TRACKS; t++)
            if ((c->arg[1] >> t) & 1u)
                p->mute[t] = c->arg[2] != 0;
        player_apply(p);
        break;
    }
    default:
        break;
    }
    unlock();
    bw_audio_player_free(old);
}

static void cmd_track_param(const BwSndCommand *c)
{
    int n = player_no(c->arg[0] & 0xFFFFFFu);
    u32 size = (u32)(c->arg[0] >> 24) & 0xFFu;
    u32 off = (u32)c->arg[2];
    int t;

    if (n < 0 || !(size == 1 || size == 2 || size == 4) || off + size > TR_SIZE)
        return;
    lock();
    for (t = 0; t < TRACKS; t++)
        if ((c->arg[1] >> t) & 1u)
            store_le(g.pl[n].track[t] + off, (u32)c->arg[3], size);
    player_apply(&g.pl[n]);
    unlock();
}

/* INVALIDATE_*: the ARM9 is about to reuse [start, end); every player that
 * reads from it stops, as the DS's driver stops it. */
static int overlaps(const BwBlob *b, uintptr_t lo, uintptr_t hi)
{
    uintptr_t a;
    if (b->data == NULL || b->size == 0)
        return 0;
    a = (uintptr_t)b->data;
    return a < hi && lo < a + b->size;
}

static void cmd_invalidate(const BwSndCommand *c)
{
    BwAudioPlayer *old[PLAYERS];
    uintptr_t lo, hi;
    u8 *h;
    int i, k;

    if (c->arg[1] <= c->arg[0])
        return;
    h = (u8 *)bw_snd_resolve(c->arg[0], NULL);
    if (h == NULL) {
        g.stats.bad_address++;
        return;
    }
    lo = (uintptr_t)h;
    hi = lo + (uintptr_t)(c->arg[1] - c->arg[0]);
    lock();
    for (i = 0; i < PLAYERS; i++) {
        Player *p = &g.pl[i];
        int hit = 0;
        old[i] = NULL;
        if (p->ap == NULL)
            continue;
        if (c->id == BW_SND_CMD_INVALIDATE_SEQ)
            hit = overlaps(&p->src.seq, lo, hi);
        else if (c->id == BW_SND_CMD_INVALIDATE_BANK)
            hit = overlaps(&p->src.bank, lo, hi);
        else
            for (k = 0; k < 4; k++)
                hit |= overlaps(&p->src.wave_arc[k], lo, hi);
        if (hit)
            old[i] = player_stop(p);
    }
    unlock();
    for (i = 0; i < PLAYERS; i++)
        bw_audio_player_free(old[i]);
}

/* ------------------------------------------------------------------ */
/* Alarms                                                              */
/* ------------------------------------------------------------------ */

static u64 now_ticks(void)
{
    return g.stats.frames * TICK_NUM / TICK_DEN;
}

static void cmd_timer(const BwSndCommand *c, int start)
{
    u32 mask = (u32)c->arg[2] & 0xFFu;
    int i;

    if ((c->arg[0] & 0xFFFFu) != 0 || (c->arg[1] & 0x3u) != 0)
        g.stats.ignored++;          /* direct channels and capture */
    lock();
    for (i = 0; i < ALARMS; i++) {
        if (!((mask >> i) & 1u))
            continue;
        g.alarm[i].armed = start;
        if (start)
            g.alarm[i].fire_at = now_ticks() + g.alarm[i].tick;
    }
    unlock();
}

static void alarms_run(void)
{
    u64 now = now_ticks();
    int i;

    for (i = 0; i < ALARMS; i++) {
        Alarm *a = &g.alarm[i];
        while (a->armed && a->fire_at <= now) {
            if (g.n_fired < ALARM_QUEUE)
                g.fired[g.n_fired++] = (u32)i | ((u32)a->id << 8);
            else
                g.stats.alarms_dropped++;
            g.stats.alarms_fired++;
            if (a->period == 0)
                a->armed = 0;
            else
                a->fire_at += a->period;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Driver info                                                         */
/* ------------------------------------------------------------------ */

static void cmd_driver_info(const BwSndCommand *c)
{
    size_t avail;
    u8 *info = (u8 *)bw_snd_resolve(c->arg[0], &avail);
    int i, t, next_track = 0;

    /* SNDDriverInfo holds pointers, so its layout is the DS's only where
     * pointers are four bytes. */
    if (info == NULL || sizeof(void *) != 4 || (avail != 0 && avail < DI_SIZE)) {
        g.stats.bad_address++;
        return;
    }
    memset(info, 0, DI_SIZE);
    lock();
    for (i = 0; i < PLAYERS; i++) {
        Player *p = &g.pl[i];
        u8 *pl = info + DI_PLAYER + (size_t)i * PL_SIZE;
        u32 tracks = (p->ap != NULL) ? bw_audio_player_track_mask(p->ap) : 0;
        memcpy(pl, p->param, PL_SIZE);
        memset(pl + 0x20, 0, 4);                  /* the ARM7's bank pointer */
        pl[1] = (u8)i;                            /* myNo                    */
        memset(pl + PL_TRACKS, 0xFF, 16);
        for (t = 0; t < TRACKS; t++) {
            if (!((tracks >> t) & 1u) || next_track >= DI_TRACK_COUNT)
                continue;
            memcpy(info + DI_TRACK + (size_t)next_track * TR_SIZE, p->track[t], TR_SIZE);
            /* base, cur, call_stack, channel_list: none to report */
            memset(info + DI_TRACK + (size_t)next_track * TR_SIZE + 0x24, 0, 0x1C);
            pl[PL_TRACKS + t] = (u8)next_track++;
        }
    }
    store_le(info + DI_WORK_ADDRESS, (u32)(uintptr_t)info, 4);
    store_le(info + DI_LOCKED, g.locked, 4);
    unlock();
}

/* ------------------------------------------------------------------ */
/* The driver hooks                                                    */
/* ------------------------------------------------------------------ */

static void drv_reset(void)
{
    BwAudioPlayer *old[PLAYERS];
    int i;

    lock();
    for (i = 0; i < PLAYERS; i++) {
        old[i] = player_stop(&g.pl[i]);
        player_defaults(&g.pl[i]);
        memset(&g.pl[i].src, 0, sizeof(g.pl[i].src));
        g.pl[i].tick = 0;
    }
    memset(g.alarm, 0, sizeof(g.alarm));
    g.n_fired = 0;
    g.master_volume = 127;
    g.locked = 0;
    g.shared = NULL;
    memset(&g.stats, 0, sizeof(g.stats));
    unlock();
    for (i = 0; i < PLAYERS; i++)
        bw_audio_player_free(old[i]);
}

static void drv_command(const BwSndCommand *c)
{
    g.stats.commands++;
    if (c->id < BW_SND_CMD_COUNT)
        g.stats.by_id[c->id]++;

    switch (c->id) {
    case BW_SND_CMD_START_SEQ:
        cmd_prepare(c, 1);
        break;
    case BW_SND_CMD_PREPARE_SEQ:
        cmd_prepare(c, 0);
        break;
    case BW_SND_CMD_START_PREPARED_SEQ:
    case BW_SND_CMD_STOP_SEQ:
    case BW_SND_CMD_PAUSE_SEQ:
    case BW_SND_CMD_PLAYER_PARAM:
    case BW_SND_CMD_MUTE_TRACK:
        cmd_player(c);
        break;
    case BW_SND_CMD_TRACK_PARAM:
        cmd_track_param(c);
        break;
    case BW_SND_CMD_SETUP_ALARM: {
        u64 n = c->arg[0];
        if (n < ALARMS) {
            lock();
            g.alarm[n].armed = 0;
            g.alarm[n].tick = (u32)c->arg[1];
            g.alarm[n].period = (u32)c->arg[2];
            g.alarm[n].id = (u8)c->arg[3];
            unlock();
        }
        break;
    }
    case BW_SND_CMD_START_TIMER:
        cmd_timer(c, 1);
        break;
    case BW_SND_CMD_STOP_TIMER:
        cmd_timer(c, 0);
        break;
    case BW_SND_CMD_MASTER_VOLUME:
        lock();
        g.master_volume = c->arg[0] > 127 ? 127 : (int)c->arg[0];
        unlock();
        break;
    case BW_SND_CMD_LOCK_CHANNEL:
        lock();
        g.locked |= (u32)c->arg[0] & 0xFFFFu;
        unlock();
        break;
    case BW_SND_CMD_UNLOCK_CHANNEL:
        lock();
        g.locked &= ~((u32)c->arg[0] & 0xFFFFu);
        unlock();
        break;
    case BW_SND_CMD_SHARED_WORK: {
        size_t avail;
        void *w = bw_snd_resolve(c->arg[0], &avail);
        if (w == NULL || (avail != 0 && avail < sizeof(SharedWork)))
            g.stats.bad_address++;
        else
            bw_snd_bind_shared(w);
        break;
    }
    case BW_SND_CMD_INVALIDATE_SEQ:
    case BW_SND_CMD_INVALIDATE_BANK:
    case BW_SND_CMD_INVALIDATE_WAVE:
        cmd_invalidate(c);
        break;
    case BW_SND_CMD_READ_DRIVER_INFO:
        cmd_driver_info(c);
        break;
    default:
        g.stats.ignored++;
        break;
    }
}

/* Write what the ARM9 polls, free finished players, send fired alarms.
 * Game thread only. */
static void publish(void)
{
    BwAudioPlayer *old[PLAYERS];
    u32 status = 0, channels = 0, fired[ALARM_QUEUE], n_fired, i;
    u32 ticks[PLAYERS];
    SharedWork *sw;

    lock();
    for (i = 0; i < PLAYERS; i++) {
        Player *p = &g.pl[i];
        old[i] = NULL;
        if (p->done)
            old[i] = player_stop(p);
        if (p->active && p->ap != NULL)
            status |= 1u << i;
        channels |= p->voices;
        ticks[i] = (u32)p->tick;
    }
    n_fired = g.n_fired;
    memcpy(fired, g.fired, n_fired * sizeof(u32));
    g.n_fired = 0;
    sw = g.shared;
    unlock();

    for (i = 0; i < PLAYERS; i++)
        bw_audio_player_free(old[i]);
    if (sw != NULL) {
        sw->playerStatus = status;
        sw->channelStatus = (unsigned short)channels;
        for (i = 0; i < PLAYERS; i++)
            sw->player[i].tickCounter = ticks[i];
    }
    for (i = 0; i < n_fired; i++)
        (void)bw_pxi_arm7_send(BW_PXI_TAG_SOUND, fired[i], 0);
}

static void drv_list_done(u32 lists)
{
    publish();
    if (g.shared != NULL)
        g.shared->finishCommandTag = lists;
}

static void drv_step(void)
{
    publish();
}

static const BwArm7SndDriver g_ops = {
    drv_reset, drv_command, drv_list_done, drv_step
};

const BwArm7SndDriver *bw_snd_driver_ops(void)
{
    return &g_ops;
}

void bw_snd_driver_attach(void)
{
    if (g.mtx == NULL) {
        int i;
        g.mtx = bw_mutex_create();
        g.master_volume = 127;
        for (i = 0; i < PLAYERS; i++)
            player_defaults(&g.pl[i]);
    }
    bw_arm7_snd_set_driver(&g_ops);
}

void bw_snd_bind_shared(void *shared_work)
{
    g.shared = (SharedWork *)shared_work;
}

/* ------------------------------------------------------------------ */
/* The mix                                                             */
/* ------------------------------------------------------------------ */

void bw_snd_render(s16 *out, u32 frames)
{
    /* The engine player renders in blocks of this size too; mixing in the
     * same blocks keeps one playing sequence bit-identical to
     * bw_audio_player_mix() over the same requests. */
    enum { BLOCK = 1024 };
    static double acc_l[BLOCK], acc_r[BLOCK], tl[BLOCK], tr[BLOCK];
    u32 done = 0;

    lock();
    while (done < frames) {
        u32 n = frames - done, k;
        int i;
        double master;

        if (n > BLOCK)
            n = BLOCK;
        memset(acc_l, 0, n * sizeof(double));
        memset(acc_r, 0, n * sizeof(double));
        for (i = 0; i < PLAYERS; i++) {
            Player *p = &g.pl[i];
            if (p->ap == NULL || !p->active || p->paused || p->done)
                continue;
            bw_audio_player_render(p->ap, tl, tr, n);
            if (p->gain != 1.0) {
                for (k = 0; k < n; k++) {
                    tl[k] *= p->gain;
                    tr[k] *= p->gain;
                }
            }
            for (k = 0; k < n; k++) {
                acc_l[k] += tl[k];
                acc_r[k] += tr[k];
            }
            p->tick = bw_audio_player_tick(p->ap);
            p->voices = bw_audio_player_voice_mask(p->ap);
            if (bw_audio_player_finished(p->ap)) {
                p->done = 1;
                p->voices = 0;
            }
        }
        if (out != NULL) {
            master = g.master_volume == 127 ? 1.0 : g.master_volume / 127.0;
            for (k = 0; k < n; k++) {
                double lv = acc_l[k], rv = acc_r[k];
                if (master != 1.0) {
                    lv *= master;
                    rv *= master;
                }
                lv = bw_audio_py_round(lv);
                rv = bw_audio_py_round(rv);
                if (lv < -32768.0) lv = -32768.0;
                if (lv > 32767.0) lv = 32767.0;
                if (rv < -32768.0) rv = -32768.0;
                if (rv > 32767.0) rv = 32767.0;
                out[(size_t)(done + k) * 2] = (s16)lv;
                out[(size_t)(done + k) * 2 + 1] = (s16)rv;
            }
        }
        g.stats.frames += n;
        alarms_run();
        done += n;
    }
    unlock();
}

/* ------------------------------------------------------------------ */
/* Inspection                                                          */
/* ------------------------------------------------------------------ */

void bw_snd_stats(BwSndStats *out)
{
    lock();
    *out = g.stats;
    unlock();
}

int bw_snd_player_state(int player, BwSndPlayerState *out)
{
    Player *p;
    if (player < 0 || player >= PLAYERS)
        return -1;
    lock();
    p = &g.pl[player];
    out->active = p->active && p->ap != NULL && !p->done;
    out->prepared = p->prepared;
    out->paused = p->paused;
    out->loops = p->ap != NULL && bw_audio_player_seq_loops(p->ap);
    out->tick = p->tick;
    out->player_fader = rd_s16(p->param + PL_EXT_FADER);
    out->tempo_ratio = bw_rd16(p->param + PL_TEMPO_RATIO);
    out->volume = p->param[PL_VOLUME];
    out->prio = p->param[PL_PRIO];
    out->gain = p->gain;
    unlock();
    return 0;
}

int bw_snd_master_volume(void)
{
    return g.master_volume;
}

u32 bw_snd_locked_channels(void)
{
    return g.locked;
}
