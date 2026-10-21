# Limits survey: other counts

Scope: abilities, types, natures, trainers and trainer classes, zones and maps,
field scripts and flags, overworld character codes, TMs and HMs, berries,
battle backgrounds, Pass Powers. All paths are relative to aletheia
`src/game/` unless noted. Archive ids map to NARC paths as
`id = 100*x + 10*y + z` for `a/x/y/z` [V: id 9 is a/0/0/9 at
arm9/arc_tool.c:201 and ov010/map_matrix.c:92; 0x39 is a/0/5/7; 0x5c is a/0/9/2;
0x30 is a/0/4/8; 0xd3 is a/2/1/1; the pattern is also stated in the aletheia
asset notes]. Member counts below come from reading the NARC headers in the
extracted tree [V]. Method note: the ROM data tables named below (for example
`gAbilityHandlerRegistry`) are `extern` in the C and live in the overlay image,
so a change to them needs an eidos-side replacement, not a C edit [V that they
have no C definition: grep finds only extern uses].

## 1. Summary

| Space | Original count | Fixed where | Difficulty |
|---|---|---|---|
| Abilities | 164 ids (0..163), 158 handler entries | handler registry loop, ROM table; `u8` ability fields | Medium: one ROM table plus `u8` widths |
| Types | 17 (0..16), 17 is a "no type" sentinel | 17x17 chart, many literal 0x11 sentinels, icon file arithmetic | Hard |
| Natures | 25 | four random draws, two ROM tables, one text bank | Medium |
| Trainers | 615 real records (616 members) | none in code besides flag and script id windows | Easy to medium |
| Trainer classes | 105 (0..104) | sprite loader `> 0x69`, BGM table sentinel, 7-bit table field | Medium |
| Zones | 427 | cache sentinel 0x1ab, u16 ids, ROM special-zone tables | Medium |
| Area data | 282 | `ZONEDATA_CreateAreaData` bound | Easy |
| Map matrices | 255 | none on id; 30x30 block grid cap | Easy (id), hard (size) |
| Per-zone event file | 428 members, max file 2178 bytes | fixed 2436 byte buffer, u8 counts | Medium |
| Persistent event flags | 2912 (ids 1..0xB5F) | save layout | Needs mod storage |
| Event work values | 318 (ids 0x4000..) | save layout, no bounds check | Needs mod storage |
| Script ids | 46 range records in a ROM table | `SCRIPT_SYS_GetScriptFileId` | Medium |
| Overworld codes | 799 records | `FLDMMDL_ObjCodeToTableIndex` | Easy once the remap is patched |
| TMs and HMs | 101 (92 TM + 6 HM + 3 late TM) | item id windows, ROM move table, 128 bit personal mask | Hard past 128 |
| Berries | 64 | `ITEM_IsBerry` loop over a ROM table | Easy |
| Battle backgrounds | 19 stage rows x 17 columns | 0x24 byte rows, no bounds check | Medium |
| Pass Powers | 48 records, 13 types | `GPOWER_TYPE_MAX`, per-type code | Hard (new mechanics) |

Biggest obstacles:

1. Type 17 is a live "no type" sentinel (0x11) in many battle sites, so a new
   type id cannot simply be 17. See section 3.
2. Facts stored in ROM data tables that the C only reads through `extern`
   (ability registry, type chart, nature mods, berry table, TM move table,
   script range table, hidden power table). Each needs an eidos-side
   replacement table.
3. Persistent event flags and work values are save-sized (2912 flags, 318
   works). Mod flags must go to per-save mod storage.
4. The event data buffer is a fixed 2436 bytes with a 258 byte headroom over
   the largest vanilla zone, and counts are `u8`.
5. The overworld table remap hard-codes three windows and ends at index 798.

## 2. Abilities

Original count: 164 ids. Evidence: the personal record has three `u8` ability
fields (arm9/poke_personal_types.h:37-39) and the battle struct stores
`ability` as `unsigned char` (arm9/poke_tool_types.h:34) [V]. The 164 figure is
from the aletheia notes and the ability name bank, not re-derived [I].

