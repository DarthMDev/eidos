/*
 * tests/test_firmware.c -- the console's user settings (sdk/firmware.h) and
 * the save folder (sdk/save.h).
 *
 * The settings file is parsed and written back; the settings reach the
 * area the ARM7's boot code fills at power-on (bw_port_reset), and the
 * game's own readers of that area, aletheia's matched OS_GetOwnerInfo,
 * OS_GetOwnerInfoEx and TP_GetUserInfo compiled as written, return them.
 * TP_GetUserInfo's arithmetic runs on the hardware divider, which the port
 * does not model yet, so TP_CalcCalibrateParam is replaced here by a
 * recorder: the check is that the matched reader hands it the calibration
 * points the settings hold.  Files go to test_firmware_saves/ in the
 * working directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsmem.h"
#include "firmware.h"
#include "port.h"
#include "port_test.h"
#include "save.h"

#define SAVE_DIR "test_firmware_saves"

typedef struct { u8 month, day; } OSBirthday;
typedef struct {
    u8 language;
    u8 favoriteColor;
    OSBirthday birthday;
    u16 nickName[11];
    u16 nickNameLength;
    u16 comment[27];
    u16 commentLength;
} OSOwnerInfo;
typedef struct {
    u8 language;
    u8 favoriteColor;
    OSBirthday birthday;
    u16 nickName[11];
    u16 nickNameLength;
    u16 comment[27];
    u16 commentLength;
    u8 country;
    u8 padding;
} OSOwnerInfoEx;
typedef struct { s16 x0, y0, xDotSize, yDotSize; } TPCalibrateParam;

extern void OS_GetOwnerInfo(OSOwnerInfo *info);      /* OS_GetOwnerInfo       */
extern void OS_GetOwnerInfoEx(OSOwnerInfoEx *info);    /* OS_GetOwnerInfoEx     */
extern BOOL TP_GetUserInfo(TPCalibrateParam *c);    /* TP_GetUserInfo        */
extern void OS_GetMacAddress(u8 *mac);             /* sdk/asm               */

/* The doubles the matched readers call.                                    */
int OS_IsRunOnTwl(void);
int OS_IsRunOnTwl(void)
{
    return 0;                       /* the port boots as a NITRO console    */
}

static u16 g_tp[8];
int TP_CalcCalibrateParam(TPCalibrateParam *c, u16 x1, u16 y1, u16 dx1,
                          u16 dy1, u16 x2, u16 y2, u16 dx2, u16 dy2);
int TP_CalcCalibrateParam(TPCalibrateParam *c, u16 x1, u16 y1, u16 dx1,
                          u16 dy1, u16 x2, u16 y2, u16 dx2, u16 dy2)
{
    g_tp[0] = x1; g_tp[1] = y1; g_tp[2] = dx1; g_tp[3] = dy1;
    g_tp[4] = x2; g_tp[5] = y2; g_tp[6] = dx2; g_tp[7] = dy2;
    c->x0 = 1;
    return 0;
}

static void test_crc(void)
{
    static const u8 v[] = "123456789";

    /* CRC-16/MODBUS check value: polynomial 0xA001 reflected, init 0xFFFF. */
    BW_CHECK_EQ("crc16 check value", bw_firmware_crc16(0xFFFF, v, 9), 0x4B37);
}

