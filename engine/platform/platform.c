/*
 * engine/platform/platform.c -- SDL2 window, keyboard, gamepads, the mouse
 * as the stylus, frame pacer, audio device.
 *
 * See platform.h for the contract and for where the numbers come from.
 * Three things in here are decisions rather than transcription, and each is
 * commented where it is made: the pixel format the textures are created
 * with, the way the pacer accumulates the frame period's remainder, and the
 * screenshot being taken at 1x from the port's own buffers rather than read
 * back out of the renderer.
 */

#define SDL_MAIN_HANDLED   /* eidos owns main(); no SDL2main, no #define main */
#include <SDL.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "input.h"
#include "mod_panel_draw.h"
#include "platform.h"
#include "plat_gl.h"
#include "settings.h"

#define MAX_PADS 8

#define SCREEN_W EIDOS_DS_SCREEN_WIDTH
#define SCREEN_H EIDOS_DS_SCREEN_HEIGHT
#define SCREEN_PIXELS (SCREEN_W * SCREEN_H)

#define DEFAULT_WINDOW_SCALE 1

/*
 * The presenter's third image: the run-status panel (engine/platform/
 * mod_panel_draw.h).  The two calls are declared here rather than in
 * plat_gl.h: the panel is the port's own picture, drawn by the platform
 * layer beside the game's screens, and plat_gl.h is the presenter's own
 * header (shared with the 3D path).  The declaration is the port's
 * convention for a call whose header is not this file's, the way
 * engine/platform/mod_panel_draw.c declares the model's.
 */
extern void eidos_gl_upload_panel(EidosGl *gl, const unsigned char *rgba,
                                   int w, int h);
extern void eidos_gl_present_panel(EidosGl *gl, int out_w, int out_h,
                                   const SDL_Rect dst[2],
                                   const SDL_Rect *panel);

struct EidosPlatform {
    SDL_Window   *window;
    SDL_Renderer *renderer;        /* NULL when the GPU presenter runs      */
    EidosGl      *gl;              /* the OpenGL presenter (plat_gl.c)      */
    SDL_Texture  *tex[2];          /* 0 = top, 1 = bottom */
    u8           *shadow[2];       /* the last buffer uploaded to each      */
    SDL_Texture  *pre[2];          /* nearest pre-scale targets             */
    int pre_w[2], pre_h[2];
    int tex_n[2];                  /* internal multiple of each tex/shadow  */

    /* the run-status panel: the port's own picture, beside the screens */
    SDL_Texture  *panel_tex;       /* the software path's texture for it    */
    int panel_tex_w, panel_tex_h;
    u32 panel_gen;                 /* the model's generation last uploaded  */
    int panel_gen_ok;              /* ... and whether there was one         */
    int panel_w, panel_h;          /* the picture's size last uploaded      */

    int layout;
    int gap;
    int swap;
    int display_mode;
    int win_x, win_y;               /* windowed position, kept across fullscreen */
    int presented;                  /* a frame has been drawn at least once  */
    int arr_w, arr_h;               /* the arrangement's size last seen      */
    SDL_threadID owner;             /* the thread that made the window and GL context */
    int pend_display, pend_layout;  /* a change asked for from another thread  */
    int pd_mode, pd_w, pd_h, pl_layout, pl_swap, pl_gap;
    EidosKeyBinding *keymap_own;    /* set by eidos_platform_set_bindings    */
    EidosPadBinding *padmap_own;
    int swallow_sc;                 /* a key held after Alt+Enter, not a press */
    int cap_state;                  /* EIDOS_CAPTURE_*                       */
    EidosCaptured cap;

    const EidosKeyBinding *keymap;
    u32   keymap_count;
    const EidosPadBinding *padmap;
    u32   padmap_count;
    SDL_GameController *pads[MAX_PADS];
    int   pad_subsystem;            /* SDL_INIT_GAMECONTROLLER is up         */

    int touch_down;                 /* the stylus, bottom-screen pixels      */
    int touch_x, touch_y;

    u32 buttons;
    u32 buttons_prev;
    u32 buttons_pressed;
    int quit;
    int shot_request;

    /* the pacer */
    u64 perf_freq;
    u64 perf_start;
    u64 next_ns;
    u64 period_rem;     /* accumulated 1/EIDOS_DS_SYSTEM_CLOCK-ths of a ns */
    int paced;          /* has the first boundary been set                 */
    u32 late_frames;
    u32 lag_frames;      /* whole frames the schedule lagged at the last wait */

    /* audio */
    SDL_AudioDeviceID audio_dev;
    EidosAudioFn      audio_fn;
    void             *audio_user;
    u64               audio_frames;
    volatile int      volume;       /* percent                               */
    int               audio_push;
    /* push mode: the device's own rate, and the resampler's state (the
     * last four input frames and the fractional input position). */
    int               dev_rate;
    double            rs_pos;
    s16               rs_hist[4][2];
};

/* ------------------------------------------------------------------ */
/* Keyboard                                                            */
/* ------------------------------------------------------------------ */

/*
 * Physical key positions (SDL_Scancode), so the defaults land on the same
 * keys on a keyboard that is not QWERTY.  The d-pad is the arrow cluster
 * under the right hand and the four face buttons are the Z X A S block under
 * the left, which is the arrangement every DS-era PC control scheme settled
 * on.  No scancode is bound twice: a table that names one key for two
 * buttons would press both, since the word is rebuilt from the whole table.
 */
static const EidosKeyBinding k_default_keymap[] = {
    { SDL_SCANCODE_UP,        EIDOS_PAD_UP },
    { SDL_SCANCODE_DOWN,      EIDOS_PAD_DOWN },
    { SDL_SCANCODE_LEFT,      EIDOS_PAD_LEFT },
    { SDL_SCANCODE_RIGHT,     EIDOS_PAD_RIGHT },
    { SDL_SCANCODE_X,         EIDOS_PAD_A },
    { SDL_SCANCODE_Z,         EIDOS_PAD_B },
    { SDL_SCANCODE_S,         EIDOS_PAD_X },
    { SDL_SCANCODE_A,         EIDOS_PAD_Y },
    { SDL_SCANCODE_Q,         EIDOS_PAD_L },
    { SDL_SCANCODE_E,         EIDOS_PAD_R },
    { SDL_SCANCODE_RETURN,    EIDOS_PAD_START },
    { SDL_SCANCODE_RSHIFT,    EIDOS_PAD_SELECT },
    { SDL_SCANCODE_BACKSPACE, EIDOS_PAD_SELECT },
};

const EidosKeyBinding *eidos_default_keymap(u32 *count)
{
    if (count != NULL) {
        *count = (u32)(sizeof(k_default_keymap) / sizeof(k_default_keymap[0]));
    }
    return k_default_keymap;
}

/*
 * The word is rebuilt from the whole table on every pump rather than toggled
 * per key event, so a binding table may name several keys for one button
 * (Backspace and Right Shift are both SELECT) and a key held across a focus
 * change cannot leave a bit stuck down.
 */
static u32 read_keyboard(const EidosPlatform *p)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    u32 word = 0;
    u32 i;

    if (keys == NULL) {
        return 0;
    }
    for (i = 0; i < p->keymap_count; i++) {
        int sc = p->keymap[i].scancode;
        if (sc == p->swallow_sc) {
            continue;
        }
        if (sc > 0 && sc < SDL_NUM_SCANCODES && keys[sc]) {
            word |= p->keymap[i].button;
        }
    }
    return word;
}

/* ------------------------------------------------------------------ */
/* Gamepads                                                            */
/* ------------------------------------------------------------------ */

/*
 * Positional: SDL's A/B/X/Y are south/east/west/north on every pad once
 * SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS is off (set in init), and the
 * DS's A/B/X/Y are east/south/north/west, so each DS button lands where it
 * sits on a DS.  The left stick doubles the d-pad and the triggers double
 * the shoulders, past half their travel.
 */
