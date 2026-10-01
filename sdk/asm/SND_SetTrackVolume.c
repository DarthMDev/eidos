/*
 * sdk/asm/SND_SetTrackVolume.c -- SND_SetTrackVolume at 0x02089400.
 *
 * Replaces src/matched/arm9_sdk/SND_SetTrackVolume.c (TwlSDK
 * snd_interface.o, an asm function):
 *
 *     stmfd sp!, {r3, lr}
 *     mov   r3, r2            data   = volume
 *     mov   ip, #2
 *     mov   r2, #0xa          offset = 0x0A (volume2)
 *     str   ip, [sp]          size   = 2 bytes (fifth argument)
 *     bl    func_020897DC
 *     ldmfd sp!, {r3, pc}
 *
 * void SND_SetTrackVolume(int playerNo, u32 trackBitMask, int volume) is
 * SNDi_SetTrackParam(playerNo, trackBitMask, 0x0A, volume, 2).
 * func_020897DC is matched C, void (int playerNo, u32 trackBitMask,
 * u32 offset, u32 data, int size).  The game caller (unit_02072F88.c)
 * declares the address spelling (u8, u32, int); that forwarder takes its
 * caller's types.
 */

#include "asmsdk.h"

extern void func_020897DC(int playerNo, u32 trackBitMask, u32 offset, u32 data, int size);

void SND_SetTrackVolume(int playerNo, u32 trackBitMask, int volume)
{
    func_020897DC(playerNo, trackBitMask, 0x0Au, (u32)volume, 2);
}

void func_02089400(u8 playerNo, u32 trackBitMask, int volume)
{
    SND_SetTrackVolume((int)playerNo, trackBitMask, volume);
}