| Site | Function | Literal | Guards | Class | Change |
|---|---|---|---|---|---|
| ov093/btl_action_word_types.h:33, ov093/hand_tokusei.c:324 | `HAND_TOKUSEI_AddAbilityHandlers` | `ABILITY_REGISTRY_COUNT 0x9E` (158) | Linear search of `gAbilityHandlerRegistry` (ROM 0x021F0E74, hand_tokusei.c:25,307). An ability not in the table gets no handler (returns 0). | array size, id check | Replace the registry with an eidos table that has more rows. The loop bound is a macro, so a PC-build `#ifndef __MWERKS__` define plus an eidos-defined array works. |
| arm9/poke_personal_types.h:37-39 | `PersonalData` | `u8 ability1, ability2, hiddenAbility` | Species ability ids | id width | 255 ids available. Above that needs a wider record (data change, see species report). |
| arm9/poke_tool_types.h:34 | `BoxPokemonBlockA` | `unsigned char ability` | Saved ability id | save layout, id width | Saved Pokemon stores the id in one byte. Ids 165..255 fit. Above 255 needs mod storage. |
| ov131/p_sta_skill.c:648-654 | `P_STA_SKILL_*` | msg bank 183 (name), 182 (description) | Text lookup by ability id | text | Add lines to both banks (text report). |
| arm9/poke_tool.c:3075 | `POKE_TOOL_GetBoxValue` | species 493, ability 121 | Arceus Multitype special case | other | None. |

No upper bound check on the ability id was found. Searches for ability
comparisons found only specific-ability tests (for example hand_tokusei.c,
btl_client.c:2896-2906, btl_pokeparam.c:1389, ov095/btl_id_sets.c:208,220) [V].
I did not enumerate every ROM "ability is X" table [I].

Data: handler code is C in ov093/hand_tokusei.c. A new ability with new
mechanics needs C (a getter that returns a `BtlEventHandler` table) plus a
registry row. Ability ids in the personal record are data.

Open: ability id lists such as `BTL_ID_SETS` (ov095/btl_id_sets.c) use fixed
tests. Check them if a mod ability must join "cannot be copied" style sets.

## 3. Types

Original count: 17 real types, ids 0..16 (Normal 0, Grass 11 per
ov093/btl_server_flow.c:5607, Dark 16) [I for the names, V for the count 17].
Id 17 (0x11) means "no type" in battle [V, sites below].

| Site | Function | Literal | Guards | Class | Change |
|---|---|---|---|---|---|
| ov093/btl_calc.c:264-265 | `gBtlCalcTypeAffinityTable` | `u8[17][17]`, ROM 0x021F047C; values 0 none, 2 half, 4 normal, 8 double | Type chart | array size | Replace with an eidos N x N table. Row stride is a compile-time constant at every index site. |
| ov093/btl_calc.c:314-342 | `BTL_CALC_GetTypeAffinity`, `...VsPair` | indexes `[atk][def]` | Effectiveness | array size | Follows the table. |
| ov093/btl_calc.c:379 | `BTL_CALC_ListResistingTypes` | `defType<17` | Loop over defender types | array size | Use the new count. |
| ov093/btl_pokeparam.c:822-830, 849 | `BTL_POKEPARAM_GetTypes`, `...HasType` | `0x11` | Sentinel: type hidden by Roost, `HasType(17)` is false | id sentinel | Move the sentinel to a new constant (for example 0xFF) in PC builds. Every 0x11 below must change with it. |
| ov093/btl_server_flow.c:5260, 7805, 8160 | flow code | `17` | Typeless move: no STAB, type forced to 17, effectiveness 3 | id sentinel | Same. |
| ov093/hand_waza.c:6467-6468 | handler | `0x11` | Typeless check and a 0x11 byte scratch buffer | id sentinel, array size | Same, and size the buffer by type count. |
| arm9/poke_tool.c:2155-2200 | `POKE_TOOL_GetHiddenPowerType` | 16 rows, `sHiddenPowerTypes` ROM 0x0209E288 (17 bytes) | Hidden Power type from IVs | array size | The formula is `n*15/63` reduced mod 16 and indexes a 17 byte table. New types cannot appear unless the formula and table change. Optional. |
| arm9/app_icon.c:128-131, 168 | `APP_ICON_GetTypeCellFileId` | `type + 0x22` in archive 0x53 (a/0/8/3, 172 members) | Type icon cell files 0x22..0x32 | asset index arithmetic | Files 0x33.. are other assets (palette 0x3b.., anim 0x3e.., move kind 0x38..). Type 17+ would read a wrong file. Needs a mod icon path or a remap. |
| arm9/app_icon.c:109,134 | `_PokeTypePltOffset[]` | ROM table indexed by type | Icon palette slot | array size | Replace table. |
| ov070/itemmenu.c:2832, 2906, 2980 | bag screen | `i < 17` | Creates 17 type icon actors | array size | Use the type count. |
| ov131/p_status_types.h:119, p_status.c:744,788 | status screen | `PSTATUS_TYPE_MAX 17` | 17 type icon resources, array `typeChrRes[17]` in a fixed struct | array size | Resize the struct field. |
| ov094/btlv_input.c:1632-1636 | move select UI | type icon loops | Battle UI icons | array size | Follow type count [I, not read in full]. |
| ov094/btlv_effvm.c:2929, 4345-4357 | effect VM | `0x19`, `0x10`, `0x11` | Per-type animation selection (`key += typeIdx`) | other | Type based effects pick a script by type id. A new type needs an effect. Unverified how the key range is bounded. |
| arm9/poke_personal_types.h:44-45 | `PersonalData` | `u8 type1, type2` | Species types | id width | 255 ids. |
| arm9/file_util.c:118 | item record | `naturalGiftType : 5` | Natural Gift type | id width | 5 bits: 32 types. |
| ov093 `hand_item` plate and similar tables | not enumerated | | Item to type | other | Unverified. |

