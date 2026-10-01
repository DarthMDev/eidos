/*
 * sdk/plat_crash.c -- a host crash reported on stderr (sdk/plat_crash.h).
 *
 * Win32: an unhandled-exception filter.  It prints the exception code, for
 * an access violation the data address and whether it was a read or a write,
 * then the faulting instruction and the frame-pointer chain's return
 * addresses, each as the executable's preferred address (ImageBase +
 * offset, what addr2line and the map file use) whatever ASLR did.
 *
 * POSIX: a SA_SIGINFO handler for SIGSEGV, SIGBUS, SIGILL and SIGFPE that
 * prints the signal and the faulting address.  The interrupted context's
 * registers are machine-specific there, so no frame walk.
 *
 * Both exit with status 3 and never return into the faulting code.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L   /* sigaction, siginfo_t */
#endif

#include "plat_crash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <stdint.h>

static int bw_crash_readable(uintptr_t addr, size_t len)
{
    MEMORY_BASIC_INFORMATION mbi;

    if (addr == 0 || VirtualQuery((LPCVOID)addr, &mbi, sizeof mbi) == 0) {
        return 0;
    }
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        return 0;
    }
    return addr + len <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

/* An address inside the executable as its preferred address, else as is. */
static void bw_crash_print_addr(const char *what, uintptr_t addr)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + (uintptr_t)dos->e_lfanew);
    uintptr_t size = nt->OptionalHeader.SizeOfImage;

    if (addr >= base && addr < base + size) {
        fprintf(stderr, "  %-6s 0x%08llX  (exe+0x%llX)\n", what,
                (unsigned long long)((uintptr_t)nt->OptionalHeader.ImageBase + (addr - base)),
                (unsigned long long)(addr - base));
    } else {
        fprintf(stderr, "  %-6s 0x%08llX  (outside the executable)\n", what,
                (unsigned long long)addr);
    }
}

/* The frame pointer and the pc of a captured context. */
static void bw_crash_context(const CONTEXT *c, uintptr_t *pc, uintptr_t *fp)
{
#if defined(_M_IX86) || defined(__i386__)
    *pc = (uintptr_t)c->Eip;
    *fp = (uintptr_t)c->Ebp;
#elif defined(_M_X64) || defined(__x86_64__)
    *pc = (uintptr_t)c->Rip;
    *fp = (uintptr_t)c->Rbp;
#elif defined(_M_ARM64) || defined(__aarch64__)
    *pc = (uintptr_t)c->Pc;
    *fp = (uintptr_t)c->Fp;
#else
    (void)c;
    *pc = 0;
    *fp = 0;
#endif
}

/* The return addresses of the frame-pointer chain from `fp`. */
static void bw_crash_walk(uintptr_t fp)
{
    int i;

    for (i = 0; i < 48 && bw_crash_readable(fp, 2 * sizeof(uintptr_t)); i++) {
        const uintptr_t *frame = (const uintptr_t *)fp;
        bw_crash_print_addr("called", frame[1]);
        if (frame[0] <= fp) {
            break;
        }
        fp = frame[0];
    }
}

static LONG WINAPI bw_crash_filter(EXCEPTION_POINTERS *ep)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    uintptr_t pc;
    uintptr_t fp;

    bw_crash_context(ep->ContextRecord, &pc, &fp);
    pc = (uintptr_t)er->ExceptionAddress;

    fprintf(stderr, "CRASH: exception 0x%08lX", (unsigned long)er->ExceptionCode);
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
        fprintf(stderr, ", %s of host address 0x%08llX",
                er->ExceptionInformation[0] == 1 ? "write" :
                er->ExceptionInformation[0] == 8 ? "execute" : "read",
                (unsigned long long)er->ExceptionInformation[1]);
    }
    fprintf(stderr, "\n");
    bw_crash_print_addr("pc", pc);
    bw_crash_walk(fp);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}

static int bw_crash_on;

void bw_plat_crash_install(void)
{
    SetUnhandledExceptionFilter(bw_crash_filter);
    bw_crash_on = 1;
}

int bw_plat_crash_installed(void)
{
    return bw_crash_on;
}

