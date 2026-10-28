/* Native imports for sdk/plat_thread.c. One instance, serialized guest
 * execution, independent shadow stacks and unwind targets per host thread. */
#ifndef EIDOS_W2C_HOST_THREAD_H
#define EIDOS_W2C_HOST_THREAD_H

#include <stdint.h>
#include <pthread.h>
#include "wasm-rt.h"

enum { EIDOS_HOST_HANDLES = 4096 };
struct w2c_eidos {
    void *guest;
    uint32_t *stack_pointer;
    void (*dispatch)(void *, uint32_t, uint32_t);
    uint64_t (*progress)(void *, uint32_t);
    void (*context)(void *);
    wasm_rt_memory_t *memory;
    pthread_mutex_t execution;
    struct { void *object; unsigned kind; } handles[EIDOS_HOST_HANDLES];
    int sockets[256];
    pthread_t watchdog;
    unsigned watchdog_seconds, watchdog_callback;
    uint64_t watchdog_progress;
    int watchdog_running, watchdog_stop;
};

/* Initialize after instantiation, before calling guest code. All guest
 * entries must be bracketed by enter/leave, including on the main thread. */
void eidos_host_threads_init(struct w2c_eidos *, void *, uint32_t *,
                            void (*)(void *, uint32_t, uint32_t));
void eidos_host_enter(struct w2c_eidos *);
void eidos_host_leave(struct w2c_eidos *);
/* All workers must have been joined before destroy. */
void eidos_host_threads_destroy(struct w2c_eidos *);
void eidos_host_watchdog_stop(struct w2c_eidos *);
void eidos_host_watchdog_sample(struct w2c_eidos *);
/* Bounds-checked translation for all non-WASI guest buffers. */
void *eidos_host_memory(struct w2c_eidos *, uint32_t, uint32_t);

#endif