Data: move records carry a type byte (move report). The type name text bank
and the type icon art must grow with the count.

Verdict: adding a type needs a sentinel move plus four table replacements plus
icon work. Unlocking only the chart size is not enough.

## 4. Natures

Original count: 25. Evidence: random draws and a `% 25` below [V].

| Site | Function | Literal | Guards | Class | Change |
|---|---|---|---|---|---|
| arm9/poke_tool.c:690 | `POKE_TOOL_SetBoxValue` init path | `GFL_USE_GetRandom(0x19)` | Random nature for new mon | id range | Use a count variable. |
| arm9/tr_tool.c:407 | `TR_TOOL_FinalizePartyMember` | `((pid>>8) % 0x19)` | Trainer mon nature from personality | id range | Same. Mod trainers could supply a nature instead. |
| ov010/mystery_gift_mon.c:309 | gift mon | `GFL_USE_GetRandom(25)` | Random nature | id range | Same. |
| ov021/sodateya.c:1003 | `SODATEYA_RollNature` | `GFL_USE_GetRandomBelow(0x19)` | Egg nature | id range | Same. |
| arm9/poke_tool.c:199, 2352-2366, 3619-3642 | nature stat tables | `s8[25][5]` (`sNatureStatMods`, `func_02019B04_Table` ROM 0x0209E324) | Stat multiplier by nature (110 percent / 90 percent) | array size | Replace table. |
| arm9/poke_tool_types.h:57 | `BoxPokemonBlockB.nature` | `unsigned char` | Saved nature | save layout | 255 ids. |
| ov119/pokemontrade_save.c:1830 | trade summary | msg index = nature | Nature name text | text | Add lines to the nature name bank. |
| ov093/hand_item.c:2094 | flavor berry confusion | `POKE_TOOL_GetNatureStatMod(..., natureColumn)` | Disliked flavor | other | Reads the same table. |

Obstacle: none hard. Nature text and the stat table are the only data.

## 5. Trainers and trainer classes

Original counts: 616 members in a/0/9/2 (615 real 20 byte records plus a
16 byte sentinel at member 0) [V sizes: min 16, max 20; aletheia asset notes
say record 0 is the sentinel]. Party archive a/0/9/3 has 616 members [V].
105 trainer classes [V from aletheia asset notes; sprite loader bound 0x69 =
105 at arm9/pokegra.c:543].

