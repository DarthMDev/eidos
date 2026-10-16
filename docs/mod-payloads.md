# Event payloads

This document is the port-side specification of the payload a mod receives at
each event. It sits under `docs/mod-events.md`, which fixes the event names,
the addresses they bind to and the state API, and beside
`docs/mod-interface.md`, which fixes the hookable set, the slot, the handler
type and the interface version.

An event that only announces that something happened is nearly useless. A mod
cannot act on "a Pokemon fainted" without knowing which one, whose side, at
what level, with what left, by what move, and on what turn. The payload is the
substance of the event. This document fixes the payload for the battle area of
`docs/mod-events.md`, section 1, and leaves the other areas to later
workstreams.

Every field is attributed to the parameter, global, struct field or context
field it comes from, with the file and line that was read. A field the call
cannot supply is taken from the battle context (section 0a) or, when no source
can supply it, the event that does carry it is named. The game functions are
read from aletheia's matched source under
`<home>\OneDrive\Documents\bw_decomp\src\matched`, revision
`80f06a261e8731f2db94c1a66627bb351ad47673`.

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

The battle area of `docs/mod-events.md`, section 1, has 48 rows in three
kinds (`docs/mod-events.md`, "The three kinds"):

- **36 events.** A slot fires the event and a payload struct is built. Each
  gets a field table below.
- **5 state entries.** A mod reads the value through an accessor, and no slot
  fires. There is no payload struct. Each is stated as a read, with the value
  and the inputs the accessor computes from.
- **7 override points.** A mod replaces or wraps the function, and the values
  it sees are the function's own arguments and return, not a `BwEvent`. Each
  is stated as an argument set.

### Source files

Short names used in the tables below map to these files under
`<home>\OneDrive\Documents\bw_decomp\src\matched`:

- `ov010_func_0216E818.c`, `ov010_func_0216EAB0.c`, `ov010_func_0216EE20.c`,
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

## 0a. The battle context

The payload is the situation, not the entity. A handler needs to know not only
what happened but why, and the why is usually not in scope at the call. The
damage that faints a Pokemon is applied a call before the faint reward step,
so the attacker, the move and the damage are off the stack by the time
`battle.faint` fires. The port therefore keeps a port-owned battle context: a
small record it updates as the battle's own events pass, and reads when it
builds a payload. The context is part of the ABI, because a payload reads it
(`docs/mod-events.md`, section 5).

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
| `battle_kind` | `encounter.start`, parameter `a4` (`ov010_func_0216E818.c:53`); `battle.kind.notify`, parameter `id` (`ov010_func_0216EE20.c:27`) | `battle.start` | tell a wild battle from a scripted or multi one |
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

### Why the reset points are what they are

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

## 1. Encounter

### `encounter.start` (0x0216E818, ov010, block)

Function `EVENT_BATTLE_CreateEncounterTask(int a0, int a1, int a2, int a3,
int a4, int a5, int a6)` (`ov010_func_0216E818.c:53`). The slot fires at
entry, before the task is created.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `ctx` | u32 | parameter `a0` (`:53`) | the game system; the port reads `GAMESYSTEM_GetGameData` from it (`:64`) |
| `field` | u32 | parameter `a1` (`:53`) | the field object; `FIELDMAP_GetEncount` reads its encounter table (`:63`) |
| `mode` | u32 | parameter `a2` (`:53`) | passed as the setup `mode` (`:66`); single, double or multi |
| `flag` | u32 | parameter `a3` (`:53`) | passed as the setup `flag` (`:66`) |
| `battle_kind` | u16 | parameter `a4` (`:53`) | `EVENT_BATTLE_DispatchBattleKindNotify(a4, 0)` (`:67`) |
| `extra` | u32 | parameter `a5` (`:53`) | passed through to the setup (`:66`) |
| `flags` | u32 | parameter `a6` (`:53`) | bit 0 sets status flag 0x100 (`:72`) |
| `encounter` | u32 | `FIELDMAP_GetEncount((u8 *)a1)` (`:63`) | the zone's encounter record; a call the port makes at the slot |

A mod wants the mode and the kind to tell a wild battle from a scripted one,
and the encounter record to key the route. Nuzlocke's first-encounter rule and
Soullink's mirror both bind here (`docs/modding.md`, "Nuzlocke").

Not reachable at entry: the task (created at `:61`) and the setup struct
(created at `:65`). The species is not chosen at entry. The two events that
carry them instead are `battle.setup.wild`, which fills the wild setup
(`arm9/func_0201347C.c:90`), and `encounter.start.mon`, which names the static
mon's species and level (`ov010_func_0216EAB0.c:86`). A rule that needs the
species reads it there, not here.

### `encounter.start.mon` (0x0216EAB0, ov010)

