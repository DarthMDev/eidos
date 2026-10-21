/*
 * sdk/os.h -- the SDK OS objects the game's code reads, and the port's
 * handle on the OS layer.
 *
 * Two halves.  The first is the game's own declarations of the OS structs
 * (OSThread, OSThreadInfo, OSAlarm, the IRQ callback table), laid out as
 * the vendor objects' DWARF records them (aletheia's tools/sdkobj.py
 * OS_CreateThread --types and OS_SetAlarm --types) and as the matched
 * sources in aletheia's src/matched/arm9_sdk/ declare them.  Matched code
 * reads these fields directly (OS_GetCurrentThread is an inline read of
 * OSi_ThreadInfo.current; OS_SetIrqFunction writes the IRQ table and the
 * callback table), so the port's shims keep every field exactly as the SDK
 * would, and keep anything host-only (the host thread behind an OSThread,
 * its saved interrupt state) in side tables of their own.
 *
 * Scalar fields use `unsigned long` / `long`, not u32 / s32, because that is
 * how the matched SDK sources spell u32 and s32 (`typedef unsigned long
 * u32;`).  On every ILP32 target and on LLP64 Windows the two are the same
 * type; on an LP64 Unix they are not, and agreeing with the matched
 * declarations is what keeps the shims and the matched callers reading the
 * same offsets there.  On an ILP32 target every struct here is
 * byte-identical to the DS's, which the static checks below assert.
 *
 * The second half is the port's API: reset, the interrupt source, the
 * safe point, the halt hook.  sdk/os_thread.c, sdk/os_irq.c,
 * sdk/os_interrupt.c, sdk/os_alarm.c and sdk/os_tick.c implement it.
 */

#ifndef BW_PORT_OS_H
#define BW_PORT_OS_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long bw_sdk_u32;   /* the matched SDK sources' u32 */
typedef long          bw_sdk_s32;   /* the matched SDK sources' s32 */

/* ------------------------------------------------------------------ */
/* Threads (os_thread.o)                                               */
/* ------------------------------------------------------------------ */

typedef enum OSThreadState {
    OS_THREAD_STATE_WAITING    = 0,
    OS_THREAD_STATE_READY      = 1,
    OS_THREAD_STATE_TERMINATED = 2
} OSThreadState;

typedef struct CPContext {
    u8 opaque[0x1C];               /* the divider/sqrt unit's state */
} CPContext;

typedef struct OSContext {
    bw_sdk_u32 cpsr;
    bw_sdk_u32 r[13];
    bw_sdk_u32 sp;
    bw_sdk_u32 lr;
    bw_sdk_u32 pc_plus4;
    bw_sdk_u32 sp_svc;
    CPContext  cp_context;
} OSContext;

typedef struct _OSThread OSThread;
typedef struct OSMutex   OSMutex;
typedef struct OSiAlarm  OSAlarm;

typedef struct _OSThreadQueue {
    OSThread *head;
    OSThread *tail;
} OSThreadQueue;

typedef struct _OSThreadLink {
    OSThread *prev;
    OSThread *next;
} OSThreadLink;

typedef struct _OSMutexQueue {
    OSMutex *head;
    OSMutex *tail;
} OSMutexQueue;

typedef struct _OSMutexLink {
    OSMutex *next;
    OSMutex *prev;
} OSMutexLink;

struct OSMutex {
    OSThreadQueue queue;
    OSThread     *thread;
    bw_sdk_s32    count;
    OSMutexLink   link;
};

struct _OSThread {
    OSContext      context;            /* +0x00 */
    OSThreadState  state;              /* +0x64 */
    OSThread      *next;               /* +0x68, the priority-ordered list */
    bw_sdk_u32     id;                 /* +0x6C */
    bw_sdk_u32     priority;           /* +0x70, 0 is highest */
    void          *profiler;           /* +0x74 */
    OSThreadQueue *queue;              /* +0x78, the queue it sleeps on */
    OSThreadLink   link;               /* +0x7C */
    OSMutex       *mutex;              /* +0x84 */
    OSMutexQueue   mutexQueue;         /* +0x88 */
    bw_sdk_u32     stackTop;           /* +0x90 */
    bw_sdk_u32     stackBottom;        /* +0x94 */
    bw_sdk_u32     stackWarningOffset; /* +0x98 */
    OSThreadQueue  joinQueue;          /* +0x9C */
    void          *specific[3];        /* +0xA4 */
    OSAlarm       *alarmForSleep;      /* +0xB0 */
    void         (*destructor)(void *);/* +0xB4 */
    void          *userParameter;      /* +0xB8 */
    int            systemErrno;        /* +0xBC */
};

