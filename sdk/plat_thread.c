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
