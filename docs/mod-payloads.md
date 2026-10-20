# Event payloads

This document owns the payload of each stable event: the fields a handler
receives, their types, and which fields a handler may modify.
`docs/mod-events.md`, section 1, owns the event list, the game function each
event binds and the refused outcome. `docs/mod-abi.md` owns the common types.
`progress/PAYLOAD_SURVEY.md` is the research behind the sources: for each game
function, which fields the call can supply and the file and line each comes
from.

## 0. The design

### The rules

1. **The port owns the payload struct.** A payload is a port-owned struct,
   filled from the game's state when the event fires. The game's own structs
   are never exposed, because aletheia restructures them.
2. **A payload is part of the ABI.** Its layout is frozen within an interface
   version, and new fields are only added at the end (`docs/mod-abi.md`).
3. **Carry the situation, not just the entity.** A handler needs to decide,
   not only to log: a faint payload says which Pokemon fainted, whose it was,
   what caused it and on which turn. A field a run plausibly needs is carried.
4. **A field must be obtainable.** It comes from the call's arguments, from a
   value the port can read at the call, or from a context the port tracks
   (section 1). No field is invented.
5. **Memory the game frees is copied.** A field that points at memory the game
   frees later is copied into the payload, never stored as a pointer.
6. **No stale values.** A context field with no feeder in the current
   situation reads 0, or an empty `BwMonRef`, never a value from an earlier
   situation.
7. **Modifiable fields are marked.** A field marked *mod* in the tables below
   may be written by a handler; the port applies it before the game function
   continues (`docs/mod-events.md`, section 2). A write to any other field has
   no effect.

### The common types

Every payload starts with `BwEvent` (`u32 size`, `u32 kind`). A Pokemon is a
`BwMonRef`, which says where it is (party, box, battle or wild), its side in
battle, its box and its slot, with an opaque handle a mod passes back to the
state calls (`docs/mod-abi.md`, section 1). Species, moves, items and abilities
are the game's own ids, as `u16`. A zone is the game's zone id, `u16`.

### How the port fills a payload

When a slot bound to an event is entered, the port builds the payload on the
slot's stack: it sets `h.size` and `h.kind`, fills the fields from the call's
arguments, from accessor calls at the slot and from the contexts, then runs
every handler. For an event that fires after the game function, the port calls
the function first and adds its result. The pointer a handler receives is
valid only for the call.

## 1. The contexts

Some fields are not in scope when the event fires: the move that caused a
faint was used a call earlier. The port keeps a small record for each such
situation and updates it from the slots it passes.

**The battle context.** Reset at `battle.start`; `active` cleared at
`battle.end`.

| Field | Type | Fed by |
|---|---|---|
| `active` | u8 | `battle.start` |
| `wild` | u8 | `battle.start` |
| `format` | u8 | `battle.start` (single, double, triple, rotation, multi) |
| `turn` | u16 | the battle's turn record, read at each battle event |
| `last_move` | u16 | `battle.move` |
| `last_attacker` | `BwMonRef` | `battle.move` and `battle.damage` |
| `last_damage` | u32 | `battle.damage`; reset at each `battle.move`, so a faint from weather, a hazard or a status reads 0 |
| `weather` | u8 | the battle's field situation, read at each battle event |

**The field context.** Reset at `save.load` and at each `route.change`.

| Field | Type | Fed by |
|---|---|---|
| `zone` | u16 | `route.change` |
| `prev_zone` | u16 | `route.change` |
| `season` | u8 | `route.change` |
| `pos_x`, `pos_y`, `pos_z` | s16 | the player position, read at `player.step` |
| `dir` | u8 | the player's facing, read at `player.step` |

A context is part of the ABI only through the payloads that read it; a mod
never reads a context directly.

## 2. The payloads

Each table lists the fields after the `BwEvent` header, in layout order.

### Session

**`game.new`**: no fields.

**`save.begin`**, **`save.done`**

| Field | Type | Meaning |
|---|---|---|
| `play_seconds` | u32 | play time |
| `zone` | u16 | where the player saves |
| `ok` | u8 | `save.done` only: the save was written |

**`save.load`**

| Field | Type | Meaning |
|---|---|---|
| `play_seconds` | u32 | play time of the loaded save |
| `zone` | u16 | where the save resumes |
| `state_in_step` | u8 | the mod's saved data was written with this save (`docs/mod-state.md`) |

