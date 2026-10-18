# The field: what wiring it takes

The port reaches the title menu only. Its compiled overlay set is the title set
(ov009, ov010, ov011, ov013, ov017, ov088, ov179, ov228; `cmake/overlays.cmake`,
`docs/title-closure.md`). The field system, the script VM, ov021, ov020 and
ov072 are absent. Everything the port is missing past the title menu converges
on the field: the field events of the catalogue, co-op, story hacks, and the
game itself. This document says what the field set is, what blocks it, the
order to add it, what it needs beyond compiling, how the work splits, and what
remains to build.

Every claim names the file or the manifest it comes from. Aletheia's metadata
is read from `layout/layout.json` and `layout/matched/**` in the aletheia
checkout. The generator report is from a
`tools/ovlink.py` pass (section 2). The C compile is not run here.

## 1. The field set, and what each overlay provides

The field needs the title set plus four overlays. Three are the field proper;
the fourth is the game's own link talk layer. Each is fully matched in aletheia.

| Overlay | RAM window | Matched functions | What it provides |
|---|---|---|---|
| ov021 | 0x02187F00..0x021EE740 | 4348 (3032 EXACT, 1316 EXACT*) | the field itself |
| ov020 | 0x02183B60..0x02187F00 | 178 (128 EXACT, 50 EXACT*) | the map change and the warp events |
| ov072 | 0x021B95A0..0x021BBD80 | 105 (61 EXACT, 44 EXACT*) | the region (town) map screen |
| ov018 | 0x0217AE20..0x02183B60 | 225 (199 EXACT, 26 EXACT*) | the game's link (comm) talk |

Counts and windows are from `layout/layout.json` and the manifests under
`layout/matched/ov018`, `ov020`, `ov021`, `ov072`. Every function of each
overlay is EXACT or EXACT*, so no field function waits on a match.

**ov021** is the field. Its manifest names run `FIELD` 1651, `FLDMMDL` 671,
`SCRCMD` 525, `FLDEFF` 236, `EVENT` 180, `FIELDMAP` 148, `WEATHER` 107,
`FLD` 91, `SODATEYA` 68, `COMM` 67, `IRCBATTLEMENU` 65, `MAP` 65, `BSUBWAY`
59, `TALKMSGWIN` 53 (counted over the 4348 entries under
`layout/matched/ov021`). It holds the field map (`FIELDMAP_Create`
0x02188030, `FIELDMAP_GetZoneId` 0x02188C9C, `FIELDMAP_GetCamera` 0x02188C58;
`src/matched/ov/ov021_func_02188030.c`), the player (`FIELD_PLAYER_Create`
0x021A2E50, `FIELD_PLAYER_CORE_Create` 0x021A36E4,
`FIELD_PLAYER_GetPos` 0x021A3038), the camera (`FIELD_CAMERA_Create`
0x0218E018, `FIELD_CAMERA_Main` 0x0218E104), the event checks
(`FIELD_EVENT_CHECK_CheckGridEvents` 0x02189BD0, `..._CheckRailEvents`
0x0218A278, `..._Run` 0x0218A4BC), the rails (`FIELD_RAIL_*`, `RAIL_ATTR_*`),
the field effects and flash (`FIELD_FLASH_Apply` 0x021C7E30), the NPC spawn
(`FIELDMAP_AddZoneNpcs` 0x021894E0), and the field script commands
(`SCRCMD_WarpToPosition` 0x021AFCD8 and its siblings, `SCRCMD_MSGWIN_*`,
`SCRCMD_SHOP_StartShop` 0x021B2FD8, `SCRCMD_POKEMON_FLD_*`,
`SCRCMD_IAIGIRI_StartOnPlayer` 0x021C4678, `SODATEYA_*`, `PDW_POSTMAN_*`).
The declared rows of `mod/declared_set.txt` that fall in ov021 are 49 of the
63 overlay rows the generator emits for this set (section 2).

