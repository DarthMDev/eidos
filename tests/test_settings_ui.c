/*
 * tests/test_settings_ui.c -- the settings screen's model (tabs, rows,
 * navigation, value changes, revert, defaults, commit) with no platform.
 */

#include <stdio.h>
#include <string.h>

#include "port_test.h"
#include "settings.h"
#include "settings_ui.h"

#define INI "test_settings_ui_work.ini"

#define A 0x1u
#define B 0x2u
#define RIGHT 0x10u
#define LEFT 0x20u
#define UP 0x40u
#define DOWN 0x80u
#define L 0x200u

static char g_label[64];
static char g_value[64];

static void row(int i)
{
    eidos_sui_row_text(i, g_label, sizeof g_label, g_value, sizeof g_value);
}

static int press(unsigned b)
{
    eidos_sui_input(0, 0);
    return eidos_sui_input(b, b);
}

int main(void)
{
    int r, i;
    char help[96];
    remove(INI);
    eidos_settings_set_path(INI);
    eidos_settings_load();
    eidos_sui_enter();

    /* Level 0: the section list. */
    BW_CHECK_EQ("starts at level 0", eidos_sui_level(), 0);
    BW_CHECK_EQ("no section yet", eidos_sui_tab(), -1);
    row(0);
    BW_CHECK_EQ("header", strcmp(g_label, "Settings"), 0);
    BW_CHECK_EQ("header has no counter", g_value[0], 0);
    BW_CHECK_EQ("section rows", eidos_sui_row_count(), 1 + 4);
    row(1);
    BW_CHECK_EQ("first section", strcmp(g_label, "Display"), 0);
    row(4);
    BW_CHECK_EQ("last section", strcmp(g_label, "Sound"), 0);
    r = press(L);
    BW_CHECK_EQ("L does nothing", r, 0);
    r = press(RIGHT);
    BW_CHECK_EQ("right does nothing", r, 0);

    /* A enters Display; the header is the section name. */
    r = press(A);
    BW_CHECK_EQ("A redraws", r & EIDOS_SUI_REDRAW, EIDOS_SUI_REDRAW);
    BW_CHECK_EQ("level 1", eidos_sui_level(), 1);
    BW_CHECK_EQ("display section", eidos_sui_tab(), 0);
    row(0);
    BW_CHECK_EQ("display header", strcmp(g_label, "Display"), 0);
    BW_CHECK_EQ("display rows", eidos_sui_row_count(), 1 + 4);
    row(1);
    BW_CHECK_EQ("mode label", strcmp(g_label, "Window mode"), 0);
    BW_CHECK_EQ("mode value", strcmp(g_value, "Windowed"), 0);
    BW_CHECK_EQ("cursor on first row", eidos_sui_cursor_row(), 1);

    /* Right changes the choice and asks for a redraw. */
    eidos_sui_input(0, 0);
    r = eidos_sui_input(RIGHT, RIGHT);
    BW_CHECK_EQ("right redraws", r & EIDOS_SUI_REDRAW, EIDOS_SUI_REDRAW);
    BW_CHECK_EQ("mode changed", eidos_settings_get(EIDOS_OPT_DISPLAY_MODE), 1);
    BW_CHECK_EQ("dirty", eidos_settings_dirty(), 1);
    row(1);
    BW_CHECK_EQ("mode value text", strcmp(g_value, "Fullscreen"), 0);
    press(LEFT);
    BW_CHECK_EQ("mode back", eidos_settings_get(EIDOS_OPT_DISPLAY_MODE), 0);

    /* Down moves, resolution steps, fit text. */
    press(DOWN);
    BW_CHECK_EQ("cursor moved", eidos_sui_cursor_row(), 2);
    row(2);
    BW_CHECK_EQ("resolution label", strcmp(g_label, "Resolution"), 0);
    BW_CHECK_EQ("resolution 1x default", strcmp(g_value, "1x"), 0);
    press(RIGHT);
    row(2);
    BW_CHECK_EQ("resolution 2x", strcmp(g_value, "2x"), 0);

    /* Revert row undoes it. */
    press(DOWN);
    row(eidos_sui_cursor_row());
    BW_CHECK_EQ("revert row", strcmp(g_label, "Revert changes"), 0);
    press(A);
    BW_CHECK_EQ("reverted", eidos_settings_get(EIDOS_OPT_DISPLAY_RESOLUTION), 0);
    BW_CHECK_EQ("clean", eidos_settings_dirty(), 0);

    /* B goes back to level 0 with the cursor on Display. */
    r = press(B);
    BW_CHECK_EQ("B back redraws", r & EIDOS_SUI_REDRAW, EIDOS_SUI_REDRAW);
    BW_CHECK_EQ("B back does not exit", r & EIDOS_SUI_EXIT, 0);
    BW_CHECK_EQ("level 0 again", eidos_sui_level(), 0);
    BW_CHECK_EQ("cursor on Display", eidos_sui_cursor_row(), 1);

    /* Layout: three options. */
    press(DOWN);
    press(A);
    BW_CHECK_EQ("layout section", eidos_sui_tab(), 1);
    BW_CHECK_EQ("layout rows", eidos_sui_row_count(), 1 + 5);
    press(B);
    BW_CHECK_EQ("cursor on Layout", eidos_sui_cursor_row(), 2);

    /* Sound: volume steps of 5. */
    press(DOWN);
    press(DOWN);
    press(A);
    BW_CHECK_EQ("sound section", eidos_sui_tab(), 3);
    row(1);
    BW_CHECK_EQ("volume label", strcmp(g_label, "Master volume"), 0);
    press(LEFT);
    BW_CHECK_EQ("volume -5", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 95);
    press(B);

    /* Controls scrolls: 12 buttons + 2 actions, 6 visible. */
    press(UP);
    BW_CHECK_EQ("cursor on Controls", eidos_sui_cursor_row(), 3);
    press(A);
    BW_CHECK_EQ("controls section", eidos_sui_tab(), 2);
    BW_CHECK_EQ("controls rows", eidos_sui_row_count(), 1 + EIDOS_SUI_VISIBLE);
    press(UP);                                /* wraps to the last row */
    BW_CHECK_EQ("cursor at bottom", eidos_sui_cursor_row(), EIDOS_SUI_VISIBLE);
    row(eidos_sui_cursor_row());
    BW_CHECK_EQ("last row", strcmp(g_label, "Restore defaults"), 0);
    press(A);
    BW_CHECK_EQ("defaults volume", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 100);

    /* Every Controls value fits the value column (about 17 characters). */
    press(DOWN);                              /* wraps to the first row */
    for (i = 0; i < 12; i++) {
        row(eidos_sui_cursor_row());
        BW_CHECK_EQ("bind label short", (int)strlen(g_label) <= 6, 1);
        BW_CHECK_EQ("bind value short", (int)strlen(g_value) <= 20, 1);
        press(DOWN);
    }
    press(DOWN);
    press(DOWN);                              /* back to the first row */
    row(eidos_sui_cursor_row());
    BW_CHECK_EQ("first button", strcmp(g_label, "A"), 0);

    /* A on a binding row starts a capture; B cancels it without a platform. */
    press(A);
    BW_CHECK_EQ("capturing", eidos_sui_capturing(), 1);
    row(eidos_sui_cursor_row());
    BW_CHECK_EQ("press a key", strcmp(g_value, "Press a key"), 0);
    eidos_sui_help_line(0, help, sizeof help);
    BW_CHECK_EQ("capture help", help[0] != 0, 1);
    eidos_sui_input(B, B);
    BW_CHECK_EQ("cancelled", eidos_sui_capturing(), 0);
    BW_CHECK_EQ("still in the section", eidos_sui_level(), 1);

    /* Help text exists for every visible row. */
    BW_CHECK_EQ("help lines", eidos_sui_help_count() > 0, 1);
    eidos_sui_help_line(0, help, sizeof help);
    BW_CHECK_EQ("bind help", help[0] != 0, 1);

    /* B twice leaves; leaving commits. */
    press(B);
    press(DOWN);                              /* Sound */
    press(A);
    press(LEFT);
    press(B);
    r = press(B);
    BW_CHECK_EQ("B exits", r & EIDOS_SUI_EXIT, EIDOS_SUI_EXIT);
    eidos_sui_leave();
    BW_CHECK_EQ("committed", eidos_settings_dirty(), 0);
    eidos_settings_load();
    BW_CHECK_EQ("saved volume", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 95);

    remove(INI);
    BW_REPORT("test_settings_ui");
}
