/*
 * sdk/os_irq.c -- the OS interrupt-state and termination shims.
 *
 * Three SDK names the heap layer reaches, and nothing else.  They come into
 * the port's closure with src/matched/sub_02030098.c (the raw allocator,
 * which brackets its critical section with the first two) and
 * src/matched/sub_02030734.c (the allocation entry point, whose failure arm
 * ends in the third).
 *
 * OS_DisableInterrupts / OS_RestoreInterrupts
 *
 *   On the DS these write the CPSR I bit and IME, and the value returned by
 *   the first is the previous state that the second puts back.  The contract
 *   the matched code depends on is only that: the pair nests, and the token
 *   handed to restore is the one disable produced.  The port is
 *   single-threaded and has no interrupt to mask -- the vblank "interrupt" in
 *   engine/frame/ is a call on the same stack -- so the honest shim is a
 *   counter: disable reports whether interrupts were enabled on entry and
 *   restore puts that answer back.  That keeps the value meaningful (a
 *   matched caller that tested it would see the truth) without pretending to
 *   mask anything.
 *
 *   The SDK's encoding is the IME register's: 1 = enabled, 0 = disabled.
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
 *   status rather than a crash dump.
 */

#include <stdio.h>
#include <stdlib.h>

#include "port.h"

static u32 g_ime = 1;

u32 OS_DisableInterrupts(void)
{
    u32 was = g_ime;
    g_ime = 0;
    return was;
}

u32 OS_RestoreInterrupts(u32 state)
{
    u32 was = g_ime;
    g_ime = state;
    return was;
}

u32 OS_EnableInterrupts(void)
{
    u32 was = g_ime;
    g_ime = 1;
    return was;
}

void OS_Terminate(void)
{
    fflush(stdout);
    fprintf(stderr, "eidos: OS_Terminate (0x02088234): the game asked the OS "
                    "to stop, which on this path means the heap was "
                    "exhausted\n");
    exit(EXIT_FAILURE);
}
