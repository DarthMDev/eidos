/*
 * tools/lp64/w2c_host/wasi_min.h -- the smallest WASI a wasm2c-translated
 * eidos module needs to run a benchmark: stdout and stderr, the clocks,
 * arguments, exit.
 *
 * Why this exists: wasm2c turns a wasm module into C but leaves its imports
 * to the embedder.  wasi-libc imports a few WASI functions even for a
 * program that only prints; this file answers them over the host C library
 * so docs/lp64.md's measurement needs no WASI runtime.  It is a prototype:
 * the port's real host layer would answer the port's own imports (files
 * through sdk/vfs.c, threads through sdk/plat_thread.c, SDL) instead of a
 * POSIX-shaped WASI, and would not be written against a benchmark.
 *
 * Assumes a little-endian host, which every target eidos has is.
 */

#ifndef EIDOS_WASI_MIN_H
#define EIDOS_WASI_MIN_H

#include <setjmp.h>
#include <pthread.h>

#include "wasm-rt.h"

struct w2c_wasi__snapshot__preview1 {
    wasm_rt_memory_t *mem;      /* the module's linear memory             */
    int               argc;
    char            **argv;
    jmp_buf           exit_to;  /* proc_exit unwinds the translated code  */
    int               exit_code;
    int               fds[256];
    pthread_t         main_thread;
};

void eidos_wasi_init(struct w2c_wasi__snapshot__preview1 *, wasm_rt_memory_t *,
                     int, char **);
void eidos_wasi_destroy(struct w2c_wasi__snapshot__preview1 *);

#endif /* EIDOS_WASI_MIN_H */
