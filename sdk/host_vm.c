/*
 * sdk/host_vm.c -- the operating-system half of sdk/host_vm.h.
 *
 * The only file in sdk/ that includes an OS header.  sdk/host_vm.h says why
 * the port needs a reserved-but-uncommitted block at all.
 */

#if !defined(_WIN32)
/* mmap and MAP_ANONYMOUS are POSIX/BSD, hidden by a strict -std=c99. */
#  ifndef _DEFAULT_SOURCE
#    define _DEFAULT_SOURCE 1
#  endif
#  ifndef _DARWIN_C_SOURCE
#    define _DARWIN_C_SOURCE 1
#  endif
#endif

#include <stdlib.h>
#include <string.h>

#include "host_vm.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

void *bw_vm_reserve(size_t size)
{
    return VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
}

int bw_vm_commit(void *p, size_t size)
{
    return VirtualAlloc(p, size, MEM_COMMIT, PAGE_READWRITE) != NULL ? 0 : -1;
}

void bw_vm_discard(void *p, size_t size)
{
    /* Decommitted pages come back zero-filled on the next commit. */
    VirtualFree(p, size, MEM_DECOMMIT);
}

#elif defined(__unix__) || defined(__APPLE__)

#include <sys/mman.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

#define BW_VM_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE)

void *bw_vm_reserve(size_t size)
{
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, BW_VM_FLAGS, -1, 0);

    return p == MAP_FAILED ? NULL : p;
}

int bw_vm_commit(void *p, size_t size)
{
    (void)p;
    (void)size;
    return 0;
}

void bw_vm_discard(void *p, size_t size)
{
    /* A fresh anonymous mapping over the same range: zero pages, none
     * resident.  If the kernel refuses, clearing by hand is still correct. */
    if (mmap(p, size, PROT_READ | PROT_WRITE, BW_VM_FLAGS | MAP_FIXED, -1, 0)
        == MAP_FAILED) {
        memset(p, 0, size);
    }
}

#else /* neither Win32 nor POSIX: commit everything up front */

void *bw_vm_reserve(size_t size)
{
    return calloc(1, size);
}

int bw_vm_commit(void *p, size_t size)
{
    (void)p;
    (void)size;
    return 0;
}

void bw_vm_discard(void *p, size_t size)
{
    memset(p, 0, size);
}

#endif
