/*
 * sdk/wasm_heap.c -- the C library's heap behind a lock, for the wasm32
 * build only (CMakeLists.txt links it with --wrap for each function).
 *
 * The host program runs the compositors' tasks on threads of its own
 * (host/par.c), and the tasks allocate (engine/g2d/bg.c's per-band plan).
 * The wasm32 C library is built without threads, so its malloc has no lock
 * of its own: the host's lock (sdk/wasm_host.h, heap_lock) guards every
 * call, from the module and from the host alike.
 */

#if defined(__wasm__)

#include <stddef.h>

#include "wasm_host.h"

void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t size);
void *__real_realloc(void *p, size_t n);
void __real_free(void *p);
int __real_posix_memalign(void **out, size_t align, size_t n);
void *__real_aligned_alloc(size_t align, size_t n);

void *__wrap_malloc(size_t n);
void *__wrap_calloc(size_t n, size_t size);
void *__wrap_realloc(void *p, size_t n);
void __wrap_free(void *p);
int __wrap_posix_memalign(void **out, size_t align, size_t n);
void *__wrap_aligned_alloc(size_t align, size_t n);

void *__wrap_malloc(size_t n)
{
    void *p;

    bw_host_heap_lock();
    p = __real_malloc(n);
    bw_host_heap_unlock();
    return p;
}

void *__wrap_calloc(size_t n, size_t size)
{
    void *p;

    bw_host_heap_lock();
    p = __real_calloc(n, size);
    bw_host_heap_unlock();
    return p;
}

void *__wrap_realloc(void *p, size_t n)
{
    void *q;

    bw_host_heap_lock();
    q = __real_realloc(p, n);
    bw_host_heap_unlock();
    return q;
}

void __wrap_free(void *p)
{
    bw_host_heap_lock();
    __real_free(p);
    bw_host_heap_unlock();
}

int __wrap_posix_memalign(void **out, size_t align, size_t n)
{
    int rc;

    bw_host_heap_lock();
    rc = __real_posix_memalign(out, align, n);
    bw_host_heap_unlock();
    return rc;
}

void *__wrap_aligned_alloc(size_t align, size_t n)
{
    void *p;

    bw_host_heap_lock();
    p = __real_aligned_alloc(align, n);
    bw_host_heap_unlock();
    return p;
}

#endif /* __wasm__ */

#if defined(__wasm__)

#include <time.h>

#include "plat_thread.h"

/*
 * The C library's sleep, likewise wrapped (CMakeLists.txt): WASI's sleep
 * stops the one host thread, so every other fiber would stop with it (a
 * network reader the sleeper waits for, say).  The fiber sleeps instead,
 * and the others run.
 */
int __wrap_nanosleep(const struct timespec *req, struct timespec *rem);

int __wrap_nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (req == NULL || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
        return -1;
    }
    bw_thread_sleep_ns((unsigned long long)req->tv_sec * 1000000000ull
                       + (unsigned long long)req->tv_nsec);
    if (rem != NULL) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

#endif /* __wasm__ */
