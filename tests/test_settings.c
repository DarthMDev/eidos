/*
 * tests/test_settings.c -- the settings backend, under SDL's dummy drivers:
 * screen geometry and the touch mapping for every layout x scale (and the
 * fullscreen fits), binding conflicts and capture, the settings.ini round
 * trip, command-line overrides and the volume gain.
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "install.h"
#include "platform.h"
#include "port_test.h"
#include "settings.h"

#define INI "test_settings_work.ini"

static void test_geometry(void)
{
    int layout, swap, gap, scale;
    EidosScreenPlacement pl;
    EidosViewport vp;
    int x, y;

    for (layout = 0; layout < 2; layout++) {
        for (swap = 0; swap < 2; swap++) {
            for (gap = 0; gap <= 8; gap += 8) {
                for (scale = 1; scale <= 4; scale++) {
                    int top = swap ? 1 : 0, second = 1 - top;
                    int ok = 1;

                    eidos_layout_place(layout, swap, gap, &pl);
                    eidos_display_viewport(&pl, pl.width * scale,
                                           pl.height * scale, 0, &vp);
                    ok &= vp.scale == scale && vp.content.x == 0 &&
                          vp.content.y == 0;
                    ok &= vp.screen[top].x == 0 && vp.screen[top].y == 0;
                    ok &= layout == EIDOS_LAYOUT_VERTICAL
                          ? vp.screen[second].x == 0 &&
                            vp.screen[second].y == (192 + gap) * scale
                          : vp.screen[second].x == (256 + gap) * scale &&
                            vp.screen[second].y == 0;
                    ok &= vp.screen[0].w == 256 * scale &&
                          vp.screen[1].h == 192 * scale;
                    BW_CHECK_EQ("rects", ok, 1);

                    /* The bottom screen's corners and centre, and a point
                     * just outside on the far side and on the top screen. */
                    ok = eidos_viewport_to_touch(&pl, &vp, vp.screen[1].x,
                                                 vp.screen[1].y, &x, &y) &&
                         x == 0 && y == 0;
                    ok &= eidos_viewport_to_touch(
                              &pl, &vp,
                              vp.screen[1].x + vp.screen[1].w - 1,
                              vp.screen[1].y + vp.screen[1].h - 1, &x, &y) &&
                          x == 255 && y == 191;
                    ok &= eidos_viewport_to_touch(
                              &pl, &vp,
                              vp.screen[1].x + 100 * scale + scale / 2,
                              vp.screen[1].y + 50 * scale, &x, &y) &&
                          x == 100 && y == 50;
                    ok &= !eidos_viewport_to_touch(
                              &pl, &vp, vp.screen[0].x + 10 * scale,
                              vp.screen[0].y + 10 * scale, &x, &y);
                    ok &= !eidos_viewport_to_touch(
                              &pl, &vp, vp.screen[1].x - 1,
                              vp.screen[1].y, &x, &y);
                    BW_CHECK_EQ("touch", ok, 1);
                }
            }
        }
    }

    /* Fullscreen 1920x1080, vertical: integer 2x (512x768) centred. */
    eidos_layout_place(EIDOS_LAYOUT_VERTICAL, 0, 0, &pl);
    eidos_display_viewport(&pl, 1920, 1080, 0, &vp);
    BW_CHECK_EQ("fs int scale", vp.scale, 2);
    BW_CHECK_EQ("fs int x", vp.content.x, 704);
    BW_CHECK_EQ("fs int y", vp.content.y, 156);
    BW_CHECK_EQ("fs int bottom y", vp.screen[1].y, 156 + 384);
    BW_CHECK_EQ("touch letterbox",
                eidos_viewport_to_touch(&pl, &vp, 703, 600, &x, &y), 0);
    BW_CHECK_EQ("touch letterbox clamp", x, 0);
    BW_CHECK_EQ("touch fs",
                eidos_viewport_to_touch(&pl, &vp, 704 + 511, 540 + 383, &x, &y)
                && x == 255 && y == 191, 1);
    /* Fit: height-limited, 720x1080 centred. */
    eidos_display_viewport(&pl, 1920, 1080, 1, &vp);
    BW_CHECK_EQ("fs fit w", vp.content.w, 720);
    BW_CHECK_EQ("fs fit h", vp.content.h, 1080);
    BW_CHECK_EQ("fs fit x", vp.content.x, 600);
    BW_CHECK_EQ("fs fit bottom y", vp.screen[1].y, 540);
    BW_CHECK_EQ("fs fit touch",
                eidos_viewport_to_touch(&pl, &vp, 600, 540, &x, &y) &&
                x == 0 && y == 0, 1);
    BW_CHECK_EQ("fs fit touch end",
                eidos_viewport_to_touch(&pl, &vp, 600 + 719, 1079, &x, &y) &&
                x == 255 && y == 191, 1);
    /* Horizontal, swapped, 1920x1080 integer: 3x, bottom on the left. */
    eidos_layout_place(EIDOS_LAYOUT_HORIZONTAL, 1, 0, &pl);
    eidos_display_viewport(&pl, 1920, 1080, 0, &vp);
    BW_CHECK_EQ("h scale", vp.scale, 3);
    BW_CHECK_EQ("h swapped bottom x", vp.screen[1].x, 192);
    BW_CHECK_EQ("h swapped top x", vp.screen[0].x, 192 + 768);
    BW_CHECK_EQ("h touch",
                eidos_viewport_to_touch(&pl, &vp, 192, 252, &x, &y) &&
                x == 0 && y == 0, 1);
    /* Smaller than 1x falls back to a fit. */
    eidos_display_viewport(&pl, 256, 96, 0, &vp);
    BW_CHECK_EQ("under 1x", vp.scale, 0);
    BW_CHECK_EQ("under 1x w", vp.content.w, 256);
}

