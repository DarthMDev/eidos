/*
 * sdk/asm/SND_SetTrackModSpeed.c -- SND_SetTrackModSpeed at 0x02089470.
 *
 * Replaces src/matched/arm9_sdk/SND_SetTrackModSpeed.c (TwlSDK
 * snd_interface.o, an asm function):
 *
 *     stmfd sp!, {r3, lr}
 *     mov   r3, r2            data   = speed
 *     mov   ip, #1
 *     mov   r2, #0x19         offset = 0x19 (mod.speed)
 *     str   ip, [sp]          size   = 1 byte (fifth argument)
 *     bl    func_020897DC
 *     ldmfd sp!, {r3, pc}
 *
 * void SND_SetTrackModSpeed(int playerNo, u32 trackBitMask, int speed) is
 * SNDi_SetTrackParam(playerNo, trackBitMask, 0x19, speed, 1).
 * func_020897DC is matched C, void (int playerNo, u32 trackBitMask,
 * u32 offset, u32 data, int size).  The game caller (unit_02072F88.c)
 * declares the address spelling (u8, u32, u32); that forwarder takes its
 * caller's types.
 */

#include "asmsdk.h"

extern void func_020897DC(int playerNo, u32 trackBitMask, u32 offset, u32 data, int size);

void SND_SetTrackModSpeed(int playerNo, u32 trackBitMask, int speed)
{
    func_020897DC(playerNo, trackBitMask, 0x19u, (u32)speed, 1);
}

void func_02089470(u8 playerNo, u32 trackBitMask, u32 speed)
{
    SND_SetTrackModSpeed((int)playerNo, trackBitMask, (int)speed);
}
