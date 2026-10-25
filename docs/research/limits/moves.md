# Limits survey: moves

Paths below are relative to aletheia `src/game/` unless stated. Claims are
marked [V] verified in code or data, [I] inferred, [U] not verified.

## 1. Summary

Original count: 559 moves, ids 1..559 (Fusion Bolt is 559, [V]
ov093/hand_waza_types.h:1301-1302). Record 0 is the "no move" slot. The move
table has 560 members (0..559), all 36 bytes [V] (data read, NARC `a/0/2/1`).

Difficulty: medium. Most of a move is data (one 36-byte record, one name, one
description, one effect script). Only the checks listed in section 2 and four
fixed-size buffers block ids past 559. Moves that need new mechanics need a
battle handler, and the handler registry is a fixed 258-row table in
overlay data.

Biggest obstacles:

1. The battle effect player maps an id below 0x231 to the move-script archive
   and an id of 0x231 or more to a separate generic-effect archive, so a move
   id past 560 would play the wrong script (ov094/btlv_effvm.c:882-899) [V].
2. The handler registry (`gWazaHandlerRegistry`) has a compile-time row count
   of 0x102 and lives in copied overlay data (ov093/hand_common_types.h:270,
   ov093/hand_waza.c:1142, 1404-1412) [V]. A mod cannot add a row without a
   replacement of `HAND_WAZA_AddMoveHandler`.
3. Eight hard-coded `< 0x230` / `560` checks reject new ids in Mystery Gift,
   the beacon and the Metronome picker (table below) [V].
4. Level-up learnset buffers hold 26 entries (25 moves plus terminator), and
   the original maximum is 23 entries incl. terminator [V]. A longer mod
   learnset overflows a heap block.
5. TM/HM data is fixed: two item id windows, a 101-entry move table in ARM9
   rodata, a 109-slot bag pocket, and 128 compatibility bits per species
   record [V].

Ids are stored in 16 bits everywhere I checked (move slots, action words,
hit records, move records, learnsets), so the id width is not a limit [V].

## 2. Site table

