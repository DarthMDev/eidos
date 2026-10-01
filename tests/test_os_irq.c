/*
 * tests/test_os_irq.c -- the interrupt controller and handler, synthetically.
 *
 * The IRQ table is filled by aletheia's own matched OS_SetIrqFunction.c and
 * read back by its OS_GetIrqFunction.c (compiled into this test as written),
 * so the checks also show that the shim's tables have the layout the
 * matched writers were compiled against.  What is checked:
 *
 *   - an interrupt is taken only with IME on, the CPSR I bit clear and its
 *     IE bit set, and is taken at the safe point where that becomes true,
 *   - IF is acknowledged when taken; a request that cannot be taken stays,
 *   - several pending requests run lowest bit first,
 *   - the timer/DMA callback slots are one-shot and set the check flag,
 *   - a game store to IF acknowledges (the write hook), and
 *     OS_ResetRequestIrqMask acknowledges and reports,
 *   - handlers run in IRQ mode with interrupts masked,
 *   - OS_WaitVBlankIntr sleeps the launcher until the frame loop's vblank,
 *     and a vblank raised from another host thread wakes a halted CPU.
 */

#include <stdio.h>
#include <string.h>

#include "os_test.h"
#include "plat_thread.h"
#include "port_test.h"

static char g_log[64];
static int  g_nlog;
static u32  g_mode_in_handler;
static u32  g_cpsr_in_handler;

static void logc(char c)
{
    if (g_nlog + 1 < (int)sizeof(g_log)) {
        g_log[g_nlog++] = c;
        g_log[g_nlog] = '\0';
    }
}

static void vblank_handler(void)
{
    logc('V');
    g_mode_in_handler = OS_GetProcMode();
    g_cpsr_in_handler = OS_DisableInterrupts();
    (void)OS_RestoreInterrupts(g_cpsr_in_handler);
    *bw_os_irq_check_flag() |= OS_IE_V_BLANK;   /* OS_SetIrqCheckFlag */
}

static void hblank_handler(void) { logc('H'); }
static void vcount_handler(void) { logc('C'); }
static void timer2_handler(void)       { logc('T'); }

static void reset_log(void)
{
    g_nlog = 0;
    g_log[0] = '\0';
}

static int test_gating(void)
{
    u32 e;

    os_test_boot();
    reset_log();
    OS_SetIrqFunction(OS_IE_V_BLANK, vblank_handler);
    BW_CHECK_EQ("table holds the handler", g_02FE0020[0] == vblank_handler, 1);
    BW_CHECK_EQ("OS_GetIrqFunction reads it back",
                OS_GetIrqFunction(OS_IE_V_BLANK) == vblank_handler, 1);
    bw_io_write32(BW_REG_IE, OS_IE_V_BLANK);

    e = OS_DisableInterrupts();
    bw_os_raise_irq(OS_IE_V_BLANK);
    bw_os_poll();
    BW_CHECK_EQ("CPSR I set: not taken", g_nlog, 0);
    BW_CHECK_EQ("still pending", bw_os_pending_irqs(), OS_IE_V_BLANK);
    (void)OS_RestoreInterrupts(e);
    BW_CHECK_EQ("taken when re-enabled", g_nlog, 1);
    BW_CHECK_EQ("acknowledged", bw_os_pending_irqs(), 0);
    BW_CHECK_EQ("IF register mirrors it", bw_io_read32(BW_REG_IF), 0);
    BW_CHECK_EQ("handler ran in IRQ mode", g_mode_in_handler, OS_PROCMODE_IRQ);
    BW_CHECK_EQ("with IRQs masked", g_cpsr_in_handler, OS_INTRMODE_IRQ_DISABLE);
    BW_CHECK_EQ("system mode afterwards", OS_GetProcMode(), OS_PROCMODE_SYS);
    BW_CHECK_EQ("check flag set by the handler", *bw_os_irq_check_flag() & 1u, 1);

    bw_io_write16(BW_REG_IME, 0);
    bw_os_raise_irq(OS_IE_V_BLANK);
    bw_os_poll();
    BW_CHECK_EQ("IME off: not taken", g_nlog, 1);
    BW_CHECK_EQ("IF register shows the request", bw_io_read32(BW_REG_IF), OS_IE_V_BLANK);
    bw_io_write16(BW_REG_IME, 1);
    bw_os_poll();
    BW_CHECK_EQ("IME on: taken", g_nlog, 2);

    bw_io_write32(BW_REG_IE, 0);
    bw_os_raise_irq(OS_IE_V_BLANK);
    bw_os_poll();
    BW_CHECK_EQ("IE clear: not taken", g_nlog, 2);
    bw_io_write32(BW_REG_IE, OS_IE_V_BLANK);
    bw_os_poll();
    BW_CHECK_EQ("IE set: taken", g_nlog, 3);
    return 0;
}

