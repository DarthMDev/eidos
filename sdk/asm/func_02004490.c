/*
 * sdk/asm/func_02004490.c -- SVC_WaitByLoop at 0x02004490 (Thumb).
 *
 * Replaces src/matched/arm9/func_02004490.c (an asm function):
 *
 *     swi 3                   BIOS WaitByLoop(r0 = count)
 *     bx  lr
 *
 * void SVC_WaitByLoop(s32 count).  The BIOS spins `count` iterations of a
 * four-cycle loop and touches nothing else; it exists to let hardware (the
 * ARM7, a register write) settle.  On the host nothing needs that time, so
 * the call returns at once.  The matched callers use both spellings
 * (func_02004490 with an int or u32 argument, SVC_WaitByLoop(int)); one
 * forwards to the other.
 */

#include "asmsdk.h"

void SVC_WaitByLoop(s32 count)
{
    (void)count;
}

void func_02004490(s32 count)
{
    SVC_WaitByLoop(count);
}
