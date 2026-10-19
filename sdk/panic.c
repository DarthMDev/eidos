/*
 * sdk/panic.c -- the one place the port gives up.
 *
 * Everything that cannot be honoured on the host funnels through bw_panic:
 * an unimplemented game function (stubs/), an FS operation on a file
 * that is not open, an assertion the game's own sink raises.  It always
 * aborts.  A port that silently returns a plausible value from a function it
 * has not implemented is worse than one that stops, because the failure then
 * shows up as a wrong number somewhere else entirely.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat_crash.h"
#include "port.h"

void bw_panic(const char *what, u32 address, const char *detail)
{
    fflush(stdout);
    if (address != 0) {
        fprintf(stderr, "bw-port: PANIC in %s at 0x%08lX",
                what != NULL ? what : "?", (unsigned long)address);
    } else {
        fprintf(stderr, "bw-port: PANIC in %s", what != NULL ? what : "?");
    }
    if (detail != NULL && *detail != '\0') {
        fprintf(stderr, ": %s", detail);
    }
    fputc('\n', stderr);
    bw_plat_crash_context_run();
    if (bw_plat_crash_installed()) {
        bw_plat_backtrace();
    }
    fflush(stderr);
#ifdef _MSC_VER
    /* The MSVC runtime's abort() opens a modal "Debug Error!" dialog in a
     * debug build and asks Windows Error Reporting for a dump in any build.
     * The message above is the report; a dialog would stall an unattended
     * run, and the expected-panic tests are unattended. */
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    abort();
}

static int g_keep_going = -1;        /* -1: read EIDOS_KEEP_GOING once */

void bw_set_keep_going(int on)
{
    g_keep_going = on ? 1 : 0;
}

int bw_keep_going(void)
{
    if (g_keep_going < 0) {
        const char *v = getenv("EIDOS_KEEP_GOING");
        g_keep_going = (v != NULL && *v != '\0' && strcmp(v, "0") != 0) ? 1 : 0;
    }
    return g_keep_going;
}

void bw_stub_reached(const char *what, u32 address, const char *detail)
{
    static u32 seen[1024];
    static unsigned nseen;
    unsigned i;

    if (!bw_keep_going()) {
        bw_panic(what, address, detail);
        return;
    }
    for (i = 0; i < nseen; i++) {
        if (seen[i] == address) {
            return;
        }
    }
    if (nseen < sizeof seen / sizeof seen[0]) {
        seen[nseen++] = address;
    }
    fflush(stdout);
    fprintf(stderr, "STUB (kept going): %s at 0x%08lX%s%s\n",
            what != NULL ? what : "?", (unsigned long)address,
            detail != NULL && *detail != '\0' ? ": " : "",
            detail != NULL ? detail : "");
    bw_plat_crash_context_run();
    fflush(stderr);
}
