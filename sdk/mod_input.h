/*
 * sdk/mod_input.h -- the input injection: a mod's way of pressing a button
 * as if the player had pressed it (docs/mod-services.md, "Input
 * injection").
 *
 * What the service is
 * -------------------
 *
 * A mod asks the port to hold a button down for a number of frames, and the
 * port's own input layer holds it: the game sees the key, exactly as if the
 * player had pressed it, and the press ends on its own when the frames are
 * up, whether the mod asks again or not.
 *
 * The player's own input is not disturbed.  The injected buttons are a third
 * source beside the host's and the script's (sdk/input.c), and they are ORed
 * into what the game reads: the game sees the injected button and the
 * player's own at the same time, and the player's own keys are never
 * changed.
 *
 * The rules it keeps
 * ------------------
 *
 *   - The buttons are the game's twelve, the port's own names (sdk/input.h):
 *     A, B, X, Y, L, R, START, SELECT and the four directions.  A mask with
 *     any other bit in it is refused and reported: the debug button is not
 *     one of the game's buttons.
 *   - A press of zero frames does nothing.
 *   - A press longer than the port allows is refused and reported.  The port
 *     allows up to BW_INPUT_INJECT_MAX_FRAMES frames, a minute at the DS's
 *     sixty frames a second, and the player's own setting may lower it
 *     (engine/platform/settings.c, the [input] section).  The port holds the
 *     button for the whole count, so a longer press would hold a key down
 *     long after the mod that asked for it stopped asking.
 *
 * Who is allowed to ask
 * ---------------------
 *
 * The injection is the port's own, not the game's: the service writes into
 * the port's own input layer (sdk/input.c) and the game only ever sees a
 * button held, through the same register the player's own keys reach.
 *
 * A mod runs in the port's process with the port's full privilege, so a mod
 * may do this by itself (docs/mod-interface.md, section 5, "The trust
 * model"); the service is the port's way of doing it for the mod, with the
 * rules above.  The service keeps no state of its own: what it holds is the
 * game's input, which is one thing for the whole port, and the presses of
 * two mods that hold the same button are the same press as far as the game
 * is concerned.
 *
 * The name the loader binds into the services table:
 *
 *   m->services.input_press = bw_mod_input_press;
 */

#ifndef BW_PORT_MOD_INPUT_H
#define BW_PORT_MOD_INPUT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The services table's call                                          */
/* ------------------------------------------------------------------ */

/*
 * Hold the given buttons for the given number of frames, as if the player
 * had pressed them.  The buttons are the game's twelve, as the bits
 * sdk/input.h names them (BW_KEY_A and the rest); several of them may be
 * held at once.
 *
 * The call answers nothing: the services table's call for the injection is
 * frozen as a void one (sdk/mod_state.h), so a refusal is reported on the
 * port's stderr rather than returned.  A press of zero frames does nothing
 * and is not reported; a mask that is not one of the game's twelve and a
 * press longer than the port allows are refused and reported.
 */
void bw_mod_input_press(u32 buttons, u32 frames);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_INPUT_H */