typedef struct OSThreadInfo {
    u16       isNeedRescheduling;
    u16       irqDepth;
    OSThread *current;
    OSThread *list;
    void     *switchCallback;
} OSThreadInfo;

typedef void (*OSSwitchThreadCallback)(OSThread *from, OSThread *to);
typedef void (*OSThreadDestructor)(void *arg);

#define OS_THREAD_LAUNCHER_PRIORITY 16u
#define OS_THREAD_PRIORITY_MAX      31u
#define OS_THREAD_IDLE_PRIORITY     32u   /* below every creatable thread */

extern OSThreadInfo OSi_ThreadInfo;   /* 0x02150FEC */
extern OSThread     OSi_LauncherThread;
extern OSThread     OSi_IdleThread;

/* ------------------------------------------------------------------ */
/* Alarms (os_alarm.o)                                                 */
/* ------------------------------------------------------------------ */

typedef void (*OSAlarmHandler)(void *arg);

struct OSiAlarm {
    OSAlarmHandler handler;
    void          *arg;
    bw_sdk_u32     tag;
    u64            fire;
    OSAlarm       *prev;
    OSAlarm       *next;
    u64            period;
    u64            start;
};

/* ------------------------------------------------------------------ */
/* Interrupts (os_interrupt.o, os_irqtable.o)                          */
/* ------------------------------------------------------------------ */

typedef void (*OSIrqFunction)(void);

typedef struct OSIrqCallbackInfo {
    void      (*func)(void *);
    bw_sdk_u32  enable;
    void       *arg;
} OSIrqCallbackInfo;

/*
 * OS_IRQTable (DTCM 0x02FE0020) and OSi_IrqCallbackInfo (0x02150F38), under
 * the names the matched OS_SetIrqFunction.c / OS_GetIrqFunction.c give
 * them.  Twelve callback slots: DMA0-3, NDMA0-3, timers 0-3.
 */
#define BW_OS_IRQ_TABLE_MAX   32
#define BW_OS_IRQ_CALLBACKS   12
extern OSIrqFunction     g_02FE0020[BW_OS_IRQ_TABLE_MAX];
extern OSIrqCallbackInfo g_02150F38[BW_OS_IRQ_CALLBACKS];

extern OSThreadQueue OSi_IrqThreadQueue;   /* 0x02FE00A0 */

#define OS_IE_V_BLANK     (1u << 0)
#define OS_IE_H_BLANK     (1u << 1)
#define OS_IE_V_COUNT     (1u << 2)
#define OS_IE_TIMER0      (1u << 3)
#define OS_IE_TIMER1      (1u << 4)
#define OS_IE_TIMER2      (1u << 5)
#define OS_IE_TIMER3      (1u << 6)
#define OS_IE_DMA0        (1u << 8)
#define OS_IE_KEY         (1u << 12)
#define OS_IE_FIFO_RECV   (1u << 18)

/* The CPU state OS_DisableInterrupts returns: the CPSR I and F bits.      */
#define OS_INTRMODE_IRQ_DISABLE 0x80u
#define OS_INTRMODE_FIQ_DISABLE 0x40u
#define OS_PROCMODE_IRQ         0x12u
#define OS_PROCMODE_SYS         0x1Fu

/* The registers, by DS address (they live in sdk/dsmem.h's register file). */
#define BW_REG_IME 0x04000208u
#define BW_REG_IE  0x04000210u
#define BW_REG_IF  0x04000214u

/* HW_INTR_CHECK_BUF: DTCM + 0x3FF8, the word OS_WaitIrq polls.            */
#define BW_DS_INTR_CHECK_BUF 0x02FE3FF8u

/* ------------------------------------------------------------------ */
/* The port's API                                                      */
/* ------------------------------------------------------------------ */

/*
 * Put the whole OS layer back to power-on: no threads but the caller (who
 * becomes OSi_LauncherThread's host thread once OS_InitThread runs), no
 * pending interrupts, no alarms, an empty IRQ table, IF's write-one-to-clear
 * hook installed on the register file.  Every other game thread's host
 * thread is stopped and joined first.  Call it after bw_ds_reset() and
 * before the game's OS_Init; bw_port_reset (sdk/port.h) is the start-up
 * that does both, then resets the PXI and joins it to this layer.
 *
 * The caller is the thread that holds the run token, which is the thread
 * that runs code now: the frame thread, the one that runs the port's own
 * code (the frame loop) while it holds the token.  It is left alone: its
 * host thread is neither killed nor joined, so it carries on, and the next
 * run's idle thread is that same host thread.  A thread running the game's
 * own code cannot be the caller: the reset clears the memory the game runs
 * in.
 */
