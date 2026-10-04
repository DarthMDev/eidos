/*
 * sdk/mod_hooks.h -- the on/off switch for port-side code hooks.
 *
 * A code hook is port C that is compiled in place of a matched function
 * (stubs/reimpl_*.c for the ARM9, stubs/ov/reimpl_ovNNN_*.c for an
 * overlay; docs/hooks.md).  A hook that changes what the game does is
 * named, and the replacement asks bw_hook_enabled() at run time.  When the
 * answer is 0 the replacement does exactly what the matched function did.
 *
 * A hook is on unless it is switched off: by the environment variable
 * EIDOS_HOOKS_OFF (a comma separated list of names, or "all"), or by
 * bw_hook_set(name, 0), which the demo's --no-hook NAME calls.
 */
#ifndef BW_PORT_MOD_HOOKS_H
#define BW_PORT_MOD_HOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

int  bw_hook_enabled(const char *name);   /* 1 = on (the default) */
void bw_hook_set(const char *name, int on);

#ifdef __cplusplus
}
#endif
#endif
