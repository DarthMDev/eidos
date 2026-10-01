/*
 * sdk/asm/OS_IsRunOnEmulator.c -- OS_IsRunOnEmulator at 0x02085D24.
 *
 * Replaces src/matched/arm9_sdk/OS_IsRunOnEmulator.c (TwlSDK os_emulator.o,
 * an asm function):
 *
 *     stmfd sp!, {r3, lr}
 *     bl    func_02085CBC          OS_GetConsoleType()
 *     tst   r0, #0x10000000        OS_CONSOLE_ISEMULATOR
 *     movne r0, #1
 *     moveq r0, #0
 *     ldmfd sp!, {r3, pc}
 *
 * BOOL OS_IsRunOnEmulator(void).  func_02085CBC is matched C, u32 (void).
 */

#include "asmsdk.h"

extern u32 func_02085CBC(void);

BOOL OS_IsRunOnEmulator(void)
{
    return (func_02085CBC() & 0x10000000u) != 0 ? 1 : 0;
}