void bw_os_reset(void);

/*
 * The OS layer's restart entry point: stop every game thread but the
 * caller's, and leave the caller runnable, so that the caller can carry
 * on and start the game again.
 *
 * The caller's own record is recognised as the caller's whatever thread it
 * is: it is the record of the thread that holds the run token (the frame
 * thread, the OS layer's idle thread's host thread, is the one the port's
 * restart uses).  Every other thread is stopped: the ones with a host
 * thread of their own are killed and joined, and the launcher, which has
 * none, is retired (it never resumes).
 *
 * The token is handed over when a thread dies, so that a wait does not
 * hang, and the kill flag is honoured where a thread blocks (in the halt
 * wait as well as in the scheduler's wait), so that a join does not hang
 * either.
 *
 * bw_port_reset (sdk/port.h) calls it, and the port's own restart
 * (bw_port_restart, sdk/port.h) calls it before the machine's power-on.
 */
void bw_os_stop_threads(void);

/*
 * Latch interrupt requests (IF |= mask).  Callable from any host thread:
 * the frame loop raising vblank, the ARM7 stand-in raising the IPC FIFO.
 * The interrupt is taken by the running game thread at its next safe point
 * (docs/boot.md): when it re-enables interrupts, halts, waits for an IRQ,
 * switches thread, or calls bw_os_poll.  Taking it means what it means on
 * the DS: IME on, the CPSR I bit clear, the IE bit set.
 */
void bw_os_raise_irq(u32 mask);

/* A safe point the port can offer explicitly (from the running thread).   */
void bw_os_poll(void);

/* IF as the OS layer sees it (requests latched and not yet taken).        */
u32  bw_os_pending_irqs(void);

/*
 * The halt hook.  OS_Halt is where the DS's CPU stops until an interrupt;
 * on the host it is where time has to pass.  With a hook installed,
 * OS_Halt calls it (on the halting thread, with no lock held) until an
 * enabled interrupt is pending: the frame loop's hook runs one frame, which
 * advances OS_GetTick and raises vblank.  With none, OS_Halt sleeps until
 * another host thread calls bw_os_raise_irq.
 */
typedef void (*BwOsHaltHook)(void *user);
void bw_os_set_halt_hook(BwOsHaltHook hook, void *user);

/* HW_INTR_CHECK_BUF in the DS address space.                              */
volatile u32 *bw_os_irq_check_flag(void);

/* The number of game threads with a live host thread (for tests).         */
int  bw_os_host_threads(void);

/* ------------------------------------------------------------------ */
/* Between the OS shim files only                                      */
/* ------------------------------------------------------------------ */

/* The one lock: the run token, the IRQ latch, the halt wait.              */
void bw_os_lock(void);
void bw_os_unlock(void);
void bw_os_wait(void);          /* lock held; releases while waiting       */
void bw_os_wake_all(void);      /* lock held                               */

/* The CPSR stand-in (sdk/os_irq.c): saved and restored per thread.        */
u32  bw_os_cpsr_get(void);
void bw_os_cpsr_set(u32 cpsr_if_bits);
void bw_os_set_proc_irq(int in_irq);
void bw_os_set_safe_point(void (*fn)(void));

/*
 * The other processor's turn: called by OS_EnableInterrupts,
 * OS_RestoreInterrupts and OS_RestoreInterrupts_IrqAndFiq before they look
 * for a pending interrupt, whatever state they leave interrupts in.
 * bw_port_reset installs bw_arm7_step (sdk/pxi.h, "Synchronisation");
 * bw_os_reset leaves it as it is.  NULL for none.
 */
void bw_os_set_cpu_step(void (*fn)(void));

/*
 * Run that step now.  Also called where the ARM9 waits rather than
 * restores: every turn of OS_Halt's wait (an ARM9 halted on a PXI reply
 * must let the ARM7 produce it) and bw_os_poll.
 */
void bw_os_cpu_step(void);

/* The tail of the IRQ handler: wake OS_WaitIrq's queue, switch if asked.  */
void bw_os_irq_thread_switch(void);

/* Latch TIMER1 if the alarm at the head of the queue is due.              */
void bw_os_alarm_poll(void);

void bw_os_thread_reset(void);
void bw_os_alarm_reset(void);
void bw_os_tick_reset(void);
void bw_os_system_reset(void);
void bw_os_argument_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_OS_H */
