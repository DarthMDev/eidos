#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "host_thread.h"
#include "wasm-rt.h"
#include "wasm-rt-exceptions.h"
#include "wasm-rt-impl.h"

#if defined(__APPLE__) && !defined(WASM_RT_C11_AVAILABLE)
#error "The threaded wasm2c host requires C11 thread-local runtime state on macOS"
#endif

#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

typedef struct w2c_eidos Host;
enum { MUTEX = 1, COND, THREAD };
typedef struct {
    pthread_t thread;
    Host *host;
    uint32_t callback, arg, stack;
} Worker;

static WASM_RT_THREAD_LOCAL uint32_t thread_stack;
static WASM_RT_THREAD_LOCAL jmp_buf *thread_unwind;

static void fail(const char *operation)
{
    fprintf(stderr, "eidos wasm2c host: %s\n", operation);
    abort();
}

static uint32_t put(Host *h, void *object, unsigned kind)
{
    uint32_t i;
    if (!object) fail("out of memory");
    for (i = 1; i < EIDOS_HOST_HANDLES; ++i) {
        if (!h->handles[i].object) {
            h->handles[i].object = object;
            h->handles[i].kind = kind;
            return i;
        }
    }
    fail("native handle table full");
    return 0;
}

static void *get(Host *h, uint32_t id, unsigned kind)
{
    if (!id || id >= EIDOS_HOST_HANDLES || !h->handles[id].object
            || h->handles[id].kind != kind) fail("invalid native handle");
    return h->handles[id].object;
}

static void drop(Host *h, uint32_t id)
{
    free(h->handles[id].object);
    h->handles[id].object = NULL;
    h->handles[id].kind = 0;
}

void eidos_host_threads_init(Host *h, void *guest, uint32_t *sp,
                            void (*dispatch)(void *, uint32_t, uint32_t))
{
    unsigned i;
    h->guest = guest;
    h->stack_pointer = sp;
    h->dispatch = dispatch;
    memset(h->handles, 0, sizeof(h->handles));
    for (i = 0; i < 256; ++i) h->sockets[i] = -1;
    if (pthread_mutex_init(&h->execution, NULL)) fail("execution mutex init");
    thread_stack = *sp;
}

void eidos_host_enter(Host *h)
{
    if (pthread_mutex_lock(&h->execution)) fail("execution mutex lock");
    *h->stack_pointer = thread_stack;
}

void eidos_host_leave(Host *h)
{
    eidos_host_watchdog_sample(h);
    thread_stack = *h->stack_pointer;
    if (pthread_mutex_unlock(&h->execution)) fail("execution mutex unlock");
}

void eidos_host_threads_destroy(Host *h)
{
    unsigned i;
    eidos_host_watchdog_stop(h);
    for (i = 0; i < 256; ++i) if (h->sockets[i] >= 0) close(h->sockets[i]);
    for (i = 1; i < EIDOS_HOST_HANDLES; ++i) {
        if (h->handles[i].kind == THREAD) fail("worker still unjoined at shutdown");
        if (h->handles[i].kind == MUTEX) {
            if (pthread_mutex_destroy(h->handles[i].object)) fail("live mutex at shutdown");
        } else if (h->handles[i].kind == COND) {
            if (pthread_cond_destroy(h->handles[i].object)) fail("live condition at shutdown");
        }
        if (h->handles[i].object) drop(h, i);
    }
    if (pthread_mutex_destroy(&h->execution)) fail("execution mutex destroy");
}

void *eidos_host_memory(Host *h, uint32_t offset, uint32_t size)
{
    if ((uint64_t)offset + size > h->memory->size) wasm_rt_trap(WASM_RT_TRAP_OOB);
    return h->memory->data + offset;
}

void w2c_eidos_sleep_ms(Host *h, uint32_t ms)
{
    struct timespec t = { ms / 1000u, (long)(ms % 1000u) * 1000000L };
    eidos_host_leave(h);
    while (nanosleep(&t, &t) && errno == EINTR) { }
    eidos_host_enter(h);
}

uint32_t w2c_eidos_mutex_create(Host *h)
{
    pthread_mutex_t *m = malloc(sizeof(*m));
    if (!m || pthread_mutex_init(m, NULL)) fail("mutex init");
    return put(h, m, MUTEX);
}

