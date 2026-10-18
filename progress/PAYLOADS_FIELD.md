# Field and interaction payloads

A payload is the situation an event carries, not the entity it names. For each
event in the field and interaction area of `docs/mod-events.md`, section 1, this
document fixes the fields a mod reads at the handler, where each field comes
from, and why a mod wants it.

The port owns each payload struct and fills it at the call. A mod is compiled C
and reads the struct at a fixed layout, so the game's own structs are not
exposed (`docs/mod-events.md`, section 5). The fields below are the members the
port copies out of the game's arguments, globals and structs into the port's
struct.

Every field is attributed to the parameter, global or struct field it comes
from, with the file and line it was read at. The sources are aletheia's matched
sources under `src/matched/`, at the checkout
`the aletheia checkout`. A citation `ov010_func_021643E8.c:59`
means `src/matched/ov/ov010_func_021643E8.c`, line 59. Addresses are the
Black-image addresses the catalogue names.

A field is included only when it is obtainable at the call: from the call's
arguments, from a global or struct the port reads there, from a call the port
makes there, or from the tracked field context of section 1. A field the call
site cannot supply is named in section 11 instead of invented.

The owner of a field subject is the local player unless the event is a comm
event. The local player's record is reached through `GAME_DATA_GetMyStatus`
(0x02012944, `ov021_func_021D2B54.c:102`) or `GAME_DATA_GetMyPlayerWork`
(0x020127A4, `ov010_func_021644C4.c:37`) from the game data argument. A comm
event's owner is the peer slot the event names.

## 0. How to read a table

- **Field.** The port's member name in the event struct.
- **Type.** The member's width and shape. A position is a fixed-point
  `VecFx32` of three `s32` unless the row says grid.
- **Where it comes from.** The argument, global, struct field or tracked
  context, with the file and line.
- **Why.** What a mod decides or acts on with it.

The event kind is the catalogue's: an event fires when the slot is entered, an
override replaces the function, a state entry is read through an accessor. An
override's payload is the situation at the call; its return is the mod's.

## 1. The field context

Some fields are not reachable at the call site. They were in scope one call
earlier, or they live in state the game does not pass. The port tracks them in a
field context and copies them into the payload. A stale context field is worse
than a missing one, so each row names the event that feeds it and where it is
cleared.

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

## 2. Map and route

### `route.change`

`FIELD_MAP_CHANGE_SetupZone` (0x021643E8, ov010), `void FIELD_MAP_CHANGE_SetupZone(void *a, void *b)`
(`ov010_func_021643E8.c:55`). `a` is the game data, `b` is the new zone id.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `zone_id` | u32 | Argument `b`, `ov010_func_021643E8.c:55`, used as `(u32)b` at `:59` | Names the zone the field loads, so a mod can gate on the route |
| `prev_zone_id` | u32 | Context, the `zone_id` before the call, section 1 | Tells a mod where the player came from |
| `field_skill_effect` | u32 | `ZONEDATA_GetFieldSkillMapEffect((u32)b)`, `ov010_func_021643E8.c:59` | Names the field skill the map needs, such as Flash |
| `flash_active` | bool | `FIELD_STATUS_SetFlashActive(w, 0)`, `ov010_func_021643E8.c:60` | Reports that the zone cleared Flash |
| `save_block` | handle | `MOVE_POKEMON_SAVE_GetBlock(GAME_DATA_GetSaveControl(a))`, `ov010_func_021643E8.c:62` | The field-skill save block the zone resets |
| `event_work` | handle | `GAME_DATA_GetEventWork(a)`, `ov010_func_021643E8.c:64` | The event work the zone resets at flag 0x964 |
| `hidden_items_respawned` | bool | `EVENTDATA_SYSTEM_RespawnHiddenItems(c)`, `ov010_func_021643E8.c:66` | Reports that hidden items respawned for the zone |
| `zone_in_table` | bool | `ZONEDATA_IsZoneInTableDFB8((u32)b)`, `ov010_func_021643E8.c:67` | Distinguishes a table zone, which a mod may treat specially |
| `owner` | handle | The game data `a`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the change to the local player |
| `cause` | enum | The context `last_warp` or the scripted map change, section 1 | Tells a mod what led to the change |

### `route.header`

`FIELD_MAP_CHANGE_ApplyZoneHeader` (0x021643AC, ov010), `void FIELD_MAP_CHANGE_ApplyZoneHeader(void *a, void *b)`
(`ov010_func_021643AC.c:38`). An override. `a` is the game data, `b` is the
field map.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `zone_id` | u16 | `FIELDMAP_GetZoneId(b)`, `ov010_func_021643AC.c:38` | The zone whose header applies |
| `prev_zone_id` | u16 | Context, section 1 | The zone left behind |
| `save_block` | handle | `MOVE_POKEMON_SAVE_GetBlock(GAME_DATA_GetSaveControl(a))`, `ov010_func_021643AC.c:38` | The save block the header seeds |
| `event_work` | handle | `GAME_DATA_GetEventWork(a)`, `ov010_func_021643AC.c:38` | The work reset at flag 0x964 |
| `effect_encount` | u32 | `FIELD_ENCOUNT_ST_GetEffectEncount((int)b)`, `ov010_func_021643AC.c:38` | The zone's effect-encounter state, useful to an encounter mod |
| `owner` | handle | The game data `a`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the header to the local player |

### `field.map.event`

`EVENT_MAPCHANGE_ChangeFieldEvent` (0x021841C0, ov020), `int EVENT_MAPCHANGE_ChangeFieldEvent(int ctx, int *state, u8 **pp)`
(`ov020_func_021841C0.c:110`). An override and a step machine. `ctx` is the
event system, `state` is the step index, `pp` points at the map-change work
`p` (`Obj01649`).

| Field | Type | Where it comes from | Why |
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

### `field.map.fixedzone`

`EVENT_MAPCHANGE_RunFixedZoneEvent` (0x02184C78, ov020), `int EVENT_MAPCHANGE_RunFixedZoneEvent(void *ctx, int *st, int *p)`
(`ov020_func_02184C78.c:37`). `ctx` is the event system, `st` is the step, `p`
is the change record.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `step` | u32 | `*st`, `ov020_func_02184C78.c:42` | Which step of the fixed-zone event runs |
| `anim_source` | handle | `p[0]`, `ov020_func_02184C78.c:39`, used at `:44` | The source of the proclink animation |
| `anim_target` | handle | `p[2]`, `ov020_func_02184C78.c:40`, used at `:44` | The target of the animation |
| `change_kind` | u32 | The literal `4` passed to `EVENT_MAPCHANGE_CreateChangeFieldEvent`, `ov020_func_02184C78.c:50` | Marks the change as a fixed-zone change |
| `change_record` | ptr | `p`, `ov020_func_02184C78.c:50` | The record the field change reads |

### `field.map.arrival`

