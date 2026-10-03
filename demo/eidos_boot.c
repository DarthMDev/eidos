/*
 * demo/eidos_boot.c -- run the game's own ARM9 boot on the host.
 *
 * docs/boot.md is the plan.  This executable is its driver: it installs the
 * player's ARM9 image (docs/armdata.md), resets the emulated machine, points
 * the file layer at the player's extract/, and calls NitroMain, which never
 * returns.  Time passes where the DS would let it pass: when every game
 * thread is waiting (NitroMain's OS_WaitIrq for the vblank, among others),
 * the idle thread halts, and the OS layer's halt hook (bw_os_set_halt_hook)
 * is this file's frame: one pass of the frame loop in engine/frame/, which
 * advances OS_GetTick, raises the vblank interrupt and, with a window,
 * composes and shows both screens.  The interrupt is taken when the halt
 * returns, the handler NitroMain installed runs, and the waiting thread
 * wakes, as on the console.
 *
 *     eidos_boot [--data-root PATH] [--frames N] [--window] [--profile [N]] [--watchdog S]
 *                [--screenshot N[,N...]] [--screenshot-dir DIR]
 *                [--press BUTTONS@FRAME[:HOLD]] [--input SCRIPT]
 *
 * --press START@4800 presses buttons at a frame (held 4 frames unless
 * :HOLD says), --input reads a script of such events (sdk/input.h's text
 * format); both are the same timed input a window's keys join, so a run
 * past the title screen repeats exactly with no window.
 *
 * --screenshot saves both screens, as the hardware state has them at the
 * end of each named frame, to DIR/eidos_boot_frame<N>.png (DIR defaults to
 * the current directory), with or without a window.
 *
 * After N frames (default 600 headless; a window runs until closed) it prints a summary and exits 0.  A panic on
 * the way (a stub reached, an overlay not in this build) ends the run with
 * the panic's message and a failure status, which is how the boot's test
 * reads how far it got (tests/boot_run.cmake).  With --watchdog S, a run
 * that passes no frame for S seconds (a game thread spinning on something
 * the port never delivers) is reported with every thread's call chain
 * (sdk/plat_crash.h) and ends with status 3.
 *
 * Two builds, chosen by the CMake option EIDOS_BOOT:
 *
 *   OFF (the default)  the boot is not linked.  This build compiles only
 *                      argument handling and the data root, says so and
 *                      exits 0, which keeps this file compiling at both
 *                      pointer widths in every build.
 *
 *   ON                 the boot is linked: NitroMain and the closure of its
 *                      matched callees (cmake/boot.cmake).  NitroMain is
 *                      aletheia's matched src/matched/arm9/func_02005124.c,
 *                      or stubs/reimpl_02005124.c with an older aletheia;
 *                      both define NitroMain() as the original loop.
 *
 * Sound: the ARM7 stand-in plays the game's SND command lists on
 * engine/audio (sdk/arm7/snd.c).  With no audio device the driver's clock
 * is run from here, one frame's worth of output frames per frame
 * (bw_snd_render with no buffer), so sequences end and the sound alarms
 * fire on time.  With a window and an audio device, the same frame thread
 * renders that frame's output and queues it on the device (the driver is
 * the game thread's, never run from SDL's audio thread).
 *
 * The window belongs to the thread that runs the frames (the game's idle
 * thread, where the halt hook runs), so it is created at the first frame,
 * not in main().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame.h"
#include "plat_crash.h"
#include "port.h"

#ifndef EIDOS_DEFAULT_DATA_ROOT
#define EIDOS_DEFAULT_DATA_ROOT ""
#endif

#ifndef EIDOS_BOOT
#define EIDOS_BOOT 0
#endif

#ifndef EIDOS_BOOT_SDL
#define EIDOS_BOOT_SDL 0
#endif

#define BOOT_DEFAULT_FRAMES 600u

/* --screenshot: the frames to save both screens at, and where. */
#define BOOT_MAX_SHOTS 32
static u64         g_shots[BOOT_MAX_SHOTS];
static int         g_shot_count;
static const char *g_shot_dir = ".";
/* --g3d-trace N: the 3D command trace of frame N (hw3d.c g3d_hw_trace). */
static u64         g_trace_frame;
static int         g_trace_on;

