/*
 * sdk/plat_thread.c -- mutex, condition variable and thread start, for
 * Win32 and for POSIX.
 *
 * The one platform file of the OS layer (sdk/plat_thread.h says why it
 * exists).  Win32 uses CRITICAL_SECTION and CONDITION_VARIABLE (Vista and
 * later) and CreateThread; everything else uses pthreads.  C11 <threads.h>
 * would cover both, but MSVC and older macOS SDKs do not ship it, so the two
 * native APIs are the portable choice.
 */

#include <stdlib.h>

#include "plat_thread.h"
#include "port.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct BwMutex  { CRITICAL_SECTION cs; };
struct BwCond   { CONDITION_VARIABLE cv; };
struct BwThread { HANDLE h; void (*fn)(void *); void *arg; };

BwMutex *bw_mutex_create(void)
{
    BwMutex *m = (BwMutex *)malloc(sizeof(*m));
    if (m == NULL) {
        bw_panic("bw_mutex_create", 0, "out of memory");
    }
    InitializeCriticalSection(&m->cs);
    return m;
}

void bw_mutex_destroy(BwMutex *m)
{
    if (m != NULL) {
        DeleteCriticalSection(&m->cs);
        free(m);
    }
}

void bw_mutex_lock(BwMutex *m)   { EnterCriticalSection(&m->cs); }
void bw_mutex_unlock(BwMutex *m) { LeaveCriticalSection(&m->cs); }

BwCond *bw_cond_create(void)
{
    BwCond *c = (BwCond *)malloc(sizeof(*c));
    if (c == NULL) {
        bw_panic("bw_cond_create", 0, "out of memory");
    }
    InitializeConditionVariable(&c->cv);
    return c;
}

void bw_cond_destroy(BwCond *c)
{
    free(c);   /* a Win32 condition variable owns no resources */
}

void bw_cond_wait(BwCond *c, BwMutex *m)
{
    SleepConditionVariableCS(&c->cv, &m->cs, INFINITE);
}

void bw_cond_broadcast(BwCond *c)
{
    WakeAllConditionVariable(&c->cv);
}

static DWORD WINAPI bw_thread_entry(LPVOID p)
{
    BwThread *t = (BwThread *)p;
    ULONG guarantee = 64 * 1024;    /* the crash report's room on an overflow */

    SetThreadStackGuarantee(&guarantee);
    t->fn(t->arg);
    return 0;
}

BwThread *bw_thread_start(void (*fn)(void *arg), void *arg)
{
    BwThread *t = (BwThread *)malloc(sizeof(*t));
    if (t == NULL) {
        bw_panic("bw_thread_start", 0, "out of memory");
    }
    t->fn = fn;
    t->arg = arg;
    t->h = CreateThread(NULL, 0, bw_thread_entry, t, 0, NULL);
    if (t->h == NULL) {
        bw_panic("bw_thread_start", 0, "CreateThread failed");
    }
    return t;
}

void bw_thread_join(BwThread *t)
{
    if (t != NULL) {
        WaitForSingleObject(t->h, INFINITE);
        CloseHandle(t->h);
        free(t);
    }
}

unsigned bw_cpu_count(void)
{
    SYSTEM_INFO si;

    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (unsigned)si.dwNumberOfProcessors : 1u;
}

unsigned long long bw_clock_ns(void)
{
    LARGE_INTEGER f, c;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (unsigned long long)(c.QuadPart / f.QuadPart) * 1000000000ull
           + (unsigned long long)(c.QuadPart % f.QuadPart) * 1000000000ull
                 / (unsigned long long)f.QuadPart;
}

#elif defined(__wasi__)

/* Native handles are opaque 32-bit IDs, never host pointers. The native
 * wasm2c embedder implements this import module and enters callbacks through
 * eidos_thread_dispatch with the correct per-thread shadow stack. */
#define BW_HOST_IMPORT(name) __attribute__((import_module("eidos"), import_name(name)))
BW_HOST_IMPORT("mutex_create") extern u32 bw_host_mutex_create(void);
BW_HOST_IMPORT("mutex_destroy") extern void bw_host_mutex_destroy(u32);
BW_HOST_IMPORT("mutex_lock") extern void bw_host_mutex_lock(u32);
BW_HOST_IMPORT("mutex_unlock") extern void bw_host_mutex_unlock(u32);
BW_HOST_IMPORT("cond_create") extern u32 bw_host_cond_create(void);
BW_HOST_IMPORT("cond_destroy") extern void bw_host_cond_destroy(u32);
BW_HOST_IMPORT("cond_wait") extern void bw_host_cond_wait(u32, u32);
BW_HOST_IMPORT("cond_broadcast") extern void bw_host_cond_broadcast(u32);
BW_HOST_IMPORT("thread_start") extern u32 bw_host_thread_start(u32, u32, u32);
BW_HOST_IMPORT("thread_join") extern void bw_host_thread_join(u32);
BW_HOST_IMPORT("cpu_count") extern u32 bw_host_cpu_count(void);
BW_HOST_IMPORT("clock_ns") extern u64 bw_host_clock_ns(void);

struct BwMutex { u32 handle; };
struct BwCond { u32 handle; };
struct BwThread { u32 handle; void *stack; };

