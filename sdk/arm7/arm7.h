/*
 * sdk/arm7/arm7.h -- the stand-in for the ARM7 on the other end of PXI.
 *
 * The DS's ARM7 runs its own firmware (the SDK's ARM7 component), which is
 * not in the ARM9 binary aletheia matches and could not run on a host if it
 * were.  What the ARM9 needs from it during boot and play is a set of
 * answers: one responder per PXI tag, each replying with the words the real
 * firmware sends and writing the shared-memory blocks the real firmware
 * writes, so the ARM9 callbacks the matched code installs fire as they do
 * on hardware.  sdk/pxi.h describes the wire and when the ARM7 runs.
 *
 *   arm7.c   power-on, the step function, OS (tag 12) and the
 *            OSi_SyncWithOtherProc handshake in shared memory
 *   pm.c     power manager (tag 8)
 *   rtc.c    real-time clock (tag 5), answered from the host clock
 *   ctrdg.c  Slot-2 cartridge (tag 13)
 *   card.c   Slot-1 card pull-out (tag 14)
 *   fs.c     Slot-1 backup commands (tag 11, PXI_FIFO_TAG_FS)
 *   snd.c    sound command lists (tag 7): the transport, sdk/arm7/snd.h
 *   tp.c     touch panel (tag 6), sampled from sdk/input.h's stylus
 *   pad.c    X, Y, DEBUG and the hinge in HW_BUTTON_XY_BUF (no tag)
 *   snd_driver.c  the sound driver behind it, on engine/audio (eidos
 *            library only; the transport links without it)
 *
 * Each responder's protocol comes from the matched ARM9 side of the same
 * SDK module (aletheia src/matched/arm9_sdk/, named in each file) and from
 * the SDK's DWARF; nothing here is taken from ARM7 code.
 */

#ifndef BW_PORT_ARM7_H
#define BW_PORT_ARM7_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Power the ARM7 on: install every responder (which publishes it in
 * pxiHandleChecker[1]) and write the shared-memory state the ARM7 firmware
 * leaves there before the ARM9's SDK reads it.  Idempotent until
 * bw_arm7_reset().  The PXI_InitFifo shim calls it, so a boot needs no
 * explicit call; a port that wants the ARM7 up earlier may call it after
 * bw_pxi_reset().
 */
void bw_arm7_boot(void);

/* Back to power-off: every responder's state cleared, not booted.         */
void bw_arm7_reset(void);

/*
 * Let the ARM7 run until it would wait: PXI service (sdk/pxi.h) and the
 * shared-memory handshakes.  The CPU stand-ins call this wherever the DS's
 * ARM9 would be waiting for the ARM7 (SVC_WaitByLoop, OS_SpinWaitSysCycles)
 * or would take an interrupt (OS_RestoreInterrupts, OS_EnableInterrupts).
 */
void bw_arm7_step(void);

/* ------------------------------------------------------------------ */
/* Responders: installed by bw_arm7_boot, exposed for tests            */
/* ------------------------------------------------------------------ */

void bw_arm7_os_init(void);
void bw_arm7_pm_init(void);
void bw_arm7_rtc_init(void);
void bw_arm7_ctrdg_init(void);
void bw_arm7_card_init(void);
void bw_arm7_fs_init(void);
void bw_arm7_snd_init(void);
void bw_arm7_tp_init(void);
void bw_arm7_pad_init(void);

/* The shared-memory handshake's progress: 1 once both directions are done. */
int bw_arm7_os_synced(void);

/* What the ARM9 asked the OS responder for (0 if nothing yet).            */
u32 bw_arm7_os_last_command(void);

/* The power manager's model, as the PMIC would report it.                 */
typedef struct BwArm7PmState {
    u16 amp;            /* sound amplifier, PM_SetAmp                       */
    u16 backlight;      /* bit 3 top, bit 2 bottom (PM_GetBackLight)        */
    u16 led;            /* PMi_SetLED status                                */
    u16 led_pattern;    /* PM_SetLEDPattern                                 */
    u16 battery_low;    /* PM_GetBattery                                    */
    u16 powered_off;    /* PM_ForceToPowerOff was asked for                 */
} BwArm7PmState;
const BwArm7PmState *bw_arm7_pm_state(void);

