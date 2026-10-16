# Story hacks

A story hack is a new region and a new narrative. This document answers
whether the planned mod system supports one, and what each part needs. Every
claim names the file, address or document it comes from. Where the tree does
not settle something, it says so and says what would settle it.

The short answer. The scripts, the zone records, the map tables, the events,
the encounters and the town map are data, and the data layer already replaces
a whole file or a NARC member (`docs/mods.md`, `sdk/vfs_narc.c`). A new region
that reuses the existing zone ids is a pure data mod. A new region added
alongside Unova needs new zone ids at or past 427, and the code uses 427 as a
"no zone" sentinel, so that form needs code overrides. A new script command or
a new event kind needs code. Nothing runs until the port wires the field
system and the script VM, which is the same work the game itself needs.

## 1. Where the story scripts live, and how a map names one

The field story runs on a bytecode VM in the game's own `vm.c`, at ARM9
`0x020111EC` to `0x02011330` (`docs/ASSETS.md`, "Script virtual machine located
and decompiled"). The field system drives it through `SCRIPT_SYS_CreateVm` at
`0x02158BFC` and `SCRIPT_SYS_Main` at `0x0215882C`
(`src/game/ov010/script_sys.c`; `mod/declared_set.txt`, header comment). The
command table is `d_0217062C` in ov010, 609 entries, with the count in the
data word `d_021705D8` = 0x261 (`docs/ASSETS.md`; `src/matched/ov/ov010_func_02158BFC.c`,
which sets `params.cmdCount = d_021705D8`).

The script bytecode archive is `a/0/5/7`, archive id 0x39 = 57
(`docs/ASSETS.md`). `SCRIPT_SYS_LoadScriptFile` loads it with
`ARC_UTIL_LoadMember(0x39, fileId, ...)` (`src/game/ov010/script_sys.c`). A
script sub-file is a table of u32 entry offsets terminated by the halfword
`0xFD13`, with code after it. Entry k starts at `4*k + 4 + table[k]`
(`docs/ASSETS.md`).

The zone record names the script file. `SCRIPT_SYS_GetScriptFileId` maps a
script id to a `(fileId, textBank, textMsgId)` triple. A 46-entry range table
`d_021701A8` answers the special ids (the init and recover scripts among them).
Below that table it answers from the current zone: `fileId =
ZONEDATA_GetScriptArcId(zoneId)`, `textMsgId = ZONEDATA_GetMsgArcId(zoneId)`,
and the entry index is `scriptId - 1` (`src/game/ov010/script_sys.c`).
`ZONEDATA_GetScriptArcId` reads record offset `+0x06` and `ZONEDATA_GetMsgArcId`
reads `+0x0a` (`src/game/arm9/zonedata.c`).

The message bank is `a/0/0/3`, archive id 3, member `+0x0a`
(`docs/ASSETS.md`; `src/game/ov010/script_sys.c`, `SCRCMD_WORK_LoadMsgData`).

The field events are separate. `EVENTDATA_SYSTEM_ReadEventFile` reads a zone's
event file from archive 0x7D = 125 = `a/1/2/5`, member
`ZONEDATA_GetEventDataId(zoneId)` = record `+0x16`
(`src/game/ov010/eventdata_system.c`). The file has four sections, background
events (0x14 bytes), NPC headers (0x24 bytes), connects or warps (0x14 bytes)
and position events (0x16 bytes), each with a count byte read from the file.
Every event record carries a u16 `scriptId`
(`src/game/ov010/eventdata_system.c`, `EVENTDATA_SYSTEM_GetPosEventScriptId`,
`EVENTDATA_SYSTEM_SearchBgEventScriptId`, the `BgEventData` and NPC structs).

So a map's event entry names its script by a u16 script id, and the zone's
script file is a member of `a/0/5/7` selected by record `+0x06`.

A mod can supply a new script through the data layer. `a/0/5/7`, `a/0/0/3` and
`a/1/2/5` are NARCs, and a mod replaces or adds a member by holding a directory
at the archive's path with files named `<index>` (`docs/mods.md`, "Member
overrides"; `sdk/vfs_narc.c`). A new script file is a new member of `a/0/5/7`.
A zone record points `+0x06` at it. A new event entry is a new member of
`a/1/2/5` or a replacement of one. The zone records themselves are the single
sub-file 0 of `a/0/1/2`, so a mod replaces that file whole or replaces member 0
(`docs/mods.md`).

## 2. The map and zone tables, and whether they can grow

The zone header table is sub-file 0 of `a/0/1/2`, 427 records of 48 bytes,
20496 bytes with no remainder (`src/game/arm9/zonedata.c`, the `ZONEDATA_Init`
comment; `docs/ASSETS.md`, "Zone header table located by code"). The record is
the map header in this game. Its named lanes are `mapType` `+0x00`,
`mapSubType` `+0x01`, `areaId` `+0x02`, `matrixId` `+0x04`, `scriptArcId`
`+0x06`, `initScriptArcId` `+0x08`, `msgArcId` `+0x0a`, four `bgm` ids at
`+0x0c`, `encountId` `+0x14`, `eventDataId` `+0x16`, `parentZoneId` `+0x18`,
`placeNameId` and `placeNameWindow` at `+0x1a`, `weatherId` and the flag word
at `+0x1c` and `+0x1e`, and the start position at `+0x24`
(`src/game/arm9/zonedata.c`, `struct ZoneHeader`).

The reader takes no count. `ZONEDATA_GetHeader` at `0x02013AD0` reads record
`id` with `ARC_TOOL_ReadHandleMemberRange(handle, 0, id * 0x30, 0x30, ...)`
and does not bounds-check the id (`src/game/arm9/zonedata.c`). It takes no
count from the data and it enforces no constant. A longer `a/0/1/2` therefore
answers a new id, because the reader indexes by id.

The count 427 appears in code, as a sentinel, not as a bound:

- `ZONEDATA_Init` at `0x020139B0` seeds the cache tag `cachedZoneId = 0x1ab` =
  427, one past the last id, a never-matches tag
  (`src/game/arm9/zonedata.c`).
- `beacon_detail_gra.c` clamps with `if (a >= 427) a = 317;` and tests
  `value >= 427` (`src/game/ov216/beacon_detail_gra.c`).
- `field_place_name.c` sets `prevZoneId = 0x1AB` (`src/game/ov021/field_place_name.c`).
- `field_area_zone.c` defines `ZONE_AREA_NONE 0x1ab`
  (`src/game/ov021/field_area_zone.c`).
- `field_gimmick_gate_types.h` defines `GATE_NEWS_ZONE_ANY 0x1ab`
  (`src/game/ov048/field_gimmick_gate_types.h`).
- `config.c` writes `location_data->zone_id = 0x1AB`
  (`src/game/arm9/config.c`).

The verdict. A new zone record is a data change, and the reader carries it. A
zone with an id below 427 is reachable with no code change. A zone with an id
at or past 427 collides with the 427 sentinel, so that form needs a code
override at each sentinel site above. The cleanest new-region form that needs
no code is to reuse the existing 427 ids and replace their content, because the
reader, the save and every other data table already address those ids.

The zone record is also one-to-one with other tables, and a new zone extends
each:

| What a zone reaches | Table | Size | Zone lane | Kind |
|---|---|---|---|---|
| area data | `a/0/1/3` (id 13) | 282 records of 10 bytes | `areaId` `+0x02`, max 273 | data |
| map matrix | `a/0/0/9` (id 9) | 255 sub-files | `matrixId` `+0x04`, max 254 | data |
| map model | `a/0/0/8` (id 8) | 649 sub-files | reached through the matrix, not the zone record | data |
| event file | `a/1/2/5` (id 0x7D) | 428 sub-files | `eventDataId` `+0x16`, bijection onto 0..426 | data |
| script file | `a/0/5/7` (id 0x39) | 899 sub-files | `scriptArcId` `+0x06`, max 853 | data |
| message bank | `a/0/0/3` (id 3) | 472 sub-files | `msgArcId` `+0x0a`, max 468 | data |
| encounter table | `a/1/2/6` (id 0x7E) | 112 sub-files | `encountId` `+0x14` | data |

Sizes are from `docs/ASSETS.md` and from the archives in `extract/data/`. The
matrix table and the map model are the map's geometry: `matrixId` selects a
matrix in `a/0/0/9`, whose u32 cells are `a/0/0/8` indices, and the empty cell
is `0xFFFFFFFF` (`docs/ASSETS.md`, "Zone to map model SOLVED"). The record
`+0x16` is a bijection onto 0..426 and `+0x16` is the event data id, so a new
zone needs a new event file and extends the permutation
(`docs/ASSETS.md`; the record read below).

The encounter lane is worth naming because two aletheia sources disagree.
`FIELD_ENCOUNT_ST_LoadEncountData` uses `member = ZONEDATA_GetEncountId(zoneId)`
and treats `0xFFFF` as "no table" (`src/game/ov010/scrcmd_musical.c`).
`ZONEDATA_GetEncountId` reads record `+0x14` (`src/matched/arm9/unit_02013EC0.c`).
A read of sub-file 0 of `a/0/1/2` shows `+0x14` is `0xFFFF` on 315 of 427
records and a distinct value in 0..111 on the other 112, one zone per member,
which matches the 112 sub-files of `a/1/2/6`. `docs/ASSETS.md`, "Wild encounter
tables located", instead ties the lane to record `+0x1a` low 10 bits. The
`+0x1a` low 10 bits are `placeNameId` in `src/game/arm9/zonedata.c`. The code
is the reader the game runs, so `+0x14` is the encounter lane. What would
settle it fully is a call-site trace of the wild encounter step, not run here.

## 3. What a new region needs

Each part, and whether it is data or code.

| Part | Where it lives | Data or code |
|---|---|---|
| zone layout and headers | sub-file 0 of `a/0/1/2`, 48-byte records | data |
| map geometry | `a/0/0/9` matrices and `a/0/0/8` models | data |
| tilesets and textures | the graphics NARCs under `extract/data/a` | data |
| warps between zones | `ConnectData` in the zone event file `a/1/2/5`, and scripted warps | data |
| encounters | `a/1/2/6`, index in record `+0x14` | data |
| NPCs and triggers | the zone event file `a/1/2/5`, each with a script id | data |
| the story itself | script sub-files in `a/0/5/7`, messages in `a/0/0/3` | data |
| region map screen rows | member 0 of `a/0/8/6` (`TOWNMAP_GRH_LoadTownInfo`, `ARC_UTIL_LoadMember(0x56, 0, 0, ...)`) | data |
| region map screen art | `a/0/8/5` (`TOWNMAP_GRH_Create`, `func_020490F4(0x55, ...)`) | data |
| region map town count | `0x3d` = 61 | code |
| matrix size limit | width and height at most 30 | code |

The region map screen is the town map, ov072. Its rows are data: member 0 of
`a/0/8/6`, a row of 27 u16 (`src/game/ov072/townmap_grh.c`,
`src/game/ov072/townmap_grh_types.h`). Its count is code. `TOWNMAP_MapInit`
sets `map->townCount = 0x3d;` and loops that many rows
(`src/game/ov072/townmap.c`). `TOWNMAP_GRH_FindRecordIndex` loops
`rowIndex < 0x3d` (`src/game/ov072/townmap_grh.c`). A region map with more than
61 towns therefore needs a code override in those two functions.

The matrix size limit is code. The runtime matrix object holds two fixed
900-entry grids, so a matrix is at most 30 by 30, and `func_02159C0C` refuses
any width or height above 30 (`docs/ASSETS.md`, "Zone to map model SOLVED"). A
region built from many small matrices, as Unova is, needs no change. A single
larger matrix does.

Warps are data in two forms. The connect records in the zone event file carry
the exit zone id and position, and the field map change code applies them
(`src/game/ov010/eventdata_system.c`, `ConnectData`; `docs/mod-coop.md`,
"Warps"). A scripted warp is a script command in `a/0/5/7`.

## 4. What a new mechanic in a story needs

A mechanic that is not data needs a code override through the seam. Four cases
in a story are code, not data.

- A new script command. The command table `d_0217062C` is ov010 data with 609
  entries, but each entry is a handler function, and the permit table
  `d_02170374` gates each command per call mode
  (`src/game/ov010/script_sys.c`, `SCRIPT_SYS_CheckCommandPermit`;
  `src/matched/ov/ov010_func_02158BFC.c`). A new opcode needs a new handler
  function and a new permit entry, so it is code. The count word `d_021705D8`
  is data, so the count itself is not the obstacle.
- A new event kind. The event file has four fixed sections, background, NPC,
  connect and position, each with a fixed record size
  (`src/game/ov010/eventdata_system.c`, `EVENTDATA_SYSTEM_ReadEventFile`). A
  fifth kind needs code.
- A new save flag or work value outside the current range. The event work is a
  fixed 0x3ea-byte struct, `work[0x13e]` for ids from 0x4000 and `flags[0x16c]`
  for ids 1 to 0x3fff (`src/game/arm9/zonedata.c`, `struct EventWork`). A story
  flag outside that range needs code.
- A new field effect or battle effect. Both are code modules in the overlays.

The seam is the declared set and the slot of `docs/mod-interface.md`. A code
override reaches a function only if the address is in `mod/declared_set.txt`
and the generators emit a slot for it (still-to-build item 1 of
`docs/mod-interface.md`). The set already names the script VM entry points and
many script command handlers (`mod/declared_set.txt`). It names the zone setup
functions `FIELD_MAP_CHANGE_ApplyZoneHeader` and `FIELD_MAP_CHANGE_SetupZone`,
the warp command `SCRCMD_WarpToFixedZone` and `FIELDMAP_GetEncount`, but no
`ZONEDATA_*` record accessor, no matrix reader and no town map function.

## 5. What a story hack waits on

The port has not wired the field system or the script VM. `docs/status.md`,
Milestone 7, states the field system (movement, warps, script VM, encounters)
is matched but not wired into the port. `docs/mod-coop.md`, section 1, states
the build list carries no `FIELDMAP`, `FLDMMDL`, `FIELD_PLAYER` or `ov021`
row. The overlay set the build compiles is the title path, ov009, ov010, ov011,
ov013, ov017, ov228, ov088 and ov179 (`CMakeLists.txt`, the `EIDOS_OVERLAYS`
comment; `docs/title-closure.md`). The field overlay ov021, the map change
overlay ov020 and the town map overlay ov072 are not in that set. So the zone
records, the matrices, the events and the scripts have no code to run them
until the field system is wired.

A story hack waits on the same work the game itself needs to go past the title
menu. It is not extra. The only part a story hack needs beyond Milestone 7 is
the mod seam: the slot and the registry of `docs/mod-interface.md`, so a
dropped-in mod can override the count constants of section 3 and add a new
script command or event kind of section 4. That seam is the port's own work,
the same seam Nuzlocke and co-op need, not a story-hack-specific one.

## Still to build

1. **Wire the field system and the script VM.** ov021, ov020 and ov072 and the
   ARM9 closure join the boot, so `ZONEDATA_*`, `FIELDMAP_*`, `SCRIPT_SYS_*`
   and the town map are callable. Evidence: `docs/status.md`, Milestone 7;
   `docs/mod-coop.md`, section 1; `CMakeLists.txt`, the `EIDOS_OVERLAYS`
   comment. Without this no zone record, event or script runs.
2. **Fix the new-region strategy.** A region that reuses the existing 427 zone
   ids is a data mod and needs no code. A region added alongside Unova needs new
   ids at or past 427. Evidence: `ZONEDATA_GetHeader` indexes by id with no
   bound (`src/game/arm9/zonedata.c`); 427 is the "no zone" sentinel
   (`ZONEDATA_Init` `0x020139B0`; the clamps in `beacon_detail_gra.c`,
   `field_place_name.c`, `field_area_zone.c`, `field_gimmick_gate_types.h`,
   `config.c`).
3. **Override the 427 sentinel sites if new ids are added.** One override per
   site named in item 2, through the declared set and the slot of
   `docs/mod-interface.md`, section 2. Evidence: the same list.
4. **Raise the town map count if the region map grows.** `TOWNMAP_MapInit`
   holds `0x3d` and `TOWNMAP_GRH_FindRecordIndex` loops `0x3d`
   (`src/game/ov072/townmap.c`, `src/game/ov072/townmap_grh.c`). A region with
   more than 61 towns needs a code override in both.
5. **Add the map, zone and town map addresses to the declared set.** The set
   names the zone setup functions `FIELD_MAP_CHANGE_ApplyZoneHeader` and
   `FIELD_MAP_CHANGE_SetupZone`, the warp command `SCRCMD_WarpToFixedZone` and
   `FIELDMAP_GetEncount`, but no `ZONEDATA_*` record accessor, no matrix reader
   and no town map function, so an override cannot reach one today. Evidence:
   `mod/declared_set.txt`; still-to-build item 1 of `docs/mod-interface.md`.
6. **Build the mod loader and the slot.** A dropped-in mod cannot run code
   without the loader, and no code override reaches a call site without the
   slot. Evidence: `docs/mod-interface.md`, still-to-build items 1 and 4;
   `docs/modding.md`, gap list item 1.
7. **Settle the encounter lane.** The code reads record `+0x14`
   (`src/matched/arm9/unit_02013EC0.c`;
   `src/game/ov010/scrcmd_musical.c`), and `docs/ASSETS.md` names record
   `+0x1a` low 10 bits. A call-site trace of the wild encounter step settles
   which lane a new zone's encounter table index belongs in.
8. **Decide how a new script command or event kind is added.** Both are code
   (`d_0217062C` and its handlers; the four event sections of
   `EVENTDATA_SYSTEM_ReadEventFile`). A mod that needs one needs a declared
   address and a slot. Evidence: `src/game/ov010/script_sys.c`;
   `src/game/ov010/eventdata_system.c`.
9. **Check the matrix size limit against the intended region.** A single matrix
   is at most 30 by 30 (`func_02159C0C`; the fixed 900-entry grids in the
   runtime object). Evidence: `docs/ASSETS.md`, "Zone to map model SOLVED".

## Sources

- `src/game/ov010/script_sys.c`, `src/game/arm9/zonedata.c`,
  `src/game/ov010/eventdata_system.c`, `src/game/ov010/scrcmd_musical.c`,
  `src/game/ov072/townmap.c`, `src/game/ov072/townmap_grh.c`,
  `src/matched/arm9/unit_02013EC0.c`, `src/matched/ov/ov010_func_02158BFC.c`.
- `docs/ASSETS.md` in the aletheia checkout: "Script virtual machine located
  and decompiled", "Zone header table located by code", "Zone to map model
  SOLVED", "Wild encounter tables located".
- `docs/mods.md`, `docs/mod-interface.md`, `docs/modding.md`, `docs/mod-coop.md`,
  `docs/status.md`, `CMakeLists.txt`, `mod/declared_set.txt`.
- The archive reads: sub-file 0 of `a/0/1/2` under `extract/data/`, read for
  the 427-record length, the `+0x14` encounter lane and the `+0x16` bijection.