static void test_platform_modes(EidosPlatform *p)
{
    int layout, swap, scale, x, y;
    EidosViewport vp;
    EidosScreenPlacement pl;

    for (layout = 0; layout < 2; layout++) {
        for (swap = 0; swap < 2; swap++) {
            for (scale = 1; scale <= 3; scale++) {
                int ww, wh, ok;

                BW_CHECK_EQ("set_layout",
                            eidos_platform_set_layout(p, layout, swap, 4), 0);
                BW_CHECK_EQ("set_display",
                            eidos_platform_set_display(
                                p, EIDOS_DISPLAY_WINDOWED, scale,
                                EIDOS_SCALING_INTEGER), 0);
                eidos_layout_place(layout, swap, 4, &pl);
                ww = pl.width * scale;
                wh = pl.height * scale;
                eidos_platform_viewport(p, &vp);
                ok = vp.screen[1].w == 256 * scale && vp.content.w == ww &&
                     vp.content.h == wh;
                BW_CHECK_EQ("platform viewport", ok, 1);
                /* The platform mapping agrees with the pure one. */
                ok = eidos_platform_window_to_touch(
                         p, ww, wh, vp.screen[1].x + 7 * scale,
                         vp.screen[1].y + 9 * scale, &x, &y) &&
                     x == 7 && y == 9;
                BW_CHECK_EQ("platform touch", ok, 1);
                ok = !eidos_platform_window_to_touch(
                         p, ww, wh, vp.screen[0].x + scale,
                         vp.screen[0].y + scale, &x, &y);
                BW_CHECK_EQ("platform touch top", ok, 1);
            }
        }
    }
    {
        u32 w = 0, h = 0;

        eidos_platform_set_layout(p, EIDOS_LAYOUT_HORIZONTAL, 0, 0);
        eidos_platform_shot_size(p, &w, &h);
        BW_CHECK_EQ("shot size follows layout", w == 512 && h == 192, 1);
    }
    /* Borderless and fullscreen: whether the dummy driver grants them or
     * not, the platform's mode and the answer agree, and the picture is
     * still placed on the output. */
    {
        int rc = eidos_platform_set_display(p, EIDOS_DISPLAY_BORDERLESS, 2,
                                            EIDOS_SCALING_INTEGER);
        BW_CHECK_EQ("borderless agrees",
                    rc == 0 ? eidos_platform_display_mode(p) ==
                                  EIDOS_DISPLAY_BORDERLESS
                            : eidos_platform_display_mode(p) ==
                                  EIDOS_DISPLAY_WINDOWED, 1);
        rc = eidos_platform_set_display(p, EIDOS_DISPLAY_FULLSCREEN, 2,
                                        EIDOS_SCALING_FIT);
        BW_CHECK_EQ("fullscreen agrees",
                    rc == 0 ? eidos_platform_display_mode(p) ==
                                  EIDOS_DISPLAY_FULLSCREEN
                            : eidos_platform_display_mode(p) !=
                                  EIDOS_DISPLAY_FULLSCREEN, 1);
        eidos_platform_viewport(p, &vp);
        BW_CHECK_EQ("fs viewport placed", vp.content.w > 0, 1);
        eidos_platform_set_display(p, EIDOS_DISPLAY_WINDOWED, 2,
                                   EIDOS_SCALING_INTEGER);
        BW_CHECK_EQ("back to windowed",
                    eidos_platform_display_mode(p), EIDOS_DISPLAY_WINDOWED);
    }
}

