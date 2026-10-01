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
 * The machine is reset with bw_port_reset (sdk/port.h): the address
 * space, the OS layer and the PXI, joined.  The vblank delivery here is the
 * hardware's half only: set DISPSTAT's vblank flag and, when DISPSTAT asks
 * for the interrupt, raise IF bit 0 through the OS layer
 * (bw_os_raise_irq), which owns IF.  The OS layer takes it at the safe
 * point that follows (bw_os_poll here, or the game's own interrupt
 * enables): IME, the CPSR I bit and IE decide, the handler NitroMain
 * installed in OS_IRQTable runs, and the threads waiting in OS_WaitIrq
 * wake, as OS_IrqHandler does on the DS.
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
#include "os.h"

/* stubs/reimpl_02005124.c */
void bw_nitromain_init(void);
int  bw_nitromain_step(void);

#define REG_DISPSTAT 0x04000004u

#define DISPSTAT_VBLANK_FLAG 0x0001u
#define DISPSTAT_VBLANK_IRQ  0x0008u

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

typedef struct BootState {
    u64 steps_run;     /* steps that ran a frame of NitroMain's loop       */
    u64 steps_waited;  /* steps that found the vblank wait unsatisfied     */
    u64 vblanks;       /* vblank interrupts raised                         */
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
    bw_io_sync();
    if (dispstat & DISPSTAT_VBLANK_IRQ) {
        bw_os_raise_irq(OS_IE_V_BLANK);
        st->vblanks++;
    }
    bw_os_poll();

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

    bw_port_reset();
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
