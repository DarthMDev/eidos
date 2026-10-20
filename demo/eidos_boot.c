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
 *                [--press BUTTONS@FRAME[:HOLD]] [--input SCRIPT] [--keep-going]
 *
 * --keep-going makes a stub (a function the build does not define yet)
 * report itself once and return instead of stopping the run, so one run
 * lists every stub on its way; what follows a stub is not reliable.
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
 * Started with no arguments it opens the window, as a double click does.
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

#include "mod_hooks.h"
#include "mod_loader.h"
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
#define BOOT_CPU_3D_MAX     2u

/* --screenshot: the frames to save both screens at, and where. */
#define BOOT_MAX_SHOTS 1024
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

/*
 * A call to a DS code address (sdk/plat_crash.h, bw_plat_crash_code_resolver):
 * the host function the build compiled for it.  The ARM9 image's function
 * table first (tools/armdata.py's k_funcs: every function the build defines,
 * the veneers and the adjustor thunks, by their DS value), then the overlay
 * resident over the address.
 */
static void *boot_resolve_code(unsigned int ds_addr, const char **name)
{
    const BwDataImage *img = bw_data_installed();
    u32 i;

    if (img != NULL) {
        for (i = 0; i < img->func_count; i++) {
            if (img->funcs[i].addr == ds_addr) {
                *name = img->funcs[i].name;
                return (void *)img->funcs[i].fn;
            }
        }
    }
#if EIDOS_OVERLAYS
    {
        BwDataFn fn = bw_ovl_code_entry(ds_addr);
        if (fn != NULL) {
            *name = "overlay code";
            return (void *)fn;
        }
    }
#endif
    return NULL;
}

#define REG_DISPSTAT 0x04000004u

#define DISPSTAT_VBLANK_FLAG 0x0001u
/* VCOUNT, the line the display is on.  The port has no line timing, so it
 * holds the line the game's code would see: 192, the first line of vblank,
 * from vblank until the next frame starts, and 0 from then.  The game's
 * frame work runs after the vblank wait, while the console is still in
 * vblank, and some of it checks for that: MCSS_TransTextureTask (the
 * intro's professor and Pokemon, battle sprites) uploads its texture only
 * when VCOUNT is between 192 and 200, and with VCOUNT stuck at 0 the sprites
 * never became drawable. */
#define REG_VCOUNT 0x04000006u
#define VCOUNT_VBLANK_LINE 192u
#define DISPSTAT_VBLANK_IRQ  0x0008u

/* One DS frame is 560190 cycles of the 33513982 Hz system clock. */
#define DS_FRAME_CLOCKS 560190u
#define DS_CLOCK_HZ     33513982u