**ov020** is the map change. Its 178 manifest names run `EVENT` 78, `FIELD`
68, `EL` 31. It holds `EVENT_MAPCHANGE_ChangeFieldEvent` 0x021841C0,
`EVENT_MAPCHANGE_RunFixedZoneEvent` 0x02184C78,
`EVENT_MAPCHANGE_RunSituationWarpArrivalEvent` 0x02185694, the warp creators
(`EVENT_MAPCHANGE_CreateWarpEvent` 0x02184D38,
`EVENT_MAPCHANGE_CreateSituationWarpEvent` 0x02184F40,
`EVENT_MAPCHANGE_CreateZoneStartWarpEvent` 0x02184FD4), and the person talk
(`FIELD_WFBC_DATA_LOCAL_CreatePersonTalkEvent` 0x021870C4, `..._ScrCmd...`
0x02187098, `..._PersonTalkEventMain` 0x021871F8) (`docs/mod-coop.md`,
"Warps"; `progress/PAYLOAD_SURVEY.md`, section 13).

**ov072** is the region map screen. Its 105 manifest names are 104 `TOWNMAP_*`
and one `ov072` entry (`TOWNMAP_MapInit` and `TOWNMAP_GRH_FindRecordIndex`
hold the town count 0x3d; `progress/STORY_HACKS.md`, section 3). It is a menu
screen, not the field loop, and it is small.

**ov018** is the co-op talk layer. Its 225 manifest names run `INTRUDE` 112
and `EVENT` 112. It holds `EVENT_COMM_COMMON_CreateTalkStartEvent` 0x0217FB5C,
`..._CreateTalkEvent` 0x0217FC08, `..._CreateTalkedEvent` 0x0217FD34,
`..._TalkEventMain` 0x0217FE6C, `..._CreateSupportMsgEvent` 0x02181D8C,
`..._OfferEventMain` 0x02182150, `..._ItemSaleEventMain` 0x021823A8 and
`..._ItemPurchaseEventMain` 0x0218261C. It is needed for co-op and for the
link trades, not for the single-player field.

**ov010 is already in the title set**, and the field leans on it. The FLDMMDL
entity system (`FLDMMDL_CreateSys` 0x0216CC58, `FLDMMDL_AddMmdl` 0x0216CE60,
`FLDMMDL_AddMmdlCore` 0x0216CDC4, `FLDMMDL_DeleteMmdl` 0x0216CF2C,
`FLDMMDL_SysSearchObjId` 0x0216DE94, `FLDMMDL_GetPos` 0x0216D88C,
`FLDMMDL_SetPos` 0x0216D89C), the script VM (`SCRIPT_SYS_CreateVm` 0x02158BFC,
`SCRIPT_SYS_Main` 0x0215882C) and the zone setup
(`FIELD_MAP_CHANGE_SetupZone` 0x021643E8,
`FIELD_MAP_CHANGE_ApplyZoneHeader` 0x021643AC) live in ov010
(`docs/mod-coop.md`, sections 1 and 2; `progress/STORY_HACKS.md`, section 1).
Today ov021 calls them through trampolines; with ov010 compiled they are
direct calls.

### The ov021 and ov072 window overlap

ov021's code range [0x02187F00, 0x021DE700) contains ov072's whole window
[0x021B95A0, 0x021BBD80). 102 ov021 manifest functions sit inside it, among
them `FIELD_RAIL_IsLocationInLine` 0x021B95C0 and `RAIL_ATTR_Delete`
0x021BB0A4. The two overlays therefore cannot be resident at once, which is
the DS behaviour: overlays sharing RAM are alternatives. The port already
enforces it. `FS_LoadOverlay` clears the loaded flag of every overlay whose
window overlaps the one loading (`sdk/overlay.c`, lines 293 to 304), and
`bw_ovl_resident_at` returns an overlay only when it is loaded
(`sdk/overlay_image.c`, lines 272 to 302). The plan must keep that rule and
must not assume the field and the town map run together.

## 2. What blocks it: the generator's report

The set is planned the way the title set is, with `tools/ovlink.py`. A pass
with the four field overlays, an empty link source list and `--arm9-stubs`
produces:

```
ovlink: 4 overlays, 4856 translation units, 800 trampolines, 1305 ARM9 names
        (1305 not in the link), 63 declared slots, 0 declared rows not in the link
ov018  ... units 225  excluded 0  defined 226  stubs 7  data CONST=1 DS=86 OBJECT=2  relocs CODE=103  slots 8
ov020  ... units 178  excluded 0  defined 178  stubs 0  data CONST=3 DS=13  relocs CODE=35 DATA=4  slots 6
ov021  ... units 4348 excluded 0  defined 4378 stubs 9  data CONST=37 DS=582 OBJECT=2 relocs CODE=1998 DATA=1322  implicit decls 7  private copies 2  slots 49
ov072  ... units 105  excluded 0  defined 106  stubs 0  data CONST=1 DS=8  relocs CODE=11  slots 0
```

The exact command is:

```
python tools/ovlink.py --aletheia-root <bw_decomp> \
    --overlays "18;20;21;72" \
    --link-sources <the eidos library source list> \
    --out-dir <build>/ovl/field --set-name field --arm9-stubs --summary
```

`ovl_report.txt` in the output directory carries every decision. The C
compile that follows is `cmake --build <build> --target eidos_boot` with
`-DEIDOS_BOOT=ON -DEIDOS_OVERLAYS="9;10;11;13;17;18;20;21;72;88;179;228"`.
The generator pass is not that compile; it produces the plan.

**Excluded files: none.** ov018, ov020, ov021 and ov072 report `excluded 0`.
No field source is an mwcc `asm` body, and no field source declares and uses a
`static` function another file defines. So no field overlay needs a
`stubs/ov/reimpl_` file or an `sdk/asm/ov/` replacement to link
(`docs/overlays.md`, "What the build lacks: stubs"). This is the cleanest part
of the set: the field sources compile as written.

**Unmatched functions (stubs).** ov020 and ov072 have none. ov021 has 9, ov018
has 7. A stub is a function known in the window that no compiled source
defines; it panics when reached. The addresses, with their manifest neighbours:

- ov021 0x021882A0, 0x02188318, 0x021883CC: between `FIELDMAP_SeqCreateSubsystems`
  0x021881D8 and `FIELDMAP_SeqStart` 0x021885E8.
- ov021 0x021B62E0: between `SCRCMD_POKEMON_FLD_GetMonTypeForTm` 0x021B62C8 and
  `SCRCMD_POKEMON_FLD_StartSelectEvent` 0x021B631C.
- ov021 0x021B639C, 0x021B63D0: between `SCRCMD_POKEMON_FLD_SelectMonMove`
  0x021B636C and `SCRCMD_POKEMON_FLD_StartEggHatch` 0x021B63D4.
- ov021 0x021B6448: between `SCRCMD_POKEMON_FLD_StartEggHatch` and
  `SCRCMD_POKEMON_FLD_CheckTradeMon` 0x021B647C.
- ov021 0x021B6498, 0x021B6520: between `SCRCMD_POKEMON_FLD_CheckTradeMon` and
  `SCRCMD_POKEMON_FLD_StartTradeEvent` 0x021B654C.
- ov018 0x0217AF38: between `INTRUDE_FIELD_Update` 0x0217AE20 and
  `INTRUDE_FIELD_FindRemotePlayerAhead` 0x0217AFC4.
- ov018 0x0217B80C: between `INTRUDE_FIELD_EventExitConfirm` 0x0217B600 and
  `INTRUDE_FIELD_CreateNoticeEvent` 0x0217B810.
- ov018 0x0217B83C: between `INTRUDE_FIELD_EventNotice` 0x0217B82C and
  `INTRUDE_FIELD_IsAtAreaEdge` 0x0217B930.
- ov018 0x0217F894: between `EVENT_COMM_COMMON_ExitSyncEventMain` 0x0217F6A0 and
  `EVENT_COMM_COMMON_CmdGetObjCode` 0x0217F8A0.
- ov018 0x0217F8C0, 0x0217F8EC, 0x0217F928: among `EVENT_COMM_COMMON_CmdGetObjCode`
  0x0217F8A0, `..._CmdCheckMissionInfoPending` 0x0217F8D0,
  `..._CmdCallMissionInfoEvent` 0x0217F914 and `..._CreateMissionInfoEvent`
  0x0217F938.

