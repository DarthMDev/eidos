/*
 * sdk/asm/func_02030BA4.c -- 0x02030BA4.
 *
 * Replaces src/matched/arm9_sdk/func_02030BA4.c (an asm function):
 *
 *     stmfd sp!, {r3, lr}
 *     bl func_02030534          ; arguments passed through
 *     cmp r0, #0
 *     ldmnefd sp!, {r3, pc}     ; non-zero: return it
 *     bl func_02088234          ; OS_Terminate, does not return
 *     ldmfd sp!, {r3, pc}
 *
 * func_02030534 is the alias of the matched sub_02030534
 * (BOOL sub_02030534(u32 size)), func_02088234 is OS_Terminate; the C
 * calls the canonical names so it needs no alias.  The caller declares it
 * void (u32); the value is returned for completeness.
 */

#include "asmsdk.h"

extern BOOL sub_02030534(u32 size);
extern void OS_Terminate(void);

BOOL func_02030BA4(u32 size)
{
    BOOL ok = sub_02030534(size);

    if (ok != 0) {
        return ok;
    }
    OS_Terminate();
    return 0;
}
