# Progression payloads

This document specifies the payload of the 52 progression events of
`docs/mod-events.md`, section 1. The event names are that document's; this
document adds the fields a handler receives.

The port owns the payload struct and fills it at the call. A mod is compiled C
and reads the struct at a fixed layout, so the game's own structs are not in it
(`docs/mod-events.md`, section 5). The payload is part of the interface version:
adding a field is cheap, removing one is not, so every field below is generous.

A payload is a situation, not an entity. Each event asks what a mod needs in
order to decide and act: the subject, its owner, the cause, and the
circumstances. Each field is attributed to the parameter, global or struct field
it comes from, with the file and line read. A field is obtainable from the
call's arguments, from a global or struct the port can read there, from a call
the port can make, from a constant, or from the tracked context of section 2.
No field is invented.

The sources are aletheia's matched files under
`<home>\OneDrive\Documents\bw_decomp\src\matched`. The address and
region of each event are `mod/declared_set.txt`.

## 1. What a payload carries

Four things, for every event in the area:

- **Subject.** The mon, the egg, the flag, the dex entry, the mission, the
  badge. In full: species, form, level, held item, move, slot, id, count, value.
- **Owner.** The player or trainer the subject belongs to, because that is what
  attributes it in a multiplayer or soullink game.
- **Cause.** What led to it: the field script VM, the script work, the calling
  command, the script frame. The port tracks the current VM (section 2).
- **Circumstances.** The party and box state, the block the value lives in, the
  prior value, the date, the surrounding counts. What a handler needs to decide,
  not only to log.

## 2. The context the area needs

The port tracks the following. A field named in a payload table as `context`
comes from here. A stale context field is worse than a missing one, so each is
given with what feeds it and where it clears.

### 2.1 The script VM and the script work (the cause)

Every field VM command in this area is a handler with the signature
`int handler(void *vm, void *work)`. The first argument is the VM, the second
is the work object the game passes. `SCRCMD_WORK_GetGameData` reads the game
data through the work's pointer
(`ov010_func_0215A5E4.c:26`, `((struct Obj00005 *)a0)->f20` then `->f8`);
`SCRCMD_WORK_GetGameSystem` reads the game system through the same pointer
(`ov010_func_0215A5DC.c:26`, `->f4`); `SCRCMD_WORK_GetScriptWork` reads the
script work through the same pointer (`ov010_func_0215A5F4.c:31`, `->f0`).
`SCRCMD_WORK_GetHeapId` reads a `u16` at the work's `+0x00`
(`ov010_func_0215A5CC.c:25`).

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

### 2.2 The party and the box (the circumstances)

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

### 2.3 The event work block (the flag values)

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

### 2.4 The misc save block 0x34 (badges, research, money, slot state)

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

### 2.5 The Pokedex block 0x37 (the dex state)

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

### 2.6 The badge date block 0x21

`TRCARD_SIGN_SetBadgeDate(obj, i, a, b, c)` stamps record `i` with three bytes,
marks the parallel entry `obj->unk638[i] = 0xF0`, and records the wall clock as a
second count (`arm9/unit_02008A7C.c:93-108`). The object is save block 0x21,
reached by `GAME_DATA_GetSaveBlock21` (`ov010_func_0215AC68.c:49`). The date
parts come from `GF_STANDARD_SetField00_F4E4` filling a four-int record
(`ov010_func_0215AC68.c:48`). What feeds it: the badge award path
`flag.misc.set`. Where it resets: save block state, durable.

## 3. The badge is state, not a call

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

## 4. Per-event payloads

Each table lists the event's own fields. Every field is obtainable as the
"Where it comes from" column states. `context` means the tracked state of
section 2.

### 4.1 Party and box adds

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

### 4.2 Moves and tutors

#### `move.learn` (0x021C360C, `SODATEYA_LearnMove`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mon` | handle | call arg `a` | the subject |
| `move` | u32 | call arg `b`, `ov021_func_021C360C.c:32,37` | the move being taught |
| `knows_already` | u32 | `SODATEYA_MonKnowsMove(a, b)`, `ov021_func_021C360C.c:37` | whether the teach is a no-op |
| `free_slot` | u32 | the loop index `i` whose `POKE_TOOL_GetPartyValue(a, t.v[i], 0)` is 0, `ov021_func_021C360C.c:38-40` | the slot the move lands in |
| `slot_field_ids` | u32[4] | the constant table `d_021DA8C8`, `ov021_func_021C360C.c:34,39` | the four move-slot ids |
| `pushed` | boolean | whether the loop fell through to `POKE_TOOL_PushMove`, `ov021_func_021C360C.c:44` | the move went to the push path |

