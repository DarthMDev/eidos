/*
 * sdk/asm/SND_SetPlayerVolume.c -- SND_SetPlayerVolume at 0x020893D0.
 *
 * Replaces src/matched/arm9_sdk/SND_SetPlayerVolume.c (TwlSDK
 * snd_interface.o, an asm tail call):
 *
 *     ldr ip, =func_020897B4
 *     mov r2, r1              data   = volume
 *     mov r1, #6              offset = 6 (extFader)
 *     mov r3, #2              size   = 2 bytes
 *     bx  ip
 *
 * void SND_SetPlayerVolume(int playerNo, int volume) is
 * SNDi_SetPlayerParam(playerNo, 6, volume, 2).  func_020897B4 is matched C,
 * void (int playerNo, u32 offset, u32 data, int size).  The game callers
 * (func_020731E0.c, func_020735EC.c) declare the address spelling
 * (u32 player, s32 vol); that forwarder takes their types.
 */

#include "asmsdk.h"

extern void func_020897B4(int playerNo, u32 offset, u32 data, int size);

void SND_SetPlayerVolume(int playerNo, int volume)
{
    func_020897B4(playerNo, 6u, (u32)volume, 2);
}

void func_020893D0(u32 playerNo, s32 volume)
{
    SND_SetPlayerVolume((int)playerNo, (int)volume);
}
