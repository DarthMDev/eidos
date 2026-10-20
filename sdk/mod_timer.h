/*
 * sdk/mod_timer.h -- the timer, the splits and the mod's own random source
 * (docs/mod-services.md, "Timer and splits" and "Seeded random source").
 *
 * One timer and one generator per mod, keyed on the mod's state namespace.
 * The timer counts the port's tick while the game runs and stands still
 * while the port's own menu is open.
 *
 * The API here is the port's own; the services table in sdk/mod_state.h
 * carries the six calls a mod reaches.
 */

#ifndef BW_PORT_MOD_TIMER_H
#define BW_PORT_MOD_TIMER_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The services table's calls. */
void bw_mod_timer_start(void);
void bw_mod_timer_stop(void);
u64  bw_mod_timer_ms(void);
void bw_mod_timer_split(const char *name);
void bw_mod_rand_seed(u64 seed);
u32  bw_mod_rand_next(void);

/* The port's own calls: the splits list, the elapsed time, the frame tick
 * and the reset. */
u32  bw_mod_timer_splits_count(const char *ns);
int  bw_mod_timer_split_at(const char *ns, u32 index, char *name, u32 name_size,
                           u64 *ms);
u64  bw_mod_timer_elapsed_ms(const char *ns);
void bw_mod_timer_frame(void);
int  bw_mod_timer_splits_write(const char *ns, const char *path);
int  bw_mod_timer_splits_write_default(const char *ns);
void bw_mod_timer_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_TIMER_H */
