/*
 * engine/platform/platform.c -- SDL2 window, frame pacer, audio device.
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

#include "platform.h"

#define SCREEN_W EIDOS_DS_SCREEN_WIDTH
#define SCREEN_H EIDOS_DS_SCREEN_HEIGHT
#define SCREEN_PIXELS (SCREEN_W * SCREEN_H)

struct EidosPlatform {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *tex[2];          /* 0 = top, 1 = bottom */
    u8           *shadow[2];       /* the last buffer uploaded to each      */

    int scale;
    int layout;
    int gap;
    u32 shot_w, shot_h;

    const EidosKeyBinding *keymap;
    u32   keymap_count;

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

    /* audio */
    SDL_AudioDeviceID audio_dev;
    EidosAudioFn      audio_fn;
    void             *audio_user;
    u64               audio_frames;
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
        if (sc > 0 && sc < SDL_NUM_SCANCODES && keys[sc]) {
            word |= p->keymap[i].button;
        }
    }
    return word;
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

u32 eidos_platform_late_frames(const EidosPlatform *p)
{
    return p == NULL ? 0 : p->late_frames;
}

void eidos_platform_wait_frame(EidosPlatform *p)
{
    u64 now;
    u64 step = eidos_frame_period_ns();
    /* 560190 * 10^9 mod 33513982 = 3790034, the part of the period the
     * integer nanosecond count drops.  Accumulating it adds the missing
     * nanosecond back every ninth frame instead of losing 0.113 ns a frame. */
    u64 rem_step = (u64)EIDOS_DS_FRAME_CLOCKS * 1000000000ull
                   % (u64)EIDOS_DS_SYSTEM_CLOCK;

    if (p == NULL) {
        return;
    }
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
        if (left > 2000000ull) {
            /* Sleep the bulk, but leave 1 ms for the spin: SDL_Delay's
             * resolution is the host scheduler's, which is coarser than the
             * 16.7 ms period can absorb. */
            SDL_Delay((Uint32)((left - 1000000ull) / 1000000ull));
        } else {
            SDL_Delay(0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */

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
    want.callback = audio_callback;
    want.userdata = p;

    p->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (p->audio_dev == 0) {
        fprintf(stderr, "eidos: no audio device (%s); running silent\n",
                SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(p->audio_dev, 0);
}

/* ------------------------------------------------------------------ */
/* Window                                                              */
/* ------------------------------------------------------------------ */

static void layout_size(int layout, int gap, u32 *w, u32 *h)
{
    if (layout == EIDOS_LAYOUT_SIDE_BY_SIDE) {
        *w = SCREEN_W * 2 + (u32)gap;
        *h = SCREEN_H;
    } else {
        *w = SCREEN_W;
        *h = SCREEN_H * 2 + (u32)gap;
    }
}

static void screen_rect(const EidosPlatform *p, int which, SDL_Rect *r)
{
    if (p->layout == EIDOS_LAYOUT_SIDE_BY_SIDE) {
        r->x = which == 0 ? 0 : (int)SCREEN_W + p->gap;
        r->y = 0;
    } else {
        r->x = 0;
        r->y = which == 0 ? 0 : (int)SCREEN_H + p->gap;
    }
    r->w = (int)SCREEN_W;
    r->h = (int)SCREEN_H;
    r->x *= p->scale;
    r->y *= p->scale;
    r->w *= p->scale;
    r->h *= p->scale;
}

int eidos_platform_init(EidosPlatform **out, const EidosPlatformConfig *cfg)
{
    EidosPlatformConfig defaults;
    EidosPlatform *p;
    u32 w, h;
    int i;

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
    p->scale = cfg->scale > 0 ? cfg->scale : 2;
    p->layout = cfg->layout == EIDOS_LAYOUT_SIDE_BY_SIDE
                ? EIDOS_LAYOUT_SIDE_BY_SIDE : EIDOS_LAYOUT_STACKED;
    p->gap = cfg->gap > 0 ? cfg->gap : 0;
    if (cfg->keymap != NULL && cfg->keymap_count > 0) {
        p->keymap = cfg->keymap;
        p->keymap_count = cfg->keymap_count;
    } else {
        p->keymap = eidos_default_keymap(&p->keymap_count);
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "eidos: SDL_Init failed: %s\n", SDL_GetError());
        free(p);
        return -1;
    }

    /* Nearest-neighbour, set before the renderer and the textures exist:
     * the hint is read when they are created, not when they are drawn.  An
     * integer upscale of art whose every pixel the 2D engine placed exactly
     * must not be filtered. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    layout_size(p->layout, p->gap, &w, &h);
    p->shot_w = w;
    p->shot_h = h;

    p->window = SDL_CreateWindow(cfg->title != NULL ? cfg->title : "eidos",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 (int)w * p->scale, (int)h * p->scale,
                                 SDL_WINDOW_SHOWN);
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
    p->renderer = SDL_CreateRenderer(p->window, -1, 0);
    if (p->renderer == NULL) {
        fprintf(stderr, "eidos: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        eidos_platform_quit(p);
        return -1;
    }
    SDL_SetRenderDrawColor(p->renderer, 0, 0, 0, 255);

    for (i = 0; i < 2; i++) {
        /*
         * ABGR8888 is a packed 32-bit pixel 0xAABBGGRR, which on a
         * little-endian host is the byte order R, G, B, A -- exactly what
         * g2d_frame_compose writes.  Naming the packed format rather than
         * SDL_PIXELFORMAT_RGBA32 keeps that reasoning visible.
         */
        p->tex[i] = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_ABGR8888,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      (int)SCREEN_W, (int)SCREEN_H);
        if (p->tex[i] == NULL) {
            fprintf(stderr, "eidos: SDL_CreateTexture failed: %s\n",
                    SDL_GetError());
            eidos_platform_quit(p);
            return -1;
        }
        p->shadow[i] = (u8 *)calloc(SCREEN_PIXELS, 4);
        if (p->shadow[i] == NULL) {
            eidos_platform_quit(p);
            return -1;
        }
    }

    p->perf_freq = SDL_GetPerformanceFrequency();
    if (p->perf_freq == 0) {
        p->perf_freq = 1000000000ull;
    }
    p->perf_start = SDL_GetPerformanceCounter();

    if (cfg->audio) {
        open_audio(p);
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
    for (i = 0; i < 2; i++) {
        if (p->tex[i] != NULL) {
            SDL_DestroyTexture(p->tex[i]);
        }
        free(p->shadow[i]);
    }
    if (p->renderer != NULL) {
        SDL_DestroyRenderer(p->renderer);
    }
    if (p->window != NULL) {
        SDL_DestroyWindow(p->window);
    }
    free(p);
    SDL_Quit();
}

int eidos_platform_pump(EidosPlatform *p)
{
    SDL_Event ev;

    if (p == NULL) {
        return 0;
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
            if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                p->quit = 1;
            } else if (ev.key.keysym.scancode == SDL_SCANCODE_F12) {
                p->shot_request = 1;
            }
            break;
        default:
            break;
        }
    }

    p->buttons_prev = p->buttons;
    p->buttons = read_keyboard(p);
    p->buttons_pressed = p->buttons & ~p->buttons_prev;
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
    const u8 *src[2];
    SDL_Rect dst;
    int i;

    if (p == NULL) {
        return;
    }
    src[0] = top;
    src[1] = bottom;

    for (i = 0; i < 2; i++) {
        if (src[i] != NULL) {
            memcpy(p->shadow[i], src[i], (size_t)SCREEN_PIXELS * 4u);
        }
        SDL_UpdateTexture(p->tex[i], NULL, p->shadow[i], (int)SCREEN_W * 4);
    }

    SDL_RenderClear(p->renderer);
    for (i = 0; i < 2; i++) {
        screen_rect(p, i, &dst);
        SDL_RenderCopy(p->renderer, p->tex[i], NULL, &dst);
    }
    SDL_RenderPresent(p->renderer);
}