#### `move.push` (0x020180F4, `POKE_TOOL_PushMove`, override)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mon` | handle | call arg `p` | the subject |
| `move` | int | call arg `move` | the move pushed into a free slot |
| `target` | handle | the thunk `POKE_TOOL_PushBoxMove`, `arm9/func_020180F4.c:32` | the original the slot wraps |

`move.push` is an override point, so it fires whenever the game pushes a move,
not one semantic moment (`docs/mod-events.md`, section 0).

#### `move.slot` (0x020181F4, `POKE_TOOL_SetMoveSlot`, override)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mon` | handle | call arg `p` | the subject |
| `move` | u16 | call arg `move` | the move written |
| `slot` | u32 | call arg `slot` | the slot written |
| `move_field` | u32 | `0x36 + slot`, written at `arm9/unit_020181A0.c:86` | the move id field |
| `pp_up_field` | u32 | `0x3E + slot`, cleared at `arm9/unit_020181A0.c:87` | the PP-up field |
| `pp_field` | u32 | `0x3A + slot`, set at `arm9/unit_020181A0.c:91` | the PP field |

#### `move.set` (0x0215BD2C, `SCRCMD_PROC_SetMonMove`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `c` | the cause |
| `party_slot` | u16 | first read, `ov010_func_0215BD2C.c:54` | which party member |
| `move_slot` | u16 | second read, `ov010_func_0215BD2C.c:55` | which move slot |
| `move` | u16 | third read, `ov010_func_0215BD2C.c:56` | the move, 0 to delete |
| `mon` | handle | `SCRCMD_PROC_GetPartyMember(vm, slot, &mon)`, `ov010_func_0215BD2C.c:57` | the subject |
| `operation` | enum | the branch: push when `moveSlot > 4`, delete when `move == 0`, set otherwise, `ov010_func_0215BD2C.c:61-82` | which write happened |
| `shifted` | boolean | the delete loop shifting ids 0x36/0x3A/0x3E down, `ov010_func_0215BD2C.c:63-80` | whether later moves moved |

#### `move.get` (0x0215BCE8, `SCRCMD_PROC_GetMonMove`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a0` | the cause |
| `party_slot` | u32 | `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `ov010_func_0215BCE8.c:36` | which member |
| `move_slot` | u32 | second read, `ov010_func_0215BCE8.c:37` | which slot |
| `mon` | handle | `SCRCMD_PROC_GetPartyMember(a1, slot, &v)`, `ov010_func_0215BCE8.c:38` | the subject |
| `move` | u32 | `POKE_TOOL_GetPartyValue(v, slot + 0x36, 0)`, `ov010_func_0215BCE8.c:39` | the move in the slot |

#### `tm.tutor.select` (0x021B61E8, `SCRCMD_POKEMON_FLD_SelectTutorMon`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | `SCRCMD_WORK_GetScriptWork(b)`, `ov021_func_021B61E8.c:48` | the cause |
| `mode` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `ov021_func_021B61E8.c:45` | the tutor kind |
| `out1` | pointer | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `ov021_func_021B61E8.c:46` | the result word |
| `out2` | pointer | second pointer read, `ov021_func_021B61E8.c:47` | the second result word |
| `candidate_mask` | u8 | `SCRCMD_POKEMON_FLD_FindTutorCandidates(a,b,mode,&c,0)`, `ov021_func_021B61E8.c:50` | which mons can be tutored |
| `tutor_move_default` | u32 | the switch on `mode`: 0xFFFE, 0xFFFD, 0x1B2, else 0xA5, `ov021_func_021B61E8.c:53-66` | the move the flow defaults to |
| `select_event` | handle | `EVENT_POKE_STATUS_CreatePokeSelectMoveEvent(t, o1, o2, c, v)`, `ov021_func_021B61E8.c:67` | the selection task |
| `result` | u16 | `*o1 = 0` when no candidate, `ov021_func_021B61E8.c:51` | whether a selection started |

#### `tm.tutor.move` (0x021B6124, `SCRCMD_POKEMON_FLD_GetMonTutorMove`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `b` | the cause |
| `sel` | u32 | first read `x`, `ov021_func_021B6124.c:37` | the tutor selector |
| `party_slot` | u32 | second read `y`, `ov021_func_021B6124.c:38` | which member |
| `mon` | handle | `SCRCMD_PROC_GetPartyMember(b, y, &rec)`, `ov021_func_021B6124.c:40` | the subject |
| `tutor_move` | u32 | `SCRCMD_POKEMON_FLD_GetTutorMoveId(rec, x)`, `ov021_func_021B6124.c:41` | the move the tutor would teach |
| `found` | boolean | whether `GetPartyMember` returned 1, `ov021_func_021B6124.c:40` | whether the member exists |

#### `tm.tutor.status` (0x021B6164, `SCRCMD_POKEMON_FLD_CheckMonTutorStatus`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `b` | the cause |
| `sel` | u32 | first read `x`, `ov021_func_021B6164.c:44` | the tutor selector |
| `party_slot` | u32 | second read `y`, `ov021_func_021B6164.c:45` | which member |
| `mon` | handle | `SCRCMD_PROC_GetPartyMember(b, y, &rec)`, `ov021_func_021B6164.c:48` | the subject |
| `tutor_move` | u32 | `SCRCMD_POKEMON_FLD_GetTutorMoveId(rec, x)`, `ov021_func_021B6164.c:52` | the move |
| `knows_move` | boolean | `SCRCMD_PROC_CheckMonKnowsMove(rec, move)`, `ov021_func_021B6164.c:52` | why the status is 3 |
| `friendship` | u32 | `POKE_TOOL_GetPartyValue(rec, 9, 0)`, `ov021_func_021B6164.c:54` | why the status is 2 |
| `required_friendship` | u32 | `SCRCMD_POKEMON_FLD_GetTutorFriendship(x)`, `ov021_func_021B6164.c:54` | the friendship threshold |
| `group_ok` | boolean | `SCRCMD_POKEMON_FLD_CheckTutorGroup(rec, x)`, `ov021_func_021B6164.c:50` | whether the mon is in the tutor group |
| `status` | u16 | written to `*o`: 0 ready, 1 group, 2 friendship, 3 known, `ov021_func_021B6164.c:47-58` | the tutor decision |

#### `tm.tutor.id` (0x021B5F24, `SCRCMD_POKEMON_FLD_GetTutorMoveId`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mon` | handle | call arg `a` | the subject |
| `sel` | int | call arg `sel` | the tutor selector |
| `compat` | u32 | `POKE_TOOL_CheckTutorCompat(a, v)` for each candidate `v`, `ov021_func_021B5F24.c:28-43` | which compatibility holds |
| `move_id` | u32 | the constant returned per selector: 0x152/0x133/0x134, 0x1B2, 0x208/0x207/0x206, else 0x133, `ov021_func_021B5F24.c:29-46` | the move the tutor gives |

