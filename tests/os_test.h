/*
 * tests/os_test.h -- what the OS tests share: the SDK prototypes the shims
 * define (os.h deliberately does not declare them, because matched files
 * declare them with their own spellings) and a boot-shaped setup.
 */

#ifndef BW_TEST_OS_TEST_H
#define BW_TEST_OS_TEST_H

#include "dsmem.h"
#include "frame.h"
#include "os.h"
#include "port.h"

/* sdk/os_irq.c */
u32  OS_DisableInterrupts(void);
u32  OS_EnableInterrupts(void);
u32  OS_RestoreInterrupts(u32 state);
u32  OS_GetProcMode(void);

/* sdk/os_interrupt.c */
void OS_Halt(void);
void OS_WaitIrq(int clear, u32 irqFlags);
void OS_WaitVBlankIntr(void);
void OS_WaitInterrupt(int clear, u32 irqFlags);
u32  OS_ResetRequestIrqMask(u32 intr);

/* aletheia's matched OS_SetIrqFunction.c / OS_GetIrqFunction.c */
void          OS_SetIrqFunction(unsigned long intrBit, OSIrqFunction function);
OSIrqFunction OS_GetIrqFunction(unsigned long intrBit);

/* sdk/os_thread.c */
void      OS_InitThread(void);
void      OS_CreateThread(OSThread *thread, void (*func)(void *), void *arg,
                          void *stack, u32 stackSize, u32 prio);
void      OS_ExitThread(void);
void      OS_DestroyThread(OSThread *thread);
void      OS_KillThread(OSThread *thread, void *arg);
void      OS_JoinThread(OSThread *thread);
int       OS_IsThreadTerminated(const OSThread *thread);
void      OS_SleepThread(OSThreadQueue *queue);
void      OS_WakeupThread(OSThreadQueue *queue);
void      OS_WakeupThreadDirect(OSThread *thread);
void      OS_YieldThread(void);
void      OS_RescheduleThread(void);
int       OS_SetThreadPriority(OSThread *thread, u32 prio);
u32       OS_DisableScheduler(void);
u32       OS_EnableScheduler(void);
void      OS_Sleep(u32 msec);
OSSwitchThreadCallback OS_SetSwitchThreadCallback(OSSwitchThreadCallback cb);

/* sdk/os_tick.c, sdk/os_alarm.c */
void OS_InitTick(void);
u64  OS_GetTick(void);
void OS_InitAlarm(void);
void OS_CreateAlarm(OSAlarm *alarm);
void OS_SetAlarm(OSAlarm *alarm, u64 tick, OSAlarmHandler handler, void *arg);
void OS_SetPeriodicAlarm(OSAlarm *alarm, u64 start, u64 period,
                         OSAlarmHandler handler, void *arg);
void OS_CancelAlarm(OSAlarm *alarm);

static EidosFrameLoop g_os_test_loop;

/* What NitroMain's system initialiser does, minus everything not OS. */
static void os_test_boot(void)
{
    bw_port_reset();
    eidos_frame_init(&g_os_test_loop, NULL, NULL, NULL);
    eidos_frame_set_current(&g_os_test_loop);
    OS_InitThread();
    OS_InitTick();
    OS_InitAlarm();
    bw_io_write16(BW_REG_IME, 1);
}

#endif /* BW_TEST_OS_TEST_H */
