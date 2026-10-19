/*
 * sdk/asm/OS_GetOwnerRtcOffset.c -- 0x02087D08 OS_GetOwnerRtcOffset.
 *
 * Replaces src/matched/arm9_sdk/OS_GetOwnerRtcOffset.c (an mwcc `asm`
 * body): s64 OS_GetOwnerRtcOffset(void).  Returns the 64-bit RTC offset the
 * firmware keeps in the owner information at 0x02FFFC80 + 0x68 (low word)
 * and + 0x6C (high word), read through the port's DS address space.
 */

#include "asmsdk.h"
#include "../dsmem.h"

s64 OS_GetOwnerRtcOffset(void)
{
    const u32 *p = (const u32 *)bw_ds_ptr(0x02FFFCE8u);

    return (s64)(((u64)p[1] << 32) | p[0]);
}
