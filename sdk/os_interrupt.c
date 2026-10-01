/*
 * sdk/os_interrupt.c -- the interrupt controller and the IRQ handler:
 * IE/IF/IME, OS_IRQTable, the callback slots, taking an interrupt, halting,
 * and waiting for one.
 *
 * What is matched and what is here
 *
 *   OS_SetIrqFunction, OS_GetIrqFunction, OS_EnableIrqMask,
 *   OS_DisableIrqMask and OS_SetIrqMask are EXACT C in aletheia
 *   (src/matched/arm9_sdk/) and are register and table traffic, so they are
 *   compiled as written: they store into IE through the register file
 *   (sdk/dsmem.h) and into the two tables defined below.  What has no C to
 *   compile is the CPU's side: OS_IrqHandler (ITCM asm), the halt (a CP15
 *   instruction), and the moment an interrupt is taken.  That is this file,
 *   plus the waits built on the check flag (see below).
 *
 * Taking an interrupt
 *
 *   bw_os_raise_irq(mask) latches requests from any host thread (the frame
 *   loop's vblank, the ARM7 stand-in's FIFO, the alarm clock).  The running
 *   game thread takes them at a safe point: when it re-enables interrupts
 *   (sdk/os_irq.c calls back here), when it halts, when the port calls
 *   bw_os_poll.  Taking one is OS_IrqHandler's job on the DS and is done the
 *   same way: with IME on, the CPSR I bit clear and the request enabled in
 *   IE, acknowledge the lowest pending bit, run its handler in IRQ mode with
 *   IRQs masked, repeat while anything enabled is pending, then run the
 *   handler's thread-switch tail (sdk/os_thread.c,
 *   bw_os_irq_thread_switch).  DMA, NDMA and timer requests go through the
 *   twelve callback slots exactly as OSi_IrqCallback (func_02084614) does:
 *   one-shot function, check flag set, mask dropped unless `enable`.
 *
 * IF and write-one-to-clear
 *
 *   The authoritative request latch is held here (the hardware's, which a
 *   game store cannot set), and the IF register in the register file is a
 *   mirror of it, rewritten at every safe point.  A matched store to IF is
 *   an acknowledge: the write hook installed on IF turns the value written
 *   into "clear these bits".  The register file only notices a store that
 *   changes the bytes (sdk/dsmem.h: hooks run on change at bw_io_sync), so
 *   an acknowledge that writes exactly the value already showing would be
 *   lost; the SDK's one IF writer, OS_ResetRequestIrqMask, is therefore a
 *   shim here too, and acknowledges the latch directly.
 *
 * The check flag
 *
 *   HW_INTR_CHECK_BUF, DTCM + 0x3FF8 (DS 0x02FE3FF8), is the word interrupt
 *   handlers set and OS_WaitIrq polls.  The matched waits reach it as
 *   `(u32)SDK_AUTOLOAD_DTCM_START + 0x3FF8`, an address computed in a u32
 *   from a linker symbol, and the game's own vblank handler
 *   (src/matched/arm9/func_0200520C.c) as SDK_AUTOLOAD_MAIN_TWL_START +
 *   0x3FF8.  The port reaches it by its DS address through bw_ds_ptr, so
 *   OS_WaitIrq, OS_WaitInterrupt and OS_WaitVBlankIntr are shims here; the
 *   DTCM symbols must be registered at 0x02FE0000 (bw_ds_register) for the
 *   matched writers to land on the same word.
 *
 * Halting
 *
 *   OS_Halt stops until an enabled request is pending, which on the host is
 *   where time has to pass: it calls the halt hook (the frame loop runs a
 *   frame) or, with none, sleeps until another host thread raises one.  The
 *   idle thread lives in this loop whenever every game thread is waiting.
 */

#include <string.h>

#include "dsmem.h"
#include "os.h"
#include "port.h"

void OS_SleepThread(OSThreadQueue *queue);
u32  OS_DisableInterrupts(void);
u32  OS_EnableInterrupts(void);
u32  OS_RestoreInterrupts(u32 state);

/* ------------------------------------------------------------------ */
/* The tables (os_interrupt.o, os_irqtable.o, os_irqhandler.o data)    */
/* ------------------------------------------------------------------ */

OSIrqFunction     g_02FE0020[BW_OS_IRQ_TABLE_MAX];   /* OS_IRQTable          */
OSIrqCallbackInfo g_02150F38[BW_OS_IRQ_CALLBACKS];   /* OSi_IrqCallbackInfo  */
OSThreadQueue     OSi_IrqThreadQueue;

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static int          g_ready;        /* bw_os_reset has run                  */
static u32          g_if;           /* the latch (lock)                     */
static u32          g_if_mirror;    /* what the IF register last showed     */
static int          g_dispatching;
static BwOsHaltHook g_halt_hook;    /* lock */
static void        *g_halt_user;

