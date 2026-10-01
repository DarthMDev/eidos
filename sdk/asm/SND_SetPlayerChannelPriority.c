/*
 * sdk/asm/SND_SetPlayerChannelPriority.c -- SND_SetPlayerChannelPriority at 0x020893E8.
 *
 * Replaces src/matched/arm9_sdk/SND_SetPlayerChannelPriority.c (TwlSDK
 * snd_interface.o, an asm tail call):
 *
 *     ldr ip, =func_020897B4
 *     mov r2, r1              data   = prio
 *     mov r1, #4              offset = 4 (prio)
 *     mov r3, #1              size   = 1 byte
 *     bx  ip
 *
 * void SND_SetPlayerChannelPriority(int playerNo, int prio) is
 * SNDi_SetPlayerParam(playerNo, 4, prio, 1).  func_020897B4 is matched C,
 * void (int playerNo, u32 offset, u32 data, int size).  The game caller
 * (unit_02072F88.c) declares the address spelling (u8, u32); that
 * forwarder takes its caller's types.
 */

#include "asmsdk.h"

extern void func_020897B4(int playerNo, u32 offset, u32 data, int size);

void SND_SetPlayerChannelPriority(int playerNo, int prio)
{
    func_020897B4(playerNo, 4u, (u32)prio, 1);
}

void func_020893E8(u8 playerNo, u32 prio)
{
    SND_SetPlayerChannelPriority((int)playerNo, (int)prio);
}
