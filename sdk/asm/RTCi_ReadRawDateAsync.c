/*
 * sdk/asm/RTCi_ReadRawDateAsync.c -- RTCi_ReadRawDateAsync at 0x02088EFC.
 *
 * Replaces src/matched/arm9_sdk/RTCi_ReadRawDateAsync.c (TwlSDK librtc
 * internal.o, an asm tail call):
 *
 *     ldr ip, =func_02088F2C; mov r0, #0x11; bx ip
 *
 * BOOL RTCi_ReadRawDateAsync(void) is RtcSendPxiCommand(0x11)
 * (RTC_PXI_COMMAND_READ_DATE).  func_02088F2C is matched C,
 * BOOL (u32 command), and its result is returned unchanged.
 */

#include "asmsdk.h"

extern BOOL func_02088F2C(u32 command);

BOOL RTCi_ReadRawDateAsync(void)
{
    return func_02088F2C(0x11u);
}