**`game.cleared`**

| Field | Type | Meaning |
|---|---|---|
| `play_seconds` | u32 | play time at the Hall of Fame |
| `team_count` | u8 | Pokemon entered |
| `team` | `BwMonRef[6]` | the team |

### World

**`route.change`**

| Field | Type | Meaning |
|---|---|---|
| `zone` | u16 | the new zone |
| `prev_zone` | u16 | the zone left |
| `season` | u8 | the current season |

**`map.warp`**

| Field | Type | Meaning |
|---|---|---|
| `zone` | u16 | where the warp starts |
| `dest_zone` | u16, *mod* | where it leads |
| `dest_x`, `dest_z` | s16, *mod* | the arrival cell |

**`player.step`**

| Field | Type | Meaning |
|---|---|---|
| `zone` | u16 | the current zone |
| `x`, `y`, `z` | s16 | the cell after the step |
| `dir` | u8 | the facing |
| `mode` | u8 | walking, running, cycling, surfing |

**`trainer.spotted`**

| Field | Type | Meaning |
|---|---|---|
| `trainer_id` | u16 | the trainer who saw the player |
| `partner_id` | u16 | the second trainer of a double, or 0 |
| `distance` | u8 | cells between them |

**`npc.talk`**

| Field | Type | Meaning |
|---|---|---|
| `zone` | u16 | the zone |
| `object_id` | u16 | the person or object talked to |
| `script_id` | u16 | the script the talk runs |

**`menu.open`**: no fields.

**`menu.build`**

| Field | Type | Meaning |
|---|---|---|
| `entries` | u32, *mod* | one bit per menu entry shown |

**`field.move`**

| Field | Type | Meaning |
|---|---|---|
| `move` | u16 | the field move |
| `user` | `BwMonRef` | the Pokemon using it |

**`pc.open`**: no fields.

### Encounters

**`encounter.wild`**

| Field | Type | Meaning |
|---|---|---|
| `zone` | u16 | the zone |
| `method` | u8 | grass, dark grass, surf, fishing, shaking grass, rippling water, dust |
| `slot` | u8 | the encounter table slot rolled |
| `species` | u16, *mod* | the species |
| `level` | u8, *mod* | the level |
| `shiny` | u8, *mod* | 1 forces shiny, 0 leaves the roll |
| `double` | u8 | a double wild battle |

**`encounter.static`**

| Field | Type | Meaning |
|---|---|---|
| `zone` | u16 | the zone |
| `species` | u16, *mod* | the species |
| `level` | u8, *mod* | the level |
| `kind` | u8 | static, legendary, scripted |

**`encounter.trainer`**

| Field | Type | Meaning |
|---|---|---|
| `trainer_id` | u16 | the trainer |
| `partner_id` | u16 | the second trainer, or 0 |
| `format` | u8 | single, double, triple, rotation, multi |

### Pokemon

**`mon.create`**

| Field | Type | Meaning |
|---|---|---|
| `species` | u16, *mod* | the species |
| `level` | u8, *mod* | the level |
| `shiny` | u8, *mod* | 1 forces shiny, 0 leaves the roll |
| `nature` | u8, *mod* | the nature, or 0xFF to leave the roll |
| `personality` | u32 | the personality value the game rolled |

**`mon.receive`**

| Field | Type | Meaning |
|---|---|---|
| `mon` | `BwMonRef` | the Pokemon |
| `source` | u8 | catch, gift, static, egg, in-game trade |
| `to_box` | u8 | it goes to a box because the party is full |
| `species` | u16 | the species |
| `level` | u8 | the level |
| `shiny` | u8 | shiny |
| `zone` | u16 | where it was received |

**`mon.nickname`**

| Field | Type | Meaning |
|---|---|---|
| `mon` | `BwMonRef` | the Pokemon |
| `force` | u8, *mod* | 1 opens the prompt even when the game would skip it |

**`mon.evolve`**

| Field | Type | Meaning |
|---|---|---|
| `mon` | `BwMonRef` | the Pokemon |
| `from` | u16 | its species |
| `to` | u16, *mod* | the species it evolves into |
| `method` | u8 | level, item, trade, friendship, other |