#### `tm.type` (0x021B62C8, `SCRCMD_POKEMON_FLD_GetMonTypeForTm`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `b` | the cause |
| `party_slot` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `ov021_func_021B62C8.c:43` | which member |
| `tm_item` | u32 | `FILE_UTIL_GetId(0x151)`, `ov021_func_021B62C8.c:44` | the TM item the type is asked for |
| `mon` | handle | `SCRCMD_PROC_GetPartyMember(b, slot, &x)`, `ov021_func_021B62C8.c:45` | the subject |
| `compat` | u32 | `POKE_TOOL_CheckTmCompat(x, tm_item)`, `ov021_func_021B62C8.c:47` | whether the TM is usable |
| `hidden_power_type` | u32 | `POKE_TOOL_GetHiddenPowerType(x)`, `ov021_func_021B62C8.c:50` | the type written to the result |
| `result` | u16 | written to `*p`: the type, or 0xFFFF, `ov021_func_021B62C8.c:46,48,50` | the answer |

#### `move.relearn.list` (0x0216F948, `WAZA_OSHIE_CreateMoveList`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `mon` | handle | call arg `mon` | the subject |
| `species` | u16 | `POKE_TOOL_GetPartyValue(mon, 5, 0)`, `ov010_func_0216F948.c:84` | which learnset loads |
| `form` | u8 | `POKE_TOOL_GetPartyValue(mon, 0x6F, 0)`, `ov010_func_0216F948.c:85` | form variant |
| `level` | u8 | `POKE_TOOL_GetPartyValue(mon, 0x9E, 0)`, `ov010_func_0216F948.c:86` | the level cap on the list |
| `current_moves` | u16[4] | `POKE_TOOL_GetPartyValue(mon, 0x36 + i, 0)`, `ov010_func_0216F948.c:87-88` | the moves excluded from the list |
| `list` | pointer | the returned `0x34`-byte list, `ov010_func_0216F948.c:90,111` | the relearnable moves |
| `list_count` | u8 | the collected count `n`, `ov010_func_0216F948.c:112` | how many are relearnable |
| `heap_id` | u32 | call arg `heap` | allocation tag |

#### `move.relearn` (0x0215C89C, `SCRCMD_PROC_CallMoveRelearn`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `vm` | the cause |
| `script_work` | handle | `SCRCMD_WORK_GetScriptWork(vm)`, `ov010_func_0215C89C.c:94` | the script frame |
| `party_slot` | u32 | `SCRIPT_WORK_ReadWorkValue(c,vm)`, `ov010_func_0215C89C.c:99` | which member |
| `mon` | handle | `POKEPARTY_GetMember(GAME_DATA_GetMyParty(w), idx)`, `ov010_func_0215C89C.c:100` | the subject |
| `session` | handle | `GAME_DATA_GetMyStatus(w)`, `ov010_func_0215C89C.c:101` | the owner session |
| `moves` | pointer | `WAZA_OSHIE_CreateMoveList(mon, 4)`, `ov010_func_0215C89C.c:102` | the relearn list |
| `proc_data` | handle | `WAZA_OSHIE_Create(4)` and `gMoveRelearnProcData`, `ov010_func_0215C89C.c:103,108` | the proc the screen runs as |
| `result_ptr` | pointer | `SCRIPT_WORK_ReadWorkPointer(c,vm)`, `ov010_func_0215C89C.c:98` | the result destination |