Function `EVENT_BATTLE_CreateEncounterTaskWithMon(void *a0, u32 a1, u32 a2,
u32 a3, u32 a4, u32 a5, u32 a6, u32 idx)` (`ov010_func_0216EAB0.c:70`). It
calls `encounter.start` and then overwrites the party mon from a save entry.

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
(`docs/modding.md`, "Nuzlocke"; `docs/mod-events.md`, "The owner's examples,
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

### `battle.item.use` (0x021CB730, ov093)

Function `BTL_SERVER_FLOW_ProcBagItemUse(u8 *p, u8 *a, u16 move, u8 arg,
u8 slot)` (`ov093_func_021CB730.c:81`). It walks the item effect table and
updates the bag.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:81`) | the battle flow struct |
| `mon` | BwPokeRef | parameter `a` (`:81`); `BTL_POKEPARAM_GetPokeId(a)` (`:100`) | the Pokemon the item is used on |
| `item` | u16 | parameter `move` (`:81`) | the item id; it is also the ball item in the throw path (`:121`) |
| `arg` | u8 | parameter `arg` (`:81`) | the extra argument the effect callbacks take (`:177`) |
| `slot` | u8 | parameter `slot` (`:81`) | the target slot; 6 means a party item (`:103`) |
| `side` | u8 | `BTL_MAIN_PokeIdToClientIdThunk(poke_id)` (`:100`) | the client the item is used on |
| `target` | BwPokeRef | `BTL_SLOT_GetPoke(party, slot)` (`:107`) | the target when the slot is a party slot |
| `item_state` | u32 | `func_021CBA30(p + 0x1ab8, monId)` (`:109`) | the item's state, 6 means disabled |
| `effect_result` | u32 | the effect callback return (`:177`, `:200`) | whether the effect applied |

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

### `battle.item.cure` (0x021C557C, ov093)

Function `BTL_SERVER_FLOW_CureMajorStatusWithMessage(u8 *p, u8 *a, int k,
int flag)` (`ov093_func_021C557C.c:28`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:28`) | the battle flow struct |
| `mon` | BwPokeRef | parameter `a` (`:28`); `BTL_POKEPARAM_GetPokeId(a)` (`:30`) | the Pokemon being cured |
| `status_kind` | int | parameter `k` (`:28`) | the status kind the message switches on (`:35`) |
| `show_message` | int | parameter `flag` (`:28`) | whether the message is printed (`:34`) |

### `battle.item.check` (0x021CBFA0, ov093)

Function `BTL_SERVER_FLOW_ItemEffectCheckSlotMessage(u8 *p, u8 *q)`
(`ov093_func_021CBFA0.c:36`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `flow` | u32 | parameter `p` (`:36`) | the battle flow struct |
| `mon` | BwPokeRef | parameter `q` (`:36`); `BTL_POKEPARAM_GetPokeId(q)` (`:41`) | the Pokemon the slot message is for |
| `second_pair` | int | `BTL_MAIN_IsPokeIdInSecondPair(poke_id)` (`:41`) | whether the Pokemon is in the second pair |

### `battle.bag.add` (0x021B9090, ov093)

Function `BTL_MAIN_AddItemToBag(u8 *p, u32 k, u32 x)`
(`ov093_func_021B9090.c:20`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `main` | u32 | parameter `p` (`:20`) | the battle main struct |
| `side` | u32 | parameter `k` (`:20`) | the side the item returns to; compared against `p[0x46c]` |
| `item` | u32 | parameter `x` (`:20`) | the item id added |
| `bag` | u32 | `p->f0` (`:20`) | the bag owner |
| `count` | u32 | the literal 1 in `func_02007F1C(..., x, 1, p->f468)` (`:20`) | the count added |
| `bag_field` | u16 | `p->f468` (`:20`) | the bag field the item is added to |

### `battle.bag.sub` (0x021B90BC, ov093)

Function `BTL_MAIN_SubItemFromBag(u8 *p, u8 k, u32 x)`
(`ov093_func_021B90BC.c:20`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `main` | u32 | parameter `p` (`:20`) | the battle main struct |
| `side` | u8 | parameter `k` (`:20`) | the side; compared against `p[0x46c]` |
| `item` | u32 | parameter `x` (`:20`) | the item id taken |
| `bag` | u32 | `p->f0` (`:20`) | the bag owner |
| `count` | u32 | the literal 1 in `func_02007E50(..., x, 1, p->f468)` (`:20`) | the count taken |
| `bag_field` | u16 | `p->f468` (`:20`) | the bag field |

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

## 13. Worked examples

The three events that matter most to the challenge runs are a faint, a catch
attempt and a level up (`docs/mod-events.md`, section 1;
`progress/EVENT_SURVEY.md`, sections 2 and 4). Each is shown in full, with
what a Nuzlocke, a Soullink and a monotype rule do with it.

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

## 14. Still to build

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
   fires before the task and the setup exist (`ov010_func_0216E818.c:61`,
   `:65`). The work is to fix whether these fire at entry, after the original,
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
10. **Cover the other areas.** The field, inventory, progression, save and
    networking areas of `docs/mod-events.md`, section 1, follow the same
    pattern: a payload struct per event, a read per state entry, an argument
    set per override point. They are for later workstreams and are not covered
    here beyond this note.
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