`EVENT_MAPCHANGE_RunSituationWarpArrivalEvent` (0x02185694, ov020), `int EVENT_MAPCHANGE_RunSituationWarpArrivalEvent(int a0, int *st, int *p)`
(`ov020_func_02185694.c:55`). `a0` is the event system, `st` is the step, `p`
is the arrival record.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `step` | u32 | `st[0]`, `ov020_func_02185694.c:59` | Which step of the arrival runs |
| `system` | handle | `p[0]`, `ov020_func_02185694.c:61` | The system the field initialises from |
| `field_map` | handle | `p[1]`, `ov020_func_02185694.c:67` | The field map the BGM and season read |
| `zone_id` | u16 | `((short *)p)[6]`, `ov020_func_02185694.c:67` | The zone the arrival plays music for |
| `season` | u8 | `GAME_DATA_GetSeason((BigObj *)p[1])`, `ov020_func_02185694.c:67` | The season the arrival BGM depends on |
| `bgm` | u32 | `FIELD_SOUND_SYSTEM_GetFieldBgmForZoneEx(p[1], zone_id, season)`, `ov020_func_02185694.c:67` | The field BGM the arrival starts |
| `player_work` | handle | `GAME_DATA_GetMyPlayerWork(GAMESYSTEM_GetGameData(p[0]))`, `ov020_func_02185694.c:65` | The player record whose object code resets |
| `object_code` | u32 | The literal `0` set at `ov020_func_02185694.c:65` | Reports the player object code the arrival resets to |
| `source_zone` | u16 | Context `last_warp`, section 1 | Tells a mod which zone the player warped from |

## 3. Talking and the script search

### `field.talk.npc`

`FIELD_WFBC_DATA_LOCAL_CreatePersonTalkEvent` (0x021870C4, ov020), `void *FIELD_WFBC_DATA_LOCAL_CreatePersonTalkEvent(void *a0, void *a1)`
(`ov020_func_021870C4.c:77`). `a0` is the game system, `a1` is the field map.
Returns 0 when no comm work runs.

| Field | Type | Where it comes from | Why |
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

### `field.talk.npc.cmd`

`FIELD_WFBC_DATA_LOCAL_ScrCmdCallPersonTalkEvent` (0x02187098, ov020), `int FIELD_WFBC_DATA_LOCAL_ScrCmdCallPersonTalkEvent(void *unused, void *p)`
(`ov020_func_02187098.c:24`). `p` is the script command context.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `script_work` | handle | `SCRCMD_WORK_GetScriptWork(p)`, `ov020_func_02187098.c:28` | The VM work the talk event is called on |
| `game_system` | handle | `SCRCMD_WORK_GetGameSystem(p)`, `ov020_func_02187098.c:29` | The system the talk builds from |
| `field_map` | handle | `GAMESYSTEM_GetFieldMap(obj)`, `ov020_func_02187098.c:30` | The field map the talk reads |
| `task` | handle | `FIELD_WFBC_DATA_LOCAL_CreatePersonTalkEvent(obj, field_map)`, `ov020_func_02187098.c:30` | The talk task the command created |
| `cause` | enum | The script command itself, opcode of this handler | Tells a mod a script started the talk |

### `field.talk.npc.main`

`FIELD_WFBC_DATA_LOCAL_PersonTalkEventMain` (0x021871F8, ov020), `int FIELD_WFBC_DATA_LOCAL_PersonTalkEventMain(void *a, int *st, char *o)`
(`ov020_func_021871F8.c:90`). `a` is the task, `st` is the step, `o` is the
0x58-byte work block.

| Field | Type | Where it comes from | Why |
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

### `field.talk.comm`

`EVENT_COMM_COMMON_CreateTalkEvent` (0x0217FC08, ov018), `void *EVENT_COMM_COMMON_CreateTalkEvent(int a, int b, u8 *c, int d, int e, u16 f)`
(`ov018_func_0217FC08.c:37`). `a` is the game system, `b` and `d`, `e`, `f` are
comm parameters, `c` is the comm work.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `task` | handle | `func_020122C0(a, 0, main, 0x9c)` return, `ov018_func_0217FC08.c:39` | The comm talk task |
| `comm_work` | handle | Argument `c`, `ov018_func_0217FC08.c:37` | The comm work the talk reads |
| `talk_type` | u8 | `EVENT_COMM_COMMON_GetTalkType(q, c)`, `ov018_func_0217FC08.c:46` | The kind of comm talk, stored at work +0x41 |
| `owner` | handle | Argument `a`; the peer slot the comm work names | Attributes the talk to the local player or the peer |

### `field.talk.comm.main`

`EVENT_COMM_COMMON_TalkEventMain` (0x0217FE6C, ov018), `int EVENT_COMM_COMMON_TalkEventMain(void *a, int *state, Outer *c)`
(`ov018_func_0217FE6C.c:115`). `a` is the task, `state` is the step, `c` is the
0x98-byte work block.

| Field | Type | Where it comes from | Why |
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

### `field.talk.search`

`EVENTDATA_SYSTEM_SearchBgEventScriptIdTalk` (0x02162A54, ov010), `u16 EVENTDATA_SYSTEM_SearchBgEventScriptIdTalk(void *set, void *flags, const void *where, u16 type)`
(`ov010_func_02162A54.c:25`). It forwards all four arguments to
`EVENTDATA_SYSTEM_SearchBgEventScriptId` (`ov010_func_02162970.c:56`).

| Field | Type | Where it comes from | Why |
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

## 4. The field event checks

### `field.event.grid`

`FIELD_EVENT_CHECK_CheckGridEvents` (0x02189BD0, ov021), `int FIELD_EVENT_CHECK_CheckGridEvents(void *sys, struct Obj00020 *pos, int *outA, int *outB)`
(`ov021_func_02189BD0.c:207`). `sys` is the game system, `pos` is the field map,
`outA` and `outB` are output flags. This is the root of the A-press tree that
runs the talk, step, menu, surf and script checks in order.

| Field | Type | Where it comes from | Why |
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

### `field.event.rail`

`FIELD_EVENT_CHECK_CheckRailEvents` (0x0218A278, ov021), `int FIELD_EVENT_CHECK_CheckRailEvents(void *sys, struct Obj00020 *pos)`
(`ov021_func_0218A278.c:132`). The rail counterpart of the grid check. It
carries the same `in` fields as `field.event.grid` from
`FIELD_EVENT_CHECK_InitInput` (`ov021_func_0218A4E0.c:99`).

| Field | Type | Where it comes from | Why |
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

### `field.event.run`

`FIELD_EVENT_CHECK_Run` (0x0218A4BC, ov021), `int FIELD_EVENT_CHECK_Run(u32 a, u32 b, u32 c, u32 d)`
(`func_0218A4BC.c:31`). `a` is the game system, `b` is the field map.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `move_kind` | u32 | `FIELDMAP_GetMoveKind((u8 *)b)`, `func_0218A4BC.c:33` | Selects the grid or rail check, the branch the run takes |
| `system` | handle | Argument `a`, `func_0218A4BC.c:31` | The system the chosen check runs under |
| `field_map` | handle | Argument `b`, `func_0218A4BC.c:31` | The field map the chosen check reads |

## 5. Messages and windows

### `field.msg.talkwin.open`