| Site | Function | Literal | Guards | Class | Change |
|---|---|---|---|---|---|
| arm9/tpoke_data.c:313-322 | `TPOKE_DATA_ReadRecord`, `...ReadParty` | archives 0x5c, 0x5d | Reads member by trainer id with no bound | asset index | Adding members past the end is enough. |
| arm9/tr_tool.c:70-79, 177-180 | `TR_TOOL_BuildParty` | record 0x14, party buffer `0x6C` (6 x 18 bytes), `POKEPARTY_InitEx(party, 6)`, `partyCount` is `u8` | Party size | array size | Six members is the engine limit (party). Buffer 0x6C only fits 6 entries of the 18 byte form. |
| arm9/tr_tool.c:146 | `TR_TOOL_LoadTrainerInfo` | msg bank 0xbe (190), index = trainer id | Trainer name | text | Add lines to bank 190. |
| arm9/tr_tool.c:386-407 | `TR_TOOL_FinalizePartyMember` | ability mode | Party member ability flags | other | none |
| ov010/event_trainer.c:56-71 | `EVENT_TRAINER_*ScriptId` | `+3000`, `+5000` | Trainer id to script id; `IsSecondary` is `>= 5000` | id arithmetic | Primary ids are 3000+id, secondary 5000+id, so a trainer id of 2000 or more collides. Ceiling 1999 [V]. |
| ov010/event_trainer.c:97-103 | defeated flag | `flagOffset + 1420` | Defeated flag is persistent flag 1420+id | save layout | Flags run to 0xB5F (2911) and 0xAA0..0xB5F is cleared as time flags (arm9/zonedata.c:1730, 1788). So ids >= 1300 would land in the daily-reset range [V arithmetic]. Mod trainers need mod storage flags. |
| ov010/fld_btl_inst_tool.c:978 | battle subway | archive 215 (a/2/1/5) | other trainer list | asset index | Separate id space. |
| arm9/tpoke_data.c:381-424 | `TPOKE_DATA_FindTrainerType` | loop `0x1c`, `TrainerKindRecord.trainerType : 7` | Class to kind/group (BGM, battle effect) table of 28 rows; 7 bit class field | array size | Rows missing: kind 12 and group 0x11 fallback [V]. Replace table. Class ids above 127 do not fit the 7 bit field. |
| arm9/field_sound_system.c:2249-2255 | `FIELD_SOUND_SYSTEM_GetTrainerClassBgm` | sentinel `0x69` | Scans `sTrainerClassBgmTable` until class 0x69 | id sentinel | A mod class with id 105 ends the scan early. Move the sentinel. |
| arm9/pokegra.c:537-650 | `POKEGRA_GetTrainer*ArcIndex` | `trType > 0x69` -> index 0; `index*8 + k`, k = 0..7 | Trainer class sprite file in archive 0x48 (a/0/7/2, 760 members) and 0x49 (a/0/7/3, 48 members) via `TrainerGraIndexTable[trType]` | asset index arithmetic | Class 105+ draws as class 0 sprite. 760 members / 8 = 95 sprite sets. Need the bound and the index table replaced, and the new sprite appended as eight consecutive members. |
| ov093/btlv_scu.c:929 | trainer intro | `BTL_MAIN_GetTrainerType` | Sprite lookup | other | Follows the above. |
| ov069/bsubway_record_list_types.h:37 | record | `u8 trainerClass` | Subway record class | id width | 255. |
| ov021/enceff.c:380-382 | encounter effect | class from record field 1 | Effect group | other | Uses the table above. |
| arm9/btl_setup.c:491, 1059 | setup | `TPOKE_DATA_GetTrainerKind` | AI/kind | other | |

Class name text: bank 191 (105 entries) per aletheia notes [V note, not
re-read]; `WORDSET_SetSlotFromMsgE7` uses bank 0xE7 (ov069/bsubway_record_list.c:352) [V].

Data: record format is 20 bytes: byte 0 party type (0..3, selects the
4 party layouts in `TR_TOOL_BuildParty`), byte 1 class, byte 3 party size, items
at 4..11 as four u16, AI flags at 12..15 [V from tr_tool.c:70-79 and the asset
notes]. Party entries are 8, 16, 10 or 18 bytes by type. Add members at the
end of both archives, plus the name line.

## 6. Zones, maps, events, encounters

