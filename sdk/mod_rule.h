/*
 * sdk/mod_rule.h -- the rule interpreter (docs/mod-rules-language.md;
 * docs/mod-rules.md).
 *
 * A rule is a small file a mod ships: it binds to a stable event and runs a
 * few actions when the event fires, so a mod can act on the game without
 * shipping code.
 *
 * The API here is the port's own, and sdk/mod_event.h is its model: a bind
 * call, a run call, a reset and a count.
 */

#ifndef BW_PORT_MOD_RULE_H
#define BW_PORT_MOD_RULE_H

#include "types.h"
#include "mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The id of the event with this name, or 0 when there is none.
 */
u32 bw_mod_rule_event_kind(const char *name);

/* The name of the event with this id, or NULL. */
const char *bw_mod_rule_event_name(u32 kind);

/*
 * Bind a rule file to an event.  `ns` is the mod's namespace (its cells)
 * and `label` the mod's name, for the reports.  Returns 0, or -1 when the
 * rule is refused: a syntax error, an event the port does not know, a
 * file written for another event or another language version, or a name
 * the port does not know.
 */
int bw_mod_rule_bind(u32 kind, const char *path, const char *ns,
                     const char *label);

/*
 * The same, from a manifest's rule line: `rule = <event> <path>`, with
 * the path relative to the mod's own folder.
 */
int bw_mod_rule_bind_manifest(const char *root, const char *dir_name,
                              const char *event_name, const char *rel_path,
                              const char *ns, const char *label);

/*
 * Run the rules bound to an event.  Returns 1 when a rule refused.
 */
int bw_mod_rule_run(u32 kind, BwEvent *event);

/* Drop every rule: the state at start-up and at a reset. */
void bw_mod_rule_reset(void);

/* How many rules are bound to an event. */
u32 bw_mod_rule_count(u32 kind);

/*
 * The peer session send.  The service is not built, so the `send` action
 * reports that it is unavailable; the port installs the call here when the
 * service is there.
 */
void bw_mod_rule_set_send(int (*send)(const char *value));

/*
 * The mod's own cells: where they are read and written.  The port
 * installs the two calls, and they reach the mod's own file under the
 * mod's namespace (docs/mod-state.md).  Without them the cells live in
 * memory for the run, and the port is told once.
 */
void bw_mod_rule_set_cells_io(s32 (*load)(const char *ns, void *buf, u32 size),
                              int (*store)(const char *ns, const void *buf,
                                           u32 size));

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_RULE_H */