static void push_key(int sc, int down)
{
    SDL_Event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.keysym.scancode = (SDL_Scancode)sc;
    SDL_PushEvent(&ev);
}

static void test_bindings(EidosPlatform *p)
{
    char buf[64];
    int kind = 0, index = 0, b;
    EidosCaptured cap;
    SDL_Event ev;

    eidos_settings_reset_bindings();
    /* Defaults: every button has a key and a pad control. */
    for (b = 0; b < eidos_settings_button_count(); b++) {
        BW_CHECK_EQ("default key", eidos_settings_get_key(b, 0) != 0, 1);
        eidos_settings_get_pad(b, 0, &kind, &index);
        BW_CHECK_EQ("default pad", kind >= 0, 1);
    }
    /* Select has two keys by default. */
    BW_CHECK_EQ("select second key", eidos_settings_get_key(7, 1) != 0, 1);

    /* Conflict: binding Z (B) to A moves it. */
    BW_CHECK_EQ("B has Z", eidos_settings_get_key(1, 0), SDL_SCANCODE_Z);
    BW_CHECK_EQ("bind Z to A", eidos_settings_set_key(0, 1, SDL_SCANCODE_Z), 0);
    BW_CHECK_EQ("A slot 1 is Z", eidos_settings_get_key(0, 1), SDL_SCANCODE_Z);
    BW_CHECK_EQ("B lost Z", eidos_settings_get_key(1, 0), 0);
    /* Slot -1 takes the first empty slot (B's slot 0 is empty now). */
    BW_CHECK_EQ("bind K to B", eidos_settings_set_key(1, -1, SDL_SCANCODE_K), 0);
    BW_CHECK_EQ("K in B slot 0", eidos_settings_get_key(1, 0), SDL_SCANCODE_K);
    BW_CHECK_EQ("clear", eidos_settings_set_key(1, 0, 0), 0);
    BW_CHECK_EQ("cleared", eidos_settings_get_key(1, 0), 0);
    BW_CHECK_EQ("escape reserved",
                eidos_settings_set_key(1, 0, SDL_SCANCODE_ESCAPE), -2);
    BW_CHECK_EQ("f12 reserved",
                eidos_settings_set_key(1, 0, SDL_SCANCODE_F12), -2);
    BW_CHECK_EQ("bad button", eidos_settings_set_key(99, 0, SDL_SCANCODE_K), -1);

    /* Pad conflict: south button moves from B to A. */
    BW_CHECK_EQ("pad bind",
                eidos_settings_set_pad(0, -1, EIDOS_PADBIND_BUTTON,
                                       SDL_CONTROLLER_BUTTON_A), 0);
    eidos_settings_get_pad(1, 0, &kind, &index);
    BW_CHECK_EQ("B lost pad A", kind == -1 ||
                !(kind == EIDOS_PADBIND_BUTTON &&
                  index == SDL_CONTROLLER_BUTTON_A), 1);

    /* Names are ASCII and non-empty. */
    BW_CHECK_EQ("name Z", strcmp(eidos_scancode_name(SDL_SCANCODE_Z, buf,
                                                     sizeof buf), "Z"), 0);
    BW_CHECK_EQ("name pad", strlen(eidos_pad_control_name(
        EIDOS_PADBIND_BUTTON, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, buf,
        sizeof buf)) > 0, 1);
    BW_CHECK_EQ("name axis", strcmp(eidos_pad_control_name(
        EIDOS_PADBIND_AXIS_NEG, SDL_CONTROLLER_AXIS_LEFTY, buf, sizeof buf),
        "Left Stick Up"), 0);

    /* Capture a key, then cancel with Escape (which must not quit). */
    eidos_settings_capture_begin();
    BW_CHECK_EQ("pending", eidos_settings_capture_poll(&cap),
                EIDOS_CAPTURE_PENDING);
    push_key(SDL_SCANCODE_J, 1);
    BW_CHECK_EQ("pump alive", eidos_platform_pump(p), 1);
    BW_CHECK_EQ("captured key", eidos_settings_capture_poll(&cap),
                EIDOS_CAPTURE_KEY);
    BW_CHECK_EQ("captured J", cap.scancode, SDL_SCANCODE_J);
    BW_CHECK_EQ("consumed", eidos_settings_capture_poll(&cap),
                EIDOS_CAPTURE_NONE);
    eidos_settings_capture_begin();
    push_key(SDL_SCANCODE_ESCAPE, 1);
    BW_CHECK_EQ("escape cancels, no quit", eidos_platform_pump(p), 1);
    BW_CHECK_EQ("cancelled", eidos_settings_capture_poll(&cap),
                EIDOS_CAPTURE_CANCELLED);
    /* A pad button and an axis. */
    eidos_settings_capture_begin();
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_CONTROLLERBUTTONDOWN;
    ev.cbutton.button = SDL_CONTROLLER_BUTTON_X;
    SDL_PushEvent(&ev);
    eidos_platform_pump(p);
    BW_CHECK_EQ("captured pad", eidos_settings_capture_poll(&cap),
                EIDOS_CAPTURE_PAD);
    BW_CHECK_EQ("pad index", cap.pad_index, SDL_CONTROLLER_BUTTON_X);
    eidos_settings_capture_begin();
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_CONTROLLERAXISMOTION;
    ev.caxis.axis = SDL_CONTROLLER_AXIS_RIGHTX;
    ev.caxis.value = -30000;
    SDL_PushEvent(&ev);
    eidos_platform_pump(p);
    BW_CHECK_EQ("captured axis", eidos_settings_capture_poll(&cap),
                EIDOS_CAPTURE_PAD);
    BW_CHECK_EQ("axis neg", cap.pad_kind, EIDOS_PADBIND_AXIS_NEG);
    /* Not armed: Escape quits again. */
    push_key(SDL_SCANCODE_ESCAPE, 1);
    BW_CHECK_EQ("escape quits when not capturing", eidos_platform_pump(p), 0);
}