| # | file:line | function | literal or size | guards | class | change needed |
|---|---|---|---|---|---|---|
| 1 | ov093/btl_calc.c:588, 605 | `BTL_CALC_PickRandomMoveExcluding` | `BTL_MOVE_ID_LIMIT 0x230`, loop `moveId = 1; < 0x230` | Metronome candidate list: every id below the limit that is not excluded | id check | Replace with the live move count (see section 3). A mod move would then be a Metronome candidate unless added to the exclusion list. |
| 2 | ov093/btl_calc.c:276 | `BTL_CALC_Init` | `GFL_HEAP_Alloc(heapId, 0x460, ...)` = 560 u16 | work table the Metronome loop fills with `count++` | array size | Size it as `2 * (move count)`. Only user is line 607-609 [V]. Without this, raising only #1 overflows the heap block. |
| 3 | ov010/mystery_gift_mon.c:268 | Mystery Gift Pokemon builder | `move != 0 && move < 0x230` | a gift's four moves; others are silently dropped | id check | Use the live move count. |
| 4 | ov010/game_beacon_accessor.c:958-961 | `GAME_BEACON_ACCESSOR_IsActionMoveNoInvalid` | `value == 0 \|\| value >= 0x230` (comment: "1..0x22F") | beacon packet move number at +0x34; returns 1 (invalid) | id check | Use the live move count. |
| 5 | ov121/mystery_album.c:2280 | Mystery Gift album builder | `m != 0 && m < 0x230` | gift moves | id check | Same. |
| 6 | ov121/mystery_net.c:917 | Mystery Gift net builder | `m != 0 && m < 0x230` | gift moves | id check | Same. |
| 7 | ov121/mystery.c:1814 | Mystery Gift builder | `value != 0 && value < 560` | gift moves (decimal form, missed by a hex search) | id check | Same. |
| 8 | ov093/hand_waza.c:1142, 1404-1412 | `HAND_WAZA_AddMoveHandler` | `WAZA_HANDLER_REGISTRY_COUNT 0x102` (ov093/hand_common_types.h:270) | linear scan of the registry for the chosen move id | table size | See section 2a. |
| 9 | ov093/hand_common_types.h:272, hand_waza.c:1446 | `HAND_WAZA_CanAddMoveHandler` | `WAZA_HANDLER_FACTOR_LIMIT 8` | at most 9 move-handler factors scanned per Pokemon; a longer list refuses a new handler | table size | Probably leave. Mods that add handlers raise the chance of hitting it [I]. |
| 10 | ov094/btlv_effvm.c:882, 889, 899, 2949, 3001-3004 | `BTLV_EFFVM_StartScript`, jump-to-archive command | `effectId < 0x231` selects archive 0x42 member `id`; else archive 0x43 member `id - 0x231` | which battle effect script a move plays | asset index arithmetic | See section 3. The move id is passed as the script id (btlv_scu.c:2062, ov094/btlv_effect.c:905, `BtlvScuWazaRequest.wazaId` aliases `BtlvEffScriptRequest.scriptId`, both u16 at +0) [V]. |
| 11 | ov094/btlv_effvm.c:882, 2993 etc. | same | non-move effect ids start at 0x231 and are used by name (0x23F..0x245, 0x256, 0x27e, 0x291 and others) | the generic-effect id space starts directly after move 560 | id collision | Remap: send mod move scripts through a third range, or move the boundary (ids are literal, so this needs a code change in the PC build). |
| 12 | arm9/waza_tool.c:178, 188, 193 (and the same pattern in every reader) | `WAZA_TOOL_GetParam`, `GetFlag`, `GetSickCont`, `GetRankEffect*`, `IsAlways*` | `ARC_TOOL_ReadMember(buf, 0x15, move)` | move record load by id, no bound check | asset index arithmetic | None. Member 0x15 = `a/0/2/1`; id n reads member n. Adding members past the end is enough. |
| 13 | arm9/waza_tool.c:651-662, ov096/tr_ai.c:339 | `WAZA_TOOL_CreateCache` | 48 records (0x30), u16 ids | record cache; full cache reuses the last slot | array size | None, it is a cache, not an id range [V]. |
| 14 | arm9/waza_tool.c (case labels of `WAZA_TOOL_GetDataParam`) | field getter | fixed field set 0..31 | which record fields exist | other | A mod cannot add new record fields; the 36-byte layout is fixed (see section 3). |
| 15 | ov010/waza_oshie.c:133-136 | `WAZA_OSHIE_CreateMoveList` (move relearner) | tbl 0x68 bytes, out 0x34 bytes, loop `j < 26` | level-up list, 26 entries | array size | Allocate by the real member size; loop to the terminator. |
| 16 | arm9/poke_tool.c:1715 | `POKE_TOOL_LearnLevelUpMove` | `GFL_HEAP_Alloc(..., 0x68, ...)` | learnset buffer on level-up | array size | Same: size by the member. `ARC_TOOL_ReadMember` copies the whole member, so a longer one overflows. |
| 17 | ov021/sodateya.c:573, 1266, 1330 | day-care level-up, egg-move and shared-move code | `LearnsetEntry learnset[26]` on the stack | learnset buffers for egg moves | array size | Same. |
| 18 | arm9/poke_tool.c:191, 1347 | `POKE_TOOL_SetBoxDefaultMoves` | shared global `gLevelUpMoveBuf` at DS 0x02146330 | default moves for a new Pokemon | array size | Capacity is about 127 entries (0x02146330 to the next object at 0x0214652C, from poke_personal.c header) [I]; fine for mods unless a learnset passes 127. |
| 19 | arm9/file_util.c:696-702 | `FILE_UTIL_IsMachineItem` | windows 0x148..0x1A9 and 0x26A..0x26C | which items are TM/HM | id check | A mod TM needs a new window or a table-driven test. |
| 20 | arm9/file_util.c:727-738, 212 | `FILE_UTIL_GetMachineMove` | table `g_0209EAA0[]`, 101 u16 | TM/HM number to move | table size | Replace by a table the mod can extend. |
| 21 | arm9/file_util.c:754-762 | `FILE_UTIL_IsHmMove` | six entries at table index 0x5C..0x61 | HM test used by the "forget move" rules | table size | Needs a list a mod can edit. |
| 22 | arm9/file_util.c:779-791, 797 | `FILE_UTIL_GetId`, `GetMachineIndex` | subtrahends 0x148, 0x145, 0x20E; index fits u8 | machine item to index 0..100 | id arithmetic | Same as #19. |
| 23 | arm9/myitem_savedata.c:45, 69, 202-204, 521-523 | bag | `BAG_TMHM_COUNT 0x6D` (109 slots, save field at offset 624) | TM/HM pocket size in the save | save layout | Pocket size is part of the save; mod TMs beyond 109 need mod storage. |
| 24 | ov021/sodateya.c:1486-1507 | `SODATEYA_CanLearnTm` | personal fields 0x27..0x2A, 4 x 32 bits | TM compatibility, machine index up to 127 | id storage width | 128 machines at most. Record is 60 bytes; no room to grow without changing the record. |
| 25 | arm9/poke_tool.c:2308-2320 | `POKE_TOOL_CheckSpeciesTutorCompat` | personal field 0x2B, 32 bits, `tutorIndex % 32` | tutor compatibility | id storage width | 32 tutor groups at most. |
| 26 | ov021/scrcmd_pokemon_fld.c:683-706 | `SCRCMD_POKEMON_FLD_GetTutorMoveId` | moves 0x152, 0x133, 0x134, 0x1b2, 0x206-0x208 | the three starter-group tutors (and default 0x133) | other | Hard-coded. Mod tutors should use script data, not this function [I]. |
| 27 | ov096/tr_ai.c:1997-2018 | `TR_AI_CmdSwitchOnMoveEffectSequence` | field 28 (u16) used as a jump index; script gives a threshold; `index / 2 > threshold` ends the script | trainer AI branch on move effect | other | None for ids. The new record's field 28 picks an AI branch; use an existing value or 0 [I]. |
| 28 | ov095/btl_id_sets.c:163-192 and the Mimic, Assist, Copycat, Sleep Talk, Call-move helpers | `BTL_ID_Is*Move` | small id lists with fixed counts (6, 10, 3, 5, 3 ...) | per-move exclusion rules | table size | Mod moves are in none of them, so they are allowed. Edit only if a mod move must be excluded. |
| 29 | ov093/btl_server_flow.c:3629-3630, 4242-4281, 10859; ov093/btl_client.c:2740, 2780, 4622; ov093/hand_item.c:3197; ov095/btlv_scd.c:872, 930, 985; ov093/btl_calc.c:722 | various | literals 150, 165 (0xA5), 174, 0xAE | special cases for Struggle, Curse and others | other | None. They name existing moves only [V]. |
| 30 | ov093/hand_waza.c (92 `HAND_WAZA_Get*Handlers` functions) | handler getters | one function per effect | per-move battle mechanics | other | See section 2a. |