static const EidosPadBinding k_default_padmap[] = {
    { EIDOS_PADBIND_BUTTON, SDL_CONTROLLER_BUTTON_B,             EIDOS_PAD_A },
    { EIDOS_PADBIND_BUTTON, SDL_CONTROLLER_BUTTON_A,             EIDOS_PAD_B },
    { EIDOS_PADBIND_BUTTON, SDL_CONTROLLER_BUTTON_Y,             EIDOS_PAD_X },
    { EIDOS_PADBIND_BUTTON, SDL_CONTROLLER_BUTTON_X,             EIDOS_PAD_Y },
    { EIDOS_PADBIND_BUTTON, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  EIDOS_PAD_L },
    { EIDOS_PADBIND_BUTTON, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, EIDOS_PAD_R },
    { EIDOS_PADBIND_BUTTON, SDL_CONTROLLER_BUTTON_START,         EIDOS_PAD_START },
    { EIDOS_PADBIND_BUTTON, SDL_CONTROLLER_BUTTON_BACK,          EIDOS_PAD_SELECT },
    { EIDOS_PADBIND_BUTTON, SDL_CONTROLLER_BUTTON_DPAD_UP,       EIDOS_PAD_UP },
    { EIDOS_PADBIND_BUTTON, SDL_CONTROLLER_BUTTON_DPAD_DOWN,     EIDOS_PAD_DOWN },
    { EIDOS_PADBIND_BUTTON, SDL_CONTROLLER_BUTTON_DPAD_LEFT,     EIDOS_PAD_LEFT },
    { EIDOS_PADBIND_BUTTON, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    EIDOS_PAD_RIGHT },
    { EIDOS_PADBIND_AXIS_NEG, SDL_CONTROLLER_AXIS_LEFTY,         EIDOS_PAD_UP },
    { EIDOS_PADBIND_AXIS_POS, SDL_CONTROLLER_AXIS_LEFTY,         EIDOS_PAD_DOWN },
    { EIDOS_PADBIND_AXIS_NEG, SDL_CONTROLLER_AXIS_LEFTX,         EIDOS_PAD_LEFT },
    { EIDOS_PADBIND_AXIS_POS, SDL_CONTROLLER_AXIS_LEFTX,         EIDOS_PAD_RIGHT },
    { EIDOS_PADBIND_AXIS_POS, SDL_CONTROLLER_AXIS_TRIGGERLEFT,   EIDOS_PAD_L },
    { EIDOS_PADBIND_AXIS_POS, SDL_CONTROLLER_AXIS_TRIGGERRIGHT,  EIDOS_PAD_R },
};

/* Half an axis's travel (SDL axes run -32768..32767, triggers 0..32767). */
#define PAD_AXIS_THRESHOLD 16384

const EidosPadBinding *eidos_default_padmap(u32 *count)
{
    if (count != NULL) {
        *count = (u32)(sizeof(k_default_padmap) / sizeof(k_default_padmap[0]));
    }
    return k_default_padmap;
}

static SDL_JoystickID pad_id(SDL_GameController *gc)
{
    return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
}

static void pad_open(EidosPlatform *p, int device_index)
{
    SDL_GameController *gc;
    SDL_JoystickID id;
    int i, slot = -1;

    if (!SDL_IsGameController(device_index)) {
        return;
    }
    id = SDL_JoystickGetDeviceInstanceID(device_index);
    for (i = 0; i < MAX_PADS; i++) {
        if (p->pads[i] == NULL) {
            if (slot < 0) {
                slot = i;
            }
        } else if (pad_id(p->pads[i]) == id) {
            return;                     /* already open                     */
        }
    }
    if (slot < 0) {
        return;
    }
    gc = SDL_GameControllerOpen(device_index);
    if (gc != NULL) {
        p->pads[slot] = gc;
    }
}

static void pad_close(EidosPlatform *p, SDL_JoystickID id)
{
    int i;

    for (i = 0; i < MAX_PADS; i++) {
        if (p->pads[i] != NULL && pad_id(p->pads[i]) == id) {
            SDL_GameControllerClose(p->pads[i]);
            p->pads[i] = NULL;
        }
    }
}

int eidos_platform_pads(const EidosPlatform *p)
{
    int i, n = 0;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < MAX_PADS; i++) {
        n += p->pads[i] != NULL;
    }
    return n;
}

static int pad_binding_held(SDL_GameController *gc, const EidosPadBinding *b)
{
    int v;

    if (b->kind == EIDOS_PADBIND_BUTTON) {
        return b->index >= 0 && b->index < SDL_CONTROLLER_BUTTON_MAX &&
               SDL_GameControllerGetButton(
                   gc, (SDL_GameControllerButton)b->index) != 0;
    }
    if (b->index < 0 || b->index >= SDL_CONTROLLER_AXIS_MAX) {
        return 0;
    }
    v = SDL_GameControllerGetAxis(gc, (SDL_GameControllerAxis)b->index);
    if (b->kind == EIDOS_PADBIND_AXIS_POS) {
        return v >= PAD_AXIS_THRESHOLD;
    }
    if (b->kind == EIDOS_PADBIND_AXIS_NEG) {
        return v <= -PAD_AXIS_THRESHOLD;
    }
    return 0;
}

/* Rebuilt from the whole table on every pump, as the keyboard is.         */
static u32 read_pads(const EidosPlatform *p)
{
    u32 word = 0;
    u32 i;
    int k;

    for (k = 0; k < MAX_PADS; k++) {
        if (p->pads[k] == NULL) {
            continue;
        }
        for (i = 0; i < p->padmap_count; i++) {
            if (pad_binding_held(p->pads[k], &p->padmap[i])) {
                word |= p->padmap[i].button;
            }
        }
    }
    return word;
}

/* ------------------------------------------------------------------ */
/* The stylus                                                          */
/* ------------------------------------------------------------------ */

static void place(const EidosPlatform *p, EidosScreenPlacement *pl)
{
    eidos_layout_place(p->layout, p->swap, p->gap, pl);
}

static void out_size(const EidosPlatform *p, int *w, int *h)
{
    *w = *h = 0;
    if (p->gl != NULL) {
        SDL_GL_GetDrawableSize(p->window, w, h);
    } else if (p->renderer == NULL ||
               SDL_GetRendererOutputSize(p->renderer, w, h) != 0) {
        if (p->window != NULL) {
            SDL_GetWindowSize(p->window, w, h);
        }
    }
}

void eidos_platform_viewport(const EidosPlatform *p, EidosViewport *vp)
{
    EidosScreenPlacement pl;
    int w, h;

    place(p, &pl);
    out_size(p, &w, &h);
    eidos_display_viewport(&pl, w, h, vp);
}

int eidos_platform_window_to_touch(const EidosPlatform *p, int win_w,
                                   int win_h, int wx, int wy, int *x, int *y)
{
    EidosScreenPlacement pl;
    EidosViewport vp;

    if (p == NULL || win_w <= 0 || win_h <= 0) {
        return 0;
    }
    place(p, &pl);
    eidos_display_viewport(&pl, win_w, win_h, &vp);
    return eidos_viewport_to_touch(&pl, &vp, wx, wy, x, y);
}

static void read_stylus(EidosPlatform *p)
{
    int mx = 0, my = 0, ww = 0, wh = 0, x = 0, y = 0, inside;
    Uint32 buttons = SDL_GetMouseState(&mx, &my);

    if (!(buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) || p->window == NULL) {
        p->touch_down = 0;
        return;
    }
    SDL_GetWindowSize(p->window, &ww, &wh);
    {
        /* The mouse is in window units, the picture in output pixels. */
        int ow = 0, oh = 0;
        out_size(p, &ow, &oh);
        if (ww > 0 && wh > 0 && ow > 0 && oh > 0) {
            mx = (int)((s64)mx * ow / ww);
            my = (int)((s64)my * oh / wh);
            ww = ow;
            wh = oh;
        }
    }
    inside = eidos_platform_window_to_touch(p, ww, wh, mx, my, &x, &y);
    if (!p->touch_down && !inside) {
        return;                         /* a press off the panel            */
    }
    p->touch_down = 1;
    p->touch_x = x;
    p->touch_y = y;
}