static void test_parse(void)
{
    BwFirmwareSettings a, b;
    char text[4096], again[4096], err[160];
    int bad;

    bw_firmware_defaults(&a);
    BW_CHECK_EQ("defaults: English", a.language, BW_FW_LANG_ENGLISH);
    BW_CHECK_EQ("defaults: nickname length", a.nickname_len, 6);
    BW_CHECK_EQ("defaults: nickname", a.nickname[0] == 'P' && a.nickname[5] == 'r', 1);
    BW_CHECK_EQ("defaults: birthday", a.birthday_month * 100 + a.birthday_day, 101);

    bad = bw_firmware_parse(&a,
        "# a comment\n"
        "nickname = N\xC3\xA9o\n"           /* "Neo" with an e-acute        */
        "message=Hello there\r\n"
        "  birthday = 12/25\n"
        "favorite_color = dark_blue\n"
        "language = French\n"
        "alarm = 07:30 on\n"
        "backlight = 2\n"
        "touch_calibration = 100 200 10 20 3000 2000 200 150\n"
        "rtc_offset = -3600\n"
        "mac = 02:11:22:33:44:55\n"
        "wireless_channels = 0x2082\n", err, sizeof err);
    BW_CHECK_EQ("parse: every line used", bad, 0);
    BW_CHECK_EQ("parse: nickname", a.nickname_len == 3 && a.nickname[1] == 0xE9, 1);
    BW_CHECK_EQ("parse: message length", a.message_len, 11);
    BW_CHECK_EQ("parse: birthday", a.birthday_month * 100 + a.birthday_day, 1225);
    BW_CHECK_EQ("parse: colour by name", a.favorite_color, 12);
    BW_CHECK_EQ("parse: language by name", a.language, BW_FW_LANG_FRENCH);
    BW_CHECK_EQ("parse: alarm", a.alarm_hour * 100 + a.alarm_minute, 730);
    BW_CHECK_EQ("parse: alarm on", a.alarm_enabled, 1);
    BW_CHECK_EQ("parse: calibration", a.tp_raw_x2 == 3000 && a.tp_y2 == 150, 1);
    BW_CHECK_EQ("parse: rtc offset", (long)a.rtc_offset, -3600);
    BW_CHECK_EQ("parse: mac", a.mac[1] == 0x11 && a.mac[5] == 0x55, 1);
    BW_CHECK_EQ("parse: channels", a.channels, 0x2082);

    /* What format writes, parse reads back to the same settings.           */
    BW_CHECK_EQ("format", bw_firmware_format(&a, text, sizeof text) > 0, 1);
    bw_firmware_defaults(&b);
    BW_CHECK_EQ("round trip: parses", bw_firmware_parse(&b, text, NULL, 0), 0);
    BW_CHECK_EQ("round trip: formats", bw_firmware_format(&b, again, sizeof again) > 0, 1);
    BW_CHECK_EQ("round trip: same text", strcmp(text, again), 0);

    /* Problems are counted and leave the field alone.                      */
    bw_firmware_defaults(&b);
    bad = bw_firmware_parse(&b,
        "language = klingon\n"
        "nickname = ABCDEFGHIJK\n"        /* 11 characters                 */
        "birthday = 13/1\n"
        "colour = red\n"
        "no equals sign\n"
        "favorite_color = 16\n"
        "language = 7\n", err, sizeof err);
    BW_CHECK_EQ("errors: counted", bad, 6);
    BW_CHECK_EQ("errors: first named", strstr(err, "line 1") != NULL, 1);
    BW_CHECK_EQ("errors: nickname kept", b.nickname_len == 6 && b.nickname[0] == 'P', 1);
    BW_CHECK_EQ("errors: birthday kept", b.birthday_month, 1);
    BW_CHECK_EQ("errors: later good line used", b.language, BW_FW_LANG_KOREAN);
}

static void test_apply(void)
{
    BwFirmwareSettings s;
    OSOwnerInfo info;
    OSOwnerInfoEx ex;
    TPCalibrateParam cal;
    u8 mac[6], block[0x100];
    const u8 *area;
    s64 rtc;
    u16 ch;

    bw_firmware_defaults(&s);
    (void)bw_firmware_parse(&s, "nickname = Hilbert\nmessage = Hi\n"
                                "birthday = 7/14\nfavorite_color = 5\n"
                                "language = german\nrtc_offset = 0x123456789\n"
                                "mac = 02:A0:B0:C0:D0:E0\n", NULL, 0);
    bw_firmware_set(&s);
    bw_port_reset();

    memset(&info, 0xCC, sizeof info);
    OS_GetOwnerInfo(&info);
    BW_CHECK_EQ("owner: language", info.language, BW_FW_LANG_GERMAN);
    BW_CHECK_EQ("owner: colour", info.favoriteColor, 5);
    BW_CHECK_EQ("owner: birthday", info.birthday.month * 100 + info.birthday.day, 714);
    BW_CHECK_EQ("owner: nickname length", info.nickNameLength, 7);
    BW_CHECK_EQ("owner: nickname", info.nickName[0] == 'H' && info.nickName[6] == 't'
                && info.nickName[7] == 0 && info.nickName[10] == 0, 1);
    BW_CHECK_EQ("owner: comment", info.commentLength == 2 && info.comment[1] == 'i', 1);

    memset(&ex, 0xCC, sizeof ex);
    OS_GetOwnerInfoEx(&ex);
    BW_CHECK_EQ("owner ex: language (NITRO path)", ex.language, BW_FW_LANG_GERMAN);
    BW_CHECK_EQ("owner ex: country", ex.country, 0);
    BW_CHECK_EQ("owner ex: nickname", ex.nickName[1], 'i');

    memset(g_tp, 0, sizeof g_tp);
    BW_CHECK_EQ("tp: TP_GetUserInfo", TP_GetUserInfo(&cal), 1);
    BW_CHECK_EQ("tp: point 1", g_tp[0] == 512 && g_tp[1] == 512 && g_tp[2] == 32
                && g_tp[3] == 32, 1);
    BW_CHECK_EQ("tp: point 2", g_tp[4] == 3584 && g_tp[5] == 2560 && g_tp[6] == 224
                && g_tp[7] == 160, 1);

    OS_GetMacAddress(mac);
    BW_CHECK_EQ("mac: OS_GetMacAddress", mac[0] == 0x02 && mac[1] == 0xA0
                && mac[5] == 0xE0, 1);
    memcpy(&ch, bw_ds_ptr(BW_FW_CHANNELS_ADDR), 2);
    BW_CHECK_EQ("channels", ch, 0x3FFE);
    memcpy(&rtc, bw_ds_ptr(BW_FW_USER_INFO_ADDR + 0x68), 8);
    BW_CHECK_EQ("rtc offset low", (u32)rtc, 0x23456789u);
    BW_CHECK_EQ("rtc offset high", (u32)((u64)rtc >> 32), 1);

    /* The flash block carries the same 0x70 bytes, a counter and a CRC.     */
    bw_firmware_user_block(&s, 3, block);
    area = (const u8 *)bw_ds_ptr(BW_FW_USER_INFO_ADDR);
    BW_CHECK_EQ("block: same bytes as RAM", memcmp(block, area, 0x70), 0);
    BW_CHECK_EQ("block: counter", block[0x70] | (block[0x71] << 8), 3);
    BW_CHECK_EQ("block: crc", block[0x72] | (block[0x73] << 8),
                bw_firmware_crc16(0xFFFF, block, 0x70));
    BW_CHECK_EQ("block: version", block[0], 5);
    bw_firmware_set(NULL);
}

