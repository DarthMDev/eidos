/*
 * sdk/asm/OS_UnlockMutex.c -- OS_UnlockMutex at 0x02086078.
 *
 * Replaces src/matched/arm9_sdk/OS_UnlockMutex.c (TwlSDK os_mutex.o, an asm
 * tail call):
 *
 *     ldr ip, =func_02086164; mov r1, #0x10000000; bx ip
 *
 * void OS_UnlockMutex(OSMutex *mutex) is OSi_UnlockMutexCore(mutex,
 * OS_MUTEX_TYPE_STD): the mutex passes through, r1 = 0x10000000.
 * func_02086164 is matched C, void (OSMutex *mutex, u32 type).  Matched
 * callers use both spellings; most declare func_02086078 with a void *
 * argument, so the address spelling takes void * and forwards.
 */

#include "asmsdk.h"

typedef struct OSMutex OSMutex;

extern void func_02086164(OSMutex *mutex, u32 type);

void OS_UnlockMutex(OSMutex *mutex)
{
    func_02086164(mutex, 0x10000000u);
}

void func_02086078(void *mutex)
{
    OS_UnlockMutex((OSMutex *)mutex);
}
