/*
 * tests/test_os_alarm.c -- alarms on the OS tick, synthetically.
 *
 * The tick is the frame loop's (8752 ticks and 62/64 a frame, see
 * engine/frame/frame.h), so every expectation is a frame number computed
 * from the fire time.  What is checked:
 *
 *   - a one-shot alarm fires at the first frame boundary at or after its
 *     fire time, once, in IRQ mode, through TIMER1,
 *   - alarms set out of order fire in fire order,
 *   - a cancelled alarm never fires, and cancelling the head re-arms the
 *     next,
 *   - a periodic alarm fires every period from its start,
 *   - with interrupts disabled an alarm waits for them to be enabled.
 */

#include <stdio.h>
#include <string.h>

#include "os_test.h"
#include "port_test.h"

#define TICKS_AFTER(n) ((u64)(n) * 560190u / 64u)   /* tick at frame n */

static char g_log[64];
static int  g_nlog;
static u64  g_fired_frame[8];
static u32  g_mode;

static void handler(void *arg)
{
    char c = *(const char *)arg;
    if (g_nlog + 1 < (int)sizeof(g_log)) {
        g_fired_frame[g_nlog & 7] = eidos_frame_count(&g_os_test_loop);
        g_log[g_nlog++] = c;
        g_log[g_nlog] = '\0';
    }
    g_mode = OS_GetProcMode();
}

static void reset_log(void)
{
    g_nlog = 0;
    g_log[0] = '\0';
}

/* One frame, then the safe point the game's next frame would reach. */
static void frame(void)
{
    eidos_frame_run_one(&g_os_test_loop);
    bw_os_poll();
}

static int test_one_shot(void)
{
    static OSAlarm a;
    static const char la = 'a';
    int i;

    os_test_boot();
    reset_log();
    BW_CHECK_EQ("tick starts at zero", (long)OS_GetTick(), 0);

    OS_CreateAlarm(&a);
    OS_SetAlarm(&a, 20000, handler, (void *)&la);
    BW_CHECK_EQ("TIMER1 enabled while armed", bw_io_read32(BW_REG_IE) & OS_IE_TIMER1,
                OS_IE_TIMER1);
    for (i = 0; i < 6; i++) {
        frame();
    }
    /* 20000 ticks: after frame 2 the tick is 17505, after frame 3 26258. */
    BW_CHECK_EQ("fired once", g_nlog, 1);
    BW_CHECK_EQ("at frame 3", (long)g_fired_frame[0], 3);
    BW_CHECK_EQ("in IRQ mode", g_mode, OS_PROCMODE_IRQ);
    BW_CHECK_EQ("one-shot: handler cleared", a.handler == NULL, 1);
    BW_CHECK_EQ("TIMER1 masked when idle", bw_io_read32(BW_REG_IE) & OS_IE_TIMER1, 0);
    BW_CHECK_EQ("timer check flag set", *bw_os_irq_check_flag() & OS_IE_TIMER1,
                OS_IE_TIMER1);
    return 0;
}

static int test_order_and_cancel(void)
{
    static OSAlarm a, b, c, d;
    static const char la = 'a', lb = 'b', lc = 'c', ld = 'd';
    int i;

    os_test_boot();
    reset_log();
    OS_CreateAlarm(&a);
    OS_CreateAlarm(&b);
    OS_CreateAlarm(&c);
    OS_CreateAlarm(&d);
    OS_SetAlarm(&a, TICKS_AFTER(4), handler, (void *)&la);
    OS_SetAlarm(&b, TICKS_AFTER(2), handler, (void *)&lb);
    OS_SetAlarm(&c, TICKS_AFTER(3), handler, (void *)&lc);
    OS_SetAlarm(&d, TICKS_AFTER(1), handler, (void *)&ld);
    OS_CancelAlarm(&d);              /* the head: b becomes it and re-arms */
    OS_CancelAlarm(&c);
    BW_CHECK_EQ("cancel clears the handler", c.handler == NULL, 1);
    for (i = 0; i < 6; i++) {
        frame();
    }
    BW_CHECK_EQ("fire order, cancelled ones absent", strcmp(g_log, "ba"), 0);
    BW_CHECK_EQ("b at frame 2", (long)g_fired_frame[0], 2);
    BW_CHECK_EQ("a at frame 4", (long)g_fired_frame[1], 4);
    return 0;
}

static int test_periodic(void)
{
    static OSAlarm p;
    static const char lp = 'p';
    int i;

    os_test_boot();
    reset_log();
    OS_CreateAlarm(&p);
    /* From frame 1's tick, every two frames' worth. */
    OS_SetPeriodicAlarm(&p, TICKS_AFTER(1), TICKS_AFTER(2), handler, (void *)&lp);
    for (i = 0; i < 8; i++) {
        frame();
    }
    BW_CHECK_EQ("four firings in eight frames", g_nlog, 4);
    BW_CHECK_EQ("first at frame 1", (long)g_fired_frame[0], 1);
    BW_CHECK_EQ("then frame 3", (long)g_fired_frame[1], 3);
    BW_CHECK_EQ("then frame 5", (long)g_fired_frame[2], 5);
    BW_CHECK_EQ("then frame 7", (long)g_fired_frame[3], 7);
    BW_CHECK_EQ("still set", p.handler == handler, 1);
    OS_CancelAlarm(&p);
    frame();
    frame();
    BW_CHECK_EQ("cancelled: no more", g_nlog, 4);
    return 0;
}

static int test_masked(void)
{
    static OSAlarm a;
    static const char la = 'm';
    u32 e;

    os_test_boot();
    reset_log();
    OS_CreateAlarm(&a);
    OS_SetAlarm(&a, 1, handler, (void *)&la);
    e = OS_DisableInterrupts();
    frame();
    frame();
    BW_CHECK_EQ("interrupts off: waits", g_nlog, 0);
    BW_CHECK_EQ("TIMER1 latched", bw_os_pending_irqs() & OS_IE_TIMER1, OS_IE_TIMER1);
    (void)OS_RestoreInterrupts(e);
    BW_CHECK_EQ("taken on enable", g_nlog, 1);
    return 0;
}

int main(void)
{
    test_one_shot();
    test_order_and_cancel();
    test_periodic();
    test_masked();
    bw_os_reset();
    BW_REPORT("test_os_alarm");
}