typedef struct BootRun {
    EidosFrameLoop loop;
    u64 limit;         /* frames to run before the clean exit            */
    u64 vblanks;       /* vblank interrupts raised                       */
    u32 skipped;       /* presents skipped in a row (badly behind)       */
    u64 ff_shown_ns;   /* fast forward: when a frame was last shown      */
    int ff_skip;       /* fast forward: this frame is neither drawn nor shown */
    u32 ff_since;      /* fast forward: frames since one was shown        */
    u64 audio_rem;     /* the output-frame remainder, in clock units     */
    u8 *top;
    u8 *bottom;
#if EIDOS_BOOT_SDL
    u8 *hi_top;        /* the window's frames, 256n x 192n               */
    u8 *hi_bottom;
    u32 hi_n;          /* the n those buffers are sized for              */
    u8 *behind;        /* the 3D screen with its 3D layer off, 256x192   */
    int gpu3d_ok;      /* the last frame could use the GPU rasteriser    */
    u8 *frame1x;       /* the GPU's 3D frame at DS size (read back)      */
    int frame1x_valid; /* the GPU drew this frame's 3D (frame1x or sentinel) */
    u8 *sentinel;      /* a 3D layer of one unlikely colour, alpha 255   */
    u8 *black3d;       /* a 3D layer of opaque black                      */
    u8 *zero;          /* the 3D screen composed with black3d, 256x192    */
    int gpu_real;      /* this frame's 3D layer is the real read-back frame */
    u8 *cap_a;         /* capture source A with the sentinel 3D layer, 256x192 */
    u8 *cap_behind;    /* the same with the 3D layer off                  */
    u8 *cap_b;         /* the captured bank as RGBA, to seed the GPU's copy */
    int cap_ok;        /* the GPU holds the capture of bank cap_bank      */
    int cap_bank;
    u32 cap_hash;      /* the bank's content after the capture the GPU mirrors */
    u32 hi_sn[2];      /* the n each screen was written at (top, bottom) */
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

#if EIDOS_BOOT_SDL
/* The GPU's 3D frame at DS size, read back now, or NULL when the CPU drew it. */
static const u8 *boot_gpu_frame(BootRun *run)
{
    const G3dSnapshot *snap;

    if (!run->frame1x_valid || run->plat == NULL || run->frame1x == NULL) {
        return NULL;
    }
    snap = g3d_hw_snapshot();
    if (snap == NULL
        || eidos_platform_read3d_1x(run->plat, snap, run->frame1x, 0) < 0) {
        return NULL;
    }
    return run->frame1x;
}
#else
static const u8 *boot_gpu_frame(BootRun *run)
{
    (void)run;
    return NULL;
}
#endif

/*
 * EIDOS_G3D_DUMP=dir: at each screenshot, write what the 3D engine was given
 * for that frame: a text summary per polygon (attributes, texture, vertex
 * colours) and each decoded texture as a PPM (alpha in a second file).  A
 * debugging aid for 3D accuracy work.
 */
/* EIDOS_IO_DUMP=1: the display registers at each screenshot. */
static void boot_dump_io(u64 n)
{
    u32 base, e;

    if (getenv("EIDOS_IO_DUMP") == NULL) {
        return;
    }
    for (e = 0; e < 2u; e++) {
        base = e ? 0x04001000u : 0x04000000u;
        fprintf(stderr, "io %llu %s: DISPCNT %08X BG0CNT %04X BG1CNT %04X BG2CNT %04X BG3CNT %04X"
                " BG0OFS %04X/%04X BG3OFS %04X/%04X BLDCNT %04X MASTER_BRIGHT %04X\n",
                (unsigned long long)n, e ? "sub " : "main", bw_io_read32(base),
                bw_io_read16(base + 8), bw_io_read16(base + 10), bw_io_read16(base + 12),
                bw_io_read16(base + 14), bw_io_read16(base + 0x10), bw_io_read16(base + 0x12),
                bw_io_read16(base + 0x1C), bw_io_read16(base + 0x1E), bw_io_read16(base + 0x50),
                bw_io_read16(base + 0x6C));
    }
    {
        /* EIDOS_PEEK=addr:words prints DS memory beside the registers. */
        const char *pk = getenv("EIDOS_PEEK");
        unsigned addr = 0, words = 0, k;

        if (pk != NULL && sscanf(pk, "%x:%u", &addr, &words) == 2) {
            for (k = 0; k < words; k++) {
                const u32 *w = (const u32 *)bw_ds_ptr(addr + k * 4u);

                fprintf(stderr, "peek %08X: %08X\n", addr + k * 4u, w != NULL ? *w : 0u);
            }
        }
    }
    fprintf(stderr, "io %llu DISPCAPCNT %08X\n", (unsigned long long)n, bw_io_read32(0x04000064u));
    fprintf(stderr, "io %llu POWCNT1 %04X VRAMCNT A-D %02X %02X %02X %02X\n", (unsigned long long)n,
            bw_io_read16(0x04000304u), bw_io_read16(0x04000240u) & 0xFFu,
            (bw_io_read16(0x04000240u) >> 8) & 0xFFu, bw_io_read16(0x04000242u) & 0xFFu,
            (bw_io_read16(0x04000242u) >> 8) & 0xFFu);
}

static void boot_dump_g3d(u64 n)
{
    const char *dir = getenv("EIDOS_G3D_DUMP");
    const G3dSnapshot *s = g3d_hw_snapshot();
    char path[1024];
    FILE *f;
    u32 i;

    if (dir == NULL || s == NULL) {
        return;
    }
    snprintf(path, sizeof(path), "%s/g3d_%llu.txt", dir, (unsigned long long)n);
    f = fopen(path, "w");
    if (f != NULL) {
        fprintf(f, "polys %u verts %u tex %u disp3dcnt %08X wbuf %u alpha_ref %u "
                   "highlight %u clear %08X unsupported %u\n",
                s->n_polys, s->n_verts, s->n_tex, s->disp3dcnt, s->wbuffer,
                s->alpha_ref, s->highlight, s->clear_color, s->unsupported);
        for (i = 0; i < s->n_polys; i++) {
            const G3dPoly *p = &s->polys[i];
            u32 k;

            fprintf(f, "poly %u n=%u attr=%08X (mode %u alpha %u lights %X id %u) "
                       "teximage=%08X (fmt %u) tex=%d  colours:",
                    i, p->count, p->attr, G3D_ATTR_MODE(p->attr),
                    G3D_ATTR_ALPHA(p->attr), G3D_ATTR_LIGHTS(p->attr),
                    G3D_ATTR_ID(p->attr), p->teximage, G3D_TEX_FORMAT(p->teximage),
                    s->poly_tex[i]);
            for (k = 0; k < p->count; k++) {
                const G3dVertex *v = &s->verts[s->idx[p->first + k]];

                fprintf(f, " (%u,%u,%u|%d,%d,%d,%d|%d,%d)", v->r, v->g, v->b, v->x, v->y, v->z, v->w, v->s, v->t);
            }
            fprintf(f, "\n");
        }
        fclose(f);
    }
    {
        /* the 3D layer as the CPU rasteriser drew it, before the 2D engines */
        const u8 *raw = g3d_hw_frame();
        u32 n3 = g3d_hw_frame_scale(), px3 = 256u * n3 * 192u * n3, q;

        snprintf(path, sizeof(path), "%s/g3d_%llu_layer.ppm", dir, (unsigned long long)n);
        f = raw != NULL ? fopen(path, "wb") : NULL;
        if (f != NULL) {
            fprintf(f, "P6 %u %u 255 ", 256u * n3, 192u * n3);
            for (q = 0; q < px3; q++) {
                fwrite(raw + q * 4u, 1, 3, f);
            }
            fclose(f);
        }
    }
    for (i = 0; i < s->n_tex; i++) {
        u32 k, npx = s->tex[i].w * s->tex[i].h;

        snprintf(path, sizeof(path), "%s/g3d_%llu_tex%u.ppm", dir,
                 (unsigned long long)n, i);
        f = fopen(path, "wb");
        if (f == NULL) {
            continue;
        }
        fprintf(f, "P6 %u %u 255 ", s->tex[i].w, s->tex[i].h);
        for (k = 0; k < npx; k++) {
            fwrite(s->tex[i].rgba + k * 4u, 1, 3, f);
        }
        fclose(f);
        snprintf(path, sizeof(path), "%s/g3d_%llu_tex%u_alpha.pgm", dir,
                 (unsigned long long)n, i);
        f = fopen(path, "wb");
        if (f == NULL) {
            continue;
        }
        fprintf(f, "P5 %u %u 255 ", s->tex[i].w, s->tex[i].h);
        for (k = 0; k < npx; k++) {
            fputc(s->tex[i].rgba[k * 4u + 3u], f);
        }
        fclose(f);
    }
}

/* Both screens as the hardware state has them now, saved for frame `n`. */
static void boot_screenshot(BootRun *run, u64 n)
{
    u32 unsupported[2];
    char path[1024];

    boot_ensure_screens(run);
    boot_dump_g3d(n);
    boot_dump_io(n);
    {
        const u8 *gpu3 = boot_gpu_frame(run);

        if (gpu3 != NULL) {
            /* The GPU drew the 3D; its DS-size frame is the layer. */
            (void)g2d_hw_render_dsmem(gpu3, run->top, run->bottom, unsupported);
        } else if (g3d_hw_frame_scale() > 1u) {
            /* A window runs the 3D at n times the DS size: render at n, then
             * box-downsample to the 256x192 the PNG writer takes. */
            u32 n = g3d_hw_frame_scale();
            size_t bytes = (size_t)256 * n * 192 * n * 4;
            u8 *a = (u8 *)calloc(2, bytes);

            if (a == NULL) {
                bw_panic("eidos_boot", 0, "out of memory for the screenshot");
            }
            (void)g2d_hw_render_dsmem_scaled(n, g3d_hw_frame(), a, a + bytes,
                                             unsupported);
            g2d_hw_box_downsample(a, n, run->top);
            g2d_hw_box_downsample(a + bytes, n, run->bottom);
            free(a);
        } else {
            (void)g2d_hw_render_dsmem(g3d_hw_frame(), run->top, run->bottom,
                                      unsupported);
        }
    }
    snprintf(path, sizeof(path), "%s/eidos_boot_frame%llu.png", g_shot_dir,
             (unsigned long long)n);
    if (write_screens_png(path, run->top, run->bottom) == 0) {
        printf("eidos_boot: frame %llu saved to %s\n", (unsigned long long)n, path);
    } else {
        fprintf(stderr, "eidos_boot: cannot write %s\n", path);
    }
#if EIDOS_BOOT_SDL
    /* With a window, also the picture as the window shows it, at its own
     * resolution (the GPU's 3D at the window's scale), as a BMP. */
    if (run->plat != NULL) {
        snprintf(path, sizeof(path), "%s/eidos_boot_frame%llu_window.bmp",
                 g_shot_dir, (unsigned long long)n);
        if (eidos_platform_screenshot(run->plat, path) == 0) {
            printf("eidos_boot: frame %llu window saved to %s\n",
                   (unsigned long long)n, path);
        }
    }
#endif
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
    io_raw_write(REG_VCOUNT, 2, VCOUNT_VBLANK_LINE);
    bw_io_sync();
    if (dispstat & DISPSTAT_VBLANK_IRQ) {
        bw_os_raise_irq(OS_IE_V_BLANK);
        run->vblanks++;
    }
}

static u64 g_render_ns;   /* this frame's 3D + 2D time, for the profiler */

#if EIDOS_BOOT_SDL
/*
 * The GPU 3D layer for this frame, when the main engine shows one and the
 * snapshot is something the GPU path can draw.  The screen composed with the
 * 3D layer off goes to the presenter beside the one the CPU composed with its
 * own 1x 3D; the presenter shows the GPU's frame where the two differ.
 */
static void boot_gpu3d(BootRun *run, int behind_done)
{
    const G3dSnapshot *snap;
    u32 dispcnt, bright;
    int main_on_top, screen;
    u32 bmode;

    run->gpu3d_ok = 0;
    if (run->plat == NULL || !eidos_platform_gpu3d(run->plat)) {
        return;
    }
    snap = g3d_hw_snapshot();
    dispcnt = bw_io_read32(0x04000000u);
    if (snap == NULL || snap->unsupported != 0u || (dispcnt & 0x8u) == 0u
        || (dispcnt & 0x100u) == 0u || ((dispcnt >> 16) & 3u) != 1u) {
        (void)eidos_platform_set_3d(run->plat, -1, NULL, NULL, 1, 0, 0.f, 0);
        run->gpu3d_ok = snap != NULL && snap->unsupported == 0u;
        return;
    }
    if (!behind_done || run->behind == NULL) {
        return;     /* the compositor did not make the 3D-off screen */
    }
    main_on_top = (bw_io_read16(0x04000304u) & 0x8000u) != 0;
    screen = main_on_top ? 0 : 1;
    bright = bw_io_read16(0x0400006Cu);
    bmode = (bright >> 14) & 3u;
    {
        u32 f = bright & 0x1Fu;

        if (f > 16u) {
            f = 16u;
        }
        /* What 2D the game blends over the 3D (the battle's message box):
         * the screen with an opaque black 3D layer, beside the one with the
         * sentinel.  Only when the screen was composed with the sentinel. */
        const u8 *zero = NULL;

        if (!run->gpu_real) {
            if (run->black3d == NULL) {
                u32 k;

                run->black3d = (u8 *)calloc(1, (size_t)256 * 192 * 4);
                for (k = 0; run->black3d != NULL && k < 256u * 192u; k++) {
                    run->black3d[k * 4u + 3] = 255;
                }
                run->zero = (u8 *)calloc(1, (size_t)256 * 192 * 4);
            }
            if (run->black3d != NULL && run->zero != NULL
                && g2d_hw_main_graphics_dsmem(run->black3d, run->zero) == 0) {
                zero = run->zero;
            }
        }
        eidos_platform_set_3d_zero(run->plat, zero);
        if (eidos_platform_set_3d(run->plat, screen, run->behind, snap,
                                  eidos_settings_internal_scale(),
                                  bmode == 1u || bmode == 2u ? (int)bmode : 0,
                                  (float)f / 16.f, run->gpu_real) == 0) {
            run->gpu3d_ok = 1;
        }
    }
}

/* The 256 x 192 BGR555 picture at the start of a VRAM bank, hashed. */
static u32 boot_bank_hash(const u8 *bank)
{
    const u32 *w = (const u32 *)(const void *)bank;
    u32 h = 2166136261u, i;

    for (i = 0; i < 256u * 192u / 2u; i++) {
        h = (h ^ w[i]) * 16777619u;
    }
    return h;
}

static u8 *boot_sentinel(BootRun *run)
{
    if (run->sentinel == NULL) {
        u32 k;

        run->sentinel = (u8 *)malloc((size_t)256 * 192 * 4);
        for (k = 0; run->sentinel != NULL && k < 256u * 192u; k++) {
            run->sentinel[k * 4u + 0] = 84;
            run->sentinel[k * 4u + 1] = 168;
            run->sentinel[k * 4u + 2] = 52;
            run->sentinel[k * 4u + 3] = 255;
        }
    }
    return run->sentinel;
}

/*
 * The display capture at the 3D's resolution (the GPU path).  A scene that
 * captures the main screen into a VRAM bank every frame and shows that bank
 * (the title: a feedback blend with the frame before) is DS size by nature.
 * The DS-size capture still runs for the game's memory; when the fields are
 * the plain ones (the whole screen, the graphics as source A, the shown bank
 * as source B) the presenter keeps the same capture at the chosen scale and
 * shows that instead.  boot_capture_plan runs before the frame's capture
 * (it needs the bank as it was), boot_capture_run after it.
 */
typedef struct BootCapture {
    int plan, blend, bank, eva, evb;
    const u8 *bankp;
    const u8 *b1x;
} BootCapture;

static void boot_capture_plan(BootRun *run, BootCapture *c)
{
    u32 cap = bw_io_read32(G2D_HW_DISPCAPCNT);
    u32 dc = bw_io_read32(0x04000000u);
    u32 mode = (cap >> 29) & 3u;
    const G3dSnapshot *snap;

    memset(c, 0, sizeof(*c));
    c->bank = (int)((dc >> 18) & 3u);
    c->bankp = (const u8 *)bw_ds_try_ptr(0x06800000u + (u32)c->bank * 0x20000u);
    if (run->plat == NULL || !eidos_platform_gpu3d(run->plat)
        || eidos_settings_internal_scale() < 2 || c->bankp == NULL) {
        return;
    }
    snap = g3d_hw_snapshot();
    if (snap == NULL || snap->unsupported != 0u
        || (cap & 0x80000000u) == 0u          /* no capture this frame */
        || ((cap >> 20) & 3u) != 3u           /* not the whole screen */
        || ((cap >> 18) & 3u) != 0u           /* a write offset */
        || (cap & 0x01000000u) != 0u          /* source A is the 3D alone */
        || (dc & 0x8u) == 0u || (dc & 0x100u) == 0u
        || ((dc >> 16) & 3u) != 2u            /* the screen does not show a bank */
        || (u32)c->bank != ((cap >> 16) & 3u)) {
        return;
    }
    if (mode == 1u || (mode >= 2u && ((cap & 0x02000000u) != 0u
                                      || ((cap >> 26) & 3u) != 0u))) {
        return;
    }
    if (run->cap_a == NULL) {
        run->cap_a = (u8 *)malloc((size_t)256 * 192 * 4);
        run->cap_behind = (u8 *)malloc((size_t)256 * 192 * 4);
        run->cap_b = (u8 *)malloc((size_t)256 * 192 * 4);
    }
    if (run->cap_a == NULL || run->cap_behind == NULL || run->cap_b == NULL
        || boot_sentinel(run) == NULL) {
        return;
    }
    c->blend = mode != 0u;
    c->eva = (int)((cap & 0x1Fu) > 16u ? 16u : (cap & 0x1Fu));
    c->evb = (int)(((cap >> 8) & 0x1Fu) > 16u ? 16u : ((cap >> 8) & 0x1Fu));
    if (c->blend && !(run->cap_ok && run->cap_bank == c->bank
                      && boot_bank_hash(c->bankp) == run->cap_hash)) {
        /* the GPU has no copy of what the bank holds: give it the bank */
        u32 k;

        for (k = 0; k < 256u * 192u; k++) {
            u32 v = (u32)c->bankp[k * 2u] | ((u32)c->bankp[k * 2u + 1u] << 8);

            run->cap_b[k * 4u + 0] = (u8)((v & 31u) * 255u / 31u);
            run->cap_b[k * 4u + 1] = (u8)(((v >> 5) & 31u) * 255u / 31u);
            run->cap_b[k * 4u + 2] = (u8)(((v >> 10) & 31u) * 255u / 31u);
            run->cap_b[k * 4u + 3] = 255;
        }
        c->b1x = run->cap_b;
    }
    if (g2d_hw_main_graphics_pair_dsmem(run->sentinel, run->cap_a,
                                        run->cap_behind) == 0) {
        c->plan = 1;
    }
}

static void boot_capture_run(BootRun *run, const BootCapture *c)
{
    u32 dc = bw_io_read32(0x04000000u);
    u32 bright = bw_io_read16(0x0400006Cu), bmode = (bright >> 14) & 3u;
    u32 f = bright & 0x1Fu;
    int main_on_top = (bw_io_read16(0x04000304u) & 0x8000u) != 0;
    int show = 0;

    if (run->plat == NULL || !eidos_platform_gpu3d(run->plat)) {
        return;
    }
    if (c->plan) {
        if (eidos_platform_capture_hires(run->plat, run->cap_a, run->cap_behind,
                                         g3d_hw_snapshot(),
                                         eidos_settings_internal_scale(),
                                         c->blend, c->eva, c->evb, c->b1x) == 0) {
            run->cap_ok = 1;
            run->cap_bank = c->bank;
            run->cap_hash = boot_bank_hash(c->bankp);
            show = 1;
        } else {
            run->cap_ok = 0;
        }
    } else if (run->cap_ok && c->bankp != NULL && ((dc >> 16) & 3u) == 2u
               && c->bank == run->cap_bank) {
        /* no capture this frame: the copy stands while the bank is as it was */
        if (boot_bank_hash(c->bankp) == run->cap_hash) {
            show = 1;
        } else {
            run->cap_ok = 0;
        }
    }
    if (f > 16u) {
        f = 16u;
    }
    eidos_platform_show_capture(run->plat, show ? (main_on_top ? 0 : 1) : -1,
                                bmode == 1u || bmode == 2u ? (int)bmode : 0,
                                (float)f / 16.f);
}

static void boot_render(void *user, u8 *top, u8 *bottom)
{
    u32 unsupported[2];
    u64 t0 = eidos_prof_begin();
    BootRun *run = (BootRun *)user;
    const u8 *rgba3d;
    u32 n;

    (void)top;
    (void)bottom;
#if EIDOS_BOOT_SDL
    if (run->ff_skip) {
        return;     /* fast forward: this frame is not shown (boot_frame) */
    }
#endif
    /*
     * The 3D renders at the scale set before its swap, so a change shows one
     * frame late.  With the GPU rasteriser the CPU renders the 3D at 1x only
     * (the 2D layer priorities and the capture need it) and the GPU draws the
     * same polygons at the chosen resolution.  Without it, or for a frame that
     * uses a feature the GPU path lacks, the CPU rasteriser draws the frame at
     * the chosen scale, held at BOOT_CPU_3D_MAX: its cost grows with n
     * squared (about 360 ms a frame at 6x).
     */
    {
        u32 want = (u32)eidos_settings_internal_scale();
        int gpu = run->plat != NULL && eidos_platform_gpu3d(run->plat);
        int skip = gpu && run->gpu3d_ok;

        g3d_hw_snapshot_enable(gpu);
        g3d_hw_skip_raster(skip);
        if (skip) {
            g3d_hw_set_scale(1u);
        } else {
            g3d_hw_set_scale(want > BOOT_CPU_3D_MAX ? BOOT_CPU_3D_MAX : want);
        }
    }
    rgba3d = g3d_hw_frame();
    n = g3d_hw_frame_scale();
    run->frame1x_valid = 0;
    run->gpu_real = 0;
    if (run->plat != NULL && eidos_platform_gpu3d(run->plat)) {
        const G3dSnapshot *snap = g3d_hw_snapshot();

        if (snap != NULL && snap->unsupported == 0u) {
            u32 dispcnt = bw_io_read32(0x04000000u);
            u32 cap = bw_io_read32(G2D_HW_DISPCAPCNT);
            int main3d = (dispcnt & 0x8u) && (dispcnt & 0x100u);
            int graphics = ((dispcnt >> 16) & 3u) == 1u;

            if (run->frame1x == NULL) {
                run->frame1x = (u8 *)calloc(1, (size_t)256 * 192 * 4);
            }
            if (cap & 0x80000000u) {
                /* The display capture reads the 3D layer: it needs the real
                 * frame at DS size.  When that frame only feeds the capture
                 * (the screen shows VRAM) the read does not wait for the GPU
                 * and arrives a frame late. */
                if (run->frame1x != NULL
                    && eidos_platform_read3d_1x(run->plat, snap, run->frame1x,
                                                !(main3d && graphics)) >= 0) {
                    rgba3d = run->frame1x;
                    n = 1u;
                    run->frame1x_valid = 1;
                    run->gpu_real = 1;
                }
            } else {
                /* The GPU draws the 3D layer itself; the 2D compositor only
                 * needs to know where it shows, so its 3D layer is one
                 * opaque unlikely colour. */
                if (boot_sentinel(run) != NULL) {
                    rgba3d = run->sentinel;
                    n = 1u;
                    run->frame1x_valid = 1;
                }
            }
        }
    }
    eidos_prof_end(EIDOS_PROF_G3D, t0);
    if (run->hi_n != n) {
        size_t bytes = (size_t)256 * n * 192 * n * 4;

        free(run->hi_top);
        free(run->hi_bottom);
        run->hi_top = (u8 *)calloc(1, bytes);
        run->hi_bottom = (u8 *)calloc(1, bytes);
        if (run->hi_top == NULL || run->hi_bottom == NULL) {
            bw_panic("eidos_boot", 0, "out of memory for the screens");
        }
        run->hi_n = n;
    }
    run->hi_sn[0] = run->hi_sn[1] = 1u;
    if (run->behind == NULL && run->plat != NULL && eidos_platform_gpu3d(run->plat)) {
        run->behind = (u8 *)calloc(1, (size_t)256 * 192 * 4);
    }
    {
        int behind_done = 0;
        BootCapture capt;

        boot_capture_plan(run, &capt);
        (void)g2d_hw_render_dsmem_adaptive_ex(
            n, rgba3d, run->hi_top, run->hi_bottom, unsupported, run->hi_sn,
            run->frame1x_valid ? run->behind : NULL, &behind_done);
        {
            unsigned long long t3 = eidos_prof_begin();

            boot_gpu3d(run, behind_done);
            boot_capture_run(run, &capt);
            eidos_prof_end(EIDOS_PROF_BOOT3D, t3);
        }
    }
    g_render_ns = eidos_prof_on ? bw_clock_ns() - t0 : 0;
    eidos_prof_end(EIDOS_PROF_RENDER, t0);
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

/*
 * Fast forward's sound: every frame's share is rendered, so the music and
 * the effects advance with the game, and queued while the device holds less
 * than the latency.  The device plays without a gap; what it cannot take in
 * real time is dropped.
 */
static void boot_audio_fast(BootRun *run)
{
    static s16 buf[2u * 1024u];
    u32 n = boot_audio_frames(run);

    if (n > 1024u) {
        n = 1024u;
    }
    bw_snd_render(buf, n);
    if (eidos_platform_audio_queued(run->plat) < AUDIO_LATENCY_FRAMES) {
        eidos_platform_queue_audio(run->plat, buf, n);
    }
}
#endif

/* The halt hook: one frame of time passing. */
static void boot_frame(void *user)
{
    BootRun *run = (BootRun *)user;
    int audio_device = 0;
    int speed = 1;   /* fast forward factor (Tab), eidos_platform_fast_forward */
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
    io_raw_write(REG_VCOUNT, 2, 0);

    /* The input of this frame, before its vblank (sdk/input.h): the
     * window's keys and the --press / --input script, KEYINPUT and the
     * ARM7's XY halfword written, the touch panel's automatic samples. */
    t_step = eidos_prof_begin();
    g_render_ns = 0;
    bw_input_frame();
    if (g_trace_on) {
        g3d_hw_trace(eidos_frame_count(&run->loop) + 1u == g_trace_frame);
    }
#if EIDOS_BOOT_SDL
    /* Fast forward (Tab): one frame in `speed` is composed and shown, the
     * rest neither, so the game's own work is most of what runs; composing a
     * frame costs more than running one, more so at higher resolutions.  The
     * 3D engine still works out every frame: a swap shows what was sent the
     * frame before, so a frame it skipped would show stale or empty.  A frame is
     * shown sooner when 100 ms have passed, so the window never stalls.  Not
     * while the display capture is on: it reads the drawn frame back into
     * VRAM for the game. */
    {
        int ff = run->plat != NULL ? eidos_platform_fast_forward(run->plat) : 1;

        run->ff_skip = ff > 1 && run->ff_since + 1u < (u32)ff
            && eidos_platform_now_ns(run->plat) - run->ff_shown_ns < 100000000ull
            && (bw_io_read32(0x04000064u) & 0x80000000u) == 0u;
        run->ff_since = run->ff_skip ? run->ff_since + 1u : 0u;
    }
#endif
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
        speed = eidos_platform_fast_forward(run->plat);
        t_ph = eidos_prof_begin();
        /* Badly behind (two frames or more): skip showing this frame, at
         * most three in a row; the game's steps are never skipped.  Fast
         * forward (Tab) shows a frame whenever a display period has passed
         * since the last one, so the window keeps its frame rate whatever
         * speed the host reaches. */
        if (run->ff_skip) {
            run->skipped = 0;
        } else if (eidos_platform_lag_frames(run->plat) >= 2u && run->skipped < 3u) {
            run->skipped++;
        } else {
            run->skipped = 0;
            run->ff_shown_ns = eidos_platform_now_ns(run->plat);
            if (run->hi_n != 0u) {
                eidos_platform_present_nn(run->plat, run->hi_top,
                                          (int)run->hi_sn[0], run->hi_bottom,
                                          (int)run->hi_sn[1]);
            }
        }
        eidos_prof_end(EIDOS_PROF_PRESENT, t_ph);
        audio_device = eidos_platform_audio_open(run->plat);
        t_ph = eidos_prof_begin();
        if (audio_device && speed > 1) {
            boot_audio_fast(run);
        } else if (audio_device) {
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
    if (getenv("EIDOS_G3D_DUMP") != NULL) {
        g3d_hw_snapshot_enable(1);
    }
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
    /* The mods: the loader runs after the save is chosen and before the game
     * runs (docs/mod-implementation.md, section 3).  The declared table the
     * manifest's address check asks is registered by bw_boot_link_init, so
     * the loader comes after it.  With no mod root there is nothing to
     * load. */
    {
        int loaded = bw_mod_loader_load_all();

        if (loaded > 0) {
            printf("eidos_boot: %d mod(s) loaded\n", loaded);
            fflush(stdout);
        }
    }
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
                    "[--screenshot-dir DIR] [--g3d-trace N] [--keep-going] [--press BUTTONS@FRAME[:HOLD]] "
                    "[--input SCRIPT] [--fullscreen|--borderless|--windowed] "
                    "[--resolution N|fit] "
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
#if EIDOS_BOOT
    bw_plat_crash_code_resolver(boot_resolve_code);
#endif
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
        } else if (strcmp(argv[i], "--no-hook") == 0 && i + 1 < argc) {
            bw_hook_set(argv[++i], 0);
        } else if (strcmp(argv[i], "--keep-going") == 0) {
            /* A diagnostic run: every stub on the way is reported once and
             * returns instead of stopping the run (sdk/port.h). */
            bw_set_keep_going(1);
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

    /* Started with no arguments (a double click): the player wants to
     * play, so the window opens.  Tests and tools always pass arguments. */
    if (argc == 1) {
        window = 1;
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