static void test_files(void)
{
    char path[1100];
    static char text[4096];
    long n;
    const BwFirmwareSettings *s;

    bw_set_save_dir(SAVE_DIR);
    BW_CHECK_EQ("save dir set", strcmp(bw_save_dir(), SAVE_DIR), 0);
    BW_CHECK_EQ("save path", bw_save_path(path, sizeof path, "firmware.ini"), 0);
    remove(path);

    BW_CHECK_EQ("default file written", bw_firmware_write_default(NULL), 0);
    BW_CHECK_EQ("default file kept", bw_firmware_write_default(NULL), 1);
    n = bw_save_read(path, (u8 *)text, sizeof text - 1);
    BW_CHECK_EQ("default file read", n > 100, 1);
    text[n > 0 ? n : 0] = '\0';
    BW_CHECK_EQ("default file documents itself",
                strstr(text, "language = english") != NULL
                && strstr(text, "nickname = Player") != NULL, 1);

    /* The player edits it; the next power-on reads it.                     */
    {
        static const char edited[] = "language = spanish\nnickname = Touko\n";

        BW_CHECK_EQ("edit", bw_save_write_atomic(path, edited,
                                                 (u32)strlen(edited)), 0);
    }
    bw_firmware_reload();
    s = bw_firmware_settings();
    BW_CHECK_EQ("edited language", s->language, BW_FW_LANG_SPANISH);
    BW_CHECK_EQ("edited nickname", s->nickname_len, 5);
    BW_CHECK_EQ("unedited keys keep defaults", s->birthday_month, 1);
    bw_port_reset();
    BW_CHECK_EQ("edited language reaches RAM",
                *(const u8 *)bw_ds_ptr(BW_FW_USER_INFO_ADDR + 0x64) & 7,
                BW_FW_LANG_SPANISH);

    /* Atomic replacement over an existing file, in a folder made for it.   */
    BW_CHECK_EQ("nested folder", bw_save_write_atomic(SAVE_DIR "/a/b/c.bin",
                                                      "xyz", 3), 0);
    BW_CHECK_EQ("replace", bw_save_write_atomic(SAVE_DIR "/a/b/c.bin", "ab", 2), 0);
    BW_CHECK_EQ("replaced length", bw_save_read(SAVE_DIR "/a/b/c.bin",
                                                (u8 *)text, 16), 2);
    BW_CHECK_EQ("missing file", bw_save_read(SAVE_DIR "/none", (u8 *)text, 4), -1);
    remove(SAVE_DIR "/a/b/c.bin");
    remove(path);
    bw_firmware_reload();
}

int main(void)
{
    test_crc();
    test_parse();
    bw_set_save_dir(SAVE_DIR);      /* never the player's own folder        */
    test_apply();
    test_files();
    BW_REPORT("test_firmware");
}