int eidos_platform_touch(const EidosPlatform *p, int *x, int *y)
{
    if (p == NULL || !p->touch_down) {
        return 0;
    }
    if (x != NULL) {
        *x = p->touch_x;
    }
    if (y != NULL) {
        *y = p->touch_y;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Timing                                                              */
/* ------------------------------------------------------------------ */

u64 eidos_frame_period_ns(void)
{
    return (u64)EIDOS_DS_FRAME_CLOCKS * 1000000000ull
           / (u64)EIDOS_DS_SYSTEM_CLOCK;
}

/*
 * Nanoseconds since perf_start, without the overflow a plain
 * `elapsed * 1000000000 / freq` has: the product overflows 64 bits after
 * about half an hour on a 10 MHz performance counter, which is exactly the
 * kind of bug that only shows up in a long session.
 */
static u64 now_ns(const EidosPlatform *p)
{
    u64 elapsed = SDL_GetPerformanceCounter() - p->perf_start;
    u64 whole = elapsed / p->perf_freq;
    u64 frac = elapsed % p->perf_freq;
    return whole * 1000000000ull + frac * 1000000000ull / p->perf_freq;
}

u64 eidos_platform_now_ns(const EidosPlatform *p)
{
    return p == NULL ? 0 : now_ns(p);
}

u32 eidos_platform_lag_frames(const EidosPlatform *p)
{
    return p == NULL ? 0 : p->lag_frames;
}

u32 eidos_platform_late_frames(const EidosPlatform *p)
{
    return p == NULL ? 0 : p->late_frames;
}

int eidos_platform_fast_forward(const EidosPlatform *p)
{
    const Uint8 *keys;

    if (p == NULL || p->window == NULL) {
        return 0;
    }
    /* SDL keeps the state of the window with keyboard focus only, so the
     * key does nothing while another program has the focus. */
    keys = SDL_GetKeyboardState(NULL);
    if (keys != NULL && keys[SDL_SCANCODE_TAB]) {
        return EIDOS_FAST_FORWARD;
    }
    /* EIDOS_FAST_FORWARD=1 holds it down for a whole run (tests, timing). */
    {
        static int env = -1;

        if (env < 0) {
            const char *v = getenv("EIDOS_FAST_FORWARD");
            env = v != NULL && *v != '\0' && strcmp(v, "0") != 0;
        }
        return env ? EIDOS_FAST_FORWARD : 1;
    }
}

void eidos_platform_wait_frame(EidosPlatform *p)
{
    u64 now;
    u64 period;
    u64 step;
    u64 rem_step;

    if (p == NULL) {
        return;
    }
    /* Held Tab: the period is EIDOS_FAST_FORWARD times shorter. */
    period = (u64)EIDOS_DS_FRAME_CLOCKS * 1000000000ull
             / (u64)eidos_platform_fast_forward(p);
    step = period / (u64)EIDOS_DS_SYSTEM_CLOCK;
    /* 560190 * 10^9 mod 33513982 = 3790034, the part of the period the
     * integer nanosecond count drops.  Accumulating it adds the missing
     * nanosecond back every ninth frame instead of losing 0.113 ns a frame. */
    rem_step = period % (u64)EIDOS_DS_SYSTEM_CLOCK;
    now = now_ns(p);
    if (!p->paced) {
        p->next_ns = now;
        p->period_rem = 0;
        p->paced = 1;
    }

    p->next_ns += step;
    p->period_rem += rem_step;
    if (p->period_rem >= (u64)EIDOS_DS_SYSTEM_CLOCK) {
        p->period_rem -= (u64)EIDOS_DS_SYSTEM_CLOCK;
        p->next_ns += 1;
    }

    p->lag_frames = now > p->next_ns ? (u32)((now - p->next_ns) / step) : 0;
    if (now > p->next_ns + 4 * step) {
        /* More than four frames behind: a breakpoint, a suspended machine or
         * a host that cannot keep up.  Running the backlog would show a
         * burst of fast frames, so the schedule is restarted from now. */
        p->late_frames++;
        p->next_ns = now;
        p->period_rem = 0;
        return;
    }

    for (;;) {
        u64 left;
        now = now_ns(p);
        if (now >= p->next_ns) {
            return;
        }
        left = p->next_ns - now;
        if (left > 2500000ull) {
            /* Sleep the bulk, but leave about 2 ms: SDL_Delay may overshoot
             * by the host scheduler's resolution (1 ms at best). */
            SDL_Delay((Uint32)((left - 2000000ull) / 1000000ull));
        } else {
            /* The last stretch: yield the core instead of burning it. */
            SDL_Delay(0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */

/* The master volume: a gain on the final mixed sample, saturating (a gain
 * over 100 percent is allowed for a later boost option). */
s16 eidos_audio_gain(s16 v, int percent)
{
    int r = ((int)v * percent + (v < 0 ? -50 : 50)) / 100;

    return (s16)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
}

void eidos_platform_set_volume(EidosPlatform *p, int percent)
{
    if (p != NULL) {
        p->volume = percent < 0 ? 0 : percent > 400 ? 400 : percent;
    }
}

static void audio_callback(void *user, Uint8 *stream, int len)
{
    EidosPlatform *p = (EidosPlatform *)user;
    u32 frames = (u32)len / (sizeof(s16) * EIDOS_AUDIO_CHANNELS);

    if (p->audio_fn != NULL) {
        p->audio_fn(p->audio_user, (s16 *)stream, frames);
    } else {
        memset(stream, 0, (size_t)len);
    }
    p->audio_frames += frames;
    if (p->volume != 100) {
        s16 *sm = (s16 *)stream;
        u32 n;
        for (n = 0; n < frames * 2u; n++) {
            sm[n] = eidos_audio_gain(sm[n], p->volume);
        }
    }
}

void eidos_platform_set_audio(EidosPlatform *p, EidosAudioFn fn, void *user)
{
    if (p == NULL) {
        return;
    }
    if (p->audio_dev != 0) {
        SDL_LockAudioDevice(p->audio_dev);
    }
    p->audio_fn = fn;
    p->audio_user = user;
    if (p->audio_dev != 0) {
        SDL_UnlockAudioDevice(p->audio_dev);
    }
}

/* Catmull-Rom through four input frames, t in [0,1) between y1 and y2. */
static s16 cubic16(s16 y0, s16 y1, s16 y2, s16 y3, double t)
{
    double a = -0.5 * y0 + 1.5 * y1 - 1.5 * y2 + 0.5 * y3;
    double b = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
    double c = -0.5 * y0 + 0.5 * y2;
    double v = ((a * t + b) * t + c) * t + y1;

    return (s16)(v > 32767.0 ? 32767 : v < -32768.0 ? -32768 : (int)v);
}

/*
 * The game's 32768 Hz stream to the device's rate, converted once, here,
 * with state kept across calls (the position and the last input frames), so
 * chunk boundaries are seamless.  A device that took 32768 Hz needs none.
 */
void eidos_platform_queue_audio(EidosPlatform *p, const s16 *in, u32 frames)
{
    static s16 out[2u * 4096u];

    if (p == NULL || p->audio_dev == 0 || !p->audio_push || frames == 0) {
        return;
    }
    p->audio_frames += frames;
    if (p->dev_rate == EIDOS_AUDIO_RATE) {
        if (p->volume == 100) {
            SDL_QueueAudio(p->audio_dev, in,
                           frames * (u32)(sizeof(s16) * EIDOS_AUDIO_CHANNELS));
        } else {
            u32 done = 0;
            while (done < frames) {
                u32 n = frames - done < 4096u ? frames - done : 4096u, k;
                for (k = 0; k < n * 2u; k++) {
                    out[k] = eidos_audio_gain(in[done * 2u + k], p->volume);
                }
                SDL_QueueAudio(p->audio_dev, out, n * 4u);
                done += n;
            }
        }
        return;
    }
    {
        const double step = (double)EIDOS_AUDIO_RATE / (double)p->dev_rate;
        u32 n = 0;
        u32 i = 0;      /* input frames consumed into the history */
        double pos = p->rs_pos;    /* between hist[1] and hist[2] */

        /* hist[0..3] are input frames k-1, k, k+1, k+2; pos is the phase
         * after hist[1].  Advance the window while pos >= 1. */
        for (;;) {
            while (pos >= 1.0) {
                if (i >= frames) {
                    goto flush;
                }
                p->rs_hist[0][0] = p->rs_hist[1][0];
                p->rs_hist[0][1] = p->rs_hist[1][1];
                p->rs_hist[1][0] = p->rs_hist[2][0];
                p->rs_hist[1][1] = p->rs_hist[2][1];
                p->rs_hist[2][0] = p->rs_hist[3][0];
                p->rs_hist[2][1] = p->rs_hist[3][1];
                p->rs_hist[3][0] = eidos_audio_gain(in[i * 2u], p->volume);
                p->rs_hist[3][1] = eidos_audio_gain(in[i * 2u + 1u], p->volume);
                i++;
                pos -= 1.0;
            }
            out[n * 2u] = cubic16(p->rs_hist[0][0], p->rs_hist[1][0],
                                  p->rs_hist[2][0], p->rs_hist[3][0], pos);
            out[n * 2u + 1u] = cubic16(p->rs_hist[0][1], p->rs_hist[1][1],
                                       p->rs_hist[2][1], p->rs_hist[3][1], pos);
            n++;
            pos += step;
            if (n == 4096u) {
                SDL_QueueAudio(p->audio_dev, out, n * 4u);
                n = 0;
            }
        }
flush:
        p->rs_pos = pos;
        if (n != 0) {
            SDL_QueueAudio(p->audio_dev, out, n * 4u);
        }
    }
}

/* Queued frames, in the game's 32768 Hz units. */
u32 eidos_platform_audio_queued(const EidosPlatform *p)
{
    u64 dev;

    if (p == NULL || p->audio_dev == 0 || !p->audio_push) {
        return 0;
    }
    dev = SDL_GetQueuedAudioSize(p->audio_dev) / (u32)(sizeof(s16) * EIDOS_AUDIO_CHANNELS);
    return (u32)(dev * EIDOS_AUDIO_RATE / (u64)p->dev_rate);
}

int eidos_platform_audio_open(const EidosPlatform *p)
{
    return p != NULL && p->audio_dev != 0;
}

u64 eidos_platform_audio_frames(const EidosPlatform *p)
{
    return p == NULL ? 0 : p->audio_frames;
}

static void open_audio(EidosPlatform *p)
{
    SDL_AudioSpec want, have;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "eidos: no audio subsystem (%s); running silent\n",
                SDL_GetError());
        return;
    }
    memset(&want, 0, sizeof(want));
    want.freq = EIDOS_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = EIDOS_AUDIO_CHANNELS;
    /* 1024 frames is 31 ms at 32768 Hz, under two DS frames: short enough
     * that a sound effect started this frame is not audibly late, long
     * enough that the callback is not called twice per frame. */
    want.samples = 1024;
    want.callback = p->audio_push ? NULL : audio_callback;
    want.userdata = p->audio_push ? NULL : p;

    /* Push mode takes the device's own rate and resamples once itself
     * (queue_audio); callback mode lets SDL convert. */
    if (p->audio_push) {
        want.samples = 1024;
    }
    p->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have,
                                       p->audio_push ? SDL_AUDIO_ALLOW_FREQUENCY_CHANGE : 0);
    if (p->audio_dev == 0) {
        fprintf(stderr, "eidos: no audio device (%s); running silent\n",
                SDL_GetError());
        return;
    }
    p->dev_rate = p->audio_push && have.freq > 0 ? have.freq : EIDOS_AUDIO_RATE;
    p->rs_pos = 1.0;
    SDL_PauseAudioDevice(p->audio_dev, 0);
}

/* ------------------------------------------------------------------ */
/* Window                                                              */
/* ------------------------------------------------------------------ */

static void layout_size(const EidosPlatform *p, u32 *w, u32 *h)
{
    EidosScreenPlacement pl;

    place(p, &pl);
    *w = (u32)pl.width;
    *h = (u32)pl.height;
}

#if SDL_VERSION_ATLEAST(2, 0, 12)
#define HAVE_SCALE_MODE 1
#else
#define HAVE_SCALE_MODE 0
#endif

static void set_filter(SDL_Texture *t, int linear)
{
#if HAVE_SCALE_MODE
    (void)SDL_SetTextureScaleMode(t, linear ? SDL_ScaleModeLinear
                                            : SDL_ScaleModeNearest);
#else
    (void)t;
    (void)linear;
#endif
}

#define PRE_MAX_SIDE 8192
/* Above this many pixels per screen (a 6x6 frame is 1.8M) the nearest
 * prescale pass costs more than it gains: plain linear instead.  The same
 * for a software renderer, where the pass is a CPU copy. */
#define PRE_MAX_PIXELS (2 * 1024 * 1024)

static int renderer_is_software(const EidosPlatform *p)
{
    SDL_RendererInfo ri;

    return SDL_GetRendererInfo(p->renderer, &ri) == 0 &&
           (ri.flags & SDL_RENDERER_SOFTWARE) != 0;
}

/* Draw screen i's texture into dst, choosing the filter (see platform.h). */
static void draw_screen(EidosPlatform *p, int i, const SDL_Rect *dst)
{
    const int tw = (int)SCREEN_W * p->tex_n[i], th = (int)SCREEN_H * p->tex_n[i];
    SDL_Texture *src = p->tex[i];

    if (dst->w < tw || dst->h < th) {
        set_filter(src, 1);                       /* downscale */
    } else if (dst->w % tw == 0 && dst->h % th == 0 &&
               dst->w / tw == dst->h / th) {
        set_filter(src, 0);                       /* whole multiple */
    } else {
        int k = (dst->w + tw - 1) / tw, kh = (dst->h + th - 1) / th;

        if (kh > k) {
            k = kh;
        }
        set_filter(src, 0);
#if HAVE_SCALE_MODE
        if (k * tw <= PRE_MAX_SIDE && k * th <= PRE_MAX_SIDE &&
            (s64)k * tw * k * th <= PRE_MAX_PIXELS && !renderer_is_software(p)) {
            if (p->pre[i] == NULL || p->pre_w[i] != k * tw ||
                p->pre_h[i] != k * th) {
                if (p->pre[i] != NULL) {
                    SDL_DestroyTexture(p->pre[i]);
                }
                p->pre[i] = SDL_CreateTexture(p->renderer,
                                              SDL_PIXELFORMAT_ABGR8888,
                                              SDL_TEXTUREACCESS_TARGET,
                                              k * tw, k * th);
                p->pre_w[i] = k * tw;
                p->pre_h[i] = k * th;
                if (p->pre[i] != NULL) {
                    set_filter(p->pre[i], 1);
                }
            }
            if (p->pre[i] != NULL &&
                SDL_SetRenderTarget(p->renderer, p->pre[i]) == 0) {
                SDL_RenderCopy(p->renderer, src, NULL, NULL);
                SDL_SetRenderTarget(p->renderer, NULL);
                SDL_RenderCopy(p->renderer, p->pre[i], NULL, dst);
                return;
            }
            SDL_SetRenderTarget(p->renderer, NULL);
        }
#endif
        set_filter(src, 1);                       /* plain linear */
    }
    SDL_RenderCopy(p->renderer, src, NULL, dst);
}

/* ------------------------------------------------------------------ */
/* The run-status panel                                                */
/* ------------------------------------------------------------------ */

/*
 * The two screens' own area: their bounding box, in the units the two
 * rectangles are given in.  The arrangement also holds the run-status
 * panel's strip, and the screenshot is the DS's screens: the panel's strip
 * is not part of it.
 */
static void screens_rect(const EidosRect *a, const EidosRect *b,
                         EidosRect *out)
{
    int x0 = a->x < b->x ? a->x : b->x;
    int y0 = a->y < b->y ? a->y : b->y;
    int x1 = a->x + a->w;
    int y1 = a->y + a->h;

    if (b->x + b->w > x1) {
        x1 = b->x + b->w;
    }
    if (b->y + b->h > y1) {
        y1 = b->y + b->h;
    }
    out->x = x0;
    out->y = y0;
    out->w = x1 - x0;
    out->h = y1 - y0;
}

/*
 * The panel's picture and the rectangle it goes to in the output, or -1
 * when there is no panel to show: the player hid it, or no mod has filled
 * a line.
 *
 * The room is the arrangement's (eidos_mod_panel_space), the same one the
 * geometry grew the arrangement by, so the picture lands in the room and
 * never on the game.
 */
static int panel_view(const EidosViewport *vp, const EidosScreenPlacement *pl,
                      EidosModPanelView *view)
{
    EidosModPanelSpace space;

    eidos_mod_panel_space(pl, &space);
    return eidos_mod_panel_view(view, vp, &space, pl->width, pl->height);
}

/*
 * The panel's picture, uploaded only when it changed.  The view carries
 * the model's generation, and the picture is redrawn only when the model's
 * lines change (engine/platform/mod_panel_draw.h), so the picture is
 * uploaded once per change and not once per frame.
 */
static void upload_panel(EidosPlatform *p, const EidosModPanelView *view)
{
    if (p->panel_gen_ok && p->panel_gen == view->generation &&
        p->panel_w == view->w && p->panel_h == view->h) {
        return;                         /* the same picture as last frame */
    }
    p->panel_gen = view->generation;
    p->panel_gen_ok = 1;
    p->panel_w = view->w;
    p->panel_h = view->h;
    if (p->gl != NULL) {
        eidos_gl_upload_panel(p->gl, view->rgba, view->w, view->h);
        return;
    }
    /*
     * The software path: a texture of its own.  ABGR8888 is a packed
     * 32-bit pixel 0xAABBGGRR, which on a little-endian host is the byte
     * order R, G, B, A -- what the panel's own drawing writes, the same
     * reasoning as the screens' textures above.
     */
    if (p->panel_tex == NULL || p->panel_tex_w != view->w ||
        p->panel_tex_h != view->h) {
        if (p->panel_tex != NULL) {
            SDL_DestroyTexture(p->panel_tex);
            p->panel_tex = NULL;
        }
        p->panel_tex = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_ABGR8888,
                                        SDL_TEXTUREACCESS_STREAMING,
                                        view->w, view->h);
        p->panel_tex_w = view->w;
        p->panel_tex_h = view->h;
        if (p->panel_tex == NULL) {
            fprintf(stderr, "eidos: SDL_CreateTexture failed: %s\n",
                    SDL_GetError());
            return;
        }
        set_filter(p->panel_tex, 0);         /* nearest: the text is sharp */
    }
    SDL_UpdateTexture(p->panel_tex, NULL, view->rgba, view->w * 4);
}

/* Draw both screen textures into the current output.  Used by present and
 * by every runtime change, so a new mode shows at once. */
static void render(EidosPlatform *p)
{
    EidosViewport vp;
    EidosScreenPlacement pl;
    EidosModPanelView view;
    int i, ow, oh, have_panel;

    place(p, &pl);
    out_size(p, &ow, &oh);
    eidos_display_viewport(&pl, ow, oh, &vp);
    have_panel = panel_view(&vp, &pl, &view) == 0;
    if (have_panel) {
        upload_panel(p, &view);
    }
    if (p->gl != NULL) {
        SDL_Rect rects[2];
        SDL_Rect prect;

        for (i = 0; i < 2; i++) {
            rects[i].x = vp.screen[i].x;
            rects[i].y = vp.screen[i].y;
            rects[i].w = vp.screen[i].w;
            rects[i].h = vp.screen[i].h;
        }
        if (have_panel && view.dst.w > 0 && view.dst.h > 0) {
            prect.x = view.dst.x;
            prect.y = view.dst.y;
            prect.w = view.dst.w;
            prect.h = view.dst.h;
            eidos_gl_present_panel(p->gl, ow, oh, rects, &prect);
        } else {
            eidos_gl_present(p->gl, ow, oh, rects);
        }
        return;
    }
    SDL_RenderClear(p->renderer);
    for (i = 0; i < 2; i++) {
        SDL_Rect dst;

        dst.x = vp.screen[i].x;
        dst.y = vp.screen[i].y;
        dst.w = vp.screen[i].w;
        dst.h = vp.screen[i].h;
        if (dst.w > 0 && dst.h > 0 && p->tex[i] != NULL) {
            draw_screen(p, i, &dst);
        }
    }
    /* The panel's picture, in the room the arrangement reserved for it. */
    if (have_panel && p->panel_tex != NULL && view.dst.w > 0 &&
        view.dst.h > 0) {
        SDL_Rect dst;

        dst.x = view.dst.x;
        dst.y = view.dst.y;
        dst.w = view.dst.w;
        dst.h = view.dst.h;
        SDL_RenderCopy(p->renderer, p->panel_tex, NULL, &dst);
    }
    SDL_RenderPresent(p->renderer);
}

/* (Re)create screen i's texture and shadow for multiple n. */
static int frame_alloc(EidosPlatform *p, int i, int n)
{
    const int w = (int)SCREEN_W * n, h = (int)SCREEN_H * n;

    if (p->tex[i] != NULL) {
        SDL_DestroyTexture(p->tex[i]);
        p->tex[i] = NULL;
    }
    free(p->shadow[i]);
    p->shadow[i] = NULL;
    /*
     * ABGR8888 is a packed 32-bit pixel 0xAABBGGRR, which on a
     * little-endian host is the byte order R, G, B, A -- exactly what
     * g2d_frame_compose writes.  Naming the packed format rather than
     * SDL_PIXELFORMAT_RGBA32 keeps that reasoning visible.
     */
    if (p->gl == NULL) {
        p->tex[i] = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_ABGR8888,
                                      SDL_TEXTUREACCESS_STREAMING, w, h);
        if (p->tex[i] == NULL) {
            fprintf(stderr, "eidos: SDL_CreateTexture failed: %s\n",
                    SDL_GetError());
            return -1;
        }
    }
    p->shadow[i] = (u8 *)calloc((size_t)w * (size_t)h, 4);
    if (p->shadow[i] == NULL) {
        return -1;
    }
    p->tex_n[i] = n;
    return 0;
}

/* The arrangement changed shape: a windowed (not maximized) window takes the
 * new shape at about the same area, within the desktop work area.  The
 * settings layer learns the new size at once (the SIZE_CHANGED event would
 * tell it later). */
static void reshape_window(EidosPlatform *p)
{
    EidosScreenPlacement pl;
    SDL_Rect area;
    int cw = 0, ch = 0, nw, nh, mw = 0, mh = 0, idx;

    if (p->display_mode != EIDOS_DISPLAY_WINDOWED ||
        (SDL_GetWindowFlags(p->window) &
         (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED |
          SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP))) {
        return;
    }
    idx = SDL_GetWindowDisplayIndex(p->window);
    if (idx >= 0 && SDL_GetDisplayUsableBounds(idx, &area) == 0) {
        mw = area.w;
        mh = area.h;
    }
    SDL_GetWindowSize(p->window, &cw, &ch);
    place(p, &pl);
    eidos_window_reshape(cw, ch, &pl, mw, mh, &nw, &nh);
    if (nw != cw || nh != ch) {
        SDL_SetWindowSize(p->window, nw, nh);
        eidos_settings_on_resize(p, nw, nh);
    }
}

int eidos_platform_display_mode(const EidosPlatform *p)
{
    return p == NULL ? 0 : p->display_mode;
}

static int set_display_now(EidosPlatform *p, int mode, int win_w,
                               int win_h)
{
    u32 w, h;
    int was_full, rc = 0;

    if (p == NULL || p->window == NULL) {
        return -1;
    }
    was_full = p->display_mode != EIDOS_DISPLAY_WINDOWED;
    layout_size(p, &w, &h);

    if (mode == EIDOS_DISPLAY_FULLSCREEN || mode == EIDOS_DISPLAY_BORDERLESS) {
        /* Desktop-resolution fullscreen for both: SDL never switches the OS
         * video mode with FULLSCREEN_DESKTOP, and the presenter scales the
         * picture.  Plain SDL_WINDOW_FULLSCREEN would change it. */
        if (!was_full) {
            SDL_GetWindowPosition(p->window, &p->win_x, &p->win_y);
        }
        if (SDL_SetWindowFullscreen(p->window,
                                    SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            fprintf(stderr, "eidos: fullscreen refused (%s)\n",
                    SDL_GetError());
            rc = -1;
        } else {
            p->display_mode = mode;
        }
    } else {
        int cw = 0, ch = 0;

        SDL_SetWindowFullscreen(p->window, 0);
        SDL_SetWindowBordered(p->window, SDL_TRUE);
        SDL_GetWindowSize(p->window, &cw, &ch);
        if (win_w > 0 && win_h > 0) {
            if (was_full || win_w != cw || win_h != ch) {
                SDL_SetWindowSize(p->window, win_w, win_h);
            }
        } else if (was_full) {
            SDL_SetWindowSize(p->window, (int)w * DEFAULT_WINDOW_SCALE,
                              (int)h * DEFAULT_WINDOW_SCALE);
        }
        if (was_full) {
            SDL_SetWindowPosition(p->window, p->win_x, p->win_y);
        }
        p->display_mode = EIDOS_DISPLAY_WINDOWED;
    }
    if (p->presented) {
        render(p);
    }
    return rc;
}

static int set_layout_now(EidosPlatform *p, int layout, int swap, int gap)
{
    u32 nw, nh;

    if (p == NULL) {
        return -1;
    }
    p->layout = layout == EIDOS_LAYOUT_SIDE_BY_SIDE
                ? EIDOS_LAYOUT_SIDE_BY_SIDE : EIDOS_LAYOUT_STACKED;
    p->swap = swap != 0;
    p->gap = gap > 0 ? gap : 0;
    layout_size(p, &nw, &nh);
    /*
     * The window takes the arrangement's shape, and the arrangement is the
     * two screens and the run-status panel together.  The size it was last
     * shaped for is the one to compare with, so a panel that appeared or
     * went away -- which changes the arrangement without changing the
     * layout -- reshapes the window too.
     */
    if (p->window != NULL &&
        ((int)nw != p->arr_w || (int)nh != p->arr_h)) {
        reshape_window(p);
    }
    p->arr_w = (int)nw;
    p->arr_h = (int)nh;
    if (p->presented) {
        render(p);
    }
    return 0;
}

/* The window and the GL context belong to the thread that made them (the
 * frame thread).  The settings screen runs on a game thread, so a change it
 * asks for is stored and applied by the owner in eidos_platform_pump. */
int eidos_platform_set_display(EidosPlatform *p, int mode, int win_w,
                               int win_h)
{
    if (p == NULL || p->window == NULL) {
        return -1;
    }
    if (p->owner != 0 && SDL_ThreadID() != p->owner) {
        p->pd_mode = mode;
        p->pd_w = win_w;
        p->pd_h = win_h;
        p->pend_display = 1;
        return 0;
    }
    return set_display_now(p, mode, win_w, win_h);
}

int eidos_platform_set_layout(EidosPlatform *p, int layout, int swap, int gap)
{
    if (p == NULL) {
        return -1;
    }
    if (p->owner != 0 && SDL_ThreadID() != p->owner) {
        p->pl_layout = layout;
        p->pl_swap = swap;
        p->pl_gap = gap;
        p->pend_layout = 1;
        return 0;
    }
    return set_layout_now(p, layout, swap, gap);
}

int eidos_platform_set_bindings(EidosPlatform *p, const EidosKeyBinding *keys,
                                u32 nkeys, const EidosPadBinding *pads,
                                u32 npads)
{
    EidosKeyBinding *k = NULL;
    EidosPadBinding *g = NULL;

    if (p == NULL) {
        return -1;
    }
    /* One extra element so an empty table is still a valid, non-NULL one. */
    k = (EidosKeyBinding *)calloc(nkeys + 1u, sizeof(*k));
    g = (EidosPadBinding *)calloc(npads + 1u, sizeof(*g));
    if (k == NULL || g == NULL) {
        free(k);
        free(g);
        return -1;
    }
    if (nkeys > 0) {
        memcpy(k, keys, nkeys * sizeof(*k));
    }
    if (npads > 0) {
        memcpy(g, pads, npads * sizeof(*g));
    }
    free(p->keymap_own);
    free(p->padmap_own);
    p->keymap_own = k;
    p->padmap_own = g;
    p->keymap = k;
    p->keymap_count = nkeys;
    p->padmap = g;
    p->padmap_count = npads;
    return 0;
}

int eidos_platform_init(EidosPlatform **out, const EidosPlatformConfig *cfg)
{
    EidosPlatformConfig defaults;
    EidosPlatform *p;
    u32 w, h;
    int init_w, init_h;

    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    if (cfg == NULL) {
        memset(&defaults, 0, sizeof(defaults));
        cfg = &defaults;
    }

    p = (EidosPlatform *)calloc(1, sizeof(*p));
    if (p == NULL) {
        return -1;
    }
    p->owner = SDL_ThreadID();
    p->tex_n[0] = p->tex_n[1] = 1;
    p->layout = cfg->layout == EIDOS_LAYOUT_SIDE_BY_SIDE
                ? EIDOS_LAYOUT_SIDE_BY_SIDE : EIDOS_LAYOUT_STACKED;
    p->gap = cfg->gap > 0 ? cfg->gap : 0;
    p->swap = cfg->swap_screens != 0;
    p->volume = 100;
    p->swallow_sc = -1;
    if (cfg->keymap != NULL && cfg->keymap_count > 0) {
        p->keymap = cfg->keymap;
        p->keymap_count = cfg->keymap_count;
    } else {
        p->keymap = eidos_default_keymap(&p->keymap_count);
    }
    if (cfg->padmap != NULL && cfg->padmap_count > 0) {
        p->padmap = cfg->padmap;
        p->padmap_count = cfg->padmap_count;
    } else {
        p->padmap = eidos_default_padmap(&p->padmap_count);
    }

    SDL_SetMainReady();
    /*
     * Windows: without DPI awareness the system stretches the finished window
     * bitmap (150 percent on a common laptop or 1440p display), which blurs
     * the sharp picture the presenter draws.  DPI scaling makes SDL declare
     * per-monitor awareness and measure windows in DPI-scaled points, so a
     * 256x384 request is still the size it looks like, drawn at the display's
     * real pixels (the drawable size, which the presenter uses).  Must be set
     * before the video subsystem starts; a user's own setting wins.
     */
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "eidos: SDL_Init failed: %s\n", SDL_GetError());
        free(p);
        return -1;
    }

    /* Nearest by default (the hint is read when a texture is created); the
     * filter is then chosen per draw (draw_screen), where SDL has it. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    layout_size(p, &w, &h);
    p->arr_w = (int)w;
    p->arr_h = (int)h;
    /* The window always opens at the DS's own size for the layout (the saved
     * window size is not used: a size from a maximized or other-shaped window
     * opened oddly and could overflow the screen). */
    init_w = (int)w * DEFAULT_WINDOW_SCALE;
    init_h = (int)h * DEFAULT_WINDOW_SCALE;
    {
        /* For checks only: EIDOS_WINDOW_SIZE=WxH opens the window at that size. */
        const char *ws = getenv("EIDOS_WINDOW_SIZE");
        int ew = 0, eh = 0;

        if (ws != NULL && sscanf(ws, "%dx%d", &ew, &eh) == 2 && ew > 0 && eh > 0) {
            init_w = ew;
            init_h = eh;
        }
    }

    /*
     * The GPU presenter is the default: the CPU hands over small images and
     * the GPU scales them.  SDL's renderer is the fallback (no OpenGL 3.3, the
     * dummy video driver the headless tests use, or EIDOS_RENDERER=sdl).
     */
    {
        const char *drv = SDL_GetCurrentVideoDriver();
        const char *want = getenv("EIDOS_RENDERER");
        int try_gl = !(drv != NULL && strcmp(drv, "dummy") == 0) &&
                     !(want != NULL && strcmp(want, "sdl") == 0);
        Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
                       SDL_WINDOW_ALLOW_HIGHDPI;
        const char *title = cfg->title != NULL ? cfg->title : "eidos";

        if (getenv("EIDOS_WINDOW_HIDDEN") != NULL) {
            /* For unattended checks: a real window system and GPU, no window
             * on the desktop. */
            flags = (flags & ~(Uint32)SDL_WINDOW_SHOWN) | SDL_WINDOW_HIDDEN;
        }

        if (try_gl) {
            p->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                                         SDL_WINDOWPOS_CENTERED, init_w,
                                         init_h, flags | SDL_WINDOW_OPENGL);
            if (p->window != NULL) {
                p->gl = eidos_gl_create(p->window);
                if (p->gl == NULL) {
                    SDL_DestroyWindow(p->window);   /* an OpenGL window cannot take SDL's renderer back */
                    p->window = NULL;
                }
            }
        }
        if (p->window == NULL) {
            p->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                                         SDL_WINDOWPOS_CENTERED, init_w,
                                         init_h, flags);
        }
    }
    if (getenv("EIDOS_RENDERER_REPORT") != NULL) {
        fprintf(stderr, "eidos: presenter %s (video driver %s)\n",
                p->gl != NULL ? "OpenGL 3.3" : "SDL renderer",
                SDL_GetCurrentVideoDriver() != NULL ? SDL_GetCurrentVideoDriver()
                                                    : "none");
    }
    if (p->window == NULL) {
        fprintf(stderr, "eidos: SDL_CreateWindow failed: %s\n", SDL_GetError());
        eidos_platform_quit(p);
        return -1;
    }

    /*
     * No SDL_RENDERER_PRESENTVSYNC: the pacer above is the timebase, and a
     * 60.000 Hz or 144 Hz display would otherwise pull the game off the DS's
     * 59.8261 Hz.  Letting SDL pick the driver keeps the dummy video driver
     * (SDL_VIDEODRIVER=dummy, which the headless test uses) working, where
     * it falls back to the software renderer.
     */
    if (p->gl == NULL) {
        p->renderer = SDL_CreateRenderer(p->window, -1, 0);
        if (p->renderer == NULL) {
            fprintf(stderr, "eidos: SDL_CreateRenderer failed: %s\n",
                    SDL_GetError());
            eidos_platform_quit(p);
            return -1;
        }
        SDL_SetRenderDrawColor(p->renderer, 0, 0, 0, 255);
    }

    if (frame_alloc(p, 0, 1) != 0 || frame_alloc(p, 1, 1) != 0) {
        eidos_platform_quit(p);
        return -1;
    }

    p->perf_freq = SDL_GetPerformanceFrequency();
    if (p->perf_freq == 0) {
        p->perf_freq = 1000000000ull;
    }
    p->perf_start = SDL_GetPerformanceCounter();

    /* Gamepads.  Positions, not labels, on Nintendo pads too (the default
     * table is positional), set before the subsystem reads the hint.  A
     * machine where the subsystem cannot start still runs, on the keyboard.
     * Pads already plugged in arrive as SDL_CONTROLLERDEVICEADDED events at
     * the first pump. */
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0) {
        p->pad_subsystem = 1;
    } else {
        fprintf(stderr, "eidos: no gamepad support (%s); keyboard only\n",
                SDL_GetError());
    }

    /* A hidden window is a test run: it stays silent (EIDOS_NO_AUDIO does
     * the same for a visible one). */
    if (cfg->audio && getenv("EIDOS_WINDOW_HIDDEN") == NULL
        && getenv("EIDOS_NO_AUDIO") == NULL) {
        p->audio_push = cfg->audio_push;
        open_audio(p);
    }

    /* The requested mode, through the same path as a runtime change. */
    if (cfg->display_mode != EIDOS_DISPLAY_WINDOWED) {
        (void)eidos_platform_set_display(p, cfg->display_mode, cfg->win_w,
                                         cfg->win_h);
    }

    *out = p;
    return 0;
}

