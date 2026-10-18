# Payload survey

Research notes: for each game function an earlier version of the event
catalogue bound, the fields the call can supply and the file and line each
comes from, read from aletheia's matched source at revision 80f06a26. The event
names here are that catalogue's labels for the functions. The stable events and
their payloads are `docs/mod-events.md` and `docs/mod-payloads.md`; the builder
of a stable event uses this survey to find where each of its fields comes from.

## 0. The design

### The rules

1. **The port owns the payload struct and fills it at the call.** A mod is
   compiled C and reads the payload at a fixed layout. The game's own structs
   are not exposed, because aletheia is still restructuring them and a mod
   compiled last week would read the wrong offsets. Each event's payload is a
   port-owned struct, populated from the game's state at the moment the call is
   intercepted (`docs/mod-interface.md`, section 4; `docs/mod-events.md`,
   section 2).
2. **The payload is part of the ABI surface.** It is covered by the `api`
   interface version and changes only with it (`docs/mod-events.md`,
   section 5). A change to a port-owned struct is a new `api`.
3. **Err generous.** Removing a field later is cheap and adding one is not, so
   each payload carries everything plausibly useful at that point, including
   the context a handler needs to decide, not only to log. A moment that is
   wrong or unused is cheap to drop later; a moment that is missing is
   expensive to add once mods bind to names (`progress/EVENT_SURVEY.md`, "How
   to read this survey").
4. **A field must be obtainable.** A field comes from the call's arguments,
   from a global or struct field the port can read there, from a call the port
   can make, or from a context the port tracks (section 0a). The payload is the
   situation, not the entity: a handler needs to know not only what happened
   but why, and the why is often not in scope at the call. A field that no
   source can supply is not invented; the event that does carry it is named
   instead.
5. **A field that points at memory the game frees is copied into the payload.**
   `item.gift.mystery` reads a 0xcc-byte card buffer that the game frees at the
   end of the same function (`src/matched/ov/ov021_func_021B6BDC.c:135`). A
   pointer to it would dangle once the call returns, so the port copies the
   bytes into its own storage and a handler reads the copy. Every field that
   points at memory the call frees is copied the same way.
6. **A field with no feeder in the current situation reads a zero or an empty
   reference.** The port trusts a context field only when the event that
   consumes it knows a feeder ran in the same situation. A field with no feeder
   in the current situation reads 0, or an empty `BwPokeRef`, never a value from
   a previous situation. The battle context resets at `battle.start` and the
   field context at `save.load` and the map change, so a stale value is never
   returned (section 0a).

### The common header

Every payload starts with the port's common header, the same `BwEvent` that
`docs/mod-events.md`, section 2, fixes:

```c
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed int     s32;

typedef struct BwEvent {
    u32 size;   /* sizeof the concrete struct, as the port built it */
    u32 kind;   /* the BW_EVENT_* id of the concrete struct */
} BwEvent;
```

The header is the port's, so a handler reads the event's identity and size
without knowing the concrete type (`docs/mod-events.md`, section 2). The
`kind` value is one `BW_EVENT_*` constant per event, named for the event key
in upper case, for example `BW_EVENT_BATTLE_FAINT` for `battle.faint`. The
names are port-owned and frozen with the `api` version (`docs/mod-events.md`,
section 5; still-to-build item 1 below).

### The Pokemon identity

Several battle payloads name a Pokemon. The game's battle code passes a
pointer to a battle poke param, and the Pokemon's own id is the byte at
`+0x19` of that struct: `BTL_POKEPARAM_GetPokeId(p)` returns `p[0x19]`
(`src/matched/ov/func_021D5440.c:26`). The port does not expose the game's
struct. It carries a small port-owned reference instead:

```c
typedef struct BwPokeRef {
    u32 handle;     /* the battle poke param pointer the game passed */
    u8  poke_id;    /* BTL_POKEPARAM_GetPokeId(handle) */
    u8  client_id;  /* the battle client that owns it */
    u8  slot;       /* its position in that client's party */
    u8  reserved;
} BwPokeRef;
```

`handle` is an opaque pointer. A mod passes it to the state accessors and does
not dereference it, so the game's layout can change behind it
(`docs/mod-events.md`, section 4). `poke_id`, `client_id` and `slot` come from
the identity accessors the game already has: `BTL_POKEPARAM_GetPokeId`
(`func_021D5440.c:26`), `BTL_MAIN_GetClientSide`, `BTL_MAIN_GetOwnClientId` and
`BTL_MAIN_GetTrainerSlotOfPoke`. The port fixes which of them it fills
(still-to-build item 3 below). A payload that names a Pokemon names it as a
`BwPokeRef`.

### How the port fills a payload

The slot at the declared address is generated host code (`docs/mod-interface.md`,
section 2). At the call it does this, in order:

1. It saves the arguments and any register values the event needs.
2. It builds the concrete struct on its own stack, sets `h.size` to
   `sizeof` the struct and `h.kind` to the event's `BW_EVENT_*` id, and fills
   the members from the arguments, from the accessor calls the function itself
   makes at that point, and from the battle context (section 0a).
3. It calls every handler bound to the event, in load order, with
   `&payload` (`docs/mod-events.md`, section 3).
4. It calls the original unless a handler returned `BW_EVENT_BLOCK`
   (`docs/mod-interface.md`, section 4).

For an event the catalogue states as "the return of" an address, such as
`catch.result`, the slot saves the arguments, calls the original, and then
builds the struct with the arguments and the return, and fires the handlers
after. The catalogue states the firing point per row (`docs/mod-events.md`,
section 1).

The struct lives on the slot's stack for the call. The port may reuse the
storage at the next event, so a handler must not keep the pointer
(`docs/mod-interface.md`, section 4).

### How a mod reads a payload

A handler takes the mod's own opaque pointer and a pointer to the event:

```c
typedef int (*BwEventHandler)(void *mod_state, const BwEvent *event);
```

The handler reads `event->kind`, casts `event` to the concrete struct for that
kind, and reads its members. The pointer is valid only for the call. The
return is `BW_EVENT_CONTINUE` or `BW_EVENT_BLOCK` (`docs/mod-events.md`,
section 2).

```c
static int on_faint(void *mod_state, const BwEvent *event)
{
    const BwEventBattleFaint *f;
    if (event->kind != BW_EVENT_BATTLE_FAINT)
        return BW_EVENT_CONTINUE;
    f = (const BwEventBattleFaint *)event;
    /* read f->mon, f->side, f->count ... */
    return BW_EVENT_CONTINUE;
}
```

### Kinds of catalogue row

The battle area of `docs/mod-events.md`, section 1, has 72 rows in three
kinds (`docs/mod-events.md`, "The four kinds"): 37 events, 24 state entries and
11 override points. This document fixes a payload for the 48 a mod acts on at
the call: 36 events, 5 state entries and 7 override points.

- **36 events.** A slot fires the event and a payload struct is built. Each
  gets a field table below. (The two `whiteout.begin` events share one entry.)
- **5 state entries.** A mod reads the value through an accessor, and no slot
  fires. There is no payload struct. Each is stated as a read, with the value
  and the inputs the accessor computes from.
- **7 override points.** A mod replaces or wraps the function, and the values
  it sees are the function's own arguments and return, not a `BwEvent`. Each
  is stated as an argument set.

### Source files

Short names used in the tables below map to these files under
`src/matched in the aletheia checkout`:

- `ov010_func_0216E7C8.c`, `ov010_func_0216EAB0.c`, `ov010_func_0216EE20.c`,
  `ov010_func_0216EE7C.c`, `ov010_func_0216EFF0.c`, `ov010_func_02158A20.c`,
  `ov010_func_02158AD4.c`
- `ov021_func_021AA5CC.c`, `ov021_func_021AEABC.c`,
  `ov021_func_021AEB3C.c`, `ov021_func_021AED24.c`
- `ov093_func_021C5200.c`, `ov093_func_021CC668.c`,
  `ov093_func_021D1290.c`, `ov093_func_021CBA40.c`,
  `ov093_func_021CBD48.c`, `ov093_func_021D2320.c`,
  `ov093_func_021D5B68.c`, `ov093_func_021D69B8.c`,
  `ov093_func_021BB8BC.c`, `ov093_func_021BA238.c`,
  `ov093_func_021BE95C.c`, `ov093_func_021C6564.c`,
  `ov093_func_021BF2EC.c`, `ov093_func_021C1A10.c`,
  `ov093_func_021C1910.c`, `ov093_func_021CB730.c`,
  `ov093_func_021CC2C0.c`, `ov093_func_021C557C.c`,
  `ov093_func_021CBFA0.c`, `ov093_func_021B9090.c`,
  `ov093_func_021B90BC.c`, `ov093_func_021D7FF8.c`,
  `ov093_func_021B9244.c`, `ov093_func_021D1960.c`,
  `ov093_func_021C5308.c`
- `ov094_func_021F93F4.c`, `ov094_func_021FBD44.c`,
  `ov094_func_021FBD88.c`, `ov094_func_021FC334.c`,
  `ov094_func_021FC374.c`, `ov094_func_021FBDD4.c`
- `func_02188CE8.c`, `func_0216EF84.c`, `func_021C84D8.c`,
  `func_021D67D0.c`, `func_021D2CA8.c`, `func_021B85A0.c`,
  `func_021D5440.c`
- `arm9/func_02013090.c`, `arm9/func_0201347C.c`,
  `arm9/unit_020271F8.c`

The battle and field areas use these short names; the inventory, progression
and system areas cite paths relative to `src/matched` directly.

## 0a. The contexts

The payload is the situation, not the entity. Five mechanisms each need state
that is not in scope at the call, and the port keeps one tracked record for
each: the battle context, the field context, the item context, the progression
contexts and the network context. Each is part of the ABI, because a payload
reads it (`docs/mod-events.md`, section 5). A stale context field is worse than
a missing one, because it is wrong rather than absent, so each record below
names what feeds every field and where it resets. A field with no feeder in the
current situation reads a zero or an empty reference (design rule 6).

### The battle context

The damage that faints a Pokemon is applied a call before the faint reward
step, so the attacker, the move and the damage are off the stack by the time
`battle.faint` fires. The port therefore keeps a port-owned battle context: a
small record it updates as the battle's own events pass, and reads when it
builds a payload.

The context is one struct per battle:

```c
typedef struct BwBattleContext {
    u32       active;            /* a battle is in progress */
    u32       battle_kind;       /* the battle kind */
    u32       mode;              /* single, double, multi, triple, rotation */
    u32       trainer_battle;    /* a trainer battle, not a wild one */
    u32       turn;              /* the game's turn count */
    u32       sides[2];          /* the two sides exist */
    u32       trainer[2];        /* each side's trainer record */
    BwPokeRef active_mon[2];     /* each side's active Pokemon */
    u16       last_move;         /* the last move used */
    u16       last_move_side;    /* the side that used it */
    u32       last_damage;       /* the last damage dealt */
    BwPokeRef last_attacker;     /* the Pokemon that used it */
    u32       weather;           /* the field weather */
    u32       terrain;           /* the field terrain or room */
    BwPokeRef last_faint;        /* the last fainted Pokemon */
    u32       last_faint_owner;  /* the side that owned it */
    u32       last_exp_gain;     /* the last experience gain */
} BwBattleContext;
```

Every field, what feeds it, and where it is reset:

| Field | Fed by | Reset at | Why a mod wants it |
|---|---|---|---|
| `active` | `battle.start` sets it to 1 | `battle.finish` sets it to 0 | a handler tests whether a battle is in progress |
| `battle_kind` | `battle.kind.notify`, parameter `id` (`ov010_func_0216EE20.c:27`) | `battle.start` | tell a wild battle from a scripted or multi one |
| `mode` | `encounter.trainer`, parameter `mode` (`ov021_func_021AA5CC.c:49`); `battle.trainer.multi`, which passes mode 1 (`ov021_func_021AEB3C.c:46`) | `battle.start` | the battle format, single to rotation |
| `trainer_battle` | `encounter.trainer` fires only for a trainer battle (`ov021_func_021AA5CC.c:49`); `battle.trainer.begin`, `battle.trainer.multi`, `battle.trainer.withmon` | `battle.start` | a wild battle has no trainer to read |
| `turn` | `BTL_SERVER_FLOW_GetTurnCount(BTL_SERVER_FLOW_GetTurnRecord(flow))`, a `u16` at `flow + 0x2c` (`func_021C812C.c:26`; `func_021C8138.c:27`); fed by `battle.move.start` and every event that holds the flow | `battle.start` | a rule keys a cap or a penalty on the turn number |
| `sides[2]` | `battle.start`; the side of a client is `BTL_MAIN_GetClientSide` (`ov093_func_021B8BC4.c:16`) and the local client is `BTL_MAIN_GetOwnClientId` (`func_021B8FF4.c:28`) | `battle.finish` | a payload names a side, and a rule maps a mon to its owner |
| `trainer[2]` | `encounter.trainer`, the trainer record at `ov021_func_021AA5CC.c:57`; `battle.trainer.begin`, `battle.trainer.multi`, `battle.trainer.withmon`, the record field at `ov021_func_021AEABC.c:49` | `battle.finish` | a rule reads each side's trainer |
| `active_mon[2]` | `battle.move.used`, parameter `a` (`ov093_func_021C1A10.c:29`); `battle.damage.dealt`, parameter `a` (`ov093_func_021C1910.c:29`); `battle.end`, the lead mon (`ov010_func_0216EE7C.c:77`); the side from `BTL_MAIN_GetClientSide` | `battle.start` | a handler needs each side's active Pokemon |
| `last_move` | `battle.move.used`, `q->f0` (`ov093_func_021C1A10.c:33`); `battle.move.start`, parameter `v` (`ov093_func_021BE95C.c:32`) | `battle.start` | the move a faint, a damage or a completion is attributed to |
| `last_move_side` | `battle.move.used`, the side of the acting mon | `battle.start` | whose move caused a later event |
| `last_damage` | `battle.damage.dealt`, parameter `v` (`ov093_func_021C1910.c:35`) | `battle.move.start`, so a move that deals no damage does not inherit the previous turn's number; and `battle.start` | the damage that felled a Pokemon |
| `last_attacker` | `battle.damage.dealt`, parameter `a` (`ov093_func_021C1910.c:29`); `battle.move.used`, parameter `a` | `battle.start` | the cause of a faint or a damage |
| `weather` | `*(int *)BTL_MAIN_GetFieldSituation(flow->f4)`, read at `ov093_func_021CBD48.c:71` (`BTL_MAIN_GetFieldSituation` is `*p + 8`, `ov093_func_021B8714.c:15`); fed by `battle.setup.wild` and every event that holds the flow | `battle.start` | a rule changes damage or a catch rate by weather |
| `terrain` | `*(int *)(BTL_MAIN_GetFieldSituation(flow->f4) + 4)`, read at `ov093_func_021CBD48.c:51`; `BTL_SERVER_FLOW_GetFieldSituationType` is the same read (`func_021C8174.c:22`) | `battle.start` | a rule reads the room or terrain |
| `last_faint` | `battle.faint`, the mon at `ov093_func_021C5200.c:51`; `battle.faint.client`, the mon at `ov093_func_021D2320.c:37` | `battle.start` | a Soullink rule pairs the faint |
| `last_faint_owner` | `battle.faint`, the side at `ov093_func_021C5200.c:48` | `battle.start` | mirror the faint to the right player |
| `last_exp_gain` | `battle.exp`, parameter `remaining` (`ov093_func_021D69B8.c:41`) | `battle.start` | the cause of a level up |

#### Why the battle reset points are what they are

A stale context field is worse than a missing one, because it is wrong rather
than absent. The port therefore resets the whole context at `battle.start`,
which is the one event every battle passes through, and clears `active` at
`battle.finish`, the event that ends it. A field read at an event is only
trusted when the event that consumes it knows a feeder ran in the same battle.
A field with no feeder in the current battle reads 0 or an empty `BwPokeRef`,
never a value from a previous battle.

`last_damage` is reset a second time, at `battle.move.start`. A faint from a
status move, a switch, a hazard or a weather tick has no damage in the current
move, and the previous move's damage would be a wrong cause. Resetting at the
move start makes the cause of such a faint read 0, which is correct: the faint
has no damage cause in this move. `last_move` and `last_attacker` are not
reset at the move start, because a faint that follows a move needs them. The
context does not name which status, hazard or weather tick caused a non-damage
faint; that cause is not obtainable from the fields above and is carried by
still-to-build item 11.

`last_exp_gain` is fed by `battle.exp` and read by the level up events. The
game applies the level up inside `BTL_POKEPARAM_AddExp` and notifies it later
through `battle.levelup` and `battle.levelup.main`, so the gain is out of
scope at the notify and must be remembered.

### The field context

The field context is the port's record of where the player is and what the last
field check saw. It feeds the map, talk, warp and ledge payloads. Each row
names the event that feeds it and where it is cleared.

| Context field | Type | Where it comes from | Fed by | Cleared or replaced |
|---|---|---|---|---|
| `zone_id` | u16 | `FIELDMAP_GetZoneId` reads +0xD8, `func_02188C9C.c:22` | `route.change`, `route.header`, `field.map.event` | Replaced at the next `route.change`; cleared at `save.load` |
| `prev_zone_id` | u16 | The `zone_id` before the change, held by the port | `route.change`, `route.header` | Replaced at the next `route.change` |
| `player_pos` | `VecFx32` | `FIELD_PLAYER_GetPos`, `func_021A3038.c:30`; the pointer is stored by `FIELD_EVENT_CHECK_InitInput` at `ov021_func_0218A4E0.c:127` | Every field check | Replaced each frame |
| `player_grid` | s16 x, s16 z | `FLDMMDL_GetGridNowX`, `FLDMMDL_GetGridNowZ`, `ov021_func_0218B6F8.c:41-42` | Every field check | Replaced each frame |
| `player_dir` | u16, 0..3 | `FLDMMDL_GetDirDisp`, `ov021_func_0218A4E0.c:126` | Every field check | Replaced each frame |
| `front_grid` | s16 x, s16 z, s16 y | `FIELD_PLAYER_GetFrontGridPos`, `func_021A3460.c:32` | `field.event.grid`, `field.talk.npc` | Replaced each field check |
| `field_action` | u32 | `FIELD_PLAYER_GetStepKind`, `ov021_func_0218A4E0.c:123` | Every field check | Replaced each frame |
| `field_phase` | u32 | `FIELD_PLAYER_GetStepPhase`, `ov021_func_0218A4E0.c:124` | Every field check | Replaced each frame |
| `proc_action` | u16 | `FIELD_STATUS_GetProcAction`, `ov021_func_0218A4E0.c:128` | Every field check | Replaced each frame |
| `map_attr` | u32 | `FIELD_PLAYER_GetNowAttribute`, `ov021_func_0218A4E0.c:129` | Every field check | Replaced each frame |
| `last_warp` | record | The operands of the `field.warp.*` call: zone, x, z, transition, kind | `field.warp.position`, `.plain`, `.event`, `.exit`, `.fixedzone`, `.position3d`, `.rail` | Consumed at `field.map.arrival`; replaced at the next warp |
| `season` | u8 | `GAME_DATA_GetSeason`, `func_0215ABD0.c:39` | `field.season`, `route.change`, `field.map.event` | Replaced at the next season read; cleared at `save.load` |
| `skill_flags` | u32 bits | `FIELD_SKILL_CHECK_FillUseFlags` sets bits 0..9, `ov010_func_02164480.c:49` | `field.skill.flags` | Replaced when the skill flags are refilled |
| `player_object_code` | u32 | `GAME_DATA_GetPlayerObjectCode`, `ov010_func_02164480.c:50` | `field.skill.flags` | Replaced when the skill flags are refilled |
| `comm_kind` | u32 | `GAME_COMM_GetKind`, `ov021_func_02189BD0.c:280` | `field.event.grid`, `field.map.event` | Replaced each field check |
| `remote_player` | handle | `INTRUDE_FIELD_FindRemotePlayerAhead` out slot, `ov021_func_02189BD0.c:294` | `field.event.grid` | Cleared when the talk is created |
| `talk_target` | handle | `FIELD_EVENT_CHECK_SearchGridFrontMmdl` return, `ov021_func_02189BD0.c:311` | `field.event.grid` | Cleared when the talk is created |
| `talk_script_id` | u16 | `EVENTDATA_SYSTEM_SearchBgEventScriptIdTalk` return, `ov021_func_02189BD0.c:323` | `field.event.grid` | Cleared when the talk is created |
| `current_talk` | handle | The task `FIELD_WFBC_DATA_LOCAL_CreatePersonTalkEvent` returns, `ov020_func_021870C4.c:100` | `field.talk.npc`, `field.talk.comm` | Cleared when the talk main returns 1 |

The context resets as a whole at `save.load` (0x02012C18), when the field is
reinitialised, and at `field.map.event` step 6, when the map change completes
(`ov020_func_021841C0.c:189`). The zone and position reset at every
`route.change`. The last warp is consumed at `field.map.arrival`, so a warp that
never arrives leaves a stale `last_warp`; the port clears it when the map change
completes.

### The item context

The item context is the port's record of the item in play, the shop, the gift
and the move write. It feeds the inventory payloads. Each group names what
feeds every field and where it clears.

#### The last item used, and on whom

| Context field | Type | Fed by | Cleared or replaced |
|---|---|---|---|
| `item_id` | u16 | `item.use.field`, the item id at `Obj00009.f280 + 0x44` (`src/matched/ov/ov091_func_021BC350.c:63`); `battle.item.use`, argument `move` (`src/matched/ov/ov093_func_021CB730.c:81`) | `field.bag.closed` (`SCRCMD_PROC_OnBagClosed` 0x0215C700), `item.use.done`, `item.use.continue`; a new `item.use.field` overwrites it |
| `item_use_kind` | u32 | `PLIST_MENU_GetItemUseKind(item_id)` (`src/matched/ov/ov091_func_021BBE74.c:23`) | as `item_id` |
| `target_mon` | party handle | `item.use.field`, `Obj00009.f38` (`src/matched/ov/ov091_func_021BC350.c:77`); `battle.item.use`, `BTL_SLOT_GetPoke(party, slot)` (`src/matched/ov/ov093_func_021CB730.c:107`) | as `item_id` |
| `target_party_index` | u32 | `item.use.field`, the target's party row (`src/matched/ov/ov091_func_021BC350.c:70`) | as `item_id` |
| `target_row` | u32 | `item.use.field`, `Obj00009.f2C` (`src/matched/ov/ov091_func_021BC350.c:70`) | as `item_id` |
| `user_mon` | party handle or battle mon | `item.use.field`; `battle.item.use`, argument `a` (`src/matched/ov/ov093_func_021CB730.c:81`) | as `item_id` |
| `user_side` | u8 | `battle.item.use`, `BTL_MAIN_PokeIdToClientIdThunk(poke_id)` (`src/matched/ov/ov093_func_021CB730.c:100`) | as `item_id` |
| `in_battle` | bool | set when `battle.item.use` fires | as `item_id` |
| `move_slot` | u16 | `item.use.field`, argument `v` (`src/matched/ov/ov091_func_021BC350.c:115`) | as `item_id` |
| `item_removed` | bool | `POKELIST_RemoveBagItem` (0x021BA670) (`src/matched/ov/ov091_func_021B7CC4.c:126`) | as `item_id` |

The item is applied by `PLIST_DEMO_UseItemOnMon` 0x021BE2B4
(`src/matched/ov/ov091_func_021BE2B4.c:46`). The item leaves the bag at
`POKELIST_RemoveBagItem`, not at the event.

#### The current shop

| Context field | Type | Fed by | Cleared or replaced |
|---|---|---|---|
| `shop_id` | u32 | `shop.start`, `SCRIPT_WORK_ReadWorkValue(a,b)` (`src/matched/ov/ov021_func_021B2FD8.c:95`) | when the shop task frees the block, `SCRCMD_PROC_FLD_StepShopTask` case 7, `func_020307B0(*slot)` (`src/matched/ov/ov021_func_021B3158.c:131`) |
| `work` | ptr | `shop.start` allocates it (`src/matched/ov/ov021_func_021B2FD8.c:103`) | as `shop_id` |
| `bag` | ptr | `GAME_DATA_GetMyItem(s)` (`src/matched/ov/ov021_func_021B2FD8.c:108`) | as `shop_id` |
| `misc_save` | ptr | `GAME_DATA_GetMiscSave(s)` (`src/matched/ov/ov021_func_021B2FD8.c:106`) | as `shop_id` |
| `trainer_record` | ptr | `GAME_DATA_GetTrainerRecord(s)` (`src/matched/ov/ov021_func_021B2FD8.c:110`) | as `shop_id` |
| `save_block_3a` | ptr | `func_020071CC(save_control, 0x3a)` (`src/matched/ov/ov021_func_021B2FD8.c:109`) | as `shop_id` |
| `msg_bg` | ptr | `FIELDMAP_GetMsgBg(field_map)` (`src/matched/ov/ov021_func_021B2FD8.c:101`) | as `shop_id` |
| `shop_kind` | u8 | `shop.start`, `w->b2` (`src/matched/ov/ov021_func_021B2FD8.c:112-133`) | as `shop_id` |

#### The daily gift index and date

| Context field | Type | Fed by | Cleared or replaced |
|---|---|---|---|
| `gift_index` | u32 | `item.gift.daily.index` (`SCRCMD_POKEMON_FLD_GetDailyGiftIndex` 0x021B73B8) (`src/matched/ov/ov021_func_021B73B8.c:43-48`) | after `item.gift.daily` reads it; the next index read overwrites it |
| `month` | u32 | `FIELD_CLOCK_GetMonth(h)` (`src/matched/ov/ov021_func_021B73B8.c:43`) | as `gift_index` |
| `day` | u32 | `FIELD_CLOCK_GetDay(h)` (`src/matched/ov/ov021_func_021B73B8.c:44`) | as `gift_index` |
| `special_date` | bool | `SCRCMD_POKEMON_FLD_IsSpecialDate(x,y)` (`src/matched/ov/ov021_func_021B73B8.c:45`) | as `gift_index` |

#### The mystery gift card

| Context field | Type | Fed by | Cleared or replaced |
|---|---|---|---|
| `card` | 0xcc-byte buffer | `item.gift.mystery`, `PDW_POSTMAN_FindMysteryCard(h, &x, buf)` (`src/matched/ov/ov021_func_021B6BDC.c:93`) | the game frees the buffer at `MYSTERY_DATA_Delete(h)` (`src/matched/ov/ov021_func_021B6BDC.c:135`), so the port copies it at the call; nothing is cleared later |
| `card_index` | u32 | `FindMysteryCard` writes it into `x` (`src/matched/ov/ov021_func_021B6BDC.c:93`) | as `card` |
| `gift_kind` | u8 | `PDW_POSTMAN_GetMysteryGiftType(h)` (`src/matched/ov/ov021_func_021B6BDC.c:94`) | as `card` |
| `handler_id` | u32 | `sMysteryGiftKindHandlers[k].id` (`src/matched/ov/ov021_func_021B6BDC.c:95`) | as `card` |

#### The relearn selection

| Context field | Type | Fed by | Cleared or replaced |
|---|---|---|---|
| `relearn_mon` | party handle | `move.relearn` stores it in the `Relearn` record (`src/matched/ov/ov010_func_0215C89C.c:104`) | the record is owned by the ov173 proc and no event fires at the move write; the port either tracks through to the ov173 write or accepts the gap |
| `relearn_moves` | list ptr | `WAZA_OSHIE_CreateMoveList(mon, 4)` (`src/matched/ov/ov010_func_0215C89C.c:102`) | as `relearn_mon` |
| `session` | status ptr | `GAME_DATA_GetMyStatus(w)` (`src/matched/ov/ov010_func_0215C89C.c:101`) | as `relearn_mon` |

#### The last move written

| Context field | Type | Fed by | Cleared or replaced |
|---|---|---|---|
| `move_id` | u16 | `move.learn`, `move.set`, `move.slot`, `move.push` | not needed if read at the call; a later write overwrites it |
| `move_slot` | u32 | `move.learn`, `move.set`, `move.slot` | as `move_id` |
| `mon` | party handle | `move.learn`, `move.set`, `move.slot`, `move.push` | as `move_id` |
| `method` | set, push or delete | `move.set` branch, `move.push` caller | as `move_id` |

### The progression contexts

The port tracks the following. A field named in a payload table as `context`
comes from here. A stale context field is worse than a missing one, so each is
given with what feeds it and where it clears.

#### The script VM and the script work (the cause)

Every field VM command in this area is a handler with the signature
`int handler(void *vm, void *work)`. The first argument is the VM, the second
is the work object the game passes. `SCRCMD_WORK_GetGameData` reads the game
data through the work's pointer (`ov010_func_0215A5E4.c:26`, `((struct
Obj00005 *)a0)->f20` then `->f8`); `SCRCMD_WORK_GetGameSystem` reads the game
system through the same pointer (`ov010_func_0215A5DC.c:26`, `->f4`);
`SCRCMD_WORK_GetScriptWork` reads the script work through the same pointer
(`ov010_func_0215A5F4.c:31`, `->f0`). `SCRCMD_WORK_GetHeapId` reads a `u16` at
the work's `+0x00` (`ov010_func_0215A5CC.c:25`).

The port tracks:

- `vm`: the VM handle, the call's `a0`. Feeds every field VM command payload.
  Cleared when the VM returns, at the end of the script frame.
- `script_work`: `SCRCMD_WORK_GetScriptWork(work)`. The script frame that ran
  the command. Cleared with `vm`.
- `game_data`: `SCRCMD_WORK_GetGameData(work)`. The live game data. Durable for
  the session; reset at `save.load` (`GAME_DATA_LoadFromSave`, 0x02012C18) and
  at `bw_ds_reset`.

The script work is the cause a handler reads to know which script or which
opcode drove the event. The catalogue derives the field VM's opcode set from
the 609-entry table at `d_0217062C` (`docs/mod-events.md`, section 0).

#### The party and the box (the circumstances)

- `party`: `GAME_DATA_GetMyParty(game_data)`, a handle at game data `+0x194`
  (`arm9/unit_02012924.c:159-162`). Read at every party add.
- `party_count`: `POKEPARTY_GetCount(party)` (0x0201AA18), read before and after
  an add.
- `party_capacity`: `POKEPARTY_GetCapacity(party)` (0x0201AA14).
- `box`: `GAME_DATA_GetBoxManager(game_data)`, at `+0x19C`
  (`arm9/unit_02012924.c:164-167`).
- `box_count`: `func_02007984(box)`, the box fill, tested against 0x2D0
  (`ov010_func_0215C4C8.c:46`).

These are live, not save-block state. The party handle persists across a save
and reload with the game data.

#### The event work block (the flag values)

The event flags are a flat bit array. The live pointer is
`GAME_DATA_GetEventWork(game_data)`, a `u32` at game data `+0x1AC`
(`arm9/unit_02012924.c:189-192`). The save copy is save block 0x2d,
`GAME_DATA_GetEventWorkFromSave(sys)`, which is `func_020071CC(sys, 0x2d)`
(`arm9/func_02012FE4.c:17`). The port exposes the block, not one flag.

`ZONEDATA_GetEventFlagAddress(bitmap, id)` maps a flag id to the byte that holds
it; `ZONEDATA_SetEventFlag` sets bit `id % 8` of that byte
(`arm9/func_02014314.c:18-24`); `ZONEDATA_ResetEventFlag` clears it
(`arm9/func_0201433C.c:18-24`); `ZONEDATA_CheckEventFlag` tests it
(`arm9/func_020142E8.c:47-57`). A null byte pointer reads as clear.

What feeds it: script flag commands and fixed set points. Where it resets: a
route change clears flag 0x964 (`FIELD_MAP_CHANGE_SetupZone`, 0x021643E8, via
`ZONEDATA_ResetEventFlag`, `ov010_func_021643E8.c:65`); `ZONEDATA_ClearTimeFlags`
clears the run 0xAA0..0xB5F (`arm9/unit_020143CC.c:56`); the block is otherwise
durable save state.

#### The misc save block 0x34 (badges, research, money, slot state)

The misc block is save block id 0x34: `GAME_DATA_GetMiscSave(sys)` is
`func_020071CC(sys, 0x34)` and `BEACON_GetBlock` is the same lookup
(`arm9/unit_0200C0E8.c:126-129`). Its layout is read off the accessors
(`arm9/unit_0200C0E8.c:92-117`):

- `+0x00` money, `BEACON_GetPoints` (`arm9/unit_0200C0E8.c:261-264`).
- `+0x04` the flag word, `BEACON_GetFlag` reads bit `bit`
  (`arm9/unit_0200C0E8.c:238-241`), `BEACON_SetFlag` sets it
  (`arm9/unit_0200C0E8.c:243-246`), `BEACON_CountFlags` counts the set bits
  (`arm9/unit_0200C0E8.c:248-259`).
- `+0x44` the research mode (rank), `BEACON_GetMode`/`BEACON_SetMode`
  (`arm9/unit_0200C0E8.c:228-236`).
- `+0x45` the slot state, two bits per index, `BEACON_SetSlotState`/`GetSlotState`
  (`arm9/unit_0200C0E8.c:307-315`).
- `+0x46` the research entry count, `BEACON_GetEntryCount`/`SetEntryCount`
  (`arm9/unit_0200C0E8.c:327-335`).
- `+0x47` three recent ids, `BEACON_GetRecentId`/`SetRecentId`
  (`arm9/unit_0200C0E8.c:337-345`).
- `+0x4A` three recent values, `BEACON_GetRecentValue`/`SetRecentValue`
  (`arm9/unit_0200C0E8.c:347-355`).
- `+0x50` the last-time `u64`, `BEACON_GetLastTime`/`SetLastTime`
  (`arm9/unit_0200C0E8.c:368-377`).

What feeds it: the badge bits come from `SCRCMD_WORK_CmdSetMiscFlag`
(`ov010_func_0215AC68.c:47`); the money from `BEACON_AddPoints`/`SubPoints`;
research from `SCRCMD_RESEARCH_CmdStartMission` and friends. Where it resets: it
is save block state, durable until `bw_ds_reset` or a later game write; the
research fields are cleared by `SCRCMD_RESEARCH_CmdClearMission`
(`ov010_func_0216B6DC.c:47-49`).

#### The Pokedex block 0x37 (the dex state)

The dex is save block 0x37: `ZUKAN_SAVE_GetFromGameData(game_data)`
(0x0200CA1C). The caught bitmap is `p->unk008`, 0x54 bytes at `+0x08`, bit
`(id - 1)` (`arm9/unit_0200CDE0.c:52-65, 116`). `ZUKAN_SAVE_IsCaught` tests it
(`arm9/unit_0200CED4.c:51-65`). The seen flags are the gender and form tables
`unk05C`, `unk1AC`, `unk2FC` (`arm9/unit_0200CFA0.c:41-45`), set by
`ZUKAN_SAVE_SetSeenGenderFlag` and `ZUKAN_SAVE_SetSeenFormFlag`
(`arm9/unit_0200CFA0.c:90-91`). The national-dex flag is
`ZUKAN_SAVE_IsNationalDexEnabled` (0x0200CA38), enabled by
`ZUKAN_SAVE_EnableNationalDex` (`func_0215AFE0.c:37`). `unk4D0` holds the first
value seen for species 0x147 (`arm9/unit_0200CDE0.c:109-111`).

What feeds it: `ZUKAN_SAVE_RegisterCaught` (0x0200CDE0) on a catch and on every
party or box add (`ov010_func_0215C510.c:51`); `ZUKAN_SAVE_RegisterSeen`
(0x0200CFA0) on a sighting. Where it resets: save block state, durable until a
new save or `bw_ds_reset`.

#### The badge date block 0x21

`TRCARD_SIGN_SetBadgeDate(obj, i, a, b, c)` stamps record `i` with three bytes,
marks the parallel entry `obj->unk638[i] = 0xF0`, and records the wall clock as
a second count (`arm9/unit_02008A7C.c:93-108`). The object is save block 0x21,
reached by `GAME_DATA_GetSaveBlock21` (`ov010_func_0215AC68.c:49`). The date
parts come from `GF_STANDARD_SetField00_F4E4` filling a four-int record
(`ov010_func_0215AC68.c:48`). What feeds it: the badge award path
`flag.misc.set`. Where it resets: save block state, durable.

#### The badge is state, not a call

A badge is a flag write, not a function call (`docs/mod-events.md`, section 1a).
There is no address the game enters to award a badge. The bit is set inside
`SCRCMD_WORK_CmdSetMiscFlag`, which calls `BEACON_SetFlag` on the misc block and
then stamps the date (`ov010_func_0215AC68.c:47-49`). `BEACON_SetFlag` is an
override point (`badge.write`), not an event: it fires whenever any caller sets
any bit of the misc flag word, which is not one semantic moment.

So the badge payload question is what a mod can read around the write:

- At `flag.misc.set`, the bit index is the call's script value `r4`
  (`ov010_func_0215AC68.c:44`), and the date parts are filled just after
  (`ov010_func_0215AC68.c:48-49`). This is the one call site where a mod sees
  which bit was set and when. The bit index is the badge index for bits 0..7.
- At `badge.write`, the override receives the block pointer and the bit, both
  arguments. A mod reads the whole flag word there.
- `badge.state` reads one bit (`BEACON_GetFlag`), `badge.count` counts the bits
  (`BEACON_CountFlags`), `badge.profile` builds the eight-bit mask by looping
  bits 0..7 (`ov010_func_0216F40C.c:264-268`), `badge.date` stamps the date.

A mod that wants "a badge was awarded" binds `flag.misc.set` and reads the bit,
or watches `badge.state`. It cannot get a "badge awarded" event that is not a
flag write, because the game has none.

### The network context

The networking payloads share state that no single call carries. The port keeps
one `BwNetContext` and fills it from the events that do carry the pieces. A
stale field is worse than a missing one, so each field names its feed and its
clear point. The context is cleared at `bw_ds_reset` and at process start
(`sdk/dsmem.h`, `bw_ds_reset`), and the link fields are cleared when
`link.down` fires.

| Context field | Type | Fed by | Cleared at | Why the payloads need it |
|---|---|---|---|---|
| `link_on` | u8 | the `SOCL_LinkIsOn` return at 0x0215CD24 (ov009_func_0215CD24.c:22) | reset, `link.down` | Distinguishes `link.up` from `link.down` and a poll from a change |
| `ap_mac` | u8[6] | `WCM_GetApMacAddress()` at 0x0215CD24 (ov009_func_0215CD24.c:18) | reset, `link.down` | The only peer-side identifier the link predicate offers |
| `link_level` | u32 | `WCM_GetLinkLevel()` (ov009_func_021666C8.c:42) | reset, `link.down` | The signal at a link event |
| `rssi` | u8 | `WCMi_GetRssiAverage()` (ov009_func_021666C8.c:52) | reset, `link.down` | The raw signal behind the level |
| `device_id` | u8 | field `WCMWork.deviceId` (ov009_func_02166710.c:39) | reset | Selects the level thresholds |
| `wm_initialized` | u16 | field `g_02153058.wmInitialized` (WM_Init.c:70, 76) | reset | Tells a live manager from a disabled one |
| `my_aid` | u16 | field `WMArm9Buf.myAid`, set at `wm.connect` to 0 and at `wm.fifo` from the callback (func_0208F84C.c:119; WmReceiveFifo.c:302) | reset | The local station id every frame needs |
| `connected_aid_bitmap` | u32 | field `WMArm9Buf.connectedAidBitmap`, updated at `wm.fifo` (WmReceiveFifo.c:298 to 301) | reset | Which stations are in the group |
| `mp_recv_buf_size` | u16 | field `WMStatus.mp_recvBufSize` (WmReceiveFifo.c:56) | reset, `wm.mp.start` | The receive budget the frames are sized to |
| `port_callback[16]` | u32[16] | field `WMArm9Buf.portCallbackTable`, set at `wm.callback` (WM_SetPortCallback.c:128) | reset | Names the handler a port frame reaches |
| `port_arg[16]` | u32[16] | field `WMArm9Buf.portCallbackArgument`, set at `wm.callback` (WM_SetPortCallback.c:129) | reset | The handler's argument |
| `comm_kind` | u8 | field `Comm.kind` at `comm.update` (func_0202627C.c:50) and `Obj.f5` at the dispatch (func_02026388.c:44) | reset, `network.exit` | The action behind a comm frame |
| `comm_state` | u8 | field `Sub.state` at `comm.update` (func_0202627C.c:38) | reset, `network.exit` | The handshake position |
| `comm_last_kind` | u8 | field `Comm.f06` at `comm.update` (func_0202627C.c:51) | reset | The kind last started |
| `comm_counter` | u32 | field `Comm.f08` at `comm.update` (func_0202627C.c:53) | reset | The request counter |
| `comm_work` | u32 | field `Comm.f14` at `comm.update` (func_0202627C.c:55) | reset | The work the callbacks share |
| `comm_blocked` | u8 | `GAMESYSTEM_SetCommBlocked` at `network.exit` and `network.restart` (ov010_func_0215F238.c:51; func_0215F280.c:37) | reset | Tells an exit from a live comm |
| `dwc_initialized` | u8 | `dwc.init` and `dwc.production` results (func_02059FA8.c:93 to 103) | reset | Tells a live WiFi interface from a disabled one |
| `game_code` | u32 | field `dwci_gamecode`, set at `dwc.init` (func_02059FA8.c:106) | reset | The WiFi game identity |
| `stats_host` | u32 | the `StatsServerHostname` global at `dwc.init` (func_02059FA8.c:110 to 113) | reset | The statistics host |
| `peer` | u32 | the `aid` of `wm.fifo` and `wm.share.recv` (WmReceiveFifo.c:168; WmDataSharingReceiveData.c:77) | reset, `link.down` | The remote station a frame is about |
| `bytes_in_flight` | ring of `{port, aid, seq, dir, len, bytes}` | the send and receive points of the networking area | reset | Orders and replays the last frames; no transport exists to feed it yet |

The `bytes_in_flight` ring has no feed today. The transport is missing, so the
ring stays empty and `net.send` and `net.receive` do not fire.

## 1. Encounter

### `encounter.start` (0x0216E7C8, ov010, block)

Function `EVENT_BATTLE_CreateScriptedBattle(int a0, int a1, int a2, int a3,
int a4)` (`ov010_func_0216E7C8.c:45`). The slot fires at entry, before the
task is created. The catalogue binds `encounter.start` here, not to
`EVENT_BATTLE_CreateEncounterTask` (0x0216E818), the trainer battle task
(`docs/mod-events.md`, section 1c).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `ctx` | u32 | parameter `a0` (`:45`) | the game system; the port reads `GAMESYSTEM_GetGameData` from it (`:57`) |
| `field` | u32 | parameter `a1` (`:45`) | the field object; `FIELDMAP_GetEncount` reads its encounter table (`:58`) |
| `lead` | u32 | parameter `a2` (`:45`) | passed to `EVENT_BATTLE_NotifyLeadSpecies(a2, 0)` (`:56`) and stored at the task's `+0x4` (`:53`) |
| `arg3` | u32 | parameter `a3` (`:45`) | stored at the task's `+0x8` (`:54`) |
| `arg4` | u32 | parameter `a4` (`:45`) | stored at the task's `+0xC` (`:55`) |
| `encounter` | u32 | `FIELDMAP_GetEncount((u8 *)a1)` (`:58`) | the zone's encounter record the call consumes a spot from; a call the port makes at the slot |
| `trainer_record` | u32 | `GAME_DATA_GetTrainerRecord(GAMESYSTEM_GetGameData(a0))` (`:57`) | the trainer record the call touches at field 5 |

A mod wants the lead and the encounter record to tell a wild battle from a
scripted one and to key the route. Nuzlocke's first-encounter rule and
Soullink's mirror both bind here (`docs/modding.md`, "Nuzlocke").

Not reachable at entry: the task (created at `:50`) and its work record
(`:51`). The species the task builds is not chosen at entry. The two events
that carry them instead are `battle.setup.wild`, which fills the wild setup
(`arm9/func_0201347C.c:90`), and `encounter.start.mon`, which names the static
mon's species and level (`ov010_func_0216EAB0.c:86`). A rule that needs the
species reads it there, not here.

### `encounter.start.mon` (0x0216EAB0, ov010)

Function `EVENT_BATTLE_CreateEncounterTaskWithMon(void *a0, u32 a1, u32 a2,
u32 a3, u32 a4, u32 a5, u32 a6, u32 idx)` (`ov010_func_0216EAB0.c:70`). It
calls `EVENT_BATTLE_CreateEncounterTask` (0x0216E818, the trainer battle task)
and then overwrites the party mon from a save entry.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `ctx` | u32 | parameter `a0` (`:70`) | the game system; `GAMESYSTEM_GetGameData(a0)` (`:81`) |
| `field` | u32 | parameter `a1` (`:70`) | the field object passed on (`:82`) |
| `mode` | u32 | parameter `a2` (`:70`) | the setup mode passed on (`:82`) |
| `flag` | u32 | parameter `a3` (`:70`) | the setup flag passed on (`:82`) |
| `battle_kind` | u16 | parameter `a4` (`:70`) | the battle kind passed on (`:82`) |
| `extra` | u32 | parameter `a5` (`:70`) | passed through (`:82`) |
| `flags` | u32 | parameter `a6` (`:70`) | passed through (`:82`) |
| `index` | u32 | parameter `idx` (`:70`) | the save mon-entry index (`:83`) |
| `save_block` | u32 | `GAME_DATA_GetSaveBlock43(GAMESYSTEM_GetGameData(a0))` (`:81`) | the block that holds the static mon entries |
| `species` | u16 | `MISC_SAVEDATA_GetMonEntryCost(data, idx, 0x15)` (`:86`) | the static species; a mod rerolls or checks it |
| `level` | u32 | `MISC_SAVEDATA_GetMonEntryLevel(data, idx)` (`:87`) | the static level |
| `experience` | u32 | `MISC_SAVEDATA_GetMonEntryExperience(data, idx)` (`:88`) | the static exp |
| `pid` | u32 | `MISC_SAVEDATA_GetMonEntryPid(data, idx)` (`:89`) | the static PID; a shiny check |
| `tag` | u32 | `MISC_SAVEDATA_GetMonEntryTag(data, idx)` (`:92`) | the entry tag |
| `name` | u32 | `MISC_SAVEDATA_GetMonEntryName(data, idx)` (`:94`) | the entry name pointer |
| `entry_set` | bool | `MISC_SAVEDATA_GetMonEntryFlag(data, idx)` (`:83`) | whether the entry is set |

### `encounter.trainer` (0x021AA5CC, ov021)

Function `FIELD_ENCOUNT_SetupTrainerBattle(struct Ctx *ctx, void *a1, u32 mode,
u32 flag, void *mon, u32 a5, u16 heap)` (`ov021_func_021AA5CC.c:49`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `setup` | u32 | parameter `a1` (`:49`) | the setup struct the function clears and fills (`:55`) |
| `field` | u32 | `ctx->f08` (`:52`) | the field object passed to `FIELD_ENCOUNT_SetFieldSituation` (`:56`) |
| `owner` | u32 | `ctx->f00` (`:56`) | the second field object of the situation |
| `mode` | u32 | parameter `mode` (`:49`) | selects single, double, multi, triple or rotation (`:61`) |
| `flag` | u32 | parameter `flag` (`:49`) | the multi variant flag (`:63`) |
| `trainer` | u32 | parameter `mon` (`:49`) | the trainer record; `TPOKE_DATA_GetRecordField(mon, 1)` (`:57`) |
| `trainer_group` | u32 | `TPOKE_DATA_GetTrainerGroup(...)` (`:57`) | the trainer class; a mod reads it before the fight |
| `a5` | u32 | parameter `a5` (`:49`) | the tag argument (`:71`) |
| `heap` | u16 | parameter `heap` (`:49`) | the heap id (`:66`) |
| `field_situation` | struct | `FIELD_ENCOUNT_SetFieldSituation(&info, ...)` (`:56`) | weather, time and the other field inputs the setup derives from |

Nuzlocke and Soullink read the trainer before the battle starts
(`docs/modding.md`, "Soullink").

### `encounter.rate` (0x02188CE8, ov021, state)

Function `FIELDMAP_GetEncount(u8 *p)` returns `p->f90`
(`func_02188CE8.c:20`). No slot fires. A mod reads the encounter table pointer
for the field object it holds, or reaches the same field through the state
accessor family of `docs/mod-events.md`, section 4. Value: `{ owner, table }`.
A randomizer weights or rerolls the table behind it (`progress/EVENT_SURVEY.md`,
"Wild encounter table read for a zone and slot").

## 2. Battle start and setup

### `battle.start` (0x02013090, arm9, block)

Function `BTL_SETUP_Create(u32 heap)` (`arm9/func_02013090.c:31`). The slot
fires at entry, before the setup is allocated.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `heap` | u32 | parameter `heap` (`:31`) | the heap id passed to the allocator (`:35`) |

The setup pointer is the function's return, created at `:35`. A pre-handler
sees only the heap id. The block point suppresses the battle, which is why the
event fires before the allocation (`docs/mod-interface.md`, section 4). The
filled setup is carried by `battle.setup.wild`, which is the next event in the
same flow (`arm9/func_0201347C.c:90`); a rule that adjusts the setup reads it
there.

### `battle.setup.wild` (0x0201347C, arm9)

Function `BTL_SETUP_SetupWild(u8 *a0, void *a1, u32 a2, u32 a3, u32 a4,
u16 a5)` (`arm9/func_0201347C.c:90`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `setup` | u32 | parameter `a0` (`:90`) | the setup struct this function fills (`:98`) |
| `game_data` | u32 | parameter `a1` (`:90`) | the save or game data (`:99`) |
| `source_party` | u32 | parameter `a2` (`:90`) | the source party copied in when not 0 (`:103`) |
| `common` | u32 | parameter `a3` (`:90`) | the common init argument (`:99`) |
| `field4` | u32 | parameter `a4` (`:90`) | stored into the setup at `+0x4` (`:106`) |
| `heap` | u16 | parameter `a5` (`:90`) | the heap id for the created party (`:101`) |
| `player_party` | u32 | `POKEPARTY_Create(a5)` (`:101`) | the party the setup holds at `+0x28` (`:102`) |
| `box_status` | bool | `func_020076D0(...)` result (`:115`) | sets setup status flag 0x40 when 0 (`:116`) |

A randomizer sets the wild species and level here, before the game derives
them (`progress/EVENT_SURVEY.md`, "Wild battle setup").

### `battle.kind.notify` (0x0216EE20, ov010)

Function `EVENT_BATTLE_DispatchBattleKindNotify(u16 id, int col)`
(`ov010_func_0216EE20.c:27`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `battle_kind` | u16 | parameter `id` (`:27`) | the battle kind dispatched |
| `column` | int | parameter `col` (`:27`) | the notify column (`:29`) |
| `class` | int | `EVENT_BATTLE_GetBattleKindClass((void *)id)` (`:29`) | the kind's class row in `d_021725C0` |
| `handler` | u32 | `d_021725C0[class][col]` (`:29`) | the handler the game will call |

A mod that needs the battle kind without parsing a script reads it here.

## 3. Battle end and outcome

### `battle.end` (0x0216EE7C, ov010)

Function `EVENT_BATTLE_NotifyBattleEnd(BattleEndCtx *c)`
(`ov010_func_0216EE7C.c:65`). The context is `{ f00, sys at +0x04, st at
+0x08 }`; `st` is `{ mode at +0x00, result at +0x4C, outcome at +0x8C }`
(`:36`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `sys` | u32 | `c->sys` (`:76`) | the game data the party is read from |
| `state` | u32 | `c->st` (`:75`) | the battle state struct |
| `outcome` | u32 | `st->outcome` (`:78`) | 0 loss, 1 win, 2 draw, 3 caught; the result |
| `mode` | u32 | `st->mode` (`:97`) | the battle mode |
| `result` | u32 | `st->result` (`:106`) | the kind-notify argument on a win |
| `lead` | BwPokeRef | `POKEPARTY_GetMember(party, POKEPARTY_FindFirstNonEgg(party))` (`:76`) | the lead party mon the message names |
| `lead_hp` | u16 | `POKE_TOOL_GetPartyValue(mon, 0xA0, 0)` (`:81`) | the lead's current HP |
| `lead_max_hp` | u16 | `POKE_TOOL_GetPartyValue(mon, 0xA1, 0)` (`:82`) | the lead's max HP |
| `lead_status` | u32 | `POKE_TOOL_GetPartyValue(mon, 0x9D, 0)` (`:93`) | the lead's status condition |
| `lead_move_out_of_pp` | bool | the loop `0x36+i` against `0x3A+i` (`:87`) | the lead has a move with no PP |
| `lead_name` | u32 | `POKE_TOOL_GetPartyValue(mon, 0x73, name)` (`:80`) | the lead's nickname buffer |

Nuzlocke and Soullink read the outcome and the lead's state at the end
(`docs/modding.md`, "Nuzlocke"; `docs/mod-events.md`, "The example concepts,
resolved").

### `battle.finish` (0x0216EFF0, ov010)

Function `EVENT_BATTLE_FinishBattleTask(int *p, void *a)`
(`ov010_func_0216EFF0.c:39`). `p` is the battle task; `p[2]` is the work
struct; the state pointer is at `p + 8` (`:40`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `task` | u32 | parameter `p` (`:39`) | the battle task |
| `work` | u32 | `p[2]` (`:41`) | the work struct passed to `MOVE_POKEMON_HandleBattleResult` |
| `sys` | u32 | parameter `a` (`:39`) | the game data (`:40`) |
| `state` | u32 | `*(p + 8)` (`:40`) | the battle state; its `+0x8C` is the field mode (`:40`) |
| `field_mode` | u32 | `state->f8C` (`:40`) | the field mode the task sets |
| `zone_id` | u16 | `GAME_DATA_GetPlayerZoneId(GAME_DATA_GetMyPlayerWork(a))` (`:44`) | the zone the player returns to; a mod keys the end on the route |

### `battle.result.sound` (0x0216EF84, ov010)

Function `EVENT_BATTLE_SetResultSound(int a0)` (`func_0216EF84.c:31`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `task` | u32 | parameter `a0` (`:31`) | the battle task |
| `state` | u32 | `a0->f8` (`:37`) | the battle state |
| `sys` | u32 | `a0->f0` (`:38`) | the game data; `GAMESYSTEM_GetGameData(sys)` (`:39`) |
| `trainer_record` | u32 | `GAME_DATA_GetTrainerRecord(p)` (`:41`) | the trainer record the sound is set on |
| `outcome` | u32 | `*(state + 0x8C)` (`:46`) | the outcome the sound depends on |

### `battle.result` (0x021C84D8, ov093)

Function `BTL_SERVER_FLOW_GetBattleResult(u8 *p)`
(`func_021C84D8.c:28`), a thunk to `BTL_SERVER_FLOW_CheckBattleOutcome`
(`:30`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:28`) | the battle flow struct |

The outcome value is the function's return, computed at `:30`. It is not
present at entry. The events that carry it are `battle.end`, whose `outcome`
member is the stored value (`ov010_func_0216EE7C.c:78`), and
`battle.result.sound`, whose `outcome` member reads the same state
(`func_0216EF84.c:46`). A mod reads it there, or through a wrap of this
function (still-to-build item 2 below).

### `battle.outcome` (0x021CC668, ov093)

Function `BTL_SERVER_FLOW_CheckBattleOutcome(u8 *p)`
(`ov093_func_021CC668.c:36`). It returns 0, 1 or 2 (`:59`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:36`) | the battle flow struct |

The per-side live counts (`live[2]`, `counts[2]`, `:38`) are locals computed
inside the function and are not present at entry. The outcome is the return.
A pre-handler sees only `flow`. The event that carries the outcome is
`battle.end`, whose `outcome` member is the value the game stored
(`ov010_func_0216EE7C.c:78`).

### `battle.result.message` (0x021D1290, ov093)

Function `BTL_CLIENT_StepShowResultMessage(u8 *p, int *state, void *a2)`
(`ov093_func_021D1290.c:46`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `ctx` | u32 | parameter `p` (`:46`) | the client battle context; the battle is at `p->f0` (`:56`) |
| `step` | int | `*state` (`:52`) | the message step |
| `a2` | u32 | parameter `a2` (`:46`) | the third argument |
| `opponent_client` | int | `BTL_MAIN_GetOwnOpponentClient(p->f0, 0)` (`:56`) | the opponent client the message names |
| `mode` | int | `BTL_CLIENT_GetOutcomeForClient(p)` (`:57`) | the client's outcome |
| `multi` | bool | `BTL_MAIN_IsMultiMode(p->f0)` (`:58`) | whether the battle is a multi battle |

## 4. Catch

### `catch.attempt` (0x021CBA40, ov093, block)

Function `BTL_SERVER_FLOW_TryThrowBall(u8 *p, void *a, u16 b)`
(`ov093_func_021CBA40.c:39`). The slot fires at entry, before the roll.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:39`) | the battle flow struct |
| `target` | BwPokeRef | parameter `a` (`:39`); `BTL_POKEPARAM_GetPokeId(a)` (`:49`) | the Pokemon the game passes as the throw's target |
| `resolved` | BwPokeRef | `m` from `BTL_SIDE_GetPokeAtPosThunk(p->f8, buf[i])` (`:51`) | the non-fainted slot the loop resolved the target to |
| `ball` | u16 | parameter `b` (`:39`) | the ball item id (`:56`, `:61`) |
| `competitor` | bool | `BTL_MAIN_GetCompetitor(p->f4)` (`:54`) | whether the thrower is the opponent |
| `target_slot` | u8 | `chosen` after the loop (`:52`, `:59`) | the target's party slot the game picked |
| `target_slot_list` | u8[6] | `BTL_MAIN_DecodeMessageCode(...)` into `buf` (`:49`) | the candidate slots |

A block here forbids the catch, which is Nuzlocke's rule against a second
catch on a route (`docs/mod-interface.md`, section 4). The roll happens inside
`BTL_SERVER_FLOW_CalcCaptureShakes` at `:56`; the catch result is not present
at entry. The event that carries it is `catch.result`, which fires after this
function returns and carries the throw's return (`:65`, `:68`); the internal
roll result joins it when the port captures it (still-to-build item 5 below).

### `catch.result` (the return of 0x021CBA40, ov093)

The slot calls the original and fires after. The arguments are the same as
`catch.attempt`; the return is 1 for a player throw and 0 for a competitor
throw (`ov093_func_021CBA40.c:65`, `:68`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` | the battle flow struct |
| `target` | BwPokeRef | parameter `a` (`:39`) | the Pokemon the ball was thrown at |
| `ball` | u16 | parameter `b` (`:39`) | the ball item id |
| `processed` | int | the return (`:65`, `:68`) | 1 player throw, 0 competitor throw |
| `target_slot` | u8 | `chosen` (`:59`) | the slot the game picked |

The true catch outcome, the shakes `out[1]` and the caught flag `result`
(`:56`), are locals pushed into the battle command stream at `:63`. They are
not on the function's return path and are not reachable at the call boundary
(still-to-build item 5 below).

### `catch.rate` (0x021CBD48, ov093, override)

Function `BTL_SERVER_FLOW_GetBallCatchRate(u8 *p, void *unused, void *q,
int k)` (`ov093_func_021CBD48.c:37`). A mod replaces or wraps it.

| Argument | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:37`) | the battle flow struct |
| `target` | u32 | parameter `q` (`:37`) | the target Pokemon; `BTL_POKEPARAM_HasType(q, ...)` (`:47`) |
| `ball_effect` | int | parameter `k` (`:37`) | the ball effect id the switch tests (`:43`) |
| return | int | the function's return (`:44` to `:83`) | the catch rate, 0x1000 default, higher for a better ball |

Nuzlocke changes the roll through this override (`docs/mod-interface.md`,
section 4).

## 5. Faint

### `battle.faint` (0x021C5200, ov093, block)

Function `BTL_SERVER_FLOW_ProcFaintRewards(u8 *p)`
(`ov093_func_021C5200.c:41`). It walks the fainted list at `p + 0x3e0` and
processes each entry.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:41`) | the battle flow struct |
| `side` | u32 | the literal `0` in `func_021C52C8(p + 0x3e0, 0)` (`:48`) | the side whose fainted list is walked |
| `count` | u32 | `func_021C52C8(p + 0x3e0, 0)` (`:48`) | the number of fainted entries |
| `poke_id` | u8 | `func_021C52E8(p + 0x3e0, 0, i)` (`:51`) | the id of the fainted Pokemon at entry `i` |
| `mon` | BwPokeRef | `BTL_SIDE_GetPokeByIdThunk(p->f8, poke_id)` (`:51`) | the fainted Pokemon |
| `slot_empty` | bool | `func_021C52D8(p + 0x3e0, 0, i)` (`:50`) | whether the entry is skipped |
| `competitor` | bool | `BTL_MAIN_GetCompetitor(p->f4)` (`:52`) | whether the player is the opponent |
| `showdown` | bool | `BTL_SERVER_FLOW_CheckShowdown(p)` (`:52`) | the showdown state |
| `player_side_alive` | bool | `BTL_SERVER_FLOW_CheckPlayerSideAlive(p)` (`:52`) | whether the player's side still has a live mon |
| `win_bgm` | u16 | `BTL_MAIN_GetWinBgm(p->f4)` (`:53`) | the victory track the game queues |
| `rewards_applied` | int | `BTL_SERVER_FLOW_ProcExpForFaintedPoke(p, a)` (`:56`) | whether the faint granted exp |
| `victim_owner` | u32 | the side whose list is walked, the literal `0` at `:48` | the side that owns the fainted Pokemon; a Soullink rule mirrors to that player |
| `cause_attacker` | BwPokeRef | the battle context `last_attacker`, fed by `battle.damage.dealt` (`ov093_func_021C1910.c:29`) | the Pokemon that dealt the fatal damage |
| `cause_attacker_species` | u16 | the context attacker, read with `POKE_TOOL_GetPartyValue(mon, 0x05, 0)` | the killer's species; a Nuzlocke rule records what killed it |
| `cause_attacker_level` | u8 | the context attacker, read with `POKE_TOOL_GetPartyValue(mon, 0x9E, 0)` | the killer's level |
| `cause_move` | u16 | the context `last_move`, fed by `battle.move.used` (`ov093_func_021C1A10.c:33`) | the move that caused the faint |
| `cause_damage` | u32 | the context `last_damage`, fed by `battle.damage.dealt` (`ov093_func_021C1910.c:35`) | the damage that felled it; 0 when the cause is not damage |
| `turn` | u32 | the context `turn`, from `BTL_SERVER_FLOW_GetTurnCount` (`func_021C8138.c:27`) | the turn it happened on |

The three list helpers `func_021C52C8`, `func_021C52D8` and `func_021C52E8`
(`:30` to `:32`) are calls the port makes at the slot. They are not in the
declared set. The function loops over every fainted entry, so the port must
fix whether the event fires once at entry with `count` and `poke_id` of the
current entry, or once per entry (still-to-build item 4 below).

Soullink mirrors the faint to the peer here (`docs/modding.md`, "Soullink").

### `battle.faint.client` (0x021D2320, ov093)

Function `BTL_CLIENT_StepFaintMon(u8 *p, int *st, u32 *c)`
(`ov093_func_021D2320.c:28`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `ctx` | u32 | parameter `p` (`:28`) | the client battle context |
| `step` | int | `*st` (`:30`) | the faint step |
| `poke_id` | u8 | `*c` (`:32`, `:37`) | the fainting Pokemon's id |
| `mon` | BwPokeRef | `BTL_SIDE_GetPokeByIdThunk(p->f4, *c)` (`:37`) | the fainting Pokemon |
| `trainer_slot` | int | `BTL_MAIN_GetTrainerSlotOfPoke(p->f0, p->f4, *c)` (`:32`) | the slot the reset animation runs on |

### `battle.fainted` (0x021D5B68, ov093, state)

Function `BTL_POKEPARAM_IsFainted(void *p)` returns whether the value at
`p + 13` is 0 (`ov093_func_021D5B68.c:21`). No slot fires. A mod reads the
predicate through the state accessor family. Value: `{ handle, fainted }`.
A Nuzlocke rule tests a party member with it to decide whether the run has
declared it dead (`progress/EVENT_SURVEY.md`, "Faint means death").

## 6. Trainer battle starts

### `battle.trainer.begin` (0x021AEABC, ov021)

Function `SCRCMD_StartTrainerBattle(u32 a, u32 b)`
(`ov021_func_021AEABC.c:39`). It reads the script work and creates the
encounter task.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | u32 | parameter `a` (`:39`) | the script VM |
| `command` | u32 | parameter `b` (`:39`) | the command; the script work is read from it (`:44`) |
| `script_work` | u32 | `SCRCMD_WORK_GetScriptWork(b)` (`:44`) | the script work struct |
| `script_id` | u16 | `SCRIPT_WORK_GetScriptId(p)` (`:45`) | the script id |
| `x` | u32 | `SCRIPT_WORK_ReadWorkValue(a, b)` (`:46`) | the trainer id read from the script |
| `y` | u32 | the second read (`:47`) | the trainer id variant |
| `z` | u32 | the third read (`:48`) | the flags |
| `record_field` | u32 | `EVENT_POKE_STATUS_GetRecordField2(x)` (`:49`) | the trainer record field |
| `record_set` | int | `EVENT_POKE_STATUS_IsRecordField2Set(x)` (`:50`) | whether the record field is set |

### `battle.trainer.multi` (0x021AEB3C, ov021)

Function `SCRCMD_StartMultiTrainerBattle(u32 a, u32 b)`
(`ov021_func_021AEB3C.c:35`). Same shape as `battle.trainer.begin` with four
script reads and mode 1.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | u32 | parameter `a` (`:35`) | the script VM |
| `command` | u32 | parameter `b` (`:35`) | the command |
| `script_work` | u32 | `SCRCMD_WORK_GetScriptWork(b)` (`:40`) | the script work struct |
| `script_id` | u16 | `SCRIPT_WORK_GetScriptId(p)` (`:41`) | the script id |
| `x` | u32 | the first `SCRIPT_WORK_ReadWorkValue` (`:42`) | the first argument |
| `y` | u32 | the second read (`:43`) | the second argument |
| `z` | u32 | the third read (`:44`) | the third argument |
| `w` | u32 | the fourth read (`:45`) | the fourth argument |

### `battle.trainer.withmon` (0x021AED24, ov021)

Function `SCRCMD_StartTrainerBattleWithMon(u32 a, u32 b)`
(`ov021_func_021AED24.c:41`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | u32 | parameter `a` (`:41`) | the script VM |
| `command` | u32 | parameter `b` (`:41`) | the command |
| `script_work` | u32 | `SCRCMD_WORK_GetScriptWork(b)` (`:47`) | the script work struct |
| `script_id` | u16 | `SCRIPT_WORK_GetScriptId(p)` (`:48`) | the script id |
| `x` | u16 | the first read (`:49`) | the trainer id |
| `y` | u16 | the second read (`:50`) | the trainer variant |
| `z` | u16 | the third read (`:51`) | the flags |
| `index` | u16 | the fourth read (`:52`) | the mon entry index passed to `EVENT_BATTLE_CreateEncounterTaskWithMon` (`:57`) |
| `record_field` | u32 | `EVENT_POKE_STATUS_GetRecordField2(x)` (`:53`) | the trainer record field |
| `record_set` | int | `EVENT_POKE_STATUS_IsRecordField2Set(x)` (`:54`) | whether the record field is set |

## 7. Experience and level up

### `battle.exp` (0x021D69B8, ov093)

Function `BTL_POKEPARAM_AddExp(struct Obj *p, u32 *remaining,
struct Result *r)` (`ov093_func_021D69B8.c:34`). It adds exp and applies one
level up in place when the threshold is crossed.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mon` | BwPokeRef | parameter `p` (`:34`) | the battle poke param; the party mon is `p->w` (`:46`) |
| `exp_before` | u32 | `p->exp` (`:40`) | the exp before the add |
| `exp_added` | u32 | `*remaining` (`:41`) | the exp the game offers |
| `exp_total` | u32 | `p->exp + *remaining` (`:41`) | the sum before the threshold test |
| `threshold` | u32 | `func_02018584(p->species, ((u8 *)p)[0x141], p->level + 1)` (`:42`) | the exp needed for the next level; a call the port makes |
| `level_before` | u8 | `p->level` (`:39`) | the level before the add |
| `species` | u16 | `p->species` (`:42`) | the species |
| `level_after` | u8 | `r->level` (`:70`) | the level after a gain |
| `hp_delta` | u16 | `p->hp - old_hp` (`:57`, `:71`) | the HP the level gained |
| `stat_deltas` | u16[5] | `r->a` to `r->e` (`:72` to `:76`) | the attack, defense, speed, special attack and special defense gains |
| `leveled` | bool | the return, 1 at `:82`, 0 at `:89` | whether a level was gained |

The port loops, so one call is at most one level (`docs/mod-events.md`,
section 1a). A Nuzlocke level cap binds here: it blocks or clamps the gain
(`progress/EVENT_SURVEY.md`, "Level cap enforcement").

### `battle.levelup.pending` (0x021D67D0, ov093, state)

Function `BTL_POKEPARAM_IsLevelUpPending(struct R3 *p)` returns bit 7 of the
byte at `p + 0x1b` (`func_021D67D0.c:27`). No slot fires. Value:
`{ handle, pending }`. A mod reads it to learn whether a level up is waiting
before it reads the level (`docs/mod-events.md`, section 1).

### `battle.levelup` (0x021BB8BC, ov093)

Function `BTL_SERVER_NotifyLevelUp(u8 *p, u32 *q)`
(`ov093_func_021BB8BC.c:22`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `ctx` | u32 | parameter `p` (`:22`) | the notify context; `fAC` is the nickname buffer (`:23`) |
| `mon` | BwPokeRef | parameter `q` (`:22`); `BTL_POKEPARAM_GetMon(q)` (`:23`) | the Pokemon that leveled |
| `nickname` | u32 | `POKE_TOOL_GetPartyValue(mon, 0x73, p->fAC)` (`:23`) | the nickname buffer the notify sends |
| `level` | u32 | `POKE_TOOL_GetPartyValue(mon, 0x9E, 0)` | the new level; a call the port makes |
| `species` | u32 | `POKE_TOOL_GetPartyValue(mon, 0x05, 0)` | the species; a call the port makes |
| `cause_exp_gain` | u32 | the battle context `last_exp_gain`, fed by `battle.exp` (`ov093_func_021D69B8.c:41`) | the experience gain that caused the level up |
| `turn` | u32 | the context `turn`, from `BTL_SERVER_FLOW_GetTurnCount` (`func_021C8138.c:27`) | the turn the gain happened on |

The game's notify reads only the nickname and hands it to
`GAME_BEACON_SetPokeLevelUp` (`arm9/unit_020271F8.c:115`). The level is read
through the party accessor, not at this call (still-to-build item 7 below).

### `battle.levelup.main` (0x021BA238, ov093)

Function `BTL_MAIN_NotifyLevelUp(char *p, void *m)`
(`ov093_func_021BA238.c:21`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `ctx` | u32 | parameter `p` (`:21`) | the main context; the flow is `p->f8` (`:24`) |
| `flow` | u32 | `p->f8` (`:24`) | the server flow passed to the notify |
| `mon` | BwPokeRef | parameter `m` (`:21`) | the Pokemon that leveled; the friendship is updated first (`:23`) |
| `cause_exp_gain` | u32 | the battle context `last_exp_gain`, fed by `battle.exp` (`ov093_func_021D69B8.c:41`) | the experience gain that caused the level up |

### `battle.levelup.learnmoves` (0x021D2CA8, ov093)

Function `BTL_CLIENT_StepLearnLevelUpMoves(LearnSeq *s, int *state,
void *mon)` (`func_021D2CA8.c:96`). The sequence struct holds `battle` at
`+0x000`, `party` at `+0x004`, `msgWin` at `+0x054`, `level` at `+0x19A` and
`side` at `+0x1A6` (`:53` to `:63`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `seq` | u32 | parameter `s` (`:96`) | the learn sequence |
| `step` | int | `*state` (`:100`) | the learn step |
| `mon` | BwPokeRef | parameter `mon` (`:96`); `BTL_POKEPARAM_GetPokeId(mon)` (`:98`) | the Pokemon learning |
| `level` | u16 | `s->level` (`:106`) | the level whose learnset is scanned |
| `side` | u8 | `s->side` (`:165`) | the client the learn screen belongs to |
| `move` | u16 | `POKE_TOOL_GetLevelUpMove(...)` (`:106`) | the move the level grants |
| `forget_slot` | u8 | `BTLV_CORE_ProcMoveForgetScreen(..., &slot)` (`:177`) | the slot the player chose to forget |
| `old_move` | u16 | `POKE_TOOL_GetPartyValue(pp, slot + 54, 0)` (`:184`) | the move displaced when the slot is overwritten |

A mod that skips or changes the learn prompt binds here
(`progress/EVENT_SURVEY.md`, "Learning a move on level up").

## 8. Moves and damage

The five move events dispatch through `BTL_EVENT_DispatchEventChecked` and set
the event VM's read-only variables. The port carries the same values in the
payload.

### `battle.move.start` (0x021BE95C, ov093)

Function `BTL_SERVER_FLOW_DispatchMoveStartEvent(u8 *p, u8 *a, u32 v)`
(`ov093_func_021BE95C.c:28`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:28`) | the battle flow struct |
| `mon` | BwPokeRef | parameter `a` (`:28`); `BTL_POKEPARAM_GetPokeId(a)` (`:31`) | the acting Pokemon |
| `value` | u32 | parameter `v` (`:28`); var 0x12 (`:32`) | the value the game sets as var 0x12 |
| `event_id` | int | the literal 3 at `BTL_EVENT_DispatchEventChecked(p, 3)` (`:33`) | the dispatched event id |

### `battle.move.done` (0x021C6564, ov093)

Function `BTL_SERVER_FLOW_DispatchMoveDoneEvent(u8 *p, u8 *a, u32 c, u32 d)`
(`ov093_func_021C6564.c:28`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:28`) | the battle flow struct |
| `mon` | BwPokeRef | parameter `a` (`:28`); `BTL_POKEPARAM_GetPokeId(a)` (`:31`) | the acting Pokemon |
| `value12` | u32 | parameter `c` (`:28`); var 0x12 (`:32`) | the value the game sets as var 0x12 |
| `value22` | u32 | parameter `d` (`:28`); var 0x22 (`:33`) | the value the game sets as var 0x22 |
| `event_id` | int | the literal 0x21 (`:34`) | the dispatched event id |
| `move` | u16 | the battle context `last_move`, fed by `battle.move.used` (`ov093_func_021C1A10.c:33`) | the move that completed |
| `turn` | u32 | the context `turn`, from `BTL_SERVER_FLOW_GetTurnCount` (`func_021C8138.c:27`) | the turn it completed on |

### `battle.move.result` (0x021BF2EC, ov093)

Function `BTL_SERVER_FLOW_DispatchMoveResultEvent(u8 *p, u32 a, u32 b, int k)`
(`ov093_func_021BF2EC.c:23`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:23`) | the battle flow struct |
| `value2` | u32 | parameter `a` (`:23`); var 2 (`:27`) | the value the game sets as var 2 |
| `value12` | u32 | parameter `b` (`:23`); var 0x12 (`:28`) | the value the game sets as var 0x12 |
| `event_id` | int | parameter `k` (`:23`) | the dispatched event id |
| `move` | u16 | the battle context `last_move`, fed by `battle.move.used` (`ov093_func_021C1A10.c:33`) | the move the result belongs to |

### `battle.move.used` (0x021C1A10, ov093)

Function `BTL_SERVER_FLOW_RunMoveUsedEvent(u8 *p, u8 *a, u8 *q)`
(`ov093_func_021C1A10.c:29`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:29`) | the battle flow struct |
| `mon` | BwPokeRef | parameter `a` (`:29`); `BTL_POKEPARAM_GetPokeId(a)` (`:32`) | the acting Pokemon |
| `move` | u32 | `q->f0` (`:33`); var 0x12 | the move id |
| `value16` | u8 | `q[6]` (`:34`); var 0x16 | the value the game sets as var 0x16 |
| `event_id` | int | the literal 0x81 (`:35`) | the dispatched event id |

The target of the move is not an argument of this function (still-to-build
item 6 below).

### `battle.damage.dealt` (0x021C1910, ov093)

Function `BTL_SERVER_FLOW_RunDamageDealtEvent(u8 *p, u8 *a, u8 *q, u32 v)`
(`ov093_func_021C1910.c:29`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:29`) | the battle flow struct |
| `attacker` | BwPokeRef | parameter `a` (`:29`); `BTL_POKEPARAM_GetPokeId(a)` (`:32`) | the attacking Pokemon |
| `move` | u32 | `q->f0` (`:33`); var 0x12 | the move id |
| `value16` | u8 | `q[6]` (`:34`); var 0x16 | the value the game sets as var 0x16 |
| `damage` | u32 | parameter `v` (`:29`); var 0x32 (`:35`) | the damage dealt |
| `event_id` | int | the literal 0x4d (`:36`) | the dispatched event id |
| `attacker_species` | u16 | the battle context `last_attacker`, read with `POKE_TOOL_GetPartyValue(mon, 0x05, 0)` | the attacker's species |
| `attacker_level` | u8 | the context attacker, read with `POKE_TOOL_GetPartyValue(mon, 0x9E, 0)` | the attacker's level |
| `turn` | u32 | the context `turn`, from `BTL_SERVER_FLOW_GetTurnCount` (`func_021C8138.c:27`) | the turn the damage was dealt on |
| `weather` | u32 | the context `weather`, from `BTL_MAIN_GetFieldSituation` (`ov093_func_021B8714.c:15`) | a rule scales or nulls the damage by weather |

The defender and its remaining HP are not arguments of this function
(still-to-build item 6 below).

## 9. Items

The six battle item and bag events are specified here, with the fields both the
battle area and the inventory area (section 14) carry.

### `battle.item.use` (0x021CB730, ov093)

Function `BTL_SERVER_FLOW_ProcBagItemUse(u8 *p, u8 *a, u16 move, u8 arg,
u8 slot)` (`ov093_func_021CB730.c:81`). It walks the item effect table and
updates the bag.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:81`) | the battle flow struct |
| `mon` | BwPokeRef | parameter `a` (`:81`); `BTL_POKEPARAM_GetPokeId(a)` (`:100`) | the Pokemon that uses the item |
| `item` | u16 | parameter `move` (`:81`, `:118`) | the item id; it is also the ball item in the throw path (`:121`) |
| `arg` | u8 | parameter `arg` (`:81`, `:177`) | the extra argument the effect callbacks take |
| `slot` | u8 | parameter `slot` (`:81`, `:103`) | the target slot; 6 means a party item |
| `side` | u8 | `BTL_MAIN_PokeIdToClientIdThunk(poke_id)` (`:100`) | the client the user belongs to |
| `target` | BwPokeRef | `BTL_SLOT_GetPoke(party, slot)` (`:107`) | the target when the slot is a party slot |
| `target_id` | u32 | `BTL_POKEPARAM_GetPokeId(mon)` (`:108`) | the target's identity |
| `condition` | u8 | computed from `slot`, `n` and the rule (`:102` to `:114`) | whether the target is valid |
| `item_state` | u32 | `func_021CBA30(p + 0x1ab8, monId)` (`:109`) | the target's semi-invulnerable or substitute state; 6 means disabled |
| `rule` | u32 | `BTL_MAIN_GetRule(p + 4)` (`:113`) | the battle rule |
| `is_ball` | bool | `BTL_CALC_GetMonParamWithHeap(move, 15) == 4` (`:119`) | that the item is a ball |
| `ball_caught` | bool | `BTL_SERVER_FLOW_TryThrowBall(p,a,move)` (`:121`) | the ball outcome |
| `effect_entry` | u32 | `gBagItemEffectTable[i]` (`:198`) | the item's effect row |
| `effect_id` | u16 | `e->id` (`:199`) | the effect's selector |
| `effect_mode` | u8 | `e->mode` (`:146`) | how the effect is gated |
| `effect_exact` | bool | `e->exact` (`:144`) | exact id match or param test |
| `effect_applied` | bool | the effect callback return `e->call(p, mon, move, result, arg)` (`:200`) | whether the effect changed the mon |
| `item_returned` | bool | `BTL_MAIN_AddItemToBag(bag, side, move)` (`:209`) | that the item was not consumed |
| `friendship_updated` | bool | `BTL_MAIN_UpdateFriendshipUseItem(bag, mon, move)` (`:210`) | that the item moved friendship |
| `message_id` | u32 | the pushed id, 0x21, 0x24, 0xc4 or 0x44 (`:118`, `:133`, `:162`, `:186`) | which message the client shows |
| `effect_table` | u32 | `gBagItemEffectTable` (`:40`) | the whole table for a handler |

Nuzlocke forbids items in battle (`progress/EVENT_SURVEY.md`, "Battle item
ban").

### `battle.item.heal` (0x021CC2C0, ov093)

Function `BTL_SERVER_FLOW_ItemEffectRecoverHp(void *a, void *m, void *s)`
(`ov093_func_021CC2C0.c:42`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `a` (`:42`) | the battle flow struct |
| `mon` | BwPokeRef | parameter `m` (`:42`); `BTL_POKEPARAM_GetPokeId(m)` (`:49`) | the Pokemon being healed |
| `item` | u32 | parameter `s` (`:42`) | the item; `BTL_CALC_GetMonParamWithHeap(s, 0x3a)` (`:53`) |
| `heal_kind` | u32 | `k` (`:53`) | 0xFF full, 0xFE half, 0xFD quarter, else a flat amount (`:54`) |
| `heal_amount` | u32 | `r->f4` (`:56` to `:65`) | the HP the item restores |
| `hp_full` | bool | `BTL_POKEPARAM_IsHpFull(m)` (`:48`) | whether the heal is refused |
| `fainted` | bool | `BTL_POKEPARAM_IsFainted(m)` (`:48`) | whether the heal is refused |
| `poke_id` | u8 | `BTL_POKEPARAM_GetPokeId(m)` (`:49`) | the healed mon's identity |
| `applied` | bool | the return, 1 or 0 (`:71`) | whether the heal happened |
| `message_id` | u32 | 387 (`:68`) | the message shown |

### `battle.item.cure` (0x021C557C, ov093)

Function `BTL_SERVER_FLOW_CureMajorStatusWithMessage(u8 *p, u8 *a, int k,
int flag)` (`ov093_func_021C557C.c:28`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:28`) | the battle flow struct |
| `mon` | BwPokeRef | parameter `a` (`:28`); `BTL_POKEPARAM_GetPokeId(a)` (`:30`) | the Pokemon being cured |
| `poke_id` | u8 | `BTL_POKEPARAM_GetPokeId(a)` (`:30`) | the cured mon's identity |
| `status_kind` | int | parameter `k` (`:28`, `:35`) | the status kind the message switches on |
| `show_message` | int | parameter `flag` (`:28`, `:34`) | whether the message is printed |
| `cleared` | bool | `BTL_POKEPARAM_ClearMajorStatus(a)` (`:31`) | that the status was cleared |
| `message_id` | u32 | 0x138, 0x126 or 0x156 (`:37` to `:43`) | the message shown |

### `battle.item.check` (0x021CBFA0, ov093)

Function `BTL_SERVER_FLOW_ItemEffectCheckSlotMessage(u8 *p, u8 *q)`
(`ov093_func_021CBFA0.c:36`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:36`) | the battle flow struct |
| `mon` | BwPokeRef | parameter `q` (`:36`); `BTL_POKEPARAM_GetPokeId(q)` (`:41`) | the Pokemon the slot message is for |
| `poke_id` | u8 | `BTL_POKEPARAM_GetPokeId(q)` (`:41`) | the mon's identity |
| `second_pair` | int | `BTL_MAIN_IsPokeIdInSecondPair(poke_id)` (`:41`) | whether the Pokemon is in the second pair |
| `message_id` | u32 | 0x88 (`:45`) | the message shown |
| `matched` | bool | the return of `func_021CBFF8` (`:43`) | whether the message fires |

### `battle.bag.add` (0x021B9090, ov093)

Function `BTL_MAIN_AddItemToBag(u8 *p, u32 k, u32 x)`
(`ov093_func_021B9090.c:20`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `main` | u32 | parameter `p` (`:20`) | the battle main struct |
| `side` | u32 | parameter `k` (`:20`); compared against `p[0x46c]` | the side the item returns to |
| `item` | u32 | parameter `x` (`:20`) | the item id added |
| `bag` | u32 | `p->f0` (`:20`) | the bag owner |
| `count` | u32 | the literal 1 in `func_02007F1C(..., x, 1, p->f468)` (`:20`) | the count added |
| `bag_field` | u16 | `p->f468` (`:20`) | the bag field the item is added to |
| `local_side` | u8 | the `q[0x23] == 0` gate (`:20`) | whether the add is for the local player |

### `battle.bag.sub` (0x021B90BC, ov093)

Function `BTL_MAIN_SubItemFromBag(u8 *p, u8 k, u32 x)`
(`ov093_func_021B90BC.c:20`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `main` | u32 | parameter `p` (`:20`) | the battle main struct |
| `side` | u8 | parameter `k` (`:20`); compared against `p[0x46c]` | the side the item leaves |
| `item` | u32 | parameter `x` (`:20`) | the item id taken |
| `bag` | u32 | `p->f0` (`:20`) | the bag owner |
| `count` | u32 | the literal 1 in `func_02007E50(..., x, 1, p->f468)` (`:20`) | the count taken |
| `bag_field` | u16 | `p->f468` (`:20`) | the bag field |
| `local_side` | u8 | the `q[0x23] == 0` gate (`:20`) | whether the take is for the local player |

## 10. Money and badges

### `battle.badge.count` (0x021B85A0, ov093, state)

Function `BTL_MAIN_GetBadgeCount(u8 **p)` returns `(*p)->f80`
(`func_021B85A0.c:27`). No slot fires. Value: `{ battle_main, badge_count }`.
The eight badges are bits 0 to 7 of the flag word at misc block `+0x04`
(`docs/mod-events.md`, section 4). A mod gates a shop or a battle on the
count (`progress/EVENT_SURVEY.md`, "Shop badge gating").

### `battle.money.loss` (0x021D7FF8, ov093, state)

Function `BTL_CALC_CalcDefeatMoneyLoss(u32 a, void *b)`
(`ov093_func_021D7FF8.c:33`). It computes the money lost on defeat from the
badge count and the party's levels.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `badge_count` | u32 | parameter `a` (`:33`); clamped to 8 at `:45` | the badge count that scales the loss |
| `side_list` | u32 | parameter `b` (`:33`) | the side's party list |
| `count` | u32 | `BTL_SLOT_GetCount(b)` (`:35`) | the number of party entries |
| `max_level` | u32 | the maximum of `func_021D59B4(..., 0xf)` (`:41`) | the highest party level; scales the loss |
| `loss` | u32 | the return `gBtlCalcDefeatMoneyScales[a] * (m << 2)` (`:47`) | the money the game will take |

`BTL_MAIN_CalcPenaltyMoney` wraps it: it reads the player money with
`BEACON_GetPoints` and clamps the loss to the money (`ov093_func_021B9244.c:40`
to `:45`). The whiteout message then prints it
(`ov093_func_021D1960.c:53`). A Nuzlocke rule that changes the penalty reads
the loss here or wraps the calc.

## 11. Whiteout

### `whiteout.begin` (0x021D1960 and 0x02158A20)

One concept over two addresses: the battle-screen half and the field-recover
half (`docs/mod-events.md`, section 1; `docs/mod-interface.md`, section 4).
The payload names which half fired.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `half` | u32 | the address the slot fired on | 0 for the battle half, 1 for the field half |
| `seq` | u32 | `BTL_CLIENT_StepShowBlackoutMessages(BattleSeq *s, int *state)`, parameter `s` (`ov093_func_021D1960.c:40`) | the blackout sequence on the battle half |
| `step` | int | `*state` (`ov093_func_021D1960.c:44`) | the message step |
| `battle` | u32 | `s->battle` (`ov093_func_021D1960.c:53`) | the battle the penalty is computed from |
| `penalty_money` | u32 | `BTL_MAIN_CalcPenaltyMoney(s->battle)` (`ov093_func_021D1960.c:53`) | the money the blackout takes |
| `script_work` | u32 | `SCRIPT_SYS_RunRecoverScript(int a, int v)`, parameter `a` (`ov010_func_02158A20.c:15`) | the script work on the field half |
| `script_id` | u16 | the literal 9601 in `SCRIPT_SYS_RunScriptNow(a, 0, 9601, 2)` (`ov010_func_02158A20.c:15`) | the recover script the field half runs |

A block here suppresses the return to the last heal point, which Nuzlocke
uses to continue after a whiteout (`docs/mod-interface.md`, section 4). The
port must fix whether one name binds both addresses or two events exist
(`docs/mod-events.md`, still-to-build item 1).

### `whiteout.fieldrecover` (0x02158AD4, ov010)

Function `SCRIPT_SYS_RunFieldRecoverScript(int a, int b)`
(`ov010_func_02158AD4.c:17`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `script_work` | u32 | parameter `a` (`:17`) | the script work passed to `SCRIPT_SYS_RunSpecialScript` (`:19`) |
| `b` | int | parameter `b` (`:17`) | the second argument |
| `key` | int | the literal 3 (`:19`) | the special script key |
| `d` | int | the literal 1 (`:19`) | the fourth argument |

## 12. The battle effect VM family

The battle effect VM dispatches through the 78-entry table at 0x02209E88; its
control handlers are named and the rest are unnamed `func_021F9*`
(`docs/mod-events.md`, section 1). The six rows here are override points, not
events, so a mod sees the function's own arguments and return rather than a
`BwEvent`. The port fixes the override signature as the function's own
(`docs/mod-interface.md`, section 3).

### `battle.effectvm.create` (0x021F93F4, ov094, override)

Function `BTLV_EFFVM_Create(void *a, u32 b)` (`ov094_func_021F93F4.c:47`).

| Argument | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `owner` | u32 | parameter `a` (`:47`) | the owner the VM is bound to (`:55`) |
| `heap` | u32 | parameter `b` (`:47`) | the heap id (`:53`) |
| return | u32 | the VM from `VM_Create` (`:64`) | the created VM instance |

### `battle.effectvm.jumpif` (0x021FBD44, ov094, override)

Function `BTLV_EFFVM_CmdJumpIf(struct Cursor *a, State *s)`
(`ov094_func_021FBD44.c:28`).

| Argument | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `cursor` | u32 | parameter `a` (`:28`) | the script cursor |
| `state` | u32 | parameter `s` (`:28`) | the VM state; the program counter is `s->f23c` (`:36`) |
| `condition` | int | `BTLV_EFFVM_EvalCondition(s, VM_GetU32(a))` (`:30`) | the evaluated condition |
| `op` | int | `VM_GetU32(a)` (`:31`) | the comparison operator |
| `y` | int | `VM_GetU32(a)` (`:32`) | the first comparand |
| `z` | int | `VM_GetU32(a)` (`:33`) | the jump offset |
| return | int | `s->f23c` (`:36`) | the program counter after the branch |

### `battle.effectvm.jumpifnot` (0x021FBD88, ov094, override)

Function `BTLV_EFFVM_CmdJumpIfNot(struct Cursor *a, State *s)`
(`ov094_func_021FBD88.c:28`). The same shape; both `x` and `y` are evaluated
conditions (`:30`, `:32`).

### `battle.effectvm.jumpscript` (0x021FC334, ov094, override)

Function `BTLV_EFFVM_CmdJumpScript(struct Cursor *a, char *s)`
(`ov094_func_021FC334.c:26`).

| Argument | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `cursor` | u32 | parameter `a` (`:26`) | the script cursor |
| `state` | u32 | parameter `s` (`:26`) | the VM state; the program counter is `s->f23C` (`:30`) |
| `offset` | int | `VM_GetU32(a)` (`:28`) | the relative jump offset |
| `base` | int | `a->f14` (`:29`) | the base the offset is added to |
| return | int | `s->f23C` (`:30`) | the program counter after the jump |

### `battle.effectvm.jumptoarchive` (0x021FC374, ov094, override)

Function `BTLV_EFFVM_CmdJumpToArchiveScript(void *a, char *s)`
(`ov094_func_021FC374.c:32`).

| Argument | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `cursor` | u32 | parameter `a` (`:32`) | the script cursor |
| `state` | u32 | parameter `s` (`:32`) | the VM state; the loaded script is at `s->f1E8` (`:37`) |
| `script_id` | int | `VM_GetU32(a)` (`:34`) | the archive script id |
| `archive` | int | 0x42 or 0x43 (`:39`, `:41`) | the archive the member is loaded from |
| `member` | int | `id` or `id - 0x231` (`:39`, `:41`) | the member index |
| return | int | `s->f23C` (`:45`) | the program counter after the jump |

### `battle.effectvm.comparejump` (0x021FBDD4, ov094, override)

Function `BTLV_EFFVM_CompareAndJump(int a, int b, int c, int op, int e)`
(`ov094_func_021FBDD4.c:20`).

| Argument | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `cursor` | int | parameter `a` (`:20`) | the script cursor; the base is `a->f14` (`:51`) |
| `b` | int | parameter `b` (`:20`) | the first comparand |
| `c` | int | parameter `c` (`:20`) | the second comparand |
| `op` | int | parameter `op` (`:20`) | 0 equal to 5 greater or equal (`:24`) |
| `e` | int | parameter `e` (`:20`) | the relative jump offset |
| `taken` | bool | the local `t` (`:22`, `:50`) | whether the branch is taken |

## 13. Field and interaction

The field and interaction area is the 67 entries of `docs/mod-events.md`,
section 1, that run on the field (50 events, 14 state entries and 3 override
points): the map change and route, the talk and the
script search, the field event checks, the messages and windows, the warps, the
ledges and hops, the field skills and field moves, the field bag and overlay
procedure, and the season, random, save record and special location commands.
The port tracks the field context (section 0a) for the position, the zone, the
last warp and the last talk. A field VM command reads the script work for its
cause; a field check reads the tracked input. Each table gives the field, its
type, where it comes from, and why a mod wants it.

### Map and route

#### `route.change`

`FIELD_MAP_CHANGE_SetupZone` (0x021643E8, ov010), `void FIELD_MAP_CHANGE_SetupZone(void *a, void *b)`
(`ov010_func_021643E8.c:55`). `a` is the game data, `b` is the new zone id.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `zone_id` | u32 | Argument `b`, `ov010_func_021643E8.c:55`, used as `(u32)b` at `:59` | Names the zone the field loads, so a mod can gate on the route |
| `prev_zone_id` | u32 | Context, the `zone_id` before the call, section 0a | Tells a mod where the player came from |
| `field_skill_effect` | u32 | `ZONEDATA_GetFieldSkillMapEffect((u32)b)`, `ov010_func_021643E8.c:59` | Names the field skill the map needs, such as Flash |
| `flash_active` | bool | `FIELD_STATUS_SetFlashActive(w, 0)`, `ov010_func_021643E8.c:60` | Reports that the zone cleared Flash |
| `save_block` | handle | `MOVE_POKEMON_SAVE_GetBlock(GAME_DATA_GetSaveControl(a))`, `ov010_func_021643E8.c:62` | The field-skill save block the zone resets |
| `event_work` | handle | `GAME_DATA_GetEventWork(a)`, `ov010_func_021643E8.c:64` | The event work the zone resets at flag 0x964 |
| `hidden_items_respawned` | bool | `EVENTDATA_SYSTEM_RespawnHiddenItems(c)`, `ov010_func_021643E8.c:66` | Reports that hidden items respawned for the zone |
| `zone_in_table` | bool | `ZONEDATA_IsZoneInTableDFB8((u32)b)`, `ov010_func_021643E8.c:67` | Distinguishes a table zone, which a mod may treat specially |
| `owner` | handle | The game data `a`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the change to the local player |
| `cause` | enum | The context `last_warp` or the scripted map change, section 0a | Tells a mod what led to the change |

#### `route.header`

`FIELD_MAP_CHANGE_ApplyZoneHeader` (0x021643AC, ov010), `void FIELD_MAP_CHANGE_ApplyZoneHeader(void *a, void *b)`
(`ov010_func_021643AC.c:38`). An override. `a` is the game data, `b` is the
field map.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `zone_id` | u16 | `FIELDMAP_GetZoneId(b)`, `ov010_func_021643AC.c:38` | The zone whose header applies |
| `prev_zone_id` | u16 | Context, section 0a | The zone left behind |
| `save_block` | handle | `MOVE_POKEMON_SAVE_GetBlock(GAME_DATA_GetSaveControl(a))`, `ov010_func_021643AC.c:38` | The save block the header seeds |
| `event_work` | handle | `GAME_DATA_GetEventWork(a)`, `ov010_func_021643AC.c:38` | The work reset at flag 0x964 |
| `effect_encount` | u32 | `FIELD_ENCOUNT_ST_GetEffectEncount((int)b)`, `ov010_func_021643AC.c:38` | The zone's effect-encounter state, useful to an encounter mod |
| `owner` | handle | The game data `a`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the header to the local player |

#### `field.map.event`

`EVENT_MAPCHANGE_ChangeFieldEvent` (0x021841C0, ov020), `int EVENT_MAPCHANGE_ChangeFieldEvent(int ctx, int *state, u8 **pp)`
(`ov020_func_021841C0.c:110`). An override and a step machine. `ctx` is the
event system, `state` is the step index, `pp` points at the map-change work
`p` (`Obj01649`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `step` | u32 | `*state`, `ov020_func_021841C0.c:127` | Names which step of the map change runs |
| `zone_id` | u32 | `p->f14`, `ov020_func_021841C0.c:135` | The zone the change applies |
| `system` | handle | `p->f0`, `ov020_func_021841C0.c:123` | The game system the change runs under |
| `game_data` | handle | `p->f4`, `ov020_func_021841C0.c:124` | The save-side game data |
| `field_map` | handle | `p->f8`, `ov020_func_021841C0.c:122` | The field map the change edits |
| `change_case` | u32 | `p->f30`, `ov020_func_021841C0.c:162`, `:166` | Selects the gimmick reset, 1, 2 or 3, or none |
| `no_game_event_zone` | u32 | `ZONEDATA_IsNoGameEventZone(p->f14)`, `ov020_func_021841C0.c:135` | Tells a mod the zone runs no game event |
| `comm_kind` | u32 | `GAME_COMM_GetKind(w)`, `ov020_func_021841C0.c:135`, `:143` | The comm state, which the change may exit |
| `season` | u8 | `GAME_DATA_GetSeason(y)`, `ov020_func_021841C0.c:153` | The season the forms update to |
| `season_disp` | u32 | `p->f4A` to `COMM_PLAYER_SUPPORT_SetSeason`, `ov020_func_021841C0.c:151` | The season display flag the change pushes |
| `party` | handle | `GAME_DATA_GetMyParty(y)`, `ov020_func_021841C0.c:154` | The party whose seasonal forms update |
| `save_control` | handle | `GAME_DATA_GetSaveControl(y)`, `ov020_func_021841C0.c:152` | The save control the season reads |
| `location` | ptr | `p + 0x10`, `ov020_func_021841C0.c:161` | The warp or location record the change applies |
| `owner` | handle | The game data `y`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the change to the local player |

#### `field.map.fixedzone`

`EVENT_MAPCHANGE_RunFixedZoneEvent` (0x02184C78, ov020), `int EVENT_MAPCHANGE_RunFixedZoneEvent(void *ctx, int *st, int *p)`
(`ov020_func_02184C78.c:37`). `ctx` is the event system, `st` is the step, `p`
is the change record.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `step` | u32 | `*st`, `ov020_func_02184C78.c:42` | Which step of the fixed-zone event runs |
| `anim_source` | handle | `p[0]`, `ov020_func_02184C78.c:39`, used at `:44` | The source of the proclink animation |
| `anim_target` | handle | `p[2]`, `ov020_func_02184C78.c:40`, used at `:44` | The target of the animation |
| `change_kind` | u32 | The literal `4` passed to `EVENT_MAPCHANGE_CreateChangeFieldEvent`, `ov020_func_02184C78.c:50` | Marks the change as a fixed-zone change |
| `change_record` | ptr | `p`, `ov020_func_02184C78.c:50` | The record the field change reads |

#### `field.map.arrival`

`EVENT_MAPCHANGE_RunSituationWarpArrivalEvent` (0x02185694, ov020), `int EVENT_MAPCHANGE_RunSituationWarpArrivalEvent(int a0, int *st, int *p)`
(`ov020_func_02185694.c:55`). `a0` is the event system, `st` is the step, `p`
is the arrival record.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `step` | u32 | `st[0]`, `ov020_func_02185694.c:59` | Which step of the arrival runs |
| `system` | handle | `p[0]`, `ov020_func_02185694.c:61` | The system the field initialises from |
| `field_map` | handle | `p[1]`, `ov020_func_02185694.c:67` | The field map the BGM and season read |
| `zone_id` | u16 | `((short *)p)[6]`, `ov020_func_02185694.c:67` | The zone the arrival plays music for |
| `season` | u8 | `GAME_DATA_GetSeason((BigObj *)p[1])`, `ov020_func_02185694.c:67` | The season the arrival BGM depends on |
| `bgm` | u32 | `FIELD_SOUND_SYSTEM_GetFieldBgmForZoneEx(p[1], zone_id, season)`, `ov020_func_02185694.c:67` | The field BGM the arrival starts |
| `player_work` | handle | `GAME_DATA_GetMyPlayerWork(GAMESYSTEM_GetGameData(p[0]))`, `ov020_func_02185694.c:65` | The player record whose object code resets |
| `object_code` | u32 | The literal `0` set at `ov020_func_02185694.c:65` | Reports the player object code the arrival resets to |
| `source_zone` | u16 | Context `last_warp`, section 0a | Tells a mod which zone the player warped from |

### Talking and the script search

#### `field.talk.npc`

`FIELD_WFBC_DATA_LOCAL_CreatePersonTalkEvent` (0x021870C4, ov020), `void *FIELD_WFBC_DATA_LOCAL_CreatePersonTalkEvent(void *a0, void *a1)`
(`ov020_func_021870C4.c:77`). `a0` is the game system, `a1` is the field map.
Returns 0 when no comm work runs.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `task` | handle | `func_020122C0(a0, 0, main, 0x58)` return, `ov020_func_021870C4.c:100` | The talk task the game created |
| `person_index` | u16 | `FIELD_WFBC_DATA_LOCAL_GetPersonIndexFromMmdl(d)`, `ov020_func_021870C4.c:121`, stored at `:126` | Names the person in the zone's people table |
| `person` | handle | `FIELD_WFBC_GetPeople(a, e)`, `ov020_func_021870C4.c:122` | The person record the talk is about |
| `mmdl` | handle | `FLDMMDL_SysSearchGridPos(c, v[2], v[0], 0)`, `ov020_func_021870C4.c:119` | The person's field model |
| `wfbc` | handle | `FIELD_G3D_MAPPER_GetWfbc(FIELDMAP_GetG3dMapper(a1))`, `ov020_func_021870C4.c:114` | The map's person table the talk reads |
| `pos` | `VecFx32` | `FLDMMDL_GetPos(d, o + 0x34)`, `ov020_func_021870C4.c:120` | The person's field position |
| `front_grid` | s16 x, s16 z, s16 y | `FIELD_PLAYER_GetFrontGridPos(b, &v[2], &v[1], &v[0])`, `ov020_func_021870C4.c:118` | The cell in front of the player the talk targets |
| `heap_id` | u16 | `FIELDMAP_GetHeapId(a1)`, `ov020_func_021870C4.c:93` | The heap the talk allocates in |
| `msg_bg` | u32 | `FIELDMAP_GetMsgBg(a1)`, `ov020_func_021870C4.c:94` | The message background the talk uses |
| `msg_bank` | handle | `func_02045B38(0, 3, 0x142, n)`, `ov020_func_021870C4.c:106` | The message bank the talk prints from |
| `return_slots` | handle | `INTRUDE_COMM_GetReturnSlots(w)`, `ov020_func_021870C4.c:99` | The comm return slots the talk fills |
| `event_data_a` | handle | `func_020127D0(h, 1)`, `ov020_func_021870C4.c:112` | The person's event data |
| `event_data_b` | handle | `func_020127D0(h, 0)`, `ov020_func_021870C4.c:113` | The zone's event data |
| `mystery_recv` | u32 | `GAME_DATA_GetMysteryRecvFlagB(GAMESYSTEM_GetGameData(a0))`, `ov020_func_021870C4.c:117` | The mystery-gift receive flag |
| `owner` | handle | The game data `h`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the talk to the local player |

#### `field.talk.npc.cmd`

`FIELD_WFBC_DATA_LOCAL_ScrCmdCallPersonTalkEvent` (0x02187098, ov020), `int FIELD_WFBC_DATA_LOCAL_ScrCmdCallPersonTalkEvent(void *unused, void *p)`
(`ov020_func_02187098.c:24`). `p` is the script command context.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `script_work` | handle | `SCRCMD_WORK_GetScriptWork(p)`, `ov020_func_02187098.c:28` | The VM work the talk event is called on |
| `game_system` | handle | `SCRCMD_WORK_GetGameSystem(p)`, `ov020_func_02187098.c:29` | The system the talk builds from |
| `field_map` | handle | `GAMESYSTEM_GetFieldMap(obj)`, `ov020_func_02187098.c:30` | The field map the talk reads |
| `task` | handle | `FIELD_WFBC_DATA_LOCAL_CreatePersonTalkEvent(obj, field_map)`, `ov020_func_02187098.c:30` | The talk task the command created |
| `cause` | enum | The script command itself, opcode of this handler | Tells a mod a script started the talk |

#### `field.talk.npc.main`

`FIELD_WFBC_DATA_LOCAL_PersonTalkEventMain` (0x021871F8, ov020), `int FIELD_WFBC_DATA_LOCAL_PersonTalkEventMain(void *a, int *st, char *o)`
(`ov020_func_021871F8.c:90`). `a` is the task, `st` is the step, `o` is the
0x58-byte work block.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `step` | u32 | `*st`, `ov020_func_021871F8.c:95` | Which step of the talk runs |
| `person_index` | u16 | `H4C` = `*(u16 *)(o + 0x4c)`, `ov020_func_021871F8.c:98` | Names the person being talked to |
| `event_data` | handle | `P44` = `*(void **)(o + 0x44)`, `ov020_func_021871F8.c:122` | The person's event data |
| `event_data_zone` | handle | `P48` = `*(void **)(o + 0x48)`, `ov020_func_021871F8.c:170` | The zone's event data |
| `wfbc` | handle | `P28` = `*(void **)(o + 0x28)`, `ov020_func_021871F8.c:171` | The person table the talk removes from |
| `mmdl` | handle | `P30` = `*(void **)(o + 0x30)`, `ov020_func_021871F8.c:172` | The person's field model |
| `return_slots` | handle | `P40` = `*(void **)(o + 0x40)`, `ov020_func_021871F8.c:97` | The comm return slots the talk requests |
| `return_slot_type` | u16 | `EVENT_FIELD_PROCLINK_GetReturnDefaultSlotType(P40)`, `ov020_func_021871F8.c:104`, `:148` | The kind of return the comm peer sent |
| `has_return_slot` | bool | `EVENT_FIELD_PROCLINK_HasReturnDefaultSlot(P40, H4C)`, `ov020_func_021871F8.c:102` | Whether the peer answered |
| `people_count` | u32 | `FIELD_WFBC_DATA_LOCAL_CountPeople(P44, 0)`, `ov020_func_021871F8.c:122` | The number of people in the event data |
| `has_entry` | bool | `EVENT_FIELD_PROCLINK_HasEventdataEntry(P44, H4C, 2)`, `ov020_func_021871F8.c:126` | Whether the person has a return entry |
| `packed_id` | u16 | `H08` = `*(u16 *)(o + 0x08)`, `ov020_func_021871F8.c:170` | The packed heap and window id |
| `status` | handle | `GAME_DATA_GetMyStatus(GAMESYSTEM_GetGameData(P(o)))`, `ov020_func_021871F8.c:170` | The local player's status the talk applies |
| `talk_type` | u8 | `B54` = `*(u8 *)(o + 0x54)`, `ov020_func_021871F8.c:184` | The talk kind, which selects the next step |
| `active` | u8 | `B56` = `*(u8 *)(o + 0x56)`, `ov020_func_021871F8.c:92` | Whether the talk is still active |
| `owner` | handle | The game data `P(o)`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the talk to the local player |

#### `field.talk.comm`

`EVENT_COMM_COMMON_CreateTalkEvent` (0x0217FC08, ov018), `void *EVENT_COMM_COMMON_CreateTalkEvent(int a, int b, u8 *c, int d, int e, u16 f)`
(`ov018_func_0217FC08.c:37`). `a` is the game system, `b` and `d`, `e`, `f` are
comm parameters, `c` is the comm work.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `task` | handle | `func_020122C0(a, 0, main, 0x9c)` return, `ov018_func_0217FC08.c:39` | The comm talk task |
| `comm_work` | handle | Argument `c`, `ov018_func_0217FC08.c:37` | The comm work the talk reads |
| `talk_type` | u8 | `EVENT_COMM_COMMON_GetTalkType(q, c)`, `ov018_func_0217FC08.c:46` | The kind of comm talk, stored at work +0x41 |
| `owner` | handle | Argument `a`; the peer slot the comm work names | Attributes the talk to the local player or the peer |

#### `field.talk.comm.main`

`EVENT_COMM_COMMON_TalkEventMain` (0x0217FE6C, ov018), `int EVENT_COMM_COMMON_TalkEventMain(void *a, int *state, Outer *c)`
(`ov018_func_0217FE6C.c:115`). `a` is the task, `state` is the step, `c` is the
0x98-byte work block.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `step` | u32 | `*state`, `ov018_func_0217FE6C.c:134` | Which step of the comm talk runs |
| `peer_slot` | u8 | `c->f40`, `ov018_func_0217FE6C.c:137` | Names the peer the talk is with, the owner in a multiplayer game |
| `talk_type` | u8 | `c->f41`, `ov018_func_0217FE6C.c:204` | The talk kind |
| `request_kind` | u32 | `INTRUDE_COMM_GetRequestKind(v + 0x1AC)`, `ov018_func_0217FE6C.c:136` | The request the peer made |
| `session_result` | u32 | `INTRUDE_COMM_GetSessionResult(v + 0x1AC)`, `ov018_func_0217FE6C.c:141` | The result of the session |
| `session_active` | bool | `INTRUDE_COMM_IsSessionActive(v + 0x1AC)`, `ov018_func_0217FE6C.c:141` | Whether a session is active |
| `session_accepted` | bool | `INTRUDE_COMM_IsSessionAccepted(v + 0x1AC)`, `ov018_func_0217FE6C.c:142` | Whether the peer accepted |
| `from_net_id` | bool | `INTRUDE_COMM_IsRequestFromNetId(v + 0x1AC, c->f40)`, `ov018_func_0217FE6C.c:142` | Whether the request came from the named peer |
| `entry_flag15` | u1 | `b->f15` at `v + 0xA2 + c->f40 * 0x1C`, `ov018_func_0217FE6C.c:138` | A per-peer entry flag |
| `entry_flag9` | u6 | `b->f9` at `v + 0xA2 + c->f40 * 0x1C`, `ov018_func_0217FE6C.c:148` | A per-peer entry flag |
| `notice_state` | u8 | `c->f99`, `ov018_func_0217FE6C.c:173` | The notice state the talk set |
| `answer_state` | u8 | `c->f98`, `ov018_func_0217FE6C.c:182` | The answer state the talk set |
| `owner` | handle | The peer at `c->f40`; `INTRUDE_COMM_GetWorkIfRunning`, `ov018_func_0217FE6C.c:123` | Attributes the talk to the peer |

#### `field.talk.search`

`EVENTDATA_SYSTEM_SearchBgEventScriptIdTalk` (0x02162A54, ov010), `u16 EVENTDATA_SYSTEM_SearchBgEventScriptIdTalk(void *set, void *flags, const void *where, u16 type)`
(`ov010_func_02162A54.c:25`). It forwards all four arguments to
`EVENTDATA_SYSTEM_SearchBgEventScriptId` (`ov010_func_02162970.c:56`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `script_id` | u16 | The return, `ov010_func_02162970.c:81`; 0xFFFF when none, `:108` | Names the talk script that fires, the subject of the search |
| `trigger_set` | handle | Argument `set`, `ov010_func_02162A54.c:25`; the caller passes `in.fC`, `ov021_func_02189BD0.c:323` | The zone's background-event table searched |
| `trigger_count` | u16 | `set->count`, `ov010_func_02162970.c:64` | The number of triggers searched |
| `trigger_list` | ptr | `set->list`, `ov010_func_02162970.c:62` | The trigger entries |
| `event_work` | handle | Argument `flags`; the caller passes `GAME_DATA_GetEventWork(in.f8)`, `ov021_func_02189BD0.c:323` | The flags the hidden-item test reads |
| `where` | `VecFx32` | Argument `where`; the caller passes `xyz`, `ov021_func_02189BD0.c:323` | The position the trigger tests against |
| `type` | u16 | Argument `type`; the caller passes the facing `dir`, `ov021_func_02189BD0.c:323` | The facing the trigger kind filters against |
| `hit_kind` | u16 | `t->kind`, `ov010_func_02162970.c:78` | The trigger kind that matched |
| `hit_mode` | u16 | `t->mode`, `ov010_func_02162970.c:66` | Whether the hit is a grid or rail test |
| `hidden` | bool | `t->kind2 == 2` branch, `ov010_func_02162970.c:73` | Whether the trigger is a hidden item |
| `owner` | handle | The local player; the caller's `in.f8` game data | Attributes the search to the local player |

### The field event checks

#### `field.event.grid`

`FIELD_EVENT_CHECK_CheckGridEvents` (0x02189BD0, ov021), `int FIELD_EVENT_CHECK_CheckGridEvents(void *sys, struct Obj00020 *pos, int *outA, int *outB)`
(`ov021_func_02189BD0.c:207`). `sys` is the game system, `pos` is the field map,
`outA` and `outB` are output flags. This is the root of the A-press tree that
runs the talk, step, menu, surf and script checks in order.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `out_a` | int | `*outA`, set at `ov021_func_02189BD0.c:224`, `:319` | Reports that a talk or script event fired |
| `out_b` | int | `*outB`, set at `ov021_func_02189BD0.c:372` | Reports a battle-record link event |
| `zone_id` | u16 | `in.f10` = `FIELDMAP_GetZoneId(pos)`, `ov021_func_0218A4E0.c:108`, read at `ov021_func_02189BD0.c:278` | The zone the check runs in |
| `heap_id` | u16 | `in.f0` = `FIELDMAP_GetHeapId(pos)`, `ov021_func_0218A4E0.c:104`, read at `ov021_func_02189BD0.c:320` | The heap the check allocates in |
| `event_work` | handle | `in.f8` = `ICA_ANIME_GetField1C(sys)`, `ov021_func_0218A4E0.c:106`, read at `ov021_func_02189BD0.c:269` | The event work the check reads and writes |
| `event_data` | handle | `in.fC` = `GAME_DATA_GetField158(in.f8)`, `ov021_func_0218A4E0.c:107`, read at `ov021_func_02189BD0.c:323` | The trigger table the talk search reads |
| `trg` | u32 | `in.trg` = `KEY_GetTrg()`, `ov021_func_0218A4E0.c:109`, read at `ov021_func_02189BD0.c:271` | The pad trigger the check acts on |
| `cont` | u32 | `in.cont` = `KEY_GetCont()`, `ov021_func_0218A4E0.c:110`, read at `ov021_func_02189BD0.c:271` | The held keys, the cause of a held-direction event |
| `dir` | u16 | `in.dir`, `ov021_func_0218A4E0.c:111`, read at `ov021_func_02189BD0.c:323` | The facing the talk search filters on |
| `player` | handle | `in.f18` = `FIELDMAP_GetFieldPlayer(pos)`, `ov021_func_0218A4E0.c:121` | The player object the check moves |
| `step_kind` | u32 | `in.f20` = `FIELD_PLAYER_GetStepKind`, `ov021_func_0218A4E0.c:123` | The player's step kind |
| `step_phase` | u32 | `in.f1C` = `FIELD_PLAYER_GetStepPhase`, `ov021_func_0218A4E0.c:124`, read at `ov021_func_02189BD0.c:345` | The player's step phase |
| `mmdl_dir` | u16 | `in.f24` = `FLDMMDL_GetDirDisp`, `ov021_func_0218A4E0.c:126` | The player's model direction |
| `pos_ptr` | ptr | `in.f34` = `FLDMMDL_GetPosPtr`, `ov021_func_0218A4E0.c:127`, read at `ov021_func_02189BD0.c:243` | The player's position record |
| `proc_action` | u16 | `in.f26` = `FIELD_STATUS_GetProcAction`, `ov021_func_0218A4E0.c:128` | The current field action |
| `map_attr` | u32 | `in.f38` = `FIELD_PLAYER_GetNowAttribute`, `ov021_func_0218A4E0.c:129` | The tile attribute under the player |
| `press_talk` | u1 | `in.b0`, `ov021_func_0218A4E0.c:135`, read at `ov021_func_02189BD0.c:300` | The A press that starts a talk |
| `press_battle` | u1 | `in.b1`, `ov021_func_0218A4E0.c:136`, read at `ov021_func_02189BD0.c:369` | The X press that starts a battle record |
| `press_menu` | u1 | `in.b6`, `ov021_func_0218A4E0.c:137`, read at `ov021_func_02189BD0.c:358` | The Y press that opens the shortcut menu |
| `stepped` | u1 | `in.b2`, `ov021_func_0218A4E0.c:148`, read at `ov021_func_02189BD0.c:235` | Whether the player took a step |
| `facing` | u1 | `in.b3`, `ov021_func_0218A4E0.c:151`, read at `ov021_func_02189BD0.c:253` | Whether the player faces a new way |
| `surf` | u1 | `in.b4`, `ov021_func_0218A4E0.c:154` | Whether a surf step applies |
| `front_tile` | u1 | `in.b5`, `ov021_func_0218A4E0.c:157`, read at `ov021_func_02189BD0.c:350` | Whether the player faces a front tile |
| `idle` | u1 | `in.b10`, `ov021_func_0218A4E0.c:134`, read at `ov021_func_02189BD0.c:367` | Whether the player is idle |
| `grid_move` | u1 | `in.b9`, `ov021_func_0218A4E0.c:150` | Whether a grid move is running |
| `front_target` | handle | `FIELD_EVENT_CHECK_SearchGridFrontMmdl(&in, pos)` return, `ov021_func_02189BD0.c:311` | The model in front of the player |
| `front_event_id` | u32 | `FLDMMDL_GetEventId(target)`, `ov021_func_02189BD0.c:313` | The event id of the model in front |
| `talk_script_id` | u16 | `EVENTDATA_SYSTEM_SearchBgEventScriptIdTalk(...)`, `ov021_func_02189BD0.c:323` | The talk script the search found |
| `tile_script_id` | u16 | `FIELD_EVENT_CHECK_GetFrontTileMessage(&in, pos)`, `ov021_func_02189BD0.c:326` | The sign or tile message the search found |
| `offer_slot` | u32 | `INTRUDE_FindOfferSlotAt(...)`, `ov021_func_02189BD0.c:333` | The intrude offer slot at the cell |
| `area_zone_id` | u16 | `FIELD_GetPlayerAreaZoneId(pos, &id)`, `ov021_func_02189BD0.c:336` | The player's area zone, which may force script 0x271B |
| `comm_kind` | u32 | `GAME_COMM_GetKind(obj)`, `ov021_func_02189BD0.c:280` | The comm state the check branches on |
| `intrude_count` | int | `GAME_DATA_GetIntrudeCount(in.f8)`, `ov021_func_02189BD0.c:281` | The intrude count the notice test reads |
| `remote_ahead` | int | `INTRUDE_FIELD_FindRemotePlayerAhead(...)` out slot, `ov021_func_02189BD0.c:294` | The remote player in front, the talk partner |
| `push_flags` | u32 | `ZONEDATA_CheckEventFlag(event_work, 0x964)`, `ov021_func_02189BD0.c:269` | The boulder-push flag the check passes |
| `owner` | handle | The game data `in.f8`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the check to the local player |

#### `field.event.rail`

`FIELD_EVENT_CHECK_CheckRailEvents` (0x0218A278, ov021), `int FIELD_EVENT_CHECK_CheckRailEvents(void *sys, struct Obj00020 *pos)`
(`ov021_func_0218A278.c:132`). The rail counterpart of the grid check. It
carries the same `in` fields as `field.event.grid` from
`FIELD_EVENT_CHECK_InitInput` (`ov021_func_0218A4E0.c:99`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `front_location` | `Cell` (u16 x, y, z, w) | `FLDMMDL_RAIL_GetFrontLocation(model, &here)`, `ov021_func_0218A278.c:144` | The rail cell in front of the player |
| `next_location` | int[2] | `FLDMMDL_RAIL_GetNextLocation(obj, cur)`, `ov021_func_0218A278.c:154` | The next rail cell on the path |
| `rail_script_id` | u16 | `EVENTDATA_SYSTEM_GetRailPosEventScriptId(in.fC, flags, cur)`, `ov021_func_0218A278.c:155` | The rail position script that fires |
| `hidden_script_id` | u16 | `EVENTDATA_SYSTEM_SearchBgEventScriptIdHidden(in.fC, flags, &copy, d)`, `ov021_func_0218A278.c:186` | The hidden trigger script that fires |
| `front_target` | handle | `FIELD_EVENT_CHECK_SearchRailFrontMmdl(&in, pos)` return, `ov021_func_0218A278.c:169` | The rail model in front |
| `front_event_id` | u32 | `FLDMMDL_GetEventId(target)`, `ov021_func_0218A278.c:174` | The event id of the rail model in front |
| `dir` | u16 | `FLDMMDL_GetDirDisp(model)`, `ov021_func_0218A278.c:184` | The facing the hidden search filters on |
| `zone_id` | u16 | `in.f10`, `ov021_func_0218A4E0.c:108` | The zone the check runs in |
| `event_work` | handle | `in.f8`, `ov021_func_0218A278.c:152` | The event work the check reads |
| `event_data` | handle | `in.fC`, `ov021_func_0218A278.c:155` | The trigger table the searches read |
| `press_talk` | u1 | `in.b0`, `ov021_func_0218A278.c:168` | The A press that starts a talk |
| `stepped` | u1 | `in.b2`, `ov021_func_0218A278.c:148` | Whether the player took a step |
| `owner` | handle | The game data `in.f8`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the check to the local player |

#### `field.event.run`

`FIELD_EVENT_CHECK_Run` (0x0218A4BC, ov021), `int FIELD_EVENT_CHECK_Run(u32 a, u32 b, u32 c, u32 d)`
(`func_0218A4BC.c:31`). `a` is the game system, `b` is the field map.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `move_kind` | u32 | `FIELDMAP_GetMoveKind((u8 *)b)`, `func_0218A4BC.c:33` | Selects the grid or rail check, the branch the run takes |
| `system` | handle | Argument `a`, `func_0218A4BC.c:31` | The system the chosen check runs under |
| `field_map` | handle | Argument `b`, `func_0218A4BC.c:31` | The field map the chosen check reads |

### Messages and windows

#### `field.msg.talkwin.open`

`SCRCMD_MSGWIN_OpenTalkWin` (0x021B0ADC, ov021), `void SCRCMD_MSGWIN_OpenTalkWin(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g)`
(`ov021_func_021B0ADC.c:26`). `a` is the script work, `b` is the message
background base, `c` is the window index, `d`, `e`, `f`, `g` are window
parameters.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `window` | handle | `FIELD_MSGBG_AddTalkWin((u8 *)b, c, d, e, f, g)`, `ov021_func_021B0ADC.c:28` | The talk window the script opened |
| `msg_window` | u32 | `SCRCMD_WORK_SetMsgWindow((u8 *)a, window)`, `ov021_func_021B0ADC.c:28` | The message window the script now uses |
| `window_index` | u32 | Argument `c`, `ov021_func_021B0ADC.c:26` | The slot index, 0 to 2, the window takes |
| `check_flag` | u32 | The literal `2` set by `SCRIPT_WORK_SetCheckFlag`, `ov021_func_021B0ADC.c:29` | The script check flag the open sets |
| `cause` | enum | The script command itself, opcode of this handler | Tells a mod a script opened the window |

#### `field.greeting.npc`

`SCRCMD_GetNpcGreetingMessage` (0x021D2B54, ov021), a state entry,
`int SCRCMD_GetNpcGreetingMessage(u32 a, u32 b)` (`ov021_func_021D2B54.c:84`).
`a` is the VM stream, `b` is the script command context. It writes a greeting
message id to the work pointer.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `message_id` | u16 | `SCRCMD_GREETING_AdjustMessageIdByRandom(h, SCRCMD_GREETING_AdjustMessageIdByMisc(h, base + k), v)`, `ov021_func_021D2B54.c:156` | The greeting the NPC says, the subject |
| `selector` | u32 | `SCRIPT_WORK_ReadWorkValue(a, b)`, `ov021_func_021D2B54.c:104` | The greeting variant the script asked for |
| `out_slot` | ptr | `SCRIPT_WORK_ReadWorkPointer(a, b)`, `ov021_func_021D2B54.c:105` | The work slot the message id lands in |
| `lead_species` | u32 | `POKE_TOOL_GetPartyValue(mon, 5, 0)` for the first non-egg mon, `ov021_func_021D2B54.c:112` | The species the greeting names |
| `party_count` | u32 | `POKEPARTY_GetCount(party)`, `ov021_func_021D2B54.c:108` | The party size the greeting may name |
| `hour` | u32 | `GF_STANDARD_SetField00(&t)`, `ov021_func_021D2B54.c:116` | The clock the greeting base depends on |
| `minute` | u32 | `t.minute`, `ov021_func_021D2B54.c:117` | The clock the greeting base depends on |
| `name` | handle | `GAME_DATA_GetMyStatus(h)`, `ov021_func_021D2B54.c:102` | The player status the greeting slots read |
| `trainer_record` | handle | `GAME_DATA_GetTrainerRecord(h)`, `ov021_func_021D2B54.c:122` | The trainer record the counters read |
| `owner` | handle | The game data `h`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the greeting to the local player |

#### `field.msg.plain`

`SCRCMD_MSGWIN_CmdPlainWinMsg` (0x021B10D0, ov021), `int SCRCMD_MSGWIN_CmdPlainWinMsg(void *a, int b)`
(`ov021_func_021B10D0.c:21`). It forwards to
`SCRCMD_MSGWIN_OpenPlainWinMsg(a, b, 0)` (`ov021_func_021B10D0.c:21`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `script_work` | handle | Argument `a`, `ov021_func_021B10D0.c:21` | The VM work the plain window opens on |
| `message_id` | int | Argument `b`, `ov021_func_021B10D0.c:21` | The message the window prints |
| `mode` | int | The literal `0` passed to `OpenPlainWinMsg`, `ov021_func_021B10D0.c:21` | Marks the plain window kind |

#### `field.msg.yesno`

`SCRCMD_MSGWIN_CmdYesNoWin` (0x021B0130, ov021), `int SCRCMD_MSGWIN_CmdYesNoWin(u32 a, u32 b)`
(`ov021_func_021B0130.c:32`). `a` is the VM stream, `b` is the script command
context.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `out_slot` | ptr | `SCRIPT_WORK_ReadWorkPointer(a, b)`, `ov021_func_021B0130.c:34` | The work slot the answer lands in |
| `script_work` | handle | `SCRCMD_WORK_GetScriptWork(b)`, `ov021_func_021B0130.c:35` | The VM work the yes or no event is called on |
| `event` | handle | `SCRCMD_MSGWIN_CreateYesNoEvent(SCRCMD_WORK_GetGameSystem(b), b, r)`, `ov021_func_021B0130.c:37` | The yes or no event the script waits on |
| `message_id` | u32 | Argument `b` passed to the create, `ov021_func_021B0130.c:37` | The prompt the window shows |

#### `field.msg.infosys`

`SCRCMD_MSGWIN_CmdInfoSysWinMsg` (0x021B04D8, ov021), `int SCRCMD_MSGWIN_CmdInfoSysWinMsg(u32 a, u32 b)`
(`ov021_func_021B04D8.c:40`). `a` is the VM stream, `b` is the script command
context.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `script_work` | handle | `SCRCMD_WORK_GetScriptWork(b)`, `ov021_func_021B04D8.c:45` | The VM work the info message uses |
| `work_value` | u32 | `SCRIPT_WORK_ReadWorkValue(a, b)`, `ov021_func_021B04D8.c:46` | The value the message expands |
| `window_kind` | u16 | `VM_GetU16(a)` passed to `OpenInfoSysWin`, `ov021_func_021B04D8.c:47` | The info window kind |
| `expanded` | u32 | `SCRCMD_MSGWIN_ExpandMessage(b, t, x)`, `ov021_func_021B04D8.c:48` | The expanded message text |
| `msg_window` | handle | `SCRCMD_WORK_GetMsgWindow(b)`, `ov021_func_021B04D8.c:49` | The window the message prints to |
| `cause` | enum | The script command itself, opcode of this handler | Tells a mod a script printed the message |

#### `field.msg.show`

`SCRCMD_PROC_FLD_ShowMessage` (0x021B4894, ov021), `void SCRCMD_PROC_FLD_ShowMessage(char *w, u32 b)`
(`ov021_func_021B4894.c:48`). `w` is the field message object, `b` is the
message id.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `message_id` | u32 | Argument `b` to `func_02045EC0(w->f188, b)`, `ov021_func_021B4894.c:53` | The message the field prints |
| `msg_bank` | handle | `w->f188`, `ov021_func_021B4894.c:53` | The bank the message comes from |
| `wordset` | handle | `w->f190`, `ov021_func_021B4894.c:54` | The word set the message expands with |
| `dst_buf` | handle | `w->f198`, `ov021_func_021B4894.c:54` | The buffer the message is copied to |
| `print_stream` | handle | `w->f1B8` from `PRINTSYS_PrintStreamCallBack`, `ov021_func_021B4894.c:56` | The print stream the message runs on |
| `wait` | s8 | `MSG_SPEED_GetWait()`, `ov021_func_021B4894.c:57` | The text speed the message waits for |
| `window` | handle | `w->f17C`, `ov021_func_021B4894.c:59` | The window the message draws to |

### Warps

Every warp event reads its operands from the script VM with `VM_GetU16`
(`ov021_func_021AFCD8.c:48` and the same in the siblings). The first operand is
the zone or warp id, the next two are x and z, and the last is the transition
or exit id. The position is `c << 16` and `d << 16`, a fixed-point cell.

#### `field.warp.position`

`SCRCMD_WarpToPosition` (0x021AFCD8, ov021), `int SCRCMD_WarpToPosition(int a0, int a1)`
(`ov021_func_021AFCD8.c:35`). `a0` is the VM stream, `a1` is the script command
context.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `zone_id` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFCD8.c:48` | The destination zone, the subject of the warp |
| `x` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFCD8.c:49` | The destination x cell |
| `z` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFCD8.c:50` | The destination z cell |
| `transition` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFCD8.c:51` | The exit or transition id |
| `target_pos` | `VecFx32` | `arr = {c << 16, 0, d << 16}`, `ov021_func_021AFCD8.c:52` | The destination position in field units |
| `field_map` | handle | `GAMESYSTEM_GetFieldMap(h)`, `ov021_func_021AFCD8.c:47` | The field map the warp event reads |
| `warp_event` | handle | `EVENT_MAPCHANGE_CreateWarpEvent(h, a, b, arr, e, 0)`, `ov021_func_021AFCD8.c:56` | The warp event the game runs |
| `source_zone` | u16 | Context `last_warp`, section 0a | The zone the player warps from |
| `owner` | handle | The game data; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the warp to the local player |
| `cause` | enum | The script command itself, opcode of this handler | Tells a mod a script issued the warp |

#### `field.warp.position.plain`

`SCRCMD_WarpToPositionPlain` (0x021AFD44, ov021), `int SCRCMD_WarpToPositionPlain(int a0, int a1)`
(`ov021_func_021AFD44.c:35`). The same operands as `field.warp.position` read
at `ov021_func_021AFD44.c:48-51`, and the same `target_pos` at `:52`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `zone_id` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFD44.c:48` | The destination zone |
| `x` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFD44.c:49` | The destination x cell |
| `z` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFD44.c:50` | The destination z cell |
| `transition` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFD44.c:51` | The exit or transition id |
| `target_pos` | `VecFx32` | `arr`, `ov021_func_021AFD44.c:52` | The destination position |
| `warp_event` | handle | `EVENT_MAPCHANGE_CreateWarpEventPlain(h, a, b, arr, e)`, `ov021_func_021AFD44.c:56` | The plain warp event |
| `source_zone` | u16 | Context `last_warp`, section 0a | The zone warped from |

#### `field.warp.position.event`

`SCRCMD_WarpToPositionEvent` (0x021AFDAC, ov021), `int SCRCMD_WarpToPositionEvent(int a0, int a1)`
(`ov021_func_021AFDAC.c:61`). The operands are read at `:79-81`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `zone_id` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFDAC.c:79` | The destination zone |
| `x` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFDAC.c:80` | The destination x cell |
| `z` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFDAC.c:81` | The destination z cell |
| `event_data` | handle | `GAME_DATA_GetEventData(e)`, `ov021_func_021AFDAC.c:83` | The position events the search reads |
| `event_work` | handle | `GAME_DATA_GetEventWork(e)`, `ov021_func_021AFDAC.c:84` | The event work the search reads |
| `player_pos` | `VecFx32` | `FIELD_PLAYER_GetPos(FIELDMAP_GetFieldPlayer(a), buf)`, `ov021_func_021AFDAC.c:85` | The player's position the search keys on |
| `pos_event` | handle | `EVENTDATA_SYSTEM_SearchPosEventIgnoreHeight(f, g, buf, 9)`, `ov021_func_021AFDAC.c:86` | The position event that matched |
| `target_pos` | `VecFx32` | `EVENTDATA_SYSTEM_GetPosEventCenterPosThunk(r, out)`, `ov021_func_021AFDAC.c:88` | The matched event's centre position |
| `target_arr` | `VecFx32` | `arr = {c << 16, 0, d << 16}`, `ov021_func_021AFDAC.c:89` | The destination position |
| `warp_event` | handle | `EVENT_MAPCHANGE_CreateWarpEventWithTargetPos(h, a, out, b, arr)`, `ov021_func_021AFDAC.c:93` | The event warp |
| `effect` | handle | `FIELDMAP_GetEffect(a)`, `ov021_func_021AFDAC.c:95` | The field effect the gyo effect starts on |

#### `field.warp.position.exit`

`SCRCMD_WarpToPositionFixedExit` (0x021AFE64, ov021), `int SCRCMD_WarpToPositionFixedExit(int a0, int a1)`
(`ov021_func_021AFE64.c:35`). The operands are read at `:48-51`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `zone_id` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFE64.c:48` | The destination zone |
| `x` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFE64.c:49` | The destination x cell |
| `z` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFE64.c:50` | The destination z cell |
| `transition` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFE64.c:51` | The fixed exit id |
| `target_pos` | `VecFx32` | `arr`, `ov021_func_021AFE64.c:52` | The destination position |
| `warp_event` | handle | `EVENT_MAPCHANGE_CreateFixedExitTypeWarpEvent(h, a, b, arr, e)`, `ov021_func_021AFE64.c:56` | The fixed-exit warp event |

#### `field.warp.fixedzone`

`SCRCMD_WarpToFixedZone` (0x021AFECC, ov021), `int SCRCMD_WarpToFixedZone(u32 a, u32 b)`
(`ov021_func_021AFECC.c:37`). `b` is the script command context. It reads no
operands, so the destination comes from the zone record.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `system` | handle | `SCRIPT_WORK_GetGameSystem(SCRCMD_WORK_GetScriptWork(b))`, `ov021_func_021AFECC.c:39` | The system the fixed-zone event runs under |
| `field_map` | handle | `GAMESYSTEM_GetFieldMap(p)`, `ov021_func_021AFECC.c:40` | The field map the event reads |
| `warp_event` | handle | `EVENT_MAPCHANGE_CreateFixedZoneEvent(p, q)`, `ov021_func_021AFECC.c:43` | The fixed-zone warp event |
| `source_zone` | u16 | Context `last_warp`, section 0a | The zone warped from |
| `destination` | u16 | Not obtainable at the call; the target zone is in the zone record, honest limits below | A mod cannot read the destination here |

#### `field.warp.position3d`

`SCRCMD_WarpToPosition3D` (0x021AFEFC, ov021), `int SCRCMD_WarpToPosition3D(int a0, int a1)`
(`ov021_func_021AFEFC.c:40`). The operands are read at `:54-58`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `zone_id` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFEFC.c:54` | The destination zone |
| `x` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFEFC.c:55` | The destination x |
| `y` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFEFC.c:56` | The destination y, the height |
| `z` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFEFC.c:57` | The destination z |
| `transition` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFEFC.c:58` | The transition id |
| `target_pos` | `VecFx32` | `arr = {c << 16, d << 16, e << 16}`, `ov021_func_021AFEFC.c:60` | The three-dimensional destination |
| `warp_event` | handle | `EVENT_MAPCHANGE_CreateWarpEventDefaultTransition(h, a, b, arr, f)`, `ov021_func_021AFEFC.c:64` | The default-transition warp event |

#### `field.warp.rail`

`SCRCMD_WarpToRailLocation` (0x021AFF7C, ov021), `int SCRCMD_WarpToRailLocation(int a0, int a1)`
(`ov021_func_021AFF7C.c:44`). The operands are read at `:58-62`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `zone_id` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFF7C.c:58` | The destination zone |
| `rail_a` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFF7C.c:59` | The first rail coordinate |
| `rail_b` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFF7C.c:60` | The second rail coordinate |
| `rail_c` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFF7C.c:61` | The third rail coordinate |
| `transition` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFF7C.c:62` | The transition id |
| `rail_loc` | `Q` (u16 f0, u16 f2, s16 f4, u16 f6) | `FIELD_NOGRID_MAPPER_CalcZoneRailLocation(FIELDMAP_GetNogridMapper(a), b, c, d, e, loc, 0x15)`, `ov021_func_021AFF7C.c:64` | The rail cell the warp targets |
| `warp_event` | handle | `EVENT_MAPCHANGE_CreateRailWarpEvent(h, a, b, loc, f, 0)`, `ov021_func_021AFF7C.c:65` | The rail warp event |

### Ledges and hops

#### `field.ledge.check`

`FIELD_EVENT_CHECK_CheckLedgeJump` (0x0218B408, ov021), `int FIELD_EVENT_CHECK_CheckLedgeJump(char *p, void *a, void *x)`
(`ov021_func_0218B408.c:46`). `p` is the field input, `a` is the game system,
`x` is the field map. This is the block candidate for a ledge jump.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `player` | handle | `FIELDMAP_GetFieldPlayer(x)`, `ov021_func_0218B408.c:57` | The player who may jump |
| `form_code` | u32 | `FIELD_PLAYER_GetFormCode(id)`, `ov021_func_0218B408.c:58` | The player form, which must be 2 to jump |
| `cam` | handle | `FIELDMAP_GetG3dMapper(x)`, `ov021_func_0218B408.c:59` | The map the attribute test reads |
| `dir` | u16 | `FIELD_PLAYER_ChooseMoveDirFromKeys(in.f18, in.f2C)`, `ov021_func_0218B408.c:60` | The direction the player pushes, the cause |
| `target_pos` | `VecFx32` | `FIELD_PLAYER_GetPosInDir(id, r7, buf)`, `ov021_func_0218B408.c:62` | The cell the player jumps to |
| `map_attr` | u32 | `MAP_ATTR_GetAt(cam, buf)`, `ov021_func_0218B408.c:63` | The attribute record of the target cell |
| `flags` | u16 | `MAP_ATTR_GetFlags(t)`, `ov021_func_0218B408.c:64`, `:69` | The attribute flags, which must have bits 0 and 1 clear |
| `terrain` | u16 | `MAP_ATTR_GetTerrainId(t)`, `ov021_func_0218B408.c:65` | The terrain id of the target cell |
| `long_hop` | bool | `MAP_ATTR_IsLongHopEdge(terrain)`, `ov021_func_0218B408.c:67` | Whether the ledge is a long hop |
| `path_clear` | bool | `FIELD_EVENT_CHECK_IsLedgePathClear(p, x)`, `ov021_func_0218B408.c:72` | Whether the two cells beyond are free |
| `owner` | handle | The game data; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the jump to the local player |

#### `field.ledge.task`

`FIELD_EVENT_CHECK_CreateLedgeJumpTask` (0x0218B3DC, ov021), `int FIELD_EVENT_CHECK_CreateLedgeJumpTask(int unused, int a, int b, int c, int d)`
(`ov021_func_0218B3DC.c:33`). `a` is the game system, `b` is the field map, `c`
is the direction, `d` is the long-hop flag.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `task` | handle | `func_020122C0(a, 0, main, 0x10)`, `ov021_func_0218B3DC.c:35` | The ledge jump task the game created |
| `system` | handle | `e->f8 = a`, `ov021_func_0218B3DC.c:37` | The system the task runs under |
| `field_map` | handle | `e->fC = b`, `ov021_func_0218B3DC.c:38` | The field map the task reads |
| `dir` | u16 | `e->f4 = c`, `ov021_func_0218B3DC.c:39` | The jump direction |
| `long_hop` | u16 | `e->f6 = d`, `ov021_func_0218B3DC.c:40` | Whether the jump is a long hop |

#### `field.ledge.main`

`FIELD_EVENT_CHECK_LedgeJumpTaskMain` (0x0218B32C, ov021), `int FIELD_EVENT_CHECK_LedgeJumpTaskMain(void *unused, int *st, char *ctx)`
(`ov021_func_0218B32C.c:55`). `ctx` is the 0x10-byte task record.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `step` | u32 | `*st`, `ov021_func_0218B32C.c:63` | Which step of the jump runs |
| `field_map` | handle | `ctx->fC`, `ov021_func_0218B32C.c:57` | The field map the task reads |
| `player` | handle | `FIELDMAP_GetFieldPlayer(ctx->fC)`, `ov021_func_0218B32C.c:57` | The player who jumps |
| `form_code` | u32 | `FIELD_PLAYER_GetFormCode(a)`, `ov021_func_0218B32C.c:61` | The player form |
| `mmdl` | handle | `FIELD_PLAYER_GetMmdl(a)`, `ov021_func_0218B32C.c:62` | The player's field model |
| `dir` | u16 | `ctx->f4`, `ov021_func_0218B32C.c:66`, `:73` | The jump direction |
| `long_hop` | u16 | `ctx->f6`, `ov021_func_0218B32C.c:74` | Whether the jump is a long hop |
| `ride_effect` | u32 | `FIELD_PLAYER_GetRideEffect(a)`, `ov021_func_0218B32C.c:70` | The ride effect the follow mode resets |

#### `field.ledge.path`

`FIELD_EVENT_CHECK_IsLedgePathClear` (0x0218B6F8, ov021), a state entry,
`int FIELD_EVENT_CHECK_IsLedgePathClear(char *p, void *q)` (`ov021_func_0218B6F8.c:37`).
`p` is the field input, `q` is the field map.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mmdl` | handle | `FIELD_PLAYER_GetMmdl(p->f18)`, `ov021_func_0218B6F8.c:39` | The player model the grid reads |
| `dir` | u16 | `p->f30`, `ov021_func_0218B6F8.c:40` | The direction the path test walks |
| `start_x` | s16 | `FLDMMDL_GetGridNowX(o)`, `ov021_func_0218B6F8.c:41` | The grid x the walk starts at |
| `start_z` | s16 | `FLDMMDL_GetGridNowZ(o)`, `ov021_func_0218B6F8.c:42` | The grid z the walk starts at |
| `cells_free` | bool | The return, 1 when both cells are free, `ov021_func_0218B6F8.c:51` | Whether the landing cells are clear |

#### `field.ledge.foot`

`FIELD_PLAYER_GRID_StartLedgeJumpOnFoot` (0x021A4ED8, ov021), `void FIELD_PLAYER_GRID_StartLedgeJumpOnFoot(u32 *p, int b, u8 *c)`
(`ov021_func_021A4ED8.c:32`). `p` is the player grid state, `b` is the model,
`c` is the step record.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `dir` | u16 | `((Obj19792 *)c)->f8`, `ov021_func_021A4ED8.c:34` | The jump direction |
| `acmd` | int | `FLDMMDL_ACMD_ConvertCodeByDir(dir, 0x38)`, `ov021_func_021A4ED8.c:34` | The animation command the jump sets |
| `step_kind` | u32 | The literal `1` set by `FIELD_PLAYER_CORE_SetStepKind`, `ov021_func_021A4ED8.c:36` | The step kind the jump records |
| `sound` | u32 | The literal `0x55E` played, `ov021_func_021A4ED8.c:37` | The jump sound |
| `owner` | handle | The player state `p`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the jump to the local player |

#### `field.ledge.cycle`

`FIELD_PLAYER_GRID_StartLedgeJumpOnCycle` (0x021A511C, ov021), `void FIELD_PLAYER_GRID_StartLedgeJumpOnCycle(u32 *p, int b, u8 *c)`
(`ov021_func_021A511C.c:32`). The same shape as `field.ledge.foot`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `dir` | u16 | `((Obj19799 *)c)->f8`, `ov021_func_021A511C.c:34` | The jump direction on the bicycle |
| `acmd` | int | `FLDMMDL_ACMD_ConvertCodeByDir(dir, 0x38)`, `ov021_func_021A511C.c:34` | The animation command |
| `step_kind` | u32 | The literal `1`, `ov021_func_021A511C.c:36` | The step kind |
| `sound` | u32 | The literal `0x55E`, `ov021_func_021A511C.c:37` | The jump sound |

#### `field.ledge.nogrid.walk`

`FIELD_PLAYER_NOGRID_StartWalkLedgeJump` (0x021A5F74, ov021), `void FIELD_PLAYER_NOGRID_StartWalkLedgeJump(S *a, void *b, int c, int d, u16 e)`
(`ov021_func_021A5F74.c:32`). `a` is the player state, `b` is the model, `e` is
the direction.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `dir` | u16 | Argument `e`, `ov021_func_021A5F74.c:34` | The jump direction |
| `acmd` | int | `FLDMMDL_ACMD_ConvertCodeByDir(e, 0x38)`, `ov021_func_021A5F74.c:34` | The animation command |
| `step_kind` | u32 | The literal `1`, `ov021_func_021A5F74.c:36` | The step kind |
| `player` | handle | `a->f14` to `FIELD_PLAYER_CORE_SetStepKind`, `ov021_func_021A5F74.c:36` | The player the step kind is set on |

#### `field.ledge.nogrid.slow`

`FIELD_PLAYER_NOGRID_StartSlowLedgeJump` (0x021A6338, ov021), `void FIELD_PLAYER_NOGRID_StartSlowLedgeJump(S *a, void *b, int c, int d, u16 e)`
(`ov021_func_021A6338.c:32`). The same shape as `field.ledge.nogrid.walk`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `dir` | u16 | Argument `e`, `ov021_func_021A6338.c:34` | The slow jump direction |
| `acmd` | int | `FLDMMDL_ACMD_ConvertCodeByDir(e, 0x38)`, `ov021_func_021A6338.c:34` | The animation command |
| `step_kind` | u32 | The literal `1`, `ov021_func_021A6338.c:36` | The step kind |
| `player` | handle | `a->f14`, `ov021_func_021A6338.c:36` | The player the step kind is set on |

#### `field.hop.jump`

`FIELD_PLAYER_GRID_EVENT_CreateJumpTask` (0x021C1228, ov021), `void FIELD_PLAYER_GRID_EVENT_CreateJumpTask(u32 a, u16 b, u32 c, u32 d)`
(`ov021_func_021C1228.c:48`). `a` is the player, `b` is the direction, `c` is
the map attribute, `d` is the heap tag.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `dir` | u16 | Argument `b`, `ov021_func_021C1228.c:57` | The jump direction |
| `player` | handle | Argument `a`, `ov021_func_021C1228.c:58` | The player who jumps |
| `field_map` | handle | `FIELD_PLAYER_GetFieldMap(a)`, `ov021_func_021C1228.c:59` | The field map the task reads |
| `pos` | `Vec3` | `FLDMMDL_GetPos(r7, &va)`, `ov021_func_021C1228.c:61` | The player's position before the jump |
| `target_pos` | `Vec3` | `vb` after `FLDMMDL_MOVE_AddDirVector(dir, &vb, 0x20000)`, `ov021_func_021C1228.c:63` | The jump target |
| `map_height` | int | `FLDMMDL_MOVE_GetMapHeight(r7, &vb, &out)`, `ov021_func_021C1228.c:64` | The height at the target |
| `climb_up` | bool | `out > va.y`, `ov021_func_021C1228.c:65` | Whether the jump goes up |
| `grid` | handle | `FIELD_PLAYER_GetGrid(a)`, `ov021_func_021C1228.c:67` | The player's grid |

#### `field.hop`

`FIELD_PLAYER_GRID_EVENT_CreateHopTask` (0x021C1024, ov021), `void FIELD_PLAYER_GRID_EVENT_CreateHopTask(u32 a, u16 b, u32 c, u32 d)`
(`ov021_func_021C1024.c:40`). `a` is the player, `b` is the direction, `c` is
the map attribute, `d` is the heap tag.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `dir` | u16 | Argument `b`, `ov021_func_021C1024.c:46` | The hop direction |
| `player` | handle | Argument `a`, `ov021_func_021C1024.c:47` | The player who hops |
| `field_map` | handle | `FIELD_PLAYER_GetFieldMap(a)`, `ov021_func_021C1024.c:45` | The field map the task reads |
| `grid` | handle | `FIELD_PLAYER_GetGrid(a)`, `ov021_func_021C1024.c:48` | The player's grid |
| `long_hop` | bool | `MAP_ATTR_IsLongHopEdge(MAP_ATTR_GetTerrainId(c))`, `ov021_func_021C1024.c:49` | Whether the hop is a long hop |

### Field skills and field moves

#### `field.skill.handler`

`FIELD_SKILL_CHECK_CallHandler` (0x021644C4, ov010), `int FIELD_SKILL_CHECK_CallHandler(int i, void *a, void *b)`
(`ov010_func_021644C4.c:30`). `i` is the handler index, `a` is the game system,
`b` is a context. The block candidate for a field skill.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `handler_index` | int | Argument `i`, `ov010_func_021644C4.c:38` | Names the field skill being dispatched |
| `handler` | ptr | `d_021714DC[i]`, `ov010_func_021644C4.c:38` | The handler function the skill uses |
| `slot_empty` | bool | `fn == 0`, `ov010_func_021644C4.c:39` | Whether the skill has no handler |
| `result` | int | The handler's return, `ov010_func_021644C4.c:41` | The skill's outcome |
| `game_data` | handle | `GAMESYSTEM_GetGameData(a)`, `ov010_func_021644C4.c:36` | The game data the handler receives |
| `player_work` | handle | `GAME_DATA_GetMyPlayerWork(sys)`, `ov010_func_021644C4.c:37` | The player work the handler receives |
| `owner` | handle | The game data `a`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the skill to the local player |

#### `field.skill.flags`

`FIELD_SKILL_CHECK_FillUseFlags` (0x02164480, ov010), `void FIELD_SKILL_CHECK_FillUseFlags(Flags *f, void *a, void *b)`
(`ov010_func_02164480.c:41`). `f` is the flags struct, `a` and `b` are context.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `skill_bits` | u32 | Bits 0..9 set by `func_02028748(f, i, FIELD_SKILL_CHECK_CallHandler(i, a, b))`, `ov010_func_02164480.c:49` | Which of the ten field skills are usable |
| `player_object_code` | u32 | `GAME_DATA_GetPlayerObjectCode(save)`, `ov010_func_02164480.c:50` | The player object code, stored at flag word +0x04 |
| `save` | handle | `GAME_DATA_GetField114(ICA_ANIME_GetField1C(a))`, `ov010_func_02164480.c:46` | The save block the object code reads |
| `owner` | handle | The context `a`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the flags to the local player |

#### `field.move.cut`

`MOVE_POKEMON_CreateCutEvent` (0x0215EA6C, ov010), `void *MOVE_POKEMON_CreateCutEvent(void *a, void *b)`
(`ov010_func_0215EA6C.c:25`). `a` is the skill pair, `b` is the context.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `task` | handle | `func_020122C0(b->f8, 0, main, 20)`, `ov010_func_0215EA6C.c:27` | The Cut field effect task |
| `owner` | handle | `((Obj02057 *)b)->f8`, `ov010_func_0215EA6C.c:27` | The task owner, the local player |
| `skill_pair` | `Pair` (u32 a, u32 b) | Argument `a` to `MOVE_POKEMON_InitSkillWork`, `ov010_func_0215EA6C.c:28` | The skill parameters the work stores |
| `skill_ctx` | handle | Argument `b`; the work stores `ctx[2]` and `ctx[3]`, `ov010_func_0215F0E8.c:15` | The skill context, the save-side state |

#### `field.move.flash`

`MOVE_POKEMON_CreateFlashEvent` (0x0215EC94, ov010), `void *MOVE_POKEMON_CreateFlashEvent(void *a, void *b)`
(`ov010_func_0215EC94.c:27`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `task` | handle | `func_020122C0(b->f8, 0, main, 20)`, `ov010_func_0215EC94.c:30` | The Flash field effect task |
| `owner` | handle | `((Obj02057 *)b)->f8`, `ov010_func_0215EC94.c:30` | The task owner, the local player |
| `skill_pair` | `Pair` | Argument `a`, `ov010_func_0215EC94.c:31` | The skill parameters |
| `skill_ctx` | handle | Argument `b`, `ov010_func_0215EC94.c:31` | The skill context |

#### `field.move.strength`

`MOVE_POKEMON_CreateStrengthEvent` (0x0215EBD0, ov010), `void *MOVE_POKEMON_CreateStrengthEvent(void *a, void *b)`
(`ov010_func_0215EBD0.c:26`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `task` | handle | `func_020122C0(b->f8, 0, main, 20)`, `ov010_func_0215EBD0.c:29` | The Strength field effect task |
| `owner` | handle | `((Obj02057 *)b)->f8`, `ov010_func_0215EBD0.c:29` | The task owner, the local player |
| `skill_pair` | `Pair` | Argument `a`, `ov010_func_0215EBD0.c:30` | The skill parameters |
| `skill_ctx` | handle | Argument `b`, `ov010_func_0215EBD0.c:30` | The skill context |

#### `field.move.dive`

`MOVE_POKEMON_CreateDiveEvent` (0x0215EC40, ov010), `void *MOVE_POKEMON_CreateDiveEvent(u16 *a, void *b)`
(`ov010_func_0215EC40.c:31`). It tail-calls
`SWEET_SCENT_CreateEvent(b->f8, b->f10, *a)` (`ov010_func_0215EC40.c:33`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `owner` | handle | `((Obj12315 *)b)->f8`, `ov010_func_0215EC40.c:33` | The task owner, the local player |
| `effect_ctx` | handle | `((Obj12315 *)b)->f10`, `ov010_func_0215EC40.c:33` | The effect context the create receives |
| `mode` | u8 | `*a`, the low byte of the first argument, `ov010_func_0215EC40.c:33` | The Dive mode byte |

#### `field.move.cut.main`

`MOVE_POKEMON_CutEventMain` (0x0215EA94, ov010), `int MOVE_POKEMON_CutEventMain(void *a, void *b, void *c)`
(`ov010_func_0215EA94.c:25`). `a` is the VM, `c` is the work.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `script_id` | u32 | The literal `0x2715` to `SCRIPT_SYS_ChangeScriptEvent(a, 0x2715, 0, 0)`, `ov010_func_0215EA94.c:31` | The Cut script the event runs |
| `param` | int | `((Obj12311 *)c)->fC`, `ov010_func_0215EA94.c:30` | The parameter the script receives |
| `script_event` | handle | `SCRIPT_SYS_ChangeScriptEvent(a, 0x2715, 0, 0)`, `ov010_func_0215EA94.c:31` | The script event the cut changes to |
| `event_work` | handle | `GAME_DATA_GetEventWork(GAMESYSTEM_GetGameData(c->f8))`, `ov010_func_0215EA94.c:29` | The event work the cut reads |

#### `field.move.strength.main`

`MOVE_POKEMON_StrengthEventMain` (0x0215EBF8, ov010), `int MOVE_POKEMON_StrengthEventMain(void *a, int *state, void *b)`
(`ov010_func_0215EBF8.c:28`). `a` is the VM, `b` is the work.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `script_id` | u32 | The literal `0x2711` to `SCRIPT_SYS_ChangeScriptEvent(a, 0x2711, 0, 0)`, `ov010_func_0215EBF8.c:34` | The Strength script the event runs |
| `param` | int | `((Obj12314 *)b)->fC`, `ov010_func_0215EBF8.c:33` | The parameter the script receives |
| `script_event` | handle | `SCRIPT_SYS_ChangeScriptEvent(a, 0x2711, 0, 0)`, `ov010_func_0215EBF8.c:34` | The script event the strength changes to |
| `event_work` | handle | `GAME_DATA_GetEventWork(GAMESYSTEM_GetGameData(b->f8))`, `ov010_func_0215EBF8.c:32` | The event work the strength reads |

#### `field.move.flash.main`

`MOVE_POKEMON_FlashEventMain` (0x0215ECBC, ov010), `int MOVE_POKEMON_FlashEventMain(void *a0, int *state, Arg *arg)`
(`ov010_func_0215ECBC.c:66`). `a0` is the VM, `arg` is the work.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `step` | u32 | `*state`, `ov010_func_0215ECBC.c:82` | Which step of the Flash effect runs |
| `system` | handle | `arg->f08`, `ov010_func_0215ECBC.c:76` | The system the Flash reads |
| `param` | u16 | `arg->f0c`, `ov010_func_0215ECBC.c:84` | The parameter the script receives |
| `game_data` | handle | `GAMESYSTEM_GetGameData(arg->f08)`, `ov010_func_0215ECBC.c:76` | The game data the effect reads |
| `move_model_sys` | handle | `GAME_DATA_GetMoveModelSys(obj)`, `ov010_func_0215ECBC.c:78` | The move model the Flash state reads |
| `flash_ctl` | handle | `FIELDSKILL_MAPEFF_GetFlash(FIELDMAP_GetMapEff(GAMESYSTEM_GetFieldMap(arg->f08)))`, `ov010_func_0215ECBC.c:79` | The Flash map-effect controller |
| `heap_id` | u16 | `FIELDMAP_GetHeapId(field_map)`, `ov010_func_0215ECBC.c:80` | The heap the script event allocates in |
| `flash_active` | u8 | `FIELD_STATUS_IsFlashActive(obj2)`, `ov010_func_0215ECBC.c:90` | Whether Flash is already active |
| `map_effect_mask` | u16 | `FIELD_STATUS_GetMapEffectMask(o)`, `ov010_func_0215ECBC.c:95` | The map effect mask the Flash sets |
| `flash_state` | u16 | `FIELD_FLASH_GetState(ctl)`, `ov010_func_0215ECBC.c:102` | The Flash controller state the effect waits on |
| `script_id` | u32 | The literal `0x2718` to `SCRIPT_SYS_CallScriptEvent(a0, 0x2718, 0, 0, d)`, `ov010_func_0215ECBC.c:85` | The Flash script the event runs |

#### `field.move.cut.check`

`MOVE_POKEMON_CheckCut` (0x0215EA58, ov010), a state entry,
`int MOVE_POKEMON_CheckCut(int a0, int a1)` (`func_0215EA58.c:22`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `skill_index` | int | The literal `0` set at `func_0215EA58.c:26` | The skill index Cut tests |
| `context` | handle | Argument `a0`, `func_0215EA58.c:28` | The skill context the test reads |
| `usable` | bool | `MOVE_POKEMON_IsSkillUsable(a0, 0)`, `func_0215EA58.c:28` | Whether Cut is usable |

#### `field.move.flash.check`

`MOVE_POKEMON_CheckFlash` (0x0215EC80, ov010), a state entry,
`int MOVE_POKEMON_CheckFlash(void *p)` (`ov010_func_0215EC80.c:15`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `skill_index` | int | The literal `5` to `MOVE_POKEMON_IsSkillUsable`, `ov010_func_0215EC80.c:17` | The skill index Flash tests |
| `context` | handle | Argument `p`, `ov010_func_0215EC80.c:17` | The skill context the test reads |
| `usable` | bool | `MOVE_POKEMON_IsSkillUsable(p, 5) != 0`, `ov010_func_0215EC80.c:17` | Whether Flash is usable |

#### `field.move.strength.check`

`MOVE_POKEMON_CheckStrength` (0x0215EBBC, ov010), a state entry,
`int MOVE_POKEMON_CheckStrength(void *p)` (`ov010_func_0215EBBC.c:15`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `skill_index` | int | The literal `3`, `ov010_func_0215EBBC.c:17` | The skill index Strength tests |
| `context` | handle | Argument `p`, `ov010_func_0215EBBC.c:17` | The skill context the test reads |
| `usable` | bool | `MOVE_POKEMON_IsSkillUsable(p, 3) != 0`, `ov010_func_0215EBBC.c:17` | Whether Strength is usable |

#### `field.move.waterfall.check`

`MOVE_POKEMON_CheckWaterfall` (0x0215EC6C, ov010), a state entry,
`int MOVE_POKEMON_CheckWaterfall(void *p)` (`ov010_func_0215EC6C.c:15`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `skill_index` | int | The literal `4`, `ov010_func_0215EC6C.c:17` | The skill index Waterfall tests |
| `context` | handle | Argument `p`, `ov010_func_0215EC6C.c:17` | The skill context the test reads |
| `usable` | bool | `MOVE_POKEMON_IsSkillUsable(p, 4) != 0`, `ov010_func_0215EC6C.c:17` | Whether Waterfall is usable |

#### `field.move.iaigiri`

`SCRCMD_IAIGIRI_StartOnPlayer` (0x021C4678, ov021), `int SCRCMD_IAIGIRI_StartOnPlayer(void *a, void *b)`
(`ov021_func_021C4678.c:35`). `b` is the script command context.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `field_map` | handle | `GAMESYSTEM_GetFieldMap(SCRCMD_WORK_GetGameSystem(b))`, `ov021_func_021C4678.c:37` | The field map the effect reads |
| `mmdl` | handle | `FIELD_PLAYER_GetMmdl(FIELDMAP_GetFieldPlayer(g))`, `ov021_func_021C4678.c:38` | The player model the effect plays on |
| `effect` | handle | `FIELDMAP_GetEffect(g)`, `ov021_func_021C4678.c:40` | The field effect the iaigiri starts on |
| `cause` | enum | The script command itself, opcode of this handler | Tells a mod a script started the cut effect |

### The field bag and overlay procedure

#### `field.bag.open`

`SCRCMD_PROC_CallBagScreen` (0x0215C72C, ov010), `int SCRCMD_PROC_CallBagScreen(void *c, void *vm)`
(`ov010_func_0215C72C.c:55`). `c` is the VM stream, `vm` is the script command
context.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `selector` | u32 | `SCRIPT_WORK_ReadWorkValue(c, vm)`, `ov010_func_0215C72C.c:66` | The bag mode the script asked for |
| `pocket` | u32 | The mapping of `selector`, `0 -> 4`, `1 -> 5`, else `0`, `ov010_func_0215C72C.c:69-74` | The pocket the bag opens on |
| `out_a` | ptr | `SCRIPT_WORK_ReadWorkPointer(c, vm)`, `ov010_func_0215C72C.c:67` | The first result slot the bag fills |
| `out_b` | ptr | `SCRIPT_WORK_ReadWorkPointer(c, vm)`, `ov010_func_0215C72C.c:68` | The second result slot the bag fills |
| `bag_param` | handle | `BAG_PARAM_Create(game_data, 0, k, 4)`, `ov010_func_0215C72C.c:75` | The bag parameter the overlay receives |
| `overlay_id` | u32 | `ov070_id`, `ov010_func_0215C72C.c:79` | The overlay, 0x46, the bag opens |
| `handoff` | handle | `h[0] = a`, `h[1] = b`, `ov010_func_0215C72C.c:76-78` | The two result pointers the close callback writes |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(vm)`, `ov010_func_0215C72C.c:75` | The game data the bag reads |

#### `field.bag.closed`

`SCRCMD_PROC_OnBagClosed` (0x0215C700, ov010), `void SCRCMD_PROC_OnBagClosed(Pair *p)`
(`ov010_func_0215C700.c:22`). `p->context` is the bag context, `p->outputs` is
the handoff record.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `changed` | bool | `c->first`, written to `*o->first`, `ov010_func_0215C700.c:26-29` | Whether the bag changed |
| `result` | u32 | `c->second`, written to `*o->second`, `ov010_func_0215C700.c:30` | The bag result the script reads |
| `context` | handle | `p->context`, `ov010_func_0215C700.c:24` | The bag context the callback reads |
| `outputs` | handle | `p->outputs`, `ov010_func_0215C700.c:25` | The two result slots the callback fills |

#### `field.overlayproc.start`

`SCRCMD_PROC_StartOverlayProc` (0x0215C63C, ov010), `void SCRCMD_PROC_StartOverlayProc(void *c, void *vm, u32 ovId, const void *proc, void *work, u32 arg0, u32 arg1)`
(`ov010_func_0215C63C.c:48`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `overlay_id` | u32 | Argument `ovId`, `ov010_func_0215C63C.c:48` | The overlay the procedure runs in |
| `proc` | ptr | Argument `proc`, `ov010_func_0215C63C.c:48` | The procedure entry the game calls |
| `work` | handle | Argument `work`, `ov010_func_0215C63C.c:48` | The work the procedure reads |
| `arg0` | u32 | Argument `arg0`, `ov010_func_0215C63C.c:48` | The first hand-off word |
| `arg1` | u32 | Argument `arg1`, `ov010_func_0215C63C.c:48` | The second hand-off word |
| `system` | handle | `SCRCMD_WORK_GetGameSystem(vm)`, `ov010_func_0215C63C.c:54` | The system the procedure runs under |
| `handoff` | handle | `h`, fields `work`, `arg1`, `arg0`, `ov010_func_0215C63C.c:56-59` | The 12-byte record the wait reads |
| `cause` | enum | The script command itself, opcode of this handler | Tells a mod a script started the procedure |

#### `field.overlayproc.wait`

`SCRCMD_PROC_WaitOverlayProc` (0x0215C69C, ov010), `int SCRCMD_PROC_WaitOverlayProc(int a0, int a1)`
(`func_0215C69C.c:34`). `a1` is the script command context.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `proc_status` | u32 | `GAMESYSTEM_GetProcStatus(t6)`, `func_0215C69C.c:51` | Whether the overlay procedure is still running |
| `system` | handle | `SCRCMD_WORK_GetGameSystem(a0)`, `func_0215C69C.c:42` | The system the status reads |
| `handoff` | handle | `SCRIPT_WORK_GetUserWork(t4)`, `func_0215C69C.c:48` | The hand-off record the wait reads |
| `callback` | ptr | `((Obj00005 *)t5)->f8`, `func_0215C69C.c:58` | The close callback the wait runs |
| `work` | handle | `((Obj00005 *)t5)->f0`, `func_0215C69C.c:66` | The work the wait frees |

### Season, random, save record and special location

#### `field.random`

`SCRCMD_WORK_CmdGetRandom` (0x0215AABC, ov010), a state entry,
`int SCRCMD_WORK_CmdGetRandom(void *a0, void *a1)` (`func_0215AABC.c:29`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `range` | u32 | `SCRIPT_WORK_ReadWorkValue(a0, a1)`, `func_0215AABC.c:33` | The range the roll is bounded by |
| `value` | u16 | `func_020056EC(range)`, `func_0215AABC.c:33` | The random value, the subject |
| `out_slot` | ptr | `SCRIPT_WORK_ReadWorkPointer(a0, a1)`, `func_0215AABC.c:31` | The work slot the value lands in |

#### `field.save.record`

`SCRCMD_WORK_CmdGetSaveRecord` (0x0215AAF4, ov010), a state entry,
`int SCRCMD_WORK_CmdGetSaveRecord(int a0, int a1)` (`func_0215AAF4.c:26`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `stars` | int | `TRCARD_SYS_CountStars(v0)`, `func_0215AAF4.c:41` | The save record the script reads, the trainer card star count |
| `out_slot` | ptr | `SCRIPT_WORK_ReadWorkPointer(a0, a1)`, `func_0215AAF4.c:39` | The work slot the count lands in |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(a1)`, `func_0215AAF4.c:35` | The game data the count reads |
| `owner` | handle | The game data `v0`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the record to the local player |

#### `field.season`

`SCRCMD_WORK_CmdGetSeason` (0x0215ABD0, ov010), a state entry,
`int SCRCMD_WORK_CmdGetSeason(int a0, int a1)` (`func_0215ABD0.c:27`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `season` | u8 | `GAME_DATA_GetSeason(v0)`, `func_0215ABD0.c:39` | The current season, the subject |
| `out_slot` | ptr | `SCRIPT_WORK_ReadWorkPointer(a0, a1)`, `func_0215ABD0.c:34` | The work slot the season lands in |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(a1)`, `func_0215ABD0.c:37` | The game data the season reads |

#### `field.special.location`

`SCRCMD_WORK_CmdSetSpecialLocation` (0x0215ADCC, ov010), `int SCRCMD_WORK_CmdSetSpecialLocation(int a0, int a1)`
(`ov010_func_0215ADCC.c:32`). `a0` is the VM stream, `a1` is the script command
context. The five operands are read at `:36-40`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `zone_id` | int | `SCRIPT_WORK_ReadWorkValue(a0, a1)`, `ov010_func_0215ADCC.c:36` | The zone of the special location |
| `x` | s16 | `SCRIPT_WORK_ReadWorkValue(a0, a1)`, `ov010_func_0215ADCC.c:37` | The x of the special location |
| `y` | int | `SCRIPT_WORK_ReadWorkValue(a0, a1)`, `ov010_func_0215ADCC.c:38` | The y of the special location, shifted left 16 |
| `z` | int | `SCRIPT_WORK_ReadWorkValue(a0, a1)`, `ov010_func_0215ADCC.c:39` | The z of the special location, shifted left 16 |
| `w` | int | `SCRIPT_WORK_ReadWorkValue(a0, a1)`, `ov010_func_0215ADCC.c:40` | The w of the special location, shifted left 16 |
| `location` | int[7] | `ZONEDATA_SetDirectLocation(buf, c, r6, r7 << 16, v10 << 16, v0 << 16)`, `ov010_func_0215ADCC.c:41` | The location record the game stores |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(a1)`, `ov010_func_0215ADCC.c:35` | The game data the location is set on |
| `cause` | enum | The script command itself, opcode of this handler | Tells a mod a script set the location |

### Npc spawn, overlay sequence and the flash override

#### `field.npc.spawn`

`FIELDMAP_AddZoneNpcs` (0x021894E0, ov021), `void FIELDMAP_AddZoneNpcs(int a, int b, int c, int d)`
(`ov021_func_021894E0.c:28`). `a` is the game data, `b` is the model list the
NPCs are added to, `c` is the zone's NPC set, `d` is the list kind. It adds the
zone's NPC models when the set is not empty.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `game_data` | handle | Argument `a`, `ov021_func_021894E0.c:28`; `GAME_DATA_GetEventWork(a)` at `:33` | The owner record the flags are read from |
| `list` | handle | Argument `b`, `ov021_func_021894E0.c:28` | The model list the NPCs are added to |
| `npc_set` | handle | Argument `c`, `ov021_func_021894E0.c:28`; `EVENTDATA_SYSTEM_GetNpcCount(c)` at `:30` | The zone's NPC set |
| `list_kind` | u32 | Argument `d`, `ov021_func_021894E0.c:28` | The list the add targets |
| `npc_count` | u32 | `EVENTDATA_SYSTEM_GetNpcCount((u8 *)c)`, `ov021_func_021894E0.c:30` | The number of NPCs the zone adds |
| `npc_headers` | handle | `EVENTDATA_SYSTEM_GetNpcHeaders((u8 *)c)`, `ov021_func_021894E0.c:34` | The NPC header list the add walks |
| `event_work` | handle | `GAME_DATA_GetEventWork((const struct BigObj *)a)`, `ov021_func_021894E0.c:33` | The flags the add tests |
| `added` | bool | The `r != 0` branch, `ov021_func_021894E0.c:32` | Whether the zone has any NPC to add |

#### `field.overlay.sequence`

`CALENDER_OverlaySequenceEvent` (0x02164A2C, ov010), `int CALENDER_OverlaySequenceEvent(void *evt, u32 *seq, SeqWork2 *w)`
(`ov010_func_02164A2C.c:85`). `evt` is the event the step calls, `seq` is the
step index, `w` is the sequence work. It runs one step of the field overlay
sequence per call and returns 1 when the step reaches 8.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event` | handle | Argument `evt`, `ov010_func_02164A2C.c:85`; `GAME_EVENT_CallEvent(evt, ...)` at `:95` | The event the step calls |
| `step` | u32 | `*seq`, `ov010_func_02164A2C.c:93` | The step the call runs, 0 to 8 |
| `work` | handle | Argument `w`, `ov010_func_02164A2C.c:85` | The sequence work |
| `system` | handle | `w->sys`, `ov010_func_02164A2C.c:90` | The game system the sequence runs under |
| `field` | handle | `ICA_ANIME_GetField20(sys)`, `ov010_func_02164A2C.c:92` | The field the fade and open steps act on |
| `handoff` | handle | `w->sub`, filled at `ov010_func_02164A2C.c:106-109` | The 12-byte record the ov216 procedure receives |
| `handoff_flag` | u8 | `w->sub.flag`, `ov010_func_02164A2C.c:124` | The flag that switches the subscreen mode at step 6 |
| `done` | bool | The return 1 at step 8, `ov010_func_02164A2C.c:134` | Whether the sequence finished |

#### `field.flash`

`FIELD_FLASH_Apply` (0x021C7E30, ov021), `void FIELD_FLASH_Apply(M *m)`
(`ov021_func_021C7E30.c:27`). An override. `m` is the field flash object; the
function draws the flash wipe with the object's tween size and alpha.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flash` | handle | Argument `m`, `ov021_func_021C7E30.c:27` | The flash object |
| `draw_handle` | u32 | `m->f0`, `ov021_func_021C7E30.c:32` | The handle the wipe is drawn with |
| `tween` | handle | `m->sub`, `ov021_func_021C7E30.c:31-32` | The tween state the size and alpha read |
| `size` | u32 | `FIELD_FLASH_GetTweenSize(m->sub)`, `ov021_func_021C7E30.c:31` | The wipe size |
| `alpha` | u8 | `FIELD_FLASH_GetTweenAlpha(m->sub)`, `ov021_func_021C7E30.c:32` | The wipe alpha |
| `applied` | bool | The `m != 0` test, `ov021_func_021C7E30.c:30` | Whether the flash was applied |

### Field state reads

These five are state entries, so no slot fires. A mod reads each through the
accessor family of `docs/mod-events.md`, section 4.

#### `weather.on_date`

`CALENDER_GetWeatherOnDate` (0x02164844, ov010), a state entry,
`u8 CALENDER_GetWeatherOnDate(u8 *a, u32 b, u8 c, u8 d)`
(`ov010_func_02164844.c:35`). It returns the weather a zone has on a date.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `weather` | u8 | The return, `ov010_func_02164844.c:41`, `:45` | The weather id for the zone and date, the subject |
| `calendar` | handle | Argument `a`, `ov010_func_02164844.c:35`; `CALENDER_FindZoneOffset(a, b)` at `:37` | The calendar object the zone offset is read from |
| `zone_id` | u32 | Argument `b`, `ov010_func_02164844.c:35` | The zone the weather is asked about |
| `month` | u8 | Argument `c`, `ov010_func_02164844.c:35`; `CALENDER_GetDayOfYear(c, d)` at `:43` | The month of the date |
| `day` | u8 | Argument `d`, `ov010_func_02164844.c:35` | The day of the date |
| `has_table` | bool | The `t == 0xffff` test, `ov010_func_02164844.c:40` | Whether the zone has a weather table or the zone default applies |

#### `season.display`

`FIELD_STATUS_GetSeasonDispFlag` (0x02014484, arm9), a state entry,
`u8 FIELD_STATUS_GetSeasonDispFlag(struct Sub02014444Obj *o)`
(`arm9/unit_02014444.c:132`). It returns the season display flag at `+0x11` of
the field status object.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flag` | u8 | The return `o->f11`, `arm9/unit_02014444.c:134` | Whether the season banner shows, the subject |
| `status` | handle | Argument `o`, `arm9/unit_02014444.c:132` | The field status object the flag is read from |

The flag is installed by `FIELD_STATUS_SwapSeasonDispFlag`
(`arm9/unit_02014444.c:125-130`), which returns the previous value.

#### `bicycle.usable`

`EVENTDATA_SYSTEM_CheckBicycleUsable` (0x02164508, ov010), a state entry,
`int EVENTDATA_SYSTEM_CheckBicycleUsable(int a0, int a1, int a2)`
(`func_02164508.c:35`). It returns whether the bicycle may be used: the player
must be off the bicycle, the zone must enable it, and the tile must not be
rough ground.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `usable` | int | The return, `func_02164508.c:69`, `:77`, `:82`, `:85-89` | Whether the bicycle is usable, the subject |
| `game_data` | handle | Argument `a2` (loaded into `a0`), `func_02164508.c:42-43` | The record the zone and object code are read from |
| `field_map` | handle | Argument `a1`, `func_02164508.c:44`, `:53-54` | The field map the player attribute is read from |
| `zone_id` | u16 | `GAME_DATA_GetPlayerZoneId(a2)`, `func_02164508.c:45` | The zone whose bicycle setting is read |
| `object_code` | s8 | `GAME_DATA_GetPlayerObjectCode(a2)` truncated to a byte, `func_02164508.c:49-52` | Whether the player is on the bicycle |
| `terrain` | int | `MAP_ATTR_GetTerrainId(FIELD_PLAYER_GetNowAttribute(player))`, `func_02164508.c:56-58` | The tile the player stands on |
| `rough_ground` | bool | `MAP_ATTR_IsRoughGround(terrain)`, `func_02164508.c:60` | Whether the tile forbids the bicycle |
| `bike_enabled` | u32 | `ZONEDATA_IsBikeEnabled(zone_id)`, `func_02164508.c:72` | Whether the zone allows the bicycle |

#### `player.position`

`FIELD_PLAYER_GetPos` (0x021A3038, ov021), a state entry,
`void FIELD_PLAYER_GetPos(u8 *p, struct VecFx32 *out)` (`func_021A3038.c:28`).
It writes the player's field position to `out`; the core object is at `p->f4`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `position` | `VecFx32` | The `out` write through `FIELD_PLAYER_CORE_GetPos(p->f4, out)`, `func_021A3038.c:30` | The player's field position, the subject |
| `player` | handle | Argument `p`, `func_021A3038.c:28` | The field player the position is read from |
| `core` | handle | `p->f4`, `func_021A3038.c:30` | The core object the position lives in |

#### `camera`

`FIELDMAP_GetCamera` (0x02188C58, ov021), a state entry,
`void *FIELDMAP_GetCamera(void *p)` (`func_02188C58.c:20`). It returns the
camera pointer at `+0x10` of the field map object.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `camera` | handle | The return `p->f10`, `func_02188C58.c:22` | The field camera, the subject |
| `field_map` | handle | Argument `p`, `func_02188C58.c:20` | The field map object the camera is read from |

### Fields a mod would want that cannot be obtained

These fields a mod plausibly wants are not reachable at the call site or through
a tracked context, so they are left out of the payloads rather than invented.

- **The NPC's own identity.** The talk events give the person index and the
  person record (`ov020_func_021870C4.c:121-122`), but aletheia does not name
  the person record's fields. The NPC's trainer id, sprite and name are not
  obtainable at the call.
- **The script id that issued a warp or a field command.** The warp events read
  the script work (`ov021_func_021AFCD8.c:46`), but aletheia does not name the
  script work's current-script field. The warp's cause is the command opcode,
  not the script id.
- **The destination of `field.warp.fixedzone`.** The command reads no operands
  (`ov021_func_021AFECC.c:37`); the target zone is in the zone record, which the
  call does not pass.
- **The map attribute record beyond flags and terrain.** `MAP_ATTR_GetAt`
  returns an opaque record; the ledge check reads only flags and terrain
  (`ov021_func_0218B408.c:64-65`). The other attribute bits are not named.
- **The remote player's identity in a talk.** The comm talk names the peer slot
  (`ov018_func_0217FE6C.c:137`), but the peer's trainer name and party are not
  read here.
- **The field input's bit flags by name.** `FIELD_EVENT_CHECK_InitInput` fills
  eleven bits (`ov021_func_0218A4E0.c:131-159`) and the comment names talk,
  menu, registered item, no-input, step, surf and push, but aletheia does not
  label every bit. The payload copies them as `b0` to `b10`.

### The thinnest payloads

These events carry the least, so a mod bound to them decides on little.

- **`field.warp.fixedzone`.** No operands are read; the payload is the system,
  the field map and the warp event.
- **`field.event.run`.** Only the move kind and the two arguments.
- **`field.map.fixedzone`.** The change record is opaque and only two of its
  words are used.
- **`field.talk.npc.cmd`.** Four fields, all handles.
- **`field.ledge.foot`, `field.ledge.cycle`, `field.ledge.nogrid.walk`,
  `field.ledge.nogrid.slow`.** Each is the direction, the animation command and
  the step kind.
- **`field.msg.plain`.** The script work, the message id and the mode.
- **`field.random`, `field.season`, `field.save.record`.** Each is one value and
  its output slot.

## 14. Inventory, items and moves

The inventory, items and moves area is the 37 entries of `docs/mod-events.md`,
section 1, that touch the bag, the gifts, the field item use, the healing, the
shops, the money, the move writes and the TM and tutor (18 events, 16 state
entries and 3 override points). The port tracks the
item context (section 0a) for the item in play, the shop, the gift and the last
move written. The six battle item and bag events are specified in section 9.
The move, tutor and relearn events are shared with the progression area; this
section is their home and the progression area points here. Each table gives
the field, its type, where it comes from, and why a mod wants it.

### Bag events

#### `item.add` (0x021AF544, ov021, `SCRCMD_AddItem`)

The command reads the item id and count from the script and adds them to the
local player's bag. It is an action the game has not yet taken at the slot, so
it is a block candidate (`docs/mod-events.md`, section 2).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `item_id` | u16 | first `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF544.c:42` | the subject; which item |
| `count` | u32 | second `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF544.c:43` | how many to add |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021AF544.c:40` | the allocation the bag uses |
| `bag` | ptr | `GAME_DATA_GetMyItem(SCRCMD_WORK_GetGameData(b))`, `src/matched/ov/ov021_func_021AF544.c:41` | the owner's bag; in a soullink game this attributes the add |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(b)`, `src/matched/ov/ov021_func_021AF544.c:41` | the owner record |
| `result` | u16 | return of `func_02007E50(x,u,v,p)`, `src/matched/ov/ov021_func_021AF544.c:45` | how many were actually added |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021AF544.c:44` | where the script reads the result |
| `pocket` | u32 | a call the port can make, `SCRCMD_GetItemPocket` 0x021AF6E8 | which pocket the item lands in |
| `cause` | u32 | the script command being run; the command context `a` | which script granted it |
| `block` | bool | the return contract, `docs/mod-events.md`, section 2 | a block forbids the add |

#### `item.remove` (0x021AF594, ov021, `SCRCMD_RemoveItem`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `item_id` | u16 | first `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF594.c:41` | the subject |
| `count` | u32 | second `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF594.c:42` | how many to remove |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021AF594.c:36` | the bag allocation |
| `bag` | ptr | `GAME_DATA_GetMyItem(SCRCMD_WORK_GetGameData(b))`, `src/matched/ov/ov021_func_021AF594.c:40` | the owner's bag |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(b)`, `src/matched/ov/ov021_func_021AF594.c:40` | the owner record |
| `result` | u16 | return of `func_02007F1C(d,e,f,c)`, `src/matched/ov/ov021_func_021AF594.c:44` | how many were actually removed |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021AF594.c:43` | where the script reads the result |
| `quantity_before` | u32 | a call the port can make, `SCRCMD_GetItemQuantity` 0x021AF684 | the count before the remove |
| `cause` | u32 | the command context `a` | which script took it |

#### `item.canadd` (0x021AF5E4, ov021, `SCRCMD_CanAddItem`)

A state read. The payload is the query and the answer.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `item_id` | u16 | first `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF5E4.c:41` | the item asked about |
| `count` | u32 | second `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF5E4.c:42` | how many asked about |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021AF5E4.c:36` | the bag allocation |
| `bag` | ptr | `GAME_DATA_GetMyItem(SCRCMD_WORK_GetGameData(b))`, `src/matched/ov/ov021_func_021AF5E4.c:40` | the owner's bag |
| `can_add` | bool | return of `func_02007E3C(d,e,f,c)`, `src/matched/ov/ov021_func_021AF5E4.c:44` | the answer a handler branches on |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021AF5E4.c:43` | where the script reads the answer |

#### `item.has` (0x021AF634, ov021, `SCRCMD_HasItem`)

A state read.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `item_id` | u16 | first `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF634.c:41` | the item asked about |
| `count` | u32 | second `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF634.c:42` | the threshold asked about |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021AF634.c:36` | the bag allocation |
| `bag` | ptr | `GAME_DATA_GetMyItem(SCRCMD_WORK_GetGameData(b))`, `src/matched/ov/ov021_func_021AF634.c:40` | the owner's bag |
| `has` | bool | return of `func_02007F68(d,e,f,c)`, `src/matched/ov/ov021_func_021AF634.c:44` | the answer a handler branches on |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021AF634.c:43` | where the script reads the answer |

#### `item.quantity` (0x021AF684, ov021, `SCRCMD_GetItemQuantity`)

A state read.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `item_id` | u16 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF684.c:41` | the item asked about |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021AF684.c:36` | the bag allocation |
| `bag` | ptr | `GAME_DATA_GetMyItem(SCRCMD_WORK_GetGameData(b))`, `src/matched/ov/ov021_func_021AF684.c:40` | the owner's bag |
| `quantity` | u16 | return of `func_02007FB8(d,e,c)`, `src/matched/ov/ov021_func_021AF684.c:43` | the count a handler branches on |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021AF684.c:42` | where the script reads the count |

#### `item.machine` (0x021AF750, ov021, `SCRCMD_GetItemMachineFlag`)

The thinnest event in the area. One argument, a boolean return, no VM context.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `item_id` | u32 | call argument `a`, `src/matched/ov/func_021AF750.c:26` | the item asked about |
| `is_machine` | bool | return of `FILE_UTIL_Check1288(a)`, `src/matched/ov/func_021AF750.c:28`, `src/matched/arm9/func_02021288.c:28` | whether the item is a TM, HM or similar machine |

#### `item.pocket` (0x021AF6E8, ov021, `SCRCMD_GetItemPocket`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `item_id` | u16 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF6E8.c:33` | the item asked about |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021AF6E8.c:32` | the item record allocation |
| `pocket` | u32 | `FILE_UTIL_Get_0FE4(x, 5, y)`, selector 5, `src/matched/ov/ov021_func_021AF6E8.c:35` | which pocket the item belongs to |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021AF6E8.c:35` | where the script reads the pocket |

#### `item.partyuse` (0x021AF71C, ov021, `SCRCMD_GetItemPartyUse`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `item_id` | u16 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF71C.c:33` | the item asked about |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021AF71C.c:32` | the item record allocation |
| `party_use` | u32 | `FILE_UTIL_Get_0FE4(x, 0xf, y)`, selector 0xf, `src/matched/ov/ov021_func_021AF71C.c:35` | the party-use kind a handler branches on |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021AF71C.c:35` | where the script reads the kind |

### Gifts

#### `item.gift.daily` (0x021B7364, ov021, `SCRCMD_POKEMON_FLD_GetDailyGift`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `gift_index` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B7364.c:31` | which daily gift |
| `gift_index_used` | u32 | clamped to 0 when `v >= 11`, `src/matched/ov/ov021_func_021B7364.c:33-35` | the index actually read |
| `gift_item` | u16 | `sDailyGifts[v]`, `src/matched/ov/ov021_func_021B7364.c:36` | the item granted |
| `gift_table` | ptr | `sDailyGifts`, `src/matched/ov/ov021_func_021B7364.c:24` | the whole table, so a handler can read neighbours |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B7364.c:32` | where the script reads the item |
| `date_context` | context | tracked, fed by `item.gift.daily.index` | the day that chose the index |

#### `item.gift.daily.index` (0x021B73B8, ov021, `SCRCMD_POKEMON_FLD_GetDailyGiftIndex`)

A state read that computes the index. It feeds the context above.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `month` | u32 | `FIELD_CLOCK_GetMonth(h)`, `src/matched/ov/ov021_func_021B73B8.c:43` | the month the gift depends on |
| `day` | u32 | `FIELD_CLOCK_GetDay(h)`, `src/matched/ov/ov021_func_021B73B8.c:44` | the day the gift depends on |
| `special_date` | bool | `SCRCMD_POKEMON_FLD_IsSpecialDate(x,y)`, `src/matched/ov/ov021_func_021B73B8.c:45` | whether the special gift applies |
| `gift_index` | u16 | 10 on a special date, else `sDailyGiftIndexByDay[y % 10]`, `src/matched/ov/ov021_func_021B73B8.c:46-48` | the index a handler branches on |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(b)`, `src/matched/ov/ov021_func_021B73B8.c:42` | the owner record the clock is read from |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B73B8.c:41` | where the script reads the index |

#### `item.gift.mystery` (0x021B6BDC, ov021, `SCRCMD_POKEMON_FLD_HandleMysteryGift`)

One command, eight modes. The mode chooses what the command does: 0 test, 1
can-receive, 2 receive message, 3 follow-up message, 4 receive, 5 delivery NPC
id, 6 delivery NPC loaded, 7 receive type.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mode` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B6BDC.c:88` | which of the eight actions is asked |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(b)`, `src/matched/ov/ov021_func_021B6BDC.c:90` | the owner record |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021B6BDC.c:91` | the mystery-data allocation |
| `mystery_data` | ptr | `MYSTERY_DATA_Create(...)`, `src/matched/ov/ov021_func_021B6BDC.c:92` | the live mystery record |
| `card` | ptr | `PDW_POSTMAN_FindMysteryCard(h, &x, buf)`, `src/matched/ov/ov021_func_021B6BDC.c:93` | the 0xcc-byte card the port must copy |
| `card_index` | u32 | written by `FindMysteryCard` into `x`, `src/matched/ov/ov021_func_021B6BDC.c:93` | which of the 12 card slots |
| `gift_kind` | u8 | `PDW_POSTMAN_GetMysteryGiftType(h)`, `src/matched/ov/ov021_func_021B6BDC.c:94`, `src/matched/ov/ov021_func_021B7864.c:32` | the gift category that picks the handler |
| `handler_id` | u32 | `sMysteryGiftKindHandlers[k].id`, `src/matched/ov/ov021_func_021B6BDC.c:95` | the kind table entry that matches |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B6BDC.c:89` | where the script reads the mode result |
| `out_value` | u16 | `*out` in each mode's case, `src/matched/ov/ov021_func_021B6BDC.c:100-133` | the answer a handler branches on |
| `received` | bool | `SCRCMD_POKEMON_FLD_ReceiveGift(...) == 1`, `src/matched/ov/ov021_func_021B6BDC.c:117` | whether the gift was taken |
| `liberty_pass` | bool | `SCRCMD_POKEMON_FLD_IsLibertyPassGift(p,k)`, `src/matched/ov/ov021_func_021B6BDC.c:119` | whether the Liberty Pass special case fired |
| `field_map` | ptr | `GAMESYSTEM_GetFieldMap(SCRCMD_WORK_GetGameSystem(b))`, `src/matched/ov/ov021_func_021B6BDC.c:126` | the map the delivery NPC is on |
| `gift_handler_table` | ptr | `sMysteryGiftKindHandlers`, `src/matched/ov/ov021_func_021B6BDC.c:36` | the kind table the handler walks |
| `receive_handler_table` | ptr | `d_021D9978`, `src/matched/ov/ov021_func_021B6D98.c:25` | the per-kind receive function |
| `type_handler_table` | ptr | `d_021D997C`, `src/matched/ov/ov021_func_021B6DC0.c:25` | the per-kind receive-type function |

The concrete gift content, an item id or a Pokemon, is not returned. It is
inside the card buffer and the handler table. The port copies the card so a
handler can read it.

#### `item.gift.postman` (0x021B76B4, ov021, `PDW_POSTMAN_ReceiveItems`)

One call walks 0x14 container slots and adds each. The payload is the container
and what was taken from it.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `game_data` | ptr | call argument `a0`, `src/matched/ov/func_021B76B4.c:35` | the owner record |
| `key` | u32 | call argument `a1`, `src/matched/ov/func_021B76B4.c:35` | the bag key the add uses |
| `container` | ptr | call argument `a2`, `src/matched/ov/func_021B76B4.c:41-42` | the postman container |
| `slot_count` | u32 | the loop bound `0x14`, `src/matched/ov/func_021B76B4.c:40` | how many slots are scanned |
| `slots` | array | per slot `i`, item id `func_02009310(a2,i)` and count `func_02009330(a2,i)`, `src/matched/ov/func_021B76B4.c:41-42` | the items offered, id and count each |
| `added` | array of bool | `func_02007E50(saved,r6,r2,a1) == 1`, `src/matched/ov/func_021B76B4.c:44` | which items were added |
| `removed` | array of bool | `func_02009364(a2,i)` when added, `src/matched/ov/func_021B76B4.c:45` | which slots the container released |
| `bag` | ptr | `GAME_DATA_GetMyItem(a0)`, `src/matched/ov/func_021B76B4.c:37` | the owner's bag |

#### `item.gift.postman.count` (0x021B76F8, ov021, `PDW_POSTMAN_CountUnreceivable`)

A state read over the same container.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `game_data` | ptr | call argument `p`, `src/matched/ov/ov021_func_021B76F8.c:39` | the owner record |
| `key` | u32 | call argument `key`, `src/matched/ov/ov021_func_021B76F8.c:31` | the bag key the test uses |
| `container` | ptr | call argument `q`, `src/matched/ov/ov021_func_021B76F8.c:43-44` | the postman container |
| `slots` | array | per slot `i`, item id and count, `src/matched/ov/ov021_func_021B76F8.c:43-44` | the items tested |
| `count` | u32 | return `n`, `src/matched/ov/ov021_func_021B76F8.c:47-50` | how many cannot be received |
| `unreceivable` | array of bool | `func_02007E3C(ctx,a,b,key) == 0`, `src/matched/ov/ov021_func_021B76F8.c:46` | which items failed |

### Field item use

#### `item.use.field` (0x021BC350, ov091, `PLIST_MENU_StartItemUse`)

The party list starts an item use. It is a block candidate
(`docs/mod-events.md`, section 2). The struct is `Obj00009`
(`src/matched/include/Obj00009.h`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `menu` | ptr | call argument `p`, the party list struct, `src/matched/ov/ov091_func_021BC350.c:61` | the screen state |
| `item_id` | u16 | `*(u16 *)(W(0x280) + 0x44)`, `f280 + 0x44`, `src/matched/ov/ov091_func_021BC350.c:63` | the item used |
| `item_use_kind` | u32 | `PLIST_MENU_GetItemUseKind(item_id)`, `src/matched/ov/ov091_func_021BC350.c:63`, `src/matched/ov/ov091_func_021BBE74.c:23` | what the item does; the branch selector |
| `target_mon` | ptr | `W(0x38)`, `Obj00009.f38`, `src/matched/ov/ov091_func_021BC350.c:77` | the Pokemon used on; the subject |
| `target_row` | u32 | `W(0x2c)`, `Obj00009.f2C`, `src/matched/ov/ov091_func_021BC350.c:70` | the party row of the target |
| `party` | ptr | `T(0)`, the list at `f280`, read in the caller `src/matched/ov/ov091_func_021B7698.c:151` | the owner's party |
| `move_slot` | u16 | call argument `v`, `src/matched/ov/ov091_func_021BC350.c:115` | the move slot for a PP item |
| `hp_before` | u16 | `PLIST_PLATE_GetHp(p, mon)` into `H(0x11c)`, `src/matched/ov/ov091_func_021BC350.c:72`, `:101` | the HP before a heal |
| `level_before` | u32 | `POKE_TOOL_CalcLevel(W(0x38))`, `src/matched/ov/ov091_func_021BC350.c:77` | the level before a rare candy |
| `stats_before` | u16[6] | `POKE_TOOL_GetPartyValue(mon, gPlistStatParamIds[i], 0)`, `src/matched/ov/ov091_func_021BC350.c:87-90` | the six stats before an EV item |
| `message_id` | u32 | the id passed to `POKELIST_ShowMessage`, `src/matched/ov/ov091_func_021BC350.c:81` and following | which result message is shown |
| `owner` | ptr | the local player's party, `GAME_DATA_GetMyParty` | attributes the use in a soullink game |

#### `item.use.done` (0x021BC2C8, ov091, `PLIST_MENU_ShowItemUsedThenClose`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `menu` | ptr | call argument `p`, `src/matched/ov/ov091_func_021BC2C8.c:30` | the screen state |
| `item_id` | u16 | `*(u16 *)(f280 + 0x44)`, `src/matched/ov/ov091_func_021BC2C8.c:34` | the item that was used |
| `word_set` | ptr | `Obj00009.f150`, `src/matched/ov/ov091_func_021BC2C8.c:33` | the message word set |
| `message_id` | u32 | `0x51`, `src/matched/ov/ov091_func_021BC2C8.c:35` | the message shown |
| `closes` | bool | the callback `POKELIST_EndMessageToExit`, `src/matched/ov/ov091_func_021BC2C8.c:35` | that the list closes after the use |

#### `item.use.continue` (0x021BC30C, ov091, `PLIST_MENU_ShowItemUsedThenContinue`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `menu` | ptr | call argument `p`, `src/matched/ov/ov091_func_021BC30C.c:30` | the screen state |
| `item_id` | u16 | `*(u16 *)(f280 + 0x44)`, `src/matched/ov/ov091_func_021BC30C.c:34` | the item that was used |
| `word_set` | ptr | `Obj00009.f150`, `src/matched/ov/ov091_func_021BC30C.c:33` | the message word set |
| `message_id` | u32 | `0x51`, `src/matched/ov/ov091_func_021BC30C.c:35` | the message shown |
| `continues` | bool | the callback `POKELIST_EndMessageToList`, `src/matched/ov/ov091_func_021BC30C.c:35` | that the list stays open |

#### `item.use.calc` (0x0201B7B4, arm9, `NATSUKI_CalcUseItem`)

A state accessor that computes an item use. The three effect bytes come from the
item record and are handed to `NATSUKI_Apply`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mon` | ptr | call argument `a0`, `src/matched/arm9/func_0201B7B4.c:34` | the Pokemon the use applies to |
| `item_id` | u32 | call argument `a1`, used as the record id, `src/matched/arm9/func_0201B7B4.c:34`, `:40` | the item used |
| `effect` | u8[3] | `FILE_UTIL_Get_0FE4(item, i + 0x3C, heap)` for `i` 0..2, `src/matched/arm9/func_0201B7B4.c:39-40` | the three effect selectors, 0x3C, 0x3D, 0x3E |
| `arg` | u16 | call argument `a2`, `src/matched/arm9/func_0201B7B4.c:42` | the caller's extra argument |
| `heap_id` | u32 | call argument `a3`, `src/matched/arm9/func_0201B7B4.c:40` | the record allocation |
| `apply` | ptr | `NATSUKI_Apply`, `src/matched/arm9/func_0201B7B4.c:42` | the function that commits the use |

### Healing

#### `item.heal.party` (0x0201BA34, arm9, `POKE_TOOL_HealParty`)

The shared healer, reached by `SCRCMD_PROC_HealParty` and by item use
(`docs/mod-events.md`, section 1a). It heals every occupied party slot.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `party` | ptr | call argument `party`, `src/matched/arm9/unit_0201B9F0.c:89` | the party healed; the subject and the owner |
| `party_count` | u32 | `POKEPARTY_GetCount(party)`, `src/matched/arm9/unit_0201B9F0.c:95` | how many slots are walked |
| `members` | array | `POKEPARTY_GetMember(party, i)` per slot, `src/matched/arm9/unit_0201B9F0.c:97` | the Pokemon healed |
| `occupied` | array of bool | `POKE_TOOL_GetPartyValue(mon, 0xA9, 0) != 0`, `src/matched/arm9/unit_0201B9F0.c:98` | which slots are healed |
| `hp_restored` | array | `POKE_TOOL_SetPartyValue(mon, 0xA0, GetPartyValue(mon, 0xA1))`, `src/matched/arm9/unit_0201B9F0.c:80` | that HP is set to max |
| `status_cleared` | array of bool | `POKE_TOOL_SetPartyValue(mon, 0x9D, 0)`, `src/matched/arm9/unit_0201B9F0.c:81` | that status is cleared |
| `pp_restored` | array | `POKE_TOOL_RestorePP(mon, i, 0x7F)` per slot, `src/matched/arm9/unit_0201B9F0.c:84` | that each move slot's PP is filled |
| `cause` | u32 | context: which caller, `SCRCMD_PROC_HealParty` or item use | a centre heal differs from an item heal |

The party field ids 0xA0, 0xA1, 0xA9 and 0x9D are not named by aletheia as HP
or status (`docs/mod-events.md`, section 4). The port can state the ids but not
"HP" until that mapping is fixed.

#### `heal.party` (0x0215B5A8, ov010, `SCRCMD_PROC_HealParty`)

The field-side trigger. It is one of the six block events
(`docs/mod-events.md`, section 2).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(a1)`, `src/matched/ov/func_0215B5A8.c:33` | the owner record |
| `party` | ptr | `GAME_DATA_GetMyParty(v0)`, `src/matched/ov/func_0215B5A8.c:35` | the party healed |
| `command_context` | ptr | call argument `a1`, `src/matched/ov/func_0215B5A8.c:27` | the script command that asked |
| `block` | bool | the return contract, `docs/mod-events.md`, section 2 | a block forbids the heal |
| `heal` | ptr | `POKE_TOOL_HealParty`, `src/matched/ov/func_0215B5A8.c:37` | the tool the payload describes |

### Battle item and bag

The six battle item and bag events (`battle.item.use`, `battle.item.heal`,
`battle.item.cure`, `battle.item.check`, `battle.bag.add`, `battle.bag.sub`)
are specified in section 9, with the fields both this area and the battle area
carry. They are not repeated here.

### Shops

#### `shop.start` (0x021B2FD8, ov021, `SCRCMD_SHOP_StartShop`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `shop_id` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B2FD8.c:95` | which shop is opened |
| `shop_kind` | u8 | `w->b2`, set from `v`, `src/matched/ov/ov021_func_021B2FD8.c:112-133` | the shop category a handler branches on |
| `shop_sub` | u8 | `w->b3`, set from `v`, `src/matched/ov/ov021_func_021B2FD8.c:131` | the second category byte |
| `work` | ptr | `func_02030734(4, 0x214, 1, ...)`, `src/matched/ov/ov021_func_021B2FD8.c:103` | the shop task block, the context for `shop.pay` |
| `bag` | ptr | `GAME_DATA_GetMyItem(s)`, `src/matched/ov/ov021_func_021B2FD8.c:108` | the buyer's bag |
| `misc_save` | ptr | `GAME_DATA_GetMiscSave(s)`, `src/matched/ov/ov021_func_021B2FD8.c:106` | the money block |
| `trainer_record` | ptr | `GAME_DATA_GetTrainerRecord(s)`, `src/matched/ov/ov021_func_021B2FD8.c:110` | the buyer's record |
| `save_block_3a` | ptr | `func_020071CC(save_control, 0x3a)`, `src/matched/ov/ov021_func_021B2FD8.c:109` | the shop's save block |
| `msg_bg` | ptr | `FIELDMAP_GetMsgBg(field_map)`, `src/matched/ov/ov021_func_021B2FD8.c:101` | the message background |
| `union_info` | ptr | `GAME_DATA_GetSecondUnionInfo(s)`, `src/matched/ov/ov021_func_021B2FD8.c:102` | the union room data |
| `field_map` | ptr | `GAMESYSTEM_GetFieldMap(t)`, `src/matched/ov/ov021_func_021B2FD8.c:99` | the map the shop is on |
| `task` | ptr | `SCRCMD_PROC_FLD_StepShopTask`, `src/matched/ov/ov021_func_021B2FD8.c:134` | the step function that later frees the block |
| `facility_enter` | bool | `GAME_BEACON_SEARCH_SetFacilityEnter()` when `v` is not 0xFE or 0xFD, `src/matched/ov/ov021_func_021B2FD8.c:136` | that a facility entry was recorded |
| `owner` | ptr | the local player, `GAME_DATA_GetMyItem` | attributes the purchase |

#### `shop.pay` (0x021B3568, ov021, `SCRCMD_PROC_FLD_PayPrice`)

It is a block candidate (`docs/mod-events.md`, section 2). The struct is
`Obj05049` (`src/matched/include/Obj05049.h`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `work` | ptr | call argument `w`, the shop block, `src/matched/ov/ov021_func_021B3568.c:32` | the shop paying |
| `price` | u32 | call argument `b`, `src/matched/ov/ov021_func_021B3568.c:32`, `:35` | the amount paid |
| `pays_money` | bool | `w[0x163] == 0`, `src/matched/ov/ov021_func_021B3568.c:34` | whether money or score is spent |
| `misc_save` | ptr | `w->fC`, `src/matched/ov/ov021_func_021B3568.c:35` | the money block |
| `score_record` | ptr | `w->f18`, `src/matched/ov/ov021_func_021B3568.c:38` | the profile score block |
| `stat_record` | ptr | `w->f14`, `src/matched/ov/ov021_func_021B3568.c:36` | the record the spend is counted in |
| `money_before` | u32 | a call the port can make, `BEACON_GetPoints(misc_save)` 0x0200C274 | the money before the pay |
| `money_after` | u32 | `money_before` minus `price` | the money after the pay |
| `score_subtracted` | u32 | `GDS_PROFILE_SubScoreRecTotal(score_record, b)`, `src/matched/ov/ov021_func_021B3568.c:38` | the score spent |
| `counter` | u32 | `func_02008DAC(stat_record, 0x16 or 0x22, b)`, `src/matched/ov/ov021_func_021B3568.c:36`, `:39` | the purchase counter |
| `commit` | u32 | `func_02008DF0(stat_record, 0x15)`, `src/matched/ov/ov021_func_021B3568.c:41` | the commit step |
| `block` | bool | the return contract, `docs/mod-events.md`, section 2 | a block forbids the pay |
| `owner` | ptr | the local player, via the shop context | attributes the purchase |

### Money

The money is the misc save block 0x34, offset +0x00
(`docs/mod-events.md`, section 4).

#### `money.set` (0x0215C558, ov010, `SCRCMD_PROC_SetMiscValue`)

The name says set, the code adds. `BEACON_AddPoints` is called.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `amount` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov010_func_0215C558.c:32` | the value added |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(b)`, `src/matched/ov/ov010_func_0215C558.c:29` | the owner record |
| `misc_save` | ptr | `GAME_DATA_GetMiscSave(p)`, `src/matched/ov/ov010_func_0215C558.c:33` | the money block |
| `player_work` | ptr | `GAME_DATA_GetMyPlayerWork(p)`, `src/matched/ov/ov010_func_0215C558.c:31` | the player record |
| `operation` | u32 | `BEACON_AddPoints(misc_save, k)`, `src/matched/ov/ov010_func_0215C558.c:33` | that the value is added |
| `money_before` | u32 | a call the port can make, `BEACON_GetPoints` 0x0200C274 | the money before |
| `money_after` | u32 | `money_before` plus `amount`, capped | the money after |
| `cause` | u32 | the command context `a` | which script changed the money |

#### `money.add` (0x0215C584, ov010, `SCRCMD_PROC_AddMiscValue`)

The name says add, the code subtracts. `BEACON_SubPoints` is called.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `amount` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov010_func_0215C584.c:32` | the value subtracted |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(b)`, `src/matched/ov/ov010_func_0215C584.c:29` | the owner record |
| `misc_save` | ptr | `GAME_DATA_GetMiscSave(p)`, `src/matched/ov/ov010_func_0215C584.c:33` | the money block |
| `player_work` | ptr | `GAME_DATA_GetMyPlayerWork(p)`, `src/matched/ov/ov010_func_0215C584.c:31` | the player record |
| `operation` | u32 | `BEACON_SubPoints(misc_save, k)`, `src/matched/ov/ov010_func_0215C584.c:33` | that the value is subtracted |
| `money_before` | u32 | a call the port can make, `BEACON_GetPoints` 0x0200C274 | the money before |
| `money_after` | u32 | `money_before` minus `amount` | the money after |
| `cause` | u32 | the command context `a` | which script changed the money |

#### `money.check` (0x0215C5B0, ov010, `SCRCMD_PROC_CheckMiscValueReached`)

A state read.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `threshold` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov010_func_0215C5B0.c:32` | the value tested against |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(b)`, `src/matched/ov/ov010_func_0215C5B0.c:33` | the owner record |
| `misc_save` | ptr | `GAME_DATA_GetMiscSave(q)`, `src/matched/ov/ov010_func_0215C5B0.c:35` | the money block |
| `money` | u32 | `BEACON_GetPoints(misc_save)`, `src/matched/ov/ov010_func_0215C5B0.c:35` | the current money |
| `reached` | bool | `v <= points`, `src/matched/ov/ov010_func_0215C5B0.c:35` | the answer a handler branches on |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov010_func_0215C5B0.c:31` | where the script reads the answer |

#### `money.print` (0x021BAFC4, ov070, `ITEMMENU_PrintMoney`)

An override. It is the bag's money row. It prints the money read from the misc
block.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `menu` | ptr | call argument `p`, `src/matched/ov/ov070_func_021BAFC4.c:106` | the bag menu the money is printed on |
| `misc_save` | ptr | `GAME_DATA_GetMiscSave(P(0))`, `src/matched/ov/ov070_func_021BAFC4.c:115` | the misc block that holds the money |
| `money` | u32 | `BEACON_GetPoints(misc_save)`, `src/matched/ov/ov070_func_021BAFC4.c:115` | the money the row prints |
| `word_set` | ptr | `P(0x520)`, `src/matched/ov/ov070_func_021BAFC4.c:115-116` | the word set the number is formatted with |
| `msg_bank` | ptr | `P(0x51C)`, `src/matched/ov/ov070_func_021BAFC4.c:111,114` | the bank the label and number strings come from |
| `text_buf` | ptr | `P(0x524)`, `src/matched/ov/ov070_func_021BAFC4.c:111,114,116` | the string buffer the labels expand into |
| `window` | ptr | `p + 0x764`, `src/matched/ov/ov070_func_021BAFC4.c:112,117` | the window the money draws to |
| `digits` | u32 | the literal `7` passed to `WORDSET_SetSlotNumber`, `src/matched/ov/ov070_func_021BAFC4.c:115` | the digit count the row pads to |

### Move events

#### `move.learn` (0x021C360C, ov021, `SODATEYA_LearnMove`)

The learn used by a TM and by a tutor. It writes a free slot or pushes.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mon` | ptr | call argument `a`, `src/matched/ov/ov021_func_021C360C.c:32` | the Pokemon that learns |
| `move_id` | u32 | call argument `b`, `src/matched/ov/ov021_func_021C360C.c:32` | the move learned |
| `knows_move` | bool | `SODATEYA_MonKnowsMove(a,b)`, `src/matched/ov/ov021_func_021C360C.c:37` | whether the learn is skipped |
| `free_slot_ids` | u32[4] | `d_021DA8C8`, `src/matched/ov/ov021_func_021C360C.c:34` | the four slot field ids tested |
| `slot` | u32 | the `i` written by `POKE_TOOL_SetMoveSlot(a, b, i)`, `src/matched/ov/ov021_func_021C360C.c:40` | which slot the move lands in |
| `pushed` | bool | `POKE_TOOL_PushMove(a, b)` when no slot is free, `src/matched/ov/ov021_func_021C360C.c:44` | that the move went through the push path |
| `moves_before` | u16[4] | a call the port can make, `POKE_TOOL_GetPartyValue(mon, 0x36+i, 0)` 0x02017E1C | the four moves before the learn |
| `cause` | u32 | context: the tutor flow or the TM flow | which flow taught it |
| `owner` | ptr | the mon's owner; the party in the field | attributes the learn |

#### `move.push` (0x020180F4, arm9, `POKE_TOOL_PushMove`)

An override point. It is a tail thunk that drops both arguments at the callee,
so the port must capture them at the call.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mon` | ptr | call argument `p`, `src/matched/arm9/func_020180F4.c:30` | the Pokemon pushed into |
| `move_id` | int | call argument `move`, `src/matched/arm9/func_020180F4.c:30` | the move pushed |
| `target` | ptr | `POKE_TOOL_PushBoxMove` 0x020180FC, `src/matched/arm9/func_020180F4.c:28-32` | the callee that sees nothing of the arguments |
| `method` | u32 | context: the caller is `SODATEYA_LearnMove` or `SCRCMD_PROC_SetMonMove` | why the move was pushed |
| `owner` | ptr | the mon's owner | attributes the push |

#### `move.slot` (0x020181F4, arm9, `POKE_TOOL_SetMoveSlot`)

An override point. It is a tail thunk to `POKE_TOOL_SetBoxMoveSlot`
0x020181FC, which writes the move, clears the PP-up and sets the fresh PP.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mon` | ptr | call argument `p`, `src/matched/arm9/unit_020181A0.c:77` | the Pokemon written |
| `move_id` | u16 | call argument `move`, `src/matched/arm9/unit_020181A0.c:77` | the move written |
| `slot` | u32 | call argument `slot`, `src/matched/arm9/unit_020181A0.c:77` | the slot written |
| `move_field` | u32 | `0x36 + slot`, `src/matched/arm9/unit_020181A0.c:86` | the move id field |
| `pp_up_cleared` | bool | `POKE_TOOL_SetBoxValue(p, 0x3E+slot, 0)`, `src/matched/arm9/unit_020181A0.c:87` | that the PP-up count is reset |
| `pp_up_field` | u32 | `0x3E + slot`, `src/matched/arm9/unit_020181A0.c:87` | the PP-up field |
| `pp_set` | u32 | `WAZA_TOOL_GetMaxPP(move, 0)` or 0, `src/matched/arm9/unit_020181A0.c:88-91` | the new PP |
| `pp_field` | u32 | `0x3A + slot`, `src/matched/arm9/unit_020181A0.c:91` | the PP field |
| `move_before` | u16 | a call the port can make, `POKE_TOOL_GetPartyValue(mon, 0x36+slot, 0)` | the move replaced |
| `owner` | ptr | the mon's owner | attributes the write |

#### `move.set` (0x0215BD2C, ov010, `SCRCMD_PROC_SetMonMove`)

A script command that sets, pushes or deletes a move. A move slot past 4 pushes;
a zero move deletes and shifts the later moves down.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `party_slot` | u16 | first `SCRIPT_WORK_ReadWorkValue(c,vm)`, `src/matched/ov/ov010_func_0215BD2C.c:54` | which party member |
| `move_slot` | u16 | second `SCRIPT_WORK_ReadWorkValue(c,vm)`, `src/matched/ov/ov010_func_0215BD2C.c:55` | which move slot |
| `move_id` | u16 | third `SCRIPT_WORK_ReadWorkValue(c,vm)`, `src/matched/ov/ov010_func_0215BD2C.c:56` | the move written, 0 to delete |
| `mon` | ptr | `SCRCMD_PROC_GetPartyMember(vm, slot, &mon)`, `src/matched/ov/ov010_func_0215BD2C.c:57` | the Pokemon written |
| `method` | u32 | the branch, `moveSlot > 4` push, `move == 0` delete, else set, `src/matched/ov/ov010_func_0215BD2C.c:61-82` | which action was taken |
| `shifted_moves` | u16[3] | `POKE_TOOL_GetPartyValue(mon, i+0x37, 0)` loop, `src/matched/ov/ov010_func_0215BD2C.c:67-80` | the moves shifted down on a delete |
| `param_before` | u32 | `POKE_TOOL_DecodePartyParam(mon)`, `src/matched/ov/ov010_func_0215BD2C.c:60` | the record state before the write |
| `param_after` | u32 | `POKE_TOOL_EncodePartyParam(mon, saved)`, `src/matched/ov/ov010_func_0215BD2C.c:84` | the record state after the write |
| `found` | bool | `SCRCMD_PROC_GetPartyMember(...) == 0` returns early, `src/matched/ov/ov010_func_0215BD2C.c:57` | whether the slot resolved |
| `vm` | handle | call argument `c` | the script frame, the cause |
| `cause` | u32 | the command context `c` | which script set it |

#### `move.get` (0x0215BCE8, ov010, `SCRCMD_PROC_GetMonMove`)

A state read.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `party_slot` | u32 | first `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `src/matched/ov/ov010_func_0215BCE8.c:36` | which party member |
| `move_slot` | u32 | second `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `src/matched/ov/ov010_func_0215BCE8.c:37` | which move slot |
| `mon` | ptr | `SCRCMD_PROC_GetPartyMember(a1, k, &v)`, `src/matched/ov/ov010_func_0215BCE8.c:38` | the Pokemon read |
| `move_id` | u16 | `POKE_TOOL_GetPartyValue(v, p + 0x36, 0)`, `src/matched/ov/ov010_func_0215BCE8.c:39` | the move at the slot |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a0,a1)`, `src/matched/ov/ov010_func_0215BCE8.c:35` | where the script reads the move |
| `found` | bool | `SCRCMD_PROC_GetPartyMember(...) == 1`, `src/matched/ov/ov010_func_0215BCE8.c:38` | whether the slot resolved; 0 when not |
| `vm` | handle | call argument `a0` | the script frame, the cause |

#### `move.relearn.list` (0x0216F948, ov010, `WAZA_OSHIE_CreateMoveList`)

Builds the list of level-up moves a mon could relearn.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mon` | ptr | call argument `mon`, `src/matched/ov/ov010_func_0216F948.c:70` | the Pokemon the list is for |
| `species` | u16 | `POKE_TOOL_GetPartyValue(mon, 5, 0)`, `src/matched/ov/ov010_func_0216F948.c:84` | the species the learnset comes from |
| `form` | u8 | `POKE_TOOL_GetPartyValue(mon, 0x6F, 0)`, `src/matched/ov/ov010_func_0216F948.c:85` | the form the learnset comes from |
| `level` | u8 | `POKE_TOOL_GetPartyValue(mon, 0x9E, 0)`, `src/matched/ov/ov010_func_0216F948.c:86` | the level that gates the list |
| `current_moves` | u16[4] | `POKE_TOOL_GetPartyValue(mon, 0x36+i, 0)`, `src/matched/ov/ov010_func_0216F948.c:87-88` | the moves already known |
| `moves` | u16[26] | the `out` buffer, `src/matched/ov/ov010_func_0216F948.c:90`, `:111` | the relearnable moves |
| `count` | u32 | entries before the 0xFFFF terminator, `src/matched/ov/ov010_func_0216F948.c:92-113` | how many moves are offered |
| `heap_id` | u32 | call argument `heap`, `src/matched/ov/ov010_func_0216F948.c:89` | the list allocation |
| `owner` | ptr | the mon's owner | attributes the list |

#### `move.relearn` (0x0215C89C, ov010, `SCRCMD_PROC_CallMoveRelearn`)

It opens the relearn screen. The move write happens later in ov173, not at this
event.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(c,vm)`, `src/matched/ov/ov010_func_0215C89C.c:98` | where the script reads the result |
| `party_index` | u32 | `SCRIPT_WORK_ReadWorkValue(c,vm)`, `src/matched/ov/ov010_func_0215C89C.c:99` | which party member |
| `mon` | ptr | `POKEPARTY_GetMember(party, idx)`, `src/matched/ov/ov010_func_0215C89C.c:100` | the Pokemon the screen is for |
| `session` | ptr | `GAME_DATA_GetMyStatus(w)`, `src/matched/ov/ov010_func_0215C89C.c:101` | the status record stored in the record |
| `moves` | ptr | `WAZA_OSHIE_CreateMoveList(mon, 4)`, `src/matched/ov/ov010_func_0215C89C.c:102` | the relearnable list |
| `record` | ptr | `WAZA_OSHIE_Create(4)` with mon, sess, sys, moves, f19, `src/matched/ov/ov010_func_0215C89C.c:103-104` | the screen record |
| `callback` | ptr | `SCRCMD_PROC_OnMoveRelearnDone`, `src/matched/ov/ov010_func_0215C89C.c:108` | the done callback |
| `write_gap` | bool | no event fires at the ov173 write, `src/matched/ov/ov010_func_0215C89C.c:108` | that the payload cannot name the move written |
| `vm` | handle | call argument `vm` | the script frame, the cause |
| `script_work` | handle | `SCRCMD_WORK_GetScriptWork(vm)`, `src/matched/ov/ov010_func_0215C89C.c:94` | the script frame |

#### `move.relearn.count` (0x0215BFD4, ov010, `SCRCMD_PROC_GetRelearnableMoveCount`)

A state read.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a0,a1)`, `src/matched/ov/func_0215BFD4.c:44` | where the script reads the count |
| `party_index` | u32 | `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `src/matched/ov/func_0215BFD4.c:48` | which party member |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(a1)`, `src/matched/ov/func_0215BFD4.c:52` | the list allocation |
| `mon` | ptr | `SCRCMD_PROC_GetPartyMember(a0, a1, &v0)`, `src/matched/ov/func_0215BFD4.c:58` | the Pokemon read |
| `count` | u32 | `WAZA_OSHIE_IsListEntryValid(buf)`, `src/matched/ov/func_0215BFD4.c:66-68` | the count a handler branches on |
| `found` | bool | `SCRCMD_PROC_GetPartyMember(...) == 1`, `src/matched/ov/func_0215BFD4.c:60` | whether the slot resolved |
| `vm` | handle | call argument `a1` | the script frame, the cause |
| `list` | pointer | `WAZA_OSHIE_CreateMoveList(mon, heap)`, `src/matched/ov/func_0215BFD4.c:64` | the relearn list |

### TM and tutor

#### `tm.tutor.select` (0x021B61E8, ov021, `SCRCMD_POKEMON_FLD_SelectTutorMon`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mode` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B61E8.c:45` | which tutor group |
| `candidate_mask` | u8 | `SCRCMD_POKEMON_FLD_FindTutorCandidates(a,b,mode,&c,0)`, `src/matched/ov/ov021_func_021B61E8.c:50`, `src/matched/ov/ov021_func_021B6048.c:36` | the party members that can be tutored |
| `candidate_result` | int | return of `FindTutorCandidates`, 0, 1 or 2, `src/matched/ov/ov021_func_021B6048.c:65-69` | none, some or all candidates |
| `tutor_value` | u32 | the switch on `mode`, 0xfffe, 0xfffd, 0x1b2 or 0xa5, `src/matched/ov/ov021_func_021B61E8.c:53-66` | the tutor's selector |
| `out_ptr1` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B61E8.c:46` | the first script output |
| `out_ptr2` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B61E8.c:47` | the second script output |
| `party` | ptr | `GAME_DATA_GetMyParty` inside `FindTutorCandidates`, `src/matched/ov/ov021_func_021B6048.c:45` | the owner's party |
| `select_event` | ptr | `EVENT_POKE_STATUS_CreatePokeSelectMoveEvent`, `src/matched/ov/ov021_func_021B61E8.c:67` | the selection event created |
| `result` | u16 | `*o1 = 0` when no candidate, `src/matched/ov/ov021_func_021B61E8.c:51` | whether a selection started |
| `vm` | handle | `SCRCMD_WORK_GetScriptWork(b)`, `src/matched/ov/ov021_func_021B61E8.c:48` | the script frame, the cause |

#### `tm.tutor.move` (0x021B6124, ov021, `SCRCMD_POKEMON_FLD_GetMonTutorMove`)

A state read.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `tutor_sel` | u32 | first `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B6124.c:37` | the tutor selector |
| `party_index` | u32 | second `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B6124.c:38` | which party member |
| `mon` | ptr | `SCRCMD_PROC_GetPartyMember(b, y, &rec)`, `src/matched/ov/ov021_func_021B6124.c:40` | the Pokemon read |
| `move_id` | u16 | `SCRCMD_POKEMON_FLD_GetTutorMoveId(rec, x)`, `src/matched/ov/ov021_func_021B6124.c:41` | the tutor move |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B6124.c:39` | where the script reads the move |
| `found` | bool | `SCRCMD_PROC_GetPartyMember(...) == 1`, `src/matched/ov/ov021_func_021B6124.c:40` | whether the slot resolved |
| `vm` | handle | call argument `b` | the script frame, the cause |

#### `tm.tutor.status` (0x021B6164, ov021, `SCRCMD_POKEMON_FLD_CheckMonTutorStatus`)

A state read.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `tutor_sel` | u32 | first `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B6164.c:44` | the tutor selector |
| `party_index` | u32 | second `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B6164.c:45` | which party member |
| `mon` | ptr | `SCRCMD_PROC_GetPartyMember(b, y, &rec)`, `src/matched/ov/ov021_func_021B6164.c:48` | the Pokemon checked |
| `tutor_move` | u32 | `SCRCMD_POKEMON_FLD_GetTutorMoveId(rec, x)`, `src/matched/ov/ov021_func_021B6164.c:52` | the move |
| `in_group` | bool | `SCRCMD_POKEMON_FLD_CheckTutorGroup(rec, x)`, `src/matched/ov/ov021_func_021B6164.c:50` | whether the mon can be tutored |
| `knows_move` | bool | `SCRCMD_PROC_CheckMonKnowsMove(rec, GetTutorMoveId(rec, x))`, `src/matched/ov/ov021_func_021B6164.c:52` | whether the mon already knows it |
| `friendship` | u32 | `POKE_TOOL_GetPartyValue(rec, 9, 0)`, `src/matched/ov/ov021_func_021B6164.c:54` | the mon's friendship |
| `friendship_required` | int | `SCRCMD_POKEMON_FLD_GetTutorFriendship(x)`, `src/matched/ov/ov021_func_021B6164.c:54`, `src/matched/ov/func_021B5FB4.c:25` | the friendship needed |
| `status` | u16 | 0 ready, 1 no group, 2 low friendship, 3 knows, `src/matched/ov/ov021_func_021B6164.c:47-58` | the answer a handler branches on |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B6164.c:46` | where the script reads the status |
| `vm` | handle | call argument `b` | the script frame, the cause |

#### `tm.tutor.id` (0x021B5F24, ov021, `SCRCMD_POKEMON_FLD_GetTutorMoveId`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mon` | ptr | call argument `a`, `src/matched/ov/ov021_func_021B5F24.c:24` | the Pokemon tested |
| `tutor_sel` | int | call argument `sel`, `src/matched/ov/ov021_func_021B5F24.c:24` | the tutor selector |
| `move_id` | u32 | 0x152, 0x133, 0x134, 0x1b2, 0x208, 0x207 or 0x206, `src/matched/ov/ov021_func_021B5F24.c:27-46` | the tutor move |
| `compat` | u32 | `POKE_TOOL_CheckTutorCompat(a, v)`, `src/matched/ov/ov021_func_021B5F24.c:28-43` | the compatibility test that picked the move |

#### `tm.type` (0x021B62C8, ov021, `SCRCMD_POKEMON_FLD_GetMonTypeForTm`)

A state read. It resolves the TM id 0x151, then the mon's compatibility and
Hidden Power type.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `party_index` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B62C8.c:43` | which party member |
| `tm_id` | u32 | `FILE_UTIL_GetId(0x151)`, `src/matched/ov/ov021_func_021B62C8.c:44` | the TM item id |
| `mon` | ptr | `SCRCMD_PROC_GetPartyMember(b, v, &x)`, `src/matched/ov/ov021_func_021B62C8.c:45` | the Pokemon read |
| `compat` | u32 | `POKE_TOOL_CheckTmCompat(x, w)`, `src/matched/ov/ov021_func_021B62C8.c:47` | whether the mon can take the TM |
| `type` | u16 | `POKE_TOOL_GetHiddenPowerType(x)`, `src/matched/ov/ov021_func_021B62C8.c:50` | the type; 0xFFFF when not compatible |
| `hidden_power_type` | u32 | `POKE_TOOL_GetHiddenPowerType(x)`, `src/matched/ov/ov021_func_021B62C8.c:50` | the type written to the result |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B62C8.c:42` | where the script reads the type |
| `vm` | handle | call argument `b` | the script frame, the cause |

### Fields a mod would want that cannot be obtained

- **An item name.** No event carries a name. The names are in the game's message
  table, not reachable at these call sites. A mod that wants a name needs a
  name table the port builds.
- **A move name.** Same as an item name; the move events carry the id only.
- **The concrete mystery gift.** `item.gift.mystery` returns a mode result and
  the gift kind, not the item or species. The content is inside the card buffer
  and the handler table `d_021D9978` (`src/matched/ov/ov021_func_021B6D98.c:25`).
  The port can copy the card, but decoding it needs the card format.
- **The HP, status and stat identity of the party field ids.** `item.heal.party`
  uses 0xA0, 0xA1, 0xA9 and 0x9D (`src/matched/arm9/unit_0201B9F0.c:80-98`).
  aletheia does not name which is HP or a stat (`docs/mod-events.md`, section
  4), so the port can state the ids but not "HP restored" as a number.
- **The shop's item list.** `shop.start` carries the shop id, not the list. The
  list is loaded later by `SCRCMD_PROC_FLD_StepShop` 0x021B35A4
  (`src/matched/ov/ov021_func_021B3158.c:110`).
- **The move the relearner writes.** `move.relearn` opens the screen; the write
  is in the ov173 proc, and no event fires there
  (`src/matched/ov/ov010_func_0215C89C.c:108`).
- **The postman record's origin.** `item.gift.postman` receives the container
  pointer, not the record that filled it.
- **The money getter.** `BEACON_GetPoints` (0x0200C274) is the read used by
  `money.check` and `shop.pay`. It has a matched source:
  `src/matched/arm9/unit_0200C0E8.c:261-264` returns `p->unk00`, the money at
  misc block 0x34, offset +0x00 (`docs/mod-events.md`, section 1c). The port
  calls the matched accessor.

### The thinnest payloads

- `item.machine` (0x021AF750): one argument, a boolean return, no VM context
  (`src/matched/ov/func_021AF750.c:26-28`).
- `item.partyuse` (0x021AF71C) and `item.pocket` (0x021AF6E8): an item id and
  one property.
- `money.check` (0x0215C5B0): a threshold and a boolean.
- `battle.item.check` (0x021CBFA0): a mon and a message id.
- `move.push` (0x020180F4) and `move.slot` (0x020181F4): tail thunks that drop
  their arguments at the callee, so the payload exists only at the call.
- `item.has` (0x021AF634) and `item.canadd` (0x021AF5E4): an item id, a count
  and a boolean.

## 15. Progression

The progression area is the 47 entries of `docs/mod-events.md`, section 1, that
add mons and eggs, learn moves, trade and select mons, register the Pokedex,
run research, set flags and misc bytes, expose the event work, and read and
write badges (25 events, 16 state entries and 6 override points). The port
tracks the progression contexts (section 0a) for the
script VM, the party and box, the event work, the misc block, the dex and the
badge date. The move, tutor and relearn events are specified in the inventory
area (section 14); this area covers the rest. Each table gives the field, its
type, where it comes from, and why a mod wants it.

### Party and box adds

#### `mon.party.add` (0x0215BB50, `SCRCMD_PROC_AddMonToPartySimple`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a0` | the script frame; the cause |
| `owner` | handle | `GAME_DATA_GetMyStatus(game_data)` is called at `ov010_func_0215BB50.c:63` | attributes the add to a player |
| `spec_species` | u16 | `SCRIPT_WORK_ReadWorkValue(a0,a1)`, first read, `ov010_func_0215BB50.c:65`; consumed as species `ov010_func_0215C40C.c:58` | which species was added |
| `spec_form` | u16 | second read, `ov010_func_0215BB50.c:66`; set to party value 0x6F `ov010_func_0215C40C.c:59` | form variant |
| `spec_level` | u16 | third read, `ov010_func_0215BB50.c:67`; `CreateEx` second arg `ov010_func_0215C40C.c:58` | level |
| `spec_held_item` | u16 | constant 0, `ov010_func_0215BB50.c:72`; set to party value 6 `ov010_func_0215C40C.c:60` | held item |
| `spec_extras` | u16[4] | constants 2,2,2,4, `ov010_func_0215BB50.c:73-76`; IV/random args `ov010_func_0215C40C.c:57` | the random-parameter seed the game will draw from |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(a1)`, `ov010_func_0215BB50.c:61,68` | allocation tag |
| `result` | u16 | written to the script destination, `ov010_func_0215BB50.c:77`; 1 on add, 0 on a full party (`ov010_func_0215C510.c:47-48`) | whether the add happened |
| `party_count_before` | u32 | `POKEPARTY_GetCount(party)`, read in `ov010_func_0215C510.c:47` | the slot the mon lands in |
| `party_capacity` | u32 | `POKEPARTY_GetCapacity(party)`, `ov010_func_0215C510.c:47` | whether the party is full |
| `added_mon` | handle | `POKEPARTY_GetMember(party, count-1)` after the call, the mon `POKEPARTY_Add` added at `ov010_func_0215C510.c:50` | the full subject after the add |

The nature, ability and IVs of the added mon are drawn inside the call by
`POKE_TOOL_CreatePersonalRnd` (`ov010_func_0215C40C.c:57`). They are not
obtainable before the call; a mod reads them from `added_mon` after.

#### `mon.party.add.detailed` (0x0215BBD0, `SCRCMD_PROC_AddMonToPartyDetailed`)

Same payload as `mon.party.add`, with these differences.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `spec_held_item` | u16 | seventh read `t7`, `ov010_func_0215BBD0.c:81,86`; Spec `f10`, set to party value 6 `ov010_func_0215C40C.c:60` | held item is given |
| `spec_extras` | u16[3] | fourth, fifth, sixth reads `t4,t5,t6`, `ov010_func_0215BBD0.c:77-79`; clamped to 2 when >= 3 at `88-95`; IV/random args `ov010_func_0215C40C.c:57` | the given random parameters |
| `spec_tail` | u16 | eighth read `t8`, `ov010_func_0215BBD0.c:81,96`; Spec `f20` `ov010_func_0215C40C.c:61` | the extra spec field the create call reads |
| `spec_species` | u16 | first read, `ov010_func_0215BBD0.c:74,83` | species |
| `spec_form` | u16 | second read, `ov010_func_0215BBD0.c:75,84` | form |
| `spec_level` | u16 | third read, `ov010_func_0215BBD0.c:76,85` | level |

#### `mon.box.add` (0x0215C180, `SCRCMD_PROC_AddMonToBoxSimple`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `c` | the cause |
| `owner` | handle | `GAME_DATA_GetMyStatus(sys)` called at `ov010_func_0215C180.c:68` | attributes the add |
| `spec_species` | u16 | first read, `ov010_func_0215C180.c:70,74`; Spec `f04` | species |
| `spec_form` | u16 | second read, `ov010_func_0215C180.c:71,75`; Spec `f08` | form |
| `spec_level` | u16 | third read, `ov010_func_0215C180.c:72,76`; Spec `f0c` | level |
| `spec_held_item` | u16 | constant 0, `ov010_func_0215C180.c:77`; Spec `f10` | held item |
| `spec_extras` | u16[4] | constants 2,2,2,4, `ov010_func_0215C180.c:78-81` | random parameters |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(vm)`, `ov010_func_0215C180.c:66,73` | allocation tag |
| `box_count` | u32 | `func_02007984(box)`, tested against 0x2D0 at `ov010_func_0215C4C8.c:46` | whether the box is full |
| `result` | u16 | written to the script destination, `ov010_func_0215C180.c:82`; 1 on add, 0 on a full box (`ov010_func_0215C4C8.c:47-48`) | whether the add happened |
| `added_mon` | handle | the mon `func_020074D8` stores at `ov010_func_0215C4C8.c:49` | the subject after the add |

#### `mon.box.add.detailed` (0x0215C200, `SCRCMD_PROC_AddMonToBoxDetailed`)

Same payload as `mon.box.add`, with these differences.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `spec_held_item` | u16 | seventh read `h`, `ov010_func_0215C200.c:85,91`; Spec `f10` | held item is given |
| `spec_extras` | u16[3] | fourth, fifth, sixth reads `e,f,g`, `ov010_func_0215C200.c:82-84`; clamped to 2 when >= 3 at `93-103` | given random parameters |
| `spec_tail` | u16 | eighth read `i`, `ov010_func_0215C200.c:86,104`; Spec `f20` | extra spec field |
| `spec_species` | u16 | first read, `ov010_func_0215C200.c:79,88` | species |
| `spec_form` | u16 | second read, `ov010_func_0215C200.c:80,89` | form |
| `spec_level` | u16 | third read, `ov010_func_0215C200.c:81,90` | level |

#### `egg.add` (0x0215C2D0, `SCRCMD_PROC_AddEggToParty`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `c` | the cause |
| `owner` | handle | `GAME_DATA_GetMyStatus(sys)`, `ov010_func_0215C2D0.c:96` | attributes the egg |
| `species` | u16 | first read, `ov010_func_0215C2D0.c:98`; `POKE_TOOL_Create` first arg `104` | which species the egg will hatch into |
| `form` | u16 | second read, `ov010_func_0215C2D0.c:99`; set to party value 0x6F `105` | form variant |
| `level` | u16 | constant 1, `ov010_func_0215C2D0.c:104` | eggs are level 1 |
| `hatch_counter` | u32 | `POKE_PERSONAL_GetParam(personal, 0x15)`, `ov010_func_0215C2D0.c:110`; set to party value 9 `112` | the steps to hatch |
| `egg_flag` | u32 | constant 1, set to party value 0x4C, `ov010_func_0215C2D0.c:113` | marks the record an egg |
| `egg_name` | handle | `func_02045EC0(gSpeciesNameBank, 0x28A)`, set to party value 0x73, `ov010_func_0215C2D0.c:114-115` | the egg's display name |
| `result` | u16 | written to the script destination, `ov010_func_0215C2D0.c:101,121`; 1 on add, 0 on a full party | whether the egg was added |
| `party_count` | u32 | `POKEPARTY_GetCount(party)`, `ov010_func_0215C2D0.c:100` | the slot |
| `party_capacity` | u32 | `POKEPARTY_GetCapacity(party)`, `ov010_func_0215C2D0.c:100` | whether the party is full |
| `added_mon` | handle | the egg `POKEPARTY_Add` adds at `ov010_func_0215C2D0.c:119` | the subject after the add |

#### `egg.hatch` (0x021B63D4, `SCRCMD_POKEMON_FLD_StartEggHatch`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | `SCRCMD_WORK_GetScriptWork(b)`, `ov021_func_021B63D4.c:53` | the cause |
| `owner` | handle | `GAME_DATA_GetMyStatus(t)`, `ov021_func_021B63D4.c:63` | attributes the hatch |
| `party_index` | u16 | written to the script destination, the loop index `i`, `ov021_func_021B63D4.c:74` | which slot hatches |
| `hatching_mon` | handle | `POKEPARTY_GetMember(party, i)`, `ov021_func_021B63D4.c:67` | the full subject |
| `is_egg` | u32 | `POKE_TOOL_GetPartyValue(mon, 0x4C, 0)`, `ov021_func_021B63D4.c:68` | the hatch test |
| `hatch_steps` | u32 | `POKE_TOOL_GetPartyValue(mon, 3, 0)`, `ov021_func_021B63D4.c:69` | the counter that reached zero |
| `hatch_remaining` | u32 | `POKE_TOOL_GetPartyValue(mon, 9, 0)`, `ov021_func_021B63D4.c:70` | the counter test |
| `party_count` | u32 | `POKEPARTY_GetCount(party)`, `ov021_func_021B63D4.c:65` | the party the search ran over |
| `hatch_task` | handle | `SYMBOL_MAP_CreatePokeEventTask(pc, t8, mon)`, `ov021_func_021B63D4.c:73` | the task the hatch runs as |
| `result` | u32 | the return, 1 when a mon is found, 0 when none, `ov021_func_021B63D4.c:75,78` | whether a hatch started |

### Eggs, daycare and shiny

#### `egg.shiny.roll` (0x021C4030, ov021, `SODATEYA_ApplyMasudaMethod`)

An override. It rerolls the offspring's second personality word for the Masuda
method: the two parents must have different languages and the offspring must
not already be shiny. It rolls up to five times.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `parent_a` | ptr | call argument `p[0]`, `src/matched/ov/ov021_func_021C4030.c:26` | the first parent record |
| `parent_b` | ptr | call argument `p[1]`, `src/matched/ov/ov021_func_021C4030.c:28` | the second parent record |
| `offspring` | ptr | call argument `q`, `src/matched/ov/ov021_func_021C4030.c:30` | the offspring record the roll edits |
| `parent_a_language` | u32 | `POKE_TOOL_GetPartyValue(p[0], 0xC, 0)`, `src/matched/ov/ov021_func_021C4030.c:26` | the first parent's language |
| `parent_b_language` | u32 | `POKE_TOOL_GetPartyValue(p[1], 0xC, 0)`, `src/matched/ov/ov021_func_021C4030.c:28` | the second parent's language |
| `different_language` | bool | the `a != b` test, `src/matched/ov/ov021_func_021C4030.c:28` | whether the Masuda roll applies |
| `personality` | u32 | `q[0xC]`, `src/matched/ov/ov021_func_021C4030.c:30` | the first personality word the shiny test reads |
| `pid` | u32 | `q[0xD]`, `src/matched/ov/ov021_func_021C4030.c:30` | the second personality word the roll rewrites |
| `already_shiny` | bool | `POKE_TOOL_CheckShiny(q[0xC], q[0xD])`, `src/matched/ov/ov021_func_021C4030.c:30` | whether the roll is skipped |
| `roll_count` | u32 | the loop `i < 5`, `src/matched/ov/ov021_func_021C4030.c:36` | how many rerolls the call makes |
| `shiny_after` | bool | `POKE_TOOL_CheckShiny(...)` in the loop, `src/matched/ov/ov021_func_021C4030.c:34` | whether a roll made the offspring shiny |

#### `egg.nature.roll` (0x021C3C78, ov021, `SODATEYA_RollNature`)

An override. It rolls the offspring nature from 0x19 values and writes it to
word 3 of the output record.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `source` | ptr | call argument `a`, `src/matched/ov/ov021_func_021C3C78.c:19` | the record the roll is made for |
| `out` | ptr | call argument `out`, `src/matched/ov/ov021_func_021C3C78.c:19` | the offspring record the nature is written to |
| `nature` | u32 | `func_02005728(0x19)`, `src/matched/ov/ov021_func_021C3C78.c:21` | the rolled nature, 0 to 0x18 |
| `slot` | u32 | the literal `3`, `src/matched/ov/ov021_func_021C3C78.c:21` | the word of the output record the nature lands in |

#### `egg.ability.roll` (0x021C3D1C, ov021, `SODATEYA_RollAbilitySlot`)

An override. It rolls which of the parent's ability slots the offspring
inherits. The slot count comes from `SODATEYA_GetAbilitySlot`; the thresholds
depend on it.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `parent` | ptr | call argument `p`, `src/matched/ov/ov021_func_021C3D1C.c:27-29` | the parent records the species, form and ability are read from |
| `offspring` | ptr | call argument `q`, `src/matched/ov/ov021_func_021C3D1C.c:37` | the offspring record the slot is written to |
| `context` | u32 | call argument `d`, `src/matched/ov/ov021_func_021C3D1C.c:25` | the argument the ability-slot lookup takes |
| `species` | u16 | `POKE_TOOL_GetPartyValue(p[1], 5, 0)`, `src/matched/ov/ov021_func_021C3D1C.c:27` | the parent's species |
| `form` | u16 | `POKE_TOOL_GetPartyValue(p[1], 0x6F, 0)`, `src/matched/ov/ov021_func_021C3D1C.c:28` | the parent's form |
| `ability` | u16 | `POKE_TOOL_GetPartyValue(p[1], 0xA, 0)`, `src/matched/ov/ov021_func_021C3D1C.c:29` | the parent's ability field |
| `slots` | u32 | `SODATEYA_GetAbilitySlot(species, form, ability, d)`, `src/matched/ov/ov021_func_021C3D1C.c:30` | how many ability slots the species has |
| `roll` | u32 | `func_02005728(100)`, `src/matched/ov/ov021_func_021C3D1C.c:31` | the roll, 0 to 99 |
| `slot` | u32 | the branch on `slots` and `roll`, written to `q[0x13]`, `src/matched/ov/ov021_func_021C3D1C.c:33-54` | the inherited ability slot |

#### `daycare.exp` (0x021C3530, ov021, `SODATEYA_GrantExp`)

An override. It grants daycare experience to a mon, clamps it to the level-100
total, recalcs the parameters and learns the moves the new level grants.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mon` | ptr | call argument `mon`, `src/matched/ov/ov021_func_021C3530.c:39` | the daycare mon the experience is granted to |
| `exp_added` | u32 | call argument `b`, `src/matched/ov/ov021_func_021C3530.c:58` | the experience the daycare offers |
| `species` | u16 | `POKE_TOOL_GetPartyValue(mon, 5, 0)`, `src/matched/ov/ov021_func_021C3530.c:54` | the species |
| `form` | u16 | `POKE_TOOL_GetPartyValue(mon, 0x6F, 0)`, `src/matched/ov/ov021_func_021C3530.c:55` | the form |
| `max_exp` | u32 | `POKE_TOOL_GetExpForLevel(species, form, 100)`, `src/matched/ov/ov021_func_021C3530.c:56` | the level-100 experience cap |
| `exp_before` | u32 | `POKE_TOOL_GetPartyValue(mon, 8, 0)`, `src/matched/ov/ov021_func_021C3530.c:58` | the experience before the grant |
| `exp_after` | u32 | `SODATEYA_AddExpSaturated(exp_before, b)` clamped to `max_exp`, `src/matched/ov/ov021_func_021C3530.c:58-61` | the experience after the grant |
| `level_before` | u32 | `POKE_TOOL_GetPartyValue(mon, 0x9E, 0)`, `src/matched/ov/ov021_func_021C3530.c:57` | the level before the grant |
| `level_after` | u32 | `POKE_TOOL_GetPartyValue(mon, 0x9E, 0)` after the recalc, `src/matched/ov/ov021_func_021C3530.c:64` | the level after the grant |
| `learned_moves` | list | `SODATEYA_LearnMove(mon, mv)` for each level in the range, `src/matched/ov/ov021_func_021C3530.c:74-75` | the moves the new level grants |

#### `daycare.egg_ready` (0x0200BCA0, arm9, `DAYCARE_GetEggReady`, state)

`int DAYCARE_GetEggReady(struct Obj11233 *p)` returns the word at `+0x1C8`
(`src/matched/arm9/func_0200BCA0.c:21`). No slot fires.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `ready` | int | the return `*(int *)&p->f1C8`, `src/matched/arm9/func_0200BCA0.c:21` | whether an egg is ready |
| `daycare` | handle | call argument `p`, `src/matched/arm9/func_0200BCA0.c:19` | the daycare object the flag is read from |

#### `daycare.steps` (0x0200BD48, arm9, `DAYCARE_AddSteps`, state)

`void DAYCARE_AddSteps(E *e, u32 add)` adds `add` to the step counter of each
active daycare entry, saturating at `0xFFFFFFFF`
(`src/matched/arm9/func_0200BD48.c:26-38`). No slot fires.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `daycare` | array | call argument `e`, the two-entry array, `src/matched/arm9/func_0200BD48.c:26` | the daycare entries the steps are added to |
| `steps_added` | u32 | call argument `add`, `src/matched/arm9/func_0200BD48.c:26` | the steps added this call |
| `active` | int per entry | `e[i].active == 1`, `src/matched/arm9/func_0200BD48.c:32` | which entries receive the steps |
| `count` | u32 per entry | `e[i].count + add`, saturated, `src/matched/arm9/func_0200BD48.c:33-36` | the step counter after the add |

#### `mon.shiny` (0x02017EAC, arm9, `POKE_TOOL_IsShiny`, state)

`u32 POKE_TOOL_IsShiny(void *p)` returns `POKE_TOOL_IsBoxShiny(p)`
(`src/matched/arm9/unit_02017E64.c:92-95`). No slot fires. Value:
`{ handle, shiny }`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `shiny` | u32 | the return of `POKE_TOOL_CheckShiny(GetBoxValue(p,7,0), GetBoxValue(p,0,0))`, `src/matched/arm9/unit_02017E64.c:99-101` | whether the mon is shiny |
| `mon` | handle | call argument `p`, `src/matched/arm9/unit_02017E64.c:92` | the party mon the test reads |

The shiny test is the exclusive-or of the two personality words; a value below
8 is shiny (`POKE_TOOL_CheckShiny`, `src/matched/arm9/unit_02017E64.c:104-112`).
The test reads the box view of the record, so `mon.shiny` and `box.mon.shiny`
are the same computation.

#### `box.mon.shiny` (0x02017EB4, arm9, `POKE_TOOL_IsBoxShiny`, state)

`u32 POKE_TOOL_IsBoxShiny(void *p)` computes the shiny test on the box view of
a record (`src/matched/arm9/unit_02017E64.c:97-102`). No slot fires. Value:
`{ handle, shiny }`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `shiny` | u32 | the return of `POKE_TOOL_CheckShiny(GetBoxValue(p,7,0), GetBoxValue(p,0,0))`, `src/matched/arm9/unit_02017E64.c:99-101` | whether the mon is shiny |
| `box_mon` | handle | call argument `p`, `src/matched/arm9/unit_02017E64.c:97` | the box mon the test reads |
| `personality` | u32 | `POKE_TOOL_GetBoxValue(p, 0, 0)`, `src/matched/arm9/unit_02017E64.c:101` | the personality word the test folds |
| `checksum_word` | u32 | `POKE_TOOL_GetBoxValue(p, 7, 0)`, `src/matched/arm9/unit_02017E64.c:99` | the second word the test folds |

### Moves and tutors

The move, tutor and relearn events (`move.learn`, `move.push`, `move.slot`,
`move.set`, `move.get`, `move.relearn.list`, `move.relearn`,
`move.relearn.count`, `tm.tutor.select`, `tm.tutor.move`, `tm.tutor.status`,
`tm.tutor.id`, `tm.type`) are specified in the inventory area, section 14, with
the fields both this area and that one carry.

### Trades and mon selection

#### `trade.start` (0x021B654C, `SCRCMD_POKEMON_FLD_StartTradeEvent`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | `SCRCMD_WORK_GetScriptWork(b)`, `ov021_func_021B654C.c:33` | the cause |
| `trade_a` | u32 | first read `t`, `ov021_func_021B654C.c:35` | the trade's first argument, cast to `u8` at the call |
| `trade_b` | u32 | second read `u`, `ov021_func_021B654C.c:36` | the trade's second argument, cast to `u8` |
| `system` | handle | `SCRCMD_WORK_GetGameSystem(b)`, `ov021_func_021B654C.c:34` | the game system |
| `trade_event` | handle | `FLD_TRADE_CreateEvent(x, (u8)t, (u8)u)`, `ov021_func_021B654C.c:37` | the trade task |
| `result` | u32 | the return 1, `ov021_func_021B654C.c:38` | the event started |

#### `trade.check` (0x021B647C, `SCRCMD_POKEMON_FLD_CheckTradeMon`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | `SCRCMD_WORK_GetScriptWork(b)`, `ov021_func_021B647C.c:64` | the cause |
| `trade_index` | u32 | first read `i2`, `ov021_func_021B647C.c:69` | which trade |
| `party_slot` | u32 | second read `idx`, `ov021_func_021B647C.c:70` | which member |
| `mon` | handle | `POKEPARTY_GetMember(GAME_DATA_GetMyParty(w), idx)`, `ov021_func_021B647C.c:71` | the subject |
| `trade_obj` | handle | `FLD_TRADE_Create(heap, i2)`, `ov021_func_021B647C.c:72` | the trade record |
| `is_egg` | u32 | `POKE_TOOL_GetPartyValue(mon, 0x4C, 0)`, `ov021_func_021B647C.c:74` | why the check fails with 1 |
| `wanted_species` | u32 | `FLD_TRADE_GetWantedSpecies(obj)`, `ov021_func_021B647C.c:79` | the species the trade wants |
| `mon_species` | u32 | `POKE_TOOL_GetPartyValue(mon, 5, 0)`, `ov021_func_021B647C.c:78` | why the check fails with 2 |
| `wanted_gender` | u32 | `FLD_TRADE_GetWantedGender(obj)`, `ov021_func_021B647C.c:83` | the gender the trade wants |
| `mon_gender` | u32 | `POKE_TOOL_GetPartyValue(mon, 0x6E, 0)`, `ov021_func_021B647C.c:84` | why the check fails with 3 |
| `status` | u16 | written to `*out`: 0 ok, 1 egg, 2 species, 3 gender, `ov021_func_021B647C.c:73-88` | the trade decision |

#### `mon.select` (0x021B631C, `SCRCMD_POKEMON_FLD_StartSelectEvent`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | `SCRCMD_WORK_GetScriptWork(b)`, `ov021_func_021B631C.c:40` | the cause |
| `out_ptr` | pointer | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `ov021_func_021B631C.c:42` | the selection result |
| `mode` | u32 | first `SCRIPT_WORK_ReadWorkValue`, `ov021_func_021B631C.c:43` | the selection mode |
| `arg` | u32 | second read, `ov021_func_021B631C.c:44` | the selection argument |
| `select_event` | handle | `SCRCMD_CreateNicknameEvent(y, (int)p, q, arg)`, `ov021_func_021B631C.c:44` | the selection task |
| `result` | u32 | the return 1, `ov021_func_021B631C.c:45` | the event started |

#### `mon.select.party` (0x021B5B4C, `SCRCMD_POKEMON_FLD_SelectPartyMon`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | `SCRCMD_WORK_GetScriptWork(a1)`, `ov021_func_021B5B4C.c:39` | the cause |
| `out_a` | pointer | first `SCRIPT_WORK_ReadWorkPointer(a0,a1)`, `ov021_func_021B5B4C.c:34` | the first result word |
| `out_b` | pointer | second pointer read, `ov021_func_021B5B4C.c:35` | the second result word |
| `filter` | u32 | `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `ov021_func_021B5B4C.c:38` | the selection filter |
| `select_event` | handle | `EVENT_POKE_STATUS_CreatePokeSelectEvent(system, a, b)`, `ov021_func_021B5B4C.c:40` | the selection task |
| `result` | u32 | the return 1, `ov021_func_021B5B4C.c:41` | the event started |

### Pokedex

#### `zukan.seen.register` (0x0215AEE8, `SCRCMD_WORK_CmdRegisterZukanSeen`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a0` | the cause |
| `dex` | handle | `ZUKAN_SAVE_GetFromGameData(game_data)`, `ov010_func_0215AEE8.c:42` | the dex block |
| `mode` | int | first read `m`, `ov010_func_0215AEE8.c:44` | the switch: 0 registers, 1 does nothing |
| `species` | int | second read `o`, `ov010_func_0215AEE8.c:45`; `POKE_TOOL_Create(o,1,...)` `46` | the species seen |
| `level` | u32 | constant 1, `ov010_func_0215AEE8.c:46` | the level of the seen record |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(a1)`, `ov010_func_0215AEE8.c:43` | allocation tag |
| `registered` | boolean | whether `mode == 0`, `ov010_func_0215AEE8.c:47-50` | whether the seen flag was written |

The dex record is created inside the call, so the gender and form flags are set
from the record by `ZUKAN_SAVE_RegisterSeen` (`arm9/unit_0200CFA0.c:75-91`).

#### `zukan.seen.count` (0x0215AF40, `SCRCMD_WORK_CmdGetZukanSeenCount`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a1` | the cause |
| `dex` | handle | `ZUKAN_SAVE_GetFromGameData(game_data)`, `ov010_func_0215AF40.c:37` | the dex block |
| `kind` | int | first read `k`, `ov010_func_0215AF40.c:38` | 0 asks seen, 1 asks caught |
| `species` | int | second read `x`, `ov010_func_0215AF40.c:39` | the species asked about |
| `result` | u16 | written to `*out`, `ov010_func_0215AF40.c:43,46,49` | the answer |

#### `zukan.flag.a` (0x0215AFE0, `SCRCMD_WORK_CmdSetZukanFlagA`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a0` | the cause |
| `dex` | handle | `ZUKAN_SAVE_GetFromGameData(game_data)`, `func_0215AFE0.c:35` | the dex block |
| `national_dex_before` | u32 | `ZUKAN_SAVE_IsNationalDexEnabled(dex)` before the write (0x0200CA38) | the prior flag |
| `national_dex_after` | u32 | `ZUKAN_SAVE_EnableNationalDex(dex)`, `func_0215AFE0.c:37` | the new flag |

The event carries no argument: it enables the national dex unconditionally. The
port can read the flag before and after by calling the accessor.

#### `zukan.flag.b` (0x0215B014, `SCRCMD_WORK_CmdSetZukanFlagB`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a0` | the cause |
| `dex` | handle | `ZUKAN_SAVE_GetFromGameData(game_data)`, `func_0215B014.c:35` | the dex block |
| `obtained_before` | u32 | the obtained flag before the write | the prior flag |
| `obtained_after` | u32 | `ZUKAN_SAVE_SetDexObtained(dex)`, `func_0215B014.c:37` | the new flag |

#### `zukan.caught` (0x0200CDE0, `ZUKAN_SAVE_RegisterCaught`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `dex` | handle | call arg `p` | the dex block |
| `mon` | handle | call arg `m` | the subject |
| `species` | u16 | `POKE_TOOL_GetPartyValue(m, 5, 0)`, `arm9/unit_0200CDE0.c:96` | which entry is marked |
| `personality` | u32 | `POKE_TOOL_GetPartyValue(m, 0, 0)`, `arm9/unit_0200CDE0.c:97` | the record identity |
| `form` | u32 | `POKE_TOOL_GetPartyValue(m, 0x6F, 0)`, `arm9/unit_0200CDE0.c:98` | form flag |
| `language` | u32 | `POKE_TOOL_GetPartyValue(m, 0xC, 0)`, `arm9/unit_0200CDE0.c:99` | language flag |
| `gender` | u32 | `POKE_TOOL_GetGender(m)`, `arm9/unit_0200CDE0.c:100` | gender flag |
| `shiny` | u32 | `POKE_TOOL_IsShiny(m)`, `arm9/unit_0200CDE0.c:101` | shiny flag |
| `is_egg` | u32 | `POKE_TOOL_GetPartyValue(m, 0x4C, 0)`, `arm9/unit_0200CDE0.c:92` | eggs are skipped |
| `invalid_species` | u32 | `ZUKAN_SAVE_IsInvalidSpecies(id)`, `arm9/unit_0200CDE0.c:103` | whether the write is skipped |
| `was_seen` | u32 | `ZUKAN_SAVE_IsSeen(p, id)`, `arm9/unit_0200CDE0.c:106` | whether the view flags update |
| `caught_bit_index` | s32 | `id - 1`, `arm9/unit_0200CDE0.c:115` | the bit set at `+0x08` |
| `first_seen_value` | u32 | `p->unk4D0` when `id == 0x147`, `arm9/unit_0200CDE0.c:109-110` | the special first-seen value |

#### `zukan.seen` (0x0200CFA0, `ZUKAN_SAVE_RegisterSeen`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `dex` | handle | call arg `p` | the dex block |
| `mon` | handle | call arg `m` | the subject |
| `species` | u16 | `POKE_TOOL_GetPartyValue(m, 5, 0)`, `arm9/unit_0200CFA0.c:75` | which entry is marked |
| `personality` | u32 | `POKE_TOOL_GetPartyValue(m, 0, 0)`, `arm9/unit_0200CFA0.c:76` | the record identity |
| `form` | u32 | `POKE_TOOL_GetPartyValue(m, 0x6F, 0)`, `arm9/unit_0200CFA0.c:77` | form flag |
| `gender` | u32 | `POKE_TOOL_GetGender(m)`, `arm9/unit_0200CFA0.c:78` | gender flag |
| `shiny` | u32 | `POKE_TOOL_IsShiny(m)`, `arm9/unit_0200CFA0.c:79` | shiny flag |
| `is_egg` | u32 | `POKE_TOOL_GetPartyValue(m, 0x4C, 0)`, `arm9/unit_0200CFA0.c:71` | eggs are skipped |
| `invalid_species` | u32 | `ZUKAN_SAVE_IsInvalidSpecies(id)`, `arm9/unit_0200CFA0.c:81` | whether the write is skipped |
| `was_seen` | u32 | `ZUKAN_SAVE_IsSeen(p, id)`, `arm9/unit_0200CFA0.c:84` | whether the view flags update |
| `first_seen_value` | u32 | `p->unk4D0` when `id == 0x147`, `arm9/unit_0200CFA0.c:87-88` | the special first-seen value |

#### `zukan.is.caught` (0x0200CED4, `ZUKAN_SAVE_IsCaught`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `dex` | handle | call arg `p` | the dex block |
| `species` | u16 | call arg `id`, `arm9/unit_0200CED4.c:51` | the entry asked about |
| `caught` | u32 | the bit test at `+0x08`, `arm9/unit_0200CED4.c:59-64` | the answer |
| `count_caught` | u16 | `ZUKAN_SAVE_CountCaught(p, heap)`, `arm9/unit_0200CED4.c:67-73` | the run's total, national or regional |
| `national_dex` | u32 | `ZUKAN_SAVE_IsNationalDexEnabled(p)`, `arm9/unit_0200CED4.c:69` | which count applies |

### Research

#### `research.mission.start` (0x0216B63C, `SCRCMD_RESEARCH_CmdStartMission`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a1` | the cause |
| `script_work` | handle | `SCRCMD_WORK_GetScriptWork(a1)`, `ov010_func_0216B63C.c:56` | the script frame |
| `block` | handle | `BEACON_GetBlock(save_control)`, `ov010_func_0216B63C.c:58` | the misc block |
| `bit_block` | handle | `SYMBOL_SAVE_GetBitBlock(save_control)`, `ov010_func_0216B63C.c:59` | the bit block |
| `entry_count` | int | first read `k`, `ov010_func_0216B63C.c:60`; `BEACON_SetEntryCount(h,k)` `64` | the mission entry count |
| `recent_ids` | u16[3] | second, third, fourth reads, `ov010_func_0216B63C.c:61-63`; `BEACON_SetRecentId(h,i,s[i])` `67` | the mission targets |
| `recent_values` | u32 | zeroed by `BEACON_SetRecentValue(h,i,0)`, `ov010_func_0216B63C.c:68` | the progress counters |
| `start_time` | u64 | `GF_STANDARD_SetupRtc()`, `ov010_func_0216B63C.c:70`; `BEACON_SetLastTime` `71` | the mission clock |

#### `research.rank.raise` (0x0216B538, `SCRCMD_RESEARCH_CmdRaiseResearchRank`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a1` | the cause |
| `script_work` | handle | `SCRCMD_WORK_GetScriptWork(a1)`, `func_0216B538.c:48` | the script frame |
| `block` | handle | `BEACON_GetBlock(save_control)`, `func_0216B538.c:55` | the misc block |
| `rank_before` | u8 | `BEACON_GetMode(block)`, `func_0216B538.c:57` | the prior rank |
| `rank_after` | u8 | `BEACON_SetMode(block, rank_before + 1)`, `func_0216B538.c:66` | the new rank |
| `capped` | boolean | the test `rank_before < 5`, `func_0216B538.c:60` | whether the raise happened |
| `research_rank` | u8 | `GAME_BEACON_SetResearchTeamRank(rank_after)`, `func_0216B538.c:71` | the beacon rank |

#### `research.mission.clear` (0x0216B6DC, `SCRCMD_RESEARCH_CmdClearMission`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a1` | the cause |
| `script_work` | handle | `SCRCMD_WORK_GetScriptWork(a1)`, `ov010_func_0216B6DC.c:43` | the script frame |
| `block` | handle | `BEACON_GetBlock(save_control)`, `ov010_func_0216B6DC.c:45` | the misc block |
| `entry_count_before` | u8 | `BEACON_GetEntryCount(block)` (0x0200C33C) before the clear | the prior count |
| `entry_count_after` | u8 | `BEACON_SetEntryCount(h, 0)`, `ov010_func_0216B6DC.c:47` | the cleared count |
| `recent_ids_after` | u8[3] | `BEACON_SetRecentId(h, i, 0xFF)`, `ov010_func_0216B6DC.c:49` | the cleared targets |

### Flags

#### `flag.set` (0x02159728, `SCRIPT_WORK_CmdSetEventFlag`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a0` | the cause |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(a0)`, `func_02159728.c:39` | the owner's data |
| `event_work` | handle | `GAME_DATA_GetEventWork(game_data)`, `func_02159728.c:41` | the flag block |
| `flag_id` | int | `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `func_02159728.c:46` | which flag is set |
| `flag_before` | u32 | `ZONEDATA_CheckEventFlag(event_work, flag_id)` (0x020142E8) before the write | the prior value |
| `flag_after` | u32 | the bit `id % 8` of the byte set by `ZONEDATA_SetEventFlag`, `func_02159728.c:50`, `arm9/func_02014314.c:23` | the new value |

#### `flag.reset` (0x02159750, `SCRIPT_WORK_CmdResetEventFlag`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a0` | the cause |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(a0)`, `func_02159750.c:39` | the owner's data |
| `event_work` | handle | `GAME_DATA_GetEventWork(game_data)`, `func_02159750.c:41` | the flag block |
| `flag_id` | int | `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `func_02159750.c:46` | which flag is reset |
| `flag_before` | u32 | `ZONEDATA_CheckEventFlag(event_work, flag_id)` before the write | the prior value |
| `flag_after` | u32 | the bit cleared by `ZONEDATA_ResetEventFlag`, `func_02159750.c:50`, `arm9/func_0201433C.c:23` | the new value |

#### `flag.check` (0x02159778, `SCRIPT_WORK_CmdCheckEventFlag`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a0` | the cause |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(a0)`, `func_02159778.c:44` | the owner's data |
| `event_work` | handle | `GAME_DATA_GetEventWork(game_data)`, `func_02159778.c:46` | the flag block |
| `flag_id` | int | `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `func_02159778.c:51` | which flag is tested |
| `value` | u16 | `ZONEDATA_CheckEventFlag(event_work, flag_id)` written to `*q`, `func_02159778.c:60,62` | the answer |

#### `flag.set.offset` (0x0215B328, `SCRCMD_WORK_CmdSetEventFlagOffset`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a0` | the cause |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(a0)`, `func_0215B328.c:39` | the owner's data |
| `event_work` | handle | `GAME_DATA_GetEventWork(game_data)`, `func_0215B328.c:41` | the flag block |
| `offset` | int | `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `func_0215B328.c:46` | the script's flag index |
| `flag_id` | u16 | `offset + 1420`, `ov010_func_0215A4EC.c:16` | the real flag id set |
| `flag_before` | u32 | `ZONEDATA_CheckEventFlag(event_work, flag_id)` before the write | the prior value |

#### `flag.reset.offset` (0x0215B350, `SCRCMD_WORK_CmdResetEventFlagOffset`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a0` | the cause |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(a0)`, `func_0215B350.c:39` | the owner's data |
| `event_work` | handle | `GAME_DATA_GetEventWork(game_data)`, `func_0215B350.c:41` | the flag block |
| `offset` | int | `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `func_0215B350.c:46` | the script's flag index |
| `flag_id` | u16 | `offset + 1420`, `ov010_func_0215A500.c:16` | the real flag id reset |
| `flag_before` | u32 | `ZONEDATA_CheckEventFlag(event_work, flag_id)` before the write | the prior value |

#### `flag.check.offset` (0x0215B378, `SCRCMD_WORK_CmdCheckEventFlagOffset`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a0` | the cause |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(a0)`, `func_0215B378.c:43` | the owner's data |
| `event_work` | handle | `GAME_DATA_GetEventWork(game_data)`, `func_0215B378.c:45` | the flag block |
| `offset` | int | `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `func_0215B378.c:50` | the script's flag index |
| `flag_id` | u16 | `offset + 1420`, `ov010_func_0215A4D8.c:18` | the real flag id tested |
| `value` | u16 | `EVENT_POKE_STATUS_CheckFlag` written to `*q`, `func_0215B378.c:59,61` | the answer |

#### `flag.write` (0x02014314, `ZONEDATA_SetEventFlag`, override)

The catalogue binds `flag.write` here, not to `SAVE_EVENT_Write`
(0x0202F190), a seven-way box-record writer (`docs/mod-events.md`, section
1c). It sets one bit of the event-flag bitmap.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `bitmap` | handle | call arg `a0`, `arm9/func_02014314.c:18` | the event-flag bitmap written |
| `flag_id` | int | call arg `a1`, `arm9/func_02014314.c:18` | which flag is set |
| `byte` | ptr | `ZONEDATA_GetEventFlagAddress(a0, a1)`, `arm9/func_02014314.c:20` | the byte that holds the flag; a null pointer means no write |
| `bit` | u32 | `a1 % 8`, `arm9/func_02014314.c:23` | the bit set in the byte |
| `value_before` | u8 | the byte before the write, `arm9/func_02014314.c:22` | the prior value |
| `value_after` | u8 | the byte with the bit set, `arm9/func_02014314.c:23` | the new value |

`SAVE_EVENT_Write` (0x0202F190) is not this function: it forwards to
`SAVE_EVENT_WriteMode`, a box-record writer keyed on the mode
(`arm9/func_0202F190.c:31`, `arm9/func_0202F1B4.c:44-84`), and the flag id is
not an argument.

#### `flag.misc.set` (0x0215AC68, `SCRCMD_WORK_CmdSetMiscFlag`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a1` | the cause |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(a1)`, `ov010_func_0215AC68.c:45` | the owner's data |
| `misc` | handle | `GAME_DATA_GetMiscSave(game_data)`, `ov010_func_0215AC68.c:47` | the misc block |
| `flag_bit` | int | `SCRIPT_WORK_ReadWorkValue(a,a1)`, `ov010_func_0215AC68.c:44` | the bit set; the badge index for bits 0..7 |
| `flag_word_before` | u32 | the `+0x04` word of the misc block before the write | the prior badge flags |
| `flag_word_after` | u32 | `BEACON_SetFlag(misc, flag_bit)`, `ov010_func_0215AC68.c:47` | the new badge flags |
| `date_y` | u32 | `GF_STANDARD_SetField00_F4E4(&v)` field `v.a`, `ov010_func_0215AC68.c:48-49` | the badge date year part |
| `date_m` | u32 | field `v.b`, `ov010_func_0215AC68.c:49` | the badge date month part |
| `date_d` | u32 | field `v.c`, `ov010_func_0215AC68.c:49` | the badge date day part |
| `badge_date_block` | handle | `GAME_DATA_GetSaveBlock21(game_data)`, `ov010_func_0215AC68.c:49` | the block 0x21 the date is stamped into |

This is the badge award path. A mod binds it and reads `flag_bit` and the date.

#### `flag.misc.check` (0x0215AC3C, `SCRCMD_WORK_CmdCheckMiscFlag`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `b` | the cause |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(b)`, `ov010_func_0215AC3C.c:30` | the owner's data |
| `misc` | handle | `GAME_DATA_GetMiscSave(game_data)`, `ov010_func_0215AC3C.c:30` | the misc block |
| `flag_bit` | int | `SCRIPT_WORK_ReadWorkValue(a,b)`, `ov010_func_0215AC3C.c:29` | which badge bit is tested |
| `value` | u32 | `BEACON_GetFlag(misc, flag_bit)`, `ov010_func_0215AC3C.c:30` | the answer |

#### `flag.fixed.set` (0x0215B068, `SCRCMD_WORK_CmdSetFixedEventFlag`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `b` | the cause |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(b)`, `ov010_func_0215B068.c:24` | the owner's data |
| `event_work` | handle | `GAME_DATA_GetEventWork(game_data)`, `ov010_func_0215B068.c:24` | the flag block |
| `flag_id` | int | the constant 0x963, `ov010_func_0215B068.c:24` | the flag set |
| `flag_before` | u32 | `ZONEDATA_CheckEventFlag(event_work, 0x963)` before the write | the prior value |

The event carries no argument: it sets flag 0x963 unconditionally.

#### `misc.byte.set` (0x0215B138, `SCRCMD_WORK_CmdSetMiscByte`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a1` | the cause |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(a1)`, `func_0215B138.c:36` | the owner's data |
| `misc` | handle | `GAME_DATA_GetMiscSave(game_data)`, `func_0215B138.c:36` | the misc block |
| `index` | u32 | `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `func_0215B138.c:34` | which slot state |
| `value` | u8 | constant 1, `func_0215B138.c:36`; `BEACON_SetSlotState(misc, index, 1)` | the state written |
| `slot_byte_before` | u8 | the `+0x45` byte before the write | the prior packed states |
| `slot_byte_after` | u8 | the `+0x45` byte after the write, `arm9/unit_0200C0E8.c:307-310` | the new packed states |

#### `misc.byte.check` (0x0215B158, `SCRCMD_WORK_CmdCheckMiscByte`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `b` | the cause |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(b)`, `func_0215B158.c:31` | the owner's data |
| `misc` | handle | `GAME_DATA_GetMiscSave(game_data)`, `func_0215B158.c:31` | the misc block |
| `index` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `func_0215B158.c:29` | which slot state |
| `state` | u8 | `BEACON_GetSlotState(misc, index)`, `func_0215B158.c:31`; `(byte >> (index*2)) & 3` `arm9/unit_0200C0E8.c:314` | the packed two-bit state |
| `value` | u16 | `state == 3`, `func_0215B158.c:31` | the answer |

#### `misc.byte.get` (0x0215B104, `SCRCMD_WORK_CmdGetMiscByte`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `b` | the cause |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(b)`, `func_0215B104.c:31` | the owner's data |
| `misc` | handle | `GAME_DATA_GetMiscSave(game_data)`, `func_0215B104.c:31` | the misc block |
| `index` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `func_0215B104.c:29` | which slot state |
| `state` | u8 | `BEACON_GetSlotState(misc, index)`, `func_0215B104.c:31` | the packed two-bit state |
| `value` | u16 | `(state & 1) != 0`, `func_0215B104.c:31` | the low bit as a boolean |

### Event work

#### `event.work` (0x0201296C, 0x02012FE4, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event_work` | handle | `GAME_DATA_GetEventWork(game_data)`, a `u32` at game data `+0x1AC`, `arm9/unit_02012924.c:189-192` | the live flag block |
| `event_work_save` | handle | `GAME_DATA_GetEventWorkFromSave(sys)`, `func_020071CC(sys, 0x2d)`, `arm9/func_02012FE4.c:17` | the saved flag block |
| `flag_id` | u32 | the id the caller passes to `ZONEDATA_CheckEventFlag`, `arm9/func_020142E8.c:47` | the flag read |
| `value` | u32 | `ZONEDATA_CheckEventFlag(block, id)`, `arm9/func_020142E8.c:47-56` | the answer |

`event.work` is the flag bitmap itself, not one flag. The port exposes the block
and the flag accessors of section 0a.

### Badges

#### `badge.state` (0x0200C234, `BEACON_GetFlag`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `misc` | handle | the block argument, `arm9/unit_0200C0E8.c:238` | the misc block |
| `bit` | u32 | the bit argument, `arm9/unit_0200C0E8.c:238` | the badge index for bits 0..7 |
| `value` | u32 | `(p->unk04 & (1 << bit)) != 0`, `arm9/unit_0200C0E8.c:240` | whether the badge is held |
| `badge_mask` | u32 | the eight bits `0..7` of `+0x04`, read by looping `BEACON_GetFlag` at `ov010_func_0216F40C.c:264-268` | all badges at once |

#### `badge.count` (0x0200C258, `BEACON_CountFlags`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `misc` | handle | the block argument, `arm9/unit_0200C0E8.c:248` | the misc block |
| `count` | u32 | the popcount of `+0x04`, `arm9/unit_0200C0E8.c:248-259` | the badge count |
| `flag_word` | u32 | `+0x04`, read in the loop, `arm9/unit_0200C0E8.c:253` | the raw badge flags |

#### `badge.write` (0x0200C248, `BEACON_SetFlag`, override)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `misc` | handle | call arg `p`, `arm9/unit_0200C0E8.c:243` | the misc block |
| `bit` | u32 | call arg `bit`, `arm9/unit_0200C0E8.c:243` | the badge index for bits 0..7 |
| `flag_word_before` | u32 | `+0x04` before the write | the prior badges |
| `flag_word_after` | u32 | `p->unk04 |= 1 << bit`, `arm9/unit_0200C0E8.c:245` | the new badges |
| `caller` | return address | the slot's return address | which write path set the bit |

`badge.write` is an override point, not an event. It fires for any bit of the
misc flag word, not only a badge, and not at one semantic moment.

#### `badge.date` (0x02008A7C, `TRCARD_SIGN_SetBadgeDate`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `obj` | handle | call arg `obj`, `arm9/unit_02008A7C.c:93` | the block 0x21 |
| `index` | u32 | call arg `i`, `arm9/unit_02008A7C.c:93` | which badge record |
| `date_a` | u32 | call arg `a`, stored at `obj->unk604[i].unk00`, `arm9/unit_02008A7C.c:102` | the date's first byte |
| `date_b` | u32 | call arg `b`, stored at `+0x01`, `arm9/unit_02008A7C.c:103` | the date's second byte |
| `date_c` | u32 | call arg `c`, stored at `+0x02`, `arm9/unit_02008A7C.c:104` | the date's third byte |
| `marker` | u16 | `obj->unk638[i] = 0xF0`, `arm9/unit_02008A7C.c:105` | the date is stamped |
| `wall_clock` | u64 | `RTC_ConvertDateTimeToSecond(date, time)` from `sub_0203F49C`, `arm9/unit_02008A7C.c:99-100` | the exact award time |
| `seconds` | u64 | `TRCARD_SIGN_SetLastBadgeTime(obj, sec, sec>>32)`, `arm9/unit_02008A7C.c:107` | the badge clock |

#### `badge.profile` (0x0216F40C, `TRCARD_SYS_BuildProfile`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `save` | handle | call arg `save`, `ov010_func_0216F40C.c:220` | the save |
| `misc` | handle | `GAME_DATA_GetMiscSave(save)`, `ov010_func_0216F40C.c:241` | the misc block |
| `badge_mask` | u8 | the loop over `BEACON_GetFlag(x1c, i)` for `i` 0..7, `out->f08`, `ov010_func_0216F40C.c:264-268` | the eight badges as a mask |
| `money` | u32 | `BEACON_GetPoints(x1c)`, `out->f20`, `ov010_func_0216F40C.c:261` | the card money |
| `dex_seen` | u16 | `ZUKAN_SAVE_CountSeen(x0c, heap)`, `out->f24`, `ov010_func_0216F40C.c:338` | the seen count on the card |
| `name` | u16[8] | `func_020084F4(player)` copied, `out->name`, `ov010_func_0216F40C.c:248` | the trainer name |
| `profile` | handle | call arg `out`, the `0x674`-byte record, `ov010_func_0216F40C.c:220` | the whole card record |

### Fields a mod would want that cannot be obtained

- **The nature, ability and IVs of a script-added mon before the add.** They are
  drawn inside `SCRCMD_PROC_CreateMonFromSpec` by `POKE_TOOL_CreatePersonalRnd`
  (`ov010_func_0215C40C.c:57`). The add events fire before the draw, so a mod
  reads them from the added mon after, not from the payload.
- **The exact species and form of a dex record at `zukan.seen.register`.** The
  species is a script value (`ov010_func_0215AEE8.c:45`), but the gender and form
  flags are set from the record the call creates, and the record's field ids are
  not all named. The port reads the flags after the call.
- **A "badge awarded" event that is not a flag write.** The game has none. The
  nearest call site is `flag.misc.set` (`ov010_func_0215AC68.c:47-49`); the bit
  is the badge index only for bits 0..7, and the game does not label it.
- **The prior value of a research field.** `research.mission.clear` sets the
  entry count to 0 and the recent ids to 0xFF without reading them
  (`ov010_func_0216B6DC.c:47-49`). A mod that wants the cleared mission reads it
  before the event from `BEACON_GetEntryCount` and `BEACON_GetRecentId`.

### The thinnest payloads

- `zukan.flag.a`, `zukan.flag.b`, `flag.fixed.set`: no argument, one
  unconditional write. The payload is the block and the before and after the
  port reads by calling the accessor.
- `badge.count`, `badge.state`, `badge.profile`: state accessors, so the payload
  is what the caller reads, not a situation. `badge.profile` is a card builder
  called on menu open, so its "event" is a read.
- `event.work`: a state read of the whole flag block; the event is the accessor
  call, not a game action.
- `misc.byte.get`, `misc.byte.check`, `flag.check`, `flag.check.offset`,
  `zukan.is.caught`, `zukan.seen.count`: read-only tests. The payload is the
  index and the answer.
- `move.push`, `move.slot`: eight-byte tail thunks to a box-move writer
  (`arm9/func_020180F4.c:32`, `arm9/unit_020181A0.c:77-80`). The move and slot
  are the call's arguments; nothing else is in scope.

## 16. Save, overlay and networking

The save, overlay and networking area is the 45 entries of `docs/mod-events.md`,
section 1 (30 events, 11 state entries and 4 override points): the twelve save
entries, the two overlay lifecycle entries, the two link entries, the one net
entry, the nine comm entries, the eleven wm entries, the two dwc entries, the
five network entries and the GTS trade entry. The port tracks the
network context (section 0a) for the link, the comm, the wm and the dwc state.

Networking has no transport anywhere in the tree, so the wm, dwc, comm and
network entries state what the payload would carry when a transport exists and
mark the transport itself still to build. The overlay events fire around the
port's own loader, so each states what a handler can rely on at that point.

The owner of a system event is the local player. The port fills `owner` with 0
for the local instance. A cross-instance player identity is not obtainable today
and is still to build; the peer of a network event is separate and comes from
the network context.

### Save and load

The save control object is the object every save entry operates on. It is
`Obj11223` at the call (`src/matched/include/Obj11223.h`, lines 3 to 8) and the
same object as `Sub02007168` (`src/matched/arm9/func_02007168.c`, lines 32 to
44): `f3` is the save kind byte, the bytes at +4 and +5 are the guard flags,
+0 and +1 are the completion flags, and +0x10 is the backup handle.

#### `save.begin` (0x02007150, arm9)

`int SAVE_CONTROL_StartSave(int a0)` (`src/matched/arm9/func_02007150.c`,
line 17). The save control object exists and the game has not written yet.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `save_control` | u32 | arg `a0` (func_02007150.c:17) | Names the save object; a mod matches it to `save.done` and `save.load` |
| `backup_handle` | u32 | field `Obj11223.f10` (Obj11223.h:7; func_02007150.c:17) | The backup target the write goes to, so a mod can tell a real save from a slot change |
| `save_kind` | u8 | call `func_02008754(a0)` (func_02007150.c:17) | The kind byte the game stores at +3; separates a full save from a sub-save. The port calls the callee or reads `f3` after the original |
| `guard_flags` | u16 | field bytes +4 and +5 (`Sub02007168.unk04`, `unk05`; func_02007168.c:37 to 38) | Nonzero means the save is a no-op (func_02007168.c:54); a mod should not treat it as a write |
| `completion_flags` | u16 | field bytes +0 and +1 (`Sub02007168.unk00`, `unk01`; func_02007168.c:33 to 34) | The pair the game sets to (0, 1) when a save completes (func_02007168.c:62 to 63); a mod reads the prior state |
| `owner` | u32 | local player, 0 (docs/mod-events.md, the owner framing) | Attributes the save to a player in a soullink or multiplayer game |

Cause, which is one call earlier and is not in scope: the requester of the save.
`GAME_DATA_SaveStart` (0x02012DAC) calls this after it sets the game data's
+0x1CE byte to 1 and sets UI flag 39 (`src/matched/arm9/func_02012DAC.c`,
lines 21) and after `WIH_DWC_PauseTimers`. The port tracks the requester only if
it also hooks 0x02012DAC, which is not in `mod/declared_set.txt`. The port
clears the tracked requester at `save.done` and at process reset.

#### `save.done` (0x02007168, arm9)

`u32 SAVE_CONTROL_MainSave(struct Sub02007168 *p)`
(`src/matched/arm9/func_02007168.c`, lines 50 to 66). The write has run or the
early guard fired.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `save_control` | u32 | arg `p` (func_02007168.c:50) | Names the save object, matching `save.begin` |
| `backup_handle` | u32 | field `Sub02007168.unk10` (func_02007168.c:43, 60) | The backup target the write used |
| `guard_flags` | u16 | field bytes +4 and +5 (func_02007168.c:37 to 38, 54) | Nonzero means the early arm returns 0 and the save did not run |
| `completion_flags` | u16 | field bytes +0 and +1 (func_02007168.c:33 to 34, 62 to 63) | The pair set to (0, 1) on a completed save; a mod persists its state on it |
| `result` | u32 | ret of `SAVE_CONTROL_MainSave` (func_02007168.c:52, 60, 65) | 0 on the early arm, else the backup result; the mod's state write should follow a success |
| `owner` | u32 | local player, 0 | Attributes the save to a player |

The result is ahead at entry. The port obtains it by running the original and
capturing its return before the handlers, or by tracking it. If the port keeps
the entry rule, `result` is absent and a mod reads the completion flags instead.
The save is observation only, so running the original first costs nothing.

#### `save.load` (0x02012C18, arm9)

`void GAME_DATA_LoadFromSave(u8 *p)`
(`src/matched/arm9/func_02012C18.c`, lines 95 to 131). `p` is the game data
object `Obj00054` (`src/matched/include/Obj00054.h`, lines 3 to 46). The load
copies the save's blocks into the game data.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `game_data` | u32 | arg `p` (func_02012C18.c:95) | The game data object the load fills; a mod reads it at `save.done` later |
| `save_control` | u32 | field `Obj00054.f0` (Obj00054.h:4; func_02012C18.c:100, 104) | The save control the blocks come through |
| `skipped` | u8 | call `func_020071E4(save_control) == 1` (func_02012C18.c:100) | The load returns early on this guard; a mod must not read the blocks as freshly loaded when it is set |
| `event_work` | u32 | call `GAME_DATA_GetEventWorkFromSave(save_control)` = block 0x2d (func_02012FE4.c:17) | The save flags and event work a Nuzlocke mod keys its used set on |
| `occupy_list` | u32 | call `func_020071CC(save_control, 0x29)` (func_02012C18.c:109) | The occupy block the load copies into the occupy info |
| `player_work` | u32 | call `GAME_DATA_GetMyPlayerWork(p)` (func_02012C18.c:104) | The player record the load fills, the owner's identity |
| `occupy_info` | u32 | call `GAME_DATA_GetOccupyInfo(p)` (func_02012C18.c:108) | The occupy info the load fills |
| `battle_rec_slot` | u32 | call `GAME_DATA_GetLastBattleRecSlot(p)` (func_02012C18.c:111) | The last battle record the load fills; a mod reads the prior battle |
| `second_union_info` | u32 | call `GAME_DATA_GetSecondUnionInfo(p)` (func_02012C18.c:113) | The union info the load fills; multiplayer state |
| `musical_shot` | u32 | call `MUSICAL_SHOT_GetFromSave(save_control)` (func_02012C18.c:114) | The saved musical data the load copies |
| `wifi_list` | u32 | call `GAME_DATA_GetWifiList(p)` (func_02012C18.c:122) | The saved WiFi list the load copies; a soullink mod reads it |
| `play_time` | u32 | call `func_02008748(save_control)` (func_02012C18.c:129) | The saved play time the load copies into `f6AC` |
| `play_time_dest` | u32 | field `Obj00054.f6AC` (Obj00054.h:45; func_02012C18.c:130) | The destination the play time lands in |
| `owner` | u32 | local player, 0 | Attributes the load to a player |

Cause, not in scope: which slot or menu requested the load. The port tracks it
from the save menu if it hooks it; the catalogue does not declare such a point.
The port clears it at the next `save.begin`.

#### `save.flag` (0x02012BF8, arm9, state)

`int GAME_DATA_CheckSaveFlag(int a0, int a1)`
(`src/matched/arm9/func_02012BF8.c`, line 18). A state accessor, not a slot.

| Accessor | Inputs | Output | Where it comes from | Why a mod wants it |
|---|---|---|---|---|
| `bw_save_flag` | `a0` object, `a1` flag id | int | `RECENT_ID_LIST_SAVEDATA_ContainsId(RECENT_ID_LIST_SAVEDATA_GetBlockFromSys(GAME_DATA_GetSaveControlWork(a0)), a1)` (func_02012BF8.c:18) | Tests a save flag by id; Nuzlocke and Soullink read their flags here |

The flag id is the caller's `a1`. The port's wrapper takes the id and returns
the boolean. The block behind it is the recent-id list block
(`src/matched/arm9/unit_02012BE0.c`, lines 45 to 47).

#### `save.block` (0x020071CC, arm9, override)

`void *SAVE_CONTROL_GetBlock(struct Sub020071CCCtrl *ctrl, u32 id)`
(`src/matched/arm9/unit_020071CC.c`, lines 58 to 61). An override point.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `save_control` | u32 | arg `ctrl` (unit_020071CC.c:58) | Names the save object |
| `block_id` | u32 | arg `id` (unit_020071CC.c:58) | Which block is asked for; the money and badges block is 0x34 |
| `backup_work` | u32 | field `Sub020071CCCtrl.unk10` (unit_020071CC.c:43 to 54, 60) | The backup work the block comes from |
| `block` | u32 | ret of `BACKUP_SYSTEM_GetRecordWork(ctrl->unk10, id)` (unit_020071CC.c:60) | The block pointer; a wrap handler may replace or inspect it |

#### `save.misc` (0x02012F08, arm9, override)

`void *GAME_DATA_GetMiscSave(int *p)` (`src/matched/arm9/func_02012F08.c`,
lines 16 to 20). An override point.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `game_data` | u32 | arg `p` (func_02012F08.c:17) | The game data object whose first word is the save control |
| `save_control` | u32 | field `*p` (func_02012F08.c:19) | The save control the block comes through |
| `block` | u32 | ret of `BEACON_GetBlock(*p)` = block 0x34 (unit_0200C0E8.c:126 to 128; func_02012F08.c:19) | The misc block that holds money at +0x00 and the badge flags at +0x04 |

### Overlay lifecycle

The port owns the loader. `sdk/overlay_image.c` installs a provider through
`bw_overlay_set_provider` (`sdk/overlay.h`, lines 137 to 143): `provider_load`
copies the file to the window, binds the names and relocates the pointers
(`sdk/overlay_image.c`, lines 226 to 253), and `provider_start` runs the static
initialisers (lines 255 to 266). The descriptor is `BwOverlayDesc`
(`sdk/overlay.h`, lines 52 to 69). The DS struct is `FSOverlayInfo`
(`src/matched/arm9_sdk/func_0207902C.c`, lines 38 to 56).

What a handler can rely on and cannot, because the events fire around the
port's own loader:

- Before the load work, the descriptor is known and the overlay is not
  resident. The window still holds the previous overlay's bytes and the new
  overlay's data is not there yet. A handler cannot read the new overlay's data
  or call its functions.
- After the load work and before the static initialisers, the image is copied,
  bound and relocated, so a handler can read the fresh data, but the overlay's
  own initialisers have not run.
- After the static initialisers, both the data and the initialised state are
  there.
- The port must fix where the handler runs relative to these steps
  (`docs/mod-events.md`, still-to-build item 10). The tables below give the
  fields that are known at either point.

#### `overlay.load` (0x02078EB0, arm9)

`BOOL FS_LoadOverlayImage(FSOverlayInfo *p_ovi)`
(`src/matched/arm9_sdk/func_02078EB0.c`, lines 73 to 91).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `overlay_id` | u32 | field `FSOverlayInfo.id` (func_0207902C.c:40; func_02078EB0.c:79) | Which overlay loads; a mod keys per-overlay work on it |
| `ram_address` | u32 | field `ram_address` (func_0207902C.c:41; func_02078EB0.c:84) | The window the image lands in; the key to which overlay it displaces |
| `ram_size` | u32 | field `ram_size` (func_0207902C.c:42) | The code and initialised data extent |
| `bss_size` | u32 | field `bss_size` (func_0207902C.c:43) | The zeroed extent after the image |
| `sinit_init` | u32 | field `sinit_init` (func_0207902C.c:44) | The first static initialiser, a DS address |
| `sinit_init_end` | u32 | field `sinit_init_end` (func_0207902C.c:45) | The initialiser list end |
| `sinit_count` | u32 | call `(sinit_init_end - sinit_init) / 4` (func_0207902C.c:117 to 118) | How many initialisers run at `overlay.start` |
| `file_id` | u32 | field `file_id` (func_0207902C.c:46) or call `FS_GetOverlayFileID(p_ovi)` (func_02078EB0.c:79) | The ROM file the image is read from |
| `compressed` | u32 | field `compressed` (func_0207902C.c:47) | The low 24 bits are the compressed size; a mod sees the encoding |
| `flag` | u8 | field `compressed >> 24` (func_0207902C.c:48) | Bit 0 is compressed, bit 1 carries the digest check (func_0207902C.c:94, 111) |
| `target` | u32 | field `FSOverlayInfo.target` (func_0207902C.c:54) | ARM9 or ARM7; the port loads ARM9 overlays |
| `file_size` | u32 | call `FSi_GetOverlayBinarySize(p_ovi)` (func_02078EB0.c:81) | The byte count read into the window |
| `result` | u32 | ret of `FS_LoadOverlayImage` (func_02078EB0.c:75, 86) | 1 when the file opened and read whole; the port owns this outcome |
| `window_was_resident` | u32 | ctx, call `bw_ovl_resident_at(ram_address)` (sdk/overlay_image.c:283 to 313) | The overlay the window held, so a mod can tell a reload from a fresh load |
| `displaces` | u32 | ctx, the resident overlay whose window overlaps (sdk/overlay_image.c:213 to 224) | Which overlay's data is lost; a mod that wrote it must not rely on it |
| `reload` | u8 | ctx, `bw_overlay_is_loaded(overlay_id)` (sdk/overlay.h:146) | A reload resets the overlay's data; a mod re-applies its write |
| `owner` | u32 | local player, 0 | Attributes the load to a player |

#### `overlay.start` (0x0207902C, arm9)

`void FS_StartOverlay(FSOverlayInfo *p_ovi)`
(`src/matched/arm9_sdk/func_0207902C.c`, lines 78 to 128). The image is loaded;
the static initialisers run here.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `overlay_id` | u32 | field `FSOverlayInfo.id` (func_0207902C.c:98) | Which overlay starts |
| `ram_address` | u32 | field `ram_address` (func_0207902C.c:82, 106, 113) | The window the initialisers run over |
| `ram_size` | u32 | field `ram_size` (func_0207902C.c:115) | The extent flushed before the initialisers |
| `bss_size` | u32 | field `bss_size` (func_0207902C.c:43) | The zeroed extent |
| `sinit_init` | u32 | field `sinit_init` (func_0207902C.c:117) | The first initialiser |
| `sinit_init_end` | u32 | field `sinit_init_end` (func_0207902C.c:118) | The list end |
| `sinit_count` | u32 | call `(sinit_init_end - sinit_init) / 4` (func_0207902C.c:117 to 118) | How many initialisers run; a mod knows the count before and after |
| `digest_ok` | u8 | call `FSi_CompareDigest(...)` ret (func_0207902C.c:100) | 1 when the image passed the digest check; a failed check terminates |
| `decompressed` | u8 | field `flag & 1` (func_0207902C.c:111) | Whether the image is decompressed here |
| `twl_autoload` | u8 | call `OS_IsRunOnTwl()` and the address window (func_0207902C.c:82 to 88) | Whether the TWL autoload path is taken; a mod knows which build runs |
| `owner` | u32 | local player, 0 | Attributes the start to a player |

### Link

#### `link.up`, `link.down` (0x0215CD24, ov009)

`int SOCL_LinkIsOn(void)` returns `WCM_GetApMacAddress() != 0`
(`src/matched/ov_sdk/ov009_func_0215CD24.c`, lines 20 to 23). One address, two
events, distinguished by the return. The predicate is called often, so the port
fires the event only when the state changes and tracks the previous state.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `link_on` | u8 | call `WCM_GetApMacAddress() != 0` (ov009_func_0215CD24.c:22) | The new link state; `link.up` when set and `link.down` when clear |
| `previous_on` | u8 | ctx, the last link state | Distinguishes a change from a poll; a mod must not act on every call |
| `changed` | u8 | ctx, `link_on != previous_on` | A mod acts only on a real change |
| `ap_mac` | u8[6] | call `WCM_GetApMacAddress()` (ov009_func_0215CD24.c:18, 22) | The access point address, the one identifier the predicate offers |
| `ap_present` | u8 | call, the pointer is not 0 (ov009_func_0215CD24.c:22) | The same bit as `link_on`, kept for a mod that logs the pointer |
| `link_level` | u32 | call `WCM_GetLinkLevel()` (ov009_func_021666C8.c:42) | The signal level at the change; a soullink mod warns on a weak link |
| `rssi` | u8 | call `WCMi_GetRssiAverage()` (ov009_func_021666C8.c:52) | The raw signal behind the level |
| `device_id` | u8 | field `WCMWork.deviceId` (ov009_func_02166710.c:39) | The RF device, which selects the level thresholds |
| `owner` | u32 | local player, 0 | Attributes the link to a player |
| `peer` | u32 | ctx, not obtainable here | Which remote player is on the other end; the AP MAC is not the peer |

The peer is not obtainable at this predicate. `SOCL_LinkIsOn` returns a boolean
over the access point MAC, not a peer identity. The port takes the peer from the
network context, which the WM AID and the DWC profile feed. The port clears
`previous_on` at process reset and at `link.down`.

#### `link.level` (0x021666C8, 0x02166710, ov009, state)

`WMLinkLevel WCM_GetLinkLevel(void)` (`src/matched/ov_sdk/ov009_func_021666C8.c`,
lines 42 to 56) and `WMLinkLevel WCM_CalcLinkLevel(unsigned char rssi)`
(`src/matched/ov_sdk/ov009_func_02166710.c`, lines 34 to 64). State, not a slot.

| Accessor | Inputs | Output | Where it comes from | Why a mod wants it |
|---|---|---|---|---|
| `bw_link_level` | none | u32 0 to 3 | `WCM_GetLinkLevel` (ov009_func_021666C8.c:42) | The link quality; a soullink mod acts on a weak link |
| `bw_link_rssi` | none | u8 | `WCMi_GetRssiAverage()` (ov009_func_021666C8.c:52) | The raw signal behind the level |
| `bw_link_phase` | none | u8 | field `WCMWork.phase` (ov009_func_021666C8.c:33, 51) | The level is nonzero only when phase is 9; a mod distinguishes "down" from "not connected" |
| `bw_link_device_id` | none | u8 | field `WCMWork.deviceId` (ov009_func_02166710.c:29, 39) | The device whose thresholds apply |

### Net

#### `net.send`, `net.receive` (not built)

No transport exists. `sdk/arm7/` has no Wm or DWC responder and the tree has no
sockets (`docs/modding.md`, "Networking seam: missing"; `docs/modding.md`, the
capability matrix, "Transport between two instances: missing"). The names are
fixed so a mod does not rebind when a transport lands (`docs/mod-interface.md`,
lines 622 to 625). The candidate send and receive points are the Wm and DWC
entry points of the wm and dwc sections.

What the payload carries once a transport exists:

| Field | Type | Where it would come from | Why a mod wants it |
|---|---|---|---|
| `transport` | u8 | ctx, which of Wm or DWC | The two transports carry different frames |
| `direction` | u8 | ctx, send or receive | One field for both events |
| `peer` | u32 | ctx, the remote station id | Which instance sent or receives; the agreement is per peer |
| `local_aid` | u16 | field `WMArm9Buf.myAid` (WmReceiveFifo.c:72) | The local station id in the multipoint group |
| `port` | u16 | arg `port` (WM_SetMPDataToPortEx.c:74) or field `WMPortRecvCallback.port` (WmReceiveFifo.c:164) | Which port the bytes belong to; the game multiplexes ports |
| `dest_bitmap` | u16 | arg `destBitmap` (WM_SetMPDataToPortEx.c:74) or field `WMPortSendCallback.destBitmap` (WmReceiveFifo.c:144) | Which stations are addressed |
| `send_bytes` | u8[] | arg `sendData`, length `sendDataSize` (WM_SetMPDataToPortEx.c:73) | The bytes that leave; the substance of a send |
| `recv_bytes` | u8[] | field `WMPortRecvCallback.data` and `length` (WmReceiveFifo.c:166 to 167) | The bytes that arrive; the substance of a receive |
| `length` | u16 | arg `sendDataSize` or field `length` (WM_SetMPDataToPortEx.c:74; WmReceiveFifo.c:167) | The byte count |
| `prio` | u16 | arg `prio` (WM_SetMPDataToPortEx.c:74) | The send priority |
| `seq_no` | u16 | field `WMPortSendCallback.seqNo` / `WMPortRecvCallback.seqNo` (WmReceiveFifo.c:153, 170) | Orders the frames; a mod drops a stale one |
| `errcode` | u16 | field `WMCallback.errcode` (WmReceiveFifo.c:88) | Whether the frame succeeded |
| `connected_aid_bitmap` | u16 | field `WMArm9Buf.connectedAidBitmap` (WmReceiveFifo.c:71, 224) | Which stations are in the group |
| `mac` | u8[6] | field `WMPortRecvCallback.macAddress` (WmReceiveFifo.c:169) | The sender's address when the aid is not enough |
| `rssi` | u16 | field `WMPortRecvCallback.rssi` (WmReceiveFifo.c:176) | The signal at receive |
| `max_send`, `max_recv` | u16 | field `WMPortRecvCallback.maxSendDataSize`, `maxRecvDataSize` (WmReceiveFifo.c:177 to 178) | The frame budget; a mod splits a large message |

The send and receive points are still to build, and which declared address is
the send and which the receive point is still to build
(`docs/mod-events.md`, still-to-build item 12).

### Comm

The comm object is the per-frame request state machine. `GAME_COMM_Update`
(0x0202627C) drives it; `GAME_COMM_CallHandler` (0x02026388) and
`GAME_COMM_CallKindNotify` (0x020263B0) dispatch the callbacks
(`src/matched/arm9/func_0202627C.c`, the header comment, lines 15 to 18). The
kind is byte +5 and selects a 0x1C-byte record of callbacks in the table at
0x0209FB5C (`src/matched/arm9/unit_02026454.c`, lines 47 to 54;
func_0202627C.c:16 to 18).

#### `comm.update` (0x0202627C, arm9)

`void GAME_COMM_Update(struct Comm *p)`
(`src/matched/arm9/func_0202627C.c`, lines 74 to 151). The struct is `Comm`
(lines 43 to 59) and the sequence sub-object is `Sub` (lines 35 to 42).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `comm` | u32 | arg `p` (func_0202627C.c:74) | Names the comm object across the comm events |
| `kind` | u8 | field `Comm.kind` (func_0202627C.c:50, 78, 81) | The request kind; selects the callback record and names the action |
| `state` | u8 | field `Sub.state` (func_0202627C.c:38, 87) | The sequence state 0 to 5; a mod sees where in the handshake the request is |
| `changed` | u8 | field `Sub.changed` (func_0202627C.c:39, 82) | Set when the sub-object was refreshed; a mod knows the work reset |
| `flags` | u8 | field `Comm.b0` to `b3` bits (func_0202627C.c:46 to 49, 103) | The per-frame flags; `b0` gates the run step |
| `last_kind` | u8 | field `Comm.f06` (func_0202627C.c:51, 92) | The kind last started |
| `counter` | u32 | field `Comm.f08` (func_0202627C.c:53, 93) | The request counter |
| `work` | u32 | field `Comm.f14` (func_0202627C.c:55, 91) | The first argument the callbacks receive |
| `start_result` | u32 | field `Comm.f18` (func_0202627C.c:56, 91, 139) | The result the start step stored |
| `finish_arg` | u32 | field `Comm.f1c` (func_0202627C.c:57, 137, 142) | The finish callback's first argument |
| `finish_callback` | u32 | field `Comm.f20` (func_0202627C.c:58, 136, 141) | The callback run at state 5 |
| `record` | u32 | call `&GAME_COMM_KindTable[p->kind]` (func_0202627C.c:81) | The callback record for the kind |
| `rec_start`, `rec_wait`, `rec_run`, `rec_stop`, `rec_end` | u32 | fields of `struct Rec` at `record` (func_0202627C.c:62 to 68, 90 to 125) | The five callbacks the kind installs; a mod names the action by them |
| `owner` | u32 | local player, 0 | Attributes the request to a player |

The action behind a kind is still to build. The record's five words are
addresses; aletheia does not name which kind is a battle turn and which is a
trade (`docs/mod-events.md`, still-to-build item 13). The port cannot name the
action from the kind number alone.

#### `comm.handler` (0x02026388, arm9, override)

`void GAME_COMM_CallHandler(struct Obj *p, u32 a)`
(`src/matched/arm9/func_02026388.c`, lines 41 to 50). An override point.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `comm` | u32 | arg `p` (func_02026388.c:41) | Names the comm object |
| `kind` | u8 | field `Obj.f5` (func_02026388.c:29, 44) | The index into `gGameCommHandlers`; the action |
| `state` | u8 | field `Obj.f10` (func_02026388.c:31, 43) | 5 means the handler is skipped |
| `f4` | u8 | field `Obj.f4` (func_02026388.c:28) | The record's second byte, unread here; a mod keeps it for parity with `notify` |
| `work` | u32 | field `Obj.f14` (func_02026388.c:33, 47) | The handler's first argument |
| `start_result` | u32 | field `Obj.f18` (func_02026388.c:34, 47) | The handler's second argument |
| `call_arg` | u32 | arg `a` (func_02026388.c:41, 47) | The handler's third argument |
| `handler` | u32 | call `*(Fn *)(gGameCommHandlers + p->f5 * 0x1C)` (func_02026388.c:44) | The handler pointer; a wrap handler decides before or after it |
| `owner` | u32 | local player, 0 | Attributes the call to a player |

#### `comm.notify` (0x020263B0, arm9)

`void GAME_COMM_CallKindNotify(struct Obj *p, u32 a)`
(`src/matched/arm9/func_020263B0.c`, lines 41 to 50). The shape is the handler's;
the table is `g_0209FB74` (func_020263B0.c:18, 39, 44).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `comm` | u32 | arg `p` (func_020263B0.c:41) | Names the comm object |
| `kind` | u8 | field `Obj.f5` (func_020263B0.c:29, 44) | The index into `g_0209FB74`; the kind notified |
| `state` | u8 | field `Obj.f10` (func_020263B0.c:31, 43) | 5 means the notify is skipped |
| `f4` | u8 | field `Obj.f4` (func_020263B0.c:28) | The record's second byte |
| `work` | u32 | field `Obj.f14` (func_020263B0.c:33, 47) | The notify's first argument |
| `start_result` | u32 | field `Obj.f18` (func_020263B0.c:34, 47) | The notify's second argument |
| `call_arg` | u32 | arg `a` (func_020263B0.c:41, 47) | The notify's third argument |
| `notify` | u32 | call `*(Fn *)(g_0209FB74 + p->f5 * 0x1C)` (func_020263B0.c:44) | The notify pointer |
| `owner` | u32 | local player, 0 | Attributes the notify to a player |

#### `comm.talk.start` (0x0217FB5C, ov018)

`void *EVENT_COMM_COMMON_CreateTalkStartEvent(void *owner, int b, int unused, int d, int e, u16 f)`
(`src/matched/ov/ov018_func_0217FB5C.c`, lines 27 to 39). The work struct is
`Req` (line 20): `f0 = b`, `f4 = d`, `f8 = e`, `fC = f`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event_owner` | u32 | arg `owner` (ov018_func_0217FB5C.c:27, 31) | The game event owner the task hangs from |
| `f0` | u32 | arg `b`, work `f0` (ov018_func_0217FB5C.c:27, 34) | The talk start's first payload word |
| `f4` | u32 | arg `d`, work `f4` (ov018_func_0217FB5C.c:27, 35) | The second payload word |
| `f8` | u32 | arg `e`, work `f8` (ov018_func_0217FB5C.c:27, 36) | The third payload word |
| `fC` | u16 | arg `f`, work `fC` (ov018_func_0217FB5C.c:27, 37) | The fourth payload word |
| `task` | u32 | ret of `func_020122C0` (ov018_func_0217FB5C.c:31, 38) | The task created; a mod tracks the talk |
| `work` | u32 | call `func_020124E8(task)` (ov018_func_0217FB5C.c:32) | The work the payload was written into |
| `main` | u32 | call `EVENT_COMM_COMMON_TalkStartEventMain` (ov018_func_0217FB5C.c:31) | The task main; names the talk |
| `owner` | u32 | local player, 0 | Attributes the talk to a player |

The meaning of `b`, `d`, `e` and `f` is not named. A mod that wants the partner
or the topic reads them as words; the port cannot name them yet.

#### `comm.talked` (0x0217FD34, ov018)

`void *EVENT_COMM_COMMON_CreateTalkedEvent(int a, int b, u8 *c, int d, int e, u16 f)`
(`src/matched/ov/ov018_func_0217FD34.c`, lines 36 to 46).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event_owner` | u32 | arg `a` (ov018_func_0217FD34.c:36, 42) | The game event owner |
| `b` | u32 | arg `b` (ov018_func_0217FD34.c:36, 42) | Forwarded to `InitCommonWork` |
| `d` | u32 | arg `d` (ov018_func_0217FD34.c:36, 42) | Forwarded to `InitCommonWork` |
| `e` | u32 | arg `e` (ov018_func_0217FD34.c:36, 42) | Forwarded to `InitCommonWork` |
| `f` | u16 | arg `f` (ov018_func_0217FD34.c:36, 42) | Forwarded to `InitCommonWork` |
| `source` | u32 | arg `c` (ov018_func_0217FD34.c:36, 42 to 44) | The record the talked data is read from |
| `talked_byte` | u8 | field `c[0x11c]` copied to work +0x41 (ov018_func_0217FD34.c:43) | The talked flag byte |
| `talked_block` | 13 u32 words | field `*(S52 *)(c + 0x120)` copied to work +0x44 (ov018_func_0217FD34.c:23 to 25, 44) | The talked record, the substance of the talk |
| `task` | u32 | ret of `func_020122C0` (ov018_func_0217FD34.c:38) | The task created |
| `work` | u32 | call `func_020124E8(r)` (ov018_func_0217FD34.c:39) | The work the payload was written into |
| `main` | u32 | call `EVENT_COMM_COMMON_TalkedEventMain` (ov018_func_0217FD34.c:38) | The task main |
| `owner` | u32 | local player, 0 | Attributes the talk to a player |

#### `comm.support` (0x02181D8C, ov018)

`void *EVENT_COMM_COMMON_CreateSupportMsgEvent(void *a, void *b)`
(`src/matched/ov/ov018_func_02181D8C.c`, lines 45 to 58).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event_owner` | u32 | arg `a` (ov018_func_02181D8C.c:45, 51) | The game event owner |
| `arg` | u32 | arg `b` (ov018_func_02181D8C.c:45, 54) | The record `MYSTATUS_Create` copies |
| `pms_work` | u32 | call `GAME_DATA_GetPmsWork(GAMESYSTEM_GetGameData(a))` (ov018_func_02181D8C.c:51) | The player support work the active record comes from |
| `status` | u32 | call `MYSTATUS_Create(b)` (ov018_func_02181D8C.c:54) | The 0x20-byte status record of the supporter; a mod reads the owner |
| `active_support` | u32 | call `COMM_PLAYER_SUPPORT_GetActiveRecord(pms_work)` (ov018_func_02181D8C.c:40, 55) | The support record copied into the status; the substance of the support message |
| `task` | u32 | ret of `func_020122C0` (ov018_func_02181D8C.c:52) | The task created |
| `work` | u32 | call `func_020124E8(r)` (ov018_func_02181D8C.c:53) | The work the payload was written into |
| `main` | u32 | call `EVENT_COMM_COMMON_SupportMsgEventMain` (ov018_func_02181D8C.c:52) | The task main |
| `owner` | u32 | local player, 0 | Attributes the support to a player |

#### `comm.offer` (0x02182150, ov018)

`int EVENT_COMM_COMMON_OfferEventMain(void *a, int *state, int c)`
(`src/matched/ov/ov018_func_02182150.c`, lines 38 to 59).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event_owner` | u32 | arg `a` (ov018_func_02182150.c:38, 48) | The game event owner |
| `state` | u32 | field `*state` (ov018_func_02182150.c:38, 45) | The offer state 0 or 1; a mod sees the pause and the answer |
| `offer` | u32 | arg `c` (ov018_func_02182150.c:38, 48) | The offer payload passed to the answer event |
| `game_system` | u32 | call `GAME_EVENT_GetGameSystem(a)` (ov018_func_02182150.c:43) | The game system the offer runs in |
| `field_map` | u32 | call `GAMESYSTEM_GetFieldMap(h)` (ov018_func_02182150.c:44) | The field map the movement pauses on |
| `mmdl_sys` | u32 | call `FIELDMAP_GetMmdlSys(r)` (ov018_func_02182150.c:47) | The model system paused and resumed |
| `answer_event` | u32 | call `EVENT_COMM_COMMON_CreateOfferAnswerEvent(h, c)` (ov018_func_02182150.c:48) | The answer event created; a mod tracks the offer exchange |
| `paused` | u8 | field `*state == 0` (ov018_func_02182150.c:46 to 49) | The movement is paused; a mod knows the field is frozen |
| `resumed` | u8 | field `*state == 1` and `FIELDMAP_IsReady` (ov018_func_02182150.c:51 to 54) | The movement resumed; the offer resolved |
| `result` | u32 | ret of `EVENT_COMM_COMMON_OfferEventMain` (ov018_func_02182150.c:54, 58) | 1 when the offer step finished |
| `owner` | u32 | local player, 0 | Attributes the offer to a player |

The offer's content and the peer are inside `c`, which the offer answer event
reads. The port cannot name them from this function alone.

#### `comm.buy` (0x0218261C, ov018)

`int EVENT_COMM_COMMON_ItemPurchaseEventMain(void *a, int *state, Outer *c)`
(`src/matched/ov/ov018_func_0218261C.c`, lines 93 to 207). The work struct is
`Outer` (lines 49 to 59): `p10` is the word-set, `b42` the message index,
`h58[2]` the item id and price, `f78` the buyer status, `f98` the no-work flag.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event_owner` | u32 | arg `a` (ov018_func_0218261C.c:93, 103) | The game event owner |
| `state` | u32 | field `*state` (ov018_func_0218261C.c:93, 119) | The purchase state; a mod sees the choice and the reply |
| `item_id` | u16 | field `Outer.h58[0]` (ov018_func_0218261C.c:55, 106, 122) | The item bought; the substance of the purchase |
| `price` | u16 | field `Outer.h58[1]` (ov018_func_0218261C.c:55, 106, 123) | The price; a mod checks it against the money |
| `buyer_status` | u32 | call `GAME_DATA_GetMyStatus(GAMESYSTEM_GetGameData(sub))` (ov018_func_0218261C.c:124) | The buyer's status record; the owner of the item |
| `money` | u32 | call `BEACON_GetPoints(GAME_DATA_GetMiscSave(x))` (ov018_func_0218261C.c:146) | The money before the purchase |
| `money_after` | u32 | call `BEACON_SubPoints(...)` (ov018_func_0218261C.c:168) | The money after the purchase |
| `can_add` | u32 | call `func_02007E3C(GAME_DATA_GetMyItem(x), h[0], 1, 0x15)` (ov018_func_0218261C.c:149) | Whether the bag can take the item |
| `item_added` | u32 | call `func_02007E50(GAME_DATA_GetMyItem(x), h[0], 1, 0x15)` (ov018_func_0218261C.c:169) | The item handed over |
| `choice` | u32 | call `EVENT_COMM_COMMON_PollChoice(...)` (ov018_func_0218261C.c:141) | The player's yes or no |
| `reply` | u32 | call `INTRUDE_COMM_SendReply(v, v[0x154], code, v[0x158])` (ov018_func_0218261C.c:167, 175, 181, 187) | The answer sent to the peer; the code is the outcome |
| `peer_token` | u32 | field `v[0x154]` (ov018_func_0218261C.c:167) | The request token identifying the peer request |
| `intrude_work` | u32 | call `INTRUDE_COMM_GetWorkIfRunning(t)` (ov018_func_0218261C.c:107) | The intrusion work; 0 means the peer is gone |
| `comm` | u32 | call `GAMESYSTEM_GetGameComm(sub)` (ov018_func_0218261C.c:105) | The comm object behind the purchase |
| `game_data` | u32 | call `GAMESYSTEM_GetGameData(sub)` (ov018_func_0218261C.c:104) | The game data the money and bag come from |
| `message_index` | u8 | field `Outer.b42` (ov018_func_0218261C.c:53, 125) | The message row the purchase shows |
| `no_work` | u8 | field `Outer.f98` (ov018_func_0218261C.c:58, 115) | The flag set when the peer work is gone |
| `result` | u32 | ret of `EVENT_COMM_COMMON_ItemPurchaseEventMain` (ov018_func_0218261C.c:204) | 1 when the purchase step finished |
| `owner` | u32 | local player, 0 | Attributes the purchase to a player |
| `peer` | u32 | field `v[0x154]` (ov018_func_0218261C.c:167) | The peer request, from the intrusion work |

#### `comm.sell` (0x021823A8, ov018)

`int EVENT_COMM_COMMON_ItemSaleEventMain(void *a, int *state, Outer *c)`
(`src/matched/ov/ov018_func_021823A8.c`, lines 86 to 195). The work struct is
`Outer` (lines 44 to 56): `h58[2]` the item id and count, `f98` and `f9C` the
result flags, `fA0` the answer flag.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event_owner` | u32 | arg `a` (ov018_func_021823A8.c:86, 92) | The game event owner |
| `state` | u32 | field `*state` (ov018_func_021823A8.c:86, 105) | The sale state; a mod sees the proposal and the answer |
| `item_id` | u16 | field `Outer.h58[0]` (ov018_func_021823A8.c:50, 108) | The item offered; the substance of the sale |
| `count` | u16 | field `Outer.h58[1]` (ov018_func_021823A8.c:50, 110) | The offered count |
| `seller_status` | u32 | call `GAME_DATA_GetMyStatus(GAMESYSTEM_GetGameData(sub))` (ov018_func_021823A8.c:111) | The seller's status record; the owner of the item |
| `request_result` | u32 | call `INTRUDE_COMM_TakeRequestResult(v)` (ov018_func_021823A8.c:117) | The peer's answer code |
| `send_answer` | u32 | call `INTRUDE_COMM_SendAnswer(v[0x154], v[0x158])` (ov018_func_021823A8.c:132) | The answer sent to the peer |
| `send_proposal` | u32 | call `INTRUDE_CMD_SendProposal(v, v + 0x1AC)` (ov018_func_021823A8.c:146) | The proposal sent to the peer |
| `join_status` | u32 | call `INTRUDE_COMM_GetJoinStatus(v, v + 0x1AC)` (ov018_func_021823A8.c:151) | Whether the peer joined |
| `peer_token` | u32 | field `v[0x154]` (ov018_func_021823A8.c:132) | The request token identifying the peer request |
| `intrude_work` | u32 | call `INTRUDE_COMM_GetWorkIfRunning(GAMESYSTEM_GetGameComm(sub))` (ov018_func_021823A8.c:93) | The intrusion work; 0 means the peer is gone |
| `comm` | u32 | call `GAMESYSTEM_GetGameComm(sub)` (ov018_func_021823A8.c:93) | The comm object behind the sale |
| `game_data` | u32 | call `GAMESYSTEM_GetGameData(sub)` (ov018_func_021823A8.c:111) | The game data the status comes from |
| `message_index` | u8 | field `Outer.b42` (ov018_func_021823A8.c:48, 112) | The message row the sale shows |
| `no_work` | u8 | field `Outer.f98` (ov018_func_021823A8.c:53, 101) | The flag set when the peer work is gone |
| `mission_result` | u8 | field `Outer.f9C` (ov018_func_021823A8.c:54, 152, 183) | The flag that routes to the mission result event |
| `answered` | u8 | field `Outer.fA0` (ov018_func_021823A8.c:55, 134, 187) | The flag that routes to a disconnect without an answer |
| `result` | u32 | ret of `EVENT_COMM_COMMON_ItemSaleEventMain` (ov018_func_021823A8.c:194) | 1 when the sale step finished |
| `owner` | u32 | local player, 0 | Attributes the sale to a player |
| `peer` | u32 | field `v[0x154]` (ov018_func_021823A8.c:132) | The peer request, from the intrusion work |

### Wm

No responder answers Wm, and that is deliberate (`docs/modding.md`, "Networking
seam: missing", lines 149 to 158). The wm payloads are specified against the
matched sources so the struct is frozen before the transport lands.

The wireless manager keeps its work in `WMArm9Buf`
(`src/matched/arm9_sdk/WmReceiveFifo.c`, lines 59 to 74): the local station id,
the connected bitmap, the callback table and the port callback table and
arguments. The status is `WMStatus` (`WM_Init.c`, lines 38 to 42).

#### `wm.init` (0x0208E010, arm9)

`WMErrCode WM_Init(void *wmSysBuf, u16 dmaNo)`
(`src/matched/arm9_sdk/WM_Init.c`, lines 81 to 89).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `wm_sys_buf` | u32 | arg `wmSysBuf` (WM_Init.c:81) | The caller's work buffer the manager uses |
| `dma_no` | u16 | arg `dmaNo` (WM_Init.c:81) | The DMA channel the manager takes |
| `buf_size` | u32 | call `WmInitCore(..., 0xF00)` (WM_Init.c:83) | The size the core is given; a mod checks the layout |
| `scan_only` | u16 | field `WMArm9Buf.scanOnlyFlag` (WM_Init.c:51, 86) | Cleared on success; a mod reads whether the manager scans only |
| `result` | u32 | ret of `WM_Init` (WM_Init.c:83, 88) | `WM_ERRCODE_SUCCESS` when the manager initialised, else a code |
| `owner` | u32 | local instance, 0 | Attributes the init to an instance |

#### `wm.enable` (0x0208F224, arm9)

`int WM_Enable(char *path)` (`src/matched/arm9_sdk/func_0208F224.c`, lines 21
to 24). It tail-calls `WMi_EnableEx(path, 0)`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `path` | u32 | arg `path` (func_0208F224.c:21) | The firmware or data path the enable reads |
| `format_media` | u32 | arg literal 0 (func_0208F224.c:23) | The enable does not format |
| `result` | u32 | ret of `WMi_EnableEx` (func_0208F224.c:23) | The enable result |
| `owner` | u32 | local instance, 0 | Attributes the enable to an instance |

#### `wm.connect` (0x0208F84C, arm9)

`WMErrCode WM_StartConnectEx(WMCallbackFunc callback, WMBssDesc *pInfo, u8 *ssid, BOOL powerSave, u16 authMode)`
(`src/matched/arm9_sdk/func_0208F84C.c`, lines 95 to 139). The beacon is
`WMBssDesc` (lines 66 to 73) and the request is `WMStartConnectReq` (lines 75 to
83).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `callback` | u32 | arg `callback` (func_0208F84C.c:95, 122) | The callback the connect result reaches |
| `bss_desc` | u32 | arg `pInfo` (func_0208F84C.c:95, 116) | The beacon the connect joins; a mod reads the network identity |
| `bssid` | u8[6] | field `WMBssDesc.bssid` (func_0208F84C.c:69) | The access point address |
| `beacon_ssid` | u8[32] | field `WMBssDesc.ssid` (func_0208F84C.c:71) | The network name |
| `beacon_ssid_length` | u16 | field `WMBssDesc.ssidLength` (func_0208F84C.c:70) | The name length |
| `rssi` | u16 | field `WMBssDesc.rssi` (func_0208F84C.c:68) | The signal of the beacon |
| `length` | u16 | field `WMBssDesc.length` (func_0208F84C.c:67, 116) | The beacon's word count, used for the store |
| `ssid` | u8[24] | arg `ssid`, copied (func_0208F84C.c:95, 127) | The requested network name |
| `power_save` | u32 | arg `powerSave` (func_0208F84C.c:95, 131) | Whether power save is requested |
| `auth_mode` | u16 | arg `authMode` (func_0208F84C.c:95, 132) | The authentication mode |
| `apiid` | u16 | field `WMStartConnectReq.apiid` = 12 (func_0208F84C.c:76, 124) | The command id; a mod matches the callback |
| `my_aid` | u16 | field `WMArm9Buf.myAid` set to 0 (func_0208F84C.c:62, 119) | The station id resets on connect |
| `connected_aid_bitmap` | u32 | field `WMArm9Buf.connectedAidBitmap` set to 0 (func_0208F84C.c:61, 120) | The group resets on connect |
| `result` | u32 | ret of `WM_StartConnectEx` (func_0208F84C.c:101, 134, 138) | `WM_ERRCODE_OPERATING` when the command is sent |
| `owner` | u32 | local instance, 0 | Attributes the connect to an instance |

#### `wm.mp.start` (0x0208FC4C, arm9)

`WMErrCode WM_StartMP(WMCallbackFunc callback, u16 *recvBuf, u16 recvBufSize, u16 *sendBuf, u16 sendBufSize, u16 mpFreq)`
(`src/matched/arm9_sdk/func_0208FC4C.c`, lines 74 to 84). The temporary
parameters are `WMMPTmpParam` (lines 59 to 69).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `callback` | u32 | arg `callback` (func_0208FC4C.c:74, 83) | The callback the multipoint frames reach |
| `recv_buf` | u32 | arg `recvBuf` (func_0208FC4C.c:74) | The receive buffer; a mod reads frames there |
| `recv_buf_size` | u16 | arg `recvBufSize` (func_0208FC4C.c:74) | The receive budget |
| `send_buf` | u32 | arg `sendBuf` (func_0208FC4C.c:74) | The send buffer |
| `send_buf_size` | u16 | arg `sendBufSize` (func_0208FC4C.c:74) | The send budget |
| `mp_freq` | u16 | arg `mpFreq` (func_0208FC4C.c:74, 80 to 81) | The frequency, also the minimum and the set frequency |
| `mask` | u32 | field `WMMPTmpParam.mask` = 3 (func_0208FC4C.c:60, 79) | The parameters set |
| `frequency` | u16 | field `WMMPTmpParam.frequency` (func_0208FC4C.c:62, 81) | The chosen frequency |
| `min_frequency` | u16 | field `WMMPTmpParam.minFrequency` (func_0208FC4C.c:61, 80) | The lower bound |
| `default_retry` | u16 | field `WMMPTmpParam.defaultRetryCount` (func_0208FC4C.c:64) | The retry count |
| `result` | u32 | ret of `WMi_StartMP` (func_0208FC4C.c:83) | The start result |
| `owner` | u32 | local instance, 0 | Attributes the multipoint start to an instance |

#### `wm.mp.send` (0x0208FCB4, arm9)

`WMErrCode WM_SetMPDataToPortEx(WMCallbackFunc callback, void *arg, u16 *sendData, u16 sendDataSize, u16 destBitmap, u16 port, u16 prio)`
(`src/matched/arm9_sdk/WM_SetMPDataToPortEx.c`, lines 73 to 119). The status is
`WMStatus` (lines 48 to 60).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `callback` | u32 | arg `callback` (WM_SetMPDataToPortEx.c:73, 113) | The callback the send result reaches |
| `arg` | u32 | arg `arg` (WM_SetMPDataToPortEx.c:73, 114) | The callback's argument |
| `send_data` | u32 | arg `sendData` (WM_SetMPDataToPortEx.c:73, 101, 113) | The bytes that leave |
| `send_bytes` | u8[] | arg `sendData`, length `sendDataSize` (WM_SetMPDataToPortEx.c:73, 111) | The substance of the send |
| `send_data_size` | u16 | arg `sendDataSize` (WM_SetMPDataToPortEx.c:73, 104, 107, 113) | The byte count, capped at 512 |
| `dest_bitmap` | u16 | arg `destBitmap` (WM_SetMPDataToPortEx.c:74, 113) | Which stations are addressed |
| `port` | u16 | arg `port` (WM_SetMPDataToPortEx.c:74, 113) | The port the bytes go to |
| `prio` | u16 | arg `prio` (WM_SetMPDataToPortEx.c:74, 113) | The send priority |
| `aid` | u16 | field `WMStatus.aid` (WM_SetMPDataToPortEx.c:59, 86 to 87) | The local station id; 0 means the parent |
| `child_bitmap` | u16 | field `WMStatus.child_bitmap` (WM_SetMPDataToPortEx.c:57, 88 to 89) | The children, when the station is the parent |
| `mp_ready_bitmap` | u16 | field `WMStatus.mp_readyBitmap` (WM_SetMPDataToPortEx.c:55, 90) | The children ready to receive |
| `send_buf` | u32 | field `WMStatus.mp_sendBuf` (WM_SetMPDataToPortEx.c:51, 101) | The manager's own buffer; a mod must not alias it |
| `apiid` | u32 | call `WMi_SendCommand(WM_APIID_SET_MP_DATA, ...)` = 15 (WM_SetMPDataToPortEx.c:42, 113) | The command id; a mod matches the callback |
| `result` | u32 | ret of `WM_SetMPDataToPortEx` (WM_SetMPDataToPortEx.c:81, 82, 115, 118) | `WM_ERRCODE_OPERATING` when the command is queued |
| `owner` | u32 | local instance, 0 | Attributes the send to an instance |

#### `wm.share.start` (0x0209004C, arm9)

`WMErrCode WM_StartDataSharing(WMDataSharingInfo *dsInfo, u16 port, u16 aidBitmap, u16 dataLength, BOOL doubleMode)`
(`src/matched/arm9_sdk/WM_StartDataSharing.c`, lines 85 to 160). The sharing
state is `WMDataSharingInfo` (lines 56 to 71).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `ds_info` | u32 | arg `dsInfo` (WM_StartDataSharing.c:85, 112) | The sharing state object |
| `port` | u16 | arg `port` (WM_StartDataSharing.c:85, 118) | The port sharing runs on |
| `aid_bitmap` | u16 | arg `aidBitmap` (WM_StartDataSharing.c:85, 121) | The stations in the group |
| `data_length` | u16 | arg `dataLength` (WM_StartDataSharing.c:85, 117) | The per-station data size |
| `double_mode` | u16 | arg `doubleMode` (WM_StartDataSharing.c:85, 119) | The double-buffer mode |
| `my_aid` | u16 | call `WM_GetAID()` (WM_StartDataSharing.c:107) | The local station id; 0 means the parent |
| `connected_aids` | u16 | call `WM_GetConnectedAIDs()` (WM_StartDataSharing.c:109) | The connected stations |
| `is_parent` | u8 | call `WM_GetAID() == 0` (WM_StartDataSharing.c:108) | The parent installs the receive callback and sends the first sets |
| `station_number` | u16 | call `MATH_CountPopulation(aidBitmap)` (WM_StartDataSharing.c:123, 125) | How many stations share |
| `data_set_length` | u16 | call `dataLength * count + 4` (WM_StartDataSharing.c:124, 130) | The whole data set size |
| `state` | u16 | field `WMDataSharingInfo.state` = 1 (WM_StartDataSharing.c:69, 131) | The sharing state; a mod sees it start |
| `callback` | u32 | call `WM_SetPortCallback(port, ...)` (WM_StartDataSharing.c:140, 157) | The receive callback installed |
| `result` | u32 | ret of `WM_StartDataSharing` (WM_StartDataSharing.c:94, 152, 159) | The start result |
| `owner` | u32 | local instance, 0 | Attributes the sharing to an instance |

#### `wm.share.addr` (0x02090B18, arm9, state)

`u16 *WM_GetSharedDataAddress(WMDataSharingInfo *dsInfo, WMDataSet *receiveData, u16 aid)`
(`src/matched/arm9_sdk/WM_GetSharedDataAddress.c`, lines 63 to 84). A state
accessor.

| Accessor | Inputs | Output | Where it comes from | Why a mod wants it |
|---|---|---|---|---|
| `bw_share_addr` | sharing info, data set, `aid` | u32 pointer | `WmGetSharedDataAddress(dsInfo, aidBitmap, receiveData->data, aid)` (WM_GetSharedDataAddress.c:83) | The address of one station's contribution; a mod reads the peer's shared data |

Fields the accessor exposes: `aid` (arg, line 63), `aid_bitmap` (field
`WMDataSet.aidBitmap`, line 75), `received_bitmap` (field
`WMDataSet.receivedBitmap`, line 76), `data_length` (field
`WMDataSharingInfo.dataLength`, line 51), `station_number` (field, line 52),
`port` (field, line 54), `double_mode` (field, line 55), `write_index` (field,
line 47), `read_index` (field, line 49).

#### `wm.share.send` (0x020909C8, arm9)

`void WmDataSharingSendDataSet(WMDataSharingInfo *dsInfo, BOOL delayed)`
(`src/matched/arm9_sdk/WmDataSharingSendDataSet.c`, lines 79 to 119).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `ds_info` | u32 | arg `dsInfo` (WmDataSharingSendDataSet.c:79, 107) | The sharing state |
| `delayed` | u8 | arg `delayed` (WmDataSharingSendDataSet.c:79, 102) | Whether the parent's own bit is cleared |
| `write_index` | u16 | field `WMDataSharingInfo.writeIndex` (WmDataSharingSendDataSet.c:55, 83, 91) | The set being written |
| `next_write_index` | u16 | call `(writeIndex + 1) % 4` (WmDataSharingSendDataSet.c:92, 100) | The set prepared next |
| `connected_aids` | u16 | call `WM_GetConnectedAIDs()` (WmDataSharingSendDataSet.c:90) | The stations to send to |
| `aid_bitmap` | u16 | field `WMDataSharingInfo.aidBitmap` (WmDataSharingSendDataSet.c:58, 99, 109) | The sharing group |
| `double_mode` | u16 | field `WMDataSharingInfo.doubleMode` (WmDataSharingSendDataSet.c:63, 93) | The double-buffer mode |
| `port` | u16 | field `WMDataSharingInfo.port` (WmDataSharingSendDataSet.c:62, 109) | The port the set goes to |
| `data_set_length` | u16 | field `WMDataSharingInfo.dataSetLength` (WmDataSharingSendDataSet.c:61, 108) | The set size |
| `dest_bitmap` | u16 | call `aidBitmap & connectedAIDs` (WmDataSharingSendDataSet.c:109) | Which stations receive |
| `bytes` | u8[] | field `ds[oldWI]`, length `dataSetLength` (WmDataSharingSendDataSet.c:101, 108) | The data set that leaves; the substance of the send |
| `result` | u32 | ret of `WM_SetMPDataToPortEx` (WmDataSharingSendDataSet.c:107, 110 to 114) | The send result; `NO_ENTRY` advances the send index |
| `send_index` | u16 | field `WMDataSharingInfo.sendIndex` (WmDataSharingSendDataSet.c:56, 112) | The next set to send |
| `seq_num` | u16 | field `WMDataSharingInfo.seqNum[oldWI]` (WmDataSharingSendDataSet.c:54, 111) | The sequence number, 0xFFFF on no entry |
| `state` | u16 | field `WMDataSharingInfo.state` = 5 on error (WmDataSharingSendDataSet.c:65, 114) | The error state |
| `owner` | u32 | local instance, 0 | Attributes the send to an instance |

#### `wm.share.recv` (0x020908F8, arm9)

`void WmDataSharingReceiveData(WMDataSharingInfo *dsInfo, u16 aid, u16 *data)`
(`src/matched/arm9_sdk/WmDataSharingReceiveData.c`, lines 75 to 107).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `ds_info` | u32 | arg `dsInfo` (WmDataSharingReceiveData.c:75, 96) | The sharing state |
| `aid` | u16 | arg `aid` (WmDataSharingReceiveData.c:75, 77) | The station that contributed; the peer |
| `data` | u32 | arg `data` (WmDataSharingReceiveData.c:75, 97) | The contribution, or 0 to clear |
| `recv_bytes` | u8[] | arg `data`, length `dataLength` (WmDataSharingReceiveData.c:98) | The bytes received; the substance of the receive |
| `write_index` | u16 | field `WMDataSharingInfo.writeIndex` (WmDataSharingReceiveData.c:53, 85) | The set written |
| `double_mode` | u16 | field `WMDataSharingInfo.doubleMode` (WmDataSharingReceiveData.c:61, 87) | The double-buffer mode |
| `data_length` | u16 | field `WMDataSharingInfo.dataLength` (WmDataSharingReceiveData.c:57, 98, 100) | The contribution size |
| `aid_bitmap` | u16 | field `WMDataSharingInfo.aidBitmap` (WmDataSharingReceiveData.c:56, 82, 96) | The sharing group |
| `received_bitmap` | u16 | field `WMDataSet.receivedBitmap` after the update (WmDataSharingReceiveData.c:45, 105) | Which stations have contributed |
| `peer` | u32 | arg `aid` (WmDataSharingReceiveData.c:75) | The contributing station |
| `owner` | u32 | local instance, 0 | Attributes the receive to an instance |

#### `wm.fifo` (0x0208E5BC, arm9)

`void WmReceiveFifo(int tag, u32 fifo_buf_adr, BOOL err)`
(`src/matched/arm9_sdk/WmReceiveFifo.c`, lines 195 to 343). The base callback is
`WMCallback` (lines 86 to 91); the port receive is `WMPortRecvCallback` (lines
160 to 179), the port send is `WMPortSendCallback` (lines 137 to 158), the
multipoint start is `WMStartMPCallback` (lines 121 to 135), and the parent and
connect callbacks are at lines 93 to 119.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `tag` | u32 | arg `tag` (WmReceiveFifo.c:195) | The PXI tag, 10 for wireless |
| `fifo_buf` | u32 | arg `fifo_buf_adr` (WmReceiveFifo.c:195, 197) | The frame buffer the callback reads |
| `err` | u8 | arg `err` (WmReceiveFifo.c:195, 200) | The error flag; the handler returns at once when set |
| `apiid` | u16 | field `WMCallback.apiid` (WmReceiveFifo.c:87, 212) | The callback id, which selects the variant |
| `errcode` | u16 | field `WMCallback.errcode` (WmReceiveFifo.c:88, 214, 239, 245) | The result code |
| `wl_cmd_id` | u16 | field `WMCallback.wlCmdID` (WmReceiveFifo.c:89) | The wireless command id |
| `wl_result` | u16 | field `WMCallback.wlResult` (WmReceiveFifo.c:90) | The wireless result |
| `port` | u16 | field `WMPortRecvCallback.port` (WmReceiveFifo.c:164, 222) | The port the frame belongs to |
| `recv_buf` | u32 | field `WMPortRecvCallback.recvBuf` (WmReceiveFifo.c:165, 225) | The receive buffer |
| `recv_bytes` | u8[] | field `WMPortRecvCallback.data`, `length` (WmReceiveFifo.c:166 to 167) | The bytes received; the substance of a receive |
| `aid` | u16 | field `WMPortRecvCallback.aid` (WmReceiveFifo.c:168, 311) | The sending station; the peer |
| `mac` | u8[6] | field `WMPortRecvCallback.macAddress` (WmReceiveFifo.c:169, 316) | The sender's address |
| `seq_no` | u16 | field `WMPortRecvCallback.seqNo` (WmReceiveFifo.c:170, 314) | The frame sequence |
| `port_arg` | u32 | field `WMPortRecvCallback.arg` (WmReceiveFifo.c:171, 223, 328) | The callback's argument, from the port table |
| `my_aid` | u16 | field `WMArm9Buf.myAid` (WmReceiveFifo.c:72, 302) | The local station id |
| `connected_aid_bitmap` | u16 | field `WMArm9Buf.connectedAidBitmap` (WmReceiveFifo.c:71, 224, 313) | The group after the update |
| `ssid` | u8[24] | field `WMPortRecvCallback.ssid` (WmReceiveFifo.c:174, 318) | The network name on a state change |
| `reason` | u16 | field `WMPortRecvCallback.reason` (WmReceiveFifo.c:175, 315) | The reason code on a state change |
| `rssi` | u16 | field `WMPortRecvCallback.rssi` (WmReceiveFifo.c:176) | The signal |
| `max_send`, `max_recv` | u16 | field `WMPortRecvCallback.maxSendDataSize`, `maxRecvDataSize` (WmReceiveFifo.c:177 to 178, 322 to 323) | The frame budget |
| `state` | u16 | field `WMStartParentCallback.state` or `WMStartConnectCallback.state` (WmReceiveFifo.c:98, 112, 275, 285) | The connect state; 7, 9 and 26 are the change points |
| `src_mac` | u8[6] | field `WMStartConnectCallback.macAddress` (WmReceiveFifo.c:116, 288) | The parent's address |
| `dest_adrs` | u8[6] | field `WMStartMPCallback.destAdrs` (WmReceiveFifo.c:129) | The multipoint destination |
| `src_adrs` | u8[6] | field `WMStartMPCallback.srcAdrs` (WmReceiveFifo.c:130) | The multipoint source |
| `poll_bitmap` | u16 | field `WMStartMPCallback.pollbmp` (WmReceiveFifo.c:133) | The multipoint poll set |
| `time_stamp` | u16 | field `WMStartMPCallback.timeStamp` (WmReceiveFifo.c:127) | The frame time |
| `rate_rssi` | u16 | field `WMStartMPCallback.rate_rssi` (WmReceiveFifo.c:128) | The rate and signal |
| `send_data` | u32 | field `WMPortSendCallback.data` (WmReceiveFifo.c:148) | The bytes a send completed with |
| `dest_bitmap` | u16 | field `WMPortSendCallback.destBitmap` (WmReceiveFifo.c:144) | The send targets |
| `sent_bitmap` | u16 | field `WMPortSendCallback.sentBitmap` (WmReceiveFifo.c:146) | The stations already sent to |
| `rest_bitmap` | u16 | field `WMPortSendCallback.restBitmap` (WmReceiveFifo.c:145) | The stations left |
| `direction` | u8 | ctx, apiid 0x82 is receive and 0x81 is send (WmReceiveFifo.c:220 to 233) | One field for both directions |
| `peer` | u32 | field `aid` (WmReceiveFifo.c:168) | The station the frame is about |
| `owner` | u32 | local instance, 0 | Attributes the frame to an instance |

#### `wm.callback` (0x0208EB0C, arm9, override)

`WMErrCode WM_SetPortCallback(u16 port, WMCallbackFunc callback, void *arg)`
(`src/matched/arm9_sdk/WM_SetPortCallback.c`, lines 98 to 137). An override
point.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `port` | u16 | arg `port` (WM_SetPortCallback.c:98, 110, 128) | The port the callback is for |
| `callback` | u32 | arg `callback` (WM_SetPortCallback.c:98, 128) | The callback installed, or 0 to clear |
| `arg` | u32 | arg `arg` (WM_SetPortCallback.c:98, 115, 129) | The callback's argument |
| `apiid` | u16 | field `WMPortRecvCallback.apiid` = 0x82 (WM_SetPortCallback.c:69, 107) | The init notification id |
| `state` | u16 | field `WMPortRecvCallback.state` = 25 (WM_SetPortCallback.c:71, 109) | The init state; a mod distinguishes the init call from a frame |
| `mac` | u8[6] | call `OS_GetMacAddress(...)` (WM_SetPortCallback.c:90, 117) | The local address |
| `connected_aid_bitmap` | u16 | call `WM_GetConnectedAIDs()` (WM_SetPortCallback.c:95, 131) | The group at install |
| `my_aid` | u16 | call `WM_GetAID()` (WM_SetPortCallback.c:96, 132) | The local station id |
| `result` | u32 | ret of `WM_SetPortCallback` (WM_SetPortCallback.c:121, 124, 136) | The install result |
| `owner` | u32 | local instance, 0 | Attributes the install to an instance |

### Dwc

`DWCi_Init` (0x02059FA8) is the WiFi interface initialiser and
`DWC_InitForProduction` (0x02059F6C) is the production wrapper. No responder
answers DWC (`docs/modding.md`, "Networking seam: missing").

#### `dwc.init` (0x02059FA8, arm9)

`int DWCi_Init(int authserver, const char *gameName, u32 gameCode, DWCAllocEx alloc, DWCFreeEx free)`
(`src/matched/arm9_sdk/func_02059FA8.c`, lines 73 to 116).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `authserver` | u32 | arg `authserver` (func_02059FA8.c:73, 105, 110) | Which authentication server; 0 is development and 1 production |
| `game_name` | u32 | arg `gameName` (func_02059FA8.c:73, 108) | The game name copied into the global |
| `game_code` | u32 | arg `gameCode` (func_02059FA8.c:73, 106) | The game code, a WiFi identity; a mod reads it for a session |
| `alloc` | u32 | arg `alloc` (func_02059FA8.c:73, 80) | The allocator the interface uses |
| `free` | u32 | arg `free` (func_02059FA8.c:73, 80) | The deallocator |
| `work` | u32 | call `DWC_Alloc(...)` (func_02059FA8.c:84) | The init work buffer |
| `backup_result` | u32 | call `DWC_BM_Init(work)` (func_02059FA8.c:87) | The backup manager result |
| `wifi_id_created` | u8 | call `DWC_Auth_CheckWiFiIDNeedCreate()` (func_02059FA8.c:88, 90) | Whether a WiFi ID was made; a mod must not duplicate it |
| `stats_host` | u32 | call, the `StatsServerHostname` global (func_02059FA8.c:69, 110 to 113) | The statistics host chosen by the auth server |
| `result` | u32 | ret of `DWCi_Init` (func_02059FA8.c:93 to 103, 115) | 0 to 4, where 0 is a clean init and 1 to 3 report a created ID or a failure |
| `owner` | u32 | local instance, 0 | Attributes the init to an instance |

#### `dwc.production` (0x02059F6C, arm9)

`int DWC_InitForProduction(const char *gameName, u32 gameCode, DWCAllocEx alloc, DWCFreeEx free)`
(`src/matched/arm9_sdk/func_02059F6C.c`, lines 31 to 35). It forwards to
`DWCi_Init` with the production auth server.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `game_name` | u32 | arg `gameName` (func_02059F6C.c:31, 34) | Forwarded to `DWCi_Init` |
| `game_code` | u32 | arg `gameCode` (func_02059F6C.c:31, 34) | Forwarded to `DWCi_Init` |
| `alloc` | u32 | arg `alloc` (func_02059F6C.c:31, 34) | Forwarded to `DWCi_Init` |
| `free` | u32 | arg `free` (func_02059F6C.c:31, 34) | Forwarded to `DWCi_Init` |
| `authserver` | u32 | call, `DWC_AUTHSERVER_PRODUCTION` = 1 (func_02059F6C.c:25, 34) | The production server, so a mod distinguishes it from `dwc.init` |
| `result` | u32 | ret of `DWCi_Init` (func_02059F6C.c:34) | The init result, as in `dwc.init` |
| `owner` | u32 | local instance, 0 | Attributes the init to an instance |

### Network script commands

The five entries are the field VM's network commands. The comm handle they read
is `GAMESYSTEM_GetGameComm` (0x02012138) and the kind is byte +5
(`src/matched/arm9/unit_02026454.c`, lines 47 to 54).

#### `network.wait` (0x0215F1C8, ov010, state)

`int SCRCMD_NETWORK_CmdWaitCommState(void *a, void *b)`
(`src/matched/ov/ov010_func_0215F1C8.c`, lines 33 to 47). A state accessor: it
sets the VM to wait on `SCRCMD_NETWORK_WaitCommState` (0x0215F168).

| Accessor | Inputs | Output | Where it comes from | Why a mod wants it |
|---|---|---|---|---|
| `bw_comm_wait_state` | script, VM | u32 state | `SCRCMD_NETWORK_WaitCommState` reads `FIELDMAP_CheckCommExit(comm)` (func_0215F168.c:53, 62) | The comm exit state 0, 1 or 2 a script waits on |

Fields exposed by the accessor: `script` (arg `b`, ov010_func_0215F1C8.c:33),
`work_ptr` (call `SCRIPT_WORK_ReadWorkPointer`, line 40), `slot` (call
`SCRIPT_WORK_GetUserWorkPtr`, line 39), `wait_fn` (call, the wait function
pointer, line 45), `comm` (call `GAMESYSTEM_GetGameComm`, func_0215F168.c:53),
`field_map` (call `GAMESYSTEM_GetFieldMap`, func_0215F168.c:56), `exit_state`
(call `FIELDMAP_CheckCommExit`, func_0215F168.c:62).

#### `network.value` (0x0215F214, ov010, state)

`int SCRCMD_NETWORK_CmdGetCommValue(int a0, int a1)`
(`src/matched/ov/func_0215F214.c`, lines 26 to 35). A state accessor.

| Accessor | Inputs | Output | Where it comes from | Why a mod wants it |
|---|---|---|---|---|
| `bw_comm_value` | script, VM | u16 value | `APP_ENV_InitParentalControl()` stored at work `f0` (func_0215F214.c:31 to 33) | The value a script reads from the comm work |

Fields: `work_ptr` (call `SCRIPT_WORK_ReadWorkPointer`, line 29), `value` (call
`APP_ENV_InitParentalControl`, line 31, stored at `Obj17703.f0`, line 33).

#### `network.start` (0x0215F228, ov010)

`int SCRCMD_NETWORK_CmdStartAutoComm(int a0, int a1)`
(`src/matched/ov/func_0215F228.c`, lines 25 to 35).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `script` | u32 | arg `a1` (func_0215F228.c:25, 29) | The script frame that runs the command |
| `game_system` | u32 | call `SCRCMD_WORK_GetGameSystem(a1)` (func_0215F228.c:30) | The game system the comm starts in |
| `start_result` | u32 | call `GAMESYSTEM_TryStartAutoComm(v0)` (func_0215F228.c:32) | 1 when auto comm started; a mod sees the start |
| `result` | u32 | ret of the command (func_0215F228.c:34) | Always 1; the command itself succeeded |
| `owner` | u32 | local player, 0 | Attributes the start to a player |

`GAMESYSTEM_TryStartAutoComm` returns 1 only when the run state is 0, the kind
is 0 and `GAMESYSTEM_CanStartAutoComm` is 1, then calls
`GAME_COMM_Start(handle, 1, p)` (`src/matched/arm9/unit_020121CC.c`, lines 47 to
60). The port reads those three tests from the tracked comm context.

#### `network.exit` (0x0215F238, ov010)

`int SCRCMD_NETWORK_CmdRequestCommExit(void *a, void *b)`
(`src/matched/ov/ov010_func_0215F238.c`, lines 33 to 54).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `script` | u32 | arg `b` (ov010_func_0215F238.c:33, 38) | The script frame that runs the command |
| `game_system` | u32 | call `SCRCMD_WORK_GetGameSystem(b)` (ov010_func_0215F238.c:38) | The game system the comm exits from |
| `comm` | u32 | call `GAMESYSTEM_GetGameComm(t)` (ov010_func_0215F238.c:39) | The comm object |
| `kind` | u32 | call `GAME_COMM_GetKind(s)` (ov010_func_0215F238.c:40) | The request kind; 1, 2 and 5 exit, 0, 3 and 4 do not |
| `exited` | u8 | call, the switch on kind (ov010_func_0215F238.c:41 to 45) | Whether the exit was requested; a mod mirrors it to the peer |
| `blocked` | u32 | call `GAMESYSTEM_SetCommBlocked(t, 1)` (ov010_func_0215F238.c:51) | The comm is blocked after the exit |
| `check_flag` | u32 | call `SCRIPT_WORK_SetCheckFlag(0xb)` (ov010_func_0215F238.c:52) | The script flag the exit sets |
| `result` | u32 | ret of the command (ov010_func_0215F238.c:53) | Always 1 |
| `owner` | u32 | local player, 0 | Attributes the exit to a player |

#### `network.restart` (0x0215F280, ov010)

`int SCRCMD_NETWORK_CmdRestartAutoComm(void *a0, void *a1)`
(`src/matched/ov/func_0215F280.c`, lines 33 to 43).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `script` | u32 | arg `a1` (func_0215F280.c:33, 35) | The script frame that runs the command |
| `game_system` | u32 | call `SCRCMD_WORK_GetGameSystem(a1)` (func_0215F280.c:35) | The game system the comm restarts in |
| `unblocked` | u32 | call `GAMESYSTEM_SetCommBlocked(p, 0)` (func_0215F280.c:37) | The comm is unblocked |
| `start_result` | u32 | call `GAMESYSTEM_TryStartAutoComm(p)` (func_0215F280.c:38) | 1 when auto comm started again |
| `check_flag` | u32 | call `SCRIPT_WORK_ResetCheckFlag(0xB)` (func_0215F280.c:39) | The script flag the restart clears |
| `result` | u32 | ret of the command (func_0215F280.c:41) | Always 1 |
| `owner` | u32 | local player, 0 | Attributes the restart to a player |

### Fields a mod wants that cannot be obtained

Each of these is named so the port can add it when the evidence arrives, rather
than let a mod bind to an absent field.

- **The peer identity at `link.up` and `link.down`.** `SOCL_LinkIsOn` returns a
  boolean over the access point MAC (ov009_func_0215CD24.c:22). It carries no
  peer. The port takes the peer from the WM AID or the DWC profile, and neither
  is a stable cross-instance identity without a session.
- **The cause of a save.** The requester is one call earlier in
  `GAME_DATA_SaveStart` (0x02012DAC), which is not in the declared set. The port
  cannot fill a cause field without a second hook.
- **The result of a save at `save.done` entry.** The return is ahead. The port
  must run the original first or track it.
- **The new overlay's data at `overlay.load`.** The load has not run. A handler
  cannot read the overlay's data or call its functions at the entry point.
- **The action behind a comm kind.** The kind is a number and the callback
  record's words are addresses (func_0202627C.c:62 to 68). Aletheia does not
  name which kind is a battle turn and which is a trade
  (`docs/mod-events.md`, still-to-build item 13).
- **The bytes for `net.send` and `net.receive`.** No transport exists, so the
  bytes never reach the payload (`docs/modding.md`, "Networking seam: missing").
- **The DWC profile and friend code.** `DWCi_Init` takes a game code and two
  allocators (func_02059FA8.c:73). The login and the profile happen later and
  are not at the init call.

### The thinnest payloads

These carry the least, so a mod that needs more must read state through an
accessor or wait for a wider field.

- `save.flag`: a flag id and a boolean (func_02012BF8.c:18).
- `save.block` and `save.misc`: a block id and a pointer (unit_020071CC.c:60;
  func_02012F08.c:19).
- `link.level`: one number from 0 to 3 (ov009_func_021666C8.c:42).
- `dwc.production`: the four forwarded arguments and the auth server constant
  (func_02059F6C.c:31 to 34).
- `wm.enable`: a path and a result (func_0208F224.c:21 to 23).
- `overlay.start`: the descriptor and the digest result (func_0207902C.c:78 to
  128).
- `network.value`: one `u16` (func_0215F214.c:31 to 33).
- `comm.handler` and `comm.notify`: the object, a kind and a callback pointer
  (func_02026388.c:41 to 49; func_020263B0.c:41 to 49).

## 17. Worked examples

The three events that matter most to the challenge runs are a faint, a catch
attempt and a level up (`docs/mod-events.md`, section 1;
`progress/EVENT_SURVEY.md`, sections 2 and 4). Each is shown in full, with
what a Nuzlocke, a Soullink and a monotype rule do with it. A fourth example
from the inventory area shows the design rule for a field that points at memory
the game frees.

### A faint: `battle.faint`

The payload:

```c
typedef struct BwEventBattleFaint {
    BwEvent   h;              /* kind = BW_EVENT_BATTLE_FAINT */
    u32       flow;           /* p, ov093_func_021C5200.c:41 */
    u32       side;           /* the literal 0 at :48 */
    u32       victim_owner;   /* the side that owns the fainted mon, :48 */
    u32       count;          /* func_021C52C8(p + 0x3e0, 0), :48 */
    BwPokeRef mon;            /* BTL_SIDE_GetPokeByIdThunk(p->f8, poke_id), :51 */
    u8        poke_id;        /* func_021C52E8(p + 0x3e0, 0, i), :51 */
    u8        entry_index;    /* the loop index i, :49 */
    u8        slot_empty;     /* func_021C52D8(p + 0x3e0, 0, i), :50 */
    u8        reserved;
    u8        competitor;     /* BTL_MAIN_GetCompetitor(p->f4), :52 */
    u8        showdown;       /* BTL_SERVER_FLOW_CheckShowdown(p), :52 */
    u8        player_side_alive; /* BTL_SERVER_FLOW_CheckPlayerSideAlive(p), :52 */
    u8        reserved2;
    u16       win_bgm;        /* BTL_MAIN_GetWinBgm(p->f4), :53 */
    u16       cause_move;     /* context last_move, battle.move.used */
    BwPokeRef cause_attacker; /* context last_attacker, battle.damage.dealt */
    u16       cause_attacker_species; /* accessor 0x05 on the attacker */
    u8        cause_attacker_level;   /* accessor 0x9E on the attacker */
    u8        reserved3;
    u32       cause_damage;   /* context last_damage, battle.damage.dealt */
    u32       turn;           /* context turn, BTL_SERVER_FLOW_GetTurnCount */
    s32       rewards_applied;/* BTL_SERVER_FLOW_ProcExpForFaintedPoke(p, a), :56 */
} BwEventBattleFaint;
```

A Nuzlocke rule marks the fainted Pokemon dead. It reads `mon.poke_id` and
`victim_owner` to record the loss in its run state, reads `player_side_alive`
to decide whether the run continues, and records the cause from
`cause_attacker`, `cause_attacker_species`, `cause_attacker_level` and
`cause_move`, so the run log says what killed it. A Soullink rule uses
`victim_owner` to send the faint to the instance that owns the Pokemon, then
looks the Pokemon up in its link table by `mon.poke_id` and faints the paired
Pokemon on the peer. A monotype rule checks the fainted Pokemon's type through
the state accessor and reports a team that no longer satisfies the type rule.
The event is a block, so a rule that forbids the reward step returns
`BW_EVENT_BLOCK` (`docs/mod-interface.md`, section 4).

### A catch attempt: `catch.attempt`

The payload:

```c
typedef struct BwEventCatchAttempt {
    BwEvent   h;              /* kind = BW_EVENT_CATCH_ATTEMPT */
    u32       flow;           /* p, ov093_func_021CBA40.c:39 */
    BwPokeRef target;         /* a, :39; BTL_POKEPARAM_GetPokeId(a), :49 */
    BwPokeRef resolved;       /* m, BTL_SIDE_GetPokeAtPosThunk(p->f8, buf[i]), :51 */
    u16       ball;           /* b, :39 */
    u8        target_slot;    /* chosen, :52 */
    u8        candidate_count;/* count, :49 */
    u8        candidate_slots[6]; /* buf, :49 */
    u8        reserved;
    u8        competitor;     /* BTL_MAIN_GetCompetitor(p->f4), :54 */
    u8        reserved2[3];
} BwEventCatchAttempt;
```

A Nuzlocke rule blocks a second catch on a route: it reads `target.poke_id`
and `target.species` through the state accessor, tests its used-route set, and
returns `BW_EVENT_BLOCK` to forbid the throw. A Soullink rule sends the catch
to the peer so both instances record the same first encounter. A monotype rule
checks the target's type before the catch and may block a catch that would
break the type rule, or allow a free catch of an off-type shiny. The catch
result is not in this payload; the game rolls it inside
`BTL_SERVER_FLOW_CalcCaptureShakes` at `:56`, and `catch.result` is the event
that carries it.

### A level up: `battle.exp`

The payload:

```c
typedef struct BwEventBattleExp {
    BwEvent   h;              /* kind = BW_EVENT_BATTLE_EXP */
    BwPokeRef mon;            /* p, ov093_func_021D69B8.c:34 */
    u32       exp_before;     /* p->exp, :40 */
    u32       exp_added;      /* *remaining, :41 */
    u32       exp_total;      /* p->exp + *remaining, :41 */
    u32       threshold;      /* func_02018584(...), :42 */
    u16       species;        /* p->species, :42 */
    u16       hp_delta;       /* p->hp - old_hp, :57 */
    u16       stat_deltas[5]; /* r->a to r->e, :72 */
    u8        level_before;   /* p->level, :39 */
    u8        level_after;    /* r->level, :70 */
    u8        leveled;        /* the return, :82 */
    u8        reserved;
    u32       turn;           /* context turn, BTL_SERVER_FLOW_GetTurnCount */
} BwEventBattleExp;
```

The gain that caused the level up is `exp_added`, so this event carries its own
cause. The later level up events, `battle.levelup` and `battle.levelup.main`,
fire after the gain is off the stack, so they carry it from the context as
`cause_exp_gain`, fed here by `remaining` (`:41`).

A Nuzlocke rule enforces a level cap: it reads `level_after` and `threshold`
and blocks the gain when the level would pass the cap, or records the level
for the run. A Soullink rule keeps both instances' levels in step by sending
the gain and the new level to the peer. A monotype rule records the level
against the party's shared type and reports a member that has fallen behind.
The gain is one level per call, so a cap that stops a gain stops one level and
the caller loops (`docs/mod-events.md`, section 1a).

### A mystery gift: `item.gift.mystery`

The payload:

```c
typedef struct BwEventItemGiftMystery {
    BwEvent   h;              /* kind = BW_EVENT_ITEM_GIFT_MYSTERY */
    u32       mode;           /* SCRIPT_WORK_ReadWorkValue(a,b), ov021_func_021B6BDC.c:88 */
    u32       game_data;      /* SCRCMD_WORK_GetGameData(b), :90 */
    u32       card_index;     /* the FindMysteryCard out word x, :93 */
    u8        gift_kind;      /* PDW_POSTMAN_GetMysteryGiftType(h), :94 */
    u32       handler_id;     /* sMysteryGiftKindHandlers[k].id, :95 */
    u8        card[0xCC];     /* a copy of the card buffer at :93, freed at :135 */
    u16       out_value;      /* *out per mode, :100 to :133 */
    u8        received;       /* ReceiveGift(...) == 1, :117 */
    u8        liberty_pass;   /* IsLibertyPassGift(p,k), :119 */
} BwEventItemGiftMystery;
```

The card buffer is the field that design rule 5 exists for. The game passes a
pointer to a 0xcc-byte card and frees it at `MYSTERY_DATA_Delete(h)`
(`ov021_func_021B6BDC.c:135`), so a pointer kept in the payload would dangle
once the call returns. The port copies the bytes into its own storage and a
handler reads the copy. The concrete gift content, an item id or a Pokemon, is
inside the card and the kind tables, so the copy is the only way a mod reaches
it.

A Nuzlocke rule that forbids gift items binds here and reads `gift_kind` and the
card copy. A Soullink rule sends the same gift to the peer so both instances
stay in step. A monotype rule checks the species in the card copy against the
team's type. The command is one command with eight modes, so a mod reads `mode`
to know which action ran, and `received` to learn whether the gift was taken.

## 18. Still to build

1. **Freeze the payload structs, the `kind` ids and the battle context.**
   Only `BwEventEncounterStart` is spelled out today (`docs/mod-events.md`,
   section 2). This document proposes the members, the `BW_EVENT_*` names and
   the battle context (section 0a) for the 36 battle events. The work is to
   fix each concrete struct's layout, the `kind` constant and the context
   fields, and to publish them with the `api` version
   (`docs/mod-events.md`, still-to-build item 3; `docs/mod-interface.md`,
   section 5; item 11 below).
2. **Fix the firing point where the value is produced inside the function.**
   `battle.start` fires before the setup exists (`arm9/func_02013090.c:35`),
   `battle.result` and `battle.outcome` fire before the outcome is computed
   (`func_021C84D8.c:30`; `ov093_func_021CC668.c:36`), and `encounter.start`
   fires before the task and its work record exist
   (`ov010_func_0216E7C8.c:50`, `:51`). The work is to fix whether these fire at
   entry, after the original,
   or both, the way `catch.result` fires after (`docs/mod-events.md`,
   section 1). The context of section 0a is the alternative where the value is
   produced inside the function and the port can read it after the original;
   the work is to fix, per event, whether the payload takes the value from the
   return or from the context.
3. **Fix the Pokemon identity in a payload.** `BTL_POKEPARAM_GetPokeId`
   returns the byte at `+0x19` (`func_021D5440.c:26`), and the side and slot
   come from separate accessors. The work is to fix what `BwPokeRef` carries
   and which accessor fills each member (`docs/mod-events.md`, still-to-build
   item 3).
4. **Fix whether `battle.faint` fires once or per fainted entry.**
   `BTL_SERVER_FLOW_ProcFaintRewards` loops over every fainted entry
   (`ov093_func_021C5200.c:49`). The work is to fix whether the slot fires
   once at entry with the list, or once per entry, and whether the three list
   helpers `func_021C52C8`, `func_021C52D8` and `func_021C52E8`
   (`:30` to `:32`) join the declared set.
5. **Fix the catch result's availability.** The catch result, the shakes and
   the caught flag are locals in `BTL_SERVER_FLOW_TryThrowBall`
   (`ov093_func_021CBA40.c:56`, `:63`). The function's return is only the
   thrower's side (`:65`, `:68`). The work is to fix whether the port captures
   the internal result, or binds a second address such as
   `BTL_SERVER_FLOW_CalcCaptureShakes` (0x021CBB34, not declared), so
   `catch.result` can carry the true outcome.
6. **Fix the target of a move event.** `battle.move.used` and
   `battle.damage.dealt` take the acting Pokemon and a move object, not the
   target (`ov093_func_021C1A10.c:29`; `ov093_func_021C1910.c:29`). The
   context supplies the attacker and the move, but not the defender, so a mod
   that needs the defender, or the defender's remaining HP, cannot read it at
   the call. The work is to fix whether the port adds a defender to the
   payload from a nearby accessor or leaves it out.
7. **Fix the level up payload's level source.** `BTL_SERVER_NotifyLevelUp`
   reads the nickname, not the level, and sends the nickname buffer to the
   beacon (`ov093_func_021BB8BC.c:23`; `arm9/unit_020271F8.c:115`). The new
   level is read through the party accessor 0x9E. The work is to fix whether
   the level up event carries the level, the nickname, or both, and which
   accessor fills the level.
8. **Name the party-area ids the payloads use.** Several fields read party
   ids 0x9D (status), 0xA0 (HP), 0xA1 (max HP), 0x73 (nickname), 0x9E
   (level), 0x05 (species), 0x08 (exp) and the move ids 0x36 and 0x3A. Some of
   these are named and some are not; aletheia does not name which party-area id
   is HP or a stat (`docs/mod-interface.md`, still-to-build item 15). The work
   is to fix the id-to-name mapping before a payload can depend on it.
9. **Name the battle effect VM's 74 handlers.** Only the six control rows and
   the VM entry point are catalogued (`docs/mod-events.md`, section 1;
   `docs/mod-events.md`, still-to-build item 14). The work is to name the rest,
   so their arguments can be specified the way the control family is here.
10. **Freeze the field, item, progression and network contexts.** The contexts
    of section 0a beyond the battle one are proposed, not fixed. The work is to
    fix which event sets each field and which event clears it, the way the
    battle context is argued, so a field with no feeder in the current situation
    reads a zero or an empty reference (design rule 6). The field context resets
    at `save.load` and the map change, the item context at `field.bag.closed`
    and `item.use.done`, and the network context at `bw_ds_reset` and
    `link.down`. The context is part of the ABI, so it is covered by the `api`
    version (`docs/mod-events.md`, section 5).
11. **Freeze the battle context and argue its reset points.** The context of
    section 0a is proposed, not fixed. The work is to fix which event sets
    each field, which event clears it, and how a field with no feeder in the
    current battle reads, so a stale value is never returned. The evidence for
    the feeders is the battle functions read here; the evidence for the reset
    points is that the whole context is cleared at `battle.start` and `active`
    at `battle.finish`. The context is part of the ABI, so it is covered by
    the `api` version (`docs/mod-events.md`, section 5). One cause is still
    unobtainable: a non-damage faint names no status, hazard or weather tick,
    because the context tracks only the last move, attacker and damage. The
    work is to fix whether the port tracks a non-damage cause too.
12. **The transport.** `sdk/arm7/` has no Wm or DWC responder and the tree has
    no sockets (`docs/modding.md`, "Networking seam: missing"). The `net.send`
    and `net.receive` payloads have no feed until it exists.
13. **The send and receive points.** Which declared Wm, DWC or `GAME_COMM_*`
    address is the send and which the receive point is not fixed
    (`docs/mod-events.md`, still-to-build item 12).
14. **The overlay event ordering.** Where the handler runs relative to the
    port's copy, bind, relocate and static initialisers is not fixed
    (`docs/mod-events.md`, still-to-build item 10). The fields are known at
    every point; the data is not.
15. **The comm action names.** The kind to action map is not named
    (`docs/mod-events.md`, still-to-build item 13). Aletheia does not name which
    kind is a battle turn and which is a trade.
16. **A cross-instance player identity.** No accessor gives a stable player id
    for the `owner` and `peer` fields. The port must fix one before a soullink
    rule can attribute a payload.
17. **The save cause.** The port needs a second hook on `GAME_DATA_SaveStart`
    (0x02012DAC) to fill the cause of `save.begin`, or it must state the cause
    as absent.
18. **The peer identity at the link events.** The link predicate carries only
    the access point MAC (`ov009_func_0215CD24.c:22`); the peer must come from
    the WM or DWC context.
