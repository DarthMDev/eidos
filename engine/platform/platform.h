/*
 * engine/platform/platform.h -- the host platform layer: the window, the
 * keyboard, gamepads, the mouse as the stylus, the audio device and the
 * frame pacer.
 *
 * This is the only part of eidos that includes SDL2, and it is the only part
 * that is optional: the engine library, the shim, the stubs and every test
 * build and run without it (CMakeLists.txt, EIDOS_SDL).  Nothing here knows
 * anything about the game; it hands the frame loop in engine/frame/ two
 * 256x192 RGBA buffers and a button word, and that is the whole contract.
 *
 * Where the numbers come from:
 *
 *   - The DS is two 256x192 screens.  The window is an integer multiple of
 *     that layout, stacked vertically (the hardware's own arrangement) or
 *     side by side, because a non-integer scale resamples a picture whose
 *     every pixel the 2D engine placed exactly.
 *   - The DS frame rate is the system clock over the frame's clock count,
 *     33513982 / 560190 = 59.8261 Hz, not 60.  The pacer keeps the exact
 *     rational period and accumulates the sub-nanosecond remainder, so a
 *     long session does not drift the way a 16 ms or 16.667 ms constant
 *     would (16 ms is 4.5 seconds fast per hour).
 *   - The audio device is 32768 Hz stereo signed 16-bit, which is the DS
 *     mixer's own output rate; the callback is silent until the sequencer
 *     exists (Milestone 5).
 *   - The button word is the DS keypad bit assignment (KEYINPUT / the SDK's
 *     PAD_* bits), active high here: a set bit means the button is down.
 *     Keeping the hardware's bit order means the matched input code can be
 *     handed this word unchanged when it arrives.
 *   - Every pump hands the buttons and the stylus to the port's input layer
 *     (sdk/input.h, bw_input_set_host), which puts them where the game
 *     reads them at the next frame boundary.  The bindings are tables a
 *     caller may replace; none is hard-coded outside the default tables.
 */

#ifndef EIDOS_ENGINE_PLATFORM_H
#define EIDOS_ENGINE_PLATFORM_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Buttons                                                             */
/* ------------------------------------------------------------------ */

/*
 * The DS keypad, in the hardware's own bit order.  On hardware KEYINPUT is
 * active low; this word is active high (a set bit is a pressed button),
 * because every caller in the port asks "is A down", and the inversion
 * belongs at the one place that reads the register, not at fifty call sites.
 */
#define EIDOS_PAD_A      0x0001u
#define EIDOS_PAD_B      0x0002u
#define EIDOS_PAD_SELECT 0x0004u
#define EIDOS_PAD_START  0x0008u
#define EIDOS_PAD_RIGHT  0x0010u
#define EIDOS_PAD_LEFT   0x0020u
#define EIDOS_PAD_UP     0x0040u
#define EIDOS_PAD_DOWN   0x0080u
#define EIDOS_PAD_R      0x0100u
#define EIDOS_PAD_L      0x0200u
#define EIDOS_PAD_X      0x0400u
#define EIDOS_PAD_Y      0x0800u
#define EIDOS_PAD_DEBUG  0x2000u    /* the debug button (DS development units) */

/*
 * One keyboard binding.  `scancode` is an SDL_Scancode value, a physical key
 * position rather than a character, so the defaults land on the same keys on
 * a QWERTZ or AZERTY keyboard as on a QWERTY one.  Several bindings may name
 * the same button; the button is down when any of them is.
 */
typedef struct EidosKeyBinding {
    int scancode;
    u32 button;
} EidosKeyBinding;

/*
 * The default table, documented in README.md:
 *
 *     d-pad        the arrow keys
 *     A            X          B            Z
 *     X            S          Y            A
 *     L            Q          R            E
 *     START        Return     SELECT       Right Shift, Backspace
 *
 * No key is bound to two buttons: the word is rebuilt from the whole table
 * on every pump, so a key that appeared twice would press both buttons.
 *
 * Escape quits, F12 writes a screenshot.  Those two are the platform
 * layer's own and are not in the table.
 */
const EidosKeyBinding *eidos_default_keymap(u32 *count);

/*
 * One gamepad binding, through SDL's game controller API (every pad SDL's
 * controller database knows, under one positional layout).  `kind` says
 * what `index` is: an SDL_GameControllerButton, or an SDL_GameControllerAxis
 * read past half its travel in the positive or the negative direction.
 * Every open pad is read through the same table; any binding held presses
 * its button.
 */
