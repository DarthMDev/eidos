/*
 * sdk/asm/RTCi_ReadRawTimeAsync.c -- RTCi_ReadRawTimeAsync at 0x02088F0C.
 *
 * Replaces src/matched/arm9_sdk/RTCi_ReadRawTimeAsync.c (TwlSDK librtc
 * internal.o, an asm tail call):
 *
 *     ldr ip, =func_02088F2C; mov r0, #0x12; bx ip
 *
 * BOOL RTCi_ReadRawTimeAsync(void) is RtcSendPxiCommand(0x12)
 * (RTC_PXI_COMMAND_READ_TIME).  func_02088F2C is matched C,
 * BOOL (u32 command), and its result is returned unchanged.
 */

#include "asmsdk.h"

extern BOOL func_02088F2C(u32 command);

BOOL RTCi_ReadRawTimeAsync(void)
{
    return func_02088F2C(0x12u);
}