static int test_order_and_slots(void)
{
    os_test_boot();
    reset_log();
    OS_SetIrqFunction(OS_IE_V_BLANK, vblank_handler);
    OS_SetIrqFunction(OS_IE_H_BLANK, hblank_handler);
    OS_SetIrqFunction(OS_IE_V_COUNT, vcount_handler);
    bw_io_write32(BW_REG_IE, OS_IE_V_BLANK | OS_IE_H_BLANK | OS_IE_V_COUNT);

    bw_io_write16(BW_REG_IME, 0);
    bw_os_raise_irq(OS_IE_V_COUNT);
    bw_os_raise_irq(OS_IE_H_BLANK);
    bw_os_raise_irq(OS_IE_V_BLANK);
    bw_io_write16(BW_REG_IME, 1);
    bw_os_poll();
    BW_REQUIRE("three handlers ran", g_nlog == 3);
    BW_CHECK_EQ("lowest bit first", strcmp(g_log, "VHC"), 0);

    /* Timer 2 goes through callback slot 10, as OS_SetIrqFunction maps it. */
    reset_log();
    *bw_os_irq_check_flag() = 0;
    OS_SetIrqFunction(OS_IE_TIMER2, timer2_handler);
    BW_CHECK_EQ("timer 2 in callback slot 10",
                g_02150F38[10].func != NULL && g_02150F38[10].enable == 1, 1);
    BW_CHECK_EQ("OS_GetIrqFunction finds it there",
                OS_GetIrqFunction(OS_IE_TIMER2) == timer2_handler, 1);
    BW_CHECK_EQ("not in the table", g_02FE0020[5] == NULL, 1);
    bw_io_write32(BW_REG_IE, OS_IE_TIMER2);
    bw_os_raise_irq(OS_IE_TIMER2);
    bw_os_poll();
    BW_CHECK_EQ("callback ran", strcmp(g_log, "T"), 0);
    BW_CHECK_EQ("check flag set for the slot", *bw_os_irq_check_flag(), OS_IE_TIMER2);
    BW_CHECK_EQ("one-shot: slot emptied", g_02150F38[10].func == NULL, 1);
    bw_os_raise_irq(OS_IE_TIMER2);
    bw_os_poll();
    BW_CHECK_EQ("second request runs nothing", g_nlog, 1);
    return 0;
}

static int test_acknowledge(void)
{
    u32 prev;

    os_test_boot();
    bw_io_write16(BW_REG_IME, 0);
    bw_os_raise_irq(OS_IE_V_BLANK | OS_IE_H_BLANK);
    bw_os_poll();
    BW_CHECK_EQ("mirrored", bw_io_read32(BW_REG_IF), OS_IE_V_BLANK | OS_IE_H_BLANK);

    /* A matched `reg_OS_IF = OS_IE_H_BLANK` lands in the register file and
     * reaches the hook at the next sync. */
    {
        volatile u8 *r = bw_io(BW_REG_IF);
        r[0] = (u8)OS_IE_H_BLANK; r[1] = 0; r[2] = 0; r[3] = 0;
    }
    bw_io_sync();
    BW_CHECK_EQ("game store to IF acknowledges", bw_os_pending_irqs(), OS_IE_V_BLANK);
    BW_CHECK_EQ("register shows what is left", bw_io_read32(BW_REG_IF), OS_IE_V_BLANK);

    prev = OS_ResetRequestIrqMask(OS_IE_V_BLANK);
    BW_CHECK_EQ("OS_ResetRequestIrqMask returns IF", prev, OS_IE_V_BLANK);
    BW_CHECK_EQ("and clears it", bw_os_pending_irqs(), 0);
    BW_CHECK_EQ("IME restored", bw_io_read16(BW_REG_IME), 0);
    return 0;
}

/* ---- waiting for vblank, with threads ------------------------------ */

static int g_hook_calls;

static void vblank_frame_hook(void *user)
{
    (void)user;
    g_hook_calls++;
    eidos_frame_run_one(&g_os_test_loop);
    bw_os_raise_irq(OS_IE_V_BLANK);
}

static void raiser(void *arg)
{
    (void)arg;
    bw_os_raise_irq(OS_IE_V_BLANK);
}

static int test_wait_vblank(void)
{
    int i;
    BwThread *t;

    os_test_boot();
    reset_log();
    g_hook_calls = 0;
    OS_SetIrqFunction(OS_IE_V_BLANK, vblank_handler);
    bw_io_write32(BW_REG_IE, OS_IE_V_BLANK);
    bw_os_set_halt_hook(vblank_frame_hook, NULL);

    for (i = 0; i < 3; i++) {
        OS_WaitVBlankIntr();
    }
    BW_CHECK_EQ("three waits, three frames", g_hook_calls, 3);
    BW_CHECK_EQ("three vblank handlers", g_nlog, 3);
    BW_CHECK_EQ("frame loop advanced", (long)eidos_frame_count(&g_os_test_loop), 3);
    BW_CHECK_EQ("launcher runs again", OSi_ThreadInfo.current == &OSi_LauncherThread, 1);

    /* No hook: the idle thread's halt waits for another host thread. */
    bw_os_set_halt_hook(NULL, NULL);
    t = bw_thread_start(raiser, NULL);
    OS_WaitVBlankIntr();
    bw_thread_join(t);
    BW_CHECK_EQ("woken by a raise from another host thread", g_nlog, 4);

    /* OS_WaitInterrupt halts in place: the launcher itself halts. */
    bw_os_set_halt_hook(vblank_frame_hook, NULL);
    OS_WaitInterrupt(1, OS_IE_V_BLANK);
    BW_CHECK_EQ("OS_WaitInterrupt took one frame", g_hook_calls, 4);
    BW_CHECK_EQ("and cleared the flag after", *bw_os_irq_check_flag() & 1u, 0);
    bw_os_set_halt_hook(NULL, NULL);
    return 0;
}

int main(void)
{
    test_gating();
    test_order_and_slots();
    test_acknowledge();
    test_wait_vblank();
    bw_os_reset();
    BW_REPORT("test_os_irq");
}