#define EIDOS_PADBIND_BUTTON   0
#define EIDOS_PADBIND_AXIS_POS 1
#define EIDOS_PADBIND_AXIS_NEG 2

typedef struct EidosPadBinding {
    int kind;
    int index;
    u32 button;
} EidosPadBinding;

/*
 * The default table.  SDL names a pad's face buttons by position (A south,
 * B east, X west, Y north; the platform layer asks SDL for positions on
 * Nintendo pads too), and the defaults keep the DS's own positions: DS A is
 * east, B south, X north, Y west.
 *
 *     A            east (SDL B)       B            south (SDL A)
 *     X            north (SDL Y)      Y            west (SDL X)
 *     L, R         the shoulders and the triggers
 *     START        Start              SELECT       Back
 *     d-pad        the d-pad and the left stick
 */
const EidosPadBinding *eidos_default_padmap(u32 *count);

/* ------------------------------------------------------------------ */
/* The window                                                          */
/* ------------------------------------------------------------------ */

#define EIDOS_DS_SCREEN_WIDTH  256u
#define EIDOS_DS_SCREEN_HEIGHT 192u

#define EIDOS_LAYOUT_STACKED       0   /* top screen above the bottom one */
#define EIDOS_LAYOUT_SIDE_BY_SIDE  1

typedef struct EidosPlatformConfig {
    const char *title;       /* NULL: "eidos"                            */
    int   scale;             /* integer multiple of 256x192; 0 means 2   */
    int   layout;            /* EIDOS_LAYOUT_*                           */
    int   gap;               /* DS pixels between the two screens        */
    int   audio;             /* non-zero: open the audio device          */
    int   audio_push;        /* non-zero: the program pushes its samples
                                from its own thread (eidos_platform_queue_
                                audio) instead of a callback on SDL's     */
    const EidosKeyBinding *keymap;  /* NULL: eidos_default_keymap()      */
    u32   keymap_count;
    const EidosPadBinding *padmap;  /* NULL: eidos_default_padmap()      */
    u32   padmap_count;
} EidosPlatformConfig;

typedef struct EidosPlatform EidosPlatform;

/*
 * Create the window, the renderer, the two screen textures and (when asked)
 * the audio device.  Returns 0 on success; on failure nothing is left open
 * and the SDL error is printed on stderr.  A failure to open *audio* is not
 * a failure to start: the message is printed and the platform runs silent,
 * because a machine with no sound device should still show the game.
 */
int  eidos_platform_init(EidosPlatform **out, const EidosPlatformConfig *cfg);
void eidos_platform_quit(EidosPlatform *p);

/*
 * Drain the event queue, update the button word and the stylus, and hand
 * both to the input layer (bw_input_set_host, sdk/input.h).  Returns 0
 * when the application has been asked to quit (the window was closed or
 * Escape was pressed), non-zero to keep running.
 */
int  eidos_platform_pump(EidosPlatform *p);

/* The buttons currently held, as EIDOS_PAD_* bits. */
u32  eidos_platform_buttons(const EidosPlatform *p);

/* The buttons that went down during the last pump (edge-triggered). */
u32  eidos_platform_buttons_pressed(const EidosPlatform *p);

/*
 * The stylus: the left mouse button, or a finger (SDL turns touches into
 * the mouse), on the bottom screen.  A press starts only inside the bottom
 * screen; a drag that leaves it is held to the nearest edge, as a stylus
 * cannot leave the panel without being lifted.  Window coordinates are
 * mapped through the window's current size, so they stay right if the
 * window is resized.  Returns non-zero while it is down, with the
 * bottom-screen pixel in *x (0..255) and *y (0..191).
 */
int  eidos_platform_touch(const EidosPlatform *p, int *x, int *y);

/*
 * The mapping eidos_platform_touch uses, exposed for tests: the
 * bottom-screen pixel under window point (wx, wy) for a window `win_w` x
 * `win_h` showing this platform's layout.  Returns non-zero when the point
 * is on the bottom screen; *x and *y are clamped to it either way.
 */
int  eidos_platform_window_to_touch(const EidosPlatform *p, int win_w,
                                    int win_h, int wx, int wy,
                                    int *x, int *y);

/* Gamepads currently open.                                                */
int  eidos_platform_pads(const EidosPlatform *p);

/* Non-zero once F12 has been pressed since the last call; clears the flag. */
int  eidos_platform_take_screenshot_request(EidosPlatform *p);

