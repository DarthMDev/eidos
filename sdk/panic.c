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
