/*
 * sdk/arm7/tp.c -- the ARM7's touch panel, PXI tag 6, answered from the
 * port's input state (sdk/input.h).
 *
 * Why a stand-in: sdk/arm7/arm7.h.  The protocol, from the matched ARM9
 * side of libspi's tp.o (aletheia src/matched/arm9_sdk/):
 *
 * Requests.  Every word carries a start bit (25) on the first word of a
 * command and an end bit (24) on the last; the first word has the command
 * in bits 8-14 and a data byte in bits 0-7, a later word its index in bits
 * 16-23 and 16 data bits.  The ARM9 sends:
 *
 *   TP_RequestSamplingAsync (func_0208AB1C.c)    0x03000000: command 0,
 *       one word.  Take one sample now.
 *   TP_RequestAutoSamplingStartAsync (func_0208AC0C.c)  0x02000100|freq
 *       then 0x01010000|vcount: command 1, two words.  Take `freq` samples
 *       a frame, the first at line `vcount`, spaced evenly over the frame.
 *   TP_RequestAutoSamplingStopAsync (func_0208AD14.c)   0x03000200:
 *       command 2.
 *   (TP_RequestSetStability, command 3, is not in the game's closure; it is
 *   accepted and acknowledged.)
 *
 * Replies.  TPi_TpCallback (TPi_TpCallback.c) takes the low halfword as
 * `command << 8 | result` and acts only on a word with the end bit, except
 * for command 0x10, the automatic sample notification, which it takes
 * whatever else the word holds: it advances its ring index and copies the
 * sample from HW_TOUCHPANEL_BUF.  Results: 0 success, 1 invalid command
 * (the ARM9 calls OS_Terminate), 2 invalid parameter, 3 illegal status, 4
 * exclusive.  Replies here carry both the start and end bits, the way the
 * firmware answers a one-word exchange.  The parameter limits are the
 * SDK's: at most 4 samples a frame (SPI_TP_SAMPLING_FREQUENCY_MAX) and a
 * line below 263 (HW_LCD_LINES).  Starting automatic sampling while it is
 * on, or stopping it while it is off, is an illegal status.
 *
 * The sample.  HW_TOUCHPANEL_BUF at 0x02FFFFAA, read as two halfwords
 * (TPi_ReadRaw): x bits 0-11, y bits 12-23, touch bit 24, validity bits
 * 25-26 (bit 0 of validity: x unreliable, bit 1: y).  The host's stylus is
 * never unreliable, so validity is 0.
 *
 * The panel.  The ARM9 turns raw readings into pixels with the calibration
 * in the user settings at 0x02FFFC80 (TP_GetUserInfo, func_0208A998.c: two
 * raw points and the pixels they were taken at, at +0x58), through
 * TP_CalcCalibrateParam (func_0208AF18.c), TP_SetCalibrateParam
 * (func_0208AA2C.c) and TP_GetCalibratedPoint (func_0208B104.c).  The
 * firmware leaves those settings in main RAM before the game runs; the
 * stand-in writes the calibration part of them at power-on and models the
 * panel as the straight line through the same two points, so the game's
 * own arithmetic carries a host point back to the pixel it was taken at.
 * A host point is the centre of a pixel, (x + 0.5, y + 0.5), so the
 * truncation in TP_GetCalibratedPoint has half a pixel of margin either
 * way.  The default points are chosen to be exact (14 raw units a pixel
 * across, 18 down, both whole numbers in TP_CalcCalibrateParam's 8.8
 * format) and are a replaceable table, not a constant of the port.
 *
 * Timing.  A single sample is taken when the request arrives.  Automatic
 * samples are taken at the frame boundary (bw_arm7_tp_frame, from
 * bw_input_frame), all from that frame's input, since the host reports the
 * stylus once a frame; a notification is not queued while the ARM9's
 * receive FIFO is full, where the firmware's sample would be lost too.
 */

#include <string.h>

#include "arm7.h"
#include "dsmem.h"
#include "input.h"
#include "pxi.h"

#define TP_START_BIT    0x02000000u
#define TP_END_BIT      0x01000000u

#define TP_CMD_SAMPLING         0x00u
#define TP_CMD_AUTO_ON          0x01u
#define TP_CMD_AUTO_OFF         0x02u
#define TP_CMD_SETUP_STABILITY  0x03u
#define TP_CMD_AUTO_SAMPLING    0x10u

