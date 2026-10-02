/*
 * sdk/asm/RTCi_ConvertTimeToSecond.c -- RTCi_ConvertTimeToSecond at
 * 0x02088FEC.
 *
 * Replaces src/matched/arm9_sdk/RTCi_ConvertTimeToSecond.c (an asm body).
 * s32 RTCi_ConvertTimeToSecond(const RTCTime *time), RTCTime being three
 * words { hour, minute, second }:
 *
 *     mov r1, #0x3c; ldmia r0, {r2, r3}; mla r3, r2, r1, r3
 *     ldr r0, [r0, #8]; mla r0, r3, r1, r0; bx lr
 *
 * i.e. (hour * 60 + minute) * 60 + second, in 32-bit arithmetic.  The DWC
 * library's initialisation reaches it (through ov009, docs/overlays.md).
 */

#include "asmsdk.h"

s32 RTCi_ConvertTimeToSecond(const u32 *time);

s32 RTCi_ConvertTimeToSecond(const u32 *time)
{
    return (s32)((time[0] * 60u + time[1]) * 60u + time[2]);
}