Checked and not a limit: move slots are `u16 id` (ov093/btl_pokeparam_types.h:30-31) [V]; the battle action word has a 16-bit `moveId` field (ov093/btl_calc_types.h:147) [V]; hit records (`wazaId` u16, ov093/btl_records_types.h:14) and `BtlMoveParam.moveId` (btl_server_types.h:55) are u16 [V]; the turn move record has 120 entries and no per-id array [V] (btl_records_types.h:22). Egg moves use `ARC_TOOL_LoadMember` into a heap block sized by the member (ov021/sodateya.c:1304) [V].

### 2a. The move handler registry (how a mod supplies a handler)

How it works [V]:

- `gWazaHandlerRegistry` at DS 0x021F3030 is an array of 0x102 rows. Each row
  is `{u16 moveId; BtlEventHandler *(*getHandlers)(int *count)}` (8 bytes with
  padding; ov093/hand_common_types.h:275-278). I read the table from overlay
  data: 258 distinct move ids (max id 559, min 6), mapped to 215 distinct getter
  functions, followed by a zero row. The ids are not sorted.
- `HAND_WAZA_AddMoveHandler(bpp, moveId, subPriority)` (hand_waza.c:1404) scans
  rows linearly. On a hit it checks that the Pokemon does not hold the handler
  yet (`HAND_WAZA_CanAddMoveHandler`, :1434) and calls the getter, which
  returns a table of event handlers (event id, function). It then calls
  `BTL_EVENT_AddFactor(BTL_EVENT_TYPE_WAZA, moveId, 0, subPriority, ownerId,
  handlers, count)`. A move with no row returns 0 and nothing is added.