/*
 * The clock the RTC responder reads.  NULL (the default) is the host's
 * local time; a non-NULL time is used as is on every read (tests).
 */
typedef struct BwArm7Time {
    int year;           /* 2000-2099                                        */
    int month;          /* 1-12                                             */
    int day;            /* 1-31                                             */
    int week;           /* 0 = Sunday                                       */
    int hour;           /* 0-23                                             */
    int minute;
    int second;
} BwArm7Time;
void bw_arm7_rtc_fix_time(const BwArm7Time *t);

/*
 * The sound responder's completion counter, mirrored to an extra word.  The
 * ARM9 reads SNDi_SharedWork->finishCommandTag, which the driver
 * (sdk/arm7/snd.h) writes once the SHARED_WORK command has named it; this
 * binding is for a transport running without a driver.  NULL unbinds.
 */
void bw_arm7_snd_bind(volatile u32 *finish_command_tag);
u32  bw_arm7_snd_lists(void);

/* Words the ARM9 sent on the Slot-2 tag.                                 */
u32 bw_arm7_ctrdg_words(void);

/* Backup requests seen on tag 11, and the last request type.              */
u32 bw_arm7_fs_requests(void);
u32 bw_arm7_fs_last_request(void);

/* ------------------------------------------------------------------ */
/* Input: the keypad's ARM7 half and the touch panel                   */
/* ------------------------------------------------------------------ */

/*
 * The ARM7's frame: publish X, Y, DEBUG and the hinge (sdk/arm7/pad.c), and
 * take the automatic touch samples the ARM9 asked for (sdk/arm7/tp.c).
 * bw_input_frame (sdk/input.h) calls both once per frame.
 */
void bw_arm7_pad_frame(void);
void bw_arm7_tp_frame(void);

/* Power-off for the touch panel: automatic sampling stopped, the command
 * being assembled dropped (bw_arm7_reset calls it).                       */
void bw_arm7_tp_reset(void);

/*
 * The panel's calibration, in the user settings' own layout (0x02FFFC80 +
 * 0x58, read by TP_GetUserInfo): two raw readings and the pixels they were
 * taken at.  The stand-in writes it into the user settings at power-on and
 * models the panel as the line through the two points (sdk/arm7/tp.c).
 * NULL, or points that share a pixel on either axis, restores the default.
 * Takes effect at the next bw_arm7_tp_init (ARM7 power-on) for the user
 * settings and at once for samples.
 */
typedef struct BwArm7TpCalibration {
    u16 raw_x1, raw_y1;
    u8  dx1, dy1;
    u16 raw_x2, raw_y2;
    u8  dx2, dy2;
} BwArm7TpCalibration;
void bw_arm7_tp_set_calibration(const BwArm7TpCalibration *c);
void bw_arm7_tp_calibration(BwArm7TpCalibration *out);

/* The raw reading the panel gives at the centre of bottom-screen pixel
 * (x, y), under the current calibration.                                  */
void bw_arm7_tp_raw_of(int x, int y, u32 *raw_x, u32 *raw_y);

typedef struct BwArm7TpState {
    int auto_on;        /* automatic sampling running                       */
    u32 frequence;      /* samples a frame                                  */
    u32 vcount;         /* the first sample's line                          */
    u32 stability;      /* TP_RequestSetStability's range                   */
    u32 words;          /* words received on tag 6                          */
    u32 requests;       /* commands completed                               */
    u32 last_command;
    u32 discarded;      /* continuation words with no command open         */
    u32 samples;        /* samples written to HW_TOUCHPANEL_BUF             */
    u32 dropped;        /* automatic notifications lost to a full FIFO      */
} BwArm7TpState;
const BwArm7TpState *bw_arm7_tp_state(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_ARM7_H */