**`mon.exp`**

| Field | Type | Meaning |
|---|---|---|
| `mon` | `BwMonRef` | the Pokemon |
| `amount` | u32, *mod* | the experience to add |
| `exp` | u32 | its experience before |
| `level` | u8 | its level before |

**`mon.levelup`**

| Field | Type | Meaning |
|---|---|---|
| `mon` | `BwMonRef` | the Pokemon |
| `from` | u8 | the level before |
| `to` | u8 | the level reached |

**`move.learn`**

| Field | Type | Meaning |
|---|---|---|
| `mon` | `BwMonRef` | the Pokemon |
| `move` | u16 | the move |
| `source` | u8 | level, TM, tutor, relearner |
| `slot` | u8 | the move slot it goes into, or 0xFF for a free one |
| `replaced` | u16 | the move it replaces, or 0 |

**`mon.release`**

| Field | Type | Meaning |
|---|---|---|
| `mon` | `BwMonRef` | the Pokemon |
| `species` | u16 | its species |

**`box.deposit`**, **`box.withdraw`**

| Field | Type | Meaning |
|---|---|---|
| `mon` | `BwMonRef` | the Pokemon, where it is now |
| `box` | u8 | the box it moves into or out of |
| `slot` | u8 | the box slot |

**`party.swap`**

| Field | Type | Meaning |
|---|---|---|
| `slot_a`, `slot_b` | u8 | the two party positions |

**`mon.helditem`**

| Field | Type | Meaning |
|---|---|---|
| `mon` | `BwMonRef` | the Pokemon |
| `item` | u16 | the item given |
| `previous` | u16 | the item it held, or 0 |

**`egg.hatch`**

| Field | Type | Meaning |
|---|---|---|
| `mon` | `BwMonRef` | the hatching Pokemon |
| `species` | u16 | its species |

**`trade.ingame`**

| Field | Type | Meaning |
|---|---|---|
| `trade_id` | u16 | the in-game trade |
| `given` | `BwMonRef` | the player's Pokemon |
| `received_species` | u16 | the species offered |

### Battle

**`battle.start`**

| Field | Type | Meaning |
|---|---|---|
| `wild` | u8 | a wild battle |
| `format` | u8, *mod* | single, double, triple, rotation, multi |
| `trainer_id` | `u16[2]` | the opposing trainers, or 0 |
| `level_adjust` | s8, *mod* | added to every opposing Pokemon's level |
| `opponent_party` | u32 | a handle to the opposing party; a handler edits it through the state calls |

**`battle.action`**

| Field | Type | Meaning |
|---|---|---|
| `mon` | `BwMonRef` | the Pokemon choosing |
| `side` | u8 | 0 the player's, 1 the opponent's (the AI asks the move check too) |
| `action` | u8 | fight, switch, run |
| `move` | u16 | for fight: the move checked |
| `switch_to` | u8 | for switch: the party position |

For fight, the game checks every move when it builds the move menu and again
when the player picks one, so the event fires several times per turn; a
handler gives the same answer for the same move. The bag is `battle.item`.

**`battle.switch`**

| Field | Type | Meaning |
|---|---|---|
| `out` | `BwMonRef` | the Pokemon leaving, or empty after a faint |
| `in` | `BwMonRef` | the Pokemon sent out |
| `forced` | u8 | the switch follows a faint or a move that forces it |

**`battle.escape`**

| Field | Type | Meaning |
|---|---|---|
| `mon` | `BwMonRef` | the Pokemon trying to flee |

**`battle.move`**

| Field | Type | Meaning |
|---|---|---|
| `user` | `BwMonRef` | the Pokemon using the move |
| `move` | u16 | the move |
| `target` | `BwMonRef` | the first target |
| `turn` | u16 | the turn |

**`battle.damage`**

| Field | Type | Meaning |
|---|---|---|
| `attacker` | `BwMonRef` | the attacker |
| `target` | `BwMonRef` | the target |
| `move` | u16 | the move |
| `amount` | u32, *mod* | the damage |
| `target_hp` | u16 | the target's HP before the hit |
| `target_max_hp` | u16 | the target's maximum HP |
| `critical` | u8 | a critical hit |
| `effectiveness` | u8 | no effect, not very, neutral, super effective |

**`battle.faint`**