`SCRCMD_MSGWIN_OpenTalkWin` (0x021B0ADC, ov021), `void SCRCMD_MSGWIN_OpenTalkWin(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g)`
(`ov021_func_021B0ADC.c:26`). `a` is the script work, `b` is the message
background base, `c` is the window index, `d`, `e`, `f`, `g` are window
parameters.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `window` | handle | `FIELD_MSGBG_AddTalkWin((u8 *)b, c, d, e, f, g)`, `ov021_func_021B0ADC.c:28` | The talk window the script opened |
| `msg_window` | u32 | `SCRCMD_WORK_SetMsgWindow((u8 *)a, window)`, `ov021_func_021B0ADC.c:28` | The message window the script now uses |
| `window_index` | u32 | Argument `c`, `ov021_func_021B0ADC.c:26` | The slot index, 0 to 2, the window takes |
| `check_flag` | u32 | The literal `2` set by `SCRIPT_WORK_SetCheckFlag`, `ov021_func_021B0ADC.c:29` | The script check flag the open sets |
| `cause` | enum | The script command itself, opcode of this handler | Tells a mod a script opened the window |

### `field.greeting.npc`

`SCRCMD_GetNpcGreetingMessage` (0x021D2B54, ov021), a state entry,
`int SCRCMD_GetNpcGreetingMessage(u32 a, u32 b)` (`ov021_func_021D2B54.c:84`).
`a` is the VM stream, `b` is the script command context. It writes a greeting
message id to the work pointer.

| Field | Type | Where it comes from | Why |
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

### `field.msg.plain`

`SCRCMD_MSGWIN_CmdPlainWinMsg` (0x021B10D0, ov021), `int SCRCMD_MSGWIN_CmdPlainWinMsg(void *a, int b)`
(`ov021_func_021B10D0.c:21`). It forwards to
`SCRCMD_MSGWIN_OpenPlainWinMsg(a, b, 0)` (`ov021_func_021B10D0.c:21`).

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `script_work` | handle | Argument `a`, `ov021_func_021B10D0.c:21` | The VM work the plain window opens on |
| `message_id` | int | Argument `b`, `ov021_func_021B10D0.c:21` | The message the window prints |
| `mode` | int | The literal `0` passed to `OpenPlainWinMsg`, `ov021_func_021B10D0.c:21` | Marks the plain window kind |

### `field.msg.yesno`

`SCRCMD_MSGWIN_CmdYesNoWin` (0x021B0130, ov021), `int SCRCMD_MSGWIN_CmdYesNoWin(u32 a, u32 b)`
(`ov021_func_021B0130.c:32`). `a` is the VM stream, `b` is the script command
context.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `out_slot` | ptr | `SCRIPT_WORK_ReadWorkPointer(a, b)`, `ov021_func_021B0130.c:34` | The work slot the answer lands in |
| `script_work` | handle | `SCRCMD_WORK_GetScriptWork(b)`, `ov021_func_021B0130.c:35` | The VM work the yes or no event is called on |
| `event` | handle | `SCRCMD_MSGWIN_CreateYesNoEvent(SCRCMD_WORK_GetGameSystem(b), b, r)`, `ov021_func_021B0130.c:37` | The yes or no event the script waits on |
| `message_id` | u32 | Argument `b` passed to the create, `ov021_func_021B0130.c:37` | The prompt the window shows |

### `field.msg.infosys`

`SCRCMD_MSGWIN_CmdInfoSysWinMsg` (0x021B04D8, ov021), `int SCRCMD_MSGWIN_CmdInfoSysWinMsg(u32 a, u32 b)`
(`ov021_func_021B04D8.c:40`). `a` is the VM stream, `b` is the script command
context.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `script_work` | handle | `SCRCMD_WORK_GetScriptWork(b)`, `ov021_func_021B04D8.c:45` | The VM work the info message uses |
| `work_value` | u32 | `SCRIPT_WORK_ReadWorkValue(a, b)`, `ov021_func_021B04D8.c:46` | The value the message expands |
| `window_kind` | u16 | `VM_GetU16(a)` passed to `OpenInfoSysWin`, `ov021_func_021B04D8.c:47` | The info window kind |
| `expanded` | u32 | `SCRCMD_MSGWIN_ExpandMessage(b, t, x)`, `ov021_func_021B04D8.c:48` | The expanded message text |
| `msg_window` | handle | `SCRCMD_WORK_GetMsgWindow(b)`, `ov021_func_021B04D8.c:49` | The window the message prints to |
| `cause` | enum | The script command itself, opcode of this handler | Tells a mod a script printed the message |

### `field.msg.show`

`SCRCMD_PROC_FLD_ShowMessage` (0x021B4894, ov021), `void SCRCMD_PROC_FLD_ShowMessage(char *w, u32 b)`
(`ov021_func_021B4894.c:48`). `w` is the field message object, `b` is the
message id.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `message_id` | u32 | Argument `b` to `func_02045EC0(w->f188, b)`, `ov021_func_021B4894.c:53` | The message the field prints |
| `msg_bank` | handle | `w->f188`, `ov021_func_021B4894.c:53` | The bank the message comes from |
| `wordset` | handle | `w->f190`, `ov021_func_021B4894.c:54` | The word set the message expands with |
| `dst_buf` | handle | `w->f198`, `ov021_func_021B4894.c:54` | The buffer the message is copied to |
| `print_stream` | handle | `w->f1B8` from `PRINTSYS_PrintStreamCallBack`, `ov021_func_021B4894.c:56` | The print stream the message runs on |
| `wait` | s8 | `MSG_SPEED_GetWait()`, `ov021_func_021B4894.c:57` | The text speed the message waits for |
| `window` | handle | `w->f17C`, `ov021_func_021B4894.c:59` | The window the message draws to |

## 6. Warps

Every warp event reads its operands from the script VM with `VM_GetU16`
(`ov021_func_021AFCD8.c:48` and the same in the siblings). The first operand is
the zone or warp id, the next two are x and z, and the last is the transition
or exit id. The position is `c << 16` and `d << 16`, a fixed-point cell.

### `field.warp.position`

`SCRCMD_WarpToPosition` (0x021AFCD8, ov021), `int SCRCMD_WarpToPosition(int a0, int a1)`
(`ov021_func_021AFCD8.c:35`). `a0` is the VM stream, `a1` is the script command
context.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `zone_id` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFCD8.c:48` | The destination zone, the subject of the warp |
| `x` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFCD8.c:49` | The destination x cell |
| `z` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFCD8.c:50` | The destination z cell |
| `transition` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFCD8.c:51` | The exit or transition id |
| `target_pos` | `VecFx32` | `arr = {c << 16, 0, d << 16}`, `ov021_func_021AFCD8.c:52` | The destination position in field units |
| `field_map` | handle | `GAMESYSTEM_GetFieldMap(h)`, `ov021_func_021AFCD8.c:47` | The field map the warp event reads |
| `warp_event` | handle | `EVENT_MAPCHANGE_CreateWarpEvent(h, a, b, arr, e, 0)`, `ov021_func_021AFCD8.c:56` | The warp event the game runs |
| `source_zone` | u16 | Context `last_warp`, section 1 | The zone the player warps from |
| `owner` | handle | The game data; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the warp to the local player |
| `cause` | enum | The script command itself, opcode of this handler | Tells a mod a script issued the warp |

