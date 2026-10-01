/*
 * demo/eidos_boot.c -- run the game's own ARM9 boot on the host.
 *
 * docs/boot.md is the plan.  This executable is its driver: it resets the DS
 * address space, points the file layer at the player's extract/, and runs
 * NitroMain (stubs/reimpl_02005124.c) under the frame loop in engine/frame/,
 * headless, for a fixed number of frames.  It has no window: the picture is
 * the render lane's business, and a run that reaches its last frame without
 * a panic is the result this step of the work is after.
 *
 *     eidos_boot [--data-root PATH] [--frames N]
 *
 * Two builds, chosen by the CMake option EIDOS_BOOT:
 *
 *   OFF (the default)  the boot is not linked.  The game's boot reaches
 *                      about 1400 matched translation units and, through
 *                      them, hundreds of SDK functions whose port shims the
 *                      other boot lanes are writing (sdk/dsmem.c, the OS
 *                      thread and interrupt layer, the ARM7 replies, the
 *                      overlay loader).  Until they land, the link cannot
 *                      succeed, so this build compiles only the parts that
 *                      link today: argument handling, the data root and the
 *                      frame loop.  It says so and exits 0, which keeps this
 *                      file compiling at both pointer widths in every build.
 *
 *   ON                 the boot is linked: stubs/reimpl_02005124.c,
 *                      stubs/reimpl_02047990.c and the closure of
 *                      NitroMain's matched callees (CMakeLists.txt,
 *                      "eidos_boot").  The frame loop's step hook is
 *                      bw_nitromain_step (one iteration of NitroMain's loop,
 *                      ending where the original waits for vblank) and its
 *                      vblank hook raises the vblank interrupt, which runs
 *                      the handler NitroMain installed (func_0200520C) and
 *                      so satisfies the wait the next step tests.
 *
 * The vblank delivery here is the hardware's half only: set DISPSTAT's
 * vblank flag, raise IF bit 0 when DISPSTAT asks for the interrupt, and, if
 * IME and IE let it through, acknowledge IF and call the IRQ table entry
 * for bit 0 (OS_IRQTable, 0x02FE0020, which the matched OS_SetIrqFunction
 * fills).  That is what OS_IrqHandler does for one source.  The thread
 * wake-ups that follow an interrupt on the DS (OSi_IrqThreadQueue) are the
 * OS lane's dispatcher; when sdk/os_irq.c provides one, deliver_vblank
 * should call it instead of the table entry.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame.h"
#include "port.h"

#ifndef EIDOS_DEFAULT_DATA_ROOT
#define EIDOS_DEFAULT_DATA_ROOT ""
#endif

#ifndef EIDOS_BOOT
#define EIDOS_BOOT 0
#endif

#define BOOT_DEFAULT_FRAMES 600u

#if EIDOS_BOOT

#include "dsmem.h"

/* stubs/reimpl_02005124.c */
void bw_nitromain_init(void);
int  bw_nitromain_step(void);

/* OS_IRQTable in DTCM, written by the matched OS_SetIrqFunction. */
extern void (*g_02FE0020[])(void);

#define REG_DISPSTAT 0x04000004u
#define REG_IME      0x04000208u
#define REG_IE       0x04000210u
#define REG_IF       0x04000214u

#define DISPSTAT_VBLANK_FLAG 0x0001u
#define DISPSTAT_VBLANK_IRQ  0x0008u
#define IRQ_V_BLANK          0x00000001u

/*
 * The register file is plain memory (sdk/dsmem.h): the hardware's own
 * updates are made on its bytes directly, so no write hook runs for them
 * (IF's write hook is the CPU-side write-one-to-clear, which is not what
 * the hardware raising a bit does).
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

typedef struct BootState {
    u64 steps_run;     /* steps that ran a frame of NitroMain's loop       */
    u64 steps_waited;  /* steps that found the vblank wait unsatisfied     */
    u64 vblanks;       /* vblank interrupts delivered to the IRQ table     */
} BootState;

static void boot_step(void *user)
{
    BootState *st = (BootState *)user;

    if (bw_nitromain_step()) {
        st->steps_run++;
    } else {
        st->steps_waited++;
    }
}

static void deliver_vblank(void *user)
{
    BootState *st = (BootState *)user;
    u32 dispstat = io_raw_read(REG_DISPSTAT, 2);

    io_raw_write(REG_DISPSTAT, 2, dispstat | DISPSTAT_VBLANK_FLAG);
    if (dispstat & DISPSTAT_VBLANK_IRQ) {
        io_raw_write(REG_IF, 4, io_raw_read(REG_IF, 4) | IRQ_V_BLANK);
    }
    bw_io_sync();

    if ((io_raw_read(REG_IME, 2) & 1u) != 0
        && (io_raw_read(REG_IE, 4) & io_raw_read(REG_IF, 4) & IRQ_V_BLANK) != 0) {
        io_raw_write(REG_IF, 4, io_raw_read(REG_IF, 4) & ~IRQ_V_BLANK);
        if (g_02FE0020[0] != NULL) {
            g_02FE0020[0]();
            st->vblanks++;
        }
    }

    /* The flag drops again at line 0 of the next frame. */
    io_raw_write(REG_DISPSTAT, 2,
                 io_raw_read(REG_DISPSTAT, 2) & ~DISPSTAT_VBLANK_FLAG);
}

static int run_boot(u32 frames)
{
    BootState st;
    EidosFrameHooks hooks;
    EidosFrameLoop loop;
    u32 i;

    memset(&st, 0, sizeof st);
    memset(&hooks, 0, sizeof hooks);
    hooks.step = boot_step;
    hooks.vblank = deliver_vblank;
    hooks.render = NULL;
    hooks.user = &st;

    bw_ds_reset();
    eidos_frame_init(&loop, &hooks, NULL, NULL);
    eidos_frame_set_current(&loop);

    bw_nitromain_init();
    for (i = 0; i < frames; i++) {
        eidos_frame_run_one(&loop);
    }

    printf("eidos_boot: %u frames, %llu loop iterations, %llu waits, "
           "%llu vblank interrupts\n",
           (unsigned)frames, (unsigned long long)st.steps_run,
           (unsigned long long)st.steps_waited,
           (unsigned long long)st.vblanks);
    return 0;
}

#endif /* EIDOS_BOOT */

static void usage(void)
{
    fprintf(stderr, "usage: eidos_boot [--data-root PATH] [--frames N]\n");
}

int main(int argc, char **argv)
{
    const char *data_root = NULL;
    unsigned long long frames = BOOT_DEFAULT_FRAMES;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data-root") == 0 && i + 1 < argc) {
            data_root = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char *end = NULL;
            frames = strtoull(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || frames > 0xFFFFFFFFull) {
                usage();
                return 2;
            }
        } else {
            usage();
            return 2;
        }
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
    return run_boot((u32)frames);
#else
    printf("eidos_boot: built with EIDOS_BOOT=OFF, so the game's boot is not "
           "linked into this binary (%u frames requested, data root '%s'). "
           "Reconfigure with -DEIDOS_BOOT=ON once the boot lanes have "
           "landed; docs/boot.md has the plan.\n",
           (unsigned)frames, bw_data_root());
    return 0;
#endif
}