volatile u32 *bw_os_irq_check_flag(void)
{
    return (volatile u32 *)bw_ds_ptr(BW_DS_INTR_CHECK_BUF);
}

static u32 latch_read(void)
{
    u32 v;

    bw_os_lock();
    v = g_if;
    bw_os_unlock();
    return v;
}

/* Token holder only: show the latch in the register file. */
static void mirror_if(void)
{
    u32 v = latch_read();

    if (v != g_if_mirror) {
        g_if_mirror = v;
        bw_io_write32(BW_REG_IF, v);
    }
}

/* The register file's IF write hook: a game store acknowledges. */
static void if_write_hook(u32 addr, u32 old_value, u32 new_value)
{
    (void)addr;
    (void)old_value;
    if (new_value == g_if_mirror) {
        return;                     /* the mirror's own write */
    }
    bw_os_lock();
    g_if &= ~new_value;
    bw_os_unlock();
    g_if_mirror = ~latch_read();    /* force the rewrite below */
    mirror_if();
}

void bw_os_raise_irq(u32 mask)
{
    if (!g_ready) {
        g_if |= mask;               /* no game threads exist yet */
        return;
    }
    bw_os_lock();
    g_if |= mask;
    bw_os_wake_all();
    bw_os_unlock();
}

u32 bw_os_pending_irqs(void)
{
    return g_ready ? latch_read() : g_if;
}

void bw_os_set_halt_hook(BwOsHaltHook hook, void *user)
{
    bw_os_lock();
    g_halt_hook = hook;
    g_halt_user = user;
    bw_os_unlock();
}

/* ------------------------------------------------------------------ */
/* OS_IrqHandler                                                       */
/* ------------------------------------------------------------------ */

/* OS_SetIrqFunction's mapping from IE bit to callback slot. */
static int callback_slot(int bit)
{
    if (bit >= 8 && bit <= 11) {
        return bit - 8;             /* DMA0-3  */
    }
    if (bit >= 28 && bit <= 31) {
        return bit - 24;            /* NDMA0-3 */
    }
    if (bit >= 3 && bit <= 6) {
        return bit - 3 + 8;         /* TIMER0-3 */
    }
    return -1;
}

static int lowest_bit(u32 v)
{
    int i = 0;

    while (!(v & 1u)) {
        v >>= 1;
        i++;
    }
    return i;
}

static void run_handler(int bit)
{
    int slot = callback_slot(bit);
    u32 mask = 1u << bit;

    if (slot >= 0) {
        /* OSi_IrqCallback (func_02084614) */
        OSIrqCallbackInfo *info = &g_02150F38[slot];
        void (*cb)(void *) = info->func;

        info->func = NULL;
        if (cb != NULL) {
            cb(info->arg);
        }
        *bw_os_irq_check_flag() |= mask;
        if (!info->enable) {
            bw_io_write32(BW_REG_IE, bw_io_read32(BW_REG_IE) & ~mask);
        }
    } else if (g_02FE0020[bit] != NULL) {
        g_02FE0020[bit]();
    }
    /* An empty slot is OS_IrqDummy: acknowledged, nothing run. */
}

static void dispatch(void)
{
    u32 cpsr;

    g_dispatching = 1;
    cpsr = bw_os_cpsr_get();
    bw_os_cpsr_set(cpsr | OS_INTRMODE_IRQ_DISABLE);
    bw_os_set_proc_irq(1);
    for (;;) {
        u32 pend;

        bw_os_alarm_poll();
        if (!(bw_io_read16(BW_REG_IME) & 1u)) {
            break;
        }
        bw_os_lock();
        pend = g_if & bw_io_read32(BW_REG_IE);
        if (pend != 0) {
            pend &= 0u - pend;      /* the lowest: the handler's priority */
            g_if &= ~pend;          /* reg_OS_IF = bit, the acknowledge   */
        }
        bw_os_unlock();
        if (pend == 0) {
            break;
        }
        mirror_if();
        run_handler(lowest_bit(pend));
    }
    mirror_if();
    bw_os_set_proc_irq(0);
    bw_os_cpsr_set(cpsr);
    g_dispatching = 0;
    bw_os_irq_thread_switch();
}

/* Take whatever can be taken now (sdk/os_irq.c calls this on enable). */
static void safe_point(void)
{
    u32 pend;

    if (!g_ready || g_dispatching) {
        return;
    }
    bw_os_alarm_poll();
    if (bw_os_cpsr_get() & OS_INTRMODE_IRQ_DISABLE) {
        return;
    }
    pend = latch_read();
    if (pend == 0) {
        return;
    }
    mirror_if();
    if (!(bw_io_read16(BW_REG_IME) & 1u) || !(pend & bw_io_read32(BW_REG_IE))) {
        return;
    }
    dispatch();
}

