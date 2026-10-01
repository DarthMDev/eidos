/*
 * sdk/asm/SND_SetTrackModDepth.c -- SND_SetTrackModDepth at 0x02089454.
 *
 * Replaces src/matched/arm9_sdk/SND_SetTrackModDepth.c (TwlSDK
 * snd_interface.o, an asm function):
 *
 *     stmfd sp!, {r3, lr}
 *     mov   r3, r2            data   = depth
 *     mov   ip, #1
 *     mov   r2, #0x1a         offset = 0x1A (mod.depth)
 *     str   ip, [sp]          size   = 1 byte (fifth argument)
 *     bl    func_020897DC
 *     ldmfd sp!, {r3, pc}
 *
 * void SND_SetTrackModDepth(int playerNo, u32 trackBitMask, int depth) is
 * SNDi_SetTrackParam(playerNo, trackBitMask, 0x1A, depth, 1).
 * func_020897DC is matched C, void (int playerNo, u32 trackBitMask,
 * u32 offset, u32 data, int size).  The game caller (unit_02072F88.c)
 * declares the address spelling (u8, u32, u32); that forwarder takes its
 * caller's types.
 */

#include "asmsdk.h"

extern void func_020897DC(int playerNo, u32 trackBitMask, u32 offset, u32 data, int size);

void SND_SetTrackModDepth(int playerNo, u32 trackBitMask, int depth)
{
    func_020897DC(playerNo, trackBitMask, 0x1Au, (u32)depth, 1);
}

void func_02089454(u8 playerNo, u32 trackBitMask, u32 depth)
{
    SND_SetTrackModDepth((int)playerNo, trackBitMask, (int)depth);
}
