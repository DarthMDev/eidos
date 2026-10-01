/*
 * sdk/boot/os.c -- OS functions of the boot whose matched C does address
 * arithmetic on a converted pointer (sdk/boot/gx.c says why sdk/boot/
 * exists).
 *
 * OS_InitException (os_exception.o, 0x02086DD8).  The matched C
 * (src/matched/arm9_sdk/func_02086DD8.c) installs the exception vector in
 * the shared work area at DS 0x02FFFD9C and, on NITRO, in its mirror 8 MiB
 * lower, which it reaches as `(u32)vec & ~0x800000` with `vec` a pointer
 * to 0x02FFFD9C.  On the host `vec` is a host address and the mask makes a
 * wild pointer of it, so the port does the same stores by DS address:
 * 0x02FFFD9C and 0x027FFD9C, the two views the console's 4 MiB of main RAM
 * mirror; the port's main RAM is flat 16 MiB (sdk/dsmem.h), so both are
 * written.  The stored value is the handler's host address (the DS stores
 * its own); nothing on the host jumps through it.  The debugger hook at the
 * same word is honoured as on the DS: a value inside 0x02600000-0x027FFFFF
 * is kept as the debugger's handler and the vector left alone.  The
 * exception state (os_exception.o's .bss at 0x02151290) is ARM9 data,
 * reached by its DS address.  cmake/boot.cmake lists the matched file as
 * replaced.
 */

#include <stdint.h>

#include "../dsmem.h"
#include "../port.h"

#define OS_EXCEPTION_VECTOR      0x02FFFD9Cu
#define OS_EXCEPTION_VECTOR_MIRR 0x027FFD9Cu
#define OS_EXCEPTION_BSS         0x02151290u

int  OS_IsRunOnTwl(void);
void OSi_ExceptionHandler(void);
void OS_InitException(void);

typedef struct BwOsExceptionBss {
    u32 debugger_handler;        /* OSi_DebuggerHandler        */
    u32 user_handler_arg;        /* OSi_UserExceptionHandlerArg */
    u32 user_handler;            /* OSi_UserExceptionHandler    */
} BwOsExceptionBss;

void OS_InitException(void)
{
    volatile u32 *vec = (volatile u32 *)bw_ds_ptr(OS_EXCEPTION_VECTOR);
    volatile BwOsExceptionBss *bss =
        (volatile BwOsExceptionBss *)bw_ds_ptr(OS_EXCEPTION_BSS);
    u32 ex = *vec;

    if (!(0x02600000u <= ex && ex < 0x02800000u)) {
        ex = 0;
    }
    bss->debugger_handler = ex;
    if (ex == 0) {
        u32 handler = (u32)(uintptr_t)OSi_ExceptionHandler;

        *vec = handler;
        if (!OS_IsRunOnTwl()) {
            *(volatile u32 *)bw_ds_ptr(OS_EXCEPTION_VECTOR_MIRR) = handler;
        }
    }
    bss->user_handler = 0;
}
