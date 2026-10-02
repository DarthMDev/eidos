/*
 * sdk/plat_crash.h -- report a host crash (an access violation in matched
 * code) on stderr instead of dying silently.
 *
 * The game's own boot runs about 1100 matched translation units on the host
 * (docs/boot.md).  A wrong binding or an unrewritten DS address shows up as
 * a fault somewhere inside them, and on a machine without a debugger the
 * only trace of it would be the shell's "Segmentation fault".  After
 * bw_plat_crash_install, a fault prints the signal or exception, the faulting
 * address, the host address of the instruction and the return addresses of
 * the frame-pointer chain, each as an offset into the executable, so
 * `addr2line -f -e <exe> <address>` (or the map file) names the functions.
 * Then the process exits with status 3, like bw_panic's abort, without a
 * dialog.
 *
 * Win32 and POSIX behind one function; nothing else in the port depends on
 * it.  The frame walk follows the frame pointers a build without
 * optimisation keeps; with -fomit-frame-pointer it ends early, which is
 * harmless.
 */

#ifndef BW_PORT_PLAT_CRASH_H
#define BW_PORT_PLAT_CRASH_H

#ifdef __cplusplus
extern "C" {
#endif

void bw_plat_crash_install(void);

/* Whether a program installed the reporter, and the calling code's return
 * addresses in the same form (Win32; nothing elsewhere).  sdk/panic.c adds
 * them to a panic's message when the reporter is installed. */
int  bw_plat_crash_installed(void);
void bw_plat_backtrace(void);

/*
 * A watchdog for a run that hangs: a host thread that reads `progress`
 * once a second and, when it has not changed for `seconds`, reports it
 * (with every other thread's pc and frame chain on Win32) and exits with
 * status 3.
 */
void bw_plat_watchdog(unsigned seconds, unsigned long long (*progress)(void));

/*
 * A program's own lines for every report (a crash, a panic, the
 * watchdog): `fn` runs after the report's first line and prints where the
 * program stood (eidos_boot: the frame count and the resident overlays).
 * NULL removes it.  bw_plat_crash_context_run is what the reporters call.
 */
void bw_plat_crash_context(void (*fn)(void));
void bw_plat_crash_context_run(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_PLAT_CRASH_H */