void eidos_platform_quit(EidosPlatform *p)
{
    int i;

    if (p == NULL) {
        return;
    }
    if (p->audio_dev != 0) {
        SDL_CloseAudioDevice(p->audio_dev);
    }
    for (i = 0; i < MAX_PADS; i++) {
        if (p->pads[i] != NULL) {
            SDL_GameControllerClose(p->pads[i]);
        }
    }
    for (i = 0; i < 2; i++) {
        if (p->tex[i] != NULL) {
            SDL_DestroyTexture(p->tex[i]);
        }
        if (p->pre[i] != NULL) {
            SDL_DestroyTexture(p->pre[i]);
        }
        free(p->shadow[i]);
    }
    if (p->panel_tex != NULL) {
        SDL_DestroyTexture(p->panel_tex);
    }
    if (p->renderer != NULL) {
        SDL_DestroyRenderer(p->renderer);
    }
    eidos_gl_destroy(p->gl);
    if (p->window != NULL) {
        SDL_DestroyWindow(p->window);
    }
    eidos_settings_on_quit(p);
    free(p->keymap_own);
    free(p->padmap_own);
    free(p);
    SDL_Quit();
}

int eidos_platform_pump(EidosPlatform *p)
{
    SDL_Event ev;

    if (p == NULL) {
        return 0;
    }
    if (p->pend_display) {
        p->pend_display = 0;
        (void)set_display_now(p, p->pd_mode, p->pd_w, p->pd_h);
    }
    if (p->pend_layout) {
        p->pend_layout = 0;
        (void)set_layout_now(p, p->pl_layout, p->pl_swap, p->pl_gap);
    }
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            p->quit = 1;
            break;
        case SDL_KEYDOWN:
            if (ev.key.repeat) {
                break;
            }
            if (ev.key.keysym.scancode == SDL_SCANCODE_RETURN &&
                (ev.key.keysym.mod & KMOD_ALT)) {
                /* Alt+Enter: windowed <-> borderless.  The Return that
                 * did it is not a Start press. */
                p->swallow_sc = SDL_SCANCODE_RETURN;
                if (!eidos_settings_on_toggle(p)) {
                    (void)eidos_platform_set_display(
                        p, p->display_mode != EIDOS_DISPLAY_WINDOWED
                               ? EIDOS_DISPLAY_WINDOWED
                               : EIDOS_DISPLAY_BORDERLESS,
                        0, 0);
                }
            } else if (p->cap_state == EIDOS_CAPTURE_PENDING &&
                       ev.key.keysym.scancode != SDL_SCANCODE_F12) {
                if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    p->cap_state = EIDOS_CAPTURE_CANCELLED;
                } else {
                    p->cap.scancode = (int)ev.key.keysym.scancode;
                    p->cap_state = EIDOS_CAPTURE_KEY;
                }
            } else if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                p->quit = 1;
            } else if (ev.key.keysym.scancode == SDL_SCANCODE_F12) {
                p->shot_request = 1;
            }
            break;
        case SDL_KEYUP:
            if ((int)ev.key.keysym.scancode == p->swallow_sc) {
                p->swallow_sc = -1;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (p->cap_state == EIDOS_CAPTURE_PENDING) {
                p->cap.pad_kind = EIDOS_PADBIND_BUTTON;
                p->cap.pad_index = ev.cbutton.button;
                p->cap_state = EIDOS_CAPTURE_PAD;
            }
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (p->cap_state == EIDOS_CAPTURE_PENDING &&
                (ev.caxis.value >= PAD_AXIS_THRESHOLD ||
                 ev.caxis.value <= -PAD_AXIS_THRESHOLD)) {
                p->cap.pad_kind = ev.caxis.value > 0 ? EIDOS_PADBIND_AXIS_POS
                                                     : EIDOS_PADBIND_AXIS_NEG;
                p->cap.pad_index = ev.caxis.axis;
                p->cap_state = EIDOS_CAPTURE_PAD;
            }
            break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED &&
                p->display_mode == EIDOS_DISPLAY_WINDOWED &&
                !(SDL_GetWindowFlags(p->window) &
                  (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP |
                   SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED))) {
                /* A maximized size is the desktop's, not the player's: it is
                 * never remembered (it would reopen in the wrong shape). */
                int cw = 0, ch = 0;

                SDL_GetWindowSize(p->window, &cw, &ch);
                eidos_settings_on_resize(p, cw, ch);
            }
            if (p->presented &&
                (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                 ev.window.event == SDL_WINDOWEVENT_EXPOSED)) {
                render(p);
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (p->pad_subsystem) {
                pad_open(p, ev.cdevice.which);
            }
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            pad_close(p, (SDL_JoystickID)ev.cdevice.which);
            break;
        default:
            break;
        }
    }

    p->buttons_prev = p->buttons;
    p->buttons = p->cap_state == EIDOS_CAPTURE_NONE
                 ? read_keyboard(p) | read_pads(p) : 0u;
    p->buttons_pressed = p->buttons & ~p->buttons_prev;
    read_stylus(p);

    /* The port's input layer takes it from here (sdk/input.h): it reaches
     * KEYINPUT, the ARM7's XY halfword and the touch panel at the next
     * frame boundary. */
    {
        BwInputState in;

        memset(&in, 0, sizeof(in));
        in.buttons = p->buttons;
        in.touch = p->touch_down;
        in.touch_x = p->touch_x;
        in.touch_y = p->touch_y;
        bw_input_set_host(&in);
    }
    return p->quit ? 0 : 1;
}