| Site | Function | Literal | Guards | Class | Change |
|---|---|---|---|---|---|
| arm9/zonedata.c:422-445 | `ZONEDATA_Init` | archive 0x0c (a/0/1/2, one member of 427 x 0x30 bytes); `cachedZoneId = 0x1ab` | Zone header cache starts "holding" record 427 with a zeroed header | id sentinel | Zone id 427 would return a zeroed header. Move the sentinel to 0xFFFF. |
| arm9/zonedata.c:560-575 | `ZONEDATA_GetHeader` | `rid * 0x30` | Reads the header with no bound | asset index | Extending the member is enough once the sentinel moves. |
| arm9/zonedata.c:53-83 | `ZoneHeader` | `placeNameId:10`, `weatherId:6`, `mapChangeType:3`, `cameraId:7`, `expObjSetId:5`, `fieldEffectSetId:5` | Field widths in the 0x30 byte record | id width | A mod zone is limited to 1023 place names, 63 weathers, 127 cameras, 31 object sets (data format). |
| arm9/zonedata.c:1324-1334 | `ZONEDATA_CreateAreaData` | `record >= 282` -> 0; file 13 (a/0/1/3, a raw file of 2820 bytes = 282 x 10) | Area (tileset) record | id check | Raise 282. The file is raw, not a NARC, so eidos must append to it. |
| arm9/zonedata.c:106-116 | zone manager | `lightCount`, `mapEffectCount` are `u8`; `fogCount` `u16` | Per-zone side tables, up to 255 pairs | array size | Light table has 0 entries and map effect 8 in vanilla [V sizes: a/0/8/2 member 1 size 0, a/1/4/9 member 0 size 32]. Fog has 26 pairs (a/0/8/1 member 13: 104 bytes). Room exists. |
| arm9/zonedata.c:819, 860-1004, 1267 | `ZONEDATA_Is*` | many hard-coded zone ids (0x1a6, 0x34, 0x117, 0x118..0x13c, 0x4c..0x69, 0x4e, 0xf6, 0xf7, 0x176, 0x17a) and ROM tables (`_Zone*Table`) | Special zone behavior (union room, entralink, palace, gimmicks) | other | A new zone gets none of these. Add tables only if the mod needs the behavior. |
| ov010/map_matrix.c:92 | `MAP_MATRIX_Load` | archive 9 (a/0/0/9, 255 members) | Matrix by id | asset index | No bound. Add members. |
| ov010/map_matrix.c:44-52, 176-183 | `struct MapMatrix`, `MAP_MATRIX_SetSize` | `zoneGrid[900]`, `mapGrid[900]`; width <= 30, height <= 30, count <= 900; struct 0x152C | Block grid | array size | Larger maps need a bigger struct and a changed offset (comment says 0x71C is pinned). Layout is also read by ov021 mapper code [I]. |
| ov021/field_g3d_mapper.c, a/0/0/8 | map models | `mapGrid` holds a u32 index into a/0/0/8 (649 members) | Map model per block | asset index | Add members. Whether any caller bounds the index was not checked [I]. |
| ov010/eventdata_system.c:244-249, 362-394 | `EVENTDATA_SYSTEM_Create`, `...ReadEventFile` | alloc `0xAA8`; file read at +0x124; counts `u8` (bg 0x14, npc 0x24, connect 0x14 byte records; pos events 0x16) | Per-zone events and warps in archive 0x7D (a/1/2/5, 428 members) | array size | The buffer holds 0xAA8-0x124 = 2436 bytes of file. The largest vanilla member is 2178 bytes [V], so 258 bytes of headroom. There is no size check, so a larger file overflows the heap block. Raise the alloc size. Counts per kind cap at 255. |
| ov010/eventdata_system.c:289-293, ov010/field_encount_st.c:81-100 | `...LoadEncountData` | archive 0x7E (a/1/2/6, 112 members); row 0xE8 bytes; `rowCount` must be 1 or 4 | Encounter rows | asset index | `ZoneHeader.encountId` (u16) selects the member; 0xFFFF means none. Add members. Species field is `u16 species:11, form:5` (field_encount_st_types.h:7-9): species up to 2047, forms up to 31. Slots per set are 5 and 5 (arrays in the row) plus fixed rows inside the 0xE8 bytes, a data-format limit. |
| ov010/calender.c:31 | `CALENDER_ZONE_MAX 68` | Calendar zone list | other | none unless mod zones should show |

Zones also feed: scripts (header `scriptArcId`, `initScriptArcId`, `msgArcId`),
BGM (4 u16 per season), weather, camera, place name. These are all header
fields, so a mod zone is a new header record plus new members.

## 7. Field scripts, flags and work values

| Site | Function | Literal | Guards | Class | Change |
|---|---|---|---|---|---|
| ov010/script_sys.c:608-650 | `SCRIPT_SYS_GetScriptFileId` | table of 46 range records (`d_021701A8`: firstId, lastId, fileId, textBank, textMsgId); `scriptId > lastId` panics; fall back to zone script file and `scriptId - 1` | Script id to file | id check, asset index | Script ids below the first record map through the zone header (`scriptArcId`). Ids past the table's last range panic. Per-zone scripts are id 1.. inside the zone file. Replace the 46 row table to add global ranges. |
| ov010/script_sys.c:657-663 | `SCRIPT_SYS_LoadScriptFile` | archive 0x39 (a/0/5/7, 899 members) | Script file by member id | asset index | Add members. |
| ov010/script_sys.c:676-682 | `SCRIPT_SYS_IsZoneScriptId` | `< 0x291C` and `!= 2000` | Zone vs other script id classes | id range | Ids 0x291C and up are not zone scripts. Mod scripts above need this widened. |
| ov010/script_sys.c:590 | `VM_Create` params | `cmdCount = d_021705D8` (ROM command table) | Number of script commands | array size | New script commands need a larger command table. Count not read [I]. |
| arm9/zonedata.c:131-137, 1722-1740 | `struct EventWork`, `ZONEDATA_GetEventFlagAddress` | `flags[0x16c]` bytes = 2912 bits; ids 1..0xB5F; id >= 0x4000 is temp: 8 bytes (64 flags) | Persistent event flags | save layout | Out of range flag ids silently return no address (set is a no-op, check is false) [V]. Mod flags go to per-save mod storage. |
| arm9/zonedata.c:1760-1768 | `ZONEDATA_GetEventWorkAddress` | `work[0x13e]`, ids 0x4000.. | Persistent work values (u16), 318 slots | save layout | No bounds check [V]. An out of range work id writes outside the save struct. Redirect ids above 0x4000+317 to mod storage. |
| arm9/zonedata.c:1782-1786 | `ZONEDATA_ClearTimeFlags` | flags 0xAA0..0xB5F | Daily reset range | other | Do not allocate vanilla-range flags there. |

