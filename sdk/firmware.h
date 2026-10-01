/*
 * sdk/firmware.h -- the console's user settings: the nickname, birthday,
 * favourite colour, language, alarm and touch-screen calibration a DS owner
 * enters in the system menu, and what the port does with them.
 *
 * Why it exists: on the DS these live in the firmware flash, and before the
 * ARM9 runs a line of the game the ARM7's boot code copies the current copy
 * into main RAM at 0x027FFC80 (0x02FFFC80 in the TWL map the matched code
 * uses; HW_NVRAM_USER_INFO), next to the console's MAC address
 * (0x02FFFCF4) and its allowed wireless channels (0x02FFFCFA).  The matched
 * ARM9 SDK reads them there and nowhere else: OS_GetOwnerInfo
 * (arm9_sdk/func_02087C88.c), OS_GetOwnerInfoEx (func_02087D28.c),
 * OS_GetOwnerRtcOffset, OS_GetMacAddress, TP_GetUserInfo
 * (func_0208A998.c), WM_GetAllowedChannel (func_0208EE60.c).  A host has no
 * firmware, so the port keeps the settings in a human-editable file and
 * writes that area at every power-on (bw_firmware_apply, from
 * bw_port_reset), as the ARM7's boot code does.
 *
 * The file is `<save dir>/firmware.ini` (sdk/save.h), key = value lines,
 * '#' comments; bw_firmware_format writes a fully commented one, and
 * docs/firmware.md documents every key.  A missing file means the defaults:
 * nickname "Player", English, birthday 1/1, colour 0, alarm off, the
 * calibration described below.  The same settings fill the user-settings
 * blocks of the firmware flash image sdk/arm7/nvram.c serves, so the flash
 * and the RAM copy agree.
 *
 * Touch calibration.  The defaults map screen pixel (x, y) to the raw ADC
 * reading (16 x, 16 y): points (32, 32) <- (512, 512) and (224, 160) <-
 * (3584, 2560).  TP_GetUserInfo turns that into an exact 16-to-1 scale, so
 * an input layer that reports a touch at pixel (x, y) as raw (16 x, 16 y)
 * reaches the game at (x, y).
 */

#ifndef BW_PORT_FIRMWARE_H
#define BW_PORT_FIRMWARE_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where the ARM7's boot code leaves them (TWL main-RAM spelling).          */
#define BW_FW_USER_INFO_ADDR    0x02FFFC80u   /* 0x70 bytes                 */
#define BW_FW_MAC_ADDR          0x02FFFCF4u   /* 6 bytes                    */
#define BW_FW_CHANNELS_ADDR     0x02FFFCFAu   /* u16, bit n = channel n     */

#define BW_FW_USER_INFO_SIZE    0x70u
#define BW_FW_NICKNAME_MAX      10            /* UTF-16 code units          */
#define BW_FW_MESSAGE_MAX       26

/* OS_LANGUAGE_*, the 3-bit field at user settings +0x64.                   */
enum {
    BW_FW_LANG_JAPANESE, BW_FW_LANG_ENGLISH, BW_FW_LANG_FRENCH,
    BW_FW_LANG_GERMAN, BW_FW_LANG_ITALIAN, BW_FW_LANG_SPANISH,
    BW_FW_LANG_CHINESE, BW_FW_LANG_KOREAN
};

typedef struct BwFirmwareSettings {
    u16 nickname[BW_FW_NICKNAME_MAX];
    u8  nickname_len;
    u16 message[BW_FW_MESSAGE_MAX];
    u8  message_len;
    u8  birthday_month;         /* 1-12                                     */
    u8  birthday_day;           /* 1-31                                     */
    u8  favorite_color;         /* 0-15                                     */
    u8  language;               /* BW_FW_LANG_*                             */
    u8  alarm_hour;             /* 0-23                                     */
    u8  alarm_minute;           /* 0-59                                     */
    u8  alarm_enabled;
    u8  backlight;              /* 0-3                                      */
    u16 tp_raw_x1, tp_raw_y1;   /* calibration point 1: raw ADC ...         */
    u8  tp_x1, tp_y1;           /* ... and the pixel it was touched at      */
    u16 tp_raw_x2, tp_raw_y2;   /* point 2                                  */
    u8  tp_x2, tp_y2;
    s64 rtc_offset;             /* OS_GetOwnerRtcOffset                     */
    u8  mac[6];                 /* OS_GetMacAddress                         */
    u16 channels;               /* WM_GetAllowedChannel                     */
} BwFirmwareSettings;

/* The built-in defaults (the header comment lists them).                   */
void bw_firmware_defaults(BwFirmwareSettings *s);

/*
 * Parse settings text over whatever `s` already holds (start from
 * bw_firmware_defaults).  Returns the number of lines it could not use;
 * the first problem is described in `err` (may be NULL).  Unknown keys and
 * out-of-range values are problems and leave the field as it was.
 */
int bw_firmware_parse(BwFirmwareSettings *s, const char *text,
                      char *err, size_t err_size);

/* The commented file for `s`.  Returns the length, or -1 if it does not fit. */
int bw_firmware_format(const BwFirmwareSettings *s, char *out, size_t out_size);

/*
 * The settings in force: what bw_firmware_set gave, else firmware.ini read
 * once (a problem in it is reported on stderr and that line skipped), else
 * the defaults.  Never writes a file.
 */
const BwFirmwareSettings *bw_firmware_settings(void);

/* Override the settings (a front end's settings screen, tests); NULL goes
 * back to the file.  Takes effect at the next bw_firmware_apply.          */
void bw_firmware_set(const BwFirmwareSettings *s);

/* Forget the file's cached contents so the next use reads it again.       */
void bw_firmware_reload(void);

/* Write the commented default file to `path` (NULL: <save dir>/firmware.ini)
 * unless something is already there.  0 written, 1 already there, -1 error. */
int bw_firmware_write_default(const char *path);

/*
 * Write the area the ARM7's boot code fills: the 0x70-byte user settings at
 * 0x02FFFC80, the MAC address and the allowed channels.  bw_port_reset calls
 * it after bw_ds_reset, which cleared main RAM.
 */
void bw_firmware_apply(void);

/*
 * The firmware flash's 0x100-byte user-settings block for `s`: the 0x70
 * bytes, the update counter at +0x70 and the CRC16 of +0x00..+0x6F at
 * +0x72 (GBATEK, "DS Firmware User Settings"), the rest zero.  Little-endian,
 * as in the flash.
 */
void bw_firmware_user_block(const BwFirmwareSettings *s, u16 counter,
                            u8 out[0x100]);

/* CRC-16 (polynomial 0xA001, reflected), the firmware's and the BIOS's.    */
u16 bw_firmware_crc16(u16 crc, const u8 *data, u32 size);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_FIRMWARE_H */