#define TP_RESULT_SUCCESS           0u
#define TP_RESULT_INVALID_COMMAND   1u
#define TP_RESULT_INVALID_PARAMETER 2u
#define TP_RESULT_ILLEGAL_STATUS    3u

#define TP_FREQUENCY_MAX    4u
#define TP_LCD_LINES        263u

#define TP_RAW_BUF          0x02FFFFAAu
#define TP_USER_INFO_CAL    (0x02FFFC80u + 0x58u)

static const BwArm7TpCalibration k_default_cal = {
    /* raw_x1 = 192 + 14 * 32, raw_y1 = 224 + 18 * 32 at pixel (32, 32);
     * raw_x2 = 192 + 14 * 224, raw_y2 = 224 + 18 * 160 at (224, 160).     */
    640, 800, 32, 32,
    3328, 3104, 224, 160,
};

static struct {
    BwArm7TpCalibration cal;
    int has_cal;
    BwArm7TpState st;
    /* The command being assembled.                                         */
    int in_command;
    u32 command;
    u32 next_index;
    u32 data[2];
} g_tp;

/* ------------------------------------------------------------------ */
/* The panel                                                           */
/* ------------------------------------------------------------------ */

static const BwArm7TpCalibration *cal(void)
{
    return g_tp.has_cal ? &g_tp.cal : &k_default_cal;
}

void bw_arm7_tp_set_calibration(const BwArm7TpCalibration *c)
{
    if (c && c->dx1 != c->dx2 && c->dy1 != c->dy2) {
        g_tp.cal = *c;
        g_tp.has_cal = 1;
    } else {
        g_tp.has_cal = 0;
    }
}

void bw_arm7_tp_calibration(BwArm7TpCalibration *out)
{
    if (out)
        *out = *cal();
}

/* floor(n / d) for d > 0.                                                  */
static s32 floor_div(s32 n, s32 d)
{
    s32 q = n / d;

    return (n % d != 0 && n < 0) ? q - 1 : q;
}

/*
 * The raw reading at the centre of pixel `p` on an axis whose calibration
 * points are (d1, r1) and (d2, r2):
 *     r1 + (p + 1/2 - d1) * (r2 - r1) / (d2 - d1),
 * rounded to the nearest unit and held to the converter's 12 bits.
 */
static u32 axis_raw(int p, int r1, int d1, int r2, int d2)
{
    s32 num = (2 * (s32)(p - d1) + 1) * (s32)(r2 - r1);
    s32 den = 2 * (s32)(d2 - d1);
    s32 v;

    if (den < 0) {
        num = -num;
        den = -den;
    }
    v = r1 + floor_div(2 * num + den, 2 * den);
    return v < 0 ? 0u : v > 0xFFF ? 0xFFFu : (u32)v;
}

void bw_arm7_tp_raw_of(int x, int y, u32 *raw_x, u32 *raw_y)
{
    const BwArm7TpCalibration *c = cal();

    if (raw_x)
        *raw_x = axis_raw(x, c->raw_x1, c->dx1, c->raw_x2, c->dx2);
    if (raw_y)
        *raw_y = axis_raw(y, c->raw_y1, c->dy1, c->raw_y2, c->dy2);
}

/* Take one sample of the stylus into HW_TOUCHPANEL_BUF.                    */
static void sample(void)
{
    BwInputState s;
    u32 word = 0;
    volatile u16 *buf = (volatile u16 *)bw_ds_ptr(TP_RAW_BUF);

    bw_input_current(&s);
    if (s.touch && !s.lid_closed) {
        u32 rx, ry;

        bw_arm7_tp_raw_of(s.touch_x, s.touch_y, &rx, &ry);
        word = rx | (ry << 12) | (1u << 24);
    }
    buf[0] = (u16)word;
    buf[1] = (u16)(word >> 16);
    g_tp.st.samples++;
}

/* The calibration points in the user settings, as the firmware leaves them.*/
static void write_user_info(void)
{
    const BwArm7TpCalibration *c = cal();
    volatile u8 *p = (volatile u8 *)bw_ds_ptr(TP_USER_INFO_CAL);

    p[0] = (u8)c->raw_x1;
    p[1] = (u8)(c->raw_x1 >> 8);
    p[2] = (u8)c->raw_y1;
    p[3] = (u8)(c->raw_y1 >> 8);
    p[4] = (u8)c->dx1;
    p[5] = (u8)c->dy1;
    p[6] = (u8)c->raw_x2;
    p[7] = (u8)(c->raw_x2 >> 8);
    p[8] = (u8)c->raw_y2;
    p[9] = (u8)(c->raw_y2 >> 8);
    p[10] = (u8)c->dx2;
    p[11] = (u8)c->dy2;
}