## 8. Overworld character codes (coordinator's extra input)

Verified: all three claims hold, with the details below.

- `FLDMMDL_LoadObjCodeParam` (ov010/fldmmdl.c:2067-2100) reads archive 0x30
  (a/0/4/8) by `offset = FLDMMDL_ObjCodeToTableIndex(objCode) * 0x1c + 4`
  (fldmmdl.c:2098) through `ARC_TOOL_ReadHandleMemberRange(handle, 0, offset,
  0x1c, out)` [V]. The handle is created at fldmmdl.c:2028.
- `FLDMMDL_ObjCodeToTableIndex` (arm9/fldmmdl_save.c:356-368, 0x0200F128) [V]:
  - `x < 0xdf` returns `x`: indices 0..222.
  - `0x1000 <= x < 0x1236` returns `(u16)(x - 0xf21)`: indices 223..788.
  - `0x2000 <= x < 0x200a` returns `(u16)(x - 0x1ceb)`: indices 789..798.
  - Anything else returns 10.
  So the last reachable index is 798. a/0/4/8 holds one member; the aletheia
  asset notes give 22376 bytes = 4 + 799 x 28, so exactly 799 records [V:
  arithmetic; the size comes from the notes, the NARC file is 22436 bytes].
  A record appended at index 799 or higher is unreachable.
- `offset` is `u16` (fldmmdl.c:2090): the largest record with a representable
  offset is `(65535 - 4) / 28 = 2340` (index 2340 gives offset 65524) [V].
  The proposal's "up to 2340" is correct. Index 2341 would wrap.
- The proposed mapping `code >= 0x3000 -> 799 + (code - 0x3000)` fits that
  limit for codes up to 0x3000 + 1541. It needs a branch added to the
  function, which has an `#ifdef` wrap point in the PC build. Alternatively
  widen `offset` to `u32` in the PC build and use any scheme.

Other readers of the same remap and of the codes (must be checked when the
function changes):

| Site | Notes |
|---|---|
| arm9/tpoke_data.c:227 `TPOKE_DATA_GetRecordPtr` | Calls `FLDMMDL_ObjCodeToTableIndex` on a record index (`recordIndex + 0`) and multiplies by 0x1c. This is the only other caller [V]. If the remap is widened, this site changes meaning for indexes above 222. The name looks odd (a trainer record container using an object code remap), so check whether the decomp name is wrong or the code reuses the remap on purpose [I, unresolved]. Keep this caller on the original function if it should keep its behavior. |
| ov010/fldmmdl.c:2077-2096 | Param cache holds `count < 0x1a` codes. Codes are compared raw, not remapped. |
| ov010/fldmmdl.c:2131-2137 `FLDMMDL_GetEventWorkObjCode` | Codes 162..177 are replaced by event work values (id `code + 0x3f7e`). Mod codes must stay outside 162..177. |
| ov010/fldmmdl.c:553 and ov021/fldmmdl_draw.c:787-858, ov010/fldmmdl_poslog.c:209 | Special cases for codes 0xb9, 0xd3-0xd6, 0x79, 0x6D. |
| ov010/fldmmdl_types.h:54,117,212 | `objCode` is `unsigned short` in headers, the live mmdl and the check info: 16 bit codes are fine. |
| ov011/intrude_comm.c:2718, ov011 `INTRUDE_COMM_IsValidObjCode` | Network players: codes in 0xdf..0xfff or >= 0x1236 become 10. A mod code is rejected here. Not needed unless mod sprites are used for remote players. |
| MmdlObjCodeParam.resource (fldmmdl_types.h:32-49) | 12 bytes of resource ids per record. These select models and textures in archive 0x31 (a/0/3/1, 24 members) through `G3D_SYSTEM_CreateResourceArcHandle` (ov021/fldmmdl_blact.c:517,693; fldmmdl_g3dobj.c:159) [V for the path, I for the exact meaning of each of the 12 bytes]. A new sprite also needs a resource member and perhaps a draw proc (`drawProcNo`). |