/*
 * --press NAME@FRAME[:HOLD] and --input FILE: timed input as the text
 * script sdk/input.h reads ("<frame> press <buttons> [<hold>]"), so a run
 * can walk past the title screen with no window and repeat exactly.
 */
static char g_input_script[8192];

static int add_input_text(const char *line)
{
    size_t have = strlen(g_input_script);
    size_t add = strlen(line);

    if (have + add + 2 > sizeof(g_input_script)) {
        return -1;
    }
    memcpy(g_input_script + have, line, add);
    g_input_script[have + add] = '\n';
    g_input_script[have + add + 1] = '\0';
    return 0;
}

/* "START@4800" or "A+B@5000:10": press the buttons at that frame, held for
 * HOLD frames (default 4). */
static int parse_press(const char *arg)
{
    char buttons[64];
    char line[128];
    const char *at = strchr(arg, '@');
    unsigned long frame, hold = 4;
    char *end = NULL;

    if (at == NULL || (size_t)(at - arg) >= sizeof(buttons) || at == arg) {
        return -1;
    }
    memcpy(buttons, arg, (size_t)(at - arg));
    buttons[at - arg] = '\0';
    frame = strtoul(at + 1, &end, 10);
    if (end == at + 1) {
        return -1;
    }
    if (*end == ':') {
        const char *h = end + 1;
        hold = strtoul(h, &end, 10);
        if (end == h) {
            return -1;
        }
    }
    if (*end != '\0') {
        return -1;
    }
    snprintf(line, sizeof(line), "%lu press %s %lu", frame, buttons, hold);
    return add_input_text(line);
}

