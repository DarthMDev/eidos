/*
 * tools/lp64/lowmem_probe.c -- where a 64-bit process puts the things the
 * game would store in a 32-bit word: its code, its globals, the C heap, a
 * large OS allocation, a thread stack, and a function in a system DLL or
 * shared library.
 *
 * Why this exists: docs/lp64.md, option (a), asks whether a 64-bit build can
 * keep every game-visible address below 4 GiB so that a host pointer still
 * fits the DS's u32.  This program answers it for one executable and one
 * linker configuration by printing each address and whether it fits.  It is
 * a measurement, not part of the port, and it is not in the default build.
 *
 * Windows: build it twice, with and without the large-address-aware flag
 * (MSVC: link /LARGEADDRESSAWARE:NO; MinGW: -Wl,--disable-large-address-aware).
 * POSIX: one build; on Linux x86-64 also try MAP_32BIT.
 */

#ifndef _WIN32
#define _GNU_SOURCE               /* MAP_ANONYMOUS, MAP_32BIT */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/mman.h>
#endif

static int g_global = 1;
static int g_fits_all = 1;

static void show(const char *what, const void *p)
{
    uintptr_t v = (uintptr_t)p;
    int fits = (uint64_t)v <= 0xFFFFFFFFu;
    if (!fits) {
        g_fits_all = 0;
    }
    printf("  %-36s %016llx  %s\n", what, (unsigned long long)v,
           fits ? "fits u32" : "ABOVE 4 GiB");
}

static void code_marker(void) { }

#ifdef _WIN32
static DWORD WINAPI thread_fn(LPVOID arg)
{
    int local = 0;
    (void)arg;
    show("thread stack (CreateThread)", &local);
    return 0;
}
#else
static void *thread_fn(void *arg)
{
    int local = 0;
    (void)arg;
    show("thread stack (pthread_create)", &local);
    return NULL;
}
#endif

int main(void)
{
    int local = 0;
    void *heap_small = malloc(64);
    void *heap_big = malloc(64u << 20);

    printf("sizeof(void *) = %u\n", (unsigned)sizeof(void *));
    show("code (a function)", (const void *)(uintptr_t)&code_marker);
    show("code (main)", (const void *)(uintptr_t)&main);
    show("global", &g_global);
    show("main thread stack", &local);
    show("malloc(64)", heap_small);
    show("malloc(64 MiB)", heap_big);
#ifdef _WIN32
    {
        void *va = VirtualAlloc(NULL, 128u << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        HANDLE t;
        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        HMODULE ucrt = LoadLibraryA("ucrtbase.dll");
        show("VirtualAlloc(128 MiB)", va);
        show("kernel32.dll!GetTickCount", (const void *)(uintptr_t)GetProcAddress(k32, "GetTickCount"));
        if (ucrt != NULL) {
            show("ucrtbase.dll!memcpy", (const void *)(uintptr_t)GetProcAddress(ucrt, "memcpy"));
        }
        show("exe image base", (const void *)GetModuleHandleA(NULL));
        t = CreateThread(NULL, 0, thread_fn, NULL, 0, NULL);
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    }
#else
    {
        pthread_t t;
        void *m = mmap(NULL, 128u << 20, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        show("mmap(128 MiB)", m == MAP_FAILED ? NULL : m);
#ifdef MAP_32BIT
        m = mmap(NULL, 128u << 20, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
        show("mmap(128 MiB, MAP_32BIT)", m == MAP_FAILED ? NULL : m);
#endif
        show("libc memcpy", (const void *)(uintptr_t)&memcpy);
        pthread_create(&t, NULL, thread_fn, NULL);
        pthread_join(t, NULL);
    }
#endif
    printf("every address fits a u32: %s\n", g_fits_all ? "yes" : "no");
    free(heap_big);
    free(heap_small);
    return 0;
}
