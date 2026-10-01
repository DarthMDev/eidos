/*
 * sdk/asm/SND_SetTrackPitch.c -- SND_SetTrackPitch at 0x0208941C.
 *
 * Replaces src/matched/arm9_sdk/SND_SetTrackPitch.c (TwlSDK
 * snd_interface.o, an asm function):
 *
 *     stmfd sp!, {r3, lr}
 *     mov   r3, r2            data   = pitch
 *     mov   ip, #2
 *     mov   r2, #0xc          offset = 0x0C (pitch)
 *     str   ip, [sp]          size   = 2 bytes (fifth argument)
 *     bl    func_020897DC
 *     ldmfd sp!, {r3, pc}
 *
 * void SND_SetTrackPitch(int playerNo, u32 trackBitMask, int pitch) is
 * SNDi_SetTrackParam(playerNo, trackBitMask, 0x0C, pitch, 2).
 * func_020897DC is matched C, void (int playerNo, u32 trackBitMask,
 * u32 offset, u32 data, int size).  The game caller (unit_02072F88.c)
 * declares the address spelling (u8, u32, u32); that forwarder takes its
 * caller's types.
 */

#include "asmsdk.h"

extern void func_020897DC(int playerNo, u32 trackBitMask, u32 offset, u32 data, int size);

void SND_SetTrackPitch(int playerNo, u32 trackBitMask, int pitch)
{
    func_020897DC(playerNo, trackBitMask, 0x0Cu, (u32)pitch, 2);
}

void func_0208941C(u8 playerNo, u32 trackBitMask, u32 pitch)
{
    SND_SetTrackPitch((int)playerNo, trackBitMask, (int)pitch);
}
