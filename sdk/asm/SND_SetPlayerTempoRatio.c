/*
 * sdk/asm/SND_SetPlayerTempoRatio.c -- SND_SetPlayerTempoRatio at 0x020893B8.
 *
 * Replaces src/matched/arm9_sdk/SND_SetPlayerTempoRatio.c (TwlSDK
 * snd_interface.o, an asm tail call):
 *
 *     ldr ip, =func_020897B4
 *     mov r2, r1              data   = ratio
 *     mov r1, #0x1a           offset = 0x1A (tempoRatio)
 *     mov r3, #2              size   = 2 bytes
 *     bx  ip
 *
 * void SND_SetPlayerTempoRatio(int playerNo, int ratio) is
 * SNDi_SetPlayerParam(playerNo, 0x1A, ratio, 2).  func_020897B4 is matched
 * C, void (int playerNo, u32 offset, u32 data, int size).  The game caller
 * (unit_02072F88.c) declares the address spelling (u8, u32); that
 * forwarder takes its caller's types.
 */

#include "asmsdk.h"

extern void func_020897B4(int playerNo, u32 offset, u32 data, int size);

void SND_SetPlayerTempoRatio(int playerNo, int ratio)
{
    func_020897B4(playerNo, 0x1Au, (u32)ratio, 2);
}

void func_020893B8(u8 playerNo, u32 ratio)
{
    SND_SetPlayerTempoRatio((int)playerNo, (int)ratio);
}
