/*
 * tools/lp64/w2c_host/wasi_min.c -- the WASI calls wasi-libc makes, answered
 * natively for a wasm2c module (see wasi_min.h for why this exists).
 *
 * Every pointer argument is an offset into the module's linear memory; it is
 * bounds-checked here before it is touched, because these are the only
 * accesses to that memory the translated code does not check itself.
 */

#define _POSIX_C_SOURCE 199309L   /* clock_gettime under -std=c99 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "wasi_min.h"

typedef struct w2c_wasi__snapshot__preview1 Wasi;

#define WASI_ESUCCESS 0u
#define WASI_EBADF    8u
#define WASI_EFAULT  21u
#define WASI_EINVAL  28u
#define WASI_ENOSYS  52u

static uint8_t *at(Wasi *w, uint32_t off, uint32_t len)
{
    if ((uint64_t)off + len > w->mem->size) {
        return NULL;
    }
    return w->mem->data + off;
}

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void wr64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }

uint32_t w2c_wasi__snapshot__preview1_fd_write(Wasi *w, uint32_t fd, uint32_t iovs,
                                               uint32_t n, uint32_t nwritten)
{
    FILE *f = fd == 1 ? stdout : fd == 2 ? stderr : NULL;
    uint32_t i, total = 0;
    uint8_t *vec = at(w, iovs, n * 8u);
    uint8_t *out = at(w, nwritten, 4);

    if (f == NULL) {
        return WASI_EBADF;
    }
    if (vec == NULL || out == NULL) {
        return WASI_EFAULT;
    }
    for (i = 0; i < n; i++) {
        uint32_t base = rd32(vec + 8u * i), len = rd32(vec + 8u * i + 4u);
        uint8_t *p = at(w, base, len);
        if (p == NULL) {
            return WASI_EFAULT;
        }
        total += (uint32_t)fwrite(p, 1, len, f);
    }
    fflush(f);
    wr32(out, total);
    return WASI_ESUCCESS;
}

uint32_t w2c_wasi__snapshot__preview1_fd_close(Wasi *w, uint32_t fd)
{
    (void)w;
    (void)fd;
    return WASI_ESUCCESS;
}

uint32_t w2c_wasi__snapshot__preview1_fd_seek(Wasi *w, uint32_t fd, uint64_t off,
                                              uint32_t whence, uint32_t out)
{
    (void)w; (void)fd; (void)off; (void)whence; (void)out;
    return WASI_ENOSYS;
}

/* No preopened directories: wasi-libc's start-up asks from fd 3 up and stops
 * at the first EBADF. */
uint32_t w2c_wasi__snapshot__preview1_fd_prestat_get(Wasi *w, uint32_t fd, uint32_t out)
{
    (void)w; (void)fd; (void)out;
    return WASI_EBADF;
}

uint32_t w2c_wasi__snapshot__preview1_fd_prestat_dir_name(Wasi *w, uint32_t fd,
                                                          uint32_t path, uint32_t len)
{
    (void)w; (void)fd; (void)path; (void)len;
    return WASI_EBADF;
}

uint32_t w2c_wasi__snapshot__preview1_fd_fdstat_get(Wasi *w, uint32_t fd, uint32_t out)
{
    uint8_t *p = at(w, out, 24);
    if (p == NULL) {
        return WASI_EFAULT;
    }
    if (fd > 2) {
        return WASI_EBADF;
    }
    memset(p, 0, 24);
    p[0] = 2;                     /* filetype: character device */
    return WASI_ESUCCESS;
}

static uint64_t now_ns(uint32_t id)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;
    (void)id;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e9 / (double)f.QuadPart);
#else
    struct timespec ts;
    clock_gettime(id == 0 ? CLOCK_REALTIME : CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
#endif
}

uint32_t w2c_wasi__snapshot__preview1_clock_time_get(Wasi *w, uint32_t id,
                                                     uint64_t precision, uint32_t out)
{
    uint8_t *p = at(w, out, 8);
    (void)precision;
    if (p == NULL) {
        return WASI_EFAULT;
    }
    if (id > 3) {
        return WASI_EINVAL;
    }
    wr64(p, now_ns(id));
    return WASI_ESUCCESS;
}

uint32_t w2c_wasi__snapshot__preview1_args_sizes_get(Wasi *w, uint32_t argc_out,
                                                     uint32_t size_out)
{
    uint8_t *a = at(w, argc_out, 4), *s = at(w, size_out, 4);
    uint32_t i, size = 0;
    if (a == NULL || s == NULL) {
        return WASI_EFAULT;
    }
    for (i = 0; i < (uint32_t)w->argc; i++) {
        size += (uint32_t)strlen(w->argv[i]) + 1u;
    }
    wr32(a, (uint32_t)w->argc);
    wr32(s, size);
    return WASI_ESUCCESS;
}

uint32_t w2c_wasi__snapshot__preview1_args_get(Wasi *w, uint32_t argv_out, uint32_t buf)
{
    uint32_t i, pos = buf;
    for (i = 0; i < (uint32_t)w->argc; i++) {
        uint32_t len = (uint32_t)strlen(w->argv[i]) + 1u;
        uint8_t *slot = at(w, argv_out + 4u * i, 4), *dst = at(w, pos, len);
        if (slot == NULL || dst == NULL) {
            return WASI_EFAULT;
        }
        wr32(slot, pos);
        memcpy(dst, w->argv[i], len);
        pos += len;
    }
    return WASI_ESUCCESS;
}

uint32_t w2c_wasi__snapshot__preview1_environ_sizes_get(Wasi *w, uint32_t n, uint32_t s)
{
    uint8_t *a = at(w, n, 4), *b = at(w, s, 4);
    if (a == NULL || b == NULL) {
        return WASI_EFAULT;
    }
    wr32(a, 0);
    wr32(b, 0);
    return WASI_ESUCCESS;
}

uint32_t w2c_wasi__snapshot__preview1_environ_get(Wasi *w, uint32_t e, uint32_t b)
{
    (void)w; (void)e; (void)b;
    return WASI_ESUCCESS;
}

void w2c_wasi__snapshot__preview1_proc_exit(Wasi *w, uint32_t code)
{
    w->exit_code = (int)code;
    longjmp(w->exit_to, 1);
}
