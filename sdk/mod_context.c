/*
 * sdk/mod_context.c -- the port's record of the situations an event's
 * payload reads but the call does not carry (docs/mod-payloads.md,
 * section 1).
 *
 * The move that caused a faint was used a call earlier; the zone the
 * player is in was read at the map change.  The two contexts hold those
 * values, and the slots that pass the events feed them: the battle context
 * is reset at battle.start and its `active` cleared at battle.end, the
 * field context is reset at save.load and at each route.change, and both
 * are cleared at power-on and at every reset (bw_port_reset).
 *
 * A context field with no feeder in the current situation reads 0, never a
 * value from an earlier situation, so a reset is a clear and nothing else:
 * the feeders write the fields, and a field nobody wrote holds 0.
 *
 * A context is part of the ABI only through the payloads that read it; a
 * mod never reads a context directly.
 */
#include "mod_context.h"

#include <string.h>

/* The battle context: the battle's own situation. */
BwBattleContext bw_mod_battle_context;

/* The field context: where the player is. */
BwFieldContext bw_mod_field_context;

void bw_mod_context_reset(void)
{
    /* A cleared context is the empty one: a BwMonRef of zeroes is the
     * empty reference (BW_MON_REF_NONE), because BW_MON_PARTY is 0. */
    memset(&bw_mod_battle_context, 0, sizeof(bw_mod_battle_context));
    memset(&bw_mod_field_context, 0, sizeof(bw_mod_field_context));
}
