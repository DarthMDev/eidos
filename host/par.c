/*
 * host/par.c -- the compositors' worker threads for the wasm32 build
 * (sdk/wasm_host.h, par_workers and par_run; engine/g2d/par.c).
 *
 * The module's own threads are fibers of one host thread, so it cannot run
 * work in parallel by itself.  These threads can: each runs the module's
 * code on an instance of its own that shares the module's memory.  A
 * wasm2c instance is the memory, the function table and the one mutable
 * global, __stack_pointer.  A worker copies the instance, gives the copy a
 * stack of its own in module memory, and a copy of the table whose entries
 * name the copy (an indirect call passes the table entry's instance, so
 * the original table would send the callee back to the shared one).  The
 * memory never moves (guard pages, host/CMakeLists.txt), so the copy's
 * view of it stays right.  Only malloc grows it, under the heap's lock, and
 * the lock carries the size both ways (host/main.c, heap_lock): a copy
 * takes the shared size when it locks and hands a grown one back when it
 * unlocks.
 *
 * The queue is the native pool's (engine/g2d/par.c): par_run queues its
 * tasks, wakes the workers and runs tasks itself until its own are done.
 * A task may call par_run again (the screens' tasks split into bands), on
 * the module's thread or on a worker; the caller helps drain the queue
 * while it waits, so nothing deadlocks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "host.h"
#include "wasm-rt.h"

#define PAR_MAX_WORKERS 7
#define PAR_QUEUE      256u
#define PAR_STACK      (1u * 1024u * 1024u)        /* module stack per worker */
#define PAR_NATIVE     (16u * 1024u * 1024u)       /* native stack per worker */

typedef struct Worker {
    SDL_Thread *thread;
    w2c_game inst;                  /* the copy this worker runs on        */
    wasm_rt_funcref_t *table;       /* its table, naming `inst`            */
} Worker;

typedef struct Task {
    u32 fn, ctx, index;
    u32 *pending;                   /* the caller's count of open tasks    */
} Task;

static Worker g_w[PAR_MAX_WORKERS];
static int g_nw = -1;               /* -1: not started                     */
static SDL_Mutex *g_m;
static SDL_Condition *g_work;       /* a task was queued                   */
static SDL_Condition *g_done;       /* a task finished                     */
static Task g_q[PAR_QUEUE];
static u32 g_head, g_count;
static SDL_TLSID g_self;            /* the worker a thread is, if any      */

static int pop(Task *out)
{
    if (g_count == 0) {
        return 0;
    }
    *out = g_q[g_head];
    g_head = (g_head + 1u) % PAR_QUEUE;
    g_count--;
    return 1;
}

/* One task on `inst`; the mutex is not held. */
static void run_task(w2c_game *inst, const Task *t)
{
    w2c_game_bw_par_task(inst, t->fn, t->ctx, t->index);
    SDL_LockMutex(g_m);
    (*t->pending)--;
    SDL_BroadcastCondition(g_done);
    SDL_UnlockMutex(g_m);
}

static int SDLCALL worker_main(void *arg)
{
    Worker *w = (Worker *)arg;

    SDL_SetTLS(&g_self, w, NULL);
    wasm_rt_init_thread();
    for (;;) {
        Task t;

        SDL_LockMutex(g_m);
        while (!pop(&t)) {
            SDL_WaitCondition(g_work, g_m);
        }
        SDL_UnlockMutex(g_m);
        run_task(&w->inst, &t);
    }
}

static void start(void)
{
    int cpus = SDL_GetNumLogicalCPUCores();
    int n = cpus > 1 ? cpus - 1 : 0, i;
    u32 k;

    if (getenv("EIDOS_PAR_WORKERS") != NULL) {
        n = atoi(getenv("EIDOS_PAR_WORKERS"));
    }
    if (n > PAR_MAX_WORKERS) {
        n = PAR_MAX_WORKERS;
    }
    g_nw = 0;
    if (n <= 0) {
        return;
    }
    g_m = SDL_CreateMutex();
    g_work = SDL_CreateCondition();
    g_done = SDL_CreateCondition();
    for (i = 0; i < n; i++) {
        Worker *w = &g_w[i];
        u32 stack = host_malloc(PAR_STACK);
        SDL_PropertiesID props;

        if (stack == 0) {
            break;
        }
        w->inst = g_game;
        w->table = (wasm_rt_funcref_t *)malloc(sizeof(*w->table) * g_game.w2c_T0.size);
        if (w->table == NULL) {
            break;
        }
        for (k = 0; k < g_game.w2c_T0.size; k++) {
            w->table[k] = g_game.w2c_T0.data[k];
            if (w->table[k].module_instance == (void *)&g_game) {
                w->table[k].module_instance = &w->inst;
            }
        }
        w->inst.w2c_T0.data = w->table;
        w->inst.w2c_0x5F_stack_pointer = (stack + PAR_STACK) & ~15u;
        props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void *)worker_main);
        SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "eidos-par");
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, w);
        SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, PAR_NATIVE);
        w->thread = SDL_CreateThreadWithProperties(props);
        SDL_DestroyProperties(props);
        if (w->thread == NULL) {
            break;
        }
        g_nw++;
    }
}

w2c_game *host_instance(void)
{
    Worker *self = g_nw > 0 ? (Worker *)SDL_GetTLS(&g_self) : NULL;

    return self != NULL ? &self->inst : &g_game;
}

u32 w2c_eidos_par_workers(struct w2c_eidos *e)
{
    (void)e;
    if (g_nw < 0) {
        start();
    }
    return (u32)g_nw;
}

void w2c_eidos_par_run(struct w2c_eidos *e, u32 fn, u32 ctx, u32 n)
{
    Worker *self = (Worker *)SDL_GetTLS(&g_self);
    w2c_game *inst = self != NULL ? &self->inst : &g_game;
    u32 pending = n, i, inline_from;

    (void)e;
    if (g_nw < 0) {
        start();
    }
    if (g_nw == 0 || n < 2) {
        for (i = 0; i < n; i++) {
            w2c_game_bw_par_task(inst, fn, ctx, i);
        }
        return;
    }
    SDL_LockMutex(g_m);
    for (i = 1; i < n && g_count < PAR_QUEUE; i++) {    /* task 0 stays here */
        Task *t = &g_q[(g_head + g_count) % PAR_QUEUE];

        t->fn = fn;
        t->ctx = ctx;
        t->index = i;
        t->pending = &pending;
        g_count++;
    }
    inline_from = i;
    SDL_BroadcastCondition(g_work);
    SDL_UnlockMutex(g_m);

    w2c_game_bw_par_task(inst, fn, ctx, 0);
    for (i = inline_from; i < n; i++) {
        w2c_game_bw_par_task(inst, fn, ctx, i);
    }
    SDL_LockMutex(g_m);
    pending -= 1u + (n - inline_from);
    for (;;) {
        Task t;

        if (pending == 0) {
            break;
        }
        if (pop(&t)) {
            SDL_UnlockMutex(g_m);
            run_task(inst, &t);
            SDL_LockMutex(g_m);
            continue;
        }
        SDL_WaitCondition(g_done, g_m);
    }
    SDL_UnlockMutex(g_m);
}
