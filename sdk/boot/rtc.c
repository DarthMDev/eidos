/*
 * sdk/boot/rtc.c -- RTCEXi_CommonCallback, the DSi RTC extension's reply
 * handler, for the boot.
 *
 * The RTC module's common callback (src/matched/arm9_sdk/RtcCommonCallback.c)
 * hands an unknown command to RTCEXi_CommonCallback only when
 * OS_IsRunOnTwl() says the console is a DSi.  That function lives in the
 * TWL-only ARM9i block (0x02700000), which a NITRO console never loads and
 * aletheia does not match.  The port runs the NITRO boot, so reaching it is
 * a port bug, reported as such.
 */

#include "../port.h"

int RTCEXi_CommonCallback(void);

int RTCEXi_CommonCallback(void)
{
    bw_panic("RTCEXi_CommonCallback", 0x02700000u,
             "the DSi RTC extension (ARM9i block); the NITRO boot never loads it");
    return 0;
}
