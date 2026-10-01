/*
 * sdk/os_alarm.c -- OS alarms (os_alarm.o) on the OS tick.
 *
 * On the DS an alarm is a sorted queue plus hardware timer 1: OSi_SetTimer
 * programs TM1 for the head's fire time and installs OSi_ArrangeTimer as the
 * timer 1 interrupt, which runs due handlers and re-arms.  The queue code is
 * EXACT C in aletheia (src/matched/arm9_sdk/func_02087208.c OS_InitAlarm,
 * OS_SetAlarm.c, OSi_InsertAlarm.c, func_020873F8.c OS_CancelAlarm,
 * func_0208748C.c OSi_ArrangeTimer) and is transcribed here rather than
 * compiled, because its other half is the timer and the tick: OS_GetTick is
 * already a shim onto the frame loop's clock (sdk/os_tick.c), there is no
 * TM1 to count down, and ArrangeTimer's check flag is reached through the
 * DTCM linker symbol (see sdk/os_interrupt.c).  The module's .bss
 * (g_02151340) is private to these functions, so the matched files of
 * os_alarm.o must stay out of the build together.
 *
 * The timer
 *
 *   "Armed" stands for TM1 running: set by OSi_SetTimer whenever the queue
 *   gets a new head, together with the TIMER1 callback slot and IE bit, as
 *   the SDK does.  bw_os_alarm_poll, called at every safe point and on every
 *   pass through OS_Halt, raises TIMER1 when the head is due by OS_GetTick.
 *   The interrupt is then taken like any other (IME, CPSR, IE), so a handler
 *   runs in IRQ mode on whichever thread holds the CPU, exactly as on the
 *   DS.  The resolution is the tick clock's: the frame loop advances it a
 *   frame at a time, so an alarm fires at the first frame boundary at or
 *   after its fire time.
 *
 * OS_SetPeriodicAlarm is not in this game's image (layout/symbols_sdk.txt
 * has no address for it); it is here because periodic alarms are part of
 * the module's contract and OSi_InsertAlarm already carries their
 * arithmetic.
 */

#include "dsmem.h"
#include "os.h"
#include "port.h"

u64  OS_GetTick(void);
u32  OS_DisableInterrupts(void);
u32  OS_RestoreInterrupts(u32 state);

typedef struct OSAlarmQueue {
    OSAlarm *head;
    OSAlarm *tail;
} OSAlarmQueue;

static u16          g_use_alarm;      /* OSi_UseAlarm   */
static OSAlarmQueue g_queue;          /* OSi_AlarmQueue */
static int          g_armed;          /* TM1 running    */

#define TIMER1_SLOT (4 - 3 + 8)       /* callback slot for IE bit 4 */

static void arrange_timer(void *unused);

static void ie_set(u32 bits, int on)
{
    u32 ie = bw_io_read32(BW_REG_IE);
    bw_io_write32(BW_REG_IE, on ? (ie | bits) : (ie & ~bits));
}

/* OSi_SetTimer (0x02087180): TM1 for `alarm`, TIMER1 handler and mask. */
static void set_timer(OSAlarm *alarm)
{
    (void)alarm;                       /* the poll reads the head's fire */
    g_armed = 1;
    g_02150F38[TIMER1_SLOT].func = arrange_timer;
    g_02150F38[TIMER1_SLOT].arg = NULL;
    g_02150F38[TIMER1_SLOT].enable = 1;
    ie_set(OS_IE_TIMER1, 1);
}

void bw_os_alarm_poll(void)
{
    if (g_use_alarm && g_armed && g_queue.head != NULL
        && OS_GetTick() >= g_queue.head->fire) {
        g_armed = 0;                   /* TM1 overflowed once */
        bw_os_raise_irq(OS_IE_TIMER1);
    }
}

/*
 * OSi_InsertAlarm.c.  `refire` is the one departure: when ArrangeTimer puts
 * a periodic alarm back, the SDK computes the next fire from "start < tick",
 * and on hardware the tick has always moved past the fire time by the time
 * the handler has run.  The frame-quantised tick can sit exactly on it, and
 * then the SDK's arithmetic would give the same fire time again and the
 * alarm would refire forever inside one interrupt; "<=" on that path is
 * what the hardware's strict inequality amounts to.
 */
static void insert_alarm(OSAlarm *alarm, u64 fire, int refire)
{
    OSAlarm *prev;
    OSAlarm *next;

    if (alarm->period > 0) {
        u64 tick = OS_GetTick();

        fire = alarm->start;
        if (alarm->start < tick || (refire && alarm->start == tick)) {
            fire += alarm->period * ((tick - alarm->start) / alarm->period + 1);
        }
    }
    alarm->fire = fire;

    for (next = g_queue.head; next != NULL; next = next->next) {
        if ((s64)(fire - next->fire) >= 0) {
            continue;
        }
        alarm->prev = next->prev;
        next->prev = alarm;
        alarm->next = next;
        prev = alarm->prev;
        if (prev != NULL) {
            prev->next = alarm;
        } else {
            g_queue.head = alarm;
            set_timer(alarm);
        }
        return;
    }
    alarm->next = NULL;
    prev = g_queue.tail;
    g_queue.tail = alarm;
    alarm->prev = prev;
    if (prev != NULL) {
        prev->next = alarm;
    } else {
        g_queue.head = g_queue.tail = alarm;
        set_timer(alarm);
    }
}

