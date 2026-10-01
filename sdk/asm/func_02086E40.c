/*
 * sdk/asm/func_02086E40.c -- OSi_ExceptionHandler at 0x02086E40.
 *
 * Replaces src/matched/arm9_sdk/func_02086E40.c (TwlSDK os_exception.o, an
 * asm function), the ARM9 exception vector target:
 *
 *     ldr r12, =g_02151290; ldr r12, [r12]; cmp r12, #0
 *     movne lr, pc; bxne r12            call the debugger hook if set
 *     ldr r12, =0x02000000
 *     stmdb r12!, {r0-r3, sp, lr}       save state below main RAM
 *     and r0, sp, #1; mov sp, r12       switch to that stack
 *     mrs r1, cpsr; and r1, r1, #0x1f   CPU mode
 *     teq r1, #0x17 (abort) or #0x1b (undefined):
 *         bl OSi_GetAndDisplayContext   dump the exception context
 *     ldr r12, =g_02151290 ...; beq _3  spin forever (both paths)
 *     ldmia sp!, {...}; mov sp, r12; bx lr   unreachable epilogue
 *
 * void OSi_ExceptionHandler(void).  Its body is CPU state (CPSR mode, a
 * stack switch, the banked registers of the faulting mode) and it never
 * returns: on the DS a data abort or undefined instruction ends here.  The
 * host has no such entry; reaching it means the port routed a fault into
 * game code, so it panics.  func_02086DD8.c (the vector installer) takes
 * the SDK name's address; the matched spelling is defined too.
 */

#include "asmsdk.h"
#include "../port.h"

void OSi_ExceptionHandler(void)
{
    bw_panic("OSi_ExceptionHandler", 0x02086E40u,
             "ARM9 exception entry (CPSR mode, stack switch, context dump) reached on the host");
}

void func_02086E40(void)
{
    OSi_ExceptionHandler();
}