### `field.warp.position.plain`

`SCRCMD_WarpToPositionPlain` (0x021AFD44, ov021), `int SCRCMD_WarpToPositionPlain(int a0, int a1)`
(`ov021_func_021AFD44.c:35`). The same operands as `field.warp.position` read
at `ov021_func_021AFD44.c:48-51`, and the same `target_pos` at `:52`.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `zone_id` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFD44.c:48` | The destination zone |
| `x` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFD44.c:49` | The destination x cell |
| `z` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFD44.c:50` | The destination z cell |
| `transition` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFD44.c:51` | The exit or transition id |
| `target_pos` | `VecFx32` | `arr`, `ov021_func_021AFD44.c:52` | The destination position |
| `warp_event` | handle | `EVENT_MAPCHANGE_CreateWarpEventPlain(h, a, b, arr, e)`, `ov021_func_021AFD44.c:56` | The plain warp event |
| `source_zone` | u16 | Context `last_warp`, section 1 | The zone warped from |

### `field.warp.position.event`

`SCRCMD_WarpToPositionEvent` (0x021AFDAC, ov021), `int SCRCMD_WarpToPositionEvent(int a0, int a1)`
(`ov021_func_021AFDAC.c:61`). The operands are read at `:79-81`.

| Field | Type | Where it comes from | Why |
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

### `field.warp.position.exit`

`SCRCMD_WarpToPositionFixedExit` (0x021AFE64, ov021), `int SCRCMD_WarpToPositionFixedExit(int a0, int a1)`
(`ov021_func_021AFE64.c:35`). The operands are read at `:48-51`.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `zone_id` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFE64.c:48` | The destination zone |
| `x` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFE64.c:49` | The destination x cell |
| `z` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFE64.c:50` | The destination z cell |
| `transition` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFE64.c:51` | The fixed exit id |
| `target_pos` | `VecFx32` | `arr`, `ov021_func_021AFE64.c:52` | The destination position |
| `warp_event` | handle | `EVENT_MAPCHANGE_CreateFixedExitTypeWarpEvent(h, a, b, arr, e)`, `ov021_func_021AFE64.c:56` | The fixed-exit warp event |

### `field.warp.fixedzone`

`SCRCMD_WarpToFixedZone` (0x021AFECC, ov021), `int SCRCMD_WarpToFixedZone(u32 a, u32 b)`
(`ov021_func_021AFECC.c:37`). `b` is the script command context. It reads no
operands, so the destination comes from the zone record.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `system` | handle | `SCRIPT_WORK_GetGameSystem(SCRCMD_WORK_GetScriptWork(b))`, `ov021_func_021AFECC.c:39` | The system the fixed-zone event runs under |
| `field_map` | handle | `GAMESYSTEM_GetFieldMap(p)`, `ov021_func_021AFECC.c:40` | The field map the event reads |
| `warp_event` | handle | `EVENT_MAPCHANGE_CreateFixedZoneEvent(p, q)`, `ov021_func_021AFECC.c:43` | The fixed-zone warp event |
| `source_zone` | u16 | Context `last_warp`, section 1 | The zone warped from |
| `destination` | u16 | Not obtainable at the call; the target zone is in the zone record, section 11 | A mod cannot read the destination here |

### `field.warp.position3d`