/* func_0208748C, OSi_ArrangeTimer: the TIMER1 interrupt. */
static void arrange_timer(void *unused)
{
    u64 tick;
    OSAlarm *alarm;
    OSAlarm *next;
    OSAlarmHandler handler;

    (void)unused;
    g_armed = 0;                       /* reg TM1CNT_H = 0 */
    ie_set(OS_IE_TIMER1, 0);
    *bw_os_irq_check_flag() |= OS_IE_TIMER1;

    tick = OS_GetTick();
    alarm = g_queue.head;
    if (alarm == NULL) {
        return;
    }
    if (tick < alarm->fire) {
        set_timer(alarm);
        return;
    }
    next = alarm->next;
    g_queue.head = next;
    if (next == NULL) {
        g_queue.tail = NULL;
    } else {
        next->prev = NULL;
    }
    handler = alarm->handler;
    if (!alarm->period) {
        alarm->handler = NULL;
    }
    if (handler != NULL) {
        handler(alarm->arg);
    }
    if (alarm->period) {
        alarm->handler = handler;
        insert_alarm(alarm, 0, 1);
    }
    if (g_queue.head != NULL) {
        set_timer(g_queue.head);
    }
}

/* func_02087208 */
void OS_InitAlarm(void)
{
    if (!g_use_alarm) {
        g_use_alarm = 1;
        /* OSi_SetTimerReserved(1): no other user of TM1 exists here. */
        g_queue.head = NULL;
        g_queue.tail = NULL;
        ie_set(OS_IE_TIMER1, 0);
    }
}

/* 0x02087244 */
int OS_IsAlarmAvailable(void)
{
    return g_use_alarm;
}

/* 0x02087254 */
void OS_CreateAlarm(OSAlarm *alarm)
{
    alarm->handler = NULL;
    alarm->tag = 0;
}

/* OS_SetAlarm.c.  The SDK stops the machine on a busy alarm; so does this,
 * with a message that says which. */
void OS_SetAlarm(OSAlarm *alarm, u64 tick, OSAlarmHandler handler, void *arg)
{
    u32 e;

    if (alarm == NULL || alarm->handler != NULL) {
        bw_panic("OS_SetAlarm", 0x0208738Cu, "alarm is NULL or already set");
    }
    e = OS_DisableInterrupts();
    alarm->period = 0;
    alarm->handler = handler;
    alarm->arg = arg;
    insert_alarm(alarm, OS_GetTick() + tick, 0);
    (void)OS_RestoreInterrupts(e);
}

void OS_SetPeriodicAlarm(OSAlarm *alarm, u64 start, u64 period,
                         OSAlarmHandler handler, void *arg)
{
    u32 e;

    if (alarm == NULL || alarm->handler != NULL || period == 0 || handler == NULL) {
        bw_panic("OS_SetPeriodicAlarm", 0, "alarm busy, or no period or handler");
    }
    e = OS_DisableInterrupts();
    alarm->period = period;
    alarm->start = start;
    alarm->handler = handler;
    alarm->arg = arg;
    insert_alarm(alarm, 0, 0);
    (void)OS_RestoreInterrupts(e);
}

/* func_020873F8 */
void OS_CancelAlarm(OSAlarm *alarm)
{
    OSAlarm *next;
    u32 e = OS_DisableInterrupts();

    if (alarm->handler == NULL) {
        (void)OS_RestoreInterrupts(e);
        return;
    }
    next = alarm->next;
    if (next == NULL) {
        g_queue.tail = alarm->prev;
    } else {
        next->prev = alarm->prev;
    }
    if (alarm->prev != NULL) {
        alarm->prev->next = next;
    } else {
        g_queue.head = next;
        if (next != NULL) {
            set_timer(next);
        }
    }
    alarm->handler = NULL;
    alarm->period = 0;
    (void)OS_RestoreInterrupts(e);
}

void bw_os_alarm_reset(void)
{
    g_use_alarm = 0;
    g_queue.head = NULL;
    g_queue.tail = NULL;
    g_armed = 0;
}

/* Address spellings (sdk/aliases.c says why a host link needs them). */
void func_02087208(void)                 { OS_InitAlarm(); }
int  func_02087244(void)                 { return OS_IsAlarmAvailable(); }
void func_02087254(OSAlarm *a)           { OS_CreateAlarm(a); }
void func_0208738C(OSAlarm *a, u64 t, OSAlarmHandler h, void *arg)
                                         { OS_SetAlarm(a, t, h, arg); }
void func_020873F8(OSAlarm *a)           { OS_CancelAlarm(a); }