- The caller is btl_server_flow.c:2131 (move chosen), :3200 and :3216.
- `HAND_WAZA_RemoveMoveHandlerUnlessKept` (:1456), `...OfPoke` (:1475) and
  `...ById` (:1494) remove handlers by the same key.

Which moves need code: 258 of 559 ids have a registry row, so 301 moves are
pure data (a record, a name, a script) [V]. The 258 rows cover only 215
distinct getter functions because several ids share one getter. A move with no
row simply gets the default flow: damage by category, secondary effects from
the record (status, rank changes, drain, heal), then generic events [I from
btl_server_flow.c:3652-3700, 5008-5025 and the field getters].

Data-driven part: the record's `category` field (waza_tool.c getter case 1)
selects the main flow in `BTL_SERVER_FLOW_ProcMoveExecute` (switch at
btl_server_flow.c:3688-3697 on categories 0, 4, 6, 7, 8, 9) and the added
effect in `BTL_SERVER_FLOW_ProcMoveCategoryEffect` (:5008, categories 4, 6, 7).
Other categories used elsewhere: 8 (drain, :5190, :8108), 10, 11
(hand_common.c:177, hand_tokusei.c:4652), OHKO (hand_tokusei.c:3458, 3552).
The record also gives sick id, percent, duration, three rank changes, hit
count, crit rank, flinch percent, drain/heal ratio, target kind and a 32-bit
flag word (priority, contact, protect, etc.) [V waza_tool.c getter].

How a mod supplies a handler: the registry has no extension point, so the port
must either (a) wrap `HAND_WAZA_AddMoveHandler` in the PC build so it checks a
mod registry (a growable array of `{moveId, getHandlers}`) before or after the
built-in scan, or (b) rebuild `gWazaHandlerRegistry` at start-up into a larger
array and replace the count constant (`WAZA_HANDLER_REGISTRY_COUNT` is a macro
used for the loop bound and the extern declaration, so it needs a variable).
Option (a) touches one function. The mod's getter returns the same
`BtlEventHandler` table the built-in ones do; the handler functions use the
`BTL_EVENT_SIGNATURE` parameters and the `BTL_EVENT_*` / `BTL_SERVER_FLOW_*`
API (hand_waza.c:10640-10700 are small examples) [V]. Note `factor->work` is
the 0xX-word scratch each factor owns; handler writers must stay within it [U].

## 3. Data and assets