static void snapshot(int *o, int *k, int *pk, int *pi)
{
    int i, b, s, n = 0;

    for (i = 0; i < EIDOS_OPT_COUNT; i++) {
        o[i] = eidos_settings_get(i);
    }
    for (b = 0; b < eidos_settings_button_count(); b++) {
        for (s = 0; s < EIDOS_BIND_SLOTS; s++) {
            k[n] = eidos_settings_get_key(b, s);
            eidos_settings_get_pad(b, s, &pk[n], &pi[n]);
            n++;
        }
    }
}

static void test_file(void)
{
    enum { N = 12 * EIDOS_BIND_SLOTS };
    int o1[EIDOS_OPT_COUNT], o2[EIDOS_OPT_COUNT];
    int k1[N], k2[N], pk1[N], pk2[N], pi1[N], pi2[N];
    FILE *fp;
    char *argv_a[] = { "x", "--fullscreen", "--scale", "3", "--volume", "40",
                       "--layout", "horizontal", "--swap-screens" };
    char *argv_bad[] = { "x", "--volume", "loud" };
    int i;

    eidos_settings_set_path(INI);
    remove(INI);
    BW_CHECK_EQ("no file -> defaults", eidos_settings_load(), 1);
    BW_CHECK_EQ("default scale", eidos_settings_get(EIDOS_OPT_DISPLAY_SCALE), 2);
    BW_CHECK_EQ("default volume", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 100);
    BW_CHECK_EQ("default gap", eidos_settings_get(EIDOS_OPT_LAYOUT_GAP), 0);
    BW_CHECK_EQ("clean at start", eidos_settings_dirty(), 0);

    eidos_settings_set(EIDOS_OPT_DISPLAY_MODE, EIDOS_DISPLAY_BORDERLESS);
    eidos_settings_set(EIDOS_OPT_DISPLAY_SCALE, 3);
    eidos_settings_set(EIDOS_OPT_DISPLAY_SCALING, EIDOS_SCALING_FIT);
    eidos_settings_set(EIDOS_OPT_LAYOUT_ARRANGEMENT, EIDOS_LAYOUT_HORIZONTAL);
    eidos_settings_set(EIDOS_OPT_LAYOUT_SWAP, 1);
    eidos_settings_set(EIDOS_OPT_LAYOUT_GAP, 12);
    eidos_settings_set(EIDOS_OPT_SOUND_VOLUME, 35);
    eidos_settings_set_key(0, -1, SDL_SCANCODE_J);
    eidos_settings_set_pad(2, 0, EIDOS_PADBIND_AXIS_POS, SDL_CONTROLLER_AXIS_RIGHTY);
    BW_CHECK_EQ("dirty", eidos_settings_dirty(), 1);
    BW_CHECK_EQ("commit", eidos_settings_commit(), 0);
    BW_CHECK_EQ("clean after commit", eidos_settings_dirty(), 0);
    BW_CHECK_EQ("no tmp left", bw_path_kind_utf8(INI ".tmp"), BW_PATH_NONE);
    snapshot(o1, k1, pk1, pi1);

    eidos_settings_defaults();
    BW_CHECK_EQ("defaults reset", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 100);
    BW_CHECK_EQ("defaults dirty", eidos_settings_dirty(), 1);
    eidos_settings_revert();
    snapshot(o2, k2, pk2, pi2);
    BW_CHECK_EQ("revert restores options", memcmp(o1, o2, sizeof o1), 0);
    BW_CHECK_EQ("revert restores keys", memcmp(k1, k2, sizeof k1), 0);

    BW_CHECK_EQ("load", eidos_settings_load(), 0);
    snapshot(o2, k2, pk2, pi2);
    BW_CHECK_EQ("round trip options", memcmp(o1, o2, sizeof o1), 0);
    BW_CHECK_EQ("round trip keys", memcmp(k1, k2, sizeof k1), 0);
    BW_CHECK_EQ("round trip pad kind", memcmp(pk1, pk2, sizeof pk1), 0);
    BW_CHECK_EQ("round trip pad index", memcmp(pi1, pi2, sizeof pi1), 0);

    /* Hand edits: unknown keys and bad values are ignored with a warning. */
    fp = fopen(INI, "wb");
    if (fp == NULL) {
        BW_CHECK_EQ("write ini", 0, 1);
        return;
    }
    fputs("# hi\n[Display]\nmode = Fullscreen\nscale = 99\nbogus = 1\n"
          "[sound]\nvolume=70\n[keys]\na = Space\nb = Nonsense Key\n"
          "[mystery]\nx=1\n[pad]\nup = dpup | lefty-\n", fp);
    fclose(fp);
    BW_CHECK_EQ("load edited", eidos_settings_load(), 0);
    BW_CHECK_EQ("mode", eidos_settings_get(EIDOS_OPT_DISPLAY_MODE),
                EIDOS_DISPLAY_FULLSCREEN);
    BW_CHECK_EQ("bad scale keeps default",
                eidos_settings_get(EIDOS_OPT_DISPLAY_SCALE), 2);
    BW_CHECK_EQ("volume", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 70);
    BW_CHECK_EQ("a is Space", eidos_settings_get_key(0, 0), SDL_SCANCODE_SPACE);
    BW_CHECK_EQ("b emptied", eidos_settings_get_key(1, 0), 0);
    BW_CHECK_EQ("untouched button keeps default",
                eidos_settings_get_key(2, 0), SDL_SCANCODE_S);

    /* Command-line overrides apply but are not saved. */
    eidos_settings_load();
    for (i = 1; i < 9; i++) {
        int rc = eidos_settings_parse_arg(9, argv_a, &i);
        BW_CHECK_EQ("arg consumed", rc, 1);
    }
    BW_CHECK_EQ("override mode", eidos_settings_get(EIDOS_OPT_DISPLAY_MODE),
                EIDOS_DISPLAY_FULLSCREEN);
    BW_CHECK_EQ("override scale", eidos_settings_get(EIDOS_OPT_DISPLAY_SCALE), 3);
    BW_CHECK_EQ("override volume", eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 40);
    BW_CHECK_EQ("override layout",
                eidos_settings_get(EIDOS_OPT_LAYOUT_ARRANGEMENT),
                EIDOS_LAYOUT_HORIZONTAL);
    BW_CHECK_EQ("override swap", eidos_settings_get(EIDOS_OPT_LAYOUT_SWAP), 1);
    BW_CHECK_EQ("override is not dirty", eidos_settings_dirty(), 0);
    eidos_settings_set(EIDOS_OPT_LAYOUT_GAP, 6);
    BW_CHECK_EQ("commit with overrides", eidos_settings_commit(), 0);
    eidos_settings_load();
    BW_CHECK_EQ("override not saved: mode",
                eidos_settings_get(EIDOS_OPT_DISPLAY_MODE),
                EIDOS_DISPLAY_FULLSCREEN);      /* the file's, not the flag's */
    BW_CHECK_EQ("override not saved: volume",
                eidos_settings_get(EIDOS_OPT_SOUND_VOLUME), 70);
    BW_CHECK_EQ("override not saved: scale",
                eidos_settings_get(EIDOS_OPT_DISPLAY_SCALE), 2);
    BW_CHECK_EQ("real change saved", eidos_settings_get(EIDOS_OPT_LAYOUT_GAP), 6);
    i = 1;
    BW_CHECK_EQ("bad value", eidos_settings_parse_arg(3, argv_bad, &i), -1);
    i = 1;
    BW_CHECK_EQ("not ours", eidos_settings_parse_arg(2, (char *[]){ "x", "--frames" }, &i), 0);

    {
        char buf[32];
        eidos_settings_set(EIDOS_OPT_SOUND_VOLUME, 55);
        BW_CHECK_EQ("text", strcmp(eidos_settings_value_text(
            EIDOS_OPT_SOUND_VOLUME, buf, sizeof buf), "55%"), 0);
        BW_CHECK_EQ("clamped", (eidos_settings_set(EIDOS_OPT_SOUND_VOLUME, 500),
                                eidos_settings_get(EIDOS_OPT_SOUND_VOLUME)), 100);
    }
    remove(INI);
}