static void *bw_host_handle(u32 handle)
{
    u32 *object;
    if (!handle) bw_panic("wasm host handle", 0, "native host creation failed");
    object = malloc(sizeof(*object));
    if (!object) bw_panic("wasm host handle", 0, "out of memory");
    *object = handle;
    return object;
}

BwMutex *bw_mutex_create(void) { return bw_host_handle(bw_host_mutex_create()); }
void bw_mutex_destroy(BwMutex *m) { if (m) { bw_host_mutex_destroy(m->handle); free(m); } }
void bw_mutex_lock(BwMutex *m) { bw_host_mutex_lock(m->handle); }
void bw_mutex_unlock(BwMutex *m) { bw_host_mutex_unlock(m->handle); }
BwCond *bw_cond_create(void) { return bw_host_handle(bw_host_cond_create()); }
void bw_cond_destroy(BwCond *c) { if (c) { bw_host_cond_destroy(c->handle); free(c); } }
void bw_cond_wait(BwCond *c, BwMutex *m) { bw_host_cond_wait(c->handle, m->handle); }
void bw_cond_broadcast(BwCond *c) { bw_host_cond_broadcast(c->handle); }
BwThread *bw_thread_start(void (*fn)(void *), void *arg)
{
    /* The DS stacks remain game-visible (including SDK canaries), but C
     * shims need room beyond the DS compiler's stack budget. Each native
     * worker owns a separate aligned wasm shadow stack until it is joined. */
    const u32 size = 1024u * 1024u;
    BwThread *t = malloc(sizeof(*t));
    if (!t) bw_panic("wasm thread", 0, "out of memory");
    t->stack = malloc(size + 15u);
    if (!t->stack) bw_panic("wasm thread stack", 0, "out of memory");
    t->handle = bw_host_thread_start((u32)(uintptr_t)fn, (u32)(uintptr_t)arg,
        ((u32)(uintptr_t)t->stack + size) & ~15u);
    if (!t->handle) bw_panic("wasm thread", 0, "native thread creation failed");
    return t;
}
void bw_thread_join(BwThread *t)
{
    if (t) { bw_host_thread_join(t->handle); free(t->stack); free(t); }
}
unsigned bw_cpu_count(void) { u32 n = bw_host_cpu_count(); return n ? n : 1; }
unsigned long long bw_clock_ns(void) { return bw_host_clock_ns(); }

__attribute__((export_name("eidos_thread_dispatch")))
void bw_wasm_thread_dispatch(u32 callback, u32 arg)
{
    ((void (*)(void *))(uintptr_t)callback)((void *)(uintptr_t)arg);
}

#else /* POSIX */

#include <pthread.h>
#include <unistd.h>
#include <time.h>

struct BwMutex  { pthread_mutex_t mx; };
struct BwCond   { pthread_cond_t cv; };
struct BwThread { pthread_t th; void (*fn)(void *); void *arg; };

BwMutex *bw_mutex_create(void)
{
    BwMutex *m = (BwMutex *)malloc(sizeof(*m));
    if (m == NULL || pthread_mutex_init(&m->mx, NULL) != 0) {
        bw_panic("bw_mutex_create", 0, "pthread_mutex_init failed");
    }
    return m;
}

void bw_mutex_destroy(BwMutex *m)
{
    if (m != NULL) {
        pthread_mutex_destroy(&m->mx);
        free(m);
    }
}

void bw_mutex_lock(BwMutex *m)   { pthread_mutex_lock(&m->mx); }
void bw_mutex_unlock(BwMutex *m) { pthread_mutex_unlock(&m->mx); }

BwCond *bw_cond_create(void)
{
    BwCond *c = (BwCond *)malloc(sizeof(*c));
    if (c == NULL || pthread_cond_init(&c->cv, NULL) != 0) {
        bw_panic("bw_cond_create", 0, "pthread_cond_init failed");
    }
    return c;
}

void bw_cond_destroy(BwCond *c)
{
    if (c != NULL) {
        pthread_cond_destroy(&c->cv);
        free(c);
    }
}

void bw_cond_wait(BwCond *c, BwMutex *m)
{
    pthread_cond_wait(&c->cv, &m->mx);
}

void bw_cond_broadcast(BwCond *c)
{
    pthread_cond_broadcast(&c->cv);
}

static void *bw_thread_entry(void *p)
{
    BwThread *t = (BwThread *)p;
    t->fn(t->arg);
    return NULL;
}

BwThread *bw_thread_start(void (*fn)(void *arg), void *arg)
{
    BwThread *t = (BwThread *)malloc(sizeof(*t));
    if (t == NULL) {
        bw_panic("bw_thread_start", 0, "out of memory");
    }
    t->fn = fn;
    t->arg = arg;
    if (pthread_create(&t->th, NULL, bw_thread_entry, t) != 0) {
        bw_panic("bw_thread_start", 0, "pthread_create failed");
    }
    return t;
}

void bw_thread_join(BwThread *t)
{
    if (t != NULL) {
        pthread_join(t->th, NULL);
        free(t);
    }
}

unsigned bw_cpu_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);

    return n > 0 ? (unsigned)n : 1u;
}

unsigned long long bw_clock_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull
           + (unsigned long long)ts.tv_nsec;
}

#endif
