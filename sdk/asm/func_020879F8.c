/*
 * sdk/asm/func_020879F8.c -- OS_SpinWaitCpuCycles at 0x020879F8.
 *
 * Replaces src/matched/arm9_sdk/func_020879F8.c (TwlSDK os_system.o, an asm
 * function):
 *
 *     subs r0, r0, #4         cycle -= 4
 *     bhs  func_020879F8      loop while no borrow (cycle was >= 4)
 *     bx   lr
 *
 * void OS_SpinWaitCpuCycles(u32 cycle).  A delay loop of about `cycle` CPU
 * cycles with no other effect (r0 is not a result: the function is void).
 * The host has no timing to honour, so the call returns at once.  The
 * matched caller (OS_SpinWaitSysCycles.c) uses the SDK name; the address
 * spelling forwards.
 */

#include "asmsdk.h"

void OS_SpinWaitCpuCycles(u32 cycle)
{
    (void)cycle;
}

void func_020879F8(u32 cycle)
{
    OS_SpinWaitCpuCycles(cycle);
}
