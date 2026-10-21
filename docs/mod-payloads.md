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

### What the port writes back

A marked field is one the event's binding row carries an applier for. The
applier reads the field back out of the payload and writes it into the
game's own work, so that the change reaches the game. The rows that have one
today are `save.done`, `map.warp`, `menu.build`, `encounter.wild`,
`encounter.static`, `item.add`, `shop.buy`, `money.change`, `heal`,
`mon.create`, `mon.evolve` and `mon.exp`. `save.done` is the one with no
marked field: its applier closes the port's own save window, the step in
which the mod's own file is written with the game's save
(`docs/mod-state.md`, section 2).

One event still carries a marked field whose row has no applier:
`mon.nickname`'s `force`. Its builder fills the field with 0, and a
handler's write to it has no effect: the game has no call that forces the
nickname prompt, so the port has no hook at the decision (the answer to the
prompt reaches the battle's return through the catch arguments, and the port
does not see that path). A mod that wants the nickname asked for overrides
the game's own prompt instead, which is a declared row
(`docs/mod-overrides.md`).

The other three now have appliers: `battle.start`'s `format` and
`level_adjust` are written into the setup the game just built, and
`battle.damage`'s `amount` and `catch.attempt`'s `rate` are applied to the
game's own values.

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
situation and updates it from the slots it passes. The two records are the
port's own (`sdk/mod_context.h`), and both are cleared at power-on and at
every reset.

**The battle context.** Reset at `battle.start`; `active` cleared at
`battle.end`.

| Field | Type | Fed by |
|---|---|---|
| `active` | u8 | `battle.start` |
| `wild` | u8 | the battle's main module, at the first battle event that carries it |
| `format` | u8 | the battle's main module, at the first battle event that carries it (single, double, triple, rotation, multi) |
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

Each table lists the fields after the `BwEvent` header, in layout order. The
rows, the builders, the appliers and the refusal functions of an area live in
one file (`sdk/mod_event_session.c` and its siblings), and the paragraph
after an area's tables says what its builder fills.

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
| `state_in_step` | u8 | reads 0: whether the mod's own saved data was written with this save is a per-mod answer, and the payload is built once for every handler of the event, so the mod asks the services table's `state_in_step` (`docs/mod-state.md`, section 2) |

**`game.cleared`**

| Field | Type | Meaning |
|---|---|---|
| `play_seconds` | u32 | play time at the Hall of Fame |
| `team_count` | u8 | Pokemon entered |
| `team` | `BwMonRef[6]` | the team |

**What the session builder fills.** The session area's rows are in
`sdk/mod_event_session.c`. `game.new` has no fields: the event is the moment
itself. The save events read the play time and the zone out of the save
control the call carries, through the save's own play time and location
blocks; `save.done`'s `ok` is the function's own answer, 1 when the write
was accepted. The two save rows are also the port's own save window:
`save.begin` opens it and `save.done` closes it, so a mod's own file is
written in the same step as the game's save (`docs/mod-state.md`, section 2).
`save.load` carries the play time and the zone of the loaded save, and it
resets the field context. `game.cleared` names the party the player brings
to the Hall of Fame: the party's own members, up to six.

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

**What the world builder fills.** The world area's rows are in
`sdk/mod_event_field.c`. `route.change` carries the new zone from the call,
the zone left from the field context and the season from the game data, and
it resets and refills the context. `map.warp` carries the zone the warp
starts in and the zone it leads to out of the map change's own work, and the
arrival cell for a direct position; a warp to a rail position has no cell.
`player.step` carries the zone from the map, the cell from the player's own
position, the facing from the step's input and the mode from the same, and
it feeds the position and the facing into the field context.
`trainer.spotted` carries the trainer that sees the player, the distance to
it and the second trainer of a pair battle; a trainer that does not see the
player leaves the payload empty. `npc.talk` carries the zone; the object and
the script read 0, because the model in front of the player is what the
bound search returns and the port does not run the search a second time.
`menu.build` carries the entries the menu shows, one bit per slot, read at
the return of the menu's creation. `field.move` carries the move the game
asks about; the user reads 0, because the Pokemon that uses the move is the
one the game's own handler looks for in the party. `menu.open` and `pc.open`
have no fields.

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

**What the encounters builder fills.** The encounter rows are in
`sdk/mod_event_field.c` too. `encounter.wild` carries the zone from the
call, the method from the game's own encounter type and the slot of the
zone's table that was rolled; the species, the level and the shiny mode are
the ones the function built, and an encounter that builds no Pokemon (a
repel) leaves them empty. A roamer's slot reads 0, because a roamer comes
from the save and not from a table of the zone, and its shiny and double
flags read 0 as well. `encounter.static` carries the player's own zone, the
species and the level of the Pokemon the setup holds, and the kind says
whether the battle is a wild one or a battle a script started; the
legendary kind has no feeder at this call, because the game chooses the
species in the script that starts the battle. `encounter.trainer` carries
the trainer and its partner from the call's own arguments and the format
from the game's own rule.

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
| `force` | u8, *mod* | 1 opens the prompt even when the game would skip it (reads 0 today) |

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