`SCRCMD_WarpToPosition3D` (0x021AFEFC, ov021), `int SCRCMD_WarpToPosition3D(int a0, int a1)`
(`ov021_func_021AFEFC.c:40`). The operands are read at `:54-58`.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `zone_id` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFEFC.c:54` | The destination zone |
| `x` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFEFC.c:55` | The destination x |
| `y` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFEFC.c:56` | The destination y, the height |
| `z` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFEFC.c:57` | The destination z |
| `transition` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFEFC.c:58` | The transition id |
| `target_pos` | `VecFx32` | `arr = {c << 16, d << 16, e << 16}`, `ov021_func_021AFEFC.c:60` | The three-dimensional destination |
| `warp_event` | handle | `EVENT_MAPCHANGE_CreateWarpEventDefaultTransition(h, a, b, arr, f)`, `ov021_func_021AFEFC.c:64` | The default-transition warp event |

### `field.warp.rail`

`SCRCMD_WarpToRailLocation` (0x021AFF7C, ov021), `int SCRCMD_WarpToRailLocation(int a0, int a1)`
(`ov021_func_021AFF7C.c:44`). The operands are read at `:58-62`.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `zone_id` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFF7C.c:58` | The destination zone |
| `rail_a` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFF7C.c:59` | The first rail coordinate |
| `rail_b` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFF7C.c:60` | The second rail coordinate |
| `rail_c` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFF7C.c:61` | The third rail coordinate |
| `transition` | u16 | `VM_GetU16(a0)`, `ov021_func_021AFF7C.c:62` | The transition id |
| `rail_loc` | `Q` (u16 f0, u16 f2, s16 f4, u16 f6) | `FIELD_NOGRID_MAPPER_CalcZoneRailLocation(FIELDMAP_GetNogridMapper(a), b, c, d, e, loc, 0x15)`, `ov021_func_021AFF7C.c:64` | The rail cell the warp targets |
| `warp_event` | handle | `EVENT_MAPCHANGE_CreateRailWarpEvent(h, a, b, loc, f, 0)`, `ov021_func_021AFF7C.c:65` | The rail warp event |

## 7. Ledges and hops

### `field.ledge.check`

`FIELD_EVENT_CHECK_CheckLedgeJump` (0x0218B408, ov021), `int FIELD_EVENT_CHECK_CheckLedgeJump(char *p, void *a, void *x)`
(`ov021_func_0218B408.c:46`). `p` is the field input, `a` is the game system,
`x` is the field map. This is the block candidate for a ledge jump.

| Field | Type | Where it comes from | Why |
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

### `field.ledge.task`

`FIELD_EVENT_CHECK_CreateLedgeJumpTask` (0x0218B3DC, ov021), `int FIELD_EVENT_CHECK_CreateLedgeJumpTask(int unused, int a, int b, int c, int d)`
(`ov021_func_0218B3DC.c:33`). `a` is the game system, `b` is the field map, `c`
is the direction, `d` is the long-hop flag.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `task` | handle | `func_020122C0(a, 0, main, 0x10)`, `ov021_func_0218B3DC.c:35` | The ledge jump task the game created |
| `system` | handle | `e->f8 = a`, `ov021_func_0218B3DC.c:37` | The system the task runs under |
| `field_map` | handle | `e->fC = b`, `ov021_func_0218B3DC.c:38` | The field map the task reads |
| `dir` | u16 | `e->f4 = c`, `ov021_func_0218B3DC.c:39` | The jump direction |
| `long_hop` | u16 | `e->f6 = d`, `ov021_func_0218B3DC.c:40` | Whether the jump is a long hop |

### `field.ledge.main`

`FIELD_EVENT_CHECK_LedgeJumpTaskMain` (0x0218B32C, ov021), `int FIELD_EVENT_CHECK_LedgeJumpTaskMain(void *unused, int *st, char *ctx)`
(`ov021_func_0218B32C.c:55`). `ctx` is the 0x10-byte task record.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `step` | u32 | `*st`, `ov021_func_0218B32C.c:63` | Which step of the jump runs |
| `field_map` | handle | `ctx->fC`, `ov021_func_0218B32C.c:57` | The field map the task reads |
| `player` | handle | `FIELDMAP_GetFieldPlayer(ctx->fC)`, `ov021_func_0218B32C.c:57` | The player who jumps |
| `form_code` | u32 | `FIELD_PLAYER_GetFormCode(a)`, `ov021_func_0218B32C.c:61` | The player form |
| `mmdl` | handle | `FIELD_PLAYER_GetMmdl(a)`, `ov021_func_0218B32C.c:62` | The player's field model |
| `dir` | u16 | `ctx->f4`, `ov021_func_0218B32C.c:66`, `:73` | The jump direction |
| `long_hop` | u16 | `ctx->f6`, `ov021_func_0218B32C.c:74` | Whether the jump is a long hop |
| `ride_effect` | u32 | `FIELD_PLAYER_GetRideEffect(a)`, `ov021_func_0218B32C.c:70` | The ride effect the follow mode resets |

### `field.ledge.path`

`FIELD_EVENT_CHECK_IsLedgePathClear` (0x0218B6F8, ov021), a state entry,
`int FIELD_EVENT_CHECK_IsLedgePathClear(char *p, void *q)` (`ov021_func_0218B6F8.c:37`).
`p` is the field input, `q` is the field map.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `mmdl` | handle | `FIELD_PLAYER_GetMmdl(p->f18)`, `ov021_func_0218B6F8.c:39` | The player model the grid reads |
| `dir` | u16 | `p->f30`, `ov021_func_0218B6F8.c:40` | The direction the path test walks |
| `start_x` | s16 | `FLDMMDL_GetGridNowX(o)`, `ov021_func_0218B6F8.c:41` | The grid x the walk starts at |
| `start_z` | s16 | `FLDMMDL_GetGridNowZ(o)`, `ov021_func_0218B6F8.c:42` | The grid z the walk starts at |
| `cells_free` | bool | The return, 1 when both cells are free, `ov021_func_0218B6F8.c:51` | Whether the landing cells are clear |

### `field.ledge.foot`

`FIELD_PLAYER_GRID_StartLedgeJumpOnFoot` (0x021A4ED8, ov021), `void FIELD_PLAYER_GRID_StartLedgeJumpOnFoot(u32 *p, int b, u8 *c)`
(`ov021_func_021A4ED8.c:32`). `p` is the player grid state, `b` is the model,
`c` is the step record.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `dir` | u16 | `((Obj19792 *)c)->f8`, `ov021_func_021A4ED8.c:34` | The jump direction |
| `acmd` | int | `FLDMMDL_ACMD_ConvertCodeByDir(dir, 0x38)`, `ov021_func_021A4ED8.c:34` | The animation command the jump sets |
| `step_kind` | u32 | The literal `1` set by `FIELD_PLAYER_CORE_SetStepKind`, `ov021_func_021A4ED8.c:36` | The step kind the jump records |
| `sound` | u32 | The literal `0x55E` played, `ov021_func_021A4ED8.c:37` | The jump sound |
| `owner` | handle | The player state `p`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the jump to the local player |

### `field.ledge.cycle`

`FIELD_PLAYER_GRID_StartLedgeJumpOnCycle` (0x021A511C, ov021), `void FIELD_PLAYER_GRID_StartLedgeJumpOnCycle(u32 *p, int b, u8 *c)`
(`ov021_func_021A511C.c:32`). The same shape as `field.ledge.foot`.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `dir` | u16 | `((Obj19799 *)c)->f8`, `ov021_func_021A511C.c:34` | The jump direction on the bicycle |
| `acmd` | int | `FLDMMDL_ACMD_ConvertCodeByDir(dir, 0x38)`, `ov021_func_021A511C.c:34` | The animation command |
| `step_kind` | u32 | The literal `1`, `ov021_func_021A511C.c:36` | The step kind |
| `sound` | u32 | The literal `0x55E`, `ov021_func_021A511C.c:37` | The jump sound |

### `field.ledge.nogrid.walk`

`FIELD_PLAYER_NOGRID_StartWalkLedgeJump` (0x021A5F74, ov021), `void FIELD_PLAYER_NOGRID_StartWalkLedgeJump(S *a, void *b, int c, int d, u16 e)`
(`ov021_func_021A5F74.c:32`). `a` is the player state, `b` is the model, `e` is
the direction.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `dir` | u16 | Argument `e`, `ov021_func_021A5F74.c:34` | The jump direction |
| `acmd` | int | `FLDMMDL_ACMD_ConvertCodeByDir(e, 0x38)`, `ov021_func_021A5F74.c:34` | The animation command |
| `step_kind` | u32 | The literal `1`, `ov021_func_021A5F74.c:36` | The step kind |
| `player` | handle | `a->f14` to `FIELD_PLAYER_CORE_SetStepKind`, `ov021_func_021A5F74.c:36` | The player the step kind is set on |

### `field.ledge.nogrid.slow`

`FIELD_PLAYER_NOGRID_StartSlowLedgeJump` (0x021A6338, ov021), `void FIELD_PLAYER_NOGRID_StartSlowLedgeJump(S *a, void *b, int c, int d, u16 e)`
(`ov021_func_021A6338.c:32`). The same shape as `field.ledge.nogrid.walk`.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `dir` | u16 | Argument `e`, `ov021_func_021A6338.c:34` | The slow jump direction |
| `acmd` | int | `FLDMMDL_ACMD_ConvertCodeByDir(e, 0x38)`, `ov021_func_021A6338.c:34` | The animation command |
| `step_kind` | u32 | The literal `1`, `ov021_func_021A6338.c:36` | The step kind |
| `player` | handle | `a->f14`, `ov021_func_021A6338.c:36` | The player the step kind is set on |

### `field.hop.jump`

`FIELD_PLAYER_GRID_EVENT_CreateJumpTask` (0x021C1228, ov021), `void FIELD_PLAYER_GRID_EVENT_CreateJumpTask(u32 a, u16 b, u32 c, u32 d)`
(`ov021_func_021C1228.c:48`). `a` is the player, `b` is the direction, `c` is
the map attribute, `d` is the heap tag.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `dir` | u16 | Argument `b`, `ov021_func_021C1228.c:57` | The jump direction |
| `player` | handle | Argument `a`, `ov021_func_021C1228.c:58` | The player who jumps |
| `field_map` | handle | `FIELD_PLAYER_GetFieldMap(a)`, `ov021_func_021C1228.c:59` | The field map the task reads |
| `pos` | `Vec3` | `FLDMMDL_GetPos(r7, &va)`, `ov021_func_021C1228.c:61` | The player's position before the jump |
| `target_pos` | `Vec3` | `vb` after `FLDMMDL_MOVE_AddDirVector(dir, &vb, 0x20000)`, `ov021_func_021C1228.c:63` | The jump target |
| `map_height` | int | `FLDMMDL_MOVE_GetMapHeight(r7, &vb, &out)`, `ov021_func_021C1228.c:64` | The height at the target |
| `climb_up` | bool | `out > va.y`, `ov021_func_021C1228.c:65` | Whether the jump goes up |
| `grid` | handle | `FIELD_PLAYER_GetGrid(a)`, `ov021_func_021C1228.c:67` | The player's grid |

### `field.hop`

`FIELD_PLAYER_GRID_EVENT_CreateHopTask` (0x021C1024, ov021), `void FIELD_PLAYER_GRID_EVENT_CreateHopTask(u32 a, u16 b, u32 c, u32 d)`
(`ov021_func_021C1024.c:40`). `a` is the player, `b` is the direction, `c` is
the map attribute, `d` is the heap tag.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `dir` | u16 | Argument `b`, `ov021_func_021C1024.c:46` | The hop direction |
| `player` | handle | Argument `a`, `ov021_func_021C1024.c:47` | The player who hops |
| `field_map` | handle | `FIELD_PLAYER_GetFieldMap(a)`, `ov021_func_021C1024.c:45` | The field map the task reads |
| `grid` | handle | `FIELD_PLAYER_GetGrid(a)`, `ov021_func_021C1024.c:48` | The player's grid |
| `long_hop` | bool | `MAP_ATTR_IsLongHopEdge(MAP_ATTR_GetTerrainId(c))`, `ov021_func_021C1024.c:49` | Whether the hop is a long hop |

## 8. Field skills and field moves

### `field.skill.handler`

`FIELD_SKILL_CHECK_CallHandler` (0x021644C4, ov010), `int FIELD_SKILL_CHECK_CallHandler(int i, void *a, void *b)`
(`ov010_func_021644C4.c:30`). `i` is the handler index, `a` is the game system,
`b` is a context. The block candidate for a field skill.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `handler_index` | int | Argument `i`, `ov010_func_021644C4.c:38` | Names the field skill being dispatched |
| `handler` | ptr | `d_021714DC[i]`, `ov010_func_021644C4.c:38` | The handler function the skill uses |
| `slot_empty` | bool | `fn == 0`, `ov010_func_021644C4.c:39` | Whether the skill has no handler |
| `result` | int | The handler's return, `ov010_func_021644C4.c:41` | The skill's outcome |
| `game_data` | handle | `GAMESYSTEM_GetGameData(a)`, `ov010_func_021644C4.c:36` | The game data the handler receives |
| `player_work` | handle | `GAME_DATA_GetMyPlayerWork(sys)`, `ov010_func_021644C4.c:37` | The player work the handler receives |
| `owner` | handle | The game data `a`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the skill to the local player |

### `field.skill.flags`

`FIELD_SKILL_CHECK_FillUseFlags` (0x02164480, ov010), `void FIELD_SKILL_CHECK_FillUseFlags(Flags *f, void *a, void *b)`
(`ov010_func_02164480.c:41`). `f` is the flags struct, `a` and `b` are context.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `skill_bits` | u32 | Bits 0..9 set by `func_02028748(f, i, FIELD_SKILL_CHECK_CallHandler(i, a, b))`, `ov010_func_02164480.c:49` | Which of the ten field skills are usable |
| `player_object_code` | u32 | `GAME_DATA_GetPlayerObjectCode(save)`, `ov010_func_02164480.c:50` | The player object code, stored at flag word +0x04 |
| `save` | handle | `GAME_DATA_GetField114(ICA_ANIME_GetField1C(a))`, `ov010_func_02164480.c:46` | The save block the object code reads |
| `owner` | handle | The context `a`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the flags to the local player |

### `field.move.cut`

`MOVE_POKEMON_CreateCutEvent` (0x0215EA6C, ov010), `void *MOVE_POKEMON_CreateCutEvent(void *a, void *b)`
(`ov010_func_0215EA6C.c:25`). `a` is the skill pair, `b` is the context.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `task` | handle | `func_020122C0(b->f8, 0, main, 20)`, `ov010_func_0215EA6C.c:27` | The Cut field effect task |
| `owner` | handle | `((Obj02057 *)b)->f8`, `ov010_func_0215EA6C.c:27` | The task owner, the local player |
| `skill_pair` | `Pair` (u32 a, u32 b) | Argument `a` to `MOVE_POKEMON_InitSkillWork`, `ov010_func_0215EA6C.c:28` | The skill parameters the work stores |
| `skill_ctx` | handle | Argument `b`; the work stores `ctx[2]` and `ctx[3]`, `ov010_func_0215F0E8.c:15` | The skill context, the save-side state |

### `field.move.flash`

`MOVE_POKEMON_CreateFlashEvent` (0x0215EC94, ov010), `void *MOVE_POKEMON_CreateFlashEvent(void *a, void *b)`
(`ov010_func_0215EC94.c:27`).

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `task` | handle | `func_020122C0(b->f8, 0, main, 20)`, `ov010_func_0215EC94.c:30` | The Flash field effect task |
| `owner` | handle | `((Obj02057 *)b)->f8`, `ov010_func_0215EC94.c:30` | The task owner, the local player |
| `skill_pair` | `Pair` | Argument `a`, `ov010_func_0215EC94.c:31` | The skill parameters |
| `skill_ctx` | handle | Argument `b`, `ov010_func_0215EC94.c:31` | The skill context |

### `field.move.strength`

`MOVE_POKEMON_CreateStrengthEvent` (0x0215EBD0, ov010), `void *MOVE_POKEMON_CreateStrengthEvent(void *a, void *b)`
(`ov010_func_0215EBD0.c:26`).

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `task` | handle | `func_020122C0(b->f8, 0, main, 20)`, `ov010_func_0215EBD0.c:29` | The Strength field effect task |
| `owner` | handle | `((Obj02057 *)b)->f8`, `ov010_func_0215EBD0.c:29` | The task owner, the local player |
| `skill_pair` | `Pair` | Argument `a`, `ov010_func_0215EBD0.c:30` | The skill parameters |
| `skill_ctx` | handle | Argument `b`, `ov010_func_0215EBD0.c:30` | The skill context |

### `field.move.dive`

`MOVE_POKEMON_CreateDiveEvent` (0x0215EC40, ov010), `void *MOVE_POKEMON_CreateDiveEvent(u16 *a, void *b)`
(`ov010_func_0215EC40.c:31`). It tail-calls
`SWEET_SCENT_CreateEvent(b->f8, b->f10, *a)` (`ov010_func_0215EC40.c:33`).

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `owner` | handle | `((Obj12315 *)b)->f8`, `ov010_func_0215EC40.c:33` | The task owner, the local player |
| `effect_ctx` | handle | `((Obj12315 *)b)->f10`, `ov010_func_0215EC40.c:33` | The effect context the create receives |
| `mode` | u8 | `*a`, the low byte of the first argument, `ov010_func_0215EC40.c:33` | The Dive mode byte |

### `field.move.cut.main`

`MOVE_POKEMON_CutEventMain` (0x0215EA94, ov010), `int MOVE_POKEMON_CutEventMain(void *a, void *b, void *c)`
(`ov010_func_0215EA94.c:25`). `a` is the VM, `c` is the work.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `script_id` | u32 | The literal `0x2715` to `SCRIPT_SYS_ChangeScriptEvent(a, 0x2715, 0, 0)`, `ov010_func_0215EA94.c:31` | The Cut script the event runs |
| `param` | int | `((Obj12311 *)c)->fC`, `ov010_func_0215EA94.c:30` | The parameter the script receives |
| `script_event` | handle | `SCRIPT_SYS_ChangeScriptEvent(a, 0x2715, 0, 0)`, `ov010_func_0215EA94.c:31` | The script event the cut changes to |
| `event_work` | handle | `GAME_DATA_GetEventWork(GAMESYSTEM_GetGameData(c->f8))`, `ov010_func_0215EA94.c:29` | The event work the cut reads |

### `field.move.strength.main`

`MOVE_POKEMON_StrengthEventMain` (0x0215EBF8, ov010), `int MOVE_POKEMON_StrengthEventMain(void *a, int *state, void *b)`
(`ov010_func_0215EBF8.c:28`). `a` is the VM, `b` is the work.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `script_id` | u32 | The literal `0x2711` to `SCRIPT_SYS_ChangeScriptEvent(a, 0x2711, 0, 0)`, `ov010_func_0215EBF8.c:34` | The Strength script the event runs |
| `param` | int | `((Obj12314 *)b)->fC`, `ov010_func_0215EBF8.c:33` | The parameter the script receives |
| `script_event` | handle | `SCRIPT_SYS_ChangeScriptEvent(a, 0x2711, 0, 0)`, `ov010_func_0215EBF8.c:34` | The script event the strength changes to |
| `event_work` | handle | `GAME_DATA_GetEventWork(GAMESYSTEM_GetGameData(b->f8))`, `ov010_func_0215EBF8.c:32` | The event work the strength reads |

### `field.move.flash.main`

`MOVE_POKEMON_FlashEventMain` (0x0215ECBC, ov010), `int MOVE_POKEMON_FlashEventMain(void *a0, int *state, Arg *arg)`
(`ov010_func_0215ECBC.c:66`). `a0` is the VM, `arg` is the work.

| Field | Type | Where it comes from | Why |
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

### `field.move.cut.check`

`MOVE_POKEMON_CheckCut` (0x0215EA58, ov010), a state entry,
`int MOVE_POKEMON_CheckCut(int a0, int a1)` (`func_0215EA58.c:22`).

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `skill_index` | int | The literal `0` set at `func_0215EA58.c:26` | The skill index Cut tests |
| `context` | handle | Argument `a0`, `func_0215EA58.c:28` | The skill context the test reads |
| `usable` | bool | `MOVE_POKEMON_IsSkillUsable(a0, 0)`, `func_0215EA58.c:28` | Whether Cut is usable |

### `field.move.flash.check`

`MOVE_POKEMON_CheckFlash` (0x0215EC80, ov010), a state entry,
`int MOVE_POKEMON_CheckFlash(void *p)` (`ov010_func_0215EC80.c:15`).

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `skill_index` | int | The literal `5` to `MOVE_POKEMON_IsSkillUsable`, `ov010_func_0215EC80.c:17` | The skill index Flash tests |
| `context` | handle | Argument `p`, `ov010_func_0215EC80.c:17` | The skill context the test reads |
| `usable` | bool | `MOVE_POKEMON_IsSkillUsable(p, 5) != 0`, `ov010_func_0215EC80.c:17` | Whether Flash is usable |

### `field.move.strength.check`

`MOVE_POKEMON_CheckStrength` (0x0215EBBC, ov010), a state entry,
`int MOVE_POKEMON_CheckStrength(void *p)` (`ov010_func_0215EBBC.c:15`).

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `skill_index` | int | The literal `3`, `ov010_func_0215EBBC.c:17` | The skill index Strength tests |
| `context` | handle | Argument `p`, `ov010_func_0215EBBC.c:17` | The skill context the test reads |
| `usable` | bool | `MOVE_POKEMON_IsSkillUsable(p, 3) != 0`, `ov010_func_0215EBBC.c:17` | Whether Strength is usable |

### `field.move.waterfall.check`

`MOVE_POKEMON_CheckWaterfall` (0x0215EC6C, ov010), a state entry,
`int MOVE_POKEMON_CheckWaterfall(void *p)` (`ov010_func_0215EC6C.c:15`).

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `skill_index` | int | The literal `4`, `ov010_func_0215EC6C.c:17` | The skill index Waterfall tests |
| `context` | handle | Argument `p`, `ov010_func_0215EC6C.c:17` | The skill context the test reads |
| `usable` | bool | `MOVE_POKEMON_IsSkillUsable(p, 4) != 0`, `ov010_func_0215EC6C.c:17` | Whether Waterfall is usable |

### `field.move.iaigiri`

`SCRCMD_IAIGIRI_StartOnPlayer` (0x021C4678, ov021), `int SCRCMD_IAIGIRI_StartOnPlayer(void *a, void *b)`
(`ov021_func_021C4678.c:35`). `b` is the script command context.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `field_map` | handle | `GAMESYSTEM_GetFieldMap(SCRCMD_WORK_GetGameSystem(b))`, `ov021_func_021C4678.c:37` | The field map the effect reads |
| `mmdl` | handle | `FIELD_PLAYER_GetMmdl(FIELDMAP_GetFieldPlayer(g))`, `ov021_func_021C4678.c:38` | The player model the effect plays on |
| `effect` | handle | `FIELDMAP_GetEffect(g)`, `ov021_func_021C4678.c:40` | The field effect the iaigiri starts on |
| `cause` | enum | The script command itself, opcode of this handler | Tells a mod a script started the cut effect |

## 9. The field bag and overlay procedure

### `field.bag.open`

`SCRCMD_PROC_CallBagScreen` (0x0215C72C, ov010), `int SCRCMD_PROC_CallBagScreen(void *c, void *vm)`
(`ov010_func_0215C72C.c:55`). `c` is the VM stream, `vm` is the script command
context.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `selector` | u32 | `SCRIPT_WORK_ReadWorkValue(c, vm)`, `ov010_func_0215C72C.c:66` | The bag mode the script asked for |
| `pocket` | u32 | The mapping of `selector`, `0 -> 4`, `1 -> 5`, else `0`, `ov010_func_0215C72C.c:69-74` | The pocket the bag opens on |
| `out_a` | ptr | `SCRIPT_WORK_ReadWorkPointer(c, vm)`, `ov010_func_0215C72C.c:67` | The first result slot the bag fills |
| `out_b` | ptr | `SCRIPT_WORK_ReadWorkPointer(c, vm)`, `ov010_func_0215C72C.c:68` | The second result slot the bag fills |
| `bag_param` | handle | `BAG_PARAM_Create(game_data, 0, k, 4)`, `ov010_func_0215C72C.c:75` | The bag parameter the overlay receives |
| `overlay_id` | u32 | `ov070_id`, `ov010_func_0215C72C.c:79` | The overlay, 0x46, the bag opens |
| `handoff` | handle | `h[0] = a`, `h[1] = b`, `ov010_func_0215C72C.c:76-78` | The two result pointers the close callback writes |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(vm)`, `ov010_func_0215C72C.c:75` | The game data the bag reads |

