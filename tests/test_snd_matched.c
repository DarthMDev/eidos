/*
 * tests/test_snd_matched.c -- the game's own SND calls reaching the driver.
 *
 * The ARM9 half here is aletheia's matched snd_command.o, snd_interface.o,
 * snd_alarm.o, snd_work.o and snd_bank.o functions, compiled as written
 * (through tools/dsaddr.py) and called the way the game's sound layer calls
 * them: SND_AlarmInit and SND_CommandInit at start-up, SND_AssignWaveArc
 * after loading a bank, SND_PrepareSeq and the start command, player
 * parameters, an alarm, driver info, SND_StopSeq.  Their command lists go
 * over the FIFO to sdk/arm7/snd.c, which finds them in sCommandArray
 * registered at its DS address (the transport's default pool), and the
 * driver plays them.  What it plays is compared with engine/audio over the
 * same bytes and must be identical.
 *
 * The sequence data sits in host memory the DS address space knows nothing
 * about, as the game's heap does, so the driver resolves seqBase, the bank
 * and the wave archive from the pointers alone and sizes them from their
 * file headers.
 *
 * Built only with 4-byte pointers: the matched files reach sFreeList
 * through views that agree with each other only where a pointer is a word
 * (SND_FlushCommand.c and func_02089978.c lay out the same nine words
 * differently around the members each does not use).
 */