u32 eidos_platform_buttons(const EidosPlatform *p)
{
    return p == NULL ? 0 : p->buttons;
}

u32 eidos_platform_buttons_pressed(const EidosPlatform *p)
{
    return p == NULL ? 0 : p->buttons_pressed;
}

int eidos_platform_take_screenshot_request(EidosPlatform *p)
{
    int req;

    if (p == NULL) {
        return 0;
    }
    req = p->shot_request;
    p->shot_request = 0;
    return req;
}

void eidos_platform_present(EidosPlatform *p, const u8 *top, const u8 *bottom)
{
    eidos_platform_present_n(p, top, bottom, 1);
}

int eidos_platform_frame_scale(const EidosPlatform *p)
{
    return p == NULL ? 1 : (p->tex_n[0] > p->tex_n[1] ? p->tex_n[0] : p->tex_n[1]);
}

void eidos_platform_present_n(EidosPlatform *p, const u8 *top,
                              const u8 *bottom, int n)
{
    eidos_platform_present_nn(p, top, n, bottom, n);
}

void eidos_platform_present_nn(EidosPlatform *p, const u8 *top, int n_top,
                               const u8 *bottom, int n_bottom)
{
    const u8 *src[2];
    int nn[2];
    int i;

    if (p == NULL) {
        return;
    }
    src[0] = top;
    src[1] = bottom;
    nn[0] = n_top;
    nn[1] = n_bottom;
    for (i = 0; i < 2; i++) {
        size_t bytes;

        if (nn[i] < 1) {
            nn[i] = 1;
        }
        if (nn[i] > 32) {
            nn[i] = 32;
        }
        if (nn[i] != p->tex_n[i] && frame_alloc(p, i, nn[i]) != 0) {
            (void)frame_alloc(p, i, 1);
            return;
        }
        bytes = (size_t)SCREEN_W * (size_t)nn[i] * SCREEN_H * (size_t)nn[i] * 4u;
        if (src[i] != NULL) {
            memcpy(p->shadow[i], src[i], bytes);
        }
        if (p->gl != NULL) {
            eidos_gl_upload(p->gl, i, p->shadow[i], (int)SCREEN_W * nn[i],
                            (int)SCREEN_H * nn[i]);
        } else {
            SDL_UpdateTexture(p->tex[i], NULL, p->shadow[i],
                              (int)SCREEN_W * nn[i] * 4);
        }
    }

    /*
     * The arrangement is the two screens and the run-status panel, so the
     * panel's space is part of the window's shape: a panel that appears
     * (or goes away) changes the arrangement, and the window takes the
     * new shape at the same area, the way a layout change does.
     */
    {
        EidosScreenPlacement pl;

        place(p, &pl);
        if (pl.width != p->arr_w || pl.height != p->arr_h) {
            p->arr_w = pl.width;
            p->arr_h = pl.height;
            reshape_window(p);
        }
    }

    p->presented = 1;
    render(p);
}

