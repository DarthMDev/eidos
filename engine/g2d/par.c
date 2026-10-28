/*
 * engine/g2d/par.c -- a small persistent worker pool for the compositors.
 *
 * The 2D engines compose independent rows and the two screens are
 * independent of each other, so a frame's compose work splits cleanly.  A
 * thread started per job costs more than a small compose, so the workers are
 * started once and fed from a queue.  g2d_par_run may be called from several
 * threads at once (the jobs of one call can themselves call it): the caller
 * runs queued tasks too while it waits, so nothing deadlocks when the queue
 * is deeper than the pool.
 */

#include <stdlib.h>
#include <string.h>

#include "g2d.h"
#include "plat_thread.h"

#if defined(__wasm__)

/*
 * The wasm32 build: the game's threads are fibers of one host thread, so a
 * pool of them would run its tasks one after another.  The host program
 * runs the tasks on its own threads instead (sdk/wasm_host.h, par_run;
 * host/par.c), each calling back in through bw_par_task.  A call from one
 * of those threads (a task that splits again) runs its tasks in place.
 */
#include <stdint.h>

#include "wasm_host.h"

__attribute__((export_name("bw_par_task")))
void bw_par_task(unsigned fn, void *ctx, unsigned index)
{
    ((void (*)(void *, unsigned))(uintptr_t)fn)(ctx, index);
}

unsigned g2d_par_workers(void)
{
    return bw_host_par_workers();
}

void g2d_par_run(unsigned n, void (*fn)(void *ctx, unsigned index), void *ctx)
{
    unsigned i;

    if (n > 1u && bw_host_par_workers() > 0u) {
        bw_host_par_run((unsigned)(uintptr_t)fn, ctx, n);
        return;
    }
    for (i = 0; i < n; i++) {
        fn(ctx, i);
    }
}

#else /* a native build: the pool below */

#define PAR_MAX_WORKERS 7u
#define PAR_QUEUE 256u

typedef struct ParTask {
    void (*fn)(void *ctx, unsigned index);
    void *ctx;
    unsigned index;
    unsigned *pending;      /* the caller's counter */
} ParTask;

static BwMutex *g_m;
static BwCond *g_work;      /* a task was queued, or stop */
static BwCond *g_done;      /* a task finished */
static ParTask g_q[PAR_QUEUE];
static unsigned g_head, g_count;
static BwThread *g_workers[PAR_MAX_WORKERS];
static unsigned g_n_workers;
static int g_stop;
static int g_started;

/* Take one task if there is one; the mutex is held. */
static int pop(ParTask *out)
{
    if (g_count == 0) {
        return 0;
    }
    *out = g_q[g_head];
    g_head = (g_head + 1u) % PAR_QUEUE;
    g_count--;
    return 1;
}

static void run_task(const ParTask *t)
{
    t->fn(t->ctx, t->index);
    bw_mutex_lock(g_m);
    (*t->pending)--;
    bw_cond_broadcast(g_done);
    bw_mutex_unlock(g_m);
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        ParTask t;

        bw_mutex_lock(g_m);
        while (!g_stop && g_count == 0) {
            bw_cond_wait(g_work, g_m);
        }
        if (g_stop) {
            bw_mutex_unlock(g_m);
            return;
        }
        pop(&t);
        bw_mutex_unlock(g_m);
        run_task(&t);
    }
}

static void start_pool(void)
{
    unsigned cpus = bw_cpu_count();
    unsigned n, i;

    g_started = 1;
    n = cpus > 1u ? cpus - 1u : 0u;
    if (n > PAR_MAX_WORKERS) {
        n = PAR_MAX_WORKERS;
    }
    if (n == 0u) {
        return;
    }
    g_m = bw_mutex_create();
    g_work = bw_cond_create();
    g_done = bw_cond_create();
    for (i = 0; i < n; i++) {
        g_workers[i] = bw_thread_start(worker, NULL);
    }
    g_n_workers = n;
}

/* Where the pool is started: once, from whichever thread asks first (the
 * frame thread, before any worker exists). */
static void ensure_pool(void)
{
    if (!g_started) {
        start_pool();
    }
}

unsigned g2d_par_workers(void)
{
    ensure_pool();
    return g_n_workers;
}

void g2d_par_run(unsigned n, void (*fn)(void *ctx, unsigned index), void *ctx)
{
    unsigned pending, i, inline_from;

    if (n == 0) {
        return;
    }
    ensure_pool();
    if (g_n_workers == 0 || n == 1) {
        for (i = 0; i < n; i++) {
            fn(ctx, i);
        }
        return;
    }
    pending = n;
    bw_mutex_lock(g_m);
    for (i = 1; i < n; i++) {            /* task 0 stays with the caller */
        ParTask *t;

        if (g_count == PAR_QUEUE) {
            break;                       /* the rest run here */
        }
        t = &g_q[(g_head + g_count) % PAR_QUEUE];
        t->fn = fn;
        t->ctx = ctx;
        t->index = i;
        t->pending = &pending;
        g_count++;
    }
    inline_from = i;
    bw_cond_broadcast(g_work);
    bw_mutex_unlock(g_m);

    fn(ctx, 0);
    for (i = inline_from; i < n; i++) {
        fn(ctx, i);
    }
    bw_mutex_lock(g_m);
    pending -= 1u + (n - inline_from);   /* task 0 and the inline ones */
    bw_mutex_unlock(g_m);

    /* Help drain the queue, then wait for the tasks other threads hold. */
    for (;;) {
        ParTask t;

        bw_mutex_lock(g_m);
        if (pending == 0) {
            bw_mutex_unlock(g_m);
            break;
        }
        if (pop(&t)) {
            bw_mutex_unlock(g_m);
            run_task(&t);
            continue;
        }
        bw_cond_wait(g_done, g_m);
        bw_mutex_unlock(g_m);
    }
}

#endif /* __wasm__ */