void w2c_eidos_mutex_destroy(Host *h, uint32_t id)
{
    if (pthread_mutex_destroy(get(h, id, MUTEX))) fail("mutex destroy");
    drop(h, id);
}

void w2c_eidos_mutex_lock(Host *h, uint32_t id)
{
    pthread_mutex_t *m = get(h, id, MUTEX);
    /* Never wait for a native lock while holding the guest execution gate.
     * A waiter waking from cond_wait owns m before it can enter the guest. */
    eidos_host_leave(h);
    if (pthread_mutex_lock(m)) fail("mutex lock");
    eidos_host_enter(h);
}

void w2c_eidos_mutex_unlock(Host *h, uint32_t id)
{
    if (pthread_mutex_unlock(get(h, id, MUTEX))) fail("mutex unlock");
}

uint32_t w2c_eidos_cond_create(Host *h)
{
    pthread_cond_t *c = malloc(sizeof(*c));
    if (!c || pthread_cond_init(c, NULL)) fail("condition init");
    return put(h, c, COND);
}

void w2c_eidos_cond_destroy(Host *h, uint32_t id)
{
    if (pthread_cond_destroy(get(h, id, COND))) fail("condition destroy");
    drop(h, id);
}

void w2c_eidos_cond_wait(Host *h, uint32_t cv, uint32_t mx)
{
    pthread_cond_t *c = get(h, cv, COND);
    pthread_mutex_t *m = get(h, mx, MUTEX);
    eidos_host_leave(h);
    if (pthread_cond_wait(c, m)) fail("condition wait");
    eidos_host_enter(h);
}

void w2c_eidos_cond_broadcast(Host *h, uint32_t id)
{
    if (pthread_cond_broadcast(get(h, id, COND))) fail("condition broadcast");
}

static void *worker_main(void *arg)
{
    Worker *w = arg;
    wasm_rt_trap_t trap;
    wasm_rt_init_thread();
    thread_stack = w->stack;
    eidos_host_enter(w->host);
    trap = wasm_rt_impl_try();
    if (trap != WASM_RT_TRAP_NONE) {
        fprintf(stderr, "eidos worker trap: %s\n", wasm_rt_strerror(trap));
        exit(3);
    }
    w->host->dispatch(w->host->guest, w->callback, w->arg);
    eidos_host_leave(w->host);
    wasm_rt_free_thread();
    return NULL;
}

uint32_t w2c_eidos_thread_start(Host *h, uint32_t fn, uint32_t arg, uint32_t stack)
{
    Worker *w = malloc(sizeof(*w));
    uint32_t id = put(h, w, THREAD);
    w->host = h;
    w->callback = fn;
    w->arg = arg;
    w->stack = stack;
    if (pthread_create(&w->thread, NULL, worker_main, w)) fail("thread start");
    return id;
}

void w2c_eidos_thread_join(Host *h, uint32_t id)
{
    Worker *w = get(h, id, THREAD);
    eidos_host_leave(h);
    if (pthread_join(w->thread, NULL)) fail("thread join");
    eidos_host_enter(h);
    drop(h, id);
}

void w2c_eidos_thread_run(Host *h, uint32_t fn, uint32_t arg)
{
    jmp_buf target;
    jmp_buf *previous = thread_unwind;
    uint32_t sp = *h->stack_pointer;
#if WASM_RT_STACK_DEPTH_COUNT
    uint32_t depth = wasm_rt_call_stack_depth;
#endif
    thread_unwind = &target;
    if (setjmp(target) == 0) h->dispatch(h->guest, fn, arg);
    /* A native longjmp skips generated epilogues. Restore both stacks before
     * returning into generated C, and keep nested protected calls valid. */
    *h->stack_pointer = sp;
#if WASM_RT_STACK_DEPTH_COUNT
    wasm_rt_call_stack_depth = depth;
#endif
    thread_unwind = previous;
}

void w2c_eidos_thread_unwind(Host *h)
{
    (void)h;
    if (!thread_unwind) fail("thread unwind outside protected call");
    longjmp(*thread_unwind, 1);
}

uint32_t w2c_eidos_cpu_count(Host *h)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    (void)h;
    return n > 0 ? (uint32_t)n : 1;
}

uint64_t w2c_eidos_clock_ns(Host *h)
{
    struct timespec t;
    eidos_host_watchdog_sample(h);
    if (clock_gettime(CLOCK_MONOTONIC, &t)) fail("monotonic clock");
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}
