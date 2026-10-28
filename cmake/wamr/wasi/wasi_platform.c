/*
 * cmake/wamr/wasi/wasi_platform.c -- WAMR's platform functions for the
 * wasm32 build (shared_platform.cmake, platform_internal.h).
 */

#include "platform_api_vmcore.h"

/* the module's monotonic clock (sdk/plat_thread.h): <time.h> would bring
 * in WASI's types, which WAMR declares itself */
unsigned long long bw_clock_ns(void);

int
bh_platform_init(void)
{
    return 0;
}

void
bh_platform_destroy(void)
{
}

void *
os_malloc(unsigned size)
{
    return malloc(size);
}

void *
os_realloc(void *ptr, unsigned size)
{
    return realloc(ptr, size);
}

void
os_free(void *ptr)
{
    free(ptr);
}

int
os_dumps_proc_mem_info(char *out, unsigned int size)
{
    (void)out;
    (void)size;
    return -1;
}

/* Linear memory has no pages to map: a block from the heap, zeroed. */
void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    void *addr;

    (void)hint;
    (void)prot;
    (void)flags;
    (void)file;
    if (size >= UINT32_MAX) {
        return NULL;
    }
    addr = malloc(size);
    if (addr != NULL) {
        memset(addr, 0, size);
    }
    return addr;
}

void
os_munmap(void *addr, size_t size)
{
    (void)size;
    free(addr);
}

int
os_mprotect(void *addr, size_t size, int prot)
{
    (void)addr;
    (void)size;
    (void)prot;
    return 0;
}

void
os_dcache_flush(void)
{
}

void
os_icache_flush(void *start, size_t len)
{
    (void)start;
    (void)len;
}

korp_tid
os_self_thread(void)
{
    return 1;
}

uint8 *
os_thread_get_stack_boundary(void)
{
    return NULL;                        /* no native stack check here */
}

void
os_thread_jit_write_protect_np(bool enabled)
{
    (void)enabled;
}

uint64
os_time_get_boot_us(void)
{
    return (uint64)(bw_clock_ns() / 1000u);
}

uint64
os_time_thread_cputime_us(void)
{
    return os_time_get_boot_us();
}

int
os_mutex_init(korp_mutex *mutex)
{
    *mutex = 0;
    return BHT_OK;
}

int
os_mutex_destroy(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

int
os_mutex_lock(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

int
os_mutex_unlock(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

uint8 *
os_get_dbus_mirror(void *ibus)
{
    return (uint8 *)ibus;
}