| What | Archive | Format | Notes |
|---|---|---|---|
| Move records | id 0x15 = `a/0/2/1` [V] (waza_tool.c:178) | 560 members x 36 bytes [V] | Fields: type, category, damage type, power, accuracy, base PP, priority, hit count (4+4 bits), sick id (u16), sick percent, sick duration type/min/max, crit rank, flinch percent, effect (u16, the AI-visible one), drain, heal, target, 3x rank type, 3x rank value, 3x rank percent, u32 flags (arm9/waza_tool.c:34-60). Two bytes at 0x1E have no reader. Adding members past 559 is enough for the data side [V mods.md:168]. |
| Battle effect scripts (moves) | id 0x42 = `a/0/6/6` [V] (btlv_effvm.c:890) | 601 members [V ASSETS.md] | Member index equals the effect id, which for a move is the move id. Members 560..600 exist; their use is not known [U]. |
| Battle effect scripts (generic) | id 0x43 = `a/0/6/7` [V] (btlv_effvm.c:899) | 98 members | Member = effect id - 0x231. |
| Move names | text bank 203 in `a/0/0/2` [V] (ov131/p_sta_skill.c:1403, ov170/box2_seq.c:8856) | one string per move id | Also reached via `WORDSET_SetSlotFromMsgCB` which uses bank 0xCB = 203 (arm9/wordset.c:473) [V]. 560 entries (ASSETS.md). `MSGDATA_ReadString` bounds the index by the bank's own count (arm9/msgdata.c:293) [V], so a longer bank works. |
| Move descriptions | bank 202 [V] (ov131/p_sta_skill.c:725, ov173/poke_summary_main.c:1010, ov198/b_plist_draw.c:1645) | one string per id | Same. |
| Battle messages for move-specific text | battle message banks, ids named `HAND_WAZA_STR_*` (ov093/hand_waza_types.h, 23 defines) | fixed string ids | A new mechanic needs new strings; the id must exist in the bank [I]. |
| Level-up learnsets | id 0x12 = `a/0/1/8` [V] (arm9/poke_personal.c, LoadDataDirect) | 668 members, member = form data id; `u16 move, u16 level` pairs, ends with `0xFFFF, 0xFFFF` [V] | Max real member is 92 bytes (23 entries incl. terminator) [V]. Buffers hold 26 entries (sites 15-17). |
| Egg moves | id 0x7B = `a/1/2/3` [V] (ov021/sodateya.c:1304) | 650 members: `u16 count`, then moves | Heap sized by member; no limit. |
| TM/HM move table | ARM9 rodata `g_0209EAA0`, 101 u16 [V] | fixed | Not an archive; must be replaced by code (sites 19-22). |
| TM/HM compatibility | personal record `a/0/1/6`, fields 0x27..0x2A | 4 x u32 in a 60-byte record | 128 bits max. |
| Tutor compatibility | personal field 0x2B | 32 bits | |
| Move animations | same as effect scripts (rows 2-3) | | Move-specific sound is inside the script (`BTLV_EFFVM_CmdPlaySe`), not a separate table [I]. |
| PP | derived from record base PP (arm9/waza_tool.c `WAZA_TOOL_GetMaxPP`: `basePP + basePP*20*ppUp/100`, ppUp clamped to 3) | | Computed, no per-move table. |
| Move relearner | level-up list of the Pokemon (ov010/waza_oshie.c) | | No separate table. |

Is adding members past the end enough? For the record, yes. For effect
scripts, no: ids 560 and up are not routed to archive 0x42 (site 10-11).
For names and descriptions, yes (the bank grows). For learnsets, only if each
member stays under 25 moves or the buffers are resized.

Contest data: I found no move-keyed contest tables in `src/game` (BW has none)
[U, only a name search].

## 4. Save data

- A Pokemon stores four move ids (u16 each), four current PP and four PP-up
  counts in the encrypted blocks (field ids 0x36..0x39 moves, 0x3A..0x3D PP,
  0x3E..0x41 PP up; arm9/poke_tool_param.h:64-71) [V ids]. The u16 width of
  the ids is [I from the `(u16)` casts in `POKE_TOOL_AddBoxMove`,
  poke_tool.c:1400]. I did not read the block layout itself, so it is [U] that
  the in-save width is exactly 16 bits per slot.
- The bag stores TMs/HMs in a pocket of 109 slots (`BAG_TMHM_COUNT 0x6D`,
  arm9/myitem_savedata.c:45, 69).
- I found no per-move flag arrays in the save (no seen/known bitmap per move)
  [U, from name searches].
- Battle records (`BtlWazaRec`, 120 entries) and replay action words hold u16
  / 16-bit ids and are in RAM or in the recording buffer, not in the save
  proper [V sizes].
- Mystery Gift and beacon packets carry move ids as u16 and are filtered by
  the checks in sites 3-7. If the mod format needs ids above 559 in a gift,
  those checks and the packet validators change; the packet field is already
  16 bits [I].

Per-save mod storage (`sdk/mod_savedata.c`) can hold: the TM table extension
(which mod TM item maps to which move), a mod TM bag, and any extra per-Pokemon
data. A Pokemon that knows a move with id above 559 is stored in the normal
u16 slots, so no side storage is needed for the id itself [I].

## 5. Mod author versus port

Mod author supplies:

- Move records (36 bytes each), appended as members to `a/0/2/1`.
- Names and descriptions in text banks 203 and 202.
- An effect script as a member of the move-script archive, and the sound it
  plays.
- Learnset, egg-move, TM and tutor data (when the mod wants them).
- For new mechanics: a handler getter plus its event functions, and any new
  battle strings.

Port must unlock:

