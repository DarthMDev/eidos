/*
 * sdk/arm7/pm.c -- the ARM7's power manager, PXI tag 8.
 *
 * Why a stand-in: sdk/arm7/arm7.h.  The protocol, from the matched ARM9
 * side of libspi's pm.o (aletheia src/matched/arm9_sdk/):
 *
 * Requests are packets of one or more words (PMi_TryToSendPxiData).  In
 * each word's 26 data bits, bit 25 marks the first word and bit 24 the
 * last, so a one-word packet carries both (PMi_SendSleepStart's
 * 0x03006000).  The first word holds the command in bits 8-14 and an 8-bit
 * argument below; a second word carries a 16-bit parameter
 * (PM_SendUtilityCommandAsync: 0x02006100 | number, 0x01010000 | param).
 *
 * The reply is one word, `command << 8 | result`, read by PMi_CommonCallback
 * (func_0208B950.c):
 *   0x60  sleep preparation, result ignored
 *   0x61  utility command: the low byte is the value, stored through the
 *         request's retValue pointer; the callback then reports success
 *   0x62  sleep start, the low byte is the result (0 = success)
 *   0x63  the ARM7 woke up: sets the sleep-end flag
 *   0x70  ARM7-initiated notices (power button, battery low); never sent
 *
 * Utility command numbers, from their ARM9 callers:
 *   1-3   LED (PMi_SetLEDAsync)          4-9   backlight on/off per screen
 *   0x0E  power off (PM_ForceToPowerOffAsync)
 *   0x0F  read status, parameter selects: 0 battery (PM_GetBattery),
 *         3 backlights (PM_GetBackLight: bit 3 top, bit 2 bottom),
 *         6 sound amplifier (PM_GetAmp), 8 LED pattern (PM_GetLEDPattern)
 *   0x10  set the sound amplifier (func_0208BF08, PMi_SetAmp)
 *   0x12  set the LED pattern (func_0208C6A4)
 *
 * The host has no power hardware, so the state is a model: the answers are
 * the ones a DS with a charged battery and both backlights on gives, and a
 * setting reads back as it was set.  Sleep returns at once: 0x62 is
 * acknowledged and followed by 0x63, which is what waking looks like to the
 * ARM9.
 */

#include <stdio.h>
#include <string.h>

#include "arm7.h"
#include "pxi.h"

#define PM_FIRST   0x02000000u
#define PM_LAST    0x01000000u
#define PM_MAXWORDS 4

static BwArm7PmState g_pm;
static u32 g_packet[PM_MAXWORDS];
static int g_words;
static u32 g_unknown_logged;

static void reply(u32 command, u32 result)
{
    (void)bw_pxi_arm7_send(BW_PXI_TAG_PM, (command << 8) | (result & 0xFFu), 0);
}

static u32 utility(u32 number, u32 param)
{
    switch (number) {
    case 1: case 2: case 3:
        g_pm.led = (u16)number;
        return 0;
    case 4: g_pm.backlight |= 4; return 0;          /* bottom on           */
    case 5: g_pm.backlight = (u16)(g_pm.backlight & ~4u); return 0; /* bottom off */
    case 6: g_pm.backlight |= 8; return 0;          /* top on              */
    case 7: g_pm.backlight = (u16)(g_pm.backlight & ~8u); return 0; /* top off */
    case 8: g_pm.backlight |= 12; return 0;         /* both on             */
    case 9: g_pm.backlight = (u16)(g_pm.backlight & ~12u); return 0; /* both off */
    case 0x0E:
        g_pm.powered_off = 1;
        return 0;
    case 0x0F:
        switch (param) {
        case 0: return g_pm.battery_low;
        case 3: return g_pm.backlight;
        case 6: return g_pm.amp ? 1u : 0u;
        case 8: return g_pm.led_pattern;
        default: return 0;
        }
    case 0x10:
        g_pm.amp = (u16)param;
        return 0;
    case 0x12:
        g_pm.led_pattern = (u16)param;
        return 0;
    default:
        if (number < 32 && !(g_unknown_logged & (1u << number))) {
            g_unknown_logged |= 1u << number;
            fprintf(stderr, "eidos: arm7 pm: utility command 0x%02lx "
                            "(parameter 0x%04lx) not modelled; answered 0\n",
                    (unsigned long)number, (unsigned long)param);
        }
        return 0;
    }
}

static void run_packet(void)
{
    u32 command = (g_packet[0] >> 8) & 0x7Fu;
    u32 param = g_words > 1 ? (g_packet[1] & 0xFFFFu) : 0;

    switch (command) {
    case 0x60:
        reply(0x60, 0);
        break;
    case 0x61:
        reply(0x61, utility(g_packet[0] & 0xFFu, param));
        break;
    case 0x62:
        reply(0x62, 0);
        reply(0x63, 0);
        break;
    default:
        fprintf(stderr, "eidos: arm7 pm: command 0x%02lx not modelled\n",
                (unsigned long)command);
        reply(command, 0);
        break;
    }
}

static void pm_callback(int tag, u32 data, int err)
{
    (void)tag;
    if (err)
        return;
    if (data & PM_FIRST)
        g_words = 0;
    if (g_words < PM_MAXWORDS)
        g_packet[g_words++] = data;
    if (data & PM_LAST) {
        run_packet();
        g_words = 0;
    }
}

void bw_arm7_pm_init(void)
{
    memset(&g_pm, 0, sizeof g_pm);
    g_pm.backlight = 12;
    g_words = 0;
    g_unknown_logged = 0;
    bw_pxi_arm7_set_callback(BW_PXI_TAG_PM, pm_callback);
}

const BwArm7PmState *bw_arm7_pm_state(void)
{
    return &g_pm;
}