These are unlabelled or table-only addresses aletheia's listing knows and the
manifests do not carry as functions. They are normal: the title set stubs 349
functions in ov009 and 38 in ov010 and still reaches the title menu
(`build32/ovl/boot/ovl_report.txt`). Each becomes a port stand-in in
`stubs/ov/ovNNN_<name>.c` only when a field path reaches it
(`docs/overlays.md`, "What the build lacks: stubs").

**Unresolved names: none.** The report lists no `unresolved` line. Every name
a field source uses has an address from its `symbols:` block, its spelling or
`layout/symbols_sdk.txt`.

**Data the generator cannot place as a macro.** Four names are used as a
member, tag, local or in a static initialiser, so each becomes a host object
filled at load (`docs/overlays.md`, "Data"): ov021 `d_021D872C` at 0x021D872C
and `gFieldGimmickInitFuncs` at 0x021D9CBC; ov018 `d_0218374C` at 0x0218374C
and `d_021837C4` at 0x021837C4. Two names are defined twice inside an overlay
and get a private copy (`docs/overlays.md`, "Names"): ov021
`FIELD_TASK_UpdateWait` in `src/matched/ov/ov021_func_021C77FC.c` (owned by
`src/matched/ov/ov021_func_021C77D8.c`) and `FLD_VREQ_ApplyBlendAlphaEv` in
`src/matched/ov/ov021_func_021D335C.c` (owned by
`src/matched/ov/ov021_func_021D331C.c`). The generator handles all six; none is
a blocker.

**The ARM9 closure.** With an empty link the four overlays name 1305 ARM9
functions, all missing. The title set names 1320, 23 missing
(`build32/ovl/boot/ovl_arm9_needs.txt`). 772 names are used by the field set
and not by the title set. These are the field's ARM9 callees: `ZONEDATA_*`,
`POKEPARTY_*`, `GAME_DATA_*`, `MAP_ATTR_*`, `EVENTDATA_SYSTEM_*` and the rest.
The boot's closure must carry them. `eidos_overlays_seeds` plans the matched
ARM9 files that define what the overlays call and the link lacks
(`cmake/overlays.cmake`, `eidos_overlays_seeds`); for this set the pass writes
883 seed paths with an empty link (`ovl_arm9_seeds.txt`). With
`EIDOS_OVERLAYS_ARM9_STUBS` on (the default) the missing names are panicking
stubs; off, they are link errors. The 772 is the concrete growth to absorb
before the field runs.

**Declared slots.** The generator emits 63 slots, one per declared overlay row
of the set, and reports 0 declared rows not in the link
(`docs/mod-interface.md`, section 2; `mod/declared_set.txt`). The field
addresses are therefore all reachable through the slot machinery once the
mod loader exists.

**The C compile itself is not run here.** The generator report is not the
compiler's verdict. The remaining compile risk is the one the title set met:
an argument mwcc passed in a register, which needs a `stubs/ov/reimpl_` file
(`docs/overlays.md`, "What the build lacks: stubs"; `docs/boot.md`,
"Arguments in registers"). The title set needed six such files, all for ov088
(`stubs/ov/reimpl_ov088_*`). The command above and `cmake --build` would name
any field file that needs one; the generator cannot.

## 3. The order to add them, smallest first

Each step adds one overlay to `EIDOS_OVERLAYS` and is verifiable on its own.

1. **ov072 (105 units, 0 stubs).** The region map. It is small and self
   contained. Test: load it from the player's extract the way
   `tests/test_ovlink_title.c` does for the title set, and check its tables;
   then open the map screen from the title menu and confirm the town count and
   rows (`progress/STORY_HACKS.md`, section 3).
2. **ov020 (178 units, 0 stubs).** The map change. Test: the boot's frame loop
   with a scripted map change; the `map.warp` and `route.change` events of
   `docs/mod-events.md`, section 1.
3. **ov018 (225 units, 7 stubs).** The game's link (comm) talk. Test: the
   overlay loads and its tables check; a comm talk needs the game's link
   features, which are separate work.
4. **ov021 (4348 units, 9 stubs).** The field. Test: the field frame loop past
   the title menu, then the field events of `docs/mod-events.md`, section 1
   (67 entries; `progress/PAYLOAD_SURVEY.md`, section 13). This is the largest step
   and the one whose stubs and host objects are new.

