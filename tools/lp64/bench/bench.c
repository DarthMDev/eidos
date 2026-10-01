/*
 * tools/lp64/bench/bench.c -- a DS workload made of the game's own matched
 * code, timed the same way on every configuration docs/lp64.md compares.
 *
 * Why this exists: the wasm32 route (docs/lp64.md, option b) runs the matched
 * code through wasm2c, so every load and store goes through a linear-memory
 * base and every call through a pointer is a checked table call.  The
 * engine tests do not exercise that (they are port code), and the boot does
 * not link yet, so this harness drives three pieces of the game that the
 * library already carries at ILP32:
 *
 *   heap   the game's expanded heap and its manager (sub_02030734 /
 *          sub_020307B0 over src/matched/sub_0202FC48.c and friends): free
 *          list walks, block headers, coalescing.  Pointer chasing.
 *   tcbl   the game's task-callback scheduler (src/matched/sub_020310C4.c
 *          and the registrar): a priority list walked every frame, one call
 *          through a function pointer per task.
 *   mtx    MTX_Concat43 from the SDK (src/matched/arm9_sdk/MTX_Concat43.c):
 *          1.19.12 fixed-point 4x3 matrix products with 64-bit intermediates.
 *
 * Each kernel computes a checksum, printed with its time, so every
 * configuration can be checked to do the same work.  It needs an ILP32
 * target (EIDOS_HEAP_MATCHED and the matched tcbl sources); on anything
 * else it says so and exits 0.
 *
 * The clock is the host's (bw_bench_now_ns, below); under wasm32 it is the
 * WASI clock, which the native host behind wasm2c answers.
 */

#define _POSIX_C_SOURCE 199309L   /* clock_gettime under -std=c99 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "port.h"
#include "tcbl.h"

typedef long bench_fx32;

typedef union {
    bench_fx32 m[4][3];
    bench_fx32 a[12];
} BenchMtx43;

extern void *sub_02030734(u32 heapId, u32 size, int clear, const char *site, u16 tag);
extern void  sub_020307B0(void *p);
extern void  MTX_Concat43(const BenchMtx43 *a, const BenchMtx43 *b, BenchMtx43 *ab);

static double now_s(void)
{
#if defined(CLOCK_MONOTONIC) && !defined(_WIN32)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#else
    return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

static u32 lcg_state;

static u32 lcg(void)
{
    lcg_state = lcg_state * 1103515245u + 12345u;
    return lcg_state >> 8;
}

#define SLOTS 256

static u32 bench_heap(u32 rounds)
{
    static void *slot[SLOTS];
    u32 sum = 0;
    u32 r, i;

    memset(slot, 0, sizeof(slot));
    lcg_state = 1u;
    for (r = 0; r < rounds; r++) {
        i = lcg() % SLOTS;
        if (slot[i] != NULL) {
            sub_020307B0(slot[i]);
            slot[i] = NULL;
        } else {
            u32 size = 8u + (lcg() % 0x400u);
            u32 heap = lcg() & 1u;
            slot[i] = sub_02030734(heap | ((lcg() & 1u) ? 0x8000u : 0u), size,
                                   (int)(r & 1u), "bench", (u16)r);
            if (slot[i] != NULL) {
                ((u8 *)slot[i])[0] = (u8)r;
                sum += (u32)(((u8 *)slot[i] - (u8 *)bw_heap_handle(heap)) & 0xFFFFu);
            } else {
                sum += 0x10000u;
            }
        }
    }
    for (i = 0; i < SLOTS; i++) {
        if (slot[i] != NULL) {
            sub_020307B0(slot[i]);
        }
    }
    return sum;
}

static u32 g_task_sum;

static void task_fn(EidosTcblNode *node, void *work)
{
    u32 *w = (u32 *)work;
    (void)node;
    w[0] = w[0] * 33u + 7u;
    g_task_sum += w[0];
}

static u32 bench_tcbl(u32 frames)
{
    EidosTcblList *list = bw_tcbl_create(0, 0, 64, 16);
    u32 f, k;

    g_task_sum = 0;
    if (list == NULL) {
        return 0;
    }
    for (k = 0; k < 48; k++) {
        EidosTcblNode *n = bw_tcbl_register(list, task_fn, 16, (k * 37u) % 11u);
        if (n != NULL) {
            memset(bw_tcbl_work(n), (int)k, 16);
        }
    }
    for (f = 0; f < frames; f++) {
        bw_tcbl_run(list);
    }
    bw_tcbl_destroy_all(list);
    bw_tcbl_free(list);
    return g_task_sum;
}

static u32 bench_mtx(u32 rounds)
{
    BenchMtx43 a, b, c;
    u32 r, k, sum = 0;

    for (k = 0; k < 12; k++) {
        a.a[k] = (bench_fx32)((k * 0x371) & 0x1FFF) - 0x1000;
        b.a[k] = (bench_fx32)((k * 0x1B3) & 0x1FFF) - 0x0F00;
    }
    for (r = 0; r < rounds; r++) {
        MTX_Concat43(&a, &b, &c);
        a = c;
        for (k = 0; k < 12; k++) {
            a.a[k] = (a.a[k] & 0x1FFF) - 0x1000;
        }
        sum += (u32)c.a[r % 12];
    }
    return sum;
}

int main(int argc, char **argv)
{
    static const u32 sizes[2] = { 0x100000u, 0x100000u };
    u32 scale = 1;
    double t0, t1;
    u32 sum;

    if (argc > 1) {
        scale = (u32)strtoul(argv[1], NULL, 10);
    }
    if (sizeof(void *) != 4) {
        printf("bench: needs an ILP32 target (the matched heap and tcbl)\n");
        return 0;
    }
    if (bw_heap_init_sizes(sizes, 2, 0x10) != 0) {
        printf("bench: heap init failed (%lu)\n", (unsigned long)bw_heap_error());
        return 1;
    }

    t0 = now_s();
    sum = bench_heap(2000000u * scale);
    t1 = now_s();
    printf("heap  %10.3f ms  sum %08lx\n", (t1 - t0) * 1e3, (unsigned long)sum);

    t0 = now_s();
    sum = bench_tcbl(200000u * scale);
    t1 = now_s();
    printf("tcbl  %10.3f ms  sum %08lx\n", (t1 - t0) * 1e3, (unsigned long)sum);

    t0 = now_s();
    sum = bench_mtx(5000000u * scale);
    t1 = now_s();
    printf("mtx   %10.3f ms  sum %08lx\n", (t1 - t0) * 1e3, (unsigned long)sum);
    return 0;
}