int eidos_platform_gpu3d(const EidosPlatform *p)
{
    return p != NULL && p->gl != NULL && eidos_gl_has3d(p->gl);
}

int eidos_platform_read3d_1x(EidosPlatform *p, const struct G3dSnapshot *snap,
                             u8 *out, int async)
{
    if (!eidos_platform_gpu3d(p)) {
        return -1;
    }
    return eidos_gl_read3d_1x(p->gl, snap, out, async);
}

void eidos_platform_set_3d_zero(EidosPlatform *p, const u8 *zero)
{
    if (eidos_platform_gpu3d(p)) {
        eidos_gl_set_3d_zero(p->gl, zero, (int)SCREEN_W, (int)SCREEN_H);
    }
}

int eidos_platform_set_3d(EidosPlatform *p, int screen, const u8 *behind,
                          const struct G3dSnapshot *snap, int n, int bmode,
                          float bfac, int dilate)
{
    if (!eidos_platform_gpu3d(p)) {
        return -1;
    }
    if (screen < 0 || behind == NULL || snap == NULL) {
        eidos_gl_set_3d(p->gl, -1, NULL, 0, 0, 0, 0.f, 0);
        return 0;
    }
    if (n < 1) {
        n = 1;
    }
    if (n > 20) {
        n = 20;
    }
    if (eidos_gl_render3d(p->gl, snap, (int)SCREEN_W * n, (int)SCREEN_H * n) != 0) {
        eidos_gl_set_3d(p->gl, -1, NULL, 0, 0, 0, 0.f, 0);
        return -1;
    }
    eidos_gl_set_3d(p->gl, screen, behind, (int)SCREEN_W, (int)SCREEN_H, bmode,
                    bfac, dilate);
    return 0;
}

