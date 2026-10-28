/*
 * sdk/host_vm.c -- the operating-system half of sdk/host_vm.h.
 *
 * The only file in sdk/ that includes an OS header.  sdk/host_vm.h says why
 * the port needs a reserved-but-uncommitted block at all.
 */

#if !defined(_WIN32) && !defined(__wasm__)
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

/* Set in a child bw_vm_relaunch_reserved started: its fixed ranges were
 * reserved for it before it ran, and bw_vm_reserve_at takes them over. */
#define BW_VM_CHILD_ENV "BW_VM_PRERESERVED"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

void *bw_vm_reserve(size_t size)
{
    return VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
}

/* 1 if [want, want + size) is one reservation starting at want, made for
 * this process by its parent (bw_vm_relaunch_reserved) and not used yet. */
static int prereserved(void *want, size_t size)
{
    MEMORY_BASIC_INFORMATION mbi;

    if (getenv(BW_VM_CHILD_ENV) == NULL
        || VirtualQuery(want, &mbi, sizeof mbi) != sizeof mbi) {
        return 0;
    }
    return mbi.State == MEM_RESERVE && mbi.Type == MEM_PRIVATE
        && mbi.AllocationBase == want && mbi.BaseAddress == want
        && mbi.RegionSize >= size;
}

void *bw_vm_reserve_at(size_t addr, size_t size)
{
    void *want = (void *)addr;
    void *p = VirtualAlloc(want, size, MEM_RESERVE, PAGE_NOACCESS);

    if (p == NULL && prereserved(want, size)) {
        return want;
    }
    if (p != NULL && p != want) {
        VirtualFree(p, 0, MEM_RELEASE);
        p = NULL;
    }
    return p;
}

/*
 * Why a relaunch: a Win32 process has mappings in its low 2 GiB before its
 * first line of code runs (code pages and locale files, the GDI shared
 * handle table user32 maps, heaps and thread stacks the loader and the CRT
 * create), and with ASLR where they land changes from run to run, often
 * inside 0x02000000-0x08000000.  A process created suspended has only its
 * executable, ntdll and its first stack mapped, so the parent reserves the
 * ranges in it then, and the child's loader places everything else around
 * them.  The child is this same program with the same command line and
 * standard handles; the parent waits and returns its exit status.
 */
int bw_vm_relaunch_reserved(const size_t *addr, const size_t *size, int count,
                            int *status)
{
    wchar_t path[MAX_PATH + 1];
    wchar_t *cmd;
    const wchar_t *line;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 0;
    DWORD n;
    int attempt, i, ok;

    if (getenv(BW_VM_CHILD_ENV) != NULL || count <= 0) {
        return 0;
    }
    n = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return 0;
    }
    path[n] = 0;
    line = GetCommandLineW();
    cmd = (wchar_t *)malloc((wcslen(line) + 1) * sizeof(wchar_t));
    if (cmd == NULL) {
        return 0;
    }
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    SetHandleInformation(si.hStdInput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(si.hStdOutput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(si.hStdError, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetEnvironmentVariableA(BW_VM_CHILD_ENV, "1");

    /* The child's executable lands where ASLR puts it, which can be inside
     * a range: then try again, a few times, before giving up. */
    for (attempt = 0; attempt < 4; attempt++) {
        wcscpy(cmd, line);
        if (!CreateProcessW(path, cmd, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL,
                            NULL, &si, &pi)) {
            break;
        }
        ok = 1;
        for (i = 0; i < count && ok; i++) {
            if (VirtualAllocEx(pi.hProcess, (void *)addr[i], size[i], MEM_RESERVE,
                               PAGE_NOACCESS) != (void *)addr[i]) {
                ok = 0;
            }
        }
        if (!ok) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            continue;
        }
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        free(cmd);
        *status = (int)code;
        return 1;
    }
    SetEnvironmentVariableA(BW_VM_CHILD_ENV, NULL);
    free(cmd);
    return 0;
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
#include <unistd.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

#define BW_VM_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE)

#ifdef MAP_FIXED_NOREPLACE
#define BW_VM_AT_FLAGS (BW_VM_FLAGS | MAP_FIXED_NOREPLACE)
#else
#define BW_VM_AT_FLAGS BW_VM_FLAGS
#endif

void *bw_vm_reserve(size_t size)
{
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, BW_VM_FLAGS, -1, 0);

    return p == MAP_FAILED ? NULL : p;
}

void *bw_vm_reserve_at(size_t addr, size_t size)
{
    void *want = (void *)addr;
    void *p = mmap(want, size, PROT_NONE, BW_VM_AT_FLAGS, -1, 0);

    if (p == MAP_FAILED) {
        return NULL;
    }
    if (p != want) {
        /* Taken as a hint and placed elsewhere: not the identity range. */
        munmap(p, size);
        return NULL;
    }
    return p;
}

int bw_vm_relaunch_reserved(const size_t *addr, const size_t *size, int count,
                            int *status)
{
    /* Nothing maps the low ranges of a fresh POSIX process behind its back
     * the way a Win32 loader does: reserving at run time is enough. */
    (void)addr;
    (void)size;
    (void)count;
    (void)status;
    return 0;
}

int bw_vm_commit(void *p, size_t size)
{
    /* A movable block is mapped read-write already; a fixed one is mapped
     * PROT_NONE until committed.  mprotect works on whole pages. */
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t lo = (size_t)p & ~(page - 1u);
    size_t hi = ((size_t)p + size + page - 1u) & ~(page - 1u);

    return mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE) == 0 ? 0 : -1;
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

#elif defined(__wasm__)

/*
 * The wasm32 build: the module's memory starts with the DS's own address
 * space.  The link puts everything the module has at or above
 * BW_WASM_DS_TOP (CMakeLists.txt, --global-base), so the DS's windows
 * below it are nobody else's: a fixed reservation there is the address
 * itself, and a DS address the game holds is the module address of its
 * byte (sdk/dsmem.c, "Identity mapping").  The memory starts zeroed.
 */
#define BW_WASM_DS_TOP 0x08000000u

void *bw_vm_reserve(size_t size)
{
    return calloc(1, size);
}

void *bw_vm_reserve_at(size_t addr, size_t size)
{
    if (addr == 0 || addr + size > BW_WASM_DS_TOP) {
        return NULL;
    }
    return (void *)addr;
}

int bw_vm_relaunch_reserved(const size_t *addr, const size_t *size, int count,
                            int *status)
{
    (void)addr;
    (void)size;
    (void)count;
    (void)status;
    return 0;
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

#else /* neither Win32 nor POSIX: commit everything up front */

void *bw_vm_reserve(size_t size)
{
    return calloc(1, size);
}

void *bw_vm_reserve_at(size_t addr, size_t size)
{
    (void)addr;
    (void)size;
    return NULL;
}

int bw_vm_relaunch_reserved(const size_t *addr, const size_t *size, int count,
                            int *status)
{
    (void)addr;
    (void)size;
    (void)count;
    (void)status;
    return 0;
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
