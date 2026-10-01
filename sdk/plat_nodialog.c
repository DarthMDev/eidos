/*
 * sdk/plat_nodialog.c -- no modal dialogs from an MSVC-built program.
 *
 * The Microsoft C runtime reports several failures with a modal window that
 * waits for a click: the debug CRT's "Debug Error!" / "Assertion Failed!"
 * boxes (_CrtDbgReport), abort()'s message box, the invalid-parameter
 * handler's report, and Windows Error Reporting's "has stopped working"
 * window on an unhandled exception.  The tests run unattended (CTest,
 * tools/check.py, CI) and several of them abort on purpose
 * (tests/expect_panic.cmake), so a dialog stalls the run until someone
 * clicks it.  Every one of those reports also goes to stderr, where CTest
 * collects it, so nothing is lost by turning the windows off.
 *
 * This file is compiled into every executable of an MSVC or clang-cl build
 * (CMakeLists.txt, EIDOS_NO_CRT_DIALOGS) and does its work from a CRT
 * initialiser, before main, so no program has to remember to call it.  It
 * is linked as an object of each executable rather than as a member of the
 * eidos library because nothing references it: a linker pulls a library
 * member only to resolve a symbol.  Other toolchains compile it to nothing:
 * MinGW programs link the release runtime, which has none of the debug
 * boxes, and the POSIX hosts have no such dialogs at all.
 */

#if defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <crtdbg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

/* An invalid argument to a CRT function (a NULL FILE *, a bad descriptor):
 * report it on stderr and stop.  Returning would let the CRT function fail
 * with EINVAL instead, which the port never checks for. */
static void __cdecl bw_invalid_parameter(const wchar_t *expr,
                                         const wchar_t *func,
                                         const wchar_t *file,
                                         unsigned int line,
                                         uintptr_t reserved)
{
    (void)reserved;
    fflush(stdout);
    fprintf(stderr, "bw-port: invalid CRT parameter");
    if (func != NULL) {
        fprintf(stderr, " in %ls", func);
    }
    if (file != NULL) {
        fprintf(stderr, " (%ls:%u)", file, line);
    }
    if (expr != NULL) {
        fprintf(stderr, ": %ls", expr);
    }
    fputc('\n', stderr);
    fflush(stderr);
    _exit(3);
}

static int __cdecl bw_no_dialogs(void)
{
    /* No "has stopped working" or "insert a disk" windows from Windows
     * itself; inherited by the processes this one starts. */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX
                 | SEM_NOOPENFILEERRORBOX);

    /* The debug CRT's reports go to stderr.  In a release-runtime build the
     * _CrtSetReport* calls are macros that expand to nothing. */
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

    /* assert() in the release runtime, and abort()'s own message box and
     * fault report (sdk/panic.c sets the same before it aborts). */
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_invalid_parameter_handler(bw_invalid_parameter);
    return 0;
}

/* A C initialiser: the CRT calls every function pointer placed in a
 * .CRT$XI* section before main (the "U" slot runs after the CRT's own).
 * The pointer is kept by the linker because __declspec(allocate) data is
 * not a COMDAT that /OPT:REF may discard. */
#pragma section(".CRT$XIU", long, read)
__declspec(allocate(".CRT$XIU"))
int (__cdecl *const bw_no_dialogs_init)(void) = bw_no_dialogs;

#else

/* ISO C forbids an empty translation unit. */
typedef int bw_plat_nodialog_unused;

#endif