/* ------------------------------------------------------------------ */
/* The responder                                                       */
/* ------------------------------------------------------------------ */

static void reply(u32 command, u32 result)
{
    (void)bw_pxi_arm7_send(BW_PXI_TAG_TOUCHPANEL,
                           TP_START_BIT | TP_END_BIT | (command << 8) | result,
                           0);
}

static u32 run(u32 command)
{
    switch (command) {
    case TP_CMD_SAMPLING:
        sample();
        return TP_RESULT_SUCCESS;
    case TP_CMD_AUTO_ON: {
        u32 freq = g_tp.data[0] & 0xFFu;
        u32 vcount = g_tp.data[1] & 0xFFFFu;

        if (g_tp.next_index != 2 || freq == 0 || freq > TP_FREQUENCY_MAX ||
            vcount >= TP_LCD_LINES)
            return TP_RESULT_INVALID_PARAMETER;
        if (g_tp.st.auto_on)
            return TP_RESULT_ILLEGAL_STATUS;
        g_tp.st.auto_on = 1;
        g_tp.st.frequence = freq;
        g_tp.st.vcount = vcount;
        return TP_RESULT_SUCCESS;
    }
    case TP_CMD_AUTO_OFF:
        if (!g_tp.st.auto_on)
            return TP_RESULT_ILLEGAL_STATUS;
        g_tp.st.auto_on = 0;
        return TP_RESULT_SUCCESS;
    case TP_CMD_SETUP_STABILITY:
        g_tp.st.stability = g_tp.data[0] & 0xFFu;
        return TP_RESULT_SUCCESS;
    default:
        return TP_RESULT_INVALID_COMMAND;
    }
}

static void tp_callback(int tag, u32 data, int err)
{
    (void)tag;
    if (err)
        return;
    g_tp.st.words++;
    if (data & TP_START_BIT) {
        g_tp.in_command = 1;
        g_tp.command = (data >> 8) & 0x7Fu;
        g_tp.data[0] = data & 0xFFu;
        g_tp.data[1] = 0;
        g_tp.next_index = 1;
    } else if (g_tp.in_command && ((data >> 16) & 0xFFu) == g_tp.next_index &&
               g_tp.next_index < 2) {
        g_tp.data[g_tp.next_index] = data & 0xFFFFu;
        g_tp.next_index++;
    } else {
        /* A continuation with no command open, or out of order: the
         * firmware would discard it, and so does this.                     */
        g_tp.in_command = 0;
        g_tp.st.discarded++;
        return;
    }
    if (data & TP_END_BIT) {
        g_tp.in_command = 0;
        g_tp.st.last_command = g_tp.command;
        g_tp.st.requests++;
        reply(g_tp.command, run(g_tp.command));
    }
}

void bw_arm7_tp_frame(void)
{
    u32 i;

    if (!g_tp.st.auto_on || !bw_pxi_powered())
        return;
    for (i = 0; i < g_tp.st.frequence; i++) {
        sample();
        if (bw_pxi_arm9_pending() >= BW_PXI_FIFO_DEPTH) {
            g_tp.st.dropped++;
            continue;
        }
        (void)bw_pxi_arm7_send(BW_PXI_TAG_TOUCHPANEL,
                               TP_START_BIT | TP_END_BIT |
                               (TP_CMD_AUTO_SAMPLING << 8) | i, 0);
    }
}

const BwArm7TpState *bw_arm7_tp_state(void)
{
    return &g_tp.st;
}

void bw_arm7_tp_reset(void)
{
    BwArm7TpCalibration keep = g_tp.cal;
    int has = g_tp.has_cal;

    /* The calibration is the panel's, not the firmware's state: it
     * survives power-off.                                                  */
    memset(&g_tp, 0, sizeof g_tp);
    g_tp.cal = keep;
    g_tp.has_cal = has;
}

void bw_arm7_tp_init(void)
{
    bw_arm7_tp_reset();
    write_user_info();
    bw_pxi_arm7_set_callback(BW_PXI_TAG_TOUCHPANEL, tp_callback);
}
