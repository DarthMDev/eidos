/*
 * cmake/wamr/wasi/platform_internal.h -- WAMR's platform types for the
 * wasm32 build (shared_platform.cmake).  The runtime runs on the module's
 * fibers, one at a time (sdk/plat_thread.c), so a mutex has nothing to
 * guard and a thread is the fiber that called in.
 *
 * No <time.h> here, nor anything else that brings in WASI's own
 * <wasi/api.h>: WAMR declares the same WASI types itself
 * (platform_wasi_types.h), with other base types.
 */

#ifndef _PLATFORM_INTERNAL_H
#define _PLATFORM_INTERNAL_H

/* <wasi/api.h> stays out (its guard defined), and the errno values it
 * would give <errno.h> come from shared_platform.cmake's copy. */
#define __wasi_api_h
#include "eidos_wasi_errno.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BH_PLATFORM_EIDOS_WASI
#define BH_PLATFORM_EIDOS_WASI
#endif

#define BH_APPLET_PRESERVED_STACK_SIZE (2 * BH_KB)
#define BH_THREAD_DEFAULT_PRIORITY 0

typedef int korp_thread;
typedef int korp_tid;
typedef int korp_mutex;
typedef int korp_sem;
typedef struct { int dummy; } korp_rwlock;
typedef struct { int dummy; } korp_cond;

#define os_printf printf
#define os_vprintf vprintf

typedef int os_file_handle;
typedef void *os_dir_stream;
typedef int os_raw_file_handle;

/* the page os_mmap rounds to: the heap has none, any small size will do */
static inline int
os_getpagesize(void)
{
    return 4096;
}

static inline os_file_handle
os_get_invalid_handle(void)
{
    return -1;
}

#endif