void bw_os_poll(void)
{
    safe_point();
}

/* ------------------------------------------------------------------ */
/* Halt and the waits                                                  */
/* ------------------------------------------------------------------ */

/* 0x020882D0, asm in aletheia (mcr p15, c7, c0, 4: wait for interrupt). */
void OS_Halt(void)
{
    if (!g_ready) {
        return;
    }
    for (;;) {
        BwOsHaltHook hook;
        void *user;

        bw_os_alarm_poll();
        bw_os_lock();
        if (g_if & bw_io_read32(BW_REG_IE)) {
            bw_os_unlock();
            break;
        }
        hook = g_halt_hook;
        user = g_halt_user;
        if (hook == NULL) {
            bw_os_wait();           /* until bw_os_raise_irq */
            bw_os_unlock();
            continue;
        }
        bw_os_unlock();
        hook(user);                 /* time passes */
    }
    mirror_if();
    safe_point();                   /* the CPU takes it on waking */
}

/* func_0208459C, transcribed (the check flag by its DS address). */
void OS_WaitIrq(int clear, u32 irqFlags)
{
    u32 e = OS_DisableInterrupts();

    if (clear) {
        *bw_os_irq_check_flag() &= ~irqFlags;
    }
    (void)OS_RestoreInterrupts(e);
    while (!(irqFlags & *bw_os_irq_check_flag())) {
        OS_SleepThread(&OSi_IrqThreadQueue);
    }
}

/*
 * 0x02087AE4: SVC_WaitByLoop(1) then OS_WaitIrq(TRUE, OS_IE_V_BLANK).  The
 * busy loop is a few CPU cycles of settling time on hardware; on the host
 * there is nothing to settle.
 */
void OS_WaitVBlankIntr(void)
{
    OS_WaitIrq(1, OS_IE_V_BLANK);
}

/* func_02087A3C, OS_WaitInterrupt: halt in place, no thread switch. */
void OS_WaitInterrupt(int clear, u32 irqFlags)
{
    u32 cpsr = OS_DisableInterrupts();
    u16 prev_ime = bw_io_read16(BW_REG_IME);

    bw_io_write16(BW_REG_IME, 1);
    if (clear) {
        *bw_os_irq_check_flag() &= ~irqFlags;
    }
    while (!(irqFlags & *bw_os_irq_check_flag())) {
        OS_Halt();
        (void)OS_EnableInterrupts();
        (void)OS_DisableInterrupts();
    }
    *bw_os_irq_check_flag() &= ~irqFlags;
    bw_io_write16(BW_REG_IME, prev_ime);
    (void)OS_RestoreInterrupts(cpsr);
}

/* 0x020849C8: IME off, read IF, acknowledge `intr`, IME back. */
u32 OS_ResetRequestIrqMask(u32 intr)
{
    u16 ime = bw_io_read16(BW_REG_IME);
    u32 prep;

    bw_io_write16(BW_REG_IME, 0);
    bw_os_lock();
    prep = g_if;
    g_if &= ~intr;
    bw_os_unlock();
    mirror_if();
    bw_io_write16(BW_REG_IME, ime);
    return prep;
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */

void bw_os_reset(void)
{
    g_ready = 0;
    bw_os_set_safe_point(NULL);
    bw_os_thread_reset();           /* creates the lock on first use */

    bw_os_lock();
    g_if = 0;
    g_halt_hook = NULL;
    g_halt_user = NULL;
    bw_os_unlock();
    g_dispatching = 0;

    memset(g_02FE0020, 0, sizeof(g_02FE0020));
    memset(g_02150F38, 0, sizeof(g_02150F38));
    memset(&OSi_IrqThreadQueue, 0, sizeof(OSi_IrqThreadQueue));

    bw_io_hook(BW_REG_IF, 4, if_write_hook);
    g_if_mirror = 0;
    bw_io_write32(BW_REG_IF, 0);
    *bw_os_irq_check_flag() = 0;

    bw_os_cpsr_set(0);
    bw_os_set_proc_irq(0);
    bw_os_alarm_reset();
    bw_os_tick_reset();
    bw_os_system_reset();
    bw_os_argument_reset();

    g_ready = 1;
    bw_os_set_safe_point(safe_point);
}

/* Address spellings (sdk/aliases.c says why a host link needs them). */
void func_0208459C(int clear, u32 f) { OS_WaitIrq(clear, f); }
void func_02087A3C(int clear, u32 f) { OS_WaitInterrupt(clear, f); }
void func_02087AE4(void)             { OS_WaitVBlankIntr(); }
void func_020882D0(void)             { OS_Halt(); }
u32  func_020849C8(u32 intr)         { return OS_ResetRequestIrqMask(intr); }