### `field.bag.closed`

`SCRCMD_PROC_OnBagClosed` (0x0215C700, ov010), `void SCRCMD_PROC_OnBagClosed(Pair *p)`
(`ov010_func_0215C700.c:22`). `p->context` is the bag context, `p->outputs` is
the handoff record.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `changed` | bool | `c->first`, written to `*o->first`, `ov010_func_0215C700.c:26-29` | Whether the bag changed |
| `result` | u32 | `c->second`, written to `*o->second`, `ov010_func_0215C700.c:30` | The bag result the script reads |
| `context` | handle | `p->context`, `ov010_func_0215C700.c:24` | The bag context the callback reads |
| `outputs` | handle | `p->outputs`, `ov010_func_0215C700.c:25` | The two result slots the callback fills |

### `field.overlayproc.start`

`SCRCMD_PROC_StartOverlayProc` (0x0215C63C, ov010), `void SCRCMD_PROC_StartOverlayProc(void *c, void *vm, u32 ovId, const void *proc, void *work, u32 arg0, u32 arg1)`
(`ov010_func_0215C63C.c:48`).

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `overlay_id` | u32 | Argument `ovId`, `ov010_func_0215C63C.c:48` | The overlay the procedure runs in |
| `proc` | ptr | Argument `proc`, `ov010_func_0215C63C.c:48` | The procedure entry the game calls |
| `work` | handle | Argument `work`, `ov010_func_0215C63C.c:48` | The work the procedure reads |
| `arg0` | u32 | Argument `arg0`, `ov010_func_0215C63C.c:48` | The first hand-off word |
| `arg1` | u32 | Argument `arg1`, `ov010_func_0215C63C.c:48` | The second hand-off word |
| `system` | handle | `SCRCMD_WORK_GetGameSystem(vm)`, `ov010_func_0215C63C.c:54` | The system the procedure runs under |
| `handoff` | handle | `h`, fields `work`, `arg1`, `arg0`, `ov010_func_0215C63C.c:56-59` | The 12-byte record the wait reads |
| `cause` | enum | The script command itself, opcode of this handler | Tells a mod a script started the procedure |