/* Every other thread of the process: suspended, its pc and frame chain. */
static void bw_crash_dump_threads(void)
{
    DWORD self = GetCurrentThreadId();
    DWORD pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;

    if (snap == INVALID_HANDLE_VALUE) {
        return;
    }
    te.dwSize = sizeof te;
    if (Thread32First(snap, &te)) {
        do {
            HANDLE h;
            CONTEXT c;
            uintptr_t pc;
            uintptr_t fp;

            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) {
                continue;
            }
            h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT
                           | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (h == NULL) {
                continue;
            }
            if (SuspendThread(h) != (DWORD)-1) {
                memset(&c, 0, sizeof c);
                c.ContextFlags = CONTEXT_FULL;
                if (GetThreadContext(h, &c)) {
                    bw_crash_context(&c, &pc, &fp);
                    fprintf(stderr, "thread %lu:\n", (unsigned long)te.th32ThreadID);
                    bw_crash_print_addr("pc", pc);
                    bw_crash_walk(fp);
                }
            }
            CloseHandle(h);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    fflush(stderr);
}

typedef struct BwWatchdog {
    unsigned seconds;
    unsigned long long (*progress)(void);
} BwWatchdog;

static BwWatchdog g_watchdog;

static DWORD WINAPI bw_watchdog_main(LPVOID arg)
{
    unsigned long long last = g_watchdog.progress();
    unsigned still = 0;

    (void)arg;
    while (still < g_watchdog.seconds) {
        unsigned long long now;

        Sleep(1000);
        now = g_watchdog.progress();
        if (now != last) {
            last = now;
            still = 0;
        } else {
            still++;
        }
    }
    fprintf(stderr, "WATCHDOG: no progress for %u s (at %llu)\n",
            g_watchdog.seconds, last);
    bw_crash_dump_threads();
    TerminateProcess(GetCurrentProcess(), 3);
    return 0;
}

void bw_plat_watchdog(unsigned seconds, unsigned long long (*progress)(void))
{
    HANDLE h;

    g_watchdog.seconds = seconds;
    g_watchdog.progress = progress;
    h = CreateThread(NULL, 0, bw_watchdog_main, NULL, 0, NULL);
    if (h != NULL) {
        CloseHandle(h);
    }
}

void bw_plat_backtrace(void)
{
    void *frames[48];
    USHORT n = CaptureStackBackTrace(1, 48, frames, NULL);
    USHORT i;

    for (i = 0; i < n; i++) {
        bw_crash_print_addr("called", (uintptr_t)frames[i]);
    }
    fflush(stderr);
}

#else /* POSIX */

#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void bw_crash_handler(int sig, siginfo_t *si, void *uc)
{
    (void)uc;
    /* fprintf is not async-signal-safe; the process is ending anyway and the
     * report is the point. */
    fprintf(stderr, "CRASH: signal %d (%s) at address %p\n", sig,
            sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" :
            sig == SIGILL ? "SIGILL" : "SIGFPE", si ? si->si_addr : NULL);
    fflush(stderr);
    _exit(3);
}

static int bw_crash_on;

int bw_plat_crash_installed(void)
{
    return bw_crash_on;
}

/* No portable way to walk the stack here; the panic message stands alone. */
void bw_plat_backtrace(void)
{
}

typedef struct BwWatchdog {
    unsigned seconds;
    unsigned long long (*progress)(void);
} BwWatchdog;

static BwWatchdog g_watchdog;

static void *bw_watchdog_main(void *arg)
{
    unsigned long long last = g_watchdog.progress();
    unsigned still = 0;

    (void)arg;
    while (still < g_watchdog.seconds) {
        struct timespec ts;
        unsigned long long now;

        ts.tv_sec = 1;
        ts.tv_nsec = 0;
        nanosleep(&ts, NULL);
        now = g_watchdog.progress();
        if (now != last) {
            last = now;
            still = 0;
        } else {
            still++;
        }
    }
    fprintf(stderr, "WATCHDOG: no progress for %u s (at %llu)\n",
            g_watchdog.seconds, last);
    fflush(stderr);
    _exit(3);
    return NULL;
}

void bw_plat_watchdog(unsigned seconds, unsigned long long (*progress)(void))
{
    pthread_t t;

    g_watchdog.seconds = seconds;
    g_watchdog.progress = progress;
    if (pthread_create(&t, NULL, bw_watchdog_main, NULL) == 0) {
        pthread_detach(t);
    }
}

void bw_plat_crash_install(void)
{
    static const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE };
    struct sigaction sa;
    size_t i;

    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = bw_crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
        sigaction(sigs[i], &sa, NULL);
    }
    bw_crash_on = 1;
}

#endif
