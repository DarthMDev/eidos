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
#define R 0x100u
#define L 0x200u

static char g_label[64];
static char g_value[64];

static void row(int i)
{
    eidos_sui_row_text(i, g_label, sizeof g_label, g_value, sizeof g_value);
}

int main(void)
{
    int r;
    remove(INI);
    eidos_settings_set_path(INI);
    eidos_settings_load();
    eidos_sui_enter();

    BW_CHECK_EQ("starts on Display", eidos_sui_tab(), 0);
    row(0);
    BW_CHECK_EQ("header", strcmp(g_label, "Display"), 0);
    BW_CHECK_EQ("header page", strcmp(g_value, "1/4"), 0);
    BW_CHECK_EQ("display rows", eidos_sui_row_count(), 1 + 4);
    row(1);
    BW_CHECK_EQ("mode label", strcmp(g_label, "Window mode"), 0);
    BW_CHECK_EQ("mode value", strcmp(g_value, "Windowed"), 0);
    BW_CHECK_EQ("cursor on first row", eidos_sui_cursor_row(), 1);

    /* Right changes the choice and asks for a redraw. */
    r = eidos_sui_input(RIGHT, RIGHT);
    BW_CHECK_EQ("right redraws", r & EIDOS_SUI_REDRAW, EIDOS_SUI_REDRAW);
    BW_CHECK_EQ("mode changed", eidos_settings_get(EIDOS_OPT_DISPLAY_MODE), 1);
    BW_CHECK_EQ("dirty", eidos_settings_dirty(), 1);
    row(1);
    BW_CHECK_EQ("mode value text", strcmp(g_value, "Fullscreen"), 0);
    eidos_sui_input(0, 0);
    eidos_sui_input(LEFT, LEFT);
    BW_CHECK_EQ("mode back", eidos_settings_get(EIDOS_OPT_DISPLAY_MODE), 0);

    /* Down moves, resolution steps, fit text. */
    eidos_sui_input(0, 0);
    eidos_sui_input(DOWN, DOWN);
    BW_CHECK_EQ("cursor moved", eidos_sui_cursor_row(), 2);
    row(2);
    BW_CHECK_EQ("resolution label", strcmp(g_label, "Resolution"), 0);
    BW_CHECK_EQ("resolution fit", strcmp(g_value, "Fit"), 0);
    eidos_sui_input(0, 0);
    eidos_sui_input(RIGHT, RIGHT);
    row(2);
    BW_CHECK_EQ("resolution 1x", strcmp(g_value, "1x"), 0);

    /* Revert row undoes it. */
    eidos_sui_input(0, 0);
    eidos_sui_input(DOWN, DOWN);
    row(eidos_sui_cursor_row());
    BW_CHECK_EQ("revert row", strcmp(g_label, "Revert changes"), 0);
    eidos_sui_input(0, 0);
    eidos_sui_input(A, A);
    BW_CHECK_EQ("reverted", eidos_settings_get(EIDOS_OPT_DISPLAY_RESOLUTION), 0);
    BW_CHECK_EQ("clean", eidos_settings_dirty(), 0);

    /* Tabs wrap with L and R. */
    eidos_sui_input(0, 0);
    eidos_sui_input(R, R);
    BW_CHECK_EQ("layout tab", eidos_sui_tab(), 1);
    eidos_sui_input(0, 0);
    eidos_sui_input(L, L);
    eidos_sui_input(0, 0);
    eidos_sui_input(L, L);
    BW_CHECK_EQ("L wraps to Sound", eidos_sui_tab(), 3);
    row(1);
    BW_CHECK_EQ("volume label", strcmp(g_label, "Master volume"), 0);
    eidos_sui_input(0, 0);
    eidos_sui_input(LEFT, LEFT);
    BW_CHECK_EQ("volume -5", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 95);

    /* Controls scrolls: 12 buttons + 2 actions, 6 visible. */
    eidos_sui_input(0, 0);
    eidos_sui_input(L, L);
    BW_CHECK_EQ("controls tab", eidos_sui_tab(), 2);
    BW_CHECK_EQ("controls rows", eidos_sui_row_count(), 1 + EIDOS_SUI_VISIBLE);
    eidos_sui_input(0, 0);
    eidos_sui_input(UP, UP);                  /* wraps to the last row */
    BW_CHECK_EQ("cursor at bottom", eidos_sui_cursor_row(), EIDOS_SUI_VISIBLE);
    row(eidos_sui_cursor_row());
    BW_CHECK_EQ("last row", strcmp(g_label, "Restore defaults"), 0);
    eidos_sui_input(0, 0);
    eidos_sui_input(A, A);
    BW_CHECK_EQ("defaults volume", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 100);

    /* A on a binding row starts a capture; B cancels it without a platform. */
    eidos_sui_input(0, 0);
    eidos_sui_input(DOWN, DOWN);              /* wraps to the first row */
    row(eidos_sui_cursor_row());
    BW_CHECK_EQ("first button", strcmp(g_label, eidos_settings_button(0)->label), 0);
    eidos_sui_input(0, 0);
    eidos_sui_input(A, A);
    BW_CHECK_EQ("capturing", eidos_sui_capturing(), 1);
    row(eidos_sui_cursor_row());
    BW_CHECK_EQ("press a key", strcmp(g_value, "Press a key"), 0);
    eidos_sui_input(B, B);
    BW_CHECK_EQ("cancelled", eidos_sui_capturing(), 0);

    /* B leaves; leaving commits. */
    eidos_sui_input(0, 0);
    eidos_sui_input(R, R);                    /* Sound */
    eidos_sui_input(0, 0);
    eidos_sui_input(LEFT, LEFT);
    r = eidos_sui_input(B, B);
    BW_CHECK_EQ("B exits", r & EIDOS_SUI_EXIT, EIDOS_SUI_EXIT);
    eidos_sui_leave();
    BW_CHECK_EQ("committed", eidos_settings_dirty(), 0);
    eidos_settings_load();
    BW_CHECK_EQ("saved volume", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 95);

    remove(INI);
    BW_REPORT("test_settings_ui");
}