### `field.overlayproc.wait`

`SCRCMD_PROC_WaitOverlayProc` (0x0215C69C, ov010), `int SCRCMD_PROC_WaitOverlayProc(int a0, int a1)`
(`func_0215C69C.c:34`). `a1` is the script command context.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `proc_status` | u32 | `GAMESYSTEM_GetProcStatus(t6)`, `func_0215C69C.c:51` | Whether the overlay procedure is still running |
| `system` | handle | `SCRCMD_WORK_GetGameSystem(a0)`, `func_0215C69C.c:42` | The system the status reads |
| `handoff` | handle | `SCRIPT_WORK_GetUserWork(t4)`, `func_0215C69C.c:48` | The hand-off record the wait reads |
| `callback` | ptr | `((Obj00005 *)t5)->f8`, `func_0215C69C.c:58` | The close callback the wait runs |
| `work` | handle | `((Obj00005 *)t5)->f0`, `func_0215C69C.c:66` | The work the wait frees |

## 10. Season, random, save record and special location

### `field.random`

`SCRCMD_WORK_CmdGetRandom` (0x0215AABC, ov010), a state entry,
`int SCRCMD_WORK_CmdGetRandom(void *a0, void *a1)` (`func_0215AABC.c:29`).

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `range` | u32 | `SCRIPT_WORK_ReadWorkValue(a0, a1)`, `func_0215AABC.c:33` | The range the roll is bounded by |
| `value` | u16 | `func_020056EC(range)`, `func_0215AABC.c:33` | The random value, the subject |
| `out_slot` | ptr | `SCRIPT_WORK_ReadWorkPointer(a0, a1)`, `func_0215AABC.c:31` | The work slot the value lands in |