Port change: patch `FLDMMDL_ObjCodeToTableIndex` in the PC build to add the
mod window, widen `offset` to `u32`, make `TPOKE_DATA_GetRecordPtr` call the
original mapping, and route the 0x1c record read for index >= 799 to the
mod-supplied record file. Resources: the mod supplies the model/texture
members.

## 9. TMs and HMs

Original count: 101 machines (92 TM, 6 HM, 3 later TM) [V from windows].

| Site | Function | Literal | Guards | Class | Change |
|---|---|---|---|---|---|
| arm9/file_util.c:696-701 | `FILE_UTIL_IsMachineItem` | item ids 0x148..0x1A9 and 0x26A..0x26C | Which items are machines | id check | Add a mod window or replace with a table. |
| arm9/file_util.c:727-738 | `FILE_UTIL_GetMachineMove` | `g_0209EAA0[]` (ROM u16 table, 101 entries) | Machine item to move | array size | Replace with a larger table. |
| arm9/file_util.c:757-762 | `FILE_UTIL_IsHmMove` | loop 6, table entries 0x5C..0x61 | HM moves cannot be forgotten | other | Table position fixed. |
| arm9/file_util.c:775-786 | `FILE_UTIL_GetId` | subtrahends 0x148, 0x145, 0x20E; returns u8, 0xFF if not a machine | Machine slot number and bag sort key | id arithmetic | Returns slot 0..100. Also used for sort (arm9/myitem_savedata.c:473). |
| arm9/file_util.c:793-803 | `FILE_UTIL_GetMachineIndex` | 0x1A4..0x1A9 -> 0..5 | HM index | other | |
| arm9/poke_tool.c:2288-2292 | `POKE_TOOL_CheckSpeciesTmCompat` | `GetPersonalParam(sp, form, tmIndex/32 + 0x27)` | Compatibility bit from four u32 words (`tmFlags1..4`, arm9/poke_personal_types.h:55-58) | array size | 128 bits. A tm index of 128 or more reads param 0x2b, which is the tutor word. Hard limit 128 machines without a larger personal record or a side table. |
| ov021/sodateya.c:1488-1509 | `SODATEYA_*` egg move TM check | `<0x20, <0x40, <0x60`, else | Same 128 bit mask | array size | Same. |
| ov021/scrcmd_pokemon_fld.c:925-928 | script | `FILE_UTIL_GetId(0x151)` | Check TM compat | other | |

Headroom: 27 free bits (101 to 128). Past that, add an eidos compat table
keyed by species, form and machine id.

## 10. Berries

| Site | Function | Literal | Guards | Class | Change |
|---|---|---|---|---|---|
| arm9/palanm.c:214-221 | `ITEM_IsBerry` | loop `0x40` over `sItemBerryTable` (ROM 0x0209E968) | Berry test used by Pluck-like moves and abilities (ov093/hand_tokusei.c:5634,5709; hand_waza.c:8660) | array size | Replace table (64 entries). Despite the name the argument is an item id [I]. |
| arm9/file_util.c:117-118, 564-566 | item record | `naturalGiftPower` u8, `naturalGiftType:5` | Natural Gift | id width | |

No berry planting system was found in this survey [I; not searched
beyond names]. Berry effects live in ov093/hand_item.c (not enumerated).

## 11. Battle backgrounds

| Site | Function | Literal | Guards | Class | Change |
|---|---|---|---|---|---|
| ov094/btlv_effect.c:197-202, 748-770 | battle effect create | archive 0x98 (a/1/5/2): member 0 is 684 bytes = 19 rows of 0x24; `fieldId[17]`, `stageId[17]` | Row chosen by `situation.stageGroup`, column by `stageIndex` | array size, asset index | No bound on `stageGroup`; rows come from a data file. 17 columns per row. Append rows and set the situation (mod). |
| ov094/btlv_stage.c:96-138 | `BTLV_STAGE_Create` | member 2 table, `fileId[4][variant]`, archive 11 (a/0/1/1, 222 members) | Stage model files | asset index | Append rows and models. The `variant` index range is not checked [I]. |
| ov094/btlv_effect_shared_types.h:29-30 | situation struct | `stageGroup`, `stageIndex` | | | The code that picks the group from the map type or zone was not found by grep; open question. |

## 12. Pass Powers

