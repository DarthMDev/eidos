/*
 * sdk/mod_frame.h -- the frame tick: the mods' own frame handlers and the
 * network's, once a game frame (docs/mod-services.md, "Frame tick",
 * "Peer sessions" and "External control"; docs/mod-implementation.md,
 * section 3, "What is still to build": the port's own frame hook).
 *
 * The port calls one call here, once a frame, in the game's thread:
 * bw_mod_frame_tick.  It runs the mods' own frame handlers and then the
 * network service's frame, so a mod's handler and a network handler both
 * run on the game's frame and never on a network thread.
 *
 * The API here is the port's own; the services table in sdk/mod_state.h
 * carries the one call a mod reaches (on_frame).
 */

#ifndef BW_PORT_MOD_FRAME_H
#define BW_PORT_MOD_FRAME_H

#include "types.h"
#include "mod_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The services table's call                                            */
/* ------------------------------------------------------------------ */

/*
 * The mod binds its frame handler.  The handler runs once a game frame,
 * after the game's own frame work, in the game's thread.
 *
 * The mod that is binding is the one the namespace in force names: the
 * loader puts a mod's namespace in force around every call it makes into
 * that mod, so the mod that binds a handler from its start-up entry point
 * is the namespace in force at that moment.  This is the network
 * service's rule (sdk/mod_net.c, "Who is calling"), and it is what lets
 * the same call be bound into every mod's services table.
 *
 * A NULL handler is refused: the port does nothing.
 */
void bw_mod_frame_on_frame(BwFrameHandler fn);

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

/*
 * Bind a frame handler for the mod with the namespace `ns`.  The loader
 * calls this: it is how the layer is told which mod a frame handler
 * belongs to, the way the event path is told (sdk/mod_event.h's
 * bw_mod_event_register and its registering-state hook).
 *
 * `ns` may be NULL, which means the namespace in force now.
 *
 * The mod's own state pointer (the value its start-up entry returned) is
 * the one the registering-state hook holds, so the loader sets the hook
 * around the call.  A mod that binds its handler before its start-up has
 * returned has no state yet, and the record carries none.
 *
 * The namespace is in force while the handler runs, so the mod's own state
 * calls reach its own file (docs/mod-state.md, section 2).
 *
 * Returns 0, or -1 when the handler is NULL or the table is full.
 */
int  bw_mod_frame_register(BwFrameHandler fn, const char *ns);

/*
 * The port's own hook into the registration: the state of the mod that is
 * registering right now.  The loader sets it around the calls it makes
 * into a mod (the event path's hook, sdk/mod_event.h).
 */
void bw_mod_frame_set_registering_state(void *state);

/*
 * The frame tick: the port's frame hook calls this once a frame, after the
 * game's own frame work.
 *
 * It runs the mods' own frame handlers, in the order the mods bound them,
 * and then the network service's frame (bw_mod_net_frame), which is where
 * the session events and the commands the network's threads queued reach
 * the mods' handlers.  Both run in the game's thread, so a mod's handler
 * and a network handler never run on a network thread.
 */
void bw_mod_frame_tick(void);

/* Drop every handler: the state at start-up and at a reset. */
void bw_mod_frame_reset(void);

/* How many handlers are bound, for the tests and the reports. */
u32  bw_mod_frame_handler_count(void);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_FRAME_H */
