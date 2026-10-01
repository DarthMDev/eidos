/*
 * sdk/asm/SNDi_IncAlarmId.c -- SNDi_IncAlarmId at 0x02089FC0.
 *
 * Replaces src/matched/arm9_sdk/SNDi_IncAlarmId.c (TwlSDK snd_alarm.o, an
 * asm function):
 *
 *     ldr r2, =g_02152F40; mov r1, #0xc; mla r1, r0, r1, r2
 *     ldrb r0, [r1, #8]; add r0, r0, #1; strb r0, [r1, #8]
 *
 * Increments the id byte at +8 of alarm `alarmNo` in the SND alarm table
 * (12-byte entries at g_02152F40) and returns the incremented value
 * *before* truncation: 0xFF comes back as 0x100 while the byte wraps to 0.
 * The one matched caller declares it void and ignores the value.
 */

#include <stddef.h>

#include "asmsdk.h"

extern u8 g_02152F40[];

u32 SNDi_IncAlarmId(int alarmNo)
{
    u8 *id = g_02152F40 + (ptrdiff_t)alarmNo * 12 + 8;
    u32 v = (u32)*id + 1u;

    *id = (u8)v;
    return v;
}