#### `move.relearn.count` (0x0215BFD4, `SCRCMD_PROC_GetRelearnableMoveCount`, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `vm` | handle | call arg `a1` | the cause |
| `party_slot` | u32 | `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `func_0215BFD4.c:48` | which member |
| `mon` | handle | `SCRCMD_PROC_GetPartyMember(a1, slot, &v0)`, `func_0215BFD4.c:58` | the subject |
| `list` | pointer | `WAZA_OSHIE_CreateMoveList(mon, heap)`, `func_0215BFD4.c:64` | the relearn list |
| `count` | u16 | `WAZA_OSHIE_IsListEntryValid(buf)` written to `*q`, `func_0215BFD4.c:66,68` | the count |

### 4.3 Trades and mon selection

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

### 4.4 Pokedex

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

### 4.5 Research

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

### 4.6 Flags

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

#### `flag.write` (0x0202F190, `SAVE_EVENT_Write`, override)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `record` | handle | call arg `a0`, passed through `POKE_TOOL_Dummy` to `SAVE_EVENT_WriteMode`, `arm9/func_0202F190.c:31` | the record the write is bracketed on |
| `mode` | u32 | call arg `a1`, the switch selector in `SAVE_EVENT_WriteMode`, `arm9/func_0202F1B4.c:48` | which write path runs |
| `arg_c` | u32 | call arg `a2`, stored by `SAVE_EVENT_StoreRecord`, `arm9/func_0202F1B4.c:52` | the record payload |
| `event_id` | u32 | call arg `a3`, the id `SAVE_EVENT_WriteEventIdWithDate` writes, `arm9/func_0202F1B4.c:51` | the event id stamped |
| `arg_e` | u16 | call arg `a4`, stored by `SAVE_EVENT_StoreRecord`, `arm9/func_0202F1B4.c:52` | the record payload |

The catalogue binds `flag.write` to `SAVE_EVENT_Write` and states it writes an
event flag to the save block (`docs/mod-events.md`, section 1). The source
contradicts that: `SAVE_EVENT_Write` forwards to `SAVE_EVENT_WriteMode`, a
seven-way writer over a box record keyed on the mode `a1`, with a date stamped by
`SAVE_EVENT_WriteEventIdWithDate` (`arm9/func_0202F190.c:31`,
`arm9/func_0202F1B4.c:44-84`). The event-flag bitmap write is
`ZONEDATA_SetEventFlag` (0x02014314), not this function. So a mod that wants the
flag bitmap write gets nothing from `flag.write`; the flag id is not an
argument. This is a name and address mismatch to fix.

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

### 4.7 Event work

#### `event.work` (0x0201296C, 0x02012FE4, state)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event_work` | handle | `GAME_DATA_GetEventWork(game_data)`, a `u32` at game data `+0x1AC`, `arm9/unit_02012924.c:189-192` | the live flag block |
| `event_work_save` | handle | `GAME_DATA_GetEventWorkFromSave(sys)`, `func_020071CC(sys, 0x2d)`, `arm9/func_02012FE4.c:17` | the saved flag block |
| `flag_id` | u32 | the id the caller passes to `ZONEDATA_CheckEventFlag`, `arm9/func_020142E8.c:47` | the flag read |
| `value` | u32 | `ZONEDATA_CheckEventFlag(block, id)`, `arm9/func_020142E8.c:47-56` | the answer |

`event.work` is the flag bitmap itself, not one flag. The port exposes the block
and the flag accessors of section 2.3.

### 4.8 Badges

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

## 5. Fields a mod would want that cannot be obtained

- **The nature, ability and IVs of a script-added mon before the add.** They are
  drawn inside `SCRCMD_PROC_CreateMonFromSpec` by `POKE_TOOL_CreatePersonalRnd`
  (`ov010_func_0215C40C.c:57`). The add events fire before the draw, so a mod
  reads them from the added mon after, not from the payload.
- **The flag bitmap write behind `flag.write`.** The bound address is
  `SAVE_EVENT_Write`, a box-record writer, not `ZONEDATA_SetEventFlag`
  (`arm9/func_0202F190.c:31`, `arm9/func_0202F1B4.c:44-84`). No flag id is an
  argument. The bitmap write has no declared override point.
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

## 6. The thinnest payloads

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
