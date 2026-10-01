/*
 * sdk/asm/SND_SetTrackPan.c -- SND_SetTrackPan at 0x02089438.
 *
 * Replaces src/matched/arm9_sdk/SND_SetTrackPan.c (TwlSDK snd_interface.o,
 * an asm function):
 *
 *     stmfd sp!, {r3, lr}
 *     mov   r3, r2            data   = pan
 *     mov   ip, #1
 *     mov   r2, #9            offset = 0x09 (pan)
 *     str   ip, [sp]          size   = 1 byte (fifth argument)
 *     bl    func_020897DC
 *     ldmfd sp!, {r3, pc}
 *
 * void SND_SetTrackPan(int playerNo, u32 trackBitMask, int pan) is
 * SNDi_SetTrackParam(playerNo, trackBitMask, 0x09, pan, 1).
 * func_020897DC is matched C, void (int playerNo, u32 trackBitMask,
 * u32 offset, u32 data, int size).  The game caller (unit_02072F88.c)
 * declares the address spelling (u8, u32, u32); that forwarder takes its
 * caller's types.
 */

#include "asmsdk.h"

extern void func_020897DC(int playerNo, u32 trackBitMask, u32 offset, u32 data, int size);

void SND_SetTrackPan(int playerNo, u32 trackBitMask, int pan)
{
    func_020897DC(playerNo, trackBitMask, 0x09u, (u32)pan, 1);
}

void func_02089438(u8 playerNo, u32 trackBitMask, u32 pan)
{
    SND_SetTrackPan((int)playerNo, trackBitMask, (int)pan);
}
