# Co-op

This document owns what the port provides for a co-op mode: each player plays
their own game, and each sees the other's avatar walking in their own world.
The mode itself (its session, its rules, what else it shares) is a mod.
`docs/modding.md` is the overview of the mod surface.

## 1. The model

- **Separate games.** Each player runs their own eidos with their own save.
  Battles, scripts, story and items stay local.
- **Shared positions.** Each instance sends its player's zone, position, facing
  and movement mode to the others, a few times per second, over a peer session
  (`docs/mod-services.md`).
- **The partner is a puppet.** Each instance draws every other player as an
  avatar in its own field, placed where that player says they are. The avatar
  has no effect on the local game: it does not block movement, trigger events
  or appear in scripts.
- **Each player owns their own avatar.** Nothing is simulated twice and nothing
  needs to agree, so no shared random source and no lockstep are needed. A mod
  that wants more (shared flags, shared money) sends those values itself.

The game's own link features (link battles, trades, the C-Gear) are separate
work and play no part here.

## 2. What the mode reads

| Value | Source |
|---|---|
| The zone | `GAME_DATA_GetPlayerZoneId` 0x02012AC8 (arm9); `FIELDMAP_GetZoneId` 0x02188C9C (ov021) |
| The position | `FIELD_PLAYER_GetPos` 0x021A3038 (ov021), a `VecFx32` read through `FIELD_PLAYER_CORE_GetPos` 0x021A3898 and `FLDMMDL_GetPos` 0x0216D88C (ov010) |
| The grid cell | `FIELD_PLAYER_GetNowGridPos` 0x021A35FC; `FLDMMDL_GetGridNowX` 0x0216D83C, `FLDMMDL_GetGridNowZ` 0x0216D86C |
| The facing and movement | the field context of `docs/mod-payloads.md`, section 1, fed at `player.step` |
| A map change | `route.change` and `map.warp` (`docs/mod-events.md`, section 1) |

## 3. The avatar

The game's field entities are FLDMMDL records, a fixed pool in ov010. The
player itself is one of them: `FIELD_PLAYER_CORE_Create` 0x021A36E4 creates
the player's record through `FLDMMDL_AddMmdl` and finds it again as object id
255 (`FLDMMDL_SysSearchPlayer` 0x0216DEE0). NPCs are records created from a
zone's event data (`FIELDMAP_AddZoneNpcs` 0x021894E0, through
`FLDMMDL_AddMmdlHeaderList` 0x0216CEAC).

So the game already draws and animates any record in its pool, and a mod
creates the partner's avatar the way the game creates an NPC:

- **Create.** `FLDMMDL_AddMmdl` 0x0216CE60 into `FLDMMDL_AddMmdlCore`
  0x0216CDC4 takes a 0x24-byte header, finds a free slot with
  `FLDMMDL_SysGetFreeMmdl` 0x0216DEEC and runs the draw and move initialisers.
- **Move.** `FLDMMDL_SetPos` 0x0216D89C each frame, with the position the
  partner sent; the grid cell through the `FLDMMDL_SetGridNow*` setters.
- **Delete.** `FLDMMDL_DeleteMmdl` 0x0216CF2C when the partner leaves the zone
  or the session.

The port wraps these in three service calls (`docs/mod-services.md`, "Field
avatars"): create an avatar with a sprite and a cell, move it, delete it. The
service gives the avatar an object id outside the game's own range and keeps it
out of the game's event checks, so the local game never treats it as an NPC.

**The zone changes.** The field reloads its entities at a map change, so the
service deletes its avatars when the zone changes and recreates the ones whose
player is in the new zone.

## 4. Open questions

1. **Pool headroom.** The pool size comes from `FLDMMDL_CreateSys` 0x0216CC58
   at run time (`FLDMMDL_SysGetMmdlMax` 0x0216D4F0). A crowded map may leave no
   free slot; the service must report that rather than take an NPC's slot.
2. **The avatar's sprite.** A second copy of the player's own sprite needs its
   resources resident (`FLDMMDL_LoadObjCodeParam`, the `FLDMMDL_BLACT_*`
   functions, ov010). Whether two copies of the player sprite can be resident
   at once, or the partner uses another trainer sprite, is to be read from the
   resource code.

## Related documents

- `docs/modding.md`: the overview.
- `docs/mod-services.md`: peer sessions and the field avatar service.
- `docs/mod-events.md`: `route.change`, `map.warp`, `player.step` and the state
  values.
- `docs/field-plan.md`: the field overlays the avatar code runs in.