/*
 * Upload two 256x192 RGBA buffers (byte order R, G, B, A -- what
 * g2d_frame_compose writes) and present them.  Either may be NULL, which
 * leaves that screen showing whatever was last uploaded to it.
 */
void eidos_platform_present(EidosPlatform *p, const u8 *top, const u8 *bottom);

/*
 * Write the last presented pair as a BMP through SDL_SaveBMP, at 1x in the
 * configured layout (256 x 384 stacked, 512 x 192 side by side, plus the
 * gap).  The window's own scale is deliberately not applied: the file is
 * meant to be checked, and an integer upscale carries no information the DS
 * resolution does not.  Returns 0 on success.
 *
 * This works under SDL_VIDEODRIVER=dummy, which is what lets a headless test
 * check that the engine actually drew something.
 */
int  eidos_platform_screenshot(EidosPlatform *p, const char *path);

/* The dimensions eidos_platform_screenshot writes, for a caller that wants
 * to check them without opening the file. */
void eidos_platform_shot_size(const EidosPlatform *p, u32 *w, u32 *h);

/* ------------------------------------------------------------------ */
/* The frame pacer                                                     */
/* ------------------------------------------------------------------ */

/*
 * The DS video timing: 355 dots x 263 lines x 6 clocks per dot = 560190
 * system clocks per frame at 33513982 Hz, i.e. 59.8261 frames per second.
 */
#define EIDOS_DS_SYSTEM_CLOCK   33513982u
#define EIDOS_DS_FRAME_CLOCKS     560190u

/* The exact frame period in nanoseconds, floor and remainder:
 * 560190 * 10^9 / 33513982 = 16715113 ns + 3790034 / 33513982 ns, i.e.
 * 16715113.113 ns.  A 16 ms period would run 4.3 percent fast and a
 * 16666667 ns one 0.29 percent fast, which is a frame every six minutes. */
u64 eidos_frame_period_ns(void);

/*
 * Sleep until the next frame boundary.  The boundary advances by the exact
 * rational period every call and the remainder is accumulated, so the n-th
 * frame is due at round(n * 560190 * 10^9 / 33513982) ns after the first.
 * A caller that falls more than a few frames behind (a breakpoint, a
 * suspended laptop) is resynchronised rather than made to run the backlog.
 */
void eidos_platform_wait_frame(EidosPlatform *p);

/* Monotonic nanoseconds since the platform was created. */
u64  eidos_platform_now_ns(const EidosPlatform *p);

/* How many times the pacer had to resynchronise because the frame took
 * longer than the period; a non-zero count means the host is too slow or
 * something blocked. */
u32  eidos_platform_late_frames(const EidosPlatform *p);
/* Whole frames the schedule lagged when the last frame wait began (0 when on
 * time): a caller that is badly behind may skip presenting, never a step. */
u32  eidos_platform_lag_frames(const EidosPlatform *p);

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */

/*
 * The DS mixer's output rate.  The ARM7 sound core mixes its sixteen
 * channels at 32768 Hz; opening the host device at the same rate keeps the
 * eventual sequencer (Milestone 5) from resampling twice.
 */
#define EIDOS_AUDIO_RATE     32768
#define EIDOS_AUDIO_CHANNELS 2

/*
 * The callback shape: fill `frames` stereo signed-16 frames (2 * frames
 * samples) and return.  With no callback registered the device is fed
 * silence, which is what happens today.
 */
typedef void (*EidosAudioFn)(void *user, s16 *out, u32 frames);

void eidos_platform_set_audio(EidosPlatform *p, EidosAudioFn fn, void *user);

/* Non-zero when a device is open. */
int  eidos_platform_audio_open(const EidosPlatform *p);

/* How many frames of audio the silent callback has produced, which is the
 * only observable the device gives us until there is a mixer. */
u64  eidos_platform_audio_frames(const EidosPlatform *p);

/*
 * The push mode (EidosPlatformConfig.audio_push): no callback runs on SDL's
 * audio thread, so a producer that is not thread-safe (the game's own sound
 * driver, which the game thread drives through the ARM7 stand-in) renders
 * on its own thread and hands the frames over here.  `queue` copies
 * `frames` stereo frames to the device's queue; `queued` is how many are
 * waiting, which the producer keeps a few frames' worth ahead.  Both do
 * nothing (0) without a push-mode device.
 */
void eidos_platform_queue_audio(EidosPlatform *p, const s16 *in, u32 frames);
u32  eidos_platform_audio_queued(const EidosPlatform *p);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_PLATFORM_H */
