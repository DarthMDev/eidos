/*
 * tests/test_snd.c -- the ARM7 sound driver (sdk/arm7/snd.h) through the
 * PXI wire, against engine/audio driven directly.
 *
 * Usage: test_snd               the synthetic checks
 *        test_snd <data root>   also the real-sequence checks
 *
 * Synthetic: a small SSEQ (two tracks, so the OpenTrack offset is
 * exercised), an SBNK with a wave instrument and a PSG square, and a SWAR
 * with one looping PCM8 wave are written into DS main RAM the way the
 * game's loader leaves them, and command lists built as the matched ARM9
 * senders build them (BwSndCommandWire, the same declaration) are sent over
 * the FIFO.  What comes out of bw_snd_render() is compared sample for
 * sample with an engine/audio player over the same bytes; the two must be
 * identical.  Around that: the shared work (finish tag, player status,
 * tick counter), player and track parameters, master volume, alarms
 * delivered back to the ARM9, invalidation and driver info.
 *
 * Real data: SEQ_SE_DECIDE2, a one-track sound effect with no jumps, is
 * played through the command path from copies of its SSEQ, SBNK and SWARs
 * in main RAM and must be bit-identical to bw_audio_player_create() on the
 * SDAT, the existing direct path.  SEQ_BGM_GYM, which has eleven tracks and
 * a loop, must be bit-identical to engine/audio driven directly over the
 * same bytes with the SDK's offset reading; how far the SDAT path's own
 * reading departs from that is printed, not checked (engine/audio/audio.h,
 * "Where a sequence's bytes come from").
 *
 * Pointer arguments are what the matched senders write, `(u32)pointer` in
 * an unsigned long, wherever that keeps the whole pointer; elsewhere (LLP64)
 * the DS address, which is what the driver reads there.
 */

