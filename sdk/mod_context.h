/*
 * sdk/mod_context.h -- the port's own record of the situations an event's
 * payload reads but the call does not carry (docs/mod-payloads.md,
 * section 1).
 *
 * A context is a small struct the port keeps and updates from the slots it
 * passes: the move that caused a faint was used a call earlier, the zone
 * the player is in was read at the map change.  A context field with no
 * feeder in the current situation reads 0, never a value from an earlier
 * situation.
 *
 * A context is part of the ABI only through the payloads that read it; a mod
 * never reads a context directly.
 */

#ifndef BW_PORT_MOD_CONTEXT_H
#define BW_PORT_MOD_CONTEXT_H

#include "types.h"
#include "mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The battle context: reset at battle.start, `active` cleared at battle.end. */
typedef struct BwBattleContext {
    u8       active;        /* a battle is running */
    u8       wild;          /* a wild battle */
    u8       format;        /* BW_FORMAT_* */
    u16      turn;          /* the battle's turn record */
    u16      last_move;     /* the last move used */
    BwMonRef last_attacker; /* the Pokemon that used it */
    u32      last_damage;   /* reset at each battle.move */
    u8       weather;       /* the battle's field situation */
} BwBattleContext;

/* The field context: reset at save.load and at each route.change. */
typedef struct BwFieldContext {
    u16 zone;       /* the zone the player is in */
    u16 prev_zone;  /* the zone left */
    u8  season;
    s16 pos_x, pos_y, pos_z;  /* the player position, read at player.step */
    u8  dir;        /* the player's facing */
} BwFieldContext;

extern BwBattleContext bw_mod_battle_context;
extern BwFieldContext bw_mod_field_context;

/* Clear both contexts: at power-on and at every reset (bw_port_reset). */
void bw_mod_context_reset(void);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_CONTEXT_H */