static int read_input_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    char line[512];

    if (fp == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (add_input_text(line) != 0) {
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return 0;
}

/* "N[,N...]": the frames --screenshot names.  0 on success. */
static int parse_shots(const char *list)
{
    const char *p = list;

    while (*p != '\0') {
        char *end = NULL;
        unsigned long long n = strtoull(p, &end, 10);

        if (end == p || g_shot_count >= BOOT_MAX_SHOTS) {
            return -1;
        }
        g_shots[g_shot_count++] = (u64)n;
        p = end;
        if (*p == ',') {
            p++;
        } else if (*p != '\0') {
            return -1;
        }
    }
    return g_shot_count > 0 ? 0 : -1;
}

#if EIDOS_BOOT

#include "armdata.h"
#include "arm7/arm7.h"
#include "arm7/snd.h"
#include "audio.h"
#include "prof.h"
#include "plat_thread.h"
#include "dsmem.h"
#include "g2d.h"
#include "hw3d.h"
#include "input.h"
#include "os.h"
#include "overlay.h"

#if EIDOS_BOOT_SDL
#include "platform.h"
#include "settings.h"
#endif

#ifndef EIDOS_OVERLAYS
#define EIDOS_OVERLAYS 0
#endif
#if EIDOS_OVERLAYS
#include "overlay_image.h"
/* Generated by tools/ovlink.py (cmake/overlays.cmake, EIDOS_OVERLAYS): the
 * overlays compiled into this executable (docs/overlays.md). */
extern const BwOvlImageSet bw_ovl_set_boot;
#endif

/* aletheia's src/matched/arm9/func_02005124.c (or stubs/reimpl_02005124.c). */
void NitroMain(void);

/*
 * Generated by tools/bootlink.py (cmake/boot.cmake): the link-time
 * constants that are DS addresses.  After every bw_port_reset.
 */
void bw_boot_link_init(void);

/* Generated by tools/armdata.py (cmake/armdata.cmake): the ARM9 image's
 * data for everything this build compiles (docs/armdata.md). */
extern const BwDataImage bw_arm9_data;

#define REG_DISPSTAT 0x04000004u

#define DISPSTAT_VBLANK_FLAG 0x0001u
#define DISPSTAT_VBLANK_IRQ  0x0008u

/* One DS frame is 560190 cycles of the 33513982 Hz system clock. */
#define DS_FRAME_CLOCKS 560190u
#define DS_CLOCK_HZ     33513982u

typedef struct BootRun {
    EidosFrameLoop loop;
    u64 limit;         /* frames to run before the clean exit            */
    u64 vblanks;       /* vblank interrupts raised                       */
    u32 skipped;       /* presents skipped in a row (badly behind)       */
    u64 audio_rem;     /* the output-frame remainder, in clock units     */
    u8 *top;
    u8 *bottom;
#if EIDOS_BOOT_SDL
    EidosPlatform *plat;
    int want_window;   /* --window: open one at the first frame          */
    int window_tried;
#endif
} BootRun;

static BootRun g_run;

/*
 * A PNG of both screens, top above bottom (256 x 384, RGB), written with
 * stored (uncompressed) deflate blocks: no compression library, and any
 * viewer opens it.  One zlib block per scanline keeps each under 64 KiB.
 */
static u32 png_crc_table[256];

static u32 png_crc(u32 crc, const u8 *p, size_t n)
{
    size_t i;

    if (png_crc_table[1] == 0u) {
        u32 c, k;
        for (i = 0; i < 256u; i++) {
            c = (u32)i;
            for (k = 0; k < 8u; k++) {
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            png_crc_table[i] = c;
        }
    }
    crc = ~crc;
    for (i = 0; i < n; i++) {
        crc = png_crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

static void png_be32(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static int png_chunk(FILE *fp, const char *type, const u8 *data, u32 n)
{
    u8 head[8];
    u8 tail[4];
    u32 crc;

    png_be32(head, n);
    memcpy(head + 4, type, 4);
    crc = png_crc(0, head + 4, 4);
    crc = png_crc(crc, data, n);
    png_be32(tail, crc);
    return fwrite(head, 1, 8, fp) == 8 && (n == 0 || fwrite(data, 1, n, fp) == n)
        && fwrite(tail, 1, 4, fp) == 4;
}

static int write_screens_png(const char *path, const u8 *top, const u8 *bottom)
{
    enum { W = 256, H = 384, ROW = 1 + W * 3 };
    static const u8 sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    u8 ihdr[13];
    u8 *z;
    size_t zlen = 2 + (size_t)H * (5 + ROW) + 4;
    size_t at = 0;
    u32 a = 1, b = 0;
    int y, x, ok;
    FILE *fp;

    z = (u8 *)malloc(zlen);
    if (z == NULL) {
        return -1;
    }
    z[at++] = 0x78;
    z[at++] = 0x01;
    for (y = 0; y < H; y++) {
        const u8 *src = (y < 192 ? top : bottom) + (size_t)(y % 192) * W * 4;
        u8 *row;
        int i;

        z[at++] = (u8)(y == H - 1 ? 1 : 0);
        z[at++] = (u8)(ROW & 0xFF);
        z[at++] = (u8)(ROW >> 8);
        z[at++] = (u8)(~ROW & 0xFF);
        z[at++] = (u8)((~ROW >> 8) & 0xFF);
        row = z + at;
        row[0] = 0;                 /* filter: none */
        for (x = 0; x < W; x++) {
            row[1 + x * 3 + 0] = src[x * 4 + 0];
            row[1 + x * 3 + 1] = src[x * 4 + 1];
            row[1 + x * 3 + 2] = src[x * 4 + 2];
        }
        for (i = 0; i < ROW; i++) {
            a = (a + row[i]) % 65521u;
            b = (b + a) % 65521u;
        }
        at += ROW;
    }
    png_be32(z + at, (b << 16) | a);
    at += 4;

    png_be32(ihdr, W);
    png_be32(ihdr + 4, H);
    ihdr[8] = 8;                    /* bit depth */
    ihdr[9] = 2;                    /* RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;

    fp = fopen(path, "wb");
    if (fp == NULL) {
        free(z);
        return -1;
    }
    ok = fwrite(sig, 1, 8, fp) == 8 && png_chunk(fp, "IHDR", ihdr, 13)
      && png_chunk(fp, "IDAT", z, (u32)at) && png_chunk(fp, "IEND", NULL, 0);
    ok = (fclose(fp) == 0) && ok;
    free(z);
    return ok ? 0 : -1;
}

static void boot_ensure_screens(BootRun *run)
{
    size_t screen = (size_t)256 * 192 * 4;

    if (run->top == NULL) {
        run->top = (u8 *)calloc(1, screen);
        run->bottom = (u8 *)calloc(1, screen);
        if (run->top == NULL || run->bottom == NULL) {
            bw_panic("eidos_boot", 0, "out of memory for the screens");
        }
    }
}

/*
 * The display capture runs with the frame it is enabled in, and the title
 * screen feeds each capture back into the next (a blend of the 3D frame
 * with the bank the main engine shows).  A headless run renders only the
 * frames it saves, so without this step the bank holds whatever the last
 * saved frame left in it.  With a window the frame is rendered every
 * frame anyway.
 */
static void boot_capture_step(BootRun *run)
{
    u32 unsupported[2];

#if EIDOS_BOOT_SDL
    if (run->plat != NULL) {
        return;
    }
#endif
    if (!(bw_io_read32(G2D_HW_DISPCAPCNT) & 0x80000000u)) {
        return;
    }
    boot_ensure_screens(run);
    (void)g2d_hw_render_dsmem(g3d_hw_frame(), run->top, run->bottom, unsupported);
}

/* Both screens as the hardware state has them now, saved for frame `n`. */
static void boot_screenshot(BootRun *run, u64 n)
{
    u32 unsupported[2];
    char path[1024];

    boot_ensure_screens(run);
    (void)g2d_hw_render_dsmem(g3d_hw_frame(), run->top, run->bottom, unsupported);
    snprintf(path, sizeof(path), "%s/eidos_boot_frame%llu.png", g_shot_dir,
             (unsigned long long)n);
    if (write_screens_png(path, run->top, run->bottom) == 0) {
        printf("eidos_boot: frame %llu saved to %s\n", (unsigned long long)n, path);
    } else {
        fprintf(stderr, "eidos_boot: cannot write %s\n", path);
    }
    fflush(stdout);
}

/* Frames passed, for the watchdog's thread (a 32-bit word: no tearing). */
static volatile u32 g_frames_seen;

static unsigned long long boot_progress(void)
{
    return g_frames_seen;
}

/*
 * The register file is plain memory (sdk/dsmem.h): the hardware's own
 * updates to DISPSTAT are made on its bytes directly, so no write hook runs
 * for them.  IF is never written here: the OS layer owns it.
 */
static u32 io_raw_read(u32 addr, u32 width)
{
    u32 v = 0;
    memcpy(&v, (const void *)bw_io(addr), width);
    return v;
}

static void io_raw_write(u32 addr, u32 width, u32 v)
{
    memcpy((void *)bw_io(addr), &v, width);
}

/* Which overlays are resident: where the run stands, in the game's terms. */
static void boot_print_resident(void)
{
    const BwOverlayTable *t = bw_overlay_table();
    u32 i;

    printf("eidos_boot: resident overlays:");
    for (i = 0; i < t->overlay_count; i++) {
        if (bw_overlay_is_loaded(t->overlays[i].id)) {
            printf(" %lu", (unsigned long)t->overlays[i].id);
        }
    }
    printf("\n");
}

/* A crash, panic or watchdog report's context (sdk/plat_crash.h): how far
 * the run got, in the form tests/boot_run.cmake reads. */
static void boot_report_context(void)
{
    fflush(stdout);
    fprintf(stderr, "eidos_boot: stopped at frame %llu\n",
            (unsigned long long)eidos_frame_count(&g_run.loop));
    boot_print_resident();
    fflush(stdout);
}

static void boot_finish(int status, const char *why)
{
    printf("eidos_boot: %s after %llu frames, %llu vblank interrupts\n", why,
           (unsigned long long)eidos_frame_count(&g_run.loop),
           (unsigned long long)g_run.vblanks);
    boot_print_resident();
    fflush(stdout);
#if EIDOS_BOOT_SDL
    if (g_run.plat != NULL) {
        eidos_platform_quit(g_run.plat);
        g_run.plat = NULL;
    }
#endif
    /* The game's threads are host threads blocked in the OS layer; the
     * process ends with them, as the console's power switch would. */
    exit(status);
}

/* The hardware's half of vblank: DISPSTAT's flag, and the request when
 * DISPSTAT asks for it.  The OS layer takes it when the halt returns. */
static void boot_vblank(void *user)
{
    BootRun *run = (BootRun *)user;
    u32 dispstat = io_raw_read(REG_DISPSTAT, 2);

    io_raw_write(REG_DISPSTAT, 2, dispstat | DISPSTAT_VBLANK_FLAG);
    bw_io_sync();
    if (dispstat & DISPSTAT_VBLANK_IRQ) {
        bw_os_raise_irq(OS_IE_V_BLANK);
        run->vblanks++;
    }
}

static u64 g_render_ns;   /* this frame's 3D + 2D time, for the profiler */

#if EIDOS_BOOT_SDL
static void boot_render(void *user, u8 *top, u8 *bottom)
{
    u32 unsupported[2];
    u64 t0 = eidos_prof_begin();
    const u8 *rgba3d;

    (void)user;
    rgba3d = g3d_hw_frame();
    eidos_prof_end(EIDOS_PROF_G3D, t0);
    (void)g2d_hw_render_dsmem(rgba3d, top, bottom, unsupported);
    g_render_ns = eidos_prof_on ? bw_clock_ns() - t0 : 0;
}

#endif

/* The output frames one DS frame is worth, the remainder carried. */
static u32 boot_audio_frames(BootRun *run)
{
    u64 num = (u64)BW_AUDIO_RATE * DS_FRAME_CLOCKS + run->audio_rem;
    u32 n = (u32)(num / DS_CLOCK_HZ);

    run->audio_rem = num % DS_CLOCK_HZ;
    return n;
}

#if EIDOS_BOOT_SDL
/*
 * The window, opened by the thread that runs the frames.  The halt hook runs
 * on the game's idle thread, not on main(): a window belongs to the thread
 * that created it (a Win32 window's messages reach only that thread's
 * queue, and SDL's renderer is not thread-safe), so it is created here, at
 * the first frame, and pumped, drawn and closed by the same thread for the
 * rest of the run.  Created on main(), it was never pumped while main() sat
 * in the game's OS_WaitIrq, and Windows showed it frozen after a few
 * seconds.
 */
static void boot_window_open(BootRun *run)
{
    EidosPlatformConfig cfg;

    run->window_tried = 1;
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "eidos boot";
    eidos_settings_fill_config(&cfg);
    cfg.audio = 1;
    cfg.audio_push = 1;
    if (eidos_platform_init(&run->plat, &cfg) != 0) {
        fprintf(stderr, "eidos_boot: no window; running headless\n");
        run->plat = NULL;
    } else {
        eidos_settings_attach(run->plat);
    }
}

/*
 * Sound, clocked by the device.  The game's sound driver (sdk/arm7/snd.c)
 * is the ARM7's, driven by the game thread through the PXI: rendering it
 * from SDL's audio thread would race the game.  So it renders here, on the
 * frame thread, in fixed chunks, and the device's queue is kept filled to a
 * fixed latency (AUDIO_LATENCY_FRAMES, about 70 ms).  A frame that runs late
 * finds the queue shorter and renders more chunks to catch up, so the device
 * never runs dry for a late frame; a frame that is early finds it full and
 * renders none.  The driver's clock follows what the device plays, so the
 * sound neither drifts nor stutters in bursts when the game runs slow.
 */
#define AUDIO_CHUNK_FRAMES   256u
#define AUDIO_LATENCY_FRAMES 2304u     /* 70 ms at 32768 Hz */
#define AUDIO_MAX_PER_CALL   8192u     /* a stall's catch-up bound */

static void boot_audio_push(BootRun *run)
{
    static s16 buf[2u * AUDIO_CHUNK_FRAMES];
    u32 queued = eidos_platform_audio_queued(run->plat);
    u32 done = 0;

    while (queued < AUDIO_LATENCY_FRAMES && done < AUDIO_MAX_PER_CALL) {
        bw_snd_render(buf, AUDIO_CHUNK_FRAMES);
        eidos_platform_queue_audio(run->plat, buf, AUDIO_CHUNK_FRAMES);
        queued += AUDIO_CHUNK_FRAMES;
        done += AUDIO_CHUNK_FRAMES;
    }
}
#endif

/* The halt hook: one frame of time passing. */
static void boot_frame(void *user)
{
    BootRun *run = (BootRun *)user;
    int audio_device = 0;
    u64 t_step, t_ph;

#if EIDOS_BOOT_SDL
    if (run->want_window && !run->window_tried) {
        boot_window_open(run);
    }
#endif

    if (eidos_frame_count(&run->loop) >= run->limit) {
        boot_finish(0, "ran");
    }
    /* Line 0 of the next frame: the vblank flag drops. */
    io_raw_write(REG_DISPSTAT, 2,
                 io_raw_read(REG_DISPSTAT, 2) & ~DISPSTAT_VBLANK_FLAG);

    /* The input of this frame, before its vblank (sdk/input.h): the
     * window's keys and the --press / --input script, KEYINPUT and the
     * ARM7's XY halfword written, the touch panel's automatic samples. */
    t_step = eidos_prof_begin();
    g_render_ns = 0;
    bw_input_frame();
    if (g_trace_on) {
        g3d_hw_trace(eidos_frame_count(&run->loop) + 1u == g_trace_frame);
    }
    eidos_frame_run_one(&run->loop);
    eidos_prof_end(EIDOS_PROF_STEP, t_step + g_render_ns);
    g_frames_seen++;
    boot_capture_step(run);
    {
        u64 n = eidos_frame_count(&run->loop);
        int i;
        for (i = 0; i < g_shot_count; i++) {
            if (g_shots[i] == n) {
                boot_screenshot(run, n);
            }
        }
    }

#if EIDOS_BOOT_SDL
    if (run->plat != NULL) {
        if (!eidos_platform_pump(run->plat)) {
            boot_finish(0, "window closed");
        }
        t_ph = eidos_prof_begin();
        /* Badly behind (two frames or more): skip showing this frame, at
         * most three in a row; the game's steps are never skipped. */
        if (eidos_platform_lag_frames(run->plat) >= 2u && run->skipped < 3u) {
            run->skipped++;
        } else {
            run->skipped = 0;
            eidos_platform_present(run->plat, run->top, run->bottom);
        }
        eidos_prof_end(EIDOS_PROF_PRESENT, t_ph);
        audio_device = eidos_platform_audio_open(run->plat);
        t_ph = eidos_prof_begin();
        if (audio_device) {
            boot_audio_push(run);
        }
        eidos_prof_end(EIDOS_PROF_AUDIO, t_ph);
        t_ph = eidos_prof_begin();
        eidos_platform_wait_frame(run->plat);
        eidos_prof_end(EIDOS_PROF_WAIT, t_ph);
    }
#endif
    if (!audio_device) {
        bw_snd_render(NULL, boot_audio_frames(run));
    }
    eidos_prof_frame(eidos_frame_count(&run->loop));
}

/*
 * Back all of main RAM now.  sdk/dsmem.c commits its flat main RAM in
 * 64 KiB pieces as addresses are first translated, and a pointer reached by
 * arithmetic may run one piece past its translation.  The game goes much
 * further than that: OS_InitArena hands it the whole arena (DS
 * 0x02xxxxxx-0x023E0000, translated once at each end) and its heaps carve
 * megabytes out of it by pointer arithmetic alone.  Translating one
 * address in every piece commits them all, through dsmem's own interface.
 */
static void commit_main_ram(void)
{
    u32 addr;

    for (addr = 0x02000000u; addr < 0x03000000u; addr += 0x10000u) {
        (void)bw_ds_ptr(addr);
    }
}

/* With a window when asked and this build has one; headless otherwise. */
static void boot_window(EidosFrameHooks *hooks, int window)
{
#if EIDOS_BOOT_SDL
    size_t screen = (size_t)256 * 192 * 4;

    if (!window) {
        return;
    }
    /* The window itself opens at the first frame (boot_window_open). */
    g_run.want_window = 1;
    g_run.top = (u8 *)calloc(1, screen);
    g_run.bottom = (u8 *)calloc(1, screen);
    if (g_run.top == NULL || g_run.bottom == NULL) {
        bw_panic("eidos_boot", 0, "out of memory for the screens");
    }
    hooks->render = boot_render;
#else
    (void)hooks;
    if (window) {
        fprintf(stderr, "eidos_boot: built without SDL; running headless\n");
    }
#endif
}

static int run_boot(u64 frames, int window, unsigned watchdog)
{
    EidosFrameHooks hooks;

    memset(&g_run, 0, sizeof g_run);
    g_run.limit = frames;

    memset(&hooks, 0, sizeof hooks);
    hooks.step = NULL;              /* the game runs on its own threads */
    hooks.vblank = boot_vblank;
    hooks.user = &g_run;
    boot_window(&hooks, window);

    /* The image is installed before the first reset, which copies it into
     * main RAM, binds the names and relocates the pointers. */
    bw_data_install(&bw_arm9_data, 0);
#if EIDOS_OVERLAYS
    /* The overlays load through FS_LoadOverlay like the game's own:
     * copied into their windows, bound and relocated at each load. */
    bw_ovl_install(&bw_ovl_set_boot, 0);
#endif
    bw_port_reset();
    bw_input_reset();
    if (g_input_script[0] != '\0') {
        char err[160];
        if (bw_input_script_load(g_input_script, err, sizeof(err)) != 0) {
            fprintf(stderr, "eidos_boot: input script: %s\n", err);
            return 2;
        }
    }
    /* The 3D hardware after the reset that cleared its access hook: the
     * game's geometry FIFO and SWAP_BUFFERS land there, and its frame is
     * the 3D layer the screens are composed with (engine/g3d/hw3d.h). */
    g3d_hw_reset();
    printf("eidos_boot: DS address space %s; %lu code pointers the metadata "
           "does not list relocated by the scan\n",
           bw_ds_identity() ? "identity-mapped" : "on movable host blocks",
           (unsigned long)bw_data_scanned());
    fflush(stdout);
    bw_boot_link_init();
    commit_main_ram();

    eidos_frame_init(&g_run.loop, &hooks, g_run.top, g_run.bottom);
    eidos_frame_set_current(&g_run.loop);
    bw_os_set_halt_hook(boot_frame, &g_run);
    bw_plat_crash_context(boot_report_context);
    if (watchdog != 0) {
        bw_plat_watchdog(watchdog, boot_progress);
    }

    NitroMain();
    bw_panic("NitroMain returned", 0x02005124u, "its loop has no exit");
    return 3;
}

#endif /* EIDOS_BOOT */

static void usage(void)
{
    fprintf(stderr, "usage: eidos_boot [--data-root PATH] [--frames N] [--window] "
                    "[--watchdog SECONDS] [--screenshot N[,N...]] "
                    "[--screenshot-dir DIR] [--g3d-trace N] [--press BUTTONS@FRAME[:HOLD]] "
                    "[--input SCRIPT] [--fullscreen|--borderless|--windowed] "
                    "[--scale N] [--scaling integer|fit] "
                    "[--layout vertical|horizontal] [--swap-screens] "
                    "[--gap N] [--volume N]\n");
}

int main(int argc, char **argv)
{
    const char *data_root = NULL;
    unsigned long long frames = BOOT_DEFAULT_FRAMES;
    int frames_given = 0;
    int window = 0;
    unsigned long watchdog = 0;
    int i;

#if EIDOS_BOOT
    {
        /* The DS address space at its own addresses (docs/boot.md,
         * "Identity mapping"), before anything else maps memory. */
        int status = 0;
        if (bw_ds_identity_prepare(&status)) {
            return status;
        }
    }
#endif
    bw_plat_crash_install();
#if EIDOS_BOOT_SDL
    eidos_settings_load();      /* settings.ini; the flags below override it */
#endif
    for (i = 1; i < argc; i++) {
#if EIDOS_BOOT_SDL
        int sr = eidos_settings_parse_arg(argc, argv, &i);

        if (sr < 0) {
            usage();
            return 2;
        }
        if (sr > 0) {
            continue;
        }
#endif
        if (strcmp(argv[i], "--data-root") == 0 && i + 1 < argc) {
            data_root = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char *end = NULL;
            frames = strtoull(argv[++i], &end, 10);
            frames_given = 1;
            if (end == argv[i] || *end != '\0') {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            if (parse_shots(argv[++i]) != 0) {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "--press") == 0 && i + 1 < argc) {
            if (parse_press(argv[++i]) != 0) {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            if (read_input_file(argv[++i]) != 0) {
                fprintf(stderr, "eidos_boot: cannot read input script %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--g3d-trace") == 0 && i + 1 < argc) {
            char *end = NULL;
            g_trace_frame = strtoull(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0') {
                usage();
                return 2;
            }
            g_trace_on = 1;
        } else if (strcmp(argv[i], "--screenshot-dir") == 0 && i + 1 < argc) {
            g_shot_dir = argv[++i];
        } else if (strcmp(argv[i], "--profile") == 0) {
            unsigned long every = 300;

            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
                every = strtoul(argv[++i], NULL, 10);
            }
#if EIDOS_BOOT
            eidos_prof_enable((unsigned)every);
#else
            (void)every;    /* the profiler is part of the boot build */
#endif
        } else if (strcmp(argv[i], "--window") == 0) {
            window = 1;
        } else if (strcmp(argv[i], "--watchdog") == 0 && i + 1 < argc) {
            char *end = NULL;
            watchdog = strtoul(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || watchdog > 86400ul) {
                usage();
                return 2;
            }
        } else {
            usage();
            return 2;
        }
    }

    if (window && !frames_given) {
        frames = ~0ull; /* a window runs until it is closed */
    }
    if (data_root == NULL) {
        data_root = getenv("BW_DATA_ROOT");
    }
    if (data_root == NULL || data_root[0] == '\0') {
        data_root = EIDOS_DEFAULT_DATA_ROOT;
    }
    if (data_root[0] != '\0') {
        bw_set_data_root(data_root);
    }

#if EIDOS_BOOT
    return run_boot((u64)frames, window, (unsigned)watchdog);
#else
    (void)window;
    (void)frames_given;
    (void)watchdog;
    (void)g_shots;
    (void)g_shot_dir;
    (void)g_input_script;
    printf("eidos_boot: built with EIDOS_BOOT=OFF, so the game's boot is not "
           "linked into this binary (%llu frames requested, data root '%s'). "
           "Reconfigure with -DEIDOS_BOOT=ON; docs/boot.md has the plan.\n",
           frames, bw_data_root());
    return 0;
#endif
}