static void test_gain(void)
{
    BW_CHECK_EQ("gain 100", eidos_audio_gain(12345, 100), 12345);
    BW_CHECK_EQ("gain 50", eidos_audio_gain(1000, 50), 500);
    BW_CHECK_EQ("gain 50 neg", eidos_audio_gain(-1000, 50), -500);
    BW_CHECK_EQ("gain 0", eidos_audio_gain(-32768, 0), 0);
    BW_CHECK_EQ("saturate hi", eidos_audio_gain(30000, 200), 32767);
    BW_CHECK_EQ("saturate lo", eidos_audio_gain(-30000, 200), -32768);
}

int main(void)
{
    EidosPlatformConfig cfg;
    EidosPlatform *p = NULL;

    SDL_setenv("SDL_VIDEODRIVER", "dummy", 0);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);
    test_geometry();
    test_gain();

    eidos_settings_set_path(INI);
    remove(INI);
    eidos_settings_load();
    memset(&cfg, 0, sizeof cfg);
    eidos_settings_fill_config(&cfg);
    cfg.audio = 1;
    cfg.audio_push = 1;
    if (eidos_platform_init(&p, &cfg) != 0) {
        printf("no SDL video (dummy driver unavailable): skipped\n");
        BW_REPORT("test_settings");
    }
    eidos_settings_attach(p);
    test_platform_modes(p);
    eidos_settings_set(EIDOS_OPT_SOUND_VOLUME, 50);
    {
        s16 frames[64];
        memset(frames, 0x11, sizeof frames);
        eidos_platform_queue_audio(p, frames, 32);   /* runs with a gain */
    }
    test_bindings(p);
    eidos_settings_detach();
    eidos_platform_quit(p);
    test_file();
    BW_REPORT("test_settings");
}