#include "port_test.h"
#include "port.h"
#include "audio.h"
#include "dsmem.h"
#include "pxi.h"
#include "arm7/arm7.h"
#include "arm7/snd.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* BW_REQUIRE for the void test functions: count the failure and stop.    */
#define REQ(label, cond)                                                   \
    do {                                                                   \
        bw_checks++;                                                       \
        if (!(cond)) {                                                     \
            bw_failures++;                                                 \
            printf("  FAIL %-40s (%s)\n", (label), #cond);                 \
            return;                                                        \
        }                                                                  \
    } while (0)

/* sdk/pxi.c's PXI_InitFifo shim names these two matched OS functions; the
 * boot links them from aletheia, and this test never runs PXI_InitFifo.    */
void OS_SetIrqFunction(u32 intr, void (*function)(void));
u32  OS_EnableIrqMask(u32 intr);
void OS_SetIrqFunction(u32 intr, void (*function)(void)) { (void)intr; (void)function; }
u32  OS_EnableIrqMask(u32 intr) { (void)intr; return 0; }

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

/* Where the test puts things in DS main RAM, clear of the ARM9 globals.   */
#define DS_SHARED   0x02400000u
#define DS_INFO     0x02401000u
#define DS_SEQ      0x02410000u
#define DS_BANK     0x02420000u
#define DS_WAVE     0x02430000u
#define DS_REAL     0x02500000u

/* ------------------------------------------------------------------ */
/* DS memory and the wire                                              */
/* ------------------------------------------------------------------ */

/* Host memory for [addr, addr + size), every 64 KiB piece committed.      */
static u8 *ds_mem(u32 addr, u32 size)
{
    u32 off;
    for (off = 0; off < size; off += 0x8000u)
        (void)bw_ds_ptr(addr + off);
    if (size != 0)
        (void)bw_ds_ptr(addr + size - 1);
    return (u8 *)bw_ds_ptr(addr);
}

static unsigned long ptr_arg(const void *host, u32 ds)
{
    if (sizeof(unsigned long) >= sizeof(void *))
        return (unsigned long)(uintptr_t)host;
    return (unsigned long)ds;
}

static void wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v)
{
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static BwSndCommandWire g_pool[64];
static int g_used;

static void list_begin(void)
{
    g_used = 0;
}

static void push(int id, unsigned long a0, unsigned long a1, unsigned long a2,
                 unsigned long a3)
{
    BwSndCommandWire *c = &g_pool[g_used];
    c->next = NULL;
    c->id = id;
    c->arg[0] = a0;
    c->arg[1] = a1;
    c->arg[2] = a2;
    c->arg[3] = a3;
    if (g_used > 0)
        g_pool[g_used - 1].next = c;
    g_used++;
}

/* SND_FlushCommand's one FIFO word, then the ARM7 runs.                   */
static int flush(void)
{
    u32 w = BW_PXI_WORD(BW_PXI_TAG_SOUND,
                        (u32)(uintptr_t)&g_pool[0] & 0x03FFFFFFu, 0);
    int r = bw_pxi_arm9_send(w);
    bw_arm7_step();
    return r;
}

static void boot(void)
{
    bw_ds_reset();
    bw_pxi_reset();
    /* PXI_InitFifo's enable, without its IPCSYNC handshake. */
    bw_io_write16(BW_REG_IPCFIFOCNT,
                  (u16)(BW_FIFOCNT_ENABLE | BW_FIFOCNT_ERROR | BW_FIFOCNT_SEND_CLEAR));
    bw_snd_driver_attach();
    bw_arm7_boot();
    bw_arm7_snd_bind_pool(g_pool, 64, 0);
}

/* ------------------------------------------------------------------ */
/* Synthetic data                                                      */
/* ------------------------------------------------------------------ */

static void nitro_header(u8 *f, const char *magic, u32 size)
{
    memcpy(f, magic, 4);
    wr16(f + 4, 0xFEFF);
    wr16(f + 6, 0x0100);
    wr32(f + 8, size);
    wr16(f + 12, 0x10);
    wr16(f + 14, 1);
    memcpy(f + 0x10, "DATA", 4);
    wr32(f + 0x14, size - 0x10);
}

/*
 * Two tracks.  Track 0: tempo 150, program 0 (the wave), two notes.
 * Track 1, opened at an offset counted from the payload: program 1 (the
 * square), a rest, a note.  Offsets in this payload are relative to
 * seqBase = file + 0x1C.
 */
static const u8 k_seq_body[] = {
    0xFE, 0x03, 0x00,                   /* AllocTrack 0,1                 */
    0x93, 0x01, 0x18, 0x00, 0x00,       /* OpenTrack 1 at +0x18           */
    0xE1, 0x96, 0x00,                   /* Tempo 150                      */
    0x81, 0x00,                         /* Program 0                      */
    0x3C, 0x7F, 0x30,                   /* Note 60 vel 127 dur 48         */
    0x40, 0x60, 0x18,                   /* Note 64 vel 96 dur 24          */
    0xFF, 0x00, 0x00, 0x00, 0x00,       /* End, padding up to +0x18       */
    0x81, 0x01,                         /* +0x18 track 1: Program 1       */
    0x80, 0x0C,                         /* Rest 12                        */
    0xC1, 0x50,                         /* Volume 80                      */
    0x43, 0x70, 0x24,                   /* Note 67 vel 112 dur 36         */
    0xFF
};

static u32 make_sseq(u8 *f)
{
    u32 size = 0x1C + (u32)sizeof(k_seq_body);
    memset(f, 0, size);
    nitro_header(f, "SSEQ", size);
    wr32(f + 0x18, 0x1C);
    memcpy(f + 0x1C, k_seq_body, sizeof(k_seq_body));
    return size;
}

static void region(u8 *r, u16 wave, u16 arc, u8 key, u8 pan)
{
    wr16(r, wave);
    wr16(r + 2, arc);
    r[4] = key;
    r[5] = 127;   /* attack  */
    r[6] = 110;   /* decay   */
    r[7] = 100;   /* sustain */
    r[8] = 100;   /* release */
    r[9] = pan;
}

/* Two instruments: type 1 (wave 0 of slot 0) and type 2 (square, duty 3). */
static u32 make_sbnk(u8 *f)
{
    u32 size = 0x3C + 2 * 4 + 2 * 10;
    memset(f, 0, size);
    nitro_header(f, "SBNK", size);
    wr32(f + 0x38, 2);
    wr32(f + 0x3C, 1u | (0x44u << 8));
    wr32(f + 0x40, 2u | (0x4Eu << 8));
    region(f + 0x44, 0, 0, 60, 64);
    region(f + 0x4E, 3, 0, 69, 40);
    return size;
}

/* One looping PCM8 wave, 64 samples of a coarse triangle.                 */
static u32 make_swar(u8 *f)
{
    u32 size = 0x3C + 4 + 12 + 64, i;
    memset(f, 0, size);
    nitro_header(f, "SWAR", size);
    wr32(f + 0x38, 1);
    wr32(f + 0x3C, 0x40);
    f[0x40] = 0;                        /* PCM8                           */
    f[0x41] = 1;                        /* loop                           */
    wr16(f + 0x42, 16384);              /* rate                           */
    wr16(f + 0x44, 1023);               /* timer                          */
    wr16(f + 0x46, 0);                  /* loop start, words              */
    wr32(f + 0x48, 16);                 /* loop length, words             */
    for (i = 0; i < 64; i++) {
        int v = i < 32 ? (int)i * 7 - 112 : (int)(63 - i) * 7 - 112;
        f[0x4C + i] = (u8)(s8)v;
    }
    return size;
}

typedef struct {
    u8 *seq_file, *bank, *wave;
    u32 seq_size, bank_size, wave_size;
    u32 ds_seq, ds_bank, ds_wave;
} Loaded;

/* SND_AssignWaveArc's effect on the bank: waveArcLink[slot].waveArc.      */
static void link_arc(Loaded *l, int slot, const u8 *arc, u32 ds_arc)
{
    wr32(l->bank + 0x18 + 8 * slot, (u32)ptr_arg(arc, ds_arc));
}

static void load_synthetic(Loaded *l)
{
    l->ds_seq = DS_SEQ;
    l->ds_bank = DS_BANK;
    l->ds_wave = DS_WAVE;
    l->seq_file = ds_mem(DS_SEQ, 0x1000);
    l->bank = ds_mem(DS_BANK, 0x1000);
    l->wave = ds_mem(DS_WAVE, 0x1000);
    l->seq_size = make_sseq(l->seq_file);
    l->bank_size = make_sbnk(l->bank);
    l->wave_size = make_swar(l->wave);
    link_arc(l, 0, l->wave, DS_WAVE);
}

/* The source engine/audio would be handed directly for the same bytes.    */
static void direct_source(const Loaded *l, BwAudioSeqSource *src)
{
    memset(src, 0, sizeof(*src));
    src->seq.data = l->seq_file + 0x1C;
    src->seq.size = l->seq_size - 0x1C;
    src->seq_start = 0;
    src->bank.data = l->bank;
    src->bank.size = l->bank_size;
    src->wave_arc[0].data = l->wave;
    src->wave_arc[0].size = l->wave_size;
}

static void send_prepare(int id, int player, const Loaded *l)
{
    list_begin();
    push(id, (unsigned long)player,
         ptr_arg(l->seq_file + 0x1C, l->ds_seq + 0x1C), 0,
         ptr_arg(l->bank, l->ds_bank));
    flush();
}

static void send1(int id, unsigned long a0, unsigned long a1, unsigned long a2,
                  unsigned long a3)
{
    list_begin();
    push(id, a0, a1, a2, a3);
    flush();
}

/* Render `frames` through the driver and through `ref` in the same
 * requests; return how many samples differ. */
static long compare_render(BwAudioPlayer *ref, u32 frames, u32 chunk, s16 *peak_out)
{
    s16 *a = (s16 *)malloc((size_t)chunk * 2 * sizeof(s16));
    s16 *b = (s16 *)malloc((size_t)chunk * 2 * sizeof(s16));
    long diff = 0;
    s16 peak = 0;
    u32 done = 0, k;

    if (a == NULL || b == NULL) {
        free(a);
        free(b);
        return -1;
    }
    while (done < frames) {
        u32 n = frames - done < chunk ? frames - done : chunk;
        bw_snd_render(a, n);
        bw_audio_player_mix(ref, b, n);
        for (k = 0; k < n * 2; k++) {
            if (a[k] != b[k])
                diff++;
            if (a[k] > peak)
                peak = a[k];
        }
        done += n;
    }
    free(a);
    free(b);
    if (peak_out)
        *peak_out = peak;
    return diff;
}

/* ------------------------------------------------------------------ */
/* Synthetic checks                                                    */
/* ------------------------------------------------------------------ */

static SharedWork *g_sw;

static void bind_shared(void)
{
    g_sw = (SharedWork *)(void *)ds_mem(DS_SHARED, sizeof(SharedWork));
    memset(g_sw, 0, sizeof(*g_sw));
    send1(BW_SND_CMD_SHARED_WORK, ptr_arg(g_sw, DS_SHARED), 0, 0, 0);
}

static void test_wire(void)
{
    BwSndStats st;

    boot();
    bind_shared();
    BW_CHECK_EQ("wire: one list completed", bw_arm7_snd_lists(), 1);
    BW_CHECK_EQ("wire: finish tag in shared work", g_sw->finishCommandTag, 1);
    BW_CHECK_EQ("wire: nothing unresolved", bw_arm7_snd_unresolved(), 0);

    /* RequestCommandProc's 0 is not a list. */
    bw_pxi_arm9_send(BW_PXI_WORD(BW_PXI_TAG_SOUND, 0, 0));
    bw_arm7_step();
    BW_CHECK_EQ("wire: process request is not a list", g_sw->finishCommandTag, 1);

    /* A word that names no pool element completes unexecuted. */
    bw_pxi_arm9_send(BW_PXI_WORD(BW_PXI_TAG_SOUND,
                                 ((u32)(uintptr_t)&g_pool[0] + 2u) & 0x03FFFFFFu, 0));
    bw_arm7_step();
    BW_CHECK_EQ("wire: unresolved list still completes", g_sw->finishCommandTag, 2);
    BW_CHECK_EQ("wire: counted unresolved", bw_arm7_snd_unresolved(), 1);

    /* Three commands in one list run in order. */
    list_begin();
    push(BW_SND_CMD_MASTER_VOLUME, 100, 0, 0, 0);
    push(BW_SND_CMD_LOCK_CHANNEL, 0x0003, 0, 0, 0);
    push(BW_SND_CMD_UNLOCK_CHANNEL, 0x0001, 0, 0, 0);
    flush();
    BW_CHECK_EQ("wire: master volume", bw_snd_master_volume(), 100);
    BW_CHECK_EQ("wire: locked channels", bw_snd_locked_channels(), 2);
    bw_snd_stats(&st);
    BW_CHECK_EQ("wire: commands executed", st.commands, 4);
    BW_CHECK_EQ("wire: finish tag", g_sw->finishCommandTag, 3);
}

static void test_sequence(void)
{
    Loaded l;
    BwAudioSeqSource src;
    BwAudioPlayer *ref;
    BwSndPlayerState ps;
    BwSndStats st;
    long diff;
    s16 peak;
    u32 frames = 0;

    boot();
    bind_shared();
    load_synthetic(&l);
    direct_source(&l, &src);

    send_prepare(BW_SND_CMD_PREPARE_SEQ, 3, &l);
    bw_snd_player_state(3, &ps);
    BW_CHECK_EQ("seq: prepared", ps.prepared, 1);
    BW_CHECK_EQ("seq: not active before START_PREPARED", ps.active, 0);
    BW_CHECK_EQ("seq: status before start", g_sw->playerStatus, 0);
    bw_snd_render(NULL, 100);          /* time passes; nothing plays */

    send1(BW_SND_CMD_START_PREPARED_SEQ, 3, 0, 0, 0);
    BW_CHECK_EQ("seq: status bit 3", g_sw->playerStatus, 1u << 3);

    ref = bw_audio_player_create_source(&src, BW_SND_SEQ_MAX_SECONDS, 0);
    REQ("seq: reference player", ref != NULL);
    diff = compare_render(ref, 32768, 700, &peak);
    BW_CHECK_EQ("seq: command path == engine/audio direct", diff, 0);
    REQ("seq: it sounds", peak > 1000);
    frames += 32768;

    bw_arm7_step();
    bw_snd_player_state(3, &ps);
    /* 150 BPM, 48 ticks a quarter: 120 ticks a second. */
    BW_CHECK_EQ("seq: tick counter after 1 s", g_sw->player[3].tickCounter, 120);
    BW_CHECK_EQ("seq: tick in state", (long)ps.tick, 120);

    /* Play to the end: the status bit drops. */
    while (frames < 32768u * 8 && bw_snd_player_state(3, &ps) == 0 && ps.active) {
        bw_snd_render(NULL, 1024);
        frames += 1024;
    }
    bw_arm7_step();
    BW_CHECK_EQ("seq: ended", ps.active, 0);
    BW_CHECK_EQ("seq: status cleared", g_sw->playerStatus, 0);
    BW_CHECK_EQ("seq: channels quiet", g_sw->channelStatus, 0);
    bw_snd_stats(&st);
    BW_CHECK_EQ("seq: no bad addresses", st.bad_address, 0);
    BW_CHECK_EQ("seq: no refused sequence", st.bad_sequence, 0);
    bw_audio_player_free(ref);

    /* START_SEQ is prepare and start in one. */
    send_prepare(BW_SND_CMD_START_SEQ, 0, &l);
    BW_CHECK_EQ("seq: START_SEQ status", g_sw->playerStatus, 1u);
    send1(BW_SND_CMD_STOP_SEQ, 0, 0, 0, 0);
    BW_CHECK_EQ("seq: STOP_SEQ status", g_sw->playerStatus, 0);
}

static void test_params(void)
{
    Loaded l;
    BwAudioSeqSource src;
    BwAudioPlayer *ref;
    BwSndPlayerState ps;
    s16 *a, *b;
    long diff;
    u32 k;
    double ratio_sum = 0.0;
    int ratio_n = 0;

    boot();
    bind_shared();
    load_synthetic(&l);
    direct_source(&l, &src);

    /* Track 1 muted, player 5: the same as the engine with track 1 at 0. */
    send_prepare(BW_SND_CMD_PREPARE_SEQ, 5, &l);
    send1(BW_SND_CMD_MUTE_TRACK, 5, 1u << 1, 3, 0);
    send1(BW_SND_CMD_START_PREPARED_SEQ, 5, 0, 0, 0);
    ref = bw_audio_player_create_source(&src, BW_SND_SEQ_MAX_SECONDS, 0);
    REQ("params: reference player", ref != NULL);
    bw_audio_player_set_track_gain(ref, 1, 0.0);
    diff = compare_render(ref, 16384, 1024, NULL);
    BW_CHECK_EQ("params: muted track == engine track gain 0", diff, 0);
    bw_audio_player_free(ref);

    /* SND_SetPlayerVolume: SNDi_SetPlayerParam(player, 6, fader, 2). */
    send_prepare(BW_SND_CMD_START_SEQ, 5, &l);
    send1(BW_SND_CMD_PLAYER_PARAM, 5, 6, (unsigned long)(u16)(s16)-60, 2);
    bw_snd_player_state(5, &ps);
    BW_CHECK_EQ("params: fader stored", ps.player_fader, -60);
    REQ("params: fader gain", fabs(ps.gain - pow(10.0, -0.3)) < 1e-12);
    /* SND_SetPlayerTempoRatio: offset 0x1A, stored for driver info. */
    send1(BW_SND_CMD_PLAYER_PARAM, 5, 0x1A, 512, 2);
    bw_snd_player_state(5, &ps);
    BW_CHECK_EQ("params: tempo ratio stored", ps.tempo_ratio, 512);

    ref = bw_audio_player_create_source(&src, BW_SND_SEQ_MAX_SECONDS, 0);
    REQ("params: reference player 2", ref != NULL);
    a = (s16 *)malloc(8192 * 2 * sizeof(s16));
    b = (s16 *)malloc(8192 * 2 * sizeof(s16));
    REQ("params: buffers", a != NULL && b != NULL);
    bw_snd_render(a, 8192);
    bw_audio_player_mix(ref, b, 8192);
    for (k = 0; k < 8192 * 2; k++) {
        if (abs(b[k]) > 2000) {
            ratio_sum += (double)a[k] / (double)b[k];
            ratio_n++;
        }
    }
    REQ("params: some loud samples", ratio_n > 100);
    REQ("params: -6.0 dB is half",
               fabs(ratio_sum / ratio_n - pow(10.0, -0.3)) < 0.01);

    /* MASTER_VOLUME 0 silences everything. */
    send1(BW_SND_CMD_MASTER_VOLUME, 0, 0, 0, 0);
    bw_snd_render(a, 2048);
    for (k = 0, diff = 0; k < 2048 * 2; k++)
        diff += a[k] != 0;
    BW_CHECK_EQ("params: master volume 0 is silence", diff, 0);
    send1(BW_SND_CMD_MASTER_VOLUME, 127, 0, 0, 0);

    /* PAUSE holds the sequence: no ticks pass. */
    bw_arm7_step();
    {
        unsigned long t0 = g_sw->player[5].tickCounter;
        send1(BW_SND_CMD_PAUSE_SEQ, 5, 1, 0, 0);
        bw_snd_render(NULL, 8192);
        bw_arm7_step();
        BW_CHECK_EQ("params: paused, no ticks", g_sw->player[5].tickCounter, t0);
        BW_CHECK_EQ("params: paused, still active", g_sw->playerStatus, 1u << 5);
        send1(BW_SND_CMD_PAUSE_SEQ, 5, 0, 0, 0);
        bw_snd_render(NULL, 8192);
        bw_arm7_step();
        REQ("params: resumed, ticks move", g_sw->player[5].tickCounter > t0);
    }

    /* SND_InvalidateBankData over the bank stops the player using it. */
    send1(BW_SND_CMD_INVALIDATE_BANK, ptr_arg(l.bank, DS_BANK),
          ptr_arg(l.bank + 0x100, DS_BANK + 0x100), 0, 0);
    BW_CHECK_EQ("params: invalidated bank stops player", g_sw->playerStatus, 0);

    free(a);
    free(b);
    bw_audio_player_free(ref);
}

static void test_alarms(void)
{
    u32 w, words = 0, last = 0;
    BwSndStats st;
    int i;

    boot();
    bind_shared();
    /* SND_SetupAlarm(2, tick, period, ...) with alarm id 7: first after
     * 1000 OS ticks, then every 2000; SND_StartTimer(0, 0, 1 << 2, 0). */
    send1(BW_SND_CMD_SETUP_ALARM, 2, 1000, 2000, 7);
    send1(BW_SND_CMD_START_TIMER, 0, 0, 1u << 2, 0);
    while (bw_pxi_arm9_recv(&w) == 0) {
    }
    /* 32768 frames = 1 s = 523655 OS ticks: fires at 1000, 3000, ...; the
     * ARM9 takes the words as an audio callback's worth is mixed. */
    for (i = 0; i < 32; i++) {
        bw_snd_render(NULL, 1024);
        bw_arm7_step();
        while (bw_pxi_arm9_recv(&w) == 0) {
            if (BW_PXI_TAG_OF(w) == BW_PXI_TAG_SOUND) {
                words++;
                last = BW_PXI_DATA_OF(w);
            }
            bw_arm7_step();
        }
    }
    bw_snd_stats(&st);
    BW_CHECK_EQ("alarm: firings in one second", st.alarms_fired, 262);
    BW_CHECK_EQ("alarm: words delivered to the ARM9", words, 262);
    BW_CHECK_EQ("alarm: word is alarmNo | id << 8", last, 2u | (7u << 8));

    send1(BW_SND_CMD_STOP_TIMER, 0, 0, 1u << 2, 0);
    bw_snd_render(NULL, 32768);
    bw_snd_stats(&st);
    BW_CHECK_EQ("alarm: stopped", st.alarms_fired, 262);
}

static void test_driver_info(void)
{
    Loaded l;
    u8 *info;

    if (sizeof(void *) != 4) {
        printf("  (driver info: DS layout only with 4-byte pointers, skipped)\n");
        return;
    }
    boot();
    bind_shared();
    load_synthetic(&l);
    send_prepare(BW_SND_CMD_START_SEQ, 2, &l);
    send1(BW_SND_CMD_TRACK_PARAM, 2u | (2u << 24), 1u << 1, 0x0A,
          (unsigned long)(u16)(s16)-120);
    send1(BW_SND_CMD_LOCK_CHANNEL, 0x8000, 0, 0, 0);
    info = ds_mem(DS_INFO, 0x11E0);
    memset(info, 0xCC, 0x11E0);
    send1(BW_SND_CMD_READ_DRIVER_INFO, ptr_arg(info, DS_INFO), 0, 0, 0);
    /* work.player[2].tracks[0..1] are track ids, then 0xFF. */
    BW_CHECK_EQ("info: player 2 track 0 id", info[0x540 + 2 * 0x24 + 8], 0);
    BW_CHECK_EQ("info: player 2 track 1 id", info[0x540 + 2 * 0x24 + 9], 1);
    BW_CHECK_EQ("info: player 2 track 2 none", info[0x540 + 2 * 0x24 + 10], 0xFF);
    BW_CHECK_EQ("info: player 0 no tracks", info[0x540 + 8], 0xFF);
    BW_CHECK_EQ("info: track 1 extFader",
                (s16)bw_rd16(info + 0x780 + 1 * 0x40 + 0x0A), -120);
    BW_CHECK_EQ("info: track 0 volume", info[0x780 + 4], 127);
    BW_CHECK_EQ("info: track 1 no channel list", bw_rd32(info + 0x780 + 0x40 + 0x3C), 0);
    BW_CHECK_EQ("info: locked channels", bw_rd32(info + 0x11C4), 0x8000);
}

/* ------------------------------------------------------------------ */
/* Real data                                                           */
/* ------------------------------------------------------------------ */

/* Copy SDAT file `file_id` into main RAM at `ds`; returns host and size. */
static u8 *load_file(const BwSdat *sd, u32 file_id, u32 ds, u32 *size)
{
    BwBlob b;
    u8 *h;
    if (bw_sdat_raw_file(sd, file_id, &b) != 0)
        return NULL;
    h = ds_mem(ds, (u32)b.size);
    memcpy(h, b.data, b.size);
    *size = (u32)b.size;
    return h;
}

/* The game's loader, roughly: the SSEQ, its SBNK and the bank's SWARs
 * into main RAM, then SND_AssignWaveArc for each slot. */
static int load_real(const BwSdat *sd, u32 seq, Loaded *l)
{
    BwSdatSeqRecord rec;
    BwSdatBankRecord brec;
    u32 at = DS_REAL, size, file_id;
    int i;

    memset(l, 0, sizeof(*l));
    if (bw_sdat_seq_record(sd, seq, &rec) != 0 || bw_sdat_bank_record(sd, rec.bank, &brec) != 0)
        return -1;
    l->ds_seq = at;
    l->seq_file = load_file(sd, rec.file_id, at, &l->seq_size);
    at += (l->seq_size + 0xFFFu) & ~0xFFFu;
    l->ds_bank = at;
    l->bank = load_file(sd, brec.file_id, at, &l->bank_size);
    at += (l->bank_size + 0xFFFu) & ~0xFFFu;
    if (l->seq_file == NULL || l->bank == NULL)
        return -1;
    for (i = 0; i < 4; i++) {
        u8 *arc;
        wr32(l->bank + 0x18 + 8 * i, 0);
        wr32(l->bank + 0x1C + 8 * i, 0);
        if (brec.wave_arc[i] == 0xFFFF || bw_sdat_wavarc_fileid(sd, brec.wave_arc[i], &file_id) != 0)
            continue;
        arc = load_file(sd, file_id, at, &size);
        if (arc == NULL)
            continue;
        link_arc(l, i, arc, at);
        at += (size + 0xFFFu) & ~0xFFFu;
    }
    return 0;
}

static void test_real(const char *data_root)
{
    char path[1024];
    BwSdat sd;
    Loaded l;
    long seq, diff;
    BwAudioPlayer *ref;
    s16 peak;

    sprintf(path, "%s/wb_sound_data.sdat", data_root);
    if (bw_sdat_load(&sd, path) != 0) {
        printf("  FAIL cannot load %s\n", path);
        bw_failures++;
        return;
    }

    /* 1. A one-track sound effect, against the existing direct path. */
    seq = bw_sdat_find(&sd, BW_SDAT_SEQ, "SEQ_SE_DECIDE2");
    BW_CHECK_EQ("real: SEQ_SE_DECIDE2 is SEQ 1357", seq, 1357);
    if (seq >= 0) {
        boot();
        bind_shared();
        if (load_real(&sd, (u32)seq, &l) == 0) {
            ref = bw_audio_player_create(&sd, (u32)seq, BW_SND_SEQ_MAX_SECONDS, 0);
            send_prepare(BW_SND_CMD_START_SEQ, 1, &l);
            BW_CHECK_EQ("real: SE playing", g_sw->playerStatus, 2);
            diff = compare_render(ref, 32768 * 2, 1024, &peak);
            printf("  SEQ_SE_DECIDE2: 65536 frames, peak %d, %ld samples differ "
                   "from bw_audio_player_create\n", peak, diff);
            BW_CHECK_EQ("real: SE == SDAT direct path", diff, 0);
            REQ("real: SE sounds", peak > 1000);
            bw_audio_player_free(ref);
        } else {
            BW_CHECK_EQ("real: SE loaded", 0, 1);
        }
    }

    /* 2. A multi-track looping BGM, against engine/audio over the same
     * bytes with the SDK's offset reading. */
    seq = bw_sdat_find(&sd, BW_SDAT_SEQ, "SEQ_BGM_GYM");
    if (seq >= 0) {
        BwAudioSeqSource src;
        BwSndPlayerState ps;
        int i;

        boot();
        bind_shared();
        if (load_real(&sd, (u32)seq, &l) == 0) {
            direct_source(&l, &src);
            for (i = 0; i < 4; i++) {
                u32 v = bw_rd32(l.bank + 0x18 + 8 * i);
                size_t avail;
                const u8 *arc = (const u8 *)bw_snd_resolve(v, &avail);
                src.wave_arc[i].data = NULL;
                src.wave_arc[i].size = 0;
                if (arc != NULL) {
                    src.wave_arc[i].data = arc;
                    src.wave_arc[i].size = bw_rd32(arc + 8);
                }
            }
            send_prepare(BW_SND_CMD_START_SEQ, 0, &l);
            bw_snd_player_state(0, &ps);
            BW_CHECK_EQ("real: BGM loops", ps.loops, 1);
            ref = bw_audio_player_create_source(&src, BW_SND_SEQ_MAX_SECONDS, 1);
            REQ("real: BGM reference", ref != NULL);
            diff = compare_render(ref, 32768 * 10, 512, &peak);
            printf("  SEQ_BGM_GYM: 10 s, peak %d, %ld samples differ from "
                   "engine/audio over the same bytes\n", peak, diff);
            BW_CHECK_EQ("real: BGM == engine/audio direct", diff, 0);
            REQ("real: BGM sounds", peak > 1000);
            bw_audio_player_free(ref);

            /* What the SDAT path's file-absolute reading does instead. */
            {
                BwAudioRender r1, r2;
                BwAudioSeqSource sdat_src;
                if (bw_audio_seq_source_sdat(&sd, (u32)seq, -1, &sdat_src) == 0
                    && bw_audio_render_source(&sdat_src, 20.0, &r1) == 0) {
                    src.volume = sdat_src.volume;
                    if (bw_audio_render_source(&src, 20.0, &r2) == 0) {
                        printf("  SEQ_BGM_GYM: %d note events with the SDK's "
                               "offsets, %d with the oracle's\n",
                               r2.note_events_total, r1.note_events_total);
                        bw_audio_render_free(&r2);
                    }
                    bw_audio_render_free(&r1);
                }
            }
        } else {
            BW_CHECK_EQ("real: BGM loaded", 0, 1);
        }
    }
    bw_sdat_close(&sd);
}

int main(int argc, char **argv)
{
    test_wire();
    test_sequence();
    test_params();
    test_alarms();
    test_driver_info();
    if (argc > 1 && argv[1] != NULL && argv[1][0] != '\0') {
        bw_set_data_root(argv[1]);
        printf("data root: %s\n", bw_data_root());
        test_real(bw_data_root());
    } else {
        printf("  (no data root: the real-sequence checks are skipped)\n");
    }
    BW_REPORT("test_snd");
}