int eidos_platform_capture_hires(EidosPlatform *p, const u8 *a2d,
                                 const u8 *behind,
                                 const struct G3dSnapshot *snap, int n,
                                 int blend, int eva, int evb, const u8 *b1x)
{
    if (!eidos_platform_gpu3d(p)) {
        return -1;
    }
    if (n < 1) {
        n = 1;
    }
    if (n > 20) {
        n = 20;
    }
    return eidos_gl_capture(p->gl, a2d, behind, snap, n, blend, eva, evb, b1x);
}

void eidos_platform_show_capture(EidosPlatform *p, int screen, int bmode,
                                 float bfac)
{
    if (p != NULL && p->gl != NULL) {
        eidos_gl_show_capture(p->gl, screen, bmode, bfac);
    }
}

void eidos_platform_capture_begin(EidosPlatform *p)
{
    if (p != NULL) {
        memset(&p->cap, 0, sizeof(p->cap));
        p->cap_state = EIDOS_CAPTURE_PENDING;
    }
}

void eidos_platform_capture_cancel(EidosPlatform *p)
{
    if (p != NULL) {
        p->cap_state = EIDOS_CAPTURE_NONE;
    }
}

int eidos_platform_capture_poll(EidosPlatform *p, EidosCaptured *out)
{
    int st;

    if (p == NULL) {
        return EIDOS_CAPTURE_NONE;
    }
    st = p->cap_state;
    if ((st == EIDOS_CAPTURE_KEY || st == EIDOS_CAPTURE_PAD) && out != NULL) {
        *out = p->cap;
    }
    if (st != EIDOS_CAPTURE_PENDING) {
        p->cap_state = EIDOS_CAPTURE_NONE;
    }
    return st;
}

