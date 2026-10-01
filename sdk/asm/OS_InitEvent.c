/*
 * sdk/asm/OS_InitEvent.c -- OS_InitEvent at 0x020882DC.
 *
 * Replaces src/matched/arm9_sdk/OS_InitEvent.c (TwlSDK os_event.o, an asm
 * function):
 *
 *     mov r1, #0
 *     str r1, [r0, #8]        queue.tail = NULL
 *     str r1, [r0, #4]        queue.head = NULL
 *     str r1, [r0]            flag = 0
 *     bx  lr
 *
 * void OS_InitEvent(OSEvent *event).  The asm clears the 12 bytes of the
 * event.  The matched callers (func_02075C38.c, func_02075E10.c) declare
 * OSEvent as an opaque u8 data[0xC], so the host clears exactly those 12
 * bytes, with memset because the caller's object is a byte array.  (A host
 * that gave OSEvent the SDK's pointer-bearing layout would need more; no
 * matched code does.)
 */

#include <string.h>

#include "asmsdk.h"

void OS_InitEvent(struct OSEvent *event)
{
    memset((void *)event, 0, 0xC);
}