- Sites 1-7: replace `0x230` / `560` by one live move count.
- Site 2: allocation size of the Metronome work table.
- Sites 8, 30: a registry that accepts mod rows (wrapper or growable table).
- Sites 10-11: routing of script ids at or above 0x231 for mod moves.
- Sites 15-18: learnset buffers sized by the member.
- Sites 19-23: a mod-extensible TM/HM table, item window test, HM list and,
  optionally, a larger bag pocket and compatibility bit field.
- Move id for AI: nothing; the AI reads the record.

## 6. Open questions

1. What do members 560..600 of `a/0/6/6` (601 members) contain? If they are
   empty, a mod could reuse them; if not, the boundary at 0x231 is also
   tied to real data. Look at the member sizes and at who loads effect ids
   0x231 and above (`BTLV_EFFECT_StartEffect(0x231)` appears at
   ov093/btlv_scu.c:838, 1077 and ov098/pdc_main.c:277).
2. Does anything else treat effect id 0x230 specially? The boundary test is
   `< 0x231` while the move limit is `< 0x230`, so id 0x230 is a gap [V
   code, intent U].
3. Move tutors other than the three starter groups: where is their move list
   (script data in `a/0/5/7`)? Not looked at.
4. Block layout of move/PP in the encrypted Pokemon record (confirm 16-bit
   move ids on the wire).
5. Trainer-battle data (trainer move sets) holds move ids in trainer Pokemon
   records; I did not check their field width or any validation.
6. `BTL_ID_GetMetronomeExcludedMoves` (the list Metronome excludes) lives in
   ov095 data; its length was not read.
7. The Pokemon summary and battle UI may use a move-keyed icon or type
   table (type icons come from the record's type field, so I expect none),
   not searched beyond names.
8. Whether `factor->work` size constrains new handlers (hand_common_types.h
   `BTL_FACTOR_WORK_KEEP 6`).

## 7. Data tables that hold generic effect ids

aletheia names every literal effect id at or above 0x231 as
`BTLV_EFFECT_MOVE_END + n` (n = id - 0x231) in the battle server, the
handlers and the client (ov093, ov095). These tables stay data in aletheia;
moving the boundary in eidos means rebasing their entries at load. Offsets
are bytes from the symbol.

| symbol | address | region | element | entry offsets: id (n) |
|---|---|---|---|---|
| gWeatherEffects | 0x021EFFAE | ov093 | Pair16[5] {a = message id, b = effect id}; b is read by BTL_CLIENT (weather start) | +2: 0x26B (58); +6: 0x26B (58); +10: 0x268 (55); +14: 0x269 (56); +18: 0x26A (57) |
| d_0220A3E4 | 0x0220A3E4 | ov094 | int[][2], read by btlv_input.c as d_0220A3E4[k][screen - 5]; 6 ints seen | +0: 0x247 (22); +4: 0x246 (21); +8: 0x245 (20); +12: 0; +16: 0; +20: 0x245 (20) |
| gBtlvIntroPickTableA | 0x0220AC70 | ov094 | u32[2], intro effect pick | +0: 0x24A (25); +4: 0x24B (26) |
| gBtlvIntroPickTableB | 0x0220AC78 | ov094 | u32[6], intro effect pick | +0: 0x235 (4); +4: 0x24C (27); +8: 0x24D (28); +12: 0x24E (29); +16: 0x24F (30); +20: 0x250 (31) |
| gSimpleEffectIds | 0x0689E35C | ov095 | u16[12], used by BTL_ID_IsSimpleEffectId | +0: 0x256 (37); +2: 0x25A (41); +4: 0x257 (38); +6: 0x258 (39); +8: 0x259 (40); +10: 0x25B (42); +12: 0x25C (43); +14: 0x273 (66); +16: 0x274 (67); +18: 0x282 (81); +20: 0x25E (45); +22: 0x263 (50) |

gSimpleEffectIds is located by symbol: the address row in aletheia's source
is wrong (0x0689E35C) until aletheia fixes it.

Not traced: BTL_CLIENT_StepPlayRecallAnim (btl_client.c) passes args[1]
(u16) to BTLV_SCU_StartMonWithdraw as an effect id (0 selects
BTLV_SCU_EFFECT_WITHDRAW); the server command that carries it was not found
in a literal form.