void eidos_platform_shot_size(const EidosPlatform *p, u32 *w, u32 *h)
{
    if (p == NULL) {
        return;
    }
    if (w != NULL) {
        *w = p->shot_w;
    }
    if (h != NULL) {
        *h = p->shot_h;
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
int eidos_platform_screenshot(EidosPlatform *p, const char *path)
{
    SDL_Surface *surf;
    u8 *pixels;
    u32 w, h, x, y, i;
    size_t pitch;
    int rc;

    if (p == NULL || path == NULL) {
        return -1;
    }
    w = p->shot_w;
    h = p->shot_h;
    pitch = (size_t)w * 3u;
    pixels = (u8 *)calloc((size_t)h, pitch);
    if (pixels == NULL) {
        return -1;
    }

    for (i = 0; i < 2; i++) {
        u32 ox, oy;
        if (p->layout == EIDOS_LAYOUT_SIDE_BY_SIDE) {
            ox = i == 0 ? 0u : SCREEN_W + (u32)p->gap;
            oy = 0;
        } else {
            ox = 0;
            oy = i == 0 ? 0u : SCREEN_H + (u32)p->gap;
        }
        for (y = 0; y < SCREEN_H; y++) {
            const u8 *s = p->shadow[i] + (size_t)y * SCREEN_W * 4u;
            u8 *d = pixels + (size_t)(oy + y) * pitch + (size_t)ox * 3u;
            for (x = 0; x < SCREEN_W; x++) {
                d[x * 3 + 0] = s[x * 4 + 0];
                d[x * 3 + 1] = s[x * 4 + 1];
                d[x * 3 + 2] = s[x * 4 + 2];
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