#include "port_test.h"
#include "port.h"
#include "audio.h"
#include "dsmem.h"
#include "os.h"
#include "pxi.h"
#include "arm7/arm7.h"
#include "arm7/snd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQ(label, cond)                                                   \
    do {                                                                   \
        bw_checks++;                                                       \
        if (!(cond)) {                                                     \
            bw_failures++;                                                 \
            printf("  FAIL %-40s (%s)\n", (label), #cond);                 \
            return 1;                                                      \
        }                                                                  \
    } while (0)

/* ------------------------------------------------------------------ */
/* The matched functions, by the names their files define              */
/* ------------------------------------------------------------------ */

typedef void (*SndAlarmHandler)(void *arg);
typedef struct {
    unsigned short prgNo;
    unsigned char volume, volume2;
    signed char pitchBend;
    unsigned char bendRange, pan;
    signed char transpose;
    unsigned char pad_, chCount;
    unsigned char channel[16];
} SndTrackInfo;

void func_02089F88(void);                                   /* SND_AlarmInit      */
void func_020898A8(void);                                   /* SND_CommandInit    */
int  SND_FlushCommand(unsigned long flags);
void func_0208A2BC(void *bank, int index, void *wave_arc);  /* SND_AssignWaveArc  */
void func_02089350(int player, const void *base, unsigned long offset,
                   void *bank);                             /* SND_PrepareSeq     */
void func_02089804(int id, unsigned long a0, unsigned long a1, unsigned long a2,
                   unsigned long a3);                       /* PushCommand_impl   */
void func_02089330(int player);                             /* SND_StopSeq        */
void func_020897B4(int player, unsigned long offset, unsigned long data,
                   int size);                               /* SNDi_SetPlayerParam */
void func_0208970C(int volume);                             /* SND_SetMasterVolume */
void func_02089540(int alarm, unsigned long tick, unsigned long period,
                   SndAlarmHandler handler, void *arg);     /* SND_SetupAlarm     */
void func_020894B4(unsigned long ch, unsigned long cap, unsigned long alarm,
                   unsigned long flags);                    /* SND_StartTimer     */
void func_02089794(void *info);                             /* SND_ReadDriverInfo */
int  func_0208A0A4(void *info, int player, int track,
                   SndTrackInfo *out);                      /* SND_ReadTrackInfo  */
unsigned long func_02089D40(void);                          /* SND_GetCurrentCommandTag */
void func_02089CD4(unsigned long tag);                      /* SND_WaitForCommandProc */
unsigned long func_0208A07C(void);                          /* SND_GetChannelStatus */
void PXIi_HandlerRecvFifoNotEmpty(void);

/* ------------------------------------------------------------------ */
/* The matched files' data, at their DS addresses                      */
/* ------------------------------------------------------------------ */

/* SNDSharedWork as func_0208A1D0.c declares it.                           */
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

/* snd_command.o and snd_alarm.o .bss (SND_CommandInit's and
 * SNDi_IncAlarmId's relocations).  sFreeList is the nine words every
 * matched view of it agrees on. */
BwSndCommandWire sCommandArray[BW_SND_COMMAND_NUM];
unsigned long    sFreeList[9];
void            *sWaitingCommandListQueue[9];
SharedWork       sSharedWork;
SharedWork      *SNDi_SharedWork;
struct { void (*func)(void *); void *arg; unsigned char id; } sCallbackTable[8];

static void register_globals(void)
{
    bw_ds_register(0x02151740u, sCommandArray, sizeof(sCommandArray), "sCommandArray");
    bw_ds_register(0x0215146Cu, sFreeList, sizeof(sFreeList), "sFreeList");
    bw_ds_register(0x02151490u, sWaitingCommandListQueue,
                   sizeof(sWaitingCommandListQueue), "sWaitingCommandListQueue");
    bw_ds_register(0x021514C0u, &sSharedWork, sizeof(sSharedWork), "sSharedWork");
    bw_ds_register(0x02152FA0u, &SNDi_SharedWork, sizeof(SNDi_SharedWork),
                   "SNDi_SharedWork");
    bw_ds_register(0x02152F40u, sCallbackTable, sizeof(sCallbackTable), "sCallbackTable");
}

/* ------------------------------------------------------------------ */
/* The SDK names the matched files call that are not matched here       */
/* ------------------------------------------------------------------ */

void DC_FlushRange(const void *p, unsigned long n);
void DC_InvalidateRange(void *p, unsigned long n);
void DC_StoreRange(const void *p, unsigned long n);
void OS_SpinWaitSysCycles(unsigned long cycles);
int  OS_IsRunOnEmulator(void);
void SNDi_LockMutex(void);
void SNDi_UnlockMutex(void);
void OS_SetIrqFunction(u32 intr, void (*function)(void));
u32  OS_EnableIrqMask(u32 intr);

/* The host has one coherent memory: cache maintenance is nothing.         */
void DC_FlushRange(const void *p, unsigned long n) { (void)p; (void)n; }
void DC_InvalidateRange(void *p, unsigned long n) { (void)p; (void)n; }
void DC_StoreRange(const void *p, unsigned long n) { (void)p; (void)n; }
/* The ARM9's "wait for the other processor": the ARM7 runs.              */
void OS_SpinWaitSysCycles(unsigned long cycles) { (void)cycles; bw_arm7_step(); }
int  OS_IsRunOnEmulator(void) { return 0; }
/* One game thread here, so the SND mutex has nothing to exclude.         */
void SNDi_LockMutex(void) {}
void SNDi_UnlockMutex(void) {}
/* sdk/pxi.c's PXI_InitFifo shim names these; this test never runs it.     */
void OS_SetIrqFunction(u32 intr, void (*function)(void)) { (void)intr; (void)function; }
u32  OS_EnableIrqMask(u32 intr) { (void)intr; return 0; }

/* The CPU stand-in's safe point: where the DS's ARM7 would have run.      */
static void safe_point(void)
{
    bw_arm7_step();
}

/* ------------------------------------------------------------------ */
/* Synthetic sound data, in host memory the way the heap holds it      */
/* ------------------------------------------------------------------ */

static void wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v)
{
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

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

static const u8 k_seq_body[] = {
    0xFE, 0x03, 0x00,                   /* AllocTrack 0,1                 */
    0x93, 0x01, 0x14, 0x00, 0x00,       /* OpenTrack 1 at +0x14           */
    0xE1, 0x78, 0x00,                   /* Tempo 120                      */
    0x81, 0x00,                         /* Program 0                      */
    0x3C, 0x7F, 0x60,                   /* Note 60 vel 127 dur 96         */
    0x80, 0x30,                         /* Rest 48                        */
    0xFF,                               /* End                            */
    0x00,                               /* padding up to +0x14            */
    0x81, 0x01,                         /* +0x14 track 1: Program 1       */
    0x45, 0x50, 0x48,                   /* Note 69 vel 80 dur 72          */
    0xFF
};

static void region(u8 *r, u16 wave, u8 key, u8 pan)
{
    wr16(r, wave);
    wr16(r + 2, 0);
    r[4] = key;
    r[5] = 127;
    r[6] = 100;
    r[7] = 90;
    r[8] = 100;
    r[9] = pan;
}

static u8 *g_seq, *g_bank, *g_wave;
static u32 g_seq_size, g_bank_size, g_wave_size;

static int make_data(void)
{
    u32 i;

    g_seq_size = 0x1C + (u32)sizeof(k_seq_body);
    g_bank_size = 0x3C + 2 * 4 + 2 * 10;
    g_wave_size = 0x3C + 4 + 12 + 64;
    g_seq = (u8 *)calloc(1, g_seq_size);
    g_bank = (u8 *)calloc(1, g_bank_size);
    g_wave = (u8 *)calloc(1, g_wave_size);
    if (g_seq == NULL || g_bank == NULL || g_wave == NULL)
        return -1;

    nitro_header(g_seq, "SSEQ", g_seq_size);
    wr32(g_seq + 0x18, 0x1C);
    memcpy(g_seq + 0x1C, k_seq_body, sizeof(k_seq_body));

    nitro_header(g_bank, "SBNK", g_bank_size);
    wr32(g_bank + 0x38, 2);
    wr32(g_bank + 0x3C, 1u | (0x44u << 8));          /* wave instrument  */
    wr32(g_bank + 0x40, 3u | (0x4Eu << 8));          /* PSG noise        */
    region(g_bank + 0x44, 0, 60, 64);
    region(g_bank + 0x4E, 0, 69, 90);

    nitro_header(g_wave, "SWAR", g_wave_size);
    wr32(g_wave + 0x38, 1);
    wr32(g_wave + 0x3C, 0x40);
    g_wave[0x40] = 0;
    g_wave[0x41] = 1;
    wr16(g_wave + 0x42, 22050);
    wr16(g_wave + 0x44, 760);
    wr16(g_wave + 0x46, 0);
    wr32(g_wave + 0x48, 16);
    for (i = 0; i < 64; i++)
        g_wave[0x4C + i] = (u8)(s8)((i & 15) * 12 - 90);
    return 0;
}

/* ------------------------------------------------------------------ */

static int g_alarm_calls;
static void *g_alarm_arg;

static void on_alarm(void *arg)
{
    g_alarm_calls++;
    g_alarm_arg = arg;
}

int main(void)
{
    BwAudioSeqSource src;
    BwAudioPlayer *ref;
    BwSndStats st;
    BwSndPlayerState ps;
    SndTrackInfo ti;
    static u8 info[0x11E0];
    s16 a[2048], b[2048];
    long diff = 0;
    int i, k, peak = 0;

    bw_ds_reset();
    register_globals();
    bw_pxi_reset();
    bw_io_write16(BW_REG_IPCFIFOCNT,
                  (u16)(BW_FIFOCNT_ENABLE | BW_FIFOCNT_ERROR | BW_FIFOCNT_SEND_CLEAR));
    bw_os_cpsr_set(0);
    bw_os_set_safe_point(safe_point);
    bw_arm7_boot();

    /* Start-up, as SND_Init does it. */
    func_02089F88();
    func_020898A8();
    REQ("init: SNDi_SharedWork is sSharedWork", SNDi_SharedWork == &sSharedWork);
    BW_CHECK_EQ("init: SHARED_WORK list completed", sSharedWork.finishCommandTag, 1);
    BW_CHECK_EQ("init: transport found it in sCommandArray", bw_arm7_snd_unresolved(), 0);
    bw_snd_stats(&st);
    BW_CHECK_EQ("init: SHARED_WORK executed", st.by_id[BW_SND_CMD_SHARED_WORK], 1);

    /* The bank's wave archive, as the sound archive loader links it. */
    REQ("data", make_data() == 0);
    func_0208A2BC(g_bank, 0, g_wave);
    BW_CHECK_EQ("bank: waveArcLink[0].waveArc",
                bw_rd32(g_bank + 0x18), (u32)(uintptr_t)g_wave);

    /* SND_PrepareSeq, then SND_StartPreparedSeq's command, one flush. */
    func_02089350(4, g_seq + 0x1C, 0, g_bank);
    func_02089804(BW_SND_CMD_START_PREPARED_SEQ, 4, 0, 0, 0);
    SND_FlushCommand(0);
    func_02089CD4(func_02089D40());          /* SND_WaitForCommandProc */
    BW_CHECK_EQ("seq: player 4 playing", sSharedWork.playerStatus, 1u << 4);
    bw_snd_stats(&st);
    BW_CHECK_EQ("seq: no bad addresses", st.bad_address, 0);

    memset(&src, 0, sizeof(src));
    src.seq.data = g_seq + 0x1C;
    src.seq.size = g_seq_size - 0x1C;
    src.bank.data = g_bank;
    src.bank.size = g_bank_size;
    src.wave_arc[0].data = g_wave;
    src.wave_arc[0].size = g_wave_size;
    ref = bw_audio_player_create_source(&src, BW_SND_SEQ_MAX_SECONDS, 0);
    REQ("seq: reference", ref != NULL);
    for (i = 0; i < 32; i++) {
        bw_snd_render(a, 1024);
        bw_audio_player_mix(ref, b, 1024);
        for (k = 0; k < 2048; k++) {
            diff += a[k] != b[k];
            if (a[k] > peak)
                peak = a[k];
        }
    }
    bw_audio_player_free(ref);
    printf("  matched SND calls: 1 s rendered, peak %d, %ld samples differ\n", peak, diff);
    BW_CHECK_EQ("seq: game's SND path == engine/audio direct", diff, 0);
    REQ("seq: it sounds", peak > 1000);
    bw_arm7_step();
    BW_CHECK_EQ("seq: tick counter (120 BPM, 1 s)", sSharedWork.player[4].tickCounter, 96);
    BW_CHECK_EQ("seq: SND_GetChannelStatus reads the driver's word",
                func_0208A07C(), sSharedWork.channelStatus);

    /* SNDi_SetPlayerParam, as SND_SetPlayerVolume sends it. */
    func_020897B4(4, 6, (unsigned long)(u16)(s16)-240, 2);
    func_0208970C(64);                       /* SND_SetMasterVolume */
    SND_FlushCommand(0);
    bw_snd_player_state(4, &ps);
    BW_CHECK_EQ("param: player fader", ps.player_fader, -240);
    BW_CHECK_EQ("param: master volume", bw_snd_master_volume(), 64);

    /* SND_ReadDriverInfo, then SND_ReadTrackInfo over it. */
    func_02089794(info);
    SND_FlushCommand(0);
    func_02089CD4(func_02089D40());
    BW_CHECK_EQ("info: track 0 of player 4", func_0208A0A4(info, 4, 0, &ti), 1);
    BW_CHECK_EQ("info: its volume", ti.volume, 127);
    BW_CHECK_EQ("info: no channels listed", ti.chCount, 0);
    BW_CHECK_EQ("info: track 1 of player 4", func_0208A0A4(info, 4, 1, &ti), 1);
    BW_CHECK_EQ("info: no track 2", func_0208A0A4(info, 4, 2, &ti), 0);
    BW_CHECK_EQ("info: player 3 has none", func_0208A0A4(info, 3, 0, &ti), 0);

    /* SND_SetupAlarm and SND_StartTimer: the ARM7's word comes back through
     * PxiFifoCallback to SNDi_CallAlarmHandler and the game's handler. */
    func_02089540(3, 500, 0, on_alarm, &sSharedWork);
    func_020894B4(0, 0, 1u << 3, 0);
    SND_FlushCommand(0);
    bw_snd_render(NULL, 1024);               /* 16365 OS ticks */
    bw_arm7_step();
    PXIi_HandlerRecvFifoNotEmpty();
    BW_CHECK_EQ("alarm: the game's handler ran", g_alarm_calls, 1);
    BW_CHECK_EQ("alarm: with its argument", g_alarm_arg == (void *)&sSharedWork, 1);

    /* SND_StopSeq. */
    func_02089330(4);
    SND_FlushCommand(0);
    func_02089CD4(func_02089D40());
    BW_CHECK_EQ("stop: status clear", sSharedWork.playerStatus, 0);
    BW_CHECK_EQ("stop: every list completed, none lost", bw_arm7_snd_unresolved(), 0);
    /* sFreeList word 8 is currentTag, one past the last list flushed. */
    BW_CHECK_EQ("stop: one completion per list flushed",
                sSharedWork.finishCommandTag, sFreeList[8] - 1);

    BW_REPORT("test_snd_matched");
}