void eidos_platform_shot_size(const EidosPlatform *p, u32 *w, u32 *h)
{
    EidosScreenPlacement pl;
    EidosRect area;

    if (p == NULL) {
        return;
    }
    place(p, &pl);
    /* The screens, not the arrangement: the screenshot is the DS's screens
     * and the run-status panel's strip is not part of them. */
    screens_rect(&pl.screen[0], &pl.screen[1], &area);
    if (w != NULL) {
        *w = (u32)area.w * (u32)eidos_platform_frame_scale(p);
    }
    if (h != NULL) {
        *h = (u32)area.h * (u32)eidos_platform_frame_scale(p);
    }
}

/*
 * The screenshot is composed from the port's own last-uploaded buffers, not
 * read back out of the renderer: a read-back is driver-dependent (and under
 * the dummy video driver there is nothing to read back at all), while the
 * shadow copies are exactly what the engine produced.  RGB24 is chosen so
 * SDL_SaveBMP writes a plain 24-bit bottom-up BMP that a test can parse with
 * no BMP library.
 */
/* The screenshot as the window shows it, the picture's rectangle only: the
 * GPU path composes the picture on the GPU, so the CPU-side copies are not it
 * (a GPU 3D layer is a placeholder colour there). */
static int screenshot_gl(EidosPlatform *p, const char *path)
{
    EidosViewport vp;
    SDL_Rect rects[2];
    SDL_Surface *surf;
    u8 *rgb, *crop;
    EidosRect area;
    int ow, oh, i, cw, ch, cx, cy, rc;

    out_size(p, &ow, &oh);
    eidos_platform_viewport(p, &vp);
    for (i = 0; i < 2; i++) {
        rects[i].x = vp.screen[i].x;
        rects[i].y = vp.screen[i].y;
        rects[i].w = vp.screen[i].w;
        rects[i].h = vp.screen[i].h;
    }
    rgb = (u8 *)malloc((size_t)ow * (size_t)oh * 3u);
    if (rgb == NULL || eidos_gl_grab(p->gl, ow, oh, rects, rgb) != 0) {
        free(rgb);
        return -1;
    }
    /*
     * The crop is the DS's screens, not the whole arrangement: the
     * run-status panel's strip is beside them (or below them) and is not
     * part of a screenshot of the screens.
     */
    screens_rect(&vp.screen[0], &vp.screen[1], &area);
    cx = area.x < 0 ? 0 : area.x;
    cy = area.y < 0 ? 0 : area.y;
    cw = area.w;
    ch = area.h;
    if (cx + cw > ow) {
        cw = ow - cx;
    }
    if (cy + ch > oh) {
        ch = oh - cy;
    }
    if (cw <= 0 || ch <= 0) {
        free(rgb);
        return -1;
    }
    crop = (u8 *)malloc((size_t)cw * (size_t)ch * 3u);
    if (crop == NULL) {
        free(rgb);
        return -1;
    }
    for (i = 0; i < ch; i++) {
        memcpy(crop + (size_t)i * (size_t)cw * 3u,
               rgb + ((size_t)(cy + i) * (size_t)ow + (size_t)cx) * 3u,
               (size_t)cw * 3u);
    }
    free(rgb);
    surf = SDL_CreateRGBSurfaceWithFormatFrom(crop, cw, ch, 24, cw * 3,
                                              SDL_PIXELFORMAT_RGB24);
    if (surf == NULL) {
        free(crop);
        return -1;
    }
    rc = SDL_SaveBMP(surf, path);
    SDL_FreeSurface(surf);
    free(crop);
    return rc == 0 ? 0 : -1;
}

int eidos_platform_screenshot(EidosPlatform *p, const char *path)
{
    SDL_Surface *surf;
    u8 *pixels;
    u32 w, h, x, y, i;
    size_t pitch;
    int rc;
    EidosScreenPlacement pl;

    if (p == NULL || path == NULL) {
        return -1;
    }
    if (p->gl != NULL && p->presented) {
        return screenshot_gl(p, path);
    }
    eidos_platform_shot_size(p, &w, &h);
    pitch = (size_t)w * 3u;
    pixels = (u8 *)calloc((size_t)h, pitch);
    if (pixels == NULL) {
        return -1;
    }

    place(p, &pl);
    {
        EidosRect area;

        /* The screens' own area: the panel's strip is not in the picture. */
        screens_rect(&pl.screen[0], &pl.screen[1], &area);
        for (i = 0; i < 2; i++) {
            /* The picture is as large as the larger screen; a screen kept at a
             * smaller multiple is repeated up to it. */
            u32 nn = (u32)eidos_platform_frame_scale(p);
            u32 own = (u32)p->tex_n[i], sw = SCREEN_W * nn, sh = SCREEN_H * nn;
            u32 ox = (u32)(pl.screen[i].x - area.x) * nn;
            u32 oy = (u32)(pl.screen[i].y - area.y) * nn;

            for (y = 0; y < sh; y++) {
                const u8 *s = p->shadow[i] +
                              (size_t)(y * own / nn) * SCREEN_W * own * 4u;
                u8 *d = pixels + (size_t)(oy + y) * pitch + (size_t)ox * 3u;
                for (x = 0; x < sw; x++) {
                    const u8 *q = s + (size_t)(x * own / nn) * 4u;

                    d[x * 3 + 0] = q[0];
                    d[x * 3 + 1] = q[1];
                    d[x * 3 + 2] = q[2];
                }
            }
        }
    }

    surf = SDL_CreateRGBSurfaceWithFormatFrom(pixels, (int)w, (int)h, 24,
                                              (int)pitch,
                                              SDL_PIXELFORMAT_RGB24);
    if (surf == NULL) {
        fprintf(stderr, "eidos: surface for the screenshot failed: %s\n",
                SDL_GetError());
        free(pixels);
        return -1;
    }
    rc = SDL_SaveBMP(surf, path);
    SDL_FreeSurface(surf);
    free(pixels);
    if (rc != 0) {
        fprintf(stderr, "eidos: SDL_SaveBMP(%s) failed: %s\n", path,
                SDL_GetError());
        return -1;
    }
    return 0;
}
