/*
 * sdk/os_irq.c -- the CPU's interrupt state: the CPSR I and F bits, the
 * processor mode, and OS_Terminate.
 *
 * Every function here is an `asm` body in aletheia (src/matched/arm9_sdk/
 * OS_DisableInterrupts.c and its siblings: mrs/msr on the CPSR), so there is
 * no C to compile and the CPU has to be stood in for (docs/boot.md, "Three
 * layers": a shim when the behaviour depends on the CPU).
 *
 * The CPSR stand-in
 *
 *   One word holding the I (0x80) and F (0x40) bits.  The values returned
 *   are the SDK's own encoding, the masked CPSR: OS_DisableInterrupts
 *   returns OS_INTRMODE_IRQ_DISABLE (0x80) if interrupts were already off
 *   and 0 if they were on, and OS_RestoreInterrupts puts that back.  A
 *   matched caller that tests the value (`== OS_INTRMODE_IRQ_DISABLE`) sees
 *   the truth.  (The first version of this file returned IME's encoding,
 *   1 = enabled; nothing tested the value then, and the SDK's is the CPSR's.)
 *
 *   On the DS the CPSR is part of a thread's context, so sdk/os_thread.c
 *   saves this word when it switches a thread out and restores it when the
 *   thread resumes (bw_os_cpsr_get / bw_os_cpsr_set).
 *
 *   Clearing the I bit is where a pending interrupt is taken on hardware,
 *   so Enable and Restore end in the OS layer's safe point when the result
 *   has interrupts on.  The safe point is installed at run time by
 *   sdk/os_interrupt.c (bw_os_reset) rather than called directly, so this
 *   file links on its own: the heap layer's critical sections reach it in
 *   builds and tests that have no interrupt controller at all, and there the
 *   pair is exactly the saved flag it always was.
 *
 * The other processor
 *
 *   On the DS the ARM7 runs alongside; here it runs when the ARM9 lets it
 *   (sdk/pxi.h, "Synchronisation").  Every PXI_SendWordByFifo ends in
 *   OS_RestoreInterrupts, so Enable and Restore first run the CPU step the
 *   port installs (bw_os_set_cpu_step; bw_port_reset installs the ARM7
 *   stand-in's bw_arm7_step), whatever state they leave interrupts in: a
 *   word stored into the send register is taken before the next store can
 *   overwrite it, and a reply is latched before the safe point looks for
 *   it.  Like the safe point it is installed at run time, so this file
 *   still links without the PXI.
 *
 * OS_GetProcMode
 *
 *   0x1F (system mode) normally, 0x12 (IRQ mode) while sdk/os_interrupt.c
 *   is running an interrupt handler.  OSi_RescheduleThread's "defer the
 *   switch to the end of the interrupt" test reads it.
 *
 * OS_Terminate
 *
 *   The SDK's "stop the machine" call.  sub_02030734 reaches it after the
 *   out-of-memory reporter, and its own header records that the compiler was
 *   not told the function is noreturn (there is a branch to the shared
 *   `return p` tail after the call).  The port must not return: a heap that
 *   could not satisfy an allocation has nothing sensible to hand back, and
 *   every caller of sub_02030734 in the game dereferences the result.  exit()
 *   rather than abort() so a host run that hits it ends with a diagnosable
 *   status rather than a crash dump.  aletheia also has a matched
 *   OS_Terminate.c (FATFS unmount, then OSi_TerminateCore's halt loop); if
 *   the build takes that one, this definition has to go.
 */

#include <stdio.h>
#include <stdlib.h>

#include "os.h"
#include "port.h"

#define CPSR_IF (OS_INTRMODE_IRQ_DISABLE | OS_INTRMODE_FIQ_DISABLE)

static u32 g_cpsr;                 /* I and F bits; 0 = both enabled */
static int g_in_irq;
static void (*g_safe_point)(void);
static void (*g_cpu_step)(void);

u32 bw_os_cpsr_get(void)                    { return g_cpsr; }
void bw_os_cpsr_set(u32 bits)               { g_cpsr = bits & CPSR_IF; }
void bw_os_set_proc_irq(int on)             { g_in_irq = on; }
void bw_os_set_safe_point(void (*fn)(void)) { g_safe_point = fn; }
void bw_os_set_cpu_step(void (*fn)(void))   { g_cpu_step = fn; }

static void after_restore(void)
{
    if (g_cpu_step != NULL) {
        g_cpu_step();
    }
    if (!(g_cpsr & OS_INTRMODE_IRQ_DISABLE) && g_safe_point != NULL) {
        g_safe_point();
    }
}

u32 OS_DisableInterrupts(void)
{
    u32 was = g_cpsr & OS_INTRMODE_IRQ_DISABLE;
    g_cpsr |= OS_INTRMODE_IRQ_DISABLE;
    return was;
}

u32 OS_EnableInterrupts(void)
{
    u32 was = g_cpsr & OS_INTRMODE_IRQ_DISABLE;
    g_cpsr &= ~OS_INTRMODE_IRQ_DISABLE;
    after_restore();
    return was;
}

u32 OS_RestoreInterrupts(u32 state)
{
    u32 was = g_cpsr & OS_INTRMODE_IRQ_DISABLE;
    g_cpsr = (g_cpsr & ~OS_INTRMODE_IRQ_DISABLE) | (state & OS_INTRMODE_IRQ_DISABLE);
    after_restore();
    return was;
}

u32 OS_DisableInterrupts_IrqAndFiq(void)
{
    u32 was = g_cpsr & CPSR_IF;
    g_cpsr |= CPSR_IF;
    return was;
}

u32 OS_RestoreInterrupts_IrqAndFiq(u32 state)
{
    u32 was = g_cpsr & CPSR_IF;
    g_cpsr = (g_cpsr & ~CPSR_IF) | (state & CPSR_IF);
    after_restore();
    return was;
}

u32 OS_GetCpsrIrq(void)
{
    return g_cpsr & OS_INTRMODE_IRQ_DISABLE;
}

u32 OS_GetProcMode(void)
{
    return g_in_irq ? OS_PROCMODE_IRQ : OS_PROCMODE_SYS;
}

void OS_Terminate(void)
{
    fflush(stdout);
    fprintf(stderr, "eidos: OS_Terminate (0x02088234): the game asked the OS "
                    "to stop, which on this path means the heap was "
                    "exhausted\n");
    exit(EXIT_FAILURE);
}

/*
 * The address spellings matched callers use: aletheia's files call most OS
 * functions by their func_ name, and a host links by name (sdk/aliases.c
 * says why).  layout/symbols_sdk.txt gives the SDK name at each address.
 */
u32 func_02087974(void)      { return OS_EnableInterrupts(); }
u32 func_02087988(void)      { return OS_DisableInterrupts(); }
u32 func_0208799C(u32 state) { return OS_RestoreInterrupts(state); }
u32 func_020879B4(void)      { return OS_DisableInterrupts_IrqAndFiq(); }
u32 func_020879C8(u32 state) { return OS_RestoreInterrupts_IrqAndFiq(state); }
u32 func_020879E0(void)      { return OS_GetCpsrIrq(); }
u32 func_020879EC(void)      { return OS_GetProcMode(); }
