/*
 * sdk/mod_avatar.h -- the field avatars: the other player's figure in the
 * local field (docs/mod-services.md, "Field avatars"; docs/mod-coop.md,
 * sections 1 to 4).
 *
 * An avatar is a field entity the port creates in the game's own entity
 * pool, with an object id outside the game's range and no events, so the
 * local game never interacts with it.  The port moves it smoothly between
 * the positions a mod sends, and the avatars go away at every map change:
 * the mod recreates the ones in the new zone.
 *
 * The API here is the port's own; the services table in sdk/mod_state.h
 * carries the three calls a mod reaches.
 *
 * The names the loader binds into the services table:
 *
 *   m->services.avatar_create = bw_mod_avatar_create;
 *   m->services.avatar_move   = bw_mod_avatar_move;
 *   m->services.avatar_delete = bw_mod_avatar_delete;
 */

#ifndef BW_PORT_MOD_AVATAR_H
#define BW_PORT_MOD_AVATAR_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The services table's calls                                          */
/* ------------------------------------------------------------------ */

/*
 * An avatar appears.  The sprite is the game's own object code (the
 * numbering of the zone's event data: 1 is the male hero and 4 the female
 * heroine, FIELD_PLAYER_OBJ_CODE_HERO and FIELD_PLAYER_OBJ_CODE_HEROINE,
 * src/game/ov021/field_player_types.h).  The codes the game takes from the
 * zone's own event data (162 to 177) are refused: the game replaces those
 * with the sprite the zone names.  The cell is the one the avatar stands
 * on and the direction is the game's own (0 up, 1 down, 2 left, 3 right).
 *
 * The answer is the avatar's handle, or 0 when there is no room: the pool
 * is full, the pool has no room left for the zone's own people, or the
 * game is not there.  A handle is the port's own number and is never 0.
 */
u32  bw_mod_avatar_create(u32 sprite, s16 x, s16 z, u8 dir);

/*
 * The avatar goes to a cell: the one the mod read from the other player.
 * The direction is the other's facing and the mode is how the other moves
 * (0 walking, 1 running, 2 cycling, 3 surfing).  The port walks the
 * avatar there; it does not jump.
 *
 * A handle the port does not know is ignored.
 */
void bw_mod_avatar_move(u32 avatar, s16 x, s16 y, s16 z, u8 dir, u8 mode);

/*
 * The avatar goes away: the other player left the zone, or the session.
 * A handle the port does not know is ignored.
 */
void bw_mod_avatar_delete(u32 avatar);

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

/*
 * The game data, and with it the entity pool.  The port binds it wherever
 * it binds it for the state calls (bw_mod_state_set_game_data); the export
 * service has the same seam (sdk/mod_export.c).  Without it no avatar can
 * be created.
 */
void bw_mod_avatar_set_game_data(void *game_data);

/*
 * Every avatar goes away.  The port calls this at the map change: the
 * route.change row of the field area (sdk/mod_event_field.c) calls it, and
 * the port's own reset (bw_port_reset, sdk/port_reset.c) calls it too.
 *
 * The avatars the game has already deleted (the map change empties the
 * pool before the event fires) are only forgotten.
 */
void bw_mod_avatar_reset(void);

/*
 * The port's own frame: it walks the avatars one step towards the cells
 * the mod named.  The port calls this once a frame, after the game's own
 * frame work, where the frame tick runs (sdk/mod_frame.c).
 */
void bw_mod_avatar_frame(void);

/* How many avatars the port holds. */
u32  bw_mod_avatar_count(void);

/* The entity pool's size and how much of it is free, for the reports and
 * the tests.  0 when the game is not there. */
u32  bw_mod_avatar_pool_max(void);
u32  bw_mod_avatar_pool_free(void);

/* The object's id the port gave an avatar (0 when there is no such
 * avatar).  The id is outside the game's own range: the zone's people
 * have the ids of the event data and the player has 0xFF. */
u32  bw_mod_avatar_obj_id(u32 avatar);

/* Where an avatar is, in cells.  0 on success, -1 when there is no such
 * avatar. */
int  bw_mod_avatar_pos(u32 avatar, s16 *x, s16 *y, s16 *z);

/* The facing of an avatar, or 0 when there is no such avatar. */
u8   bw_mod_avatar_dir(u32 avatar);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_AVATAR_H */
