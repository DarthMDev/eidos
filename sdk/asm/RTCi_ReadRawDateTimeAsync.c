/*
 * sdk/asm/RTCi_ReadRawDateTimeAsync.c -- RTCi_ReadRawDateTimeAsync at 0x02088EEC.
 *
 * Replaces src/matched/arm9_sdk/RTCi_ReadRawDateTimeAsync.c (TwlSDK
 * librtc internal.o, an asm tail call):
 *
 *     ldr ip, =func_02088F2C; mov r0, #0x10; bx ip
 *
 * BOOL RTCi_ReadRawDateTimeAsync(void) is RtcSendPxiCommand(0x10)
 * (RTC_PXI_COMMAND_READ_DATETIME).  func_02088F2C is matched C,
 * BOOL (u32 command), and its result is returned unchanged.
 */

#include "asmsdk.h"

extern BOOL func_02088F2C(u32 command);

BOOL RTCi_ReadRawDateTimeAsync(void)
{
    return func_02088F2C(0x10u);
}
