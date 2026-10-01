/*
 * sdk/port_reset.c -- bw_port_reset: the emulated machine's power-on, and
 * the joins between the layers that each own part of it.
 *
 * Why one function: three layers own state the game's boot needs at
 * power-on, and each one's reset undoes part of another's.
 *
 *   bw_ds_reset (sdk/dsmem.c) clears the register file and drops every I/O
 *       write hook, so it has to come first; it installs only the VRAMCNT
 *       hooks it owns, never one on IF.
 *   bw_os_reset (sdk/os_interrupt.c and its siblings) installs IF's
 *       write-one-to-clear hook (the OS layer owns 0x04000214) and stops any
 *       game thread left over from a previous run.
 *   bw_pxi_reset (sdk/pxi.c) installs the IPCSYNC, IPCFIFOCNT and send
 *       register hooks and powers the ARM7 stand-in off.
 *
 * Then the joins.  A PXI interrupt request must go into the OS layer's
 * latch: the PXI's default sink ORs the bit into the IF register's bytes,
 * which the OS layer's IF hook would read, at the next bw_io_sync, as the
 * game acknowledging it.  And the ARM7 has to run where the ARM9 would let
 * it (sdk/pxi.h, "Synchronisation"), which on the OS layer's side is every
 * OS_RestoreInterrupts.  Both are installed here, not by either layer, so
 * the OS layer links without the PXI and the PXI without the OS layer.
 */

#include "arm7/arm7.h"
#include "dsmem.h"
#include "os.h"
#include "port.h"
#include "pxi.h"

void bw_port_reset(void)
{
    /* Unjoin first, so nothing a reset runs reaches a half-reset layer.   */
    bw_os_set_cpu_step(NULL);
    bw_pxi_set_irq_sink(NULL);

    bw_ds_reset();
    bw_os_reset();
    bw_pxi_reset();

    bw_pxi_set_irq_sink(bw_os_raise_irq);
    bw_os_set_cpu_step(bw_arm7_step);
}