The order is by size, so each overlay links before the next is added. ov021
last, because it is 40 times ov072 and its only new generator decisions (the
9 stubs, the 2 host objects, the 2 private copies) are isolated there. A
combined field set is the sum; the steps only order the verification.

What each step is tested against:

- the boot's frame loop: `test_boot_run` asserts the title menu at frame 4800
  and 20000 frames without a stop (`docs/boot.md`, "How far it gets");
- the port's existing tests: `test_ovlink_title` loads every overlay of the
  set from the extract and checks its tables; `test_overlay_refuses` checks
  that an overlay not in the build panics naming the first missing function
  (`CMakeLists.txt`, lines 2250 to 2262, 1415);
- the field events: they fire once the event dispatch and the mod loader are
  built (`docs/mod-implementation.md`, section 6).

## 4. What the field needs beyond compiling

### The mod layer's field parts

The stable events of the field (`route.change`, `map.warp`, `player.step`,
`trainer.spotted`, `npc.talk`, `menu.open`, `field.move`, `encounter.wild` and
the others of `docs/mod-events.md`, section 1) fire from slots in ov010, ov020
and ov021, so they work once those overlays are compiled and the event
dispatch is built. Their payloads read the field context
(`docs/mod-payloads.md`, section 1).

Co-op's partner avatars are field entities the port's avatar service creates
through the game's own entity functions (`FLDMMDL_AddMmdl` 0x0216CE60 into
`FLDMMDL_AddMmdlCore` 0x0216CDC4, `FLDMMDL_SetPos` 0x0216D89C,
`FLDMMDL_DeleteMmdl` 0x0216CF2C; `docs/mod-coop.md`, section 3;
`docs/mod-services.md`, "Field avatars"). The service is port code calling game
functions, so it needs no declared rows. Two questions are read from the field
code once it runs: the entity pool's headroom (`FLDMMDL_CreateSys` 0x0216CC58
sets the size at run time) and whether two copies of the player's sprite can be
resident at once (`docs/mod-coop.md`, section 4).

### The map and zone tables for a roguelike run

`progress/STORY_HACKS.md`, sections 2 and 3, names the tables a run needs and
whether each is data or code: the zone headers (sub-file 0 of `a/0/1/2`, 427
records of 48 bytes; `src/game/arm9/zonedata.c`, `struct ZoneHeader`), the map
matrices (`a/0/0/9`) and models (`a/0/0/8`), the event files (`a/1/2/5`), the
scripts (`a/0/5/7`), the messages (`a/0/0/3`), the encounters (`a/1/2/6`) and
the town map rows (member 0 of `a/0/8/6`). The reader indexes by id with no
bound (`ZONEDATA_GetHeader` 0x02013AD0), so a new zone below 427 needs no code.
The id 427 is the "no zone" sentinel at six sites
(`ZONEDATA_Init` 0x020139B0; `beacon_detail_gra.c`, `field_place_name.c`,
`field_area_zone.c`, `field_gimmick_gate_types.h`, `config.c`), so a zone at or
past 427 needs an override at each. The matrix limit is 30 by 30
(`func_02159C0C`).

## 5. How it splits into parallel workstreams

The split is by file, one writer per file. The build workstream holds
`CMakeLists.txt`, `tools/bootlink.py`, `tools/ovlink.py`, `stubs/`,
`cmake/boot.cmake` and `tests/` today. The field workstreams below take files
those do not cover, or ask the build workstream for the one edit to
`CMakeLists.txt`.

| Workstream | Files | Needs a build |
|---|---|---|
| F1. The field set | `CMakeLists.txt` (`EIDOS_OVERLAYS`), the seeds pass | yes |
| F2. The field port C | `stubs/ov/` | yes |
| F4. The state API | a new `sdk/field_state.c` and its header | yes |
| F6. The entity seam | `sdk/` bindings over `FLDMMDL_*` | yes |
| F7. The map and zone tables | `docs/`, and the 427 sentinel rows | no |
| F8. The field tests | `tests/` (with the build workstream) | yes |

- **F1** edits `CMakeLists.txt` and reruns the seeds pass. It is the only
  workstream that touches the build list, so it serializes with the build
  workstream. It needs a build.