### `field.save.record`

`SCRCMD_WORK_CmdGetSaveRecord` (0x0215AAF4, ov010), a state entry,
`int SCRCMD_WORK_CmdGetSaveRecord(int a0, int a1)` (`func_0215AAF4.c:26`).

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `stars` | int | `TRCARD_SYS_CountStars(v0)`, `func_0215AAF4.c:41` | The save record the script reads, the trainer card star count |
| `out_slot` | ptr | `SCRIPT_WORK_ReadWorkPointer(a0, a1)`, `func_0215AAF4.c:39` | The work slot the count lands in |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(a1)`, `func_0215AAF4.c:35` | The game data the count reads |
| `owner` | handle | The game data `v0`; `GAME_DATA_GetMyStatus`, `ov021_func_021D2B54.c:102` | Attributes the record to the local player |

### `field.season`

`SCRCMD_WORK_CmdGetSeason` (0x0215ABD0, ov010), a state entry,
`int SCRCMD_WORK_CmdGetSeason(int a0, int a1)` (`func_0215ABD0.c:27`).

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `season` | u8 | `GAME_DATA_GetSeason(v0)`, `func_0215ABD0.c:39` | The current season, the subject |
| `out_slot` | ptr | `SCRIPT_WORK_ReadWorkPointer(a0, a1)`, `func_0215ABD0.c:34` | The work slot the season lands in |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(a1)`, `func_0215ABD0.c:37` | The game data the season reads |

### `field.special.location`

`SCRCMD_WORK_CmdSetSpecialLocation` (0x0215ADCC, ov010), `int SCRCMD_WORK_CmdSetSpecialLocation(int a0, int a1)`
(`ov010_func_0215ADCC.c:32`). `a0` is the VM stream, `a1` is the script command
context. The five operands are read at `:36-40`.

| Field | Type | Where it comes from | Why |
|---|---|---|---|
| `zone_id` | int | `SCRIPT_WORK_ReadWorkValue(a0, a1)`, `ov010_func_0215ADCC.c:36` | The zone of the special location |
| `x` | s16 | `SCRIPT_WORK_ReadWorkValue(a0, a1)`, `ov010_func_0215ADCC.c:37` | The x of the special location |
| `y` | int | `SCRIPT_WORK_ReadWorkValue(a0, a1)`, `ov010_func_0215ADCC.c:38` | The y of the special location, shifted left 16 |
| `z` | int | `SCRIPT_WORK_ReadWorkValue(a0, a1)`, `ov010_func_0215ADCC.c:39` | The z of the special location, shifted left 16 |
| `w` | int | `SCRIPT_WORK_ReadWorkValue(a0, a1)`, `ov010_func_0215ADCC.c:40` | The w of the special location, shifted left 16 |
| `location` | int[7] | `ZONEDATA_SetDirectLocation(buf, c, r6, r7 << 16, v10 << 16, v0 << 16)`, `ov010_func_0215ADCC.c:41` | The location record the game stores |
| `game_data` | handle | `SCRCMD_WORK_GetGameData(a1)`, `ov010_func_0215ADCC.c:35` | The game data the location is set on |
| `cause` | enum | The script command itself, opcode of this handler | Tells a mod a script set the location |

## 11. Fields a mod would want that cannot be obtained

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

## 12. The thinnest payloads

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