| Field | Type | Meaning |
|---|---|---|
| `mon` | `BwMonRef` | the Pokemon that fainted |
| `side` | u8 | 0 the player's, 1 the opponent's |
| `cause` | `BwMonRef` | the last attacker, or empty |
| `cause_move` | u16 | the last move, or 0 |
| `cause_damage` | u32 | the last damage, or 0 for weather, a hazard or a status |
| `turn` | u16 | the turn |

**`battle.item`**

| Field | Type | Meaning |
|---|---|---|
| `item` | u16 | the item chosen from the bag |
| `mon` | `BwMonRef` | the Pokemon whose turn it is |
| `ball` | u8 | the item is a ball |
| `revive` | u8 | the item revives a fainted Pokemon |

**`catch.attempt`**

| Field | Type | Meaning |
|---|---|---|
| `target` | `BwMonRef` | the wild Pokemon |
| `species` | u16 | its species |
| `level` | u8 | its level |
| `shiny` | u8 | shiny |
| `ball` | u16 | the ball item |
| `rate` | u8, *mod* | the catch rate the roll uses |

**`catch.result`**

| Field | Type | Meaning |
|---|---|---|
| `target` | `BwMonRef` | the wild Pokemon |
| `species` | u16 | its species |
| `ball` | u16 | the ball item |
| `caught` | u8 | the catch succeeded |
| `shakes` | u8 | the shakes shown |

**`battle.end`**

| Field | Type | Meaning |
|---|---|---|
| `outcome` | u8 | win, loss, fled, caught, draw |
| `wild` | u8 | a wild battle |
| `trainer_id` | `u16[2]` | the opposing trainers, or 0 |
| `turns` | u16 | turns taken |

**`whiteout`**

| Field | Type | Meaning |
|---|---|---|
| `in_battle` | u8 | 1 the battle half, 0 the field half |
| `money_lost` | u32 | the money the game takes |

### Items, money and healing

**`item.add`**

| Field | Type | Meaning |
|---|---|---|
| `item` | u16 | the item |
| `count` | u16, *mod* | how many |
| `pocket` | u8 | the bag pocket |

**`item.use`**

| Field | Type | Meaning |
|---|---|---|
| `item` | u16 | the item |
| `target` | `BwMonRef` | the Pokemon it is used on, or empty |

**`shop.buy`**

| Field | Type | Meaning |
|---|---|---|
| `item` | u16 | the item picked |
| `price` | u32, *mod* | the price of one; the shop shows and charges it |

**`money.change`**

| Field | Type | Meaning |
|---|---|---|
| `delta` | s32, *mod* | the change, positive or negative |
| `before` | u32 | the money before |

**`heal`**

| Field | Type | Meaning |
|---|---|---|
| `source` | u8 | Pokemon Centre or script, the HP-restore power |
| `mask` | u8, *mod* | one bit per party position to heal |

### Progress

**`badge.get`**

| Field | Type | Meaning |
|---|---|---|
| `badge` | u8 | the badge, 0 to 7 |
| `count` | u8 | badges held after it |

**`flag.set`**

| Field | Type | Meaning |
|---|---|---|
| `flag` | u16 | the event flag |
| `value` | u8 | set or cleared |

**`dex.register`**

| Field | Type | Meaning |
|---|---|---|
| `species` | u16 | the species |
| `caught` | u8 | 1 caught, 0 seen |

## Still to build

1. **The source of each field.** For each field, the argument, accessor or
   context it is read from, confirmed against the bound game function when its
   builder is written (`progress/PAYLOAD_SURVEY.md` holds most of them). The
   session and items areas' builders are written (`sdk/mod_event_session.c`,
   `sdk/mod_event_items.c`); the pokemon, field and battle areas' are not.
2. **The builders for the rest of the events.** The pokemon, field and battle
   areas have no binding rows yet, and so no builder, applier or refusal
   function (`docs/mod-implementation.md`, section 6, step 4).

## Related documents

- `docs/mod-events.md`: the events, the handler contract and the state calls.
- `docs/mod-abi.md`: `BwEvent`, `BwMonRef` and the version rule.
- `docs/mod-implementation.md`: the builders and the contexts in code.
- `progress/PAYLOAD_SURVEY.md`: where each value comes from in the game.
