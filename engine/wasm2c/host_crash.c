#define _POSIX_C_SOURCE 200809L
#include "host_thread.h"
#include "wasm-rt-exceptions.h"
#include "wasm-rt-impl.h"
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void w2c_eidos_crash_install(struct w2c_eidos *h)
{
    /* WABT installs the signal handlers needed for guarded linear memory.
     * The main and worker trampolines already report every guest trap. */
    (void)h;
}

void w2c_eidos_backtrace(struct w2c_eidos *h)
{
    void *frames[48];
    int n = backtrace(frames, 48);
    (void)h;
    backtrace_symbols_fd(frames, n, 2);
}

static void *watchdog_main(void *arg)
{
    struct w2c_eidos *h = arg;
    uint64_t last = 0;
    unsigned still = 0;
    while (!__atomic_load_n(&h->watchdog_stop, __ATOMIC_ACQUIRE)) {
        struct timespec t = { 1, 0 };
        nanosleep(&t, NULL);
        if (__atomic_load_n(&h->watchdog_stop, __ATOMIC_ACQUIRE)) break;
        {
            uint64_t now = __atomic_load_n(&h->watchdog_progress, __ATOMIC_ACQUIRE);
            if (now != last) { last = now; still = 0; }
            else ++still;
        }
        if (still >= h->watchdog_seconds) {
            fprintf(stderr, "WATCHDOG: no progress for %u s (at %llu)\n",
                    h->watchdog_seconds, (unsigned long long)last);
            /* A stalled guest may own the execution gate: never enter it
             * asynchronously to ask for diagnostics or read its memory. */
            exit(3);
        }
    }
    return NULL;
}

void eidos_host_watchdog_sample(struct w2c_eidos *h)
{
    /* Only the execution-gate holder enters the guest. The monitor reads
     * this native snapshot, so even a guest stuck with the gate held can be
     * reported without asynchronously touching its state. */
    static WASM_RT_THREAD_LOCAL int sampling;
    if (h->watchdog_running && !sampling) {
        uint64_t now;
        sampling = 1;
        now = h->progress(h->guest, h->watchdog_callback);
        sampling = 0;
        __atomic_store_n(&h->watchdog_progress, now, __ATOMIC_RELEASE);
    }
}

void w2c_eidos_watchdog(struct w2c_eidos *h, uint32_t seconds, uint32_t callback)
{
    if (!seconds || h->watchdog_running) return;
    h->watchdog_seconds = seconds;
    h->watchdog_callback = callback;
    __atomic_store_n(&h->watchdog_stop, 0, __ATOMIC_RELEASE);
    if (pthread_create(&h->watchdog, NULL, watchdog_main, h)) abort();
    h->watchdog_running = 1;
}

void eidos_host_watchdog_stop(struct w2c_eidos *h)
{
    if (!h->watchdog_running) return;
    __atomic_store_n(&h->watchdog_stop, 1, __ATOMIC_RELEASE);
    pthread_join(h->watchdog, NULL);
    h->watchdog_running = 0;
}