- **F2** writes `stubs/ov/ovNNN_<name>.c` for the 9 ov021 and 7 ov018 stubs
  when a field path reaches one, and `stubs/ov/reimpl_ovNNN_*.c` for any
  register-argument file the C compile finds. It needs a build to know which.
- **F4** writes the field state calls of `docs/mod-events.md`, section 4, and
  tests them against a running field. It needs a build.
- **F6** writes the field avatar service (`docs/mod-services.md`, "Field
  avatars") and settles the two questions of section 4. It needs a build, and
  it depends on F1.
- **F7** names the map and zone tables and the 427 sentinel overrides
  (`progress/STORY_HACKS.md`, still-to-build items 2 to 5). The table names
  are documentation and rows; no build. The override rows go in `mod/declared_set.txt`.
- **F8** adds the extract-load test and the field-loop test. It shares
  `tests/` with the build workstream, so it is the build workstream's work or
  a handoff to it.

F1, F2, F4, F6 and F8 need a build. F7 does not.

## Still to build

1. **Compile the field overlays into the boot.** `EIDOS_OVERLAYS` adds
   `18;20;21;72` to the title set. Evidence: the generator report of section 2
   (0 excluded files, 63 slots, 0 declared rows not in the link);
   `CMakeLists.txt`, the `EIDOS_OVERLAYS` comment.
2. **Grow the boot's ARM9 closure for the field.** 772 ARM9 names the field
   set uses and the title set does not join the boot's closure through
   `eidos_overlays_seeds`. Evidence: `ovl_arm9_needs.txt` for both sets;
   `cmake/overlays.cmake`, `eidos_overlays_seeds`.
3. **Stand in the 9 ov021 and 7 ov018 unmatched functions if a field path
   reaches one.** Each is a `stubs/ov/ovNNN_<name>.c` written from the
   listing. Evidence: the stub addresses of section 2; `docs/overlays.md`,
   "What the build lacks: stubs".
4. **Keep the ov021 and ov072 window overlap exclusive.** The loader already
   clears an overlapping window on load; the field must not assume the town
   map and the field run together. Evidence: `layout/layout.json`; 102 ov021
   functions inside ov072's window; `sdk/overlay.c`, lines 293 to 304.
5. **The field state calls.** The values of `docs/mod-events.md`, section 4,
   that live in the field (position, camera, encounter table), tested against
   a running field.
6. **The field avatar service.** `docs/mod-services.md`, "Field avatars", with
   the pool headroom and the sprite question of `docs/mod-coop.md`, section 4.
7. **Name the map and zone tables and override the 427 sentinel.** The tables
   of `progress/STORY_HACKS.md`, section 3, become replaceable, and the six
   sentinel sites get an override for a zone at or past 427. Evidence:
   `ZONEDATA_GetHeader` 0x02013AD0; `ZONEDATA_Init` 0x020139B0.
8. **Test the field.** Load each field overlay from the player's extract
    (`test_ovlink_title`'s shape) and run the field loop past the title menu,
    then the field events. Evidence: `CMakeLists.txt`, lines 2250 to 2262;
    `docs/boot.md`, "How far it gets".

## Sources

- `docs/overlays.md`, `cmake/overlays.cmake`, `tools/ovlink.py`,
  `sdk/overlay.c`, `sdk/overlay_image.c`, `docs/status.md`,
  `docs/title-closure.md`, `docs/boot.md`.
- `docs/mod-coop.md`, `docs/mod-events.md`, `docs/mod-payloads.md`,
  `docs/mod-services.md`, `docs/modding.md`,
  `progress/STORY_HACKS.md`, `progress/SEAM_COST.md`.
- `mod/declared_set.txt`, `CMakeLists.txt`.
- Aletheia's `layout/layout.json` and `layout/matched/ov018`, `ov020`, `ov021`,
  `ov072`.
- The generator report and the `ovl_arm9_needs.txt`, `ovl_arm9_seeds.txt` and
  `ovNNN_link.c` files of a `tools/ovlink.py` pass for `18;20;21;72`, and the
  title set's `build32/ovl/boot/ovl_report.txt` and `ovl_arm9_needs.txt`.