| Site | Function | Literal | Guards | Class | Change |
|---|---|---|---|---|---|
| arm9/gpower.c:37 | `GPowerType` | `GPOWER_TYPE_MAX 13` (type 0xD is none) | Arrays `secondsLeft`, `activeId`, `value` sized by it | array size | Runtime state struct of 0x60 bytes. |
| arm9/gpower.c:59, 398-402 | | `GPOWER_ID_NONE 0x30` (48); archive 0x26 (a/0/3/8) member 0 = 636 bytes in the NARC member, 12 byte records (48 records) | Power table | id sentinel | Records past 47 clash with the sentinel only if the sentinel stays 0x30. |
| arm9/gpower.c:458-651 | per-type effects | `value[0]`..`value[10]` read by hard-coded index | Each type has fixed code (catch rate, exp, price, healing, etc.) | other | A new power of an existing type is data only. A new type needs C. |

I did not check whether `GPowerState` is written to the save [I]. Its comment
says it is the runtime state.

## 13. Data and assets (per-id data)

| Archive | Path | Members | Record | Adding past the end is enough? |
|---|---|---|---|---|
| 0x5c | a/0/9/2 | 616 | 20 bytes | Yes (trainer data) |
| 0x5d | a/0/9/3 | 616 | 6..108 bytes, by party type | Yes |
| 0x48, 0x49 | a/0/7/2, a/0/7/3 | 760, 48 | 8 files per class | No: also needs index table and bound |
| 0x0c | a/0/1/2 | 1 | 427 x 0x30 | Extend the member |
| 13 (raw file) | a/0/1/3 | raw, 2820 bytes | 282 x 10 | Extend and raise the bound |
| 0x7D | a/1/2/5 | 428 | event file, max 2178 bytes | Yes, within 2436 bytes |
| 0x7E | a/1/2/6 | 112 | 0xE8 x 1 or x 4 | Yes |
| 9 | a/0/0/9 | 255 | matrix, 900 block cap | Yes |
| 0x39 | a/0/5/7 | 899 | script bytecode | Yes (plus range table) |
| 0x30 | a/0/4/8 | 1 | 799 x 28 | Only with the remap change |
| 0x31 | a/0/3/1 | 24 | resource sets | See section 8 |
| 0x98 | a/1/5/2 | 3 | rows 0x24 / table / table | Extend |
| 0x26 | a/0/3/8 | 1 | 48 x 12 | Extend (power ids) |
| 0x53 | a/0/8/3 | 172 | UI icon cells (type icons 0x22..0x32) | No (index arithmetic) |

## 14. Save data

- Ability, nature, types are stored in the Pokemon record as bytes (ability
  u8, nature u8). Ids above 255 need mod storage.
- Event flags (2912) and work values (318) are save-sized (section 7). Mod
  flags and works go to mod storage.
- Trainer defeated state is persistent event flag 1420 + id; mod trainers
  must use mod storage (section 5).
- G-Power state is runtime (0x60 bytes) [I].
- The map matrix, zone header and trainer records are read only from archives.

## 15. What the mod author supplies vs what the port unlocks

Mod author: records for trainers, trainer classes and their sprites, zone
headers, matrices, event files, encounter rows, script bytecode, area data,
overworld records and resources, stage rows, nature and ability names, new
type art, and C for new ability or Pass Power mechanics.

Port: sentinel moves (type 17, zone cache 427, BGM class 0x69, Pass Power
0x30), table replacements for the ROM tables listed above, bound removals
(area 282, class 0x69, ability registry count, event buffer size), the
overworld remap and `u16` offset widening, mod storage for flags and work
values, and `ZONEDATA_GetEventWorkAddress` bounds.

## 16. Open questions

1. Where `stageGroup` and `stageIndex` are computed from the map (search
   ov093/ov094/ov096 for the setup builder).
2. Whether `TPOKE_DATA_GetRecordPtr` (arm9/tpoke_data.c:227) really calls the
   overworld remap on purpose.
3. Number of script commands (`d_021705D8`) and whether it can be extended.
4. Bounds in `field_g3d_mapper` on the a/0/0/8 index.
5. Type based tables not read: plate and drive items, Judgment, Natural Gift
   power table, AI type tables (ov096/tr_ai), effect script key range.
6. Whether any other table keyed by ability id exists in ROM (clone, swap or
   suppress sets).
7. Contents of the 12 byte resource field in `MmdlObjCodeParam`.
8. Text bank numbers named here (182, 183, 190, 191, 0xE7) come from code
   (182, 183, 0xbe, 0xE7 [V]) and the asset notes (191 [V note]).