**What the Pokemon builder fills.** The Pokemon area's rows are in
`sdk/mod_event_mon.c`. `mon.create` carries the species and the level the
caller asks for and the personality value it passes; the row that rolls the
personality reads 0 for it, because the game rolls it inside that call, and
the nature reads 0xFF, the sentinel "leave the roll", because the nature is
rolled inside the creation call as well. The shiny flag reads 0 on the
creation row, where the roll is the caller's, and is the caller's own request
on the row that rolls the personality. `mon.receive` names the Pokemon
that was received, where it came from and whether it went to a box because
the party was full; a Pokemon that went into a box has no slot, because the
free slot inside that box is not named by the call, and a Pokemon given
while the party was full has no record left to read, so its shiny flag reads
0 and the species and the level come from the script's own creation request.
An in-game trade's species reads 0, because the trade record is the
overlay's own data. `mon.nickname`'s Pokemon reads 0 and its force flag
reads 0: the prompt's own work holds the Pokemon, and the game decides to
ask before it gets to the prompt. `mon.evolve` names the Pokemon and the
species it comes from, and the species it evolves into is the check's own
result. `mon.exp` names the first member of the party that gains
experience, with the experience and the level read out of the party record
through the state calls; the day care's payload has no party position,
because the call carries the Pokemon and not the party. `move.learn`
carries the move and its source, and the move slot is the first of the
Pokemon's four that is empty, or 0xFF when it holds four moves already; the
replaced move reads 0, because the game asks for that later, in the screen
that owns the choice. The event fires from all six of the area's rows: the
level-up check, the day care, the party screen's two, the nurse's relearner
and the script command that writes a move
(`docs/mod-events.md`, section 1).
`mon.helditem` carries the item given and the item it held, but the party
position reads 0: the party is not carried by the call.

### Battle

**`battle.start`**

| Field | Type | Meaning |
|---|---|---|
| `wild` | u8 | a wild battle |
| `format` | u8, *mod* | single, double, triple, rotation, multi; 0xFF where the port does not know the format yet (the row that creates the setup) |
| `trainer_id` | `u16[2]` | the opposing trainers, or 0 |
| `level_adjust` | s8, *mod* | added to every opposing Pokemon's level |
| `opponent_party` | u32 | a handle to the opposing party; a handler edits it through the state calls |

The format and the levels are applied: the port writes the format into the
setup's own rule and the levels through the game's own setter, after the
handlers have run.  A handler that writes nothing leaves the game's own
values alone; a zero level adjustment means "no adjustment", and a zero
format is a format a handler may ask for, so the row that creates the setup
names the unknown one 0xFF instead.

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
| `amount` | u32, *mod* | the damage (reads 0 today) |
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
| `rate` | u8, *mod* | the catch rate the roll uses (reads 0 today) |

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

**What the battle builder fills.** The battle area's rows are in
`sdk/mod_event_battle.c`. `battle.start` carries nothing: the setup the game
will build does not exist yet, so the wild flag, the format, the trainer ids
and the handle of the opposing party read 0, and the port reads them from
the battle's main module at the first battle event instead. `battle.action`
carries the action, the side and, for a fight, the move the game checks; the
Pokemon reads 0 for a switch, because the choice list holds the party
positions and nothing else, and the party position the player picked is the
function's own result, 6 being the game's "backed out". `battle.switch`'s
forced flag is set when the Pokemon that leaves has fainted; a move that
forces the switch runs the same function and cannot be told from the call.
`battle.move`'s target reads 0, because the move record holds no target: the
server picks the target set itself. `battle.damage` carries the attacker,
the target, the move, the critical flag and the type matchup, and the
target's HP before the hit, because the hit has not landed yet; the amount
reads 0, because the game computes it inside the function. `catch.attempt`
carries the ball, the target and its species, level and shiny flag; the
catch rate reads 0, because the game computes the rate itself when it rolls
the catch. `catch.result`'s caught flag and shakes read 0: the game keeps
both in the locals of the roll. `battle.faint`'s cause comes from the battle
context, so a faint from the weather, a hazard or a status has no cause.
`whiteout`'s money lost reads 0: the game computes the penalty inside the
step, at the state that shows the amount.

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

**What the items builder fills.** The items area's rows are in
`sdk/mod_event_items.c`. `item.add` carries the item and the count from the
call and the pocket the game puts the item in, read out of the item's own
data. `item.use` carries the item and the Pokemon the check is made for; the
target's position in the party reads 0, because the call carries the party
record but not the party. `shop.buy` carries the item and the price out of
the shop's own fields, so a changed price is the one the shop shows and
charges. `money.change` carries the change and the money before, read out of
the misc save block; the row that adds the money applies only a positive
change and the row that takes it away only a negative one, so a change of
the other sign is applied as 0 and the money stays as it was. `heal` names
the party positions the heal covers and says where the heal came from: a
script for a Pokemon Centre, because the nurse runs the same script command,
and the power for the HP-restore power.

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

**What the progress builder fills.** The progress rows are in
`sdk/mod_event_session.c`, beside the session ones. `badge.get` carries the
badge the call names and the number of badges held once it is set, counted
out of the misc save block. `flag.set` carries the flag id the call names
and the value 1, because the bound function is the setting one: the game
clears a flag with another function, which is not a declared row. `dex.register`
carries the species read out of the Pokemon the call hands over, and whether
it was caught or only seen.

## Still to build

1. **The applier the nickname's row still needs.** `mon.nickname`'s `force`
   is marked, but the row that carries it has no applier, so a write to it
   has no effect (section 0, "What the port writes back"). The game has no
   call that forces the prompt, so the hook the port would need does not
   exist to be hooked.
2. **The fields the call does not carry.** The builders fill what the bound
   call and the game's own accessors give them; the fields they leave at 0
   are listed with the reason in `docs/mod-events.md`, section 1, "What the
   builders fill, and what they cannot". Filling one of them needs a hook
   the port does not have yet, and that hook is what is left to build
   (`progress/PAYLOAD_SURVEY.md` holds the survey behind them).

## Related documents

- `docs/mod-events.md`: the events, the handler contract and the state calls.
- `docs/mod-abi.md`: `BwEvent`, `BwMonRef` and the version rule.
- `docs/mod-implementation.md`: the builders and the contexts in code.
- `progress/PAYLOAD_SURVEY.md`: where each value comes from in the game.
