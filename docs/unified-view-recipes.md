# Unified view: scene recipes (detection, state, input)

This document is the code-derived input for the recipes of the one-screen layout (`engine/ui/ui.h`). For each scene it says how the port can tell the scene is active, where the scene's state lives and which fields a recipe reads, and which bottom-screen rectangle the game tests for each control, so that a click can become a tap at the centre of that rectangle. It extends `docs/unified-view-inventory.md` with run-time facts. Where the two disagree, this document is the newer reading of the code; the disagreements are listed in section 0.7.

Evidence is written `ovNNN/file.c:LINE` for C in the aletheia checkout (`src/game`), `arm9/file.c:LINE` for ARM9 code, or an overlay address plus the table name for ROM data. Tables that are not in matched C were read from the decompressed overlay images of the aletheia extract (`extract/overlays_dec/y9_NNNN.bin`, or `extract/overlays/y9_NNNN.bin` for overlays that are not compressed) with a small dump script; the DS address is quoted so the read can be repeated. "Unverified" means the code did not settle the point; it is not a guess.

All ROM tables and texts are from the Black dump that aletheia extracted (`extract/`); the White tables were not compared (an overlay's table addresses can differ in White). Text strings quoted in this document (menu labels, option names) were decoded from the message archive `a/0/0/2` of the Black ROM with `tools/assets/text.py`; the bank number is the member number of that archive.

## Contents

0. Toolbox: how any scene is detected and how input is injected
1. Field (normal play, "nothing open")
2. X menu (start menu, `ov021` field menu)
3. Bag (`ov070`)
4. Pokedex (`ov212`, `ov211`, `ov209`, `ov208`, `ov206`, `ov210`)
5. Party list (`ov091`)
6. Pokemon summary (`ov131`)
7. Battle (`ov093`/`ov094`/`ov095` and the battle bag `ov197`, battle party `ov198`)
8. Options (`ov068`)
9. Trainer card (`ov111`)
10. Save / report (`ov034` and the report event)
11. PC boxes (`ov170`)
12. Poke Mart
13. Name entry (`ov194`)
14. Xtransceiver (`ov172`)
15. Title screen and title menu (`ov088`)
16. C-Gear (`ov029`, `ov030`, `ov031`)
17. Overworld player sprite
18. What is solid and what is unverified

## Summary table: the cheapest reliable test per scene

"G" is `_GameSystemGlobal.gameSystem`, "gsp" is `G->procSys->current` (the proc on top of the in-game stack), "rootp" is `g_ProcSys->current` (the global stack used by the boot, title and intro). Details and caveats are in each section.

| Scene | Test | Section |
|---|---|---|
| Field idle | `G != 0`, `G->fieldMap != 0`, `fieldMap->mainState == 1`, `G->event == 0`, `subscreen->state == 0`, `subscreen->mode` is 0 or 5 | 1 |
| Field with a script, dialogue or other event | same but `G->event != 0` | 1 |
| X menu | field map up, `subscreen->mode == 1`, `((FieldMenu*)subscreen->cgear)->state == 3` for "accepting input" | 2 |
| Bag | `gsp->overlayId == 70` | 3 |
| Pokedex | `gsp->overlayId == 212`, child screen from its private stack | 4 |
| Party list | `gsp->overlayId == 91` | 5 |
| Summary | `gsp->overlayId == 131` | 6 |
| Battle | a proc with `overlayId == 93` on the stack; sub-state from `BtlvInput.screen` | 7 |
| Options | `gsp->overlayId == 68` | 8 |
| Trainer card | `bw_overlay_is_loaded(111)` (the proc on the stack is ov010 code) | 9 |
| Save / report | field map up, `subscreen->mode == 7` | 10 |
| PC boxes | `gsp->overlayId == 170` | 11 |
| Poke Mart | not a proc; script wait function in ov021 | 12 |
| Name entry | `gsp->overlayId == 194` | 13 |
| Xtransceiver | `gsp->overlayId == 172` | 14 |
| Title screen | `rootp->overlayId == 88` and `rootp->data == 0x021BD574` | 15 |
| Title menu | `rootp->overlayId == 88` and `rootp->data == 0x021BD66C` | 15 |
| C-Gear | field map up, `subscreen->mode` 0 or 8 | 16 |

## 0. Toolbox

### 0.1 Overlay presence: `bw_overlay_is_loaded(id)`

`sdk/overlay.h` declares `int bw_overlay_is_loaded(u32 id)`: 1 if the overlay is resident "(loaded and not since unloaded or overlaid)" (`sdk/overlay.h`, end of the file before `BwOverlayInfo`). The flag is `g_loaded[id]` (`sdk/overlay.c:55,187-190`), set at `sdk/overlay.c:310` and cleared by `FS_EndOverlay` (`sdk/overlay.c:353`, reached from `FS_UnloadOverlay` at `sdk/overlay.c:380`). The game loads the overlay of a proc when the proc starts (`PROCSYS_StepProc` seq 0 calls `GF_OVERLAY_Load`, `arm9/procsys.c:324-346`) and unloads it when the proc ends (seq 3 calls `GF_OVERLAY_Unload`). `GF_OVERLAY_Unload` finds the slot and calls `FS_UnloadOverlay` (`arm9/gf_overlay.c:173-190`, `:143-147`). So for an app that is one proc, `bw_overlay_is_loaded(id)` is true from the first frame of the proc (before its init has built the work struct) until its end callback finished.

Caveats:

* True from seq 0, so the work struct may not exist yet. Combine with the proc test (0.2) and check `proc->seq >= 2` (main) before reading the work.
* The flag is also cleared when another overlay is loaded over its RAM range (`sdk/overlay.c:299-302`), as on the DS, so an overlay that shares addresses with a later one reads as unloaded.
* Some overlays are also loaded by code that wants only their data or helper functions. Examples: ov093 is loaded outside battle by `ov006/event_wificlub.c:505` and `ov008/event_battle_rec_save.c:303` (found by the battle reader); ov067 (shared UI widgets) is loaded by the options and other apps (`ov068/option_menu.c:450`); ov230 is loaded briefly during the game system init (`arm9/gamesystem.c:447-460`). A test on a shared-widget overlay is not a scene test.
* Nested overlays: ov206 is loaded only while the Pokedex detail info page is up (section 4).

### 0.2 The proc stacks

There are two proc stacks.

1. The global stack, `g_ProcSys` (a `ProcSys*` data object at DS `0x02146990`, `arm9/procsys.c:16,53,60`). It runs the boot, logos, opening, title, title menu, new-game intro and the GameSystem proc itself. `OVERLAY_LAUNCH_Driver` pushes the logo (ov088 data `0x021BD8E8`), the opening (ov179 `0x021B68D8`) and the title (ov088 `0x021BD574`) with `PROCSYS_PushGlobal` (`arm9/app_power.c:412-442`); the title menu is a `PROCSYS_ReplaceGlobal` to `0x021BD66C` (`ov088/startmenu.c:1165`).
2. The in-game stack, `gameSystem->procSys`, a second `ProcSys` created by `GAMESYSTEM_Init` (`arm9/gamesystem.c:586`). The field map is a proc on it (`GAMESYSTEM_CallFieldProc`, `arm9/gamesystem.c:805-808`: `GAMESYSTEM_CallProc(gs, ov021, &FieldMapProcData, gs)`), and so are the X-menu apps, the battle and most other apps (`GAMESYSTEM_CallProc` -> `PROCSYS_Push`, `arm9/gamesystem.c:771-773`).

Layouts (DS, 32-bit; the eidos build is 32-bit i686 per the project notes, so the offsets hold, but compile recipes against the real headers):

| Struct | Fields | Evidence |
|---|---|---|
| `Proc` (0x20) | `data` 0x00 (a `ProcData*`: init, main, end function pointers), `overlayId` 0x04 (-1 means none), `seq` 0x08 (0 load overlay, 1 init, 2 main, 3 end), `subSeq` 0x0C, `procWork` 0x10 (the argument block given by the caller), `work` 0x14 (the buffer the proc allocated with `PROCSYS_AllocProcWork`), `prev` 0x18, `next` 0x1C | `arm9/procsys.h:11-20`, `arm9/procsys.c:PROCSYS_NewProc` |
| `ProcSys` (0x20) | `heapId` 0x00, `current` 0x08 (top of the stack), `pushPending` 0x0C, `replacePending` 0x10, `nextOverlayId` 0x14, `nextData` 0x18, `nextProcWork` 0x1C | `arm9/procsys.h:23-31` |
| `GameSystem` (0x34) | `heapId` 0x00, `initParam` 0x04, `procSys` 0x08, `procStatus` 0x0C, `eventCreator` 0x10, `eventCreatorArg` 0x14, `event` 0x18, `gameData` 0x1C, `fieldMap` 0x20, `gameComm` 0x24, `issSystem` 0x2C, `fieldMapState` 0x30, `eventActive` 0x31, `commBlocked` 0x32 | `arm9/ica_anime_shared_types.h:25-41`; the 0x34 size is `GAMESYSTEM_GetWorkSize`, `arm9/gamesystem.c:563-566` |
| `GameEvent` (0x14) | `parent` 0x00, `func` 0x04, `seq` 0x08, `work` 0x0C, `gameSystem` 0x10 | `arm9/ica_anime_shared_types.h:47-53` |

How to find the GameSystem: `GAMESYSTEM_ProcInit` stores it in `_GameSystemGlobal.gameSystem` (`arm9/gamesystem.c:467`; the struct is `arm9/gamesystem.c:101-105`, DS address `0x02146248`, field `gameSystem` at +0). `GAMESYSTEM_ProcEnd` frees the work but does not clear the pointer, and then resets the machine (`arm9/gamesystem.c:512-523`), so the pointer is valid whenever the GameSystem proc is alive. To know that the game system exists, test the global stack's current proc or its children: the GameSystem proc is created by `GAMESYSTEM_ProcInit` (its ProcData is not named in the decomp C; unverified which `ProcData` address, see below), so the practical test is `_GameSystemGlobal.gameSystem != 0` combined with the global stack not being at the title (`rootp->overlayId != 88`). How the port exposes `_GameSystemGlobal` to `engine/ui` (a C symbol of the game build, or a host accessor) is not settled by the decomp: unverified.

Procedure for "which app is up" (in-game):

```
G = _GameSystemGlobal.gameSystem
gsp = G->procSys->current            // may be 0 between apps
if gsp == 0: no app proc (rare, transition frame)
overlay = gsp->overlayId             // 21 = field map; 70 bag; 91 party; 131 summary; ...
work = gsp->work                     // the app's work struct once gsp->seq >= 2
```

When the field map is the current proc `gsp->overlayId == 21` and `gsp->data` is `FieldMapProcData` (`arm9/gamesystem.c:805-808`, data at ov021 `0x021D359C`, init/main/end are `FIELDMAP_ProcInit/Main/Exit`, `ov021/fieldmap.c:809-831`). `Proc.work` of the field proc is a `FieldMapProcWork` whose `fieldMap` member is the `FieldMap*` (`ov021/fieldmap.c:809-815`); `G->fieldMap` (+0x20) holds the same pointer once the map exists and is set to 0 by `FIELDMAP_ProcExit` (`ov021/fieldmap.c:817-821`).

### 0.3 Apps replace the field; events run behind them

The X-menu apps do not run on top of a live field. The event that starts them (`EVENT_SHORTCUT_MENU_ProcEventMain`, `ov010/event_shortcut_menu.c:876-1004`) first closes the field (`FIELD_STATUS_CreateFieldCloseEvent`, seq 6-7), then calls `GAMESYSTEM_CallProc` (seq 11, line 961-963), waits for `GAMESYSTEM_GetProcStatus == 0` (seq 12, line 966), then reopens the field (`FIELD_STATUS_CreateFieldOpenEvent`, seq 8-10). So while a bag, party list, Pokedex, trainer card, options or summary is up:

* `G->fieldMap == 0` (set by `FIELDMAP_ProcExit`, `ov021/fieldmap.c:817-821`),
* `gsp` is the app proc,
* `G->event != 0` (the shortcut proc event stays alive).

So "event == 0" is not a test for "no app"; use `gsp->overlayId`.

### 0.4 Field map and field sub-screen

* `FieldMap` (0x148+): `heapId` 0x000, `gameSystem` 0x004, `gameData` 0x008, `subscreen` 0x024 (`FieldSubscreen*`), `msgBg` 0x028, `infoWindow` 0x02C, `placeName` 0x030, `player` 0x08C, `mainState` 0x0C0 (0 starting, 1 running, 2 closing), `zoneId` 0x0D0, `subscreenBusy` 0x144 (`ov021/fieldmap_types.h:119-182`). `FIELDMAP_IsReady` is `mainState == 1` (`ov021/fieldmap.c:1218-1224`).
* `FieldSubscreen` (0x2C): `mode` 0x00, `nextMode` 0x04, `state` 0x08 (0 run, 1 fade out, 2 fade-out wait, 3 halt, 4 fade in, 5 fade-in wait), `heapId` 0x0C, `panelResult` 0x10, `fieldMap` 0x14, `callback` 0x18, `callbackArg` 0x1C, `cgear` 0x20 (the handle of the loaded panel: `CGearWork*` in modes 0 and 8, `FieldMenu*` in mode 1, `ReportWork*` in mode 7) (`ov021/field_subscreen_types.h`, `ov021/field_subscreen.c:332-345`).
* The mode table `d_021D872C` (28-byte rows `init, update, updateNoTransition, checkRequest, exit, ...`, `ov021/field_subscreen.c:322`, ROM data ov021 `0x021D872C`). Row to function, matched by address in `layout/function_addrs.tsv`: 0 `InitCGear`, 1 `InitMenu`, 2 `InitUnion`, 3 `InitIntrude`, 4 `InitBeaconView`, 5 `InitNoGear`, 6 `InitDowsing`, 7 `InitReport`, 8 `InitCGearWithCallback`, 9 `InitPowerOnoff`. The constants `FIELD_SUBSCREEN_MODE_CGEAR 0`, `MODE_MENU 1`, `MODE_NO_CGEAR 5`, `MODE_CGEAR_CALLBACK 8` are at `ov021/field_subscreen.c:129-131,146-147`.
* Find it: `G->fieldMap->subscreen` (`FIELDMAP_GetSubscreen`, `ov021/fieldmap.c:1343`).
* `FIELD_SUBSCREEN_IsIdle(sub)` is `sub->state == 0` (`ov021/field_subscreen.c:417-423`). A request for a new mode sets `nextMode`, then state 1 (`ov021/field_subscreen.c:RequestMode`); the old mode exits and the new one inits after a fade, so during a change the `mode` still shows the old mode until state 2 finishes.

### 0.5 Touch: how the game reads it, how a tap must look

* A hit table is an array of 4-byte entries `top, bottom, left, right` (u8). A first byte of `0xFF` ends the table, `0xFE` makes the entry a circle `(code, x, y, r)`, `0xFD` skips the entry. The rectangle test is inclusive on all four edges: `x - left <= right - left && y - top <= bottom - top`, unsigned (`arm9/touchpanel.c:45-49`, `:315-352`). A rectangle with `right < left` (for example a right edge written as 0 for a 256 wide strip) therefore matches every x at or above `left`.
* `TOUCHPANEL_HitTrg` and `TOUCHPANEL_GetPointTrg` fire on the first frame the stylus is down (a press). `TOUCHPANEL_HitCont` and `GetPointCont` fire every frame it is held (`arm9/touchpanel.c:354-444`). So a tap from the recipe should be held 2 or more frames (several apps also read the held point, for example the bag list and Pokedex stepping), and must start while the game is accepting input.
* `BUTTON_MAN` (the shared touch-button manager used by the bag, C-Gear and others): it calls back `cb(index, flag, arg)` where `flag` 0 is the press, 1 released inside, 2 held, 3 released outside (`arm9/button_man.c:UpdateSlots`, `SlotHandlerIdle`, `SlotHandlerPressed`, `ov070/itemmenu.c:2201`). Apps that act on `flag == 0` act on the press frame.
* `APP_TASKMENU` (the pop-up menu used for yes/no and context menus in many apps) builds a rectangle per entry from its template at run time: `top = posY*8 + 8*height*i`, `bottom = top + 8*height`, `left = posX*8`, `right = (posX+width)*8 - 1`; with `posType != 0` the template position is the bottom-right corner (`posX = charPosX - width`, `posY = charPosY - height*itemNum`). A tap decides the entry at once (`arm9/app_taskmenu.c:656-696`, template `arm9/app_taskmenu.h:19-29`). The rectangles are in bottom-screen touch coordinates even when the menu is drawn on the top screen (the touch panel is always the bottom screen).
* All rectangles in this document are bottom-screen DS pixels, x 0-255, y 0-191.

### 0.6 Keys

DS key bits as the game uses them: A 0x001, B 0x002, SELECT 0x004, START 0x008, RIGHT 0x010, LEFT 0x020, UP 0x040, DOWN 0x080, R 0x100, L 0x200, X 0x400, Y 0x800 (`ov021/field_menu.c:231-237`, `ov070/itemmenu_types.h:ITEMMENU_KEY_*`, `ov021/field_event_check.c:1037-1039`). The game also has an input mode flag: `UI_SetInputMode(1)` after a touch (hide the key cursor), `UI_SetInputMode(0)` after a key (show it) (`ov021/field_menu.c:1006-1032`, `ov068/option_menu.c:1135-1139`). A key press while the mode is touch mostly only brings the cursor back and does nothing else (bag: `ov070/itemmenu.c:1529-1542`, Battle: `ov094/btlv_input.c:3399-3402`), so a recipe that presses a button should press it twice, or use taps.

### 0.7 Corrections to `docs/unified-view-inventory.md`

| Inventory statement | What the code says | Evidence |
|---|---|---|
| Field-menu layout 0 order is "Pokedex, Pokemon", flag 0x961 adds the Pokedex, 0x962 the Pokemon (inventory section 0, "Field-menu order") | Button 1 uses message 2 = "POKeMON" and goes to proc kind 0 (the party list); button 2 uses message 1 = "POKeDEX" and goes to kind 1 (ov212). In layout 0 (`1,2,3,4,5,6,0`) the first slot is the Pokemon list. Flag 0x961 therefore unlocks the Pokemon button, 0x962 the Pokedex button | `ov021/field_menu.c:815`, ROM `gFieldMenuMsgIds` ov021 `0x021D8B34` = u16 {7,2,1,3,4,5,6,9}; message bank 34: 1 POKeDEX, 2 POKeMON; `sProcKindOfButton` ov010 `0x021712AC` = {-1,0,1,2,3,4,5,-1}; proc table ov010 `0x021712CC` |
| Trainer card edit sub-screen is ov110 | ov110 is the easy-chat word input (`PMS_INPUT`), used to edit the greeting sentence | `ov010/trcard_sys.c:StateStartEditProc`; section 9 |
| Trainer card is one proc | The proc on the game stack is `TRCARD_SYS` (ov010 code); ov111 screens run on a private `ProcSys` | section 9 |
| Pokedex screens are pushed on the game stack | They run on a private `ProcSys` held in `ZukanWork` | section 4 |
| Save is "kind 5" in one place and "kind 4" in another | Kind 4 is the report event; kind 5 is Options | `ov010/event_shortcut_menu.c` table; section 10 |

The remaining pieces of the inventory (screen assignments, art layers) were not rechecked here.

---

### 0.8 More shared facts found while reading the apps

C paths are relative to `C:/aletheia-w20/src/game` (`ovNNN/file.c:LINE`). "ROM data" means bytes read from the decompressed overlay image (`extract/overlays_dec/y9_NNNN.bin`) at the DS address given; the C names for those tables are `d_021XXXXX` or `g...`.

Facts that every scene below relies on, all from the decomp:

| Fact | Evidence |
|---|---|
| `Proc` fields (DS layout): `data` 0x00, `overlayId` 0x04, `seq` 0x08, `subSeq` 0x0C, `procWork` 0x10 (the argument block), `work` 0x14 (the buffer from `PROCSYS_AllocProcWork`), `prev` 0x18, `next` 0x1C | `arm9/procsys.h:11-20` |
| `ProcSys`: `current` is at 0x08; `PROCSYS_Main` steps only `current` | `arm9/procsys.h:23-31`, `arm9/procsys.c:178-205` |
| The apps of the X menu run on `gameSystem->procSys`, one proc at a time. The event first closes the field (`FIELD_STATUS_CreateFieldCloseEvent`), then calls `GAMESYSTEM_CallProc`; the field reopens when the app proc ends. So while an app runs, `gameSystem->fieldMap` is NULL and `procSys->current` is the app | `ov010/event_shortcut_menu.c:927-933` (seq 6,7,11), `:961-963`, `:937-941` (seq 8,9) |
| The shortcut event stays alive behind the app (`GAME_EVENT` non-NULL), so "event == NULL" is false during every app | `ov010/event_shortcut_menu.c:872-1004` |
| A proc unloads its overlay when it ends (`GF_OVERLAY_Unload` in seq 3), so `bw_overlay_is_loaded(id)` is a good "this app is up" test; it is true from seq 0 (the load) on | `arm9/procsys.c:322-345` |
| Touch hit tables are `TouchHitTable` entries of 4 bytes `top, bottom, left, right` (inclusive, `x - left <= right - left` in unsigned arithmetic); first byte `0xFF` ends a table, `0xFE` marks a circle, `0xFD` skips | `arm9/touchpanel_shared_types.h:23-36`, `arm9/touchpanel.c:45-49,327-352` |
| `TOUCHPANEL_HitTrg` fires on the first frame the stylus is down (a press). A tap held 2 or more frames is safe. A rectangle whose `right` byte overflowed to 0 (a 256 wide edge) still hits for x at or above `left` | `arm9/touchpanel.c:327-333` |
| `BUTTON_MAN` callbacks get `(index, flag, arg)`; flag 0 is the press, 2 held, 1 released inside, 3 released outside | `arm9/button_man.c:UpdateSlots/SlotHandlerIdle/SlotHandlerPressed` |
| The shared `TaskMenu` (context menus) builds one rectangle per entry: `top = (posY*8) + 8*height*i`, `bottom = top + 8*height`, `left = posX*8`, `right = (posX+width)*8 - 1`, with `posType != 0` anchoring bottom right (`posX = charPosX - width`, `posY = charPosY - height*itemNum`). A tap decides at once | `arm9/app_taskmenu.c:655-686`, `arm9/app_taskmenu.h:24-31` |
| `FRAME_LIST` (shared scroll list, ov067): `cursor` (s16) at 0x5C and `scrollPos` (s16) at 0x62 of the object, `items` pointer at 0x28 (array of `{mapIndex, value}`); selected index = `scrollPos + cursor` | `ov067/frame_list_types.h:59-115` (offsets computed from the struct, comments name 0x4B and 0x52 and agree), `ov067/frame_list.c:1376-1379` |
| Naming note: `FrameListHitEntry` declares `left, top, right, bottom` but the ROM bytes and the consumer (`TOUCHPANEL_HitTrg` on a 4-byte copy) are `top, bottom, left, right` | `ov067/frame_list.c:196-205,413`, ROM data check below |
| All offsets are the DS (32-bit) layout. A host build with 64-bit pointers changes them; compile recipes against the real headers rather than hard-coding offsets | n/a |

- Archive ids: id N is the file `a/(N/100)/(N/10 % 10)/(N % 10)` (aletheia `docs/ASSETS.md:7176-7186`). Archive 0x77 = `a/1/1/9`, 0x30 = `a/0/4/8`, 0x31 = `a/0/4/9`.

---

## 1. Field (normal play, "nothing open")

### Detection

The field is the proc `FieldMapProcData` (overlay 21) on the in-game stack, and the field state is split over four objects: the field map, the field sub-screen, the game event and the field message layer.

| Question | Test | Evidence |
|---|---|---|
| Is the game system alive and is the field up? | `G = _GameSystemGlobal.gameSystem`, `G != 0`, `G->fieldMap != 0` | `GAMESYSTEM_IsFieldMapActive`, `arm9/gamesystem.c:893-899`; the field map pointer is set by the field proc and cleared by `FIELDMAP_ProcExit` (`ov021/fieldmap.c:817-821`) |
| Is the map built and running (not loading, not closing)? | `fieldMap->mainState == 1` (`FIELDMAP_IsReady`) | `ov021/fieldmap.c:1218-1224`; `mainState` is set to 1 at the end of the start sequence (`ov021/fieldmap.c:1020-1031`) and to 2 while closing (`ov021/fieldmap_types.h:155`) |
| Is the field proc the one running (not an app on top)? | `G->procSys->current->overlayId == 21` | `arm9/gamesystem.c:805-808`, section 0.2 |
| Is any game event running (script, dialogue, cut scene, menu event, shortcut menu, report)? | `G->event != 0` (`GAME_EVENT_IsRunning`); the cached flag `G->eventActive` (+0x31) is refreshed once per `GAMESYSTEM_Main` | `arm9/game_event.c:232-238`, `arm9/gamesystem.c:671-678,751-754,810-815` |
| Which event is it? | `G->event->func` (+0x04) compared with the event function: `EVENT_FIELD_MENU_Main` (X menu), `EVENT_SHORTCUT_MENU_MainEvent` (Y shortcut menu), `REPORT_EVENT_Main` through `EVENT_FIELD_PROCLINK_FreeEventMain` (save), script events (`SCRIPT_SYS_*`), and so on. Events nest: `GameEvent.parent` (+0x00) is the caller | `arm9/ica_anime_shared_types.h:47-53`; function addresses from `layout/function_addrs.tsv`; the list of event kinds is open-ended, so treat any unknown `func` as "event running" |
| Is the lower-screen panel idle and which one is it? | `sub = fieldMap->subscreen`; `sub->state == 0` (`FIELD_SUBSCREEN_IsIdle`) and `sub->mode` (section 0.4) | `ov021/field_subscreen.c:417-423` |

"Nothing open" is therefore:

```
G != 0 && G->fieldMap != 0 && fieldMap->mainState == 1
  && G->procSys->current->overlayId == 21
  && G->event == 0
  && sub->state == 0 && (sub->mode == 0 || sub->mode == 5)
```

Why `G->event == 0` is the right "idle" test: each frame `GAMESYSTEM_Main` calls `GAME_EVENT_StartByCreator` with the event creator that the field map installed (`FIELD_EVENT_CHECK_RunGrid`, `_RunRail` or `_Run`, chosen by the map type, `ov021/fieldmap.c:1020-1030`); `GAME_EVENT_StartByCreator` returns at once when an event already exists (`arm9/game_event.c:266-279`). The event creator is where the X, Y and A presses of an idle field are turned into events (`ov021/field_event_check.c:762-773,958-974`). Walking is not an event, so a walking player still reads as idle; the player's step state is in `FIELD_PLAYER_GetStepPhase`/`GetStepKind` (`ov021/field_event_check.c:1002-1060`, `InitInput`), and the pad is read directly by the field (`KEY_GetCont`, same function).

Field states a recipe may want to tell apart:

| State | Test |
|---|---|
| Idle, C-Gear panel below | idle test above with `sub->mode == 0` |
| Idle, no C-Gear yet | idle test above with `sub->mode == 5` (`FIELD_SUBSCREEN_ResolveMode` maps mode 0 to 5 when the save has no C-Gear, or in zones 34 and the 0x4B pair, `ov021/field_subscreen.c:427-441`) |
| X menu | section 2 |
| Save / report | section 10 |
| Script, dialogue, cut scene | `G->event != 0` and `sub->mode` is 0, 5 or 8; the lower panel is dimmed ("off") while an event runs (`ov029/c_gear.c`, section 16) |
| Y shortcut menu | `G->event->func` is the shortcut menu event; the lower panel stays C-Gear/No-Gear (section 0.4: Y needs `FIELD_SUBSCREEN_IsIdle`, `ov021/field_event_check.c:762-770`) |
| Transition (wipe, door, zone change) | `fieldMap->mainState != 1`, or `WIPE_SYS_EndCheck() == 0` (the X and Y checks are gated on it: `ov021/field_event_check.c:762,773`); a recipe that only wants "interactive" can use `WIPE_SYS_EndCheck()` as an extra condition |
| Union Room, Entralink, beacon, dowsing | `sub->mode` 2, 3, 4, 6 (table in section 0.4); not covered further |

Unverified: whether the dialogue/message window state is reachable without the event (the field message layer is `fieldMap->msgBg` +0x28, `FieldMsgBg`, with `FIELD_MSGBG_IsPrintQueFinished` and the window getters in `ov021/field_msgbg.h:33-65`; no single "a message is showing" boolean was found). Use `G->event != 0` for "an event owns the field".

### State

What a recipe reads on the field (for the one-screen layout the field itself is drawn by the game; the recipe draws a status bar and the lower-screen replacements):

* Clock and battery: the C-Gear/INFOWIN code reads them (`ov029/c_gear.c:2024 C_GEAR_UpdateClock`, `C_GEAR_GetBatteryFrame`; the X menu strip is `INFOWIN`, `arm9/infowin.c:375-389`). A recipe can read the RTC and battery state the port already provides instead of the sprite state.
* Zone: `fieldMap->zoneId` (+0x0D0) and `fieldMap->location.zoneId` (+0x0D4, current zone; `ov021/fieldmap_types.h:161-163`). Place name text: `ZONEDATA_GetPlaceNameId(zone)` into bank 59 of `a/0/0/2` (as used by the report, `ov034/report.c:482`).
* Player: `fieldMap->player` (+0x08C, `FieldPlayer*`) and its move model (`FIELD_PLAYER_GetMmdl`); direction `FLDMMDL_GetDirDisp`; see section 17 for the sprite.
* Party, badges, money: from `gameData = G->gameData` (+0x1C): `GAME_DATA_GetMyStatus`, `MISC_CountBadges(GAME_DATA_GetMiscSave(gd))`, `POKEPARTY` (party list section).

### Input

The idle field has no touch input: the world is moved by the pad (`KEY_GetCont`, `ov021/field_event_check.c:1002-1060`), the A, X, Y presses produce events, and the stylus is read only by the lower panel (C-Gear/No-Gear) and by menus. To press X from the one-screen layout: `eidos_ui_press(X, frames)` (key bit 0x400). The event creator reads `KEY_GetTrg() & 0x400` on a frame where the player is on a tile boundary (`input->moveState == 3 || == 0`, `ov021/field_event_check.c:1033-1043`), so the press must be held long enough to land on such a frame (a few frames; if the player is mid-step, the press is only seen on the step's last frame). Y is 0x800 and opens the shortcut menu only if `EVENT_SHORTCUT_MENU_HasShortcuts` and the sub-screen is idle (`:762-770`).

The C-Gear's touch controls are in section 16.

---

## 2. X menu (start menu, `ov021` field menu)

### Detection

The X menu is the field sub-screen mode 1 (`FIELD_SUBSCREEN_MODE_MENU`), driven by the game event `EVENT_FIELD_MENU_Main`. It is not a proc and loads no overlay of its own (it lives in ov021), so there is no overlay test; use the field-map tests.

| Question | Test | Evidence |
|---|---|---|
| Menu open (any phase) | field up (section 1) and `sub->mode == 1` | `ov021/field_subscreen.c:629-650` (`InitMenu`), mode table row 1 |
| Menu accepting input | additionally `sub->state == 0` and `((FieldMenu*)sub->cgear)->state == 3` | `FIELD_MENU_STATE_IDLE 3`, `ov021/field_menu.c:218-229`; `FIELD_MENU_Update` only runs `HandleKeys`/`HandleTouch` in that state (`ov021/field_menu.c:549-650`, idle case at `:589-593`) |
| Menu sliding in / out | `FieldMenu.state` 0 (slide in), 9 (slide out); `scrollY` (+0xFC, s16) goes 0xC0 to 0 and back in steps of 0x40 | `ov021/field_menu.c:219-245,571-588,641-650` |
| Event that owns it | `G->event->func == EVENT_FIELD_MENU_Main`; its work is `FieldMenuWork` (0x5C bytes; `state`, `selectedButton`, `request.kind`) | `ov010/event_field_menu.c:166-204,225-306` |
| Menu handed off to an app | `FieldMenu.state` 8 then 10 and `panelResult == 3`; the event then runs `EVENT_SHORTCUT_MENU_CreateProcEvent`, which closes the field (section 0.3) | `ov021/field_menu.c:633-640`, `ov010/event_field_menu.c:244-263,296-299` |

The event's own state machine (`FieldMenuWork.state`): 0 wait for the sub-screen to be idle then pause the movement and request mode 1; 1 wait for the panel result (3 = a button was selected, 4 = closed); 2 read the selected button and its proc kind; 3 resume movement and leave; 4 field skill; 5/6 run the app through the shortcut proc event; 7/8 follow-up link event (`ov010/event_field_menu.c:225-306`).

### State

`FieldMenu` = `(FieldMenu*)sub->cgear` (0x138 bytes, `ov021/field_menu.c:179-216`; `FIELD_SUBSCREEN_InitMenu` stores it at `work->cgear`, `ov021/field_subscreen.c:649`).

| Field | Offset | Meaning |
|---|---|---|
| `heapId`, `subHeapId` | 0x00, 0x02 | |
| `fieldMap`, `parent` | 0x04, 0x08 | `parent` is the `FieldSubscreen` |
| `state` | 0x0C | 0 slide in, 1 wait print, 2 wait wipe, 3 idle, 4 back animation, 5 decide, 6 press animation, 7 notify close, 8 notify select, 9 slide out, 10 done (`ov021/field_menu.c:218-229`) |
| `myStatus` | 0x18 | the player's `MyStatus` (name, sex) |
| `cursorCol`, `cursorRow` | 0x1E, 0x1F | the highlighted slot is `col + row*2`; row 3 means the back slot |
| `layoutType` | 0x21 | 0 to 5, which button set (table below) |
| `keyCursorActive` | 0x2C | non-zero when the key cursor is shown (hidden after a touch) |
| `chosenButton` | 0x30 | button id picked (set by `FIELD_MENU_StartLeave`) |
| `items[7]` | 0x34, stride 0x1C | `inUse` +0, `bmpWin` +4, `buttonId` +8, `cursorX` +0x14 (u8), `cursorY` +0x15 (u8), `actor` +0x18 |
| `selectedItem` | 0xF8 | pointer to the highlighted `items[]` entry, or 0 |
| `scrollY` | 0xFC | s16 slide offset |

`FIELD_MENU_GetSelectedButtonId(menu)` returns the highlighted button id (0 when none) (`ov021/field_menu.c:902-908`); the accessor `FIELD_SUBSCREEN_GetMenuSelectedButton(sub)` works only in mode 1 (`ov021/field_subscreen.c:686-692`).

Slot layout and labels. The layout is chosen by `FIELD_MENU_GetLayoutType(gameData, eventWork, zoneId)` (`ov021/field_menu.c:769-785`): Union Room or zone-96 pair gives 1, zone 34 gives 4, `GAME_DATA_GetIntrudeCount != 0` gives 5, event flag 0x961 clear gives 3, flag 0x962 clear gives 2, else 0. The button ids per layout are the ROM table `gFieldMenuLayouts` (ov021 `0x021D8D24`, 6 rows of 7 ints, 0 ends the row; slot 6 is always the back slot):

| Layout | Slots 0 to 5 (button ids) | Buttons shown |
|---|---|---|
| 0 | 1, 2, 3, 4, 5, 6 | POKeMON, POKeDEX, BAG, player name, SAVE, OPTIONS |
| 1, 4, 5 | 1, 2, 3, 4, 6, 7 | POKeMON, POKeDEX, BAG, player name, OPTIONS, (blank) |
| 2 | 1, 3, 4, 5, 6, 7 | POKeMON, BAG, player name, SAVE, OPTIONS, (blank) |
| 3 | 3, 4, 5, 6, 7, 7 | BAG, player name, SAVE, OPTIONS, (blank), (blank) |

Button ids: 1 POKeMON (message 2), 2 POKeDEX (message 1), 3 BAG (message 3), 4 the player's name (`MYSTATUS_GetName`, or the default name `gFieldMenuDefaultName` when empty; this opens the trainer card), 5 SAVE (message 5), 6 OPTIONS (message 6), 7 empty. Messages are in bank 34 of `a/0/0/2` (`ov021/field_menu.c:815`; message ids ROM `gFieldMenuMsgIds` ov021 `0x021D8B34` = u16 {7,2,1,3,4,5,6,9}). The layout-count function `FIELD_MENU_CountLayoutButtons` counts non-blank ids (`ov021/field_menu.c:788-800`); the back slot's label is not a message but the back-button art.

Icon resources per button id (`gFieldMenuIconRes`, ov021 `0x021D8CAC`, triples `charId, cellId, animId`): id0 (13,28,38), id1 (8,23,33), id2 (11,26,36), id3 (6,21,31), id4 (10,25,35), id5 (9,24,34), id6 (5,20,30), id7 (8,23,33), all in the status-icon archive `APP_ICON_GetArchiveId()`; for a girl (`MYSTATUS_GetSex == 1`) buttons 3 and 2 use (7,0x16,0x20) and (0xC,0x1B,0x25) instead (`ov021/field_menu.c:804-860` `CreateItems`). Which art each id is (the recipe can also draw its own icons from the button id) is unverified.

Slot positions on the old lower screen: item `j` has `cursorX = (j % 2)*128 + 0x40`, `cursorY = (j / 2)*0x30 + 0x2C`, icon at `cursorX - 0x20`; text window top-left tile from ROM `gFieldMenuWindowPos` (ov021 `0x021D8B18`): (6,4), (22,4), (6,10), (22,10), (6,16), (22,16), (13,21), each 8 by 2 tiles; the back slot is `cursorX = 0x80, cursorY = 0xB4` (`ov021/field_menu.c:804-860`).

### Input

Touch table `gFieldMenuTouchTable` (ov021 `0x021D8C2C`, 7 entries then end; tested with `TOUCHPANEL_HitTrg`, `ov021/field_menu.c:1012-1030`). A hit selects the slot and leaves with that button. Only slots whose `buttonId` is not 7 react.

| Slot | Rectangle (x, y) | Tap centre |
|---|---|---|
| 0 | x 8-120, y 24-64 | (64, 44) |
| 1 | x 136-248, y 24-64 | (192, 44) |
| 2 | x 8-120, y 72-112 | (64, 92) |
| 3 | x 136-248, y 72-112 | (192, 92) |
| 4 | x 8-120, y 120-160 | (64, 140) |
| 5 | x 136-248, y 120-160 | (192, 140) |
| 6 (back) | x 224-248, y 168-191 | (236, 180) |

The slot to button mapping is the layout table above. A tap that lands on slot 6 starts the back animation and closes the menu (`FIELD_MENU_StartLeave(menu, 1)`); any other slot starts the leave with `chosenButton`. Keys: D-pad moves the cursor through the transition table `gFieldMenuCursorMoves` (ov021 `0x021D8C4C`), A selects, B or X closes (`FIELD_MENU_HandleKeys`, `ov021/field_menu.c:962-1010`). The first key press when no key cursor is shown only shows the cursor (`:966-973`), so a button press from the recipe should be sent twice, or a tap used instead. The menu opens and closes with a slide of three steps of 0x40 (`scrollY` from 0xC0 to 0, one step per frame, `ov021/field_menu.c:571-588`); input starts when `state == 3`.

Which proc each button opens: button id to proc kind `sProcKindOfButton` (ov010 `0x021712AC`) = {-1, 0, 1, 2, 3, 4, 5, -1}; proc table (ov010 `0x021712CC`, 24-byte rows `{overlayId, procTemplate, init, finish, start, exit}`, `ov010/event_shortcut_menu.c:160-167`):

| Button | Kind | Overlay | ProcData | Opens |
|---|---|---|---|---|
| 1 POKeMON | 0 | 91 | ov091 `0x021BED00` | party list (section 5) |
| 2 POKeDEX | 1 | 212 | ov212 `0x021C9938` | Pokedex (section 4) |
| 3 BAG | 2 | 70 | ov070 `0x021BB96C` | bag (section 3) |
| 4 name | 3 | 111 | ov010 `0x02172614` (`TRCARD_SYS`) | trainer card (section 9) |
| 5 SAVE | 4 | none | none; the row's `start` (`0x02161351`) is `EVENT_FIELD_PROCLINK_StartConfigEvent` | save report event (section 10) |
| 6 OPTIONS | 5 | 68 | ov068 `0x021BB5C8` | options (section 8) |

Kind 6 (overlay 131, summary) and kind 7 (town map) are not reachable from the X menu's own buttons (they come from the Y shortcut/party flows; `docs/unified-view-inventory.md` section 0).

---

## 3. Bag (`ov070`, "ITEMMENU")

### Detection

* `G->procSys->current->overlayId == 70` (0x46); also `bw_overlay_is_loaded(70)`. Entry: X menu kind 2 (ov010 `0x021712CC` row 2: overlay `0x46`, ProcData ov070 `0x021BB96C`, section 2). The bag is also opened by the shortcut/Y flows and by other apps (give item, sell, use item from the party list) with different `bagMode` values.
* The proc functions are `ITEMMENU_Init` / `ITEMMENU_Main` / `ITEMMENU_Exit` (`ov070/itemmenu.c:2338,2406,2429`). `ITEMMENU_Init` allocates the work with `PROCSYS_AllocProcWork(proc, 0x874, 0xe)` (`:2341`), so `Proc.work` is the `ItemMenuWork` (0x874 bytes, `ov070/itemmenu_types.h`) once `Proc.seq >= 2`. `Proc.procWork` is the caller's `ItemMenuArgs` (`saveData`, `myStatus`, `optionsSave`, `bagCursor`, `bag`, `actionPermissions`, `bagMode`, `bagFilter`; copied into the work at `:2350-2354`).
* The screen is the main engine routed to the bottom with `GX_SetDispSelect(GX_DISP_SELECT_SUB_MAIN)` (`docs/unified-view-inventory.md` section 0, `ov070/itemmenu.c:2727,2772-2778`).
* Sub-state: `ItemMenuWork.taskFn` (+0x4F8) is the current task function. Compare it with:

| `taskFn` | Meaning | Line |
|---|---|---|
| `ITEMMENU_TaskStartFadeIn`, `ITEMMENU_TaskWaitFadeIn` | opening fade | `:2290-2310` |
| `ITEMMENU_TaskBagMain` | the pocket list is browsable (normal state) | `:1515` |
| `ITEMMENU_TaskSelectItemFlash`, `ITEMMENU_TaskSelectItem` | an item was picked, flash then act | `:1475` |
| `ITEMMENU_HandleActionMenuChoice` (and `ITEMMENU_TaskActionMenuInput` for yes/no style prompts) | the action pop-up (USE, GIVE, TOSS, REGISTER, ...) is open | `:1379,1631` |
| `ITEMMENU_TaskListInput` | the move-an-item mode (SELECT) | `:1060-1135` |
| quantity tasks (`ITEMMENU_HandleQuantityInput`, toss/sell quantity tasks) | a count is being chosen | `:1938-` |
| `ITEMMENU_TaskWaitMessage*`, `ITEMMENU_TaskReturnToListAfterMessage` | a message window is showing | |
| `taskFn == 0` | the screen is closing (`ITEMMENU_Main` starts the wipe out and returns 1) | `:2406-2412` |

  Also `ItemMenuWork.listMode`, `messageWindowOpen` (+0x514) and `menuList` (+0x77C, non-zero while a pop-up list exists). `ItemMenuWork.bagMode` (+0x010): 0, 1 and 3 are the browse modes (`ITEMMENU_IsBrowseMode`, `:2086`), 2 is a selection mode whose pick returns `nextState` 3 (or 0x12 for mail) to the caller and has no X exit (`:1485-1500,1560,2252`), 4 runs `ITEMMENU_TaskGiveItem`, 5 returns the pick with `nextState = 1` (`ITEMMENU_TaskSelectItem`, `:1475-1510`); which caller uses which value is unverified.

### State

All fields of `ItemMenuWork` (DS offsets; `ov070/itemmenu_types.h`):

| Field | Offset | Meaning |
|---|---|---|
| `saveData` | 0x000 | the `GameData*` |
| `myStatus` | 0x008 | player `MyStatus` (money for the sell/buy price display, `ITEMMENU_PrintMoney`, `ov070/itemmenu.c:3654`) |
| `bagMode`, `bagFilter` | 0x010, 0x01C | see above |
| `bagCursor` | 0x014 | the save's `BagCursor` (per-pocket scroll/cursor; also the battle-bag last item, section 7) |
| `bag` | 0x018 | the save's `MyItem` bag |
| `items[310]` | 0x020 | a copy of one pocket (`BagSlot`s), valid only while `usingWorkCopy` is set (the SELECT move mode); otherwise the list is read from the bag |
| `taskFn` | 0x4F8 | current task |
| `currentPocket` | 0x80C | the pocket shown: 0 ITEMS, 1 MEDICINE, 2 TMs & HMs, 3 BERRIES, 4 KEY ITEMS (`BAG_POCKET_*`, `arm9/myitem_savedata.c:31-39`; names are bank 55 of `a/0/0/2`: "ITEMS", "MEDICINE", "TMs & HMs", "BERRIES", "KEY ITEMS") |
| `cursorRow` | 0x810 | highlighted row 0 to 5 |
| `scrollOffset` | 0x814 | scroll; the index of the first fully visible row is `scrollOffset + 1`; starts at -1 |
| `usingWorkCopy` | 0x81C | non-zero in the move mode |
| `selectedItemId` | 0x84C | the item picked (action menu) |
| `screenState`, `nextState` | 0x840, 0x844 | exit request: `nextState` 0 = X (give-up/close variant), 1 = B (close) (`ov070/itemmenu.c:1565-1580`) |
| `isTouchActive`, `isDraggingItem` | 0x85C, 0x860 | touch drag bookkeeping |

* The list: the selected entry is `ITEMMENU_GetCursorIndex(work) = cursorRow + scrollOffset + 1` (`ov070/itemmenu.c:795-798`); the number of entries is `ITEMMENU_GetItemCount(work)` (`:800-804`, `MYITEM_SAVEDATA_CountUsedSlotsInPocket(bag, currentPocket)`); entry `i` is `ITEMMENU_GetSlot(work, i)` (`:775-780`), a `BagSlot {u16 itemId; u16 quantity;}` (`arm9/myitem_savedata_types.h:7-10`). Row `r` (0 to 5) shows entry `scrollOffset + 1 + r` (`ITEMMENU_PrintItemList` draws entries `scrollOffset + i` for i = 0 to 7, of which rows 1 to 6 are the visible ones, `:3217-3262`). An entry with `itemId == 0` is empty.
* Item names: message bank 0x36 (54) of `a/0/0/2`, entry = item id (`WORDSET_SetSlotFromMsg36` -> bank 0x36, `arm9/wordset.c:476-479`; checked: entry 4 = "Poke Ball", entry 3 = "Great Ball"). Pocket names: bank 0x37 (55). Window titles and labels come from the bag's own bank `MSGDATA_CreateFromArchive(0, 2, 3, ...)` = bank 3 (`ov070/itemmenu.c:2380`; "USE", "TRASH", "Items have been sorted!", "It's empty." and so on, indexes in the code such as 0x87 title, 0x8B pocket count line).
* Quantity: `BagSlot.quantity`; key items show no quantity. Item description and price are read from the item data record (`FILE_UTIL_LoadArcMember(itemArchive, itemId)`; `FILE_UTIL_GetItemParam(id, 0, heap)` is the price, `:790`). The description text bank is not in the functions read: unverified.
* Registered (Y) items: `ITEMMENU_IsItemRegistered` = `GAME_DATA_ContainsRecentId(saveData, ITEM_GetRegisterFlagId(itemId))` (`ov070/itemmenu.c:2192-2195`).
* Pop-up action list: `ITEMMENU_BuildActionList` fills up to four action ids (`ov070/itemmenu.c:2136-2164`, starting from `sDefaultActionIds` = FF FF FF FF at ov070 `0x021BB95C`; 0xFF means "omit"), `ITEMMENU_OpenActionMenu` removes the omitted ones, maps each id to a message through `sActionMsgIds` (ov070 `0x021BB998`, ints {0, 6, 16, 1, 2, 18, 3, 5, 8, 87, 0}) and opens an `APP_TASKMENU` list (`:2169-2188`, `ITEMMENU_OpenPopupList :3470`). Slot 0 is USE (id 0), WALK (id 1, chosen for item 0x1C2 when `bagFilter == 1`, `ITEMMENU_CheckItemFilter`) or CHECK (id 2, mail); slot 1 is GIVE (id 6, only items that may be held and are outside the reserved id range); slot 2 is TRASH (id 3, not in the TMs & HMs pocket), REGISTER (id 4) or DESELECT (id 5) for registerable items; slot 3 is CANCEL (id 8). The labels are bank 3 messages 0 USE, 6 WALK, 16 CHECK, 3 GIVE, 1 TRASH, 2 REGISTER, 18 DESELECT, 8 CANCEL (decoded). The chosen action is `work->menuActionTable[cursorPos]` (+0x828) after `ITEMMENU_HandleActionMenuChoice` (`:1379`). `ITEMMENU_IsCheckModeAllowed` can remove the first entry (`ITEMMENU_BuildActionList`, `:2143-2145`).

### Input

The bag has two touch mechanisms:

**(a) The `BUTTON_MAN` table `sButtonHitTable`** (ov070 `0x021BBA04`, `ov070/itemmenu.c:2385-2387`), polled by `BUTTON_MAN_UpdateSlots` in `ITEMMENU_TaskBagMain` (`:1523`). The callback `ITEMMENU_HandleTouchTarget(idx, flag, work)` acts only on `flag == 0` (the press) (`:2201-2290`). Entries (top, bottom, left, right), dumped from the ROM:

| Index | Rectangle (x, y) | Tap centre | Action (`ITEMMENU_HandleTouchTarget`) |
|---|---|---|---|
| 0 | x 0-36, y 100-147 | (18, 123) | pocket tab: `sTabPockets[0]` = pocket 1 (MEDICINE) |
| 1 | x 37-73, y 105-144 | (55, 124) | pocket tab: pocket 2 (TMs & HMs) |
| 2 | x 74-111, y 94-128 | (92, 111) | pocket tab: pocket 3 (BERRIES) |
| 3 | x 64-115, y 56-93 | (89, 74) | pocket tab: pocket 4 (KEY ITEMS) |
| 4 | x 0-63, y 40-104 | (31, 72) | pocket tab: pocket 0 (ITEMS) |
| 5 | x 0-23, y 168-191 | (12, 180) | previous pocket (wraps) |
| 6 | x 120-143, y 168-191 | (131, 180) | next pocket (wraps) |
| 7 | x 152-175, y 168-191 | (163, 180) | SORT button: only if the pocket is not 2 or 3 (TMs & HMs, BERRIES) and has more than one entry; plays the button animation, sorts the pocket and prints "Items have been sorted!" (bank 3 message 0x8A) (`ITEMMENU_TaskInputGuardMessage`, `ov070/itemmenu.c:2655-2666`; the code calls this button's animation actor the "input guard" actor, `ITEMMENU_ShowInputGuardError`, `:2058`). Same as the START key |
| 8 | x 176-191, y 168-191 | (183, 180) | toggle the pocket's sort-mode flag (`ITEMMENU_TogglePocketSortFlag`), browse modes only; the Y key does this in every pocket except KEY ITEMS (where Y registers) |
| 9 | x 192-215, y 168-191 | (203, 180) | select/confirm side button: `nextState = 0` and close (`StartSideButtonPress(3)`), not in bagMode 2 |
| 10 | x 224-247, y 168-191 | (235, 180) | back/exit: `nextState = 1` and close |
| 11 to 16 | x 144-223, y 12-35, 36-59, 60-83, 84-107, 108-131, 132-155 (rows 0 to 5) | (183, 23), (183, 47), (183, 71), (183, 95), (183, 119), (183, 143) | select that list row: moves the cursor and starts the pick (`ITEMMENU_TaskSelectItemFlash`); ignored if the row is beyond the item count or if `taskFn != ITEMMENU_TaskBagMain` |
| 17 to 22 | x 120-143, y 12-35 ... 132-155 (rows 0 to 5) | (131, 23) ... (131, 143) | toggle the Y-registration of the entry in that row (`ITEMMENU_ToggleRegisteredItem(row)`), browse modes |

The pocket tab rectangles are irregular because the tabs are drawn as a fan; the labelling of indexes 0 to 4 by screen position is by pocket id (the table maps index to pocket through `sTabPockets` ROM `0x021BB984` = {1, 2, 3, 4, 0}, `ov070/itemmenu.c:2215-2220`). Buttons 5 to 10 are the bottom-row buttons; their on-screen icons are not named by the code beyond the actions listed (unverified which art is which). A recipe that draws its own tabs should tap the tab's index above.

**(b) Direct touch reads in the same task and in other tasks:**

* Scroll bar: `ITEMMENU_HandleScrollbarTouch` (`ov070/itemmenu.c:900-926`): active when the pocket has 7 or more entries; a press with x in 0xE0-0xFF and y in 0x10-0x97 starts a drag (`isTouchActive`), then the held point sets `scrollOffset = SCROLLBAR_GetValueFromPos(itemCount - 6, touchY, 0x1A, 0x8E, 0) - 1`. So a scroll-wheel recipe can tap at x 240, y between 26 and 142 (the position maps linearly to the scroll value).
* Back button for the list-move mode: `ITEMMENU_IsBackButtonTouched` (`:1044-1050`): x 0xE0-0xF7, y 0xA8-0xBF.
* Row touch in the move mode (`ITEMMENU_HandleListTouch`, `:1004-1040`): x 0x90-0xE7, y 0x0C-0x9B, row = `(y - 0x0C) / 0x18` (24 px rows).
* Quantity selector: `sQuantityTouchRects` (ov070 `0x021BB960`), polled with `TOUCHPANEL_HitCont` (`:1938-1960`): up/down arrows for the count (rectangles not dumped: unverified).
* Yes/no list: `ITEMMENU_GetYesNoTouchChoice` (`:2600-2606`) tests `sBackButtonHitRect` (ov070 `0x021BB978`); the yes/no and the action pop-up are `APP_TASKMENU` lists with run-time rectangles (section 0.5, `ITEMMENU_OpenPopupList`, `:3470`).

Keys (`ITEMMENU_TaskBagMain`, `:1515-1630`): D-pad up/down move the cursor, L/R change by 6 rows (`ITEMMENU_HandleListKeys`, `:928-950`), left/right (0x20/0x10 repeat) change pocket, A picks, B closes (`nextState = 1`), X (0x400) closes with `nextState = 0` (not in bagMode 2), Y (0x800) toggles the registration (pocket 4) or the sort flag, START (0x08) sorts the pocket, SELECT (0x04) starts the move mode. As elsewhere, the first key press after a touch only brings the key cursor back (`UI_GetInputMode() == 1` branch, `:1529-1542`).

---

## 4. Pokedex (ov212 wrapper, ov211 splash, ov209 list, ov208 detail, ov206 info, ov210 search)

### Detection

The Pokedex is one proc (ov212) that runs its own private `ProcSys`. The screens are children of that private stack, not of `gameSystem->procSys`.

* Wrapper is up: `gameSystem->procSys->current->overlayId == 0xD4` (212). ROM data: kind 1 row of the X menu proc table, `0x021712E4` in ov010 holds `0xD4`. Also `bw_overlay_is_loaded(212)`.
* `current->work` is the `ZukanWork` (44 bytes, `PROCSYS_AllocProcWork(obj,44,105)`): `ov212/zukan.c:221-226`. Fields: `ctx` 0x00, `proc` (private `ProcSys*`) 0x04, `procResult` 0x08, `idList` 0x0C, `count` 0x10, `mode` 0x12, `query[8]` 0x14, `state` 0x1C, `resumeState` 0x20, `subMode` 0x24, `subWork` 0x28 (`ov212/zukan.c:74-87`).
* Which screen is showing: read `((ProcSys*)ZukanWork.proc)->current->overlayId`, or use `bw_overlay_is_loaded`.

| Screen | Overlay | Test | `ZukanWork.resumeState` while it runs |
|---|---|---|---|
| Splash with the Pokedex skin | ov211 (211) | `bw_overlay_is_loaded(211)` | 2 |
| Species list | ov209 (209) | `bw_overlay_is_loaded(209)` | 4 |
| Detail, any of the 4 pages | ov208 (208) | `bw_overlay_is_loaded(208)` | 6 |
| Search | ov210 (210) | `bw_overlay_is_loaded(210)` | 8 |

* State machine, indices from the ROM table `0x021C9944` (`ov212/zukan.c:243`, function addresses from `layout/function_addrs.tsv`): 0 wait for child (returns `resumeState` once the child stack finished), 1 start ov211, 2 resume after ov211, 3 start ov209, 4 resume after ov209, 5 start ov208, 6 resume after ov208, 7 start ov210, 8 resume after ov210, 9 exit.
* The first screen is chosen by `ZukanParam.startSpecies` (misnamed): 0 starts the ov211 splash, 1 the list, 2..5 the detail page 1..4, else the search (`ov212/zukan.c:233-239`). The X menu passes -1 which becomes 0 (`ov010/event_field_proclink.c:594-598`), so the normal path is splash, then list.
* ov206 (the description widget `ZUKAN_INFO_*`) is loaded only while detail page 1 (info) is active: `GF_OVERLAY_Load(ov206_id)` in `ZUKAN_DETAIL_INFO_Init`, unloaded in its Exit (`ov208/zukan_detail_info.c:381-382,418`). So `bw_overlay_is_loaded(206)` means "detail, info page". ov208 loaded without ov206 means detail page area, voice or form.
* Detail page: `ZukanDetailArgs.mode` (1 info, 2 area, 3 voice, 4 form), valid while `resumeState == 6`, read as `((ZukanDetailArgs*)ZukanWork.subWork)->mode` (`ov212/zukan.c:98-105`, `ov208/zukan_detail.c:74-99`). The tabs rewrite it (`ov208/zukan_detail_main.c:Main`).
* LCD routing: list and splash call `BG_SYS_SetLcdSwap(0)` (main engine on the bottom): `ov209/zukan_list.c:1109`, `ov211/zukan_search_screen.c:261`; the detail calls it with `display` = 0 from `ZUKAN_DETAIL_MAIN_Init` (`ov208/zukan_detail_graphic.c:287`, `ov208/zukan_detail_main.c:Init`).

### State

Common source of truth (all screens): `ZUKAN_SAVE_GetFromGameData(gameData)` gives the `ZukanSave`. Accessors (`arm9/zukan_save.h:22-51`):

* `ZUKAN_SAVE_IsSeen(save, species)`, `ZUKAN_SAVE_IsCaught(save, species)`
* `ZUKAN_SAVE_CountSeenNational/CaughtNational(save)`, `...Regional(save, 0x806b)` (`ov209/zukan_list.c:1464-1474`)
* `ZUKAN_SAVE_GetViewMode(save)` (1 national order, 0 regional), `ZUKAN_SAVE_GetLastViewedSpecies`, `ZUKAN_SAVE_GetDexObtained`, `ZUKAN_SAVE_IsNationalDexEnabled`

**List (ov209).** Work: `ZukanList`, 0xC44 bytes, `PROCSYS_AllocProcWork(p,0xc44,107)` and `*work = arg` (`ov209/zukan_list.c:785`); found as `private ProcSys->current->work`.

| Field | Offset | Meaning | Evidence |
|---|---|---|---|
| `args` | 0x000 | `ZukanListArgs`: `gameData` 0, `save` 4, `mode` 8 (0 regional header, 1 national), `count` 0xA, `ids` 0xC (species per row), `state` 0x10 (what the list asks the wrapper to do) | `ov209/zukan_list.c:275-282` |
| `state` | 0x010 | list state machine (`gZukanListStateHandlers`; 4 is the idle input state, 5 scroll, 6 button animation, 7 cursor flash, 9 end) | `:1197-1245,1297-1325` |
| `frameList` | 0xC2C | the `FrameList*` | `:296-330` |
| `rowCount` | 0xC34 | rows shown, at most 7 | `:1042` |
| `speciesText[649]` | 0x04C | the name string of every row (a "?" string for unseen) | `:1046-1065` |

Row data: `FRAME_LIST_GetItemValue(frameList, i)` is `(status << 28) | species`, status 2 caught, 1 seen, 0 unknown (`ov209/zukan_list.c:1052-1065`, `ZUKAN_LIST_FrameListMoveCallback :1073-1080`). The species ids of the rows are `args->ids[i]` (`count` rows; the regional list skips entry 0 when the national dex is not enabled, `ov212/zukan.c:StartProc209`). Species names: message bank `gSpeciesNameBank`, string index = species. Selected row: `FRAME_LIST_GetSelectedIndex` = `cursor + scrollPos`; the header counts are drawn from the two `Count*` calls above (regional uses a 3 digit field, `ov209/zukan_list.c:1464-1484`). The big Pokemon preview on the other screen shows `ZUKAN_LIST_LoadSpeciesIcon(p, species)` of the selected row (`:1086-1091`).

**Detail (ov208).** Work chain: `Proc.work` of ov208 is `Ov208Main` (0x2C bytes, `ov208/zukan_detail_main.c:Init`, `ov208/zukan_detail_main_shared_types.h:Ov208Main`). The caller block is `Proc.procWork` = `ZukanDetailArgs` (`ov212/zukan.c:98-105`): `gameData` 0, `mode` 4, `idList` 8, `count` 0xC (u16), `index` 0xE (u16), `step` 0x10. The shown species is `idList[index]`. (ov208 names the same block `Ov208Args {src, mode, extra, formNo, shiny, speciesStep}`, the same offsets; `extra` is the id list, `formNo` the count, `shiny` the index. This is the naming of the same words, settled by the common field offsets and `ZUKAN_DETAIL_COMMON_SelectNextSeenSpecies`, `ov208/zukan_detail_common.c:310-334`, which moves `*cursorPtr` through `speciesTable`.) Next and previous skip species that are not seen. The shared context is `Ov208Ctx` (`ov208/zukan_detail_shared_types.h:70-81`). Per-page work is allocated from the node, for example `Ov208InfoWork` 0x120 bytes (`ov208/zukan_detail_info.c:384-386`).

Info page text: `ZUKAN_INFO_SetPokemon(p, species, form, gender, rare, personalParam, showCurrent)` (ov206, `ov208/zukan_detail_info.c:300`). Gender and form viewed: `ZUKAN_SAVE_GetViewGenderAndForm` (declared in the ov209 symbol list, `ov209/zukan_list.c:191`).

**Splash (ov211).** Work `ZukanSearchWork` 16 bytes (`ov211/zukan_search_types.h`), owner `ZukanSearchOwner {gameData, context, state}`. It leaves on any touch, A, or after 300 frames (`HandleInput`, `ov211/zukan_search_screen.c:274-300`).

**Search (ov210).** Work 0x2D4 bytes (`PROCSYS_AllocProcWork(man,0x2d4,0x7b)`, `ov210/zknsearch_sys.c:77`); args `ZknSearchArgs {gameData, save, query*, result, idList*, count}` (`ov212/zukan.c:111-119`); result 0 = list replaced by the search hits, 1 = back, 2 = leave the Pokedex (`ov212/zukan.c:ResumeProc210`). The five search pages are not documented here (unverified beyond the two tables below).

### Input

All taps are bottom screen coordinates. All Pokedex controls react on the press (`TOUCHPANEL_HitTrg` or `GetPointTrg`).

**List (ov209).** Hit table in ROM at overlay 209 `0x021BEAE0`, read by `ZUKAN_LIST_HandleTouchInput` (`ov209/zukan_list.c:1903-1952`) and consumed by `ZUKAN_LIST_StateMain` (`:1215-1252`):

| Code | Rectangle (x, y) | Action | Tap centre |
|---|---|---|---|
| 0 | x 0-95, y 0-95 | open the species under the cursor (if seen or caught) | (47, 47) |
| 1..7 | x 96-127, y 24*(n-1) .. +23 | open the species of that visible row (`scrollPos + n - 1`), cursor moves there | (111, 24*(n-1)+12) |
| 8 | x 0-71, y 168-191 | search; only when `ZUKAN_SAVE_GetDexObtained != 0`. Sets `args->state = 4`, wrapper starts ov210. Key: START | (35, 179) |
| 9 | x 72-135, y 168-191 | toggle national / regional order; needs national dex enabled and `args->mode == 0`. Sets `args->state = 2`. Key: SELECT | (103, 179) |
| 10 | x 184-207, y 168-191 | toggles a flag in the save (`ZUKAN_LIST_ToggleSaveFlag15`); what it means on screen is unverified. Key: Y. Ignored when `args->mode == 1` | (195, 179) |
| 11 | x 208-231, y 168-191 | close the whole Pokedex, `args->state = 1`. Key: X | (219, 179) |
| 12 | x 232-255, y 168-191 | back, `args->state = 0` (to the previous list, or out). Key: B | (243, 179) |

The scrolling list itself is a `FRAME_LIST`; its table is at overlay 209 `0x021BE914` (`gZukanListFrameListParams.hitEntries`, `ov209/zukan_list.c:1032`), read as 8-byte entries `{top, bottom, left, right, kind}`:

| Kind | Rectangle | Meaning |
|---|---|---|
| 0 (row) x7 | x 128-231, y 24*i .. 24*i+23, i = 0..6 | press selects the row and lets the stylus drag; a flick scrolls |
| 1 | x 232-255, y 8-160 | scroll bar |
| 4 | x 136-159, y 168-191 | page up |
| 5 | x 160-183, y 168-191 | page down |

(`ov067/frame_list_types.h:FrameListHitKind`, `ov067/frame_list.c:411-470`.) Keys: A opens the species (`default` branch of `StateMain`), up/down move the cursor, L/R page (`FRAME_LIST_ReadKeys`, `:497-`). The safest tap that opens a species is code 1..7 (it opens at once); a tap on a list row only moves the cursor (confirm-on-second-tap or on release is unverified).

**Detail (ov208).** Bar buttons come from ROM data at overlay 208 `0x021C8440` (9 entries of 20 bytes `{id, x, y, hitWidth, pad, pressedId, heldId}`, read by `ZUKAN_DETAIL_TOUCHBAR_LoadBarResources`, `ov208/zukan_detail_touchbar.c:595-632`). Hit test `x - x0 <= hitWidth && y - y0 <= 24` on the press (`ov208/zknd_tbar.c:536-543`), so the rectangle is `x0 .. x0+hitWidth`, `y0 .. y0+24`. The bar only reacts once it has slid in (`ZKND_TBAR_SetTouchEnable`, `ov208/zukan_detail_touchbar.c:350,399`).

| Bar id | x0, y0, width | pressedId -> action in `ZUKAN_DETAIL_MAIN_Main` | Tap centre |
|---|---|---|---|
| 1 | 232, 168, 24 | 2: `speciesStep = 1`, the screen ends and the wrapper goes back to the list (`ov212/zukan.c:ResumeProc208` case 1) | (244, 180) |
| 0 | 208, 168, 24 | 1: `speciesStep = 0`, the wrapper leaves the whole Pokedex (case 0) | (220, 180) |
| 6 | 184, 172, 24 | 5: sets a "recent id" flag 0x11 (`ov208/zukan_detail_info.c:530-535`); the control's label is unverified | (196, 184) |
| 10 | 144, 168, 32 | 9: switch to page 4 (form) | (160, 180) |
| 9 | 112, 168, 32 | 8: page 3 (voice / cry) | (128, 180) |
| 8 | 80, 168, 32 | 7: page 2 (area / habitat) | (96, 180) |
| 7 | 48, 168, 32 | 6: page 1 (info) | (64, 180) |
| 2 | 24, 168, 24 | pressedId 3 (clears state); heldId 12 = next seen species while held | (36, 180) |
| 3 | 0, 168, 24 | pressedId 4 (clears state); heldId 13 = previous seen species while held | (12, 180) |

The mapping of the page buttons to pages: `ZUKAN_DETAIL_MAIN_Main` switch (`pendingMode = 1..4` for pressed 6..9) and `ZUKAN_DETAIL_MAIN_SwitchScreen` (`ov208/zukan_detail.c:66-101`). Species stepping uses the held id (12 / 13), `ZUKAN_DETAIL_INFO_HandleKeyEvent` (`ov208/zukan_detail_info.c:560-575`), so the stylus must stay down for at least 2 frames. Other bar layouts (mode 1 left arrow only: table `0x021C8390`, one button id 1 at x 232, y 168, w 24, pressedId 19; mode 2 area sub mode: table `0x021C83C8`, 6 buttons) are used by the area screen; their action is unverified. Buttons: B and X act like bar ids 1 and 0 (`ov208/zukan_detail_info.c` key arm: key 1 and 2 set mode 2), left/right choose the previous/next page (unverified).

**Search (ov210).** Two list hit tables in ROM at overlay 210: `0x021C0B3C` (rows 0..7: x 128-143, y 24*i .. 24*i+23, plus entry 0 back at x 232-255, y 168-191) and `0x021C0B24` (4 rows of 40 px, the same back button) (`ov210/zknsearch_main.c:3842-3875`). The rest of the search UI (condition pages, result) is unverified.

---

## 5. Party list (ov091)

### Detection

* `gameSystem->procSys->current->overlayId == 0x5B` (91), also `bw_overlay_is_loaded(91)`. ROM data: X menu kind 0 row at ov010 `0x021712CC` holds overlay `0x5B`; the same table is used for the shortcut menu (`ov010/event_shortcut_menu.c:160-167`). ov090 is the link/trade party list, not this one.
* The work is `Proc.work`: `POKELIST_Init` calls `PROCSYS_AllocProcWork(procObj, 0x284, 0x17)` (`ov091/pokelist.c:792`); `Proc.procWork` is the `PokelistArg` (0x98 bytes, `ov091/pokelist_types.h:46-77`).
* Sub-states: `PokelistWork.phase` (0x0C), set up in `POKELIST_InitWork` (`ov091/pokelist.c:916-`), dispatched in `POKELIST_UpdatePhase` (`:1037-1165`).

| `phase` | Meaning | Evidence |
|---|---|---|
| 0, 1 | fade in | `pokelist.c:1043-1050` |
| 2 | plain list: pick a Pokemon (cursor keys or touch) | `:1051-1055,1919-1935` |
| 3 | choosing the Pokemon to swap with (the "switch" mode; the source plate stays marked) | `:2066-`, `POKELIST_DispatchMenuChoice` case 3 (`:2582-2594`) |
| 4 | heal / share target pick | `:2093-` |
| 5 | swap animation | `:1056-1058,2847-2896` |
| 6 | the context menu is open | `:1059-1061,2478-2537` |
| 7 | message window | `:1062-1064,2801-2812` |
| 9 | waiting for a menu choice (callback menus) | `:1065-1067,2825-` |
| 10, 11 | HP count-up animation | `:1068-1090` |
| 0x13 (19), 0x14 (20), 0x15 (21) | close: button press animation, fade out, return | `:1138-1152` |

  `inputStage` (0x0E) is the sub-sub-state: 0 init, 1 read cursor/touch, 2 act on `command` (`:1919-1935`).
* The list is also used for item use, move teaching, evolution items and the battle party order; `PokelistArg.mode` (0x34) says which: 0 plain field list, 1 / 22 / 23 item-style list with confirm buttons (`POKELIST_IsItemMode`, `:2931-2937`), 5 item use, 6 TM, 18, 21, 25 and so on (`:1799-1918`). Mode 21 has no back button (`:2946-2949`). A recipe that only covers the X-menu path sees mode 0.
* LCD: `GX_SetDispSelect(GX_DISP_SELECT_SUB_MAIN)` (`ov091/pokelist.c:1299`) puts the main engine on the bottom screen; the plates are main-engine layers.

### State

`PokelistWork` (0x284, `ov091/pokelist_types.h:80-170`): `touchMode` 0x08 (0 key cursor, 1 touch), `phase` 0x0C, `inputStage` 0x0E, `command` 0x0F, `switchButtonEnabled` 0x14, `selectedSlot` 0x2C (0..5 a plate, 6 and up a command button), `swapSourceSlot` 0x30, `selectedMon` 0x38, `menuChoice` 0x3C (0x17 none), `messageWindow` 0x150, `contextMenu` 0x154, `plates[6]` 0x160, `arg` 0x280.

Party: `arg->party` (`PokelistArg` 0x00). Use `POKEPARTY_GetCount(party)` and `POKEPARTY_GetMember(party, i)` (`arm9/pokeparty.h:19,26`). Each plate is a `PlistPlate` (0x44, `ov091/plist_plate_types.h:76-95`): `mon` 0x00, `slot` 0x04, `hp` 0x06 (the value shown, it counts to `targetHp` 0x08), `isEgg` 0x0C, `selected` 0x10, `isEmpty` 0x14, `labelState` 0x1C (0..5 order number in battle-order mode, 6 not eligible, 7 eligible, 8 none).

Values to draw come from `POKE_TOOL_GetPartyValue(mon, id, 0)` (`arm9/poke_tool.h:42`; ids in `arm9/poke_tool_param.h`): species 0x05, held item 0x06, moves 0x36-0x39, PP 0x3A-0x3D, max PP 0x42-0x45, is egg 0x4C, gender 0x6E (0 male, 1 female; only printed when the nickname is not the default, `plist_plate.c:395-402`), form 0x6F, nickname strbuf 0x73 (the plate prints it with `WORDSET_SetSlotMonNickname`, `plist_plate.c:389`), status condition 0x9D (also `POKE_TOOL_GetStatusCondition`; the status icon anim map is 1->1, 2->3, 3->2, 4->5, 5->4, and HP 0 shows the fainted icon 6, `plist_plate.c:419-429`; the exact names of the five status codes are unverified), level 0x9E (`POKE_TOOL_CalcLevel`), HP 0xA0, max HP 0xA1. The plate shows the level only when status is 0 and HP is not 0 (`plist_plate.c:405-411`). The HP bar fills `BGWINFRM_ScaleRatio(hp, maxHp, 0x30)` dots with colour from `PLIST_PLATE_GetHpBarColor` (0 red, 1 yellow, 2 green, 3 full; `plist_plate_types.h:63-69`).

Plate positions: ROM data `gPlistPlateTileX/Y` at overlay 91 `0x021BEC74`: tiles (0,1), (16,2), (0,7), (16,8), (0,13), (16,14) for slots 0..5, each 16 by 6 tiles = 128 by 48 px (`ov091/plist_plate.c:734-740`). So slot rectangles in pixels: slot 0 x 0-127, y 8-55; slot 1 x 128-255, y 16-63; slot 2 y 56-103; slot 3 y 64-111; slot 4 y 104-151; slot 5 y 112-159.

Context menu (phase 6): `PlistMenu` (`ov091/plist_menu_types.h:36-44`): `count` 0x00, `ids[8]` u16 at 0x02, `entries[8]` at 0x14 (each `{label strbuf, color, disabled}`), `taskMenu` 0x74. Command ids: 0 summary, 3 switch, 4 item (opens a sub menu), 5 mail, 6 cancel, 16..19 field moves 1..4 (label = move name from bank 0xCB), 20 / 21 battle-party select / deselect (`plist_menu.c:326-386,387-400`, `pokelist.c:2571-2640`). For a plain field list the menu is built from `{0, 1, 3, 4, 6, end}` for a normal Pokemon and `{0, 3, 6, end}` for an egg, with `1` expanding to the field-move entries and `4` becoming `5` for a mail item (`pokelist.c:1803-1819`; `0` is summary).

### Input

All taps react on the press (`TOUCHPANEL_HitTrg`).

| Control | Rectangle (x, y) | Result | Evidence |
|---|---|---|---|
| Plate of slot n (occupied) | slot rectangle above, centre (64, 32), (192, 40), (64, 80), (192, 88), (64, 128), (192, 136) | `command = 3`: select that Pokemon; phase 2 then opens the context menu (`POKELIST_ApplySelection`). Eggs may play a refuse sound when `allowDisabledSelect == 0` | `pokelist.c:2365-2420,1991-2000` |
| Back (normal list, `HasBackButton`) | x 224-248, y 168-192 (`top 0xA8, bottom 0xC0, left 0xE0, right 0xF8`) | `command = 1`: close with `selectedIndex = 7`; centre (236, 180) | `pokelist.c:2430-2437`, `:2025-2043` |
| Switch button (normal list, only when `switchButtonEnabled`) | x 200-224, y 168-192 | `command = 2`: close with `selectedIndex = 8`; the field handles it. Centre (212, 180) | `pokelist.c:2438-2440`, `:2045-2062` |
| Item mode, confirm | x 96-176, y 168-192 (not-item-use variant); cancel x 176-255, same y | `command = 0` confirm, `command = 1` cancel | `pokelist.c:2420-2429` |
| Context menu entry i of n | `TaskMenu` geometry with `posType = 1`, `charPosX = 0x20`, `charPosY = 0x18` (24), `width = 13`, `height = 3`: x 152-255, y `(24-3n)*8 + 24*i` .. +24. Tap decides at once | `plist_menu.c:269-284` and `arm9/app_taskmenu.c:655-686`; `bgFrame = 3` in `PlistTaskMenuTemplate` is the TaskMenu `height` field (the header names it differently) |

Plates are tested as a compacted list of occupied plates (`rects[count]`), so `hit` is an index into the occupied plates; they are contiguous, so it equals the slot (`pokelist.c:2365-2395`). Keys: D-pad moves between plates (`POKELIST_ReadCursorKeys`, `:2225-2363`), A selects, B backs out. Unverified: the exact keys of the switch button and the position of the cursor with the D-pad.

---

## 6. Pokemon summary (ov131)

### Detection

* `gameSystem->procSys->current->overlayId == 0x83` (131); ROM data: X menu kind 6 row, ov010 `0x0217135C` holds `0x83`. Also `bw_overlay_is_loaded(131)`. In the field it is reached from the party list context menu (command 0 -> `exitCode = 1`, then `EVENT_FIELD_PROCLINK_CreatePokeStatusParam`, `ov010/event_field_proclink.c:620-690`). ov178 only wraps this proc for the network and ov173 is the move relearner, a different screen.
* `Proc.work` = `PStatusWork` (0x264 bytes): `PROCSYS_AllocProcWork(proc,0x264,0x42)` at `ov131/p_status.c:1459`; `Proc.procWork` = `PStatusProcParam` (0x28, `ov131/p_status_types.h:96-113`).
* Sub-states:

| Item | Field | Values | Evidence |
|---|---|---|---|
| Screen state | `PStatusWork.seq` 0x50 | 0 fade in / refresh, 1 wait wipe, 2 closing (button animation), 3 wipe out, 4 normal input | `p_status.c:529-577` |
| Page | `page` 0x58 (shown page `dispPage` 0x5C) | 0 info, 1 skills (stats; the move slots are shown here too and a tap on a slot opens a move detail view in place), 2 ribbons, 3 move list used only when forgetting a move (`param->mode == 2`) | `p_status.c:431-495,1255-1340`, `ov131/p_sta_skill.c:149-186` |
| Mode | `param->mode` 0x0D | 0 normal, 2 choose a move to forget (page 3 is forced) | `p_status.c:462-471` |
| Closed by | `param->retSel` 0x12 | 1 B, 2 X | `p_status.c:927-945` |

* LCD: `GX_SetDispSelect(GX_DISP_SELECT_SUB_MAIN)` (`p_status.c:638`): the main engine is on the bottom, the 3D model (MCSS) and 2D layers of the page share it with the sub engine as the game draws them.

### State

`PStatusProcParam` (the call block): `ppd` 0x00 (a party, one Pokemon or an array, per `ppt`), `ppt` 0x0C (0 one Pokemon, 1 party, 2 array of box Pokemon), `mode` 0x0D, `max` 0x0E (count), `pos` 0x0F (Pokemon shown first), `page` 0x10, `retMovePos` 0x11, `retSel` 0x12, `newMove` 0x14, `isNationalDex` 0x18, `canUseButtons` 0x20 (X and Y usable), `forceExit` 0x24.

`PStatusWork` (`ov131/p_status_types.h:134-190`): `param` 0x08, `refreshReq` 0x0C, `pos` 0x18 (the Pokemon shown), `inputType` 0x1C (0 keys, 1 touch), `tpHit` 0x24 (touched button, set every frame by `TOUCHPANEL_HitTrg`), `isEgg` 0x38, `friendship` 0x3C, `hasRibbon` 0x40, `isInputEnabled` 0x44, `page` 0x58, `pageToggle[3]` 0x60 (state of the Y toggle per page), `subWork` 0x88, `infoWork` 0x8C, `skillWork` 0x90, `ribbonWork` 0x94, `buttonActor[8]` 0x178 (tabs 0..2, Y 3, up 4, down 5, X 6, B 7).

The Pokemon: `P_STATUS_GetPokePara(wk)`: ppt 0 = `ppd`, ppt 1 = `POKEPARTY_GetMember(ppd, pos)`, ppt 2 = `ppd + boxSize*pos` (`ov131/p_status.c:1342-1350`); use `P_STATUS_GetPokeParaSource` for the full param (array entries are expanded into `tempPP`, `:1352-1365`). The fields to draw are the same `POKE_TOOL_GetPartyValue` ids as the party list, plus: exp 0x08, friendship 0x09, ability 0x0A, nature 0x70, OT name 0x8E, OT id 0x07, ball 0x98, met level 0x99, met location 0x96, EVs 0x0D-0x12, IVs 0x46-0x4B, attack/defense/speed/sp.attack/sp.defense 0xA2-0xA6, type 1 0xAE (`arm9/poke_tool_param.h`). The move list on page 1 uses ids 0x36+slot (`p_sta_skill.c:1118` region, `P_STATUS_SearchPokemon` uses `+54`) and PP ids 0x3A, 0x42. Page 1 work `PStaSkillWork` (0x128, `p_sta_skill.c:149-186`): `cursorSlot` 0x29 (0xFF none), `heldSlot` 0x2A, `isMoveHeld` 0x14, `slotCount` 0x2B (4, or 5 when a move is to be learned). Ribbon page `PStaRibbonWork` (`p_sta_ribbon.c:165-201`): `scrollPos` 0x14, `selectedRow` 0x58, `owned[]` at 0x138. A Pokemon with no ribbons has no ribbon tab (`hasRibbon`). Egg: only page 0 (`isEgg`).

### Input

Reacts on the press. The eight buttons come from ROM data `_TouchHitRects` at overlay 131 `0x021DC114`, tested by `P_STATUS_UpdateTouch` (`ov131/p_status.c:859-863`) and acted on in `P_STATUS_CheckTouch` (`:954-1037`):

| Index | Rectangle (x, y) | Action | Tap centre |
|---|---|---|---|
| 0 | x 0-40, y 168-192 | page 0 (info) | (20, 180) |
| 1 | x 40-80, y 168-192 | page 1 (skills); not for eggs | (60, 180) |
| 2 | x 80-120, y 168-192 | page 2 (ribbons); only when `hasRibbon` | (100, 180) |
| 3 | x 120-144, y 168-192 | Y toggle for the page (needs `canUseButtons`); what it switches on each page is not in the shared code (unverified) | (132, 180) |
| 4 | x 144-168, y 168-192 | previous Pokemon (up arrow) | (156, 180) |
| 5 | x 168-192, y 168-192 | next Pokemon (down arrow) | (180, 180) |
| 6 | x 200-224, y 168-192 | X: close with `retSel = 2` (needs `canUseButtons`) | (212, 180) |
| 7 | x 232-255, y 168-192 | B: close with `retSel = 1` | (244, 180) |

Keys (`P_STATUS_CheckKey`, `:872-952`): down 0x80 next Pokemon, up 0x40 previous, right 0x10 next page, left 0x20 previous page, B close, X close (needs `canUseButtons`), Y toggle. Next/previous skip entries that are empty, and on the ribbon page skip Pokemon without ribbons (`P_STATUS_SearchPokemon`, `:1169-1191`).

Page 1 move slots (`P_STA_SKILL_GetSlotTouchRect`, `p_sta_skill.c:1472-1479`; tile positions from ROM data `P_STA_SKILL_SlotTilePos` at overlay 131 `0x021DC2B8`: (1,2), (1,6), (1,10), (1,14), (1,20)): rectangles x 8-144, y 16-48, 48-80, 80-112, 112-144, and slot 4 y 160-192 (only when a fifth move is to be learned). A press on an occupied slot selects it and shows the move detail; a drag of more than 8 px vertically picks the move up to swap it with another slot (`p_sta_skill.c:940-1010`); B (index 7) returns from the move detail to the stats view (`:922-936`). Ribbon page: a press with x in 9-143 selects/drag-scrolls the list (`p_sta_ribbon.c:504-520`). The Pokemon sprite on the page reacts to touch (petting), `p_sta_sub.c:826-890`.

---

## 7. Battle (`ov093` main, `ov094` view and input, `ov095` command screens, `ov197` bag, `ov198` party)

The sub-headings below are the battle states a recipe can meet. "Battle: common facts" must be read first.

### Battle: common facts (read first)

Paths: C is `C:/aletheia-w20/src/game/<path>:LINE`. ROM data is quoted as `ovNNN 0xADDR` and was dumped from the decompressed overlay files of the aletheia extract (`extract/overlays_dec/y9_NNNN.bin`, or `extract/overlays/y9_NNNN.bin` for overlays that are not compressed, such as ov095). Hit tables are `top,bottom,left,right` u8, inclusive on both ends, first byte 0xFF ends the table (arm9/touchpanel.c:45-49, 327-333, 336-355). Every rectangle below is a bottom-screen DS pixel rectangle. "Centre" means `((left+right)/2, (top+bottom)/2)`, clipped to y <= 191.

Overlay ids (decimal): ov092 = battle return event, ov093 = battle main (BTL_MAIN, BTLV_CORE, BTLV_SCU), ov094 = battle view (BTLV_EFFECT, BTLV_INPUT, gauges), ov095 = command select view (BTLV_SCD), ov096 = loaded only when not Wi-Fi (btlv_core.c:507-516), ov196 = shared battle UI tools (`b_app_tool`), ov197 = battle bag, ov198 = battle party list, ov223 = PSEL (not a battle screen, see the end).

#### Detection of "a battle is running"

| Check | Evidence | Reliability |
|---|---|---|
| A `Proc` on the game system's proc stack (`_GameSystemGlobal.gameSystem->procSys->current`, walk `prev`) has `overlayId == 93` and `data == d_021EFDAC` (the BTL_MAIN proc data). | The battle is started with `GAME_EVENT_CallProc(event, ov093_id, d_021EFDAC, btlSetup)` (ov010/event_battle.c:571). `Proc` and `ProcSys` layout: arm9/procsys.h. | Best. `proc->work` is the `BtlMain` (0x474 bytes), allocated in `BTL_MAIN_ProcInit` state 0 (ov093/btl_main.c:385). Valid once `proc->seq >= 1` and `proc->work != 0`. |
| `gBtlvEff != 0` (ov094 data 0x0220AF20; set in `BTLV_EFFECT_Init` ov094/btlv_effect.c:747-748, cleared at :826). | `BTLV_EFFECT_GetBattleMain()` returns `gBtlvEff->setup.battle` (btlv_effect.c:1524-1527). It dereferences `gBtlvEff`, so test the pointer first. | Good for "the battle view is built"; false during the first battle frames and after exit. |
| `bw_overlay_is_loaded(93)` | ov093 is also loaded outside battle by ov006/event_wificlub.c:505 and ov008/event_battle_rec_save.c:303. | Weak alone. |
| `bw_overlay_is_loaded(94)` and `(95)` | `BTL_MAIN_ProcInit` calls `BTLV_CORE_LoadOverlays(0x13)` (btl_main.c:387), which loads ov094, ov095 and (non Wi-Fi) ov096 (btlv_core.c:507-516). They stay loaded for the whole battle. | Good second check. |

#### Where the objects live at run time

Chain: `BtlMain` (proc work) -> `viewCore` (BtlvCore, +0x004) -> `auxView` (BtlvAuxView, core+0x180) -> `input` (BtlvInput, auxView+0xB0).
Field offsets are DS 32-bit; the eidos build is 32-bit (i686), so they hold. Verify with the sizeof checks in the headers.

| Object | Where | Fields a recipe needs |
|---|---|---|
| `BtlMain` | `proc->work` of the ov093 proc; also `BTLV_EFFECT_GetBattleMain()` | ov093/btl_main_types.h:112. `setup` +0x000 (BattleSetupParam: `rule`, `competitor`, `mapMatrix` = bag cursor), `viewCore` +0x004 (0 until `BTL_MAIN_CreateViewCore`, btl_main.c:782-791), `clients[4]` +0x010, `sides[2]` +0x0C8, `battleParties[4]` +0x298. |
| Rule | `BTL_MAIN_GetRule(main)` = `main->setup->rule` (btl_main.c:1699-1702) | 0 single, 1 double, 2 triple, 3 rotation (ov093/btl_calc_types.h:92-98). |
| Competitor | `BTL_MAIN_GetCompetitor` = `setup->competitor` (btl_main.c:1758-1761) | 0 wild, 1 trainer, 3 link; 0 and 4 hide the opponent party balls (btlv_scd.c:610). Other values unverified. |
| `BtlvCore` | `main->viewCore` | ov093/btlv_core_types.h:182 (size 0x188). `stepFn` +0x014 (non-zero while the action, move, target or rotation menu step runs), `pokeParam` +0x0BC (the Pokemon that is choosing), `actionResult` +0x0C4, `bag` (BtlvBagWork) +0x0C8, `plist` (BtlvPlistWork) +0x10C, `subStep` +0x174 (bag and party sub screens), `msgBuffer` +0x0AC (text of the last battle message, Strbuf), `auxView` +0x180. |
| `BtlvAuxView` | `core->auxView` | ov095/btlv_scd_types.h:74. `pokeParam` +0x6C, `action` +0x70, `canBack` +0x8B, `ownParty` +0xA8, `input` +0xB0, `targets` +0x80 (BtlvTargetSet: `viewPos[6]` of the legal targets, a list of battle slots), `moveTargetKind` +0x8A. Created by `BTLV_SCD_Create` (btlv_scd.c:259-282). |
| `BtlvInput` | `auxView->input` (created in `BTLV_SCD_CreateInput`, btlv_scd.c:284-299, called from `BTLV_CORE_StepOpeningEntry` btlv_core.c:660) | ov094/btlv_input_shared_types.h:50. `screen` +0x058, `battleType` +0x050, `flags` +0x068 (`busy`, `inputLock`), `enabled[8]` +0x25A (per button: tap is ignored when 0), `enabled2[4]` +0x262, `struggle` +0x220, `moveIds[4]` +0x26C, `partyIdx` +0x27C, `transformed` +0x280. |
| `BtlPokeParam` (one battle Pokemon) | `BTLV_CORE_GetPokeParamAtViewPos(core, viewPos)` (btlv_core.c:1291) or `BTL_SIDE_GetPokeAtPos(sides, slot)` | ov093/btl_pokeparam_types.h:120. `species` +0x0C, `maxHp` +0x0E, `hp` +0x10, `heldItem` +0x12, `level` +0x18, `type1/type2` +0xF8/+0xF9, `gender` +0xFA, `moves[4]` +0x104 (each `cur` and `saved` MoveInfo: `id`, `pp`, `ppMax`, `ppUp`), `moveCount` +0x140, `mon` +0x00 (the PokemonParam, nickname via `POKE_TOOL_GetPartyValue`). Status: `BTL_POKEPARAM_GetMajorStatusIndex(p)` non-zero means a major status. Use the accessors `BTL_POKEPARAM_GetValue(p, 13 / 14 / 15)` for hp / max hp / level (btl_calc_types.h:82-87). |
| Move data | `WAZA_TOOL_GetType(move)`, `WAZA_TOOL_GetCategory(move)`, `WAZA_TOOL_GetDamageType(move)`, `WAZA_TOOL_GetPower(move)` (arm9/waza_tool.c:94-110, getter switch at :198-216: param 0 type, 1 category, 2 damage type, 3 power, 4 accuracy, 5 base PP) | The move menu itself uses `WAZA_TOOL_GetParam(id, 0)` for the type icon (ov094/btlv_input.c:2771 area, `BTLV_INPUT_DrawWazaButtons`). |

#### Battle view positions

View positions are 2..7; even = the local player's side, odd = the opponent's. Evidence: `row = (userViewPos - 2) / 2` for the local mon (ov095/btlv_scd.c:946); `BTLV_SCD_ShowTargetMenu` fills `rows[viewPos - 2]` (btlv_scd.c:877); `gBtlvScdViewPosByRegion` = `02 03 04 05 06 07` (ov095 0x0689D82C). Single: own 2 vs foe 3. Double: own 2, 4 and foe 3, 5. Triple: own 2, 4, 6 and foe 3, 5, 7. The parity rule is derived from those; the exact left/right placement on the 3D field is unverified.

#### Pad buttons of every command screen (BTLV_INPUT)

Source: `BTLV_INPUT_ProcessButtonSelection` (ov094/btlv_input.c:3334-3463). The first d-pad or button press when `*input->cursorVisible == 0` only shows the cursor (line 3399-3402, mask 0xCF3). Then d-pad moves through the neighbour rows (`s8[12]`: bytes 6..9 = up, down, left, right, byte 10 = result of A, byte 11 = result of B; a negative B value only works when `restrictedChoice` is set), A takes `row[10]` if `input->enabled[r] == 1`. Touch uses `TOUCHPANEL_HitTrg` (the first frame of a touch, `BTLV_INPUT_ProcessTouchInput` btlv_input.c:1396-1455), so a tap needs at least one frame of stylus down on a rectangle. All input is ignored while `BTLV_INPUT_IsInputBlocked(input) != 0` (busy, a palette fade request, or `inputLock`; btlv_input.c:1594-1599). A recipe must wait for it to be 0 before queueing a tap.

### Battle: scene states (which screen is up)

#### Detection

Read `core = main->viewCore`, `input = core->auxView->input`.

| Recipe state | Condition | Evidence |
|---|---|---|
| Battle not ready | `main == 0` or `main->viewCore == 0` or `core->auxView->input == 0` or `gBtlvEff == 0` | `viewCore` set at btl_main.c:782-791; input at btlv_core.c:660 |
| Message only / animation running | `input->screen == 0` (BTLV_INPUT_SCREEN_CLOSE) and `core->stepFn == 0` and `core->subStep == 0` | screen values: ov095/btlv_scd_types.h:111-117; `BTLV_INPUT_GetScreen` btlv_input.c:1601-1604 |
| Animation running (subset) | `BTLV_EFFECT_IsBusy() != 0` (effect script, damage tint, hit or sprite transition tasks; btlv_effect.c:1055-1063) | |
| Waiting for a message key | `BTLV_CORE_WaitMessage(core)` is 0 while text is printing (`BTLV_SCU_UpdateMessage`, btlv_core.c:2152). Text is in `core->msgBuffer` | btlv_core.c:2105-2109 |
| Action menu | `input->screen == 1`, and `core->stepFn == BTLV_CORE_StepActionMenu` | btlv_core.c:1297-1309, 1351-1361; screen set at btlv_scd.c:638 (`BTLV_INPUT_ChangeScreen(.., ACTION, &info)`) |
| Move menu | `input->screen == 2` | btlv_scd.c:704 |
| Target menu | `input->screen == 3` | btlv_scd.c:858-905 |
| Yes/no choice (shift prompt, learn move confirm and similar) | `input->screen == 4` | `BTLV_SCD_OpenChoiceScreen` btlv_scd.c:1056-1060, `BTLV_CORE_StartYesNoMenu` btlv_core.c:2397-2409 |
| Rotation menu (rule 3) | `input->screen == 5` | btlv_scd.c:749-763 |
| Counter screen | `input->screen == 6` | btlv_scd.c:1127-1135. What triggers it is unverified (`BTLV_CORE_SetCounterDisplay`, btlv_core.c:2514) |
| Battle bag | `bw_overlay_is_loaded(197)` (ov196 is loaded together), `core->subStep` in 2..3 | btlv_core.c:1652-1710 |
| Battle party list | `bw_overlay_is_loaded(198)`; `core->subStep` 1..2 in `BTLV_CORE_PollPartyMenu` (btlv_core.c:1544-1588) or 5 in the bag flow (btlv_core.c:1679-1696, party prompt for "use item on") or the move-forget flow (btlv_core.c:2428-2462) | which flow: `core->plist.mode` (0x1F): 3 = item target (btlv_core.c:1636), 4 = move forget (btlv_core.c:2418), 5 = move highlight (btlv_core.c:1590), others come from `BtlSlotChoices.mode` (btlv_core.c:1514-1520) |
| Input not ready | `BTLV_INPUT_IsInputBlocked(input) != 0` | btlv_input.c:1594-1599 |
| Opening demo (tutorial battle) | `BTLV_CORE_StepOpeningDemo` (btlv_core.c:689-815): back-only action menu with a finger cursor, then a bag opened with mode 2 (btlv_core.c:799). The bag in this demo has `BagMenu.mode == 2` | |

Which multi-turn flow asked for a screen (fight, switch, item, run) is visible from the client's sub function: `BTL_CLIENT_StepSelectAction` (ov093/btl_client.c:1945-2040), `BTL_CLIENT_StepSelectMove` (:2188), `BTL_CLIENT_StepSelectItem` (:2386-2432). Unverified: a stable way to read the client's current `subFn` from outside; `core->stepFn` and `input->screen` are enough for the screens above.

#### State to draw (all screens)

| What | How |
|---|---|
| Own and foe active mon (name, species, level, HP, max HP, status, item, gender) | for each view position `vp` in 2..7: `BTLV_CORE_GetPokeParamAtViewPos(core, vp)`; skip empty/fainted (`BTL_POKEPARAM_IsFainted`). Nickname: `POKE_TOOL_GetPartyValue(BTL_POKEPARAM_GetDisplayMon(p), 111, 0)` is what the action menu uses for the Pokemon label (btlv_scd.c:582). Unverified: that 111 is the nickname value in the shared value table; check against the species name path in `WORDSET_SetSlotMonNickname`. |
| Party ball status (own side and foe side) | built in `BTLV_SCD_OpenActionMenuStep` (btlv_scd.c:531-640): `BtlvPartyBall.status`: 0 unable, 1 healthy, 2 fainted, 3 major status (btlv_scd_types.h:137-140). Recompute the same way from `BTL_SIDE_GetSlotOfClientThunk` + `BTL_SLOT_GetPokeParam`. |
| Which Pokemon is choosing | `core->pokeParam` (+0x0BC) and its view position `BTL_MAIN_GetViewPosOfSlot(main, BTL_MAIN_GetTrainerSlotOfPoke(main, sides, BTL_POKEPARAM_GetPokeId(p)))` (btlv_scd.c:711-713 style) |
| Back button offered | `core->canBack` / `auxView->canBack`: `slotIndex > slotsDone` (btl_client.c:1978). The fourth action button reads Run when the first mon chooses and Back for later mons (`BTLV_INPUT_DrawRunButtonText`, btlv_input.c:3594; in `BTL_CLIENT_StepSelectAction`, result 4 with `slotIndex != slotsDone` steps back, btl_client.c:2018-2033). |

### Battle: action menu (Fight / Bag / Pokemon / Run)

#### Detection
`input->screen == 1` (see table above). Payload built at ov095/btlv_scd.c:531-640 (`BtlvActionMenuInfo`, btlv_scd_types.h:143-160).

#### State
- Button results delivered to `BTL_CLIENT_StepSelectAction`: 1 = Fight (go to the move menu), 2 = Bag (item), 3 = Pokemon (switch), 4 = Run or Back (btl_client.c:1993-2037).
- Button enable flags: `input->enabled[0..3]`; `enabled[1]` (Bag) is `BTLV_EFFECT_IsRecordAllowed()` (btlv_input.c:1251; set in `ChangeScreen` case 1). A tap on a disabled button is ignored (btlv_input.c:1429-1435).
- Bag in link battles with no record: closes and re-opens the menu instead (btl_client.c:2006-2011).
- Own and foe party balls, active mon labels: `BtlvActionMenuInfo` as above; species and nickname values per view position at +0x98/+0xA4.

#### Input
Region table `gBtlvScdActionRegionsA` (ov095 0x0689D8D4: key rects at 0x0689DA28, neighbour rows `gBtlvScdActionRegionsB` at 0x0689DF64) and `gBtlvScdActionButtonByRegion` (0x0689D828) = `01 02 03 04`.

| Region | Rectangle (top,bottom,left,right) | Centre to tap | Result | Meaning |
|---|---|---|---|---|
| 0 | 24..144, 0..255 | (127, 84) | 1 | Fight |
| 1 | 144..192, 0..80 | (40, 167) | 2 | Bag |
| 2 | 144..192, 176..255 | (215, 167) | 3 | Pokemon |
| 3 | 152..192, 88..168 | (128, 171) | 4 | Run (or Back for the second and third mon of a turn) |

Pad: d-pad moves, A selects, B only works when `restrictedChoice` is set (row[11] = -3 on region 0, which moves the cursor to the fourth button, ov095 0x0689DF64+11). Touch handling code: `BTLV_SCD_WaitActionTouchStep` (btlv_scd.c:645-673). A Run result also writes `BTL_ACTION_SetEscape` and closes the screen.

### Battle: move menu

#### Detection
`input->screen == 2`, `core->stepFn == BTLV_CORE_StepMoveMenu` (btlv_core.c:1371-1398). Rotation battles use screen 5 instead (below).

#### State
Payload `BtlvMoveMenuInfo` (btlv_scd_types.h:163-170) built at btlv_scd.c:687-712 from the choosing mon: `moveId[4]`, `pp[4]`, `ppMax[4]` via `BTL_POKEPARAM_GetMovePP(p, i, &pp, &ppMax)`; `transformed` = `BTL_POKEPARAM_IsTransformed`. Per-button enable flags: `input->enabled[i] = (pp != 0)`, `enabled2[i] = (id != 0)` (btlv_input.c:2801-2802). `input->struggle == 1` means no usable move (`BTLV_INPUT_InitButtonState`, btlv_input.c:2903-2925): the pad/touch table then is the "struggle" set `sBtlvInputSectionData.db`. Move type: `WAZA_TOOL_GetType(id)`; the type icon file is chosen as `d_0220A5F0[type]` (btlv_input.c:2771).
Unverified: restrictions such as Taunt, Torment, Encore or Choice lock are not checked on this screen; they are checked by the client after the tap (`BTL_CLIENT_StepSelectMove`, btl_client.c:2188 onward, not read).

#### Input
`BTLV_SCD_WaitMoveTouchStep` (btlv_scd.c:714-747). Tables chosen at :719-725. Result `0..3` = move slot, `4` = Back (`BTL_ACTION_SetNone`), `5` = skip (triple battles only, `BTL_ACTION_SetSkip`). Bit 0x8000 in the result is the "L held" slot flag (move info or similar; unverified).

Single, double and rotation use `gBtlvScdMoveRegionsSingleA` (key rects ov095 0x0689DB70):

| Region | Rectangle | Centre | Meaning |
|---|---|---|---|
| 0 | 32..80, 0..128 | (64, 56) | move 1 |
| 1 | 32..80, 128..255 | (191, 56) | move 2 |
| 2 | 80..128, 0..128 | (64, 104) | move 3 |
| 3 | 80..128, 128..255 | (191, 104) | move 4 |
| 4 | 144..192, 176..255 | (215, 167) | Back |

Triple battles when the chooser is not in the middle (`rule == 2 && viewPos != 4`, btlv_scd.c:719-725) use `gBtlvScdMoveRegionsDoubleA` (ov095 0x0689DD54): the same five rectangles plus region 5 = 144..192, 0..80, centre (40, 167), meaning "skip / shift" (`BTL_ACTION_SetSkip`, btlv_scd.c:741-744). The table is called "Double" in the symbol names but is selected only for `rule == 2`, which is triple (BTL_RULE_TRIPLE = 2).

Pad: d-pad moves between the four moves (rows at ov095 0x0689E054, e.g. move 0: down 2, right 1), A selects, B selects Back (row[11] = 4).

### Battle: target menu

#### Detection
`input->screen == 3`, `core->stepFn == BTLV_CORE_StepTargetMenu` (btlv_core.c:1312-1325). In a triple battle the menu may be skipped automatically: `BTLV_SCD_OpenTargetMenuStep` pre-fills the target (btlv_scd.c:914-935).

#### State
`BTLV_SCD_ShowTargetMenu` (btlv_scd.c:858-895) builds `BtlvTargetMenuInfo`: `rows[viewPos - 2]` with `hp`, `hpMax`, `flags` (bits 0..1 gender, bit 2 present), `state` (1 healthy, 2 fainted, 3 major status), `monWord` (the display mon), plus `userViewPos` and `targetKind` (`WAZA_TOOL_GetParam(move, 0x1B)`, 14 for the "reaches both" flag). Legal targets are in `auxView->targets.viewPos[0..count)` as battle slots (btlv_scd.c:975-1027, `BTLV_SCD_BuildTargetSet`). A tap on an illegal position is ignored (`BTLV_SCD_PickTargetFromTouch`, btlv_scd.c:841-856). `auxView->moveTargetKind` +0x8A is the kind used to pick the table.

#### Input
`BTLV_SCD_WaitTargetTouchStep` (btlv_scd.c:936-973). Table selection: rule != 2 uses `gBtlvScdTargetRegionsSingleA/B[row][kind]` (ov095 0x0689E5D8 / 0x0689E560), `stopIndex = 4`; rule == 2 uses `gBtlvScdTargetRegionsDoubleA/B[row][kind]` (0x0689E650 / 0x0689E704), `stopIndex = 6`. `row = (userViewPos - 2) / 2`. The region index maps to a view position through `gBtlvScdViewPosByRegion` (`02 03 04 05 06 07`, btlv_scd.c:845-848).

Single and double battles (rows 0 and 1 share one rectangle set for all 15 kinds, ov095 0x0689DBA0; row 2 only occurs in triples):

| Region | Rectangle | Centre | View position | Meaning |
|---|---|---|---|---|
| 0 | 88..128, 0..128 | (64, 108) | 2 | own side, first slot |
| 1 | 32..88, 128..255 | (191, 60) | 3 | foe, first slot |
| 2 | 88..128, 128..255 | (191, 108) | 4 | own partner slot |
| 3 | 32..88, 0..128 | (64, 60) | 5 | foe, second slot |
| 4 | 144..192, 176..255 | (215, 167) | none | Back (returns to the move menu) |

Triple battles (the same key table, ov095 0x0689DDC4, for every row and kind):

| Region | Rectangle | Centre | View position |
|---|---|---|---|
| 0 | 88..128, 8..88 | (48, 108) | 2 |
| 1 | 32..88, 168..248 | (208, 60) | 3 |
| 2 | 88..128, 88..168 | (128, 108) | 4 |
| 3 | 32..88, 88..168 | (128, 60) | 5 |
| 4 | 88..128, 168..248 | (208, 108) | 6 |
| 5 | 32..88, 8..88 | (48, 60) | 7 |
| 6 | 144..192, 176..255 | (215, 167) | Back |

Moves that need no choice (target kinds with `count == 0`) skip the screen: `outSlot = 6` (btlv_scd.c:849-853). The `vals`/neighbour data (colours and pad neighbours per kind) was not decoded; unverified. Pad: d-pad moves between target buttons, A selects, B goes to Back through the neighbour rows.

### Battle: rotation menu (rule 3)

#### Detection
`input->screen == 5`; `BTL_MAIN_GetRule == 3`. Started by `BTLV_CORE_StartMoveMenuForEntry` (btlv_core.c:1380-1390) -> `BTLV_SCD_StartRotateMenu` (btlv_scd.c:474-485).

#### State
`BtlvRotateSet` (btlv_scd_types.h:59-64): three candidates `cand[3]` (`pokeParam`, `info[4]`), the first is the current front mon, the other two are the rotate-left and rotate-right mons. The screen shows the moves of one candidate at a time; `input->flags.slotIndex` (BtlvInputFlags, btlv_input_shared_types.h:15-35) is the candidate currently shown, and `BtlvInput.pokeParam[3]` (+0x2A4) are the three mons. Move data per candidate: `BTL_POKEPARAM_GetMoveId/GetMovePP` on `cand[i].pokeParam`.

#### Input
Pad and touch go through `BTLV_INPUT_ProcessKeyInput` (btlv_input.c:1512-1590) with `sBtlvInputSectionData.da` (ov094 0x0220A39C; key rects at 0x0220A4CC):

| Region | Rectangle | Centre | Meaning |
|---|---|---|---|
| 0..3 | same as the single move menu (32..80 / 80..128 by 0..128 / 128..255) | (64,56) (191,56) (64,104) (191,104) | move 1..4 of the shown candidate |
| 4 | 144..192, 176..255 | (215, 167) | Back |
| 5 | 152..192, 0..80 | (40, 171) | show candidate 5 (left rotate candidate) |
| 6 | 152..192, 80..160 | (120, 171) | show candidate 6 (right rotate candidate) |

Values 5 and 6 do not finish the screen: they run `BTLV_INPUT_StartScreenTransition` (btlv_input.c:2450-2461), which changes `slotIndex` through `d_0220A38C[k][screen - 5]`. Picking a move calls `BTL_ACTION_SetRotate(&rotateAction, key)` with `key = sBtlvInputSectionData.key[slotIndex]` = 1, 2, 3 for the three candidates and `BTL_ACTION_SetFight` (btlv_scd.c:765-798). If `input->struggle == 1` the "struggle" set `db` (ov094 0x0220A3A8, rects at 0x0220A48C) is used: moves 0..3 all map to one rectangle 56..104, 64..192 (centre (128, 80)). Which region is the left or the right candidate on screen is unverified (no label text was read).

### Battle: yes/no choice (screen 4)

Detection: `input->screen == 4`; started by `BTLV_CORE_StartYesNoMenu` (btlv_core.c:2397-2409) with two message texts (`BtlvYesNoWork.yesText/noText`). Poll: `BTLV_SCD_PollChoiceScreen` (btlv_scd.c:1063-1085). Regions `gBtlvScdChoiceRegionsA` key rects (ov095 0x0689D8F8): region 0 = 32..80, 0..255 (centre (127, 56), first text = "yes"); region 1 = 80..128, 0..255 (centre (127, 104), second text). The returned index is the region. What each caller uses this for (shift prompt before sending out the next mon, learn-move prompt) was not traced: unverified. Pad B: neighbour rows at ov095 0x0689DC48 (not decoded).

### Battle: counter screen (screen 6)

Detection: `input->screen == 6`. Regions (ov095 0x0689D934 -> key rects 0x0689D958): 0 = 144..192, 0..80 centre (40, 167); 1 = 144..192, 176..255 centre (215, 167); 2 = 152..192, 88..168 centre (128, 171). `BTLV_SCD_PollCounterTouch` (btlv_scd.c:1133-1136) with `BTLV_INPUT_ProcessTouchInput`, screen 6 branch (btlv_input.c:1415-1431): the returned region is passed through `input->enabled[r]`. The meaning of the three buttons and when the screen appears (`BTLV_CORE_PollCounterInput`, `SetCounterDisplay`) is unverified.

### Battle: battle bag (ov197)

#### Detection
`bw_overlay_is_loaded(197)`. Sequence in `BTLV_CORE_PollBagMenu`: slide in, load ov196, load ov197 and call `B_BAG_MAIN_Create(&core->bag)` (btlv_core.c:1652-1670). Alternatively `core->subStep` is 3 and `core->bag.done == 0` while the bag runs; `core->bag.done != 0` when an item was picked or the screen closed (btlv_core_types.h:129).
The bag has no global pointer. Its work (`BagWork`, 0x5A4 bytes, ov197/b_bag_main.c:247) is allocated in `B_BAG_MAIN_Create` (b_bag_main.c:1306-1349) and driven by a task in the battle view task system: `TCB_AddTask(BTLV_EFFECT_GetOwner(), B_BAG_MAIN_Main, p, 0x64)`. To find it, walk the task list of `BTLV_EFFECT_GetOwner()` (struct `TcbSystem` / `TcbTask`, arm9/tcb_list.c:25-43: `sentinel` +0x08, task `next` +0x08, `func` +0x14, `work` +0x10) and take the task whose `func` is `B_BAG_MAIN_Main`; `work` is the `BagWork`. This is the only run-time path found; unverified that the walk is safe while the task list is being modified (read it between game frames).

#### State
Input struct handed in (`BagMenu`, same memory as `core->bag`, b_bag_main.c:223-245 and btlv_core_types.h:109-134): `myItem` +0x00 (player bag), `isComm` +0x08 (this is the bag "mode": 0 normal, 1 link battle with item point costs, 2 tutorial demo; 3 is tested in `B_BAG_MAIN_Create` but nothing sets it, unverified), `pointCounter` +0x14, `lastItem` +0x15 (a u8 the client fills with the sum of slot flags before the current slot, NOT an item id; see btl_client.c:2390-2401 `itemId = BTL_CLIENT_SumSlotFlagsBeforeCurrent`), `selectedItem` +0x16 (the result), `selectedItemPage` +0x2A (pocket of the result; 4 = cancelled, see StateSelectPocket), `done` +0x19, `myItemSave` +0x1C (the bag cursor block), `buttonMode` +0x28 (0..4: 3 = not the last chooser, 1 = status flag 0x40, 2 = wild battle with more than one foe, 4 = lone foe semi-invulnerable; btlv_core.c:1626-1634; effect on which items are blocked unverified), `pocketCursorPos[4]` +0x30 (slot 0..5 within the page, per pocket), `pocketScrollPos[4]` +0x38 (page number, per pocket).
`BagWork` fields (b_bag_main.c:247-291): `itemData` +0x000 (the BagMenu), `rows[4][36][2]` +0x150 (u16 pairs `{itemId, quantity}`: 4 pockets, up to 36 entries each), `state` +0x530, `nextState` +0x531, `page` +0x532 (0 = pocket screen, 1 = item list, 2 = confirm; this indexes the cursor table), `pocket` +0x533, `countPerPocket[4]` +0x535, `pagePerPocket[4]` +0x539 (last page index per pocket; 0 means a single page), `keyItem` +0x540 (the "last used item" id, 0 if none), `keyItemSlot` +0x542 (its pocket), `cursor` +0x144 (CURSOR_MOVE object: `cur` is the highlighted entry; struct `Cursor` in arm9/cursor_move.c).
Pockets (index = `pocket`): 0 HP/PP RESTORE, 1 STATUS RESTORE, 2 POKE BALLS, 3 BATTLE ITEMS. Evidence: header labels in message bank a/0/0/2 member 17 (`MSGDATA_CreateFromArchive(0, 2, 17)`, b_bag_main.c:1938), strings 22..27 printed by `B_BAG_MAIN_DrawInfoBox` per pocket (b_bag_main.c:690-700): "HP/PP RESTORE", "STATUS RESTORE", "POK\e BALLS", "BATTLE ITEMS"; the other labels are 0 "HP/PP", 1 "RESTORE", 2 "STATUS", 3 "RESTORE", 6 "BATTLE ITEMS", 7 "POK\e BALLS", 8 "LAST USED ITEM", 28 "/", 31 "USE". Item filling: `B_BAG_MAIN_BuildPocketLists` (b_bag_main.c:838-885) reads game pockets {0,1,3} (`d_0220DB3C` = `00 01 03`) and a per-item bit mask from archive 0xE8 member 0; mask bit k goes to bag pocket `d_0220DB3F[k]` = `02 03 00 01`.
Items on the list page: `B_BAG_MAIN_GetSlotItem(p, n)` = `rows[pocket][n + page*6].itemId` (b_bag_main.c:911-921), where `page = BagMenu.pocketScrollPos[pocket]` (+0x38) and six items per page, row-major (slot k is row k/2, column k%2). Quantity is the second u16. The "x" quantity text is message 10, item name message 9 (tags `{F000:0109}` and `{F000:0202}`).
Modes: in link battles (`isComm == 1`) the bag opens directly on the item list of pocket 0 and shows the item point cost (`B_BAG_MAIN_GetItemFlag`, b_bag_main.c:1273 area); in the tutorial demo (`isComm == 2`) it shows an "other" list of 46 fixed items and a finger cursor (b_bag_main.c:884-897, 1210-1245).

#### Input
The bag uses `CURSOR_MOVE` objects; each entry of its table is 12 bytes (`CursorEntry`: neighbours up, down, left, right, then the touch rectangle; arm9/cursor_move.c:46-49, 334-360 `CURSOR_MOVE_PollTouch` uses `TOUCHPANEL_HitTrg` per entry, in table order). The three tables are selected by `BagWork.page` (b_bag_main.c:1990-1997, `d_0220DF9C[page]`, ov197 0x0220DF9C).

Page 0, pocket screen (ov197 0x0220DFFC); result handling `B_BAG_MAIN_StateSelectPocket` (b_bag_main.c:1436-1480):

| Entry | Rectangle | Centre | Meaning |
|---|---|---|---|
| 0 | 8..71, 0..127 | (63, 39) | pocket 0 HP/PP RESTORE |
| 1 | 80..143, 0..127 | (63, 111) | pocket 1 STATUS RESTORE |
| 2 | 8..71, 128..255 | (191, 39) | pocket 2 POKE BALLS |
| 3 | 80..143, 128..255 | (191, 111) | pocket 3 BATTLE ITEMS |
| 4 | 152..191, 8..207 | (107, 171) | LAST USED ITEM (only acts when `keyItem != 0`) |
| 5 | 152..191, 216..255 | (235, 171) | Back (closes the bag, `selectedItem = 0`, `selectedItemPage = 4`) |

Page 1, item list (ov197 0x0220E050); `B_BAG_MAIN_StateSelectItem` (b_bag_main.c:1482-1591):

| Entry | Rectangle | Centre | Meaning |
|---|---|---|---|
| 0..5 | 8..55 / 56..103 / 104..151 by 0..127 (left) and 128..255 (right) | (63,31) (191,31) (63,79) (191,79) (63,127) (191,127) | item slot 0..5 of the page (acts only if the slot holds an item) |
| 6 | 152..191, 216..255 | (235, 171) | Back (to the pocket screen, or closes in modes 1 and 3) |
| 7 | 152..191, 0..39 | (19, 171) | previous page (acts only if the pocket has more than one page) |
| 8 | 152..191, 40..79 | (59, 171) | next page (same condition) |

Page 2, confirm (ov197 0x0220DFD8); `B_BAG_MAIN_StateChooseItem` (b_bag_main.c:1593-1620): entry 0 = 152..191, 8..207 centre (107, 171) = USE (sets `itemId = GetSlotItem(...)`, `slot = pocket`); entry 1 = 152..191, 216..255 centre (235, 171) = Back to the list.

Pad: d-pad follows the neighbour bytes of each entry, A takes the highlighted entry, B returns -2 which is Back (arm9/cursor_move.c:141-190; in the bag `-2` in each state is the same as Back). On the item list L and R are not used; pages change by entries 7 and 8 or d-pad left/right from the edge (case -5, -6 in StateSelectItem).
Button states of the left/right arrows (disabled when `pagePerPocket[pocket] == 0`) are read from `BagWork.pagePerPocket`.

After USE the party prompt (ov198, mode 3) opens unless the item needs no target (`BTLV_CORE_PollBagMenu` case 4, btlv_core.c:1676-1690).

### Battle bag: last item used

- Stored in the save, in the bag cursor block (`struct BagCursor`, arm9/myitem_savedata.c:54-63): `lastBattleItem` u16 at +0x28, `lastBattleItemPocket` u16 at +0x2A (also `battlePocketPosition[4]` +0x18, `battlePocketScroll[4]` +0x20). Accessors `MYITEM_SAVEDATA_GetLastBattleItem/Pocket` (myitem_savedata.c:589-597), `MYITEM_SAVEDATA_SetLastBattleItem` (:609-613).
- Reach it at run time from `core->bag.myItemSave` (+0x1C of the bag work), which `BTLV_CORE_StartBagMenu` sets to `BTL_MAIN_GetMapMatrix(main)` (btlv_core.c:1604; `BTL_MAIN_GetMapMatrix` returns `main->setup->mapMatrix`, btl_main.c:2397-2400, a misnamed accessor of the bag cursor).
- Set: `BTLV_CORE_SaveBagCursors(core)` (btlv_core.c:1739-1745) writes the pocket cursors and `selectedItem` / `selectedItemPage` as the last item, but only if `selectedItem != 0`, `plist.modeSlot != 6` and `bag.isComm != 1` (never in link battles). It is called by `BTL_CLIENT_StepSelectItem` after a valid item pick (btl_client.c:2390-2432 (call at :2424)).
- Read: `B_BAG_MAIN_Create` copies it to `BagWork.keyItem` / `keyItemSlot` (b_bag_main.c:1338-1339) when `isComm != 1`, then `B_BAG_MAIN_CheckKeyItem` clears both if the player no longer owns the item (`MYITEM_SAVEDATA_HasItem`, b_bag_main.c:815).
- Drawn: label "LAST USED ITEM" (message 8) in window 4, printed only if `keyItem != 0` (`B_BAG_MAIN_DrawLabels`, b_bag_main.c:534-555); the item icon (actor 6) is placed at (0x24, 0xB4) = (36, 180) (`d_0220DB7C`, `B_BAG_MAIN_DisplaySingle`, b_bag_main.c:1209).
- Hit: pocket-screen cursor entry 4, rectangle 152..191 by 8..207, centre (107, 171) (ov197 0x0220DFFC entry 4), handled in `B_BAG_MAIN_StateSelectPocket` case 4 (b_bag_main.c:1449-1460): when `keyItem != 0` it sets `pocket = keyItemSlot`, `nextState = 9`, calls `B_BAG_MAIN_SyncCursorToItem` to put the page and cursor on that item and goes to the confirm page. Tapping the entry with no last item does nothing.

### Battle: battle party list and summary (ov198)

#### Detection
`bw_overlay_is_loaded(198)`. Opened by `BTLV_CORE_PollPartyMenu` (switch or forced replacement; btlv_core.c:1544-1588), by `PollBagMenu` as the item-target prompt (btlv_core.c:1676-1696) and by `BTLV_CORE_ProcMoveForgetScreen` (btlv_core.c:2428-2462). All call `B_PLIST_MAIN_Create(&core->plist)` (ov198/b_plist_main.c:560-575). `core->plist.done` (BtlvPlistWork +0x4C) is non-zero when it closes; the result: `pickedSlots[3]` +0x48 (party slot picks, 0xFF = none), `result` +0x4B, `modeSlot` +0x20 (item target slot, 6 = none).
The screen work (`B_PlistWork`, 0x2544 bytes, ov198/b_plist_types.h:90) is allocated by `B_PLIST_MAIN_Create` and driven by `TCB_AddTask(core->plist.effectSys, B_PLIST_MAIN_Task, q, 100)` (b_plist_main.c:562-565, `effectSys` = `BTLV_EFFECT_GetOwner()` stored at btlv_core.c:1497). Find it the same way as the bag: walk the task list of `BTLV_EFFECT_GetOwner()` for `func == B_PLIST_MAIN_Task`; `work` is the `B_PlistWork`.

#### State
- Host (`B_PlistHost` = `BtlvPlistWork`, b_plist_types.h:29-55 and btlv_core_types.h:136-165): `party` +0x04 (POKEPARTY, `POKEPARTY_GetCount/GetMember`), `partnerParty` +0x08 (link multi), `mode` ("listType") +0x1F, `modeSlot` +0x20 (selected row), `itemId` +0x24 (item being used), `modeParam`/`moveId` +0x26, `cancelRequest` +0x30, `exitRequest` +0x34.
- Per row summaries: `B_PlistWork + 4 + i*0x4C` for i in 0..5 holds a `struct Summary` (b_plist_main.c:215-224, filled by `B_PLIST_MAIN_ReadParty/ReadSummary`, b_plist_main.c:1426-1507): `mon` +0x00, `species` +0x04, `attack/defense/speed/spAttack/spDefense` +0x06..+0x0E, `hp` +0x10, `maxHp` +0x12, `ability` +0x14, `form` +0x15, `level:7 egg:1` +0x16, `gender` bits +0x17, `abilityId` +0x18, `heldItem` +0x1A, `exp` +0x1C, `lower/upper` +0x20/+0x24, `personality` +0x28, `moves[4]` +0x2C (8 bytes each: `id` u16, `pp` u8, `up` u8 = max PP, then three bytes whose field names are swapped: `kind` is the move type, `power` the damage class, `type` the accuracy, `accuracy` the power; compare b_plist_main.c:1466-1473 with waza_tool.c:198-216).
  Visible row r maps to the summary index `B_PLIST_MAIN_GetOrderIndex(p, r) = p[0x1CC + r]` (b_plist_main.c:1871-1874; table `d_02212138[listType][6]`, b_plist_main.c:1814-1818). `B_PlistWork.state` +0x2178 (index into the state handler table `d_0221221C`, ov198 0x0221221C) and `mode` +0x2180 (page 0..7, below) tell which page is up. Slot enabled state: `B_PLIST_MAIN_GetSlotState(p, i)` (0 empty, 1 selectable, 2 present but not selectable). Row status for the menu text: `B_PLIST_MAIN_GetRowStatus` (b_plist_main.c:1663): 2 cannot battle, 3 in battle, 4 stand by, 1 partner's, else "SHIFT" (b_plist_draw.c:1925-1939).
- Texts: message bank a/0/0/2 member 18: "Choose a Pok\emon." (6), "Use on which Pok\emon?" (7), "Battle using which Pok\emon?" (9), "Switch out which Pok\emon?" (10), "SHIFT" (21), "SUMMARY" (24), "CHECK MOVES" (25), "CANNOT BATTLE" (26), "IN BATTLE" (27), "STAND BY" (28), "PARTNER'S" (29), "FORGET" (68), "CANCEL" (69), "Restore which move?" (104). Header per `listType`: 3 -> msg 7, 2 -> msg 9, otherwise msg 6 (b_plist_draw.c:532-548; `listType 8` -> msg 10).

#### Input
Pages (`mode`) and their cursor tables (`d_022122D4[mode]`, ov198 0x022122D4; tables were dumped; entries are listed `top..bottom, left..right`):

Mode 0, party list (state 3 `HandleSelectInput` -> `HandleCursorInput`, b_plist_main.c:645-680, 1509-1535):

| Entry | Rectangle | Centre | Meaning |
|---|---|---|---|
| 0 | 0..47, 0..127 | (63, 23) | row 0 (left column top) |
| 1 | 8..55, 128..255 | (191, 31) | row 1 (right column top) |
| 2 | 48..95, 0..127 | (63, 71) | row 2 |
| 3 | 56..103, 128..255 | (191, 79) | row 3 |
| 4 | 96..143, 0..127 | (63, 119) | row 4 |
| 5 | 104..151, 128..255 | (191, 127) | row 5 |
| 6 | 152..191, 216..255 | (235, 171) | Cancel / Back |

A row tap only counts when the slot state is non-zero. For listType 3 (item use) the tap goes straight to the confirm (`B_PLIST_MAIN_ConfirmSelect`, b_plist_main.c:682-711); otherwise it opens mode 1.

Mode 1, Pokemon menu (state 4 `HandleModeSelect`, b_plist_main.c:713-760):

| Entry | Rectangle | Centre | Meaning |
|---|---|---|---|
| 0 | 24..131, 52..203 | (127, 77) | the big button: SHIFT (or the status text), it picks the Pokemon (or shows the "cannot" message) |
| 1 | 152..191, 0..103 | (51, 171) | SUMMARY (next state 13 = `ApplySummaryPage`; only if the row is not flagged) |
| 2 | 152..191, 104..207 | (155, 171) | CHECK MOVES (next state 14 = `ApplyMoveListPage`) |
| 3 | 152..191, 216..255 | (235, 171) | Back to the list (next state 11) |

Mode 2, summary (state 5 `HandleListSelect`, b_plist_main.c:762-820; ov198 0x02212334):

| Entry | Rectangle | Centre | Meaning |
|---|---|---|---|
| 0 | 152..191, 0..39 | (19, 171) | previous Pokemon |
| 1 | 152..191, 40..79 | (59, 171) | next Pokemon |
| 2 | 152..191, 96..199 | (147, 171) | CHECK MOVES |
| 3 | 152..191, 216..255 | (235, 171) | Back (to the Pokemon menu) |

Mode 3, move list (state 6 `HandleMoveSelect`, b_plist_main.c:821-886; ov198 0x022124B4): entries 0..3 = 48..95 / 96..143 by 0..127 and 128..255, centres (63,71) (191,71) (63,119) (191,119), open the move detail of that move (only if the slot has a move); 4 = 152..191, 0..39 previous Pokemon; 5 = 152..191, 40..79 next Pokemon; 6 = 152..191, 96..199 SUMMARY; 7 = 152..191, 216..255 Back to the Pokemon menu.

Mode 4, move detail (state 7 `HandleSlotSelect`, b_plist_main.c:888-925; ov198 0x02212370): entries 0..3 = small rectangles 152..167 / 168..183 by 88..127 and 128..167 that jump to another move (centres (107,159) (147,159) (107,175) (147,175)); entry 4 = 152..191, 216..255 Back to the move list.

Mode 5 (move list for an item such as an Ether, "Restore which move?", ov198 0x022123B8): entries 0..3 = move tiles (48..95 / 96..143 by 0..127 / 128..255), entry 4 = 152..191, 216..255 Back. Mode 6 (move list with a new move, the "forget" screen, ov198 0x02212400): entries 0..3 = move tiles, 4 = 144..191, 64..191 (centre (127, 167)) the new move, 5 = 152..191, 216..255 Back. Mode 7 (detail with new move, ov198 0x022122B0): entry 0 = 152..191, 0..207 (centre (103, 171)), entry 1 = 152..191, 216..255 Back. The meaning of mode 5, 6 and 7 entries is taken from the handler table names (`ApplyMoveListForItemPage`, `...WithNewMovePage`, `...DetailWithNewMovePage`, b_plist_main.c:1162-1200) and the draw functions, not from running the handlers: partly unverified.

Pad: the same `CURSOR_MOVE` neighbour bytes; A selects, B (-2) goes back.

### Battle: battle party for the opening screen (PSEL, ov223) is not a battle screen

ov223 is a script-started proc (`SCRCMD_PROC_CallWordScreen` starts it with `gPselProcData`, ov010/scrcmd_proc.c:2145-2157; the C name of the script command is probably wrong). It offers three Pokemon: species ids 0x1EF, 0x1F2, 0x1F5 (495, 498, 501; ov223 0x021BC710) shown with text; input code (psel.c:1248-1275) only reads A/B or a touch to advance the printed text. No touch hit table was found in `ov223/psel.c`; the selection mechanism (task menu) is unverified. It does not appear in any battle flow.

### Unverified summary for the battle sections
- Which on-screen position (left/right/front/back) each odd and even view position has on the 3D field.
- Whether `POKE_TOOL_GetPartyValue(mon, 111, 0)` is the nickname.
- What triggers the counter screen (screen 6) and the callers of the yes/no screen (screen 4).
- Neighbour/vals data of the target tables and the colour semantics.
- Taunt, Torment and other move restrictions at the move menu (checked after the tap).
- Bag `isComm == 3` (never set by the view code), `buttonMode` effects.
- Modes 5, 6, 7 of the party list entries (not exercised by reading handlers).
- A safe way to read the client's current step function from outside; the safe indicators are `input->screen`, `core->stepFn`, `core->subStep`.

---

## 8. Options (`ov068`)

### Detection

* `G->procSys->current->overlayId == 68` (0x44); also `bw_overlay_is_loaded(68)`. Entry: X menu kind 5 (ov010 `0x021712CC` row 5: overlay `0x44`, ProcData ov068 `0x021BB5C8`). `OPTION_MENU_Init` also loads ov067 (shared widgets) (`ov068/option_menu.c:450`) and unloads it in `OPTION_MENU_Exit` (`:492`).
* `Proc.work` is the `OptionMenuWork` (0x270 bytes), allocated by `PROCSYS_AllocProcWork(proc, sizeof(OptionMenuWork), 0x1c)` (`ov068/option_menu.c:452`). `Proc.procWork` is `OptionMenuArgs {u32 result; GameSystem *gameSystem}` (`ov068/option_menu_types.h`); `result` is 1 when the player left with the return button.
* The screen is main-engine-on-bottom (`BG_SYS_SetLcdSwap(0)`, `ov068/option_menu.c:517`).
* Sub-state is the runner callback `OptionMenuWork.runner.callback` (+0x0D4): `OPTION_MENU_StateFadeIn`, `StateMain` (`:1939`), `StateDiscardPrompt` (`:2027`, "Would you like to make these changes?" with YES/NO), `StateKeepSaved`, `StateApply` (`:2069`), `StateFadeOut`. Within `StateMain` the sub-state is `*state` (0 normal, 1 "cannot change" message, 2 to 4 setting-specific help/message, 5 waiting for a message) (`:1953-2023`).

### State

Row labels (bank 29 of `a/0/0/2`, `OPTION_MENU_Init`: `MSGDATA_CreateFromArchive(0, 2, 0x1d)`, `ov068/option_menu.c:461`): title 0 "OPTIONS"; rows 1 "TEXT SPEED", 2 "BATTLE SCENE", 3 "BATTLE STYLE", 4 "SOUND", 7 "SAVE BEFORE IR" (this fifth row is called `OPTION_BUTTON_MODE` in the header but its labels are SAVE / NO SAVE); values 10 "SLOW", 11 "MID", 12 "FAST", 13 "ON", 14 "OFF", 15 "SHIFT", 16 "SET", 17 "STEREO", 18 "MONO", 23 "SAVE", 24 "NO SAVE"; buttons 8 "CONFIRM" and 9 "QUIT"; help texts 25 to 28 and 31; 33 "Would you like to make these changes?", 34 "YES", 35 "NO". The label table `gOptionMenuLabels` (ov068 `0x021BB820`, 16 entries of message id, x, y, w, h in tiles; `ov068/option_menu_types.h`):

| Label | Message | Tile x, y | Meaning |
|---|---|---|---|
| 0 to 2 | 10, 11, 12 | (14,5), (20,5), (26,5) | SLOW, MID, FAST |
| 3, 4 | 13, 14 | (15,8), (24,8) | BATTLE SCENE ON, OFF |
| 5, 6 | 15, 16 | (15,11), (24,11) | BATTLE STYLE SHIFT, SET |
| 7, 8 | 17, 18 | (15,14), (24,14) | SOUND STEREO, MONO |
| 9, 10 | 23, 24 | (15,17), (24,17) | SAVE BEFORE IR: SAVE, NO SAVE |
| 11 to 15 | 1, 2, 3, 4, 7 | x = 1, y = 5, 8, 11, 14, 17 | the five row names |

The edited values live in the list: `OptionMenuWork.list.settings` (list at +0x148, `settings` at list+0x44, so +0x18C): `OptionSettings {u8 values[5]; u8 lastChanged; Config *config}` (`ov068/option_menu_types.h`). `values[row]` is the index of the chosen choice of that row: row 0 TEXT SPEED 0 SLOW / 1 MID / 2 FAST; row 1 BATTLE SCENE 0 ON / 1 OFF; row 2 BATTLE STYLE 0 SHIFT / 1 SET; row 3 SOUND 0 STEREO / 1 MONO; row 4 SAVE BEFORE IR: the choice with `value 1` is SAVE and `value 0` NO SAVE. The mapping choice to value is the ROM table `gOptionMenuChoices` (below). The saved copy (what the save holds) is `OptionMenuWork.savedSettings` (+0x248). `list.cursor` (+0x14C) is the highlighted row 0 to 4, 5 CONFIRM, 6 CANCEL. `list.scrollY` (+0x17C) is the list scroll. `OPTION_MENU_GetListSettings(&list, &out)` copies the current settings.

### Input

Hit regions, all from ROM data:

* The 11 choices `gOptionMenuChoices` (ov068 `0x021BB704`, 24-byte records `{settingIndex, value, actorX, actorY, rectX, rectY, rectW, rectH}`; `OptionMenuChoice`, `ov068/option_menu_types.h`). The tested rectangle is `x = rectX`, `y = list.scrollY + rectY`, size `rectW x rectH`; `list.scrollY` starts at 0x18 (`OPTION_MENU_InitList` sets 0x10 and scrolls by -8, `ov068/option_menu.c:1317,1322`) and drag scrolling moves it within -176 to 208 (`OPTION_MENU_ScrollList`, `:1566-1568`), and a choice is hit only when `0x18 <= y` and `y + rectH` are both inside 0x18 to 0xA8 (`OPTION_MENU_SelectChoiceAtTouch`, `ov068/option_menu.c:1577-1602`). Values dumped:

| # | Setting | Value | Rectangle (x, y in list space, w x h) | Tap centre at the initial scrollY = 0x18 |
|---|---|---|---|---|
| 0 | TEXT SPEED | 0 SLOW | 112, 0, 48 x 24 | (136, 36) |
| 1 | TEXT SPEED | 1 MID | 160, 0, 48 x 24 | (184, 36) |
| 2 | TEXT SPEED | 2 FAST | 208, 0, 48 x 24 | (232, 36) |
| 3 | BATTLE SCENE | 0 ON | 112, 24, 72 x 24 | (148, 60) |
| 4 | BATTLE SCENE | 1 OFF | 184, 24, 72 x 24 | (220, 60) |
| 5 | BATTLE STYLE | 0 SHIFT | 112, 48, 72 x 24 | (148, 84) |
| 6 | BATTLE STYLE | 1 SET | 184, 48, 72 x 24 | (220, 84) |
| 7 | SOUND | 0 STEREO | 112, 72, 72 x 24 | (148, 108) |
| 8 | SOUND | 1 MONO | 184, 72, 72 x 24 | (220, 108) |
| 9 | SAVE BEFORE IR | 1 SAVE | 112, 96, 72 x 24 | (148, 132) |
| 10 | SAVE BEFORE IR | 0 NO SAVE | 184, 96, 72 x 24 | (220, 132) |

  The y coordinates are in list space; the list is placed on the screen so that these rows appear at y = 0x18 + 24*row (the y test only accepts rows fully inside 0x18-0xA8). The list can be scrolled by dragging (`OPTION_MENU_ScrollList`, `ov068/option_menu.c:381-384`); a recipe should read `list.scrollY` (work+0x17C) and tap `y = scrollY + rectY + 12`, with the y test above (y and the bottom edge inside 0x18-0xA8); at the start that is y = 36, 60, 84, 108, 132 for the five rows. Whether the drawn rows follow `scrollY` exactly (the sprites are placed at `scrollY + actorY`, `OPTION_MENU_ScrollList`, `ov068/option_menu.c:1571`) is consistent with this reading; the window/BG scroll that moves the text with it is not read: unverified.
* Confirm and cancel rectangles, ROM `gOptionMenuConfirmRect` (ov068 `0x021BB550`) and `gOptionMenuCancelRect` (`0x021BB560`), each `{left, top, right, bottom}` as four ints (`ov068/option_menu.c:1133,1141`): CONFIRM x 112-184, y 168-192, centre (148, 180); CANCEL x 184-256, y 168-192, centre (220, 180). Tested with `OPTION_MENU_IsPointInRect` (`:419`) on the press (`OPTION_INPUT_TOUCH`).
* Touch bar buttons at the bottom left (`gOptionMenuReturnButtonCells`, ov068 `0x021BB5D4`, `TouchBarButtonDef` x2, `OPTION_MENU_InitButtons`, `ov068/option_menu.c:1065-1095`): kind 0 (return, `OPTION_BUTTON_RETURN`) at x 0x50, y 0xA8, and kind 6 (the Y-registration toggle for item 0x18) at x 0x30, y 0xAC. A touch bar button's hit test is `x in [def.x, def.x + 0x18]`, `y in [def.y, def.y + 0x18]` on the press (`ov067/touchbar.c:533-545`). Tap centres: return (92, 180); Y toggle (60, 184). The key mask of these buttons is in the definition (B and Y respectively: unverified).
* Keys: D-pad up/down change the row, left/right change the value (`OPTION_MENU_StepSelectedSetting`), A on CONFIRM/CANCEL, B cancels (`OPTION_INPUT_*`, `ov068/option_menu_types.h`, `OPTION_MENU_ReadInput :868`).
* Discard prompt: an `APP_TASKMENU` yes/no (`OPTION_MENU_PrintDiscardPrompt`; positions not read: unverified).

---

## 9. Trainer card (ov111; ov110 is not the editor)

### Detection

* `gameSystem->procSys->current->overlayId == 0x6F` (111); ROM data: X menu kind 3 row (`0x02171314` in ov010) holds overlay `0x6F` and the ProcData at ov010 `0x02172614`. That ProcData is not the viewer: its entries `0x0216F0AD / 0x0216F175 / 0x0216F1E1` are `TRCARD_SYS_Init/Main/Exit` (`layout/function_addrs.tsv`, ov010 code). So the proc on the game stack is `TRCARD_SYS` and ov111 only provides the screens, run on a private `ProcSys` (`TrainerCardWork.childProc` 0x08): `TRCARD_SYS_StateStartCardProc` pushes ProcData `0x021725FC` (ROM: `0x021C3CC1 / 0x021C3F55 / 0x021C40DD` = `VIEWER_MAIN_Create/Main/Delete`, `ov010/trcard_sys.c:StateStartCardProc`). So `bw_overlay_is_loaded(111)` is the test, and the screen proc is `((TrainerCardWork*)Proc.work)->childProc->current`.
* `TRCARD_SYS` states (`ov010/trcard_sys.c:TRCARD_SYS_Main`): 1 start card, 2 wait card, 3 start edit, 4 wait edit, 5 start badge, 6 wait badge, 7 end. `StateChooseScreen` starts with the card unless the player has not cleared the game and a play time exists, then it starts with the badge screen (`:StateChooseScreen`). After the card, `param->nextScreen` 1 goes to edit, 2 to the badge screen, else out (`StateWaitCardProc`).
* Three screens, all in ov111/ov110:
  1. The card (`VIEWER_MAIN_*`, `ov111/viewer_main.c:483-700`). Work `ViewerWork` (0x3310, `PROCSYS_AllocProcWork(man, sizeof(ViewerWork), 0x27)`, `viewer_main.c:498`); `Proc.procWork` is `ViewerArgs`.
  2. The badge viewer (`VIEWER_TEXT_*`, `ov111/viewer_text.c:861-925`): a 3D badge model with 8 rows. Work `ViewerTextWork` (0x1C8). ProcData in ROM at overlay 111 `0x021C892C` (`0x021C6FC5 / 0x021C705D / 0x021C7099` = `VIEWER_TEXT_Create/Main/Delete`). It is the badge case (8 badges, `rowCount = 8`, `badgeTimer[8]`).
  3. The edit screen is ov110, which is the easy-chat word input (`PMS_INPUT`, ProcData `gPmsInputProcData`), pushed with `ov110_id` (`ov010/trcard_sys.c:StateStartEditProc`). It edits the greeting sentence, not the card art.
* Sub-state of the card: `ViewerWork.state` (0x32C6), `phase` (0x32C8), `pane` (0x305C), `inputMode` (0x305E, 0 touch, 1 key per the comment, but note the code assigns `mode` from `UI_GetInputMode`, `viewer_main.c:508`), `decoVisible` (0x3034, the signature editing view is open). `ViewerWork.mode` (0x3060) mirrors `UI_GetInputMode`; `VIEWER_MAIN_UpdateInput` uses touch rules when it is 1 (`viewer_main.c:1778-1786`; whether 1 means touch is consistent with `UI_SetInputMode(1)` after a touch in `FIELD_MENU_HandleTouch`).
* LCD: `VIEWER_TEXT_InitVram` calls `GX_SetDispSelect(GX_DISP_SELECT_SUB_MAIN)` (`viewer_text.c:1000`); `VIEWER_MAIN_InitBg` sets the layout for the card (`viewer_main.c:839-880`); which half goes to which screen for the card is unverified.

### State

`ViewerInfo` (0x674, `ov111/viewer_types.h:31-86`, built by `TRCARD_SYS_BuildProfile` into a 0x678 buffer, `ov010/trcard_sys.c:TRCARD_SYS_Init`): `kind` 0x00, `bgSlot` 0x03, flag bits at 0x04 (`f4_4` card style / side, `f4_5`, `f4_6` editable, `f4_7` view-only), `rowIndex` 0x06, `rowMask` 0x08, `name[9]` 0x0A (UCS-2 string), `playTime` 0x1C (`PLAYTIME_GetHours`...), `topValue` 0x20 (7 digit number, shown on the card top), `row0Value` 0x28 (ID No.), `row6..row9Value` 0x38-0x44 (links, link battles, wins, losses ...), `row10..13Value` 0x4C-0x58, `row14..18Value` 0x5C-0x64, `hasRow14..18` bits at 0x68, signature bitmap at 0x74 (0x600 bytes). Dates: `year1/month1/day1` 0x2F-0x31 (adventure started), `year2/month2/day2` 0x32-0x34 (hall of fame debut; `month2 != 0` means present).

Row labels (message bank 0xBB of `a/0/0/2`, `viewer_main.c:742`, decoded text): 5 ID No., 6 TIME, 7 ADVENTURE STARTED, 8 HALL OF FAME DEBUT, 9 TIMES LINKED, 10 LINK BATTLES, 11 LINK BATTLE WINS, 12 LINK BATTLE LOSSES, 13 LINK TRADES, 14 WILD POKeMON ENCOUNTERS, 15 TRAINERS BATTLED, 16 PEOPLE PASSED BY, 17 FEELING CHECKS, 18 USABLE PASS POWERS, 19 MUSICAL PERFORMANCES, 20 Poke Transfer HIGH SCORE, 21 Battle Test HIGH SCORE (message ids = row label index + 5 for rows 0 to 18, `VIEWER_TEXT_DrawRow`, `viewer_text.c:487-575`). Front-of-card fields NAME 0, NATURE 2, POKeDEX 3, MONEY 4 are in the same bank (index 0, 2, 3, 4); the card front layout is drawn in `VIEWER_TEXT_Draw/DrawTable` (`viewer_text.c:345-413`) and which `ViewerInfo` word backs MONEY and POKeDEX is not settled by the code read (unverified; the visible rows are `rowList[]`, built by `VIEWER_TEXT_BuildRowList`, `viewer_text.c:590-618`: rows 0-12 always, 14 and 13 if `hasRow14`, then 15-18 as present). Five rows are visible at a time with a 16 px step and a scroll offset `ViewerWork.scroll` (0x32E2) (`VIEWER_TEXT_DrawPage`, `:620-640`).

Badges (`ViewerTextWork`): `rowEnabled[8]` 0x17C (which badges are owned), `badgeTimer[8]` 0x188, `page` 0x114, `pageCount` 0x116, `angle` 0x13C (model angle), `snap` 0x144 (`ov111/viewer_types.h:262-338`). The owned badges come from the save through `VIEWER_ACTOR_LoadBadgeTimers`; the exact accessor of "badge n owned" is unverified.

### Input

Tables are ROM data at overlay 111 (`top, bottom, left, right`); they are read in `ov111/viewer_main.c:1745-1790` and the helpers. The card reacts on the press.

Card, touch mode, view layout (`d_021C8594`, used when `info->f4_7 == 0`), result codes as in `VIEWER_MAIN_HandleRowTouch` (`:1572-1651`) and `DrawSelected` (`:1793-1848`):

| Entry | Rectangle (x, y) | Result | Tap centre |
|---|---|---|---|
| 0 | x 228-252, y 168-192 | code 4: back/exit (nextScreen 0, exit) | (240, 180) |
| 1 | x 204-228, y 168-192 | code 5: nextScreen 1 (go to the edit screen) | (216, 180) |
| 2 | x 100-164, y 168-192 | code 3: turn the card (flip side/page) | (132, 180) |
| 3 | x 4-28, y 168-192 | in signature view: toggles the card style; else code 10: exit with `exitCode` 2 | (16, 180) |
| 4 | x 36-60, y 168-192 | code 8: pen tool, signature view only | (48, 180) |
| 5 | x 68-92, y 168-192 | pen size toggle, signature view only | (80, 180) |
| 6 | x 180-204, y 168-192 | favourite toggle (`VIEWER_MAIN_ToggleFavorite`) | (192, 180) |
| 7 | x 24-160, y 48-64 | code 6: exitCode 1 (only when the signature view is closed) | (92, 56) |
| 8 | x 24-160, y 64-80 | code 7: exitCode 2 | (92, 72) |
| 9 | x 16-239, y 120-151 | code 9: opens the window update (`seq 9`) | (127, 135) |
| 10, 11 | x 231-255 and x 0-24, y 0-168 | edge scroll in key mode | n/a |

The card keys: B back, X next-screen, left/right (0x30) flip, Y favourite, A code 10 (`VIEWER_MAIN_HandleKey`, `:1444-1483`). The meaning of `exitCode` values and the labels of table entries 3, 7, 8, 9 in terms of what the player sees is only partly settled (flag meanings of `f4_x` are not named in the code): treat the table above as the code-level truth and the labels as unverified. Other tables: `d_021C8554` (view-only layout, 3 entries: back x 228-252, back again, flip x 100-164, all y 168-192), `d_021C852C` / `d_021C853C` / `d_021C8534` / `d_021C854C` / `d_021C8544` are the signature canvas and page areas (for example `d_021C8544` x 16-240, y 16-80 is the scrolling rows area and `d_021C852C` x 32-225, y 88-152 the left drawing pane): drawing and scrolling are stylus drags, not buttons.

Badge screen (`VIEWER_TEXT`): touch handling is in `VIEWER_ACTOR_*` (`ov111/viewer_actor.c`), with a horizontal drag on the model (`dragX`, `dragStartAngle`) and touch rows; the rectangles were not extracted (unverified).

---

## 10. Save / report (`ov034` and the report event)

### Detection

* Not a proc and not an overlay of its own app: the save is the game event `REPORT_EVENT_Main`, wrapped by `EVENT_FIELD_PROCLINK_FreeEventMain` (`ov010/event_field_proclink.c:1200-1215`), started from X menu button 5 through the proc-table row 4 `start` function `EVENT_FIELD_PROCLINK_StartConfigEvent` (`ov010/event_field_proclink.c:971`, ov010 `0x02161351`). The field stays alive (the row has no overlay and no ProcData, so the shortcut event does not close it, section 2).
* Test: field up and `sub->mode == 7` (`FIELD_SUBSCREEN_MODE` row 7 = `InitReport`, `ov021/field_subscreen.c:839-843`), `bw_overlay_is_loaded(34)` (ov034 is loaded by `FIELD_SUBSCREEN_InitReport`, `abs_id_22` = 0x22 = 34), `G->event != 0`.
* `REPORT_EVENT_Main` state (`ReportEventWork`, 0x1C bytes: `saveControl` 0x00, `gameSystem` 0x04, `fieldMap` 0x08, `msgContext` 0x0C, `heapId` 0x10, `resultOut` 0x14, `res` 0x18; `ov010/field_event_types.h:80-89`): the state variable is the `u32 *state` passed by the event wrapper; the named states are `REPORT_EVENT_STATE_*` (`ov010/report_event.c:283-446`): LOCK_PLAYER (request mode 7), CREATE (wait for the panel, build the message window and the yes/no resources), PRINT_ASK ("Would you like to save the game?" text), WAIT_ASK, YES_NO, then either WAIT_MESSAGE with the overwrite warning, or PRINT_SAVING, WAIT_SAVING, WAIT_REQUEST, START_SAVE, SAVE (the write; `GAME_DATA_SaveMain` returns 0/1 busy, 2 done, 3 failed), SAVED, WAIT_SAVED_MSG, WAIT_SAVED_SE, WAIT_KEY_TIMEOUT (150 frames or A/B), FINISH_SAVED, FAILED, CANCEL.
* The overlay ov205 (`SAVE_SCREEN`) is not this: it is the Hall of Fame / game-clear save (`ov010/event_gameclear.c:263`).

### State

* The report panel is `(ReportWork*)sub->cgear` (`ov021/field_subscreen.c:839-843`). `ReportWork` (`ov034/report_types.h`): `gameSystem`, `saveControl`, `printQue`, `windows[8]` (one `BmpWin` per record line), `actors[16]` (0 to 5 party icons, 6 to 15 progress-bar pieces), `partyCharRes[6]`, `saveAnimRunning`, `saveCountCurrent`/`saveCountMax`, `barPiecesLit`, `shownFlags`. The panel prints these 8 lines from the game state (`REPORT_Create`, `ov034/report.c:447-510`), so a recipe can read the same sources:

| Line | Source | Evidence |
|---|---|---|
| 0 player name | `WORDSET_SetSlotMyStatus(GAME_DATA_GetMyStatus(gd))` | `ov034/report.c:464-467` |
| 1 date | the real-time clock date (year, month, day) | `:470-473` |
| 2 time | the clock (hour, minute) | `:477-480` |
| 3 location | `ZONEDATA_GetPlaceNameId(GAME_DATA_GetPlayerZoneId(...))`, bank 59 | `:482` |
| 4 badges | `MISC_CountBadges(GAME_DATA_GetMiscSave(gd))` | `:486` |
| 5 Pokedex | `ZUKAN_SAVE_CountSeen(ZUKAN_SAVE_GetFromGameData(gd))` | `:491` |
| 6 play time | `GAME_DATA_GetPlayTime` hours, minutes (`PLAYTIME_GetHours/Minutes`) | `:495-500` |
| 7 last save | `SYSTEM_DATA_GetPlayTime(saveControl)` stamp (`PLAYTIME_GetStampYear/Month/Day/Hour/Minute`) | `:502-` |

  Party icons: `REPORT_ShowPartyIcons`, one icon per party member (`POKEPARTY` of the save). The panel's labels are bank 168 (0xA8) of `a/0/0/2` (`MSGDATA_CreateFromArchive(0, 2, 168, ...)`, `ov034/report.c:458`): 0 "<name>'s Data", 1 the date (`m/d/20yy`), 2 the time (`h:mm`), 3 the location, 4 "Gym Badges: n", 5 "Pokedex: n", 6 "Time: h:mm" (play time), 7 "Last saved on m/d/20yy at h:mm" (decoded). The event's own strings are bank 0x9E (158) of archive `a/0/0/3` (`MSGDATA_CreateFromArchive(0, 3, 0x9E, ...)`, `ov010/report_event.c:297`), a different archive from the `a/0/0/2` banks used elsewhere in this document (decoded with the same tool): 2 "Would you like to save the game?", 4 "Saving... Don't turn off the power.", 5 "<name> saved the game.", 7 "Save error.", 8 and 9 the "There is already a saved game file. It is impossible to save." messages, 10 "Saving a lot of data...".
* The question and result text are printed into a message window on layer 6 (`BMP_WIN_Create(6, 1, 1, 0x1E, 4, ...)`, `ov010/report_event.c:REPORT_EVENT_CreateMessageWindow`) with the print stream in `ReportEventRes` (`res`); the current text is the string buffer `res->message`.

### Input

* Yes/no menu: `APP_TASKMENU` with template `itemNum 2`, `posType 1`, `charPosX 0x20`, `charPosY 12`, `width 8`, `height 3` (`REPORT_EVENT_OpenYesNoMenu`, `ov010/report_event.c:603-616`). With `posType 1` the rectangles are `posX = 0x20 - 8 = 24`, `posY = 12 - 3*2 = 6` characters, so entry 0 (YES) is x 192-255, y 48-71 (centre (224, 60)) and entry 1 (NO) is x 192-255, y 72-95 (centre (224, 84)) (computation per `APP_TASKMENU_UpdateTouch`, `arm9/app_taskmenu.c:656-677`). The menu is `fast decide` (`APP_TASKMENU_OpenMenuFastDecide`): a tap decides at once. `REPORT_EVENT_UpdateYesNoMenu` returns 0 for YES and 1 for NO (`:619-632`).
* Message advance: while the text prints, a touch (`TOUCHPANEL_GetTrg() == 1`) or A/B speeds it up and releases pauses (`REPORT_EVENT_UpdatePrintStream`, `ov010/report_event.c:495-520`), so a tap anywhere on the bottom screen advances the message. After a successful save the screen waits for A/B or 150 frames (`:WAIT_KEY_TIMEOUT`).
* Keys: A/B for the yes/no and messages (`KEY_GetTrg() & 3`).

---

## 11. PC boxes (ov170, "BOX2")

### Detection

- Overlay 170 loaded (`ov170/box2_main.c:442` `BOX2_MAIN_Init` runs as the proc init; the overlay is ov170 per `docs/unified-view-inventory.md` section 3.10). The proc is the PC box app, pushed on the game system's proc stack.
- The proc work is a `BoxTask`, 0x30 bytes, allocated with `PROCSYS_AllocProcWork(manager, 0x30, 75)` (`ov170/box2_main.c:448`). So `Proc.work` of the current proc is the `BoxTask` while `Proc.overlayId == 170`.
- The app has its own nested `ProcSys` (`BoxTask.procSys`, `ov170/box2_main.c:466`) that runs the sub screens (summary, name entry, bag). When the summary (ov131), the name entry (ov194) or the bag (ov070) is on top, that nested stack's current proc is the visible scene, not the box grid (`ov170/box2_main.c:3099` starts name entry for a box rename: kind 2, max length 8).
- Sub-state: `BoxTask.state` (+0x28, initial value 11, `ov170/box2_main.c:454`) is the row of a state table in `box2_seq.c` / `box2_main.c`; the meaning of the individual rows is not tabulated here (unverified). `BoxParam.mode` (`BoxTask.params->mode`) picks the entry mode: 0 normal PC, 1, 2, 3, 4, 5 (`ov170/box2_ui.c:761-768` maps modes 0..5 to cursor tables; mode 4 is the trade/link mode, `ov170/box2_main.c:1919`; the names of modes 1, 2, 3, 5 are unverified).

### State

`BoxTask` (`ov170/box2_types.h:46`), offsets confirmed against Init (`ov170/box2_main.c:449-462`):

| Offset | Field | Meaning |
|---|---|---|
| 0x00 | `params` | `BoxParam *` (below) |
| 0x04 | `procSys` | nested `ProcSys *` |
| 0x13 | `menuMode` | menu mode byte (values unverified) |
| 0x14 | `curBox` | current box number, set from `BOX_SAVEDATA_GetCurrentBox` (`box2_main.c:449`) |
| 0x15 | `boxLimit` | usable boxes: 8, 16 or 24 (`box2_main.c:450`, `arm9/box_savedata.c` header `usableBoxCount`) |
| 0x16 | `cursorSlot` | cursor slot index (see slot numbering) |
| 0x17 | `heldSlot` | 255 at start (`box2_main.c:453`) = nothing held; the code also compares it with `curBox` (`box2_main.c:1646-1648`, `2000`), so it may hold the box number the held Pokemon came from. Unverified which. |
| 0x24 | `pickedSlot` | slot picked (unverified) |
| 0x28 | `state` | sequence state |
| 0x2C | `work` | `BoxWork *` (about 0xA190 bytes; fields `cursorX/cursorY` at +0xA180/+0xA184 are the last tap, `heldSlot` +0xA16C, `cursorIndex`, `currentBox` `ov170/box2_types.h:156-246`) |

`BoxParam` (`ov170/box2_types.h:30`): `+0 gameData`, `+4 boxSave` (this is the `GameData *` the `BOX_SAVEDATA_*` calls take, `box2_main.c:449`), `+8 party` (`struct Party *`), `+0xC config`, `+0x18 linkMode`, `+0x20 mode`.

Slot numbering used by the app (`ov170/box2_main.c:1924`, `ov170/box2_ui.c:1520-1565`): 0..29 are the 30 slots of the shown box (`BOX_CAPACITY` 30, `arm9/box_savedata.c:44`), 0x1E..0x23 (30..35) are the six party slots, 0x24 and up (36..39) are the four buttons of the right column (`BOX2_UI_GetGridSlotPoint` returns `index + 0x24`, `box2_ui.c:1565-1569`).

Contents, all through accessors, nothing needs the BoxWork:

- Box Pokemon: `BOX_SAVEDATA_GetPokemon(gameData, box, slot)` (`arm9/box_savedata.h:39`), `BOX_SAVEDATA_GetPokemonValue(gameData, box, slot, field, arg)` (`box_savedata.h:38`, box 0..23, slot 0..29), `BOX_SAVEDATA_CountPokemonInBox`, `BOX_SAVEDATA_GetBoxName(gameData, box, Strbuf *)`, `BOX_SAVEDATA_GetWallpaper(gameData, box)` (`box_savedata.h:30-34`).
- Party: `POKEPARTY_GetCount(party)`, `POKEPARTY_GetMember(party, pos)` (`arm9/pokeparty.h`).
- The per-slot value ids used by the box code are the `POKE_TOOL` / `POKE_TOOL_GetPartyValue` field ids (0x4C is "is egg" style test in `box2_main.c:1924`; the full id list is outside this note).

### Input

Taps are tested with the per-frame 12-byte-stride tables below (the rectangle is the 4 bytes at +8 of each 12-byte record, `ov170/box2_ui.c:1486`). All are overlay-170 data.

| Table (DS address) | Function that tests it | Entries | What it is |
|---|---|---|---|
| 0x021F0DCC | `BOX2_UI_GetPartySlotHit/Point` (`box2_ui.c:1520,1525`; despite the name this is the box grid) | 30 | Box grid: 6 columns x 5 rows. Column x ranges 12-35, 36-59, 60-83, 84-107, 108-131, 132-155; row y ranges 40-63, 64-87, 88-111, 112-135, 136-159. Index = row * 6 + column. Centre of slot i: x = 23 + 24*(i % 6), y = 51 + 24*(i / 6). |
| 0x021F0574 | `BOX2_UI_GetHandSlotHit` (`box2_ui.c:1530`) | 6 | Six party slots in a staggered two-column tray on the left: (x 30-53, y 56-79), (66-89, 64-87), (30-53, 88-111), (66-89, 96-119), (30-53, 120-143), (66-89, 128-151) |
| 0x021F1354 | `BOX2_UI_GetBodySlotHit` (`box2_ui.c:1535`) | 6 | The same tray mirrored on the right: (182-205, 56-79), (218-241, 64-87), (182-205, 88-111), (218-241, 96-119), (182-205, 120-143), (218-241, 128-151) |
| 0x021F1174 | `BOX2_UI_GetGridSlotPoint` (`box2_ui.c:1565`) | 4 | Right-column buttons, x 200-223, y 22-45, 56-79, 90-113, 124-147; result slot = index + 36 |
| 0x021F0F40 | `BOX2_UI_GetCloseButtonHit` (`box2_ui.c:1540`) | 2 | (y 18-38, x 6-26) and (y 18-38, x 141-161): by position these are the previous/next box arrows in the box header. Unverified that the label is "arrows". |
| 0x021F0F58 | `BOX2_UI_GetPageButtonHit2` (`box2_ui.c:1580`) | 1 | y 168-191, x 0-95 (bottom-left button) |
| 0x021F0024 | `BOX2_UI_GetBackButtonPoint` (`box2_ui.c:1558`) | 1 | y 0-191, x 168-255 (a drag-target region, not a button) |
| 0x021F002C | `BOX2_UI_GetPageButtonHit` (`box2_ui.c:1573`) | 1 | y 48-167, x 16-103 |
| 0x021F0034 | `BOX2_UI_GetExitButtonPoint` (`box2_ui.c:1550`) | 2 | y 0-167 with x 228-255, and x 168-195 |
| 0x021F0040 | `BOX2_UI_GetExitButtonHit` (`box2_ui.c:1545`) | 2 | x 173-255 with y 0-21 and y 148-167 |

The names of the last five come from the function names; the geometry (large strips) suggests they are drag edge zones and bars, not small buttons. Their on-screen meaning is unverified.

- A tap on a slot selects it, a second tap or A picks it up (`BOX2_MAIN_HandleMonPickup` family). The app is drag driven: while the stylus is held the held Pokemon follows (`TOUCHPANEL_GetPointCont` calls at `box2_main.c:3126-3316`, `box2_seq.c:2068-2392`), so moving a Pokemon by tap needs a tap on the source, then a tap on the target; a drag needs the tap held with the point moved each frame (`eidos_ui_tap` holds one point; unverified that two taps work in the pick-then-drop order the game expects, the code also supports it through the A button path).
- Buttons: the cursor is a `CURSOR_MOVE` object driven by the d-pad (`box2_ui.c:761-775`), A selects, B cancels (unverified for each sub menu).

---

## 12. Poke Mart (shop)

### Detection

- The shop is NOT an overlay proc. It is a script command running in the field: `SCRCMD_SHOP_StartShop` (`ov021/scrcmd_shop.c:90`) allocates a `ShopTask` (0x214 bytes, `scrcmd_shop.c:111`), stores its pointer in the script's user work slot (`scrcmd_shop.c:112`) and arms `SCRCMD_PROC_FLD_StepShopTask` as the script VM wait function (`scrcmd_shop.c:140`, `ov021/scrcmd_proc_fld.c:780`). So the field proc and ov021 stay loaded and the field map stays alive; the field camera is moved aside (`scrcmd_shop.c:152-174`).
- Reliable test: the script VM's wait function pointer equals `SCRCMD_PROC_FLD_StepShopTask` (`ScriptVm.wait` at +0x10, `arm9/script_vm_types.h:24-38`). Reaching the VM from outside is not settled by the code read here: the chain is `GameSystem.event` -> script event work -> `ScriptWork` (`ov010/script_work_types.h:29`; `event` +0x14, `userWork` +0x3C) -> VM (unverified link between event work and VM). The simple robust option is a port-side hook in `SCRCMD_SHOP_StartShop` that records the `ShopTask *` and clears it when `StepShopTask` returns 1 (`scrcmd_proc_fld.c:780-835`, case 7 frees the task).
- `ShopTask.state` (+0, `ov021/scrcmd_proc_fld_types.h:167`): 0..3 camera moves in, 4 = the shop screen runs, 5..6 camera moves back, 7 done (`scrcmd_proc_fld.c:793-826`). The shop UI is up exactly while `state == 4`.
- Sub-state: `ShopTask.shop.state` (+8, `ShopWork.state`): 0 init, 1 item list, 2 quantity enter, 3 quantity step, 4 list resume, 5 and 6 yes/no open, 7 yes/no poll, 8 purchase, 9 bonus item, 10 message wait, 11 exit (`scrcmd_proc_fld_types.h:37-48`).

### State

`ShopWork` sits at `ShopTask + 8` (`scrcmd_proc_fld_types.h:104-166`, `ShopTask` at 167-174). Offsets within `ShopWork`:

| Offset | Field | Meaning |
|---|---|---|
| 0x000 | `state` | see above |
| 0x00C | `moneyData` | `MiscSave *`; money = `MISC_GetMoney(moneyData)` (`scrcmd_proc_fld.c:GetShopCurrency` ~line 896) |
| 0x010 | `bagData` | the bag (owned count via `MYITEM_SAVEDATA_GetItemQuantity`, `scrcmd_proc_fld.c:906`) |
| 0x018 | `battlePointData` | Battle Point source when `usesBattlePoints` |
| 0x020 | `items[40]` | `ShopItem { u16 itemId; u16 pad; s32 price; }` (`scrcmd_proc_fld_types.h:51-55`) |
| 0x160 | `itemCount` | number of entries |
| 0x162 | `isTmShop` | 1 for the TM shop (id 0xFD) |
| 0x163 | `usesBattlePoints` | 1 for BP shops (0xFE, 0xFD) |
| 0x1A4 | `listMenu` | `BmpMenuList *`; cursor and scroll via `BMP_MENULIST_GetPos(menu, &scroll, &row)` and `BMP_MENULIST_GetListIndex` (`arm9/bmp_menulist.h:23-24`) |
| 0x1FC | `itemId` | item under the cursor (set on selection) |
| 0x200 | `price` | unit price of the selected item |
| 0x204 | `maxQuantity` | upper bound (money / price, capped at 99 and by bag room, `scrcmd_proc_fld.c:927-941`) |
| 0x208 | `quantity` | current quantity (starts at 1) |

Which list: `ShopTask.shopKind` (+2): 0xFF badge-tiered (`BuildItemListByBadges`, `scrcmd_proc_fld.c:850-854`), 0xFE fixed BP list, 0xFD TM BP list, 0xF7..0xFB range lists, anything else a badge-tier list with `shopVariant` (+3) (`scrcmd_shop.c:117-137`, `scrcmd_proc_fld.c:846-877`). The item name is the item message bank (`ShopWork.itemMsgBank` +0x18C); description is printed by `PrintItemDescription`. Prices shown: `price` per item; total = quantity * price.

### Input

- No touch anywhere in the shop. `BMP_MENULIST_Main` reads only keys (`arm9/bmp_menulist.c:250,252`), the yes/no menu reads only keys (`arm9/bmp_menu.c:235`), and the quantity screen reads only keys (`scrcmd_proc_fld.c:1784-1818`): Up +1, Down -1, Left -10, Right +10 (key repeat; bits 0x40, 0x80, 0x20, 0x10), A confirms, B cancels. List: Up/Down move, A selects, B leaves (selection -2 = exit, `scrcmd_proc_fld.c:PollItemList`). Message boxes advance on A or B (`PollMessageEnd`).
- So a recipe must press buttons (`eidos_ui_press`) for every mart control. There is no bottom-screen rectangle to tap. If the port wants mouse control it must translate "click row k" into the right number of Up/Down presses from `BMP_MENULIST_GetPos` and then A.

---

## 13. Name entry (ov194)

### Detection

- Overlay 194 loaded; the proc is `gNameInProcData` (`ov170/box2_main.c:188`, ProcData at 0x021F5580) with init `NAMEIN_GRAPHIC_Create` (`ov194/namein_graphic_main.c:252`), main `NAMEIN_GRAPHIC_Main` (`namein_graphic_main.c:~330`), exit `NAMEIN_GRAPHIC_Delete`.
- Work: `PROCSYS_AllocProcWork(proc, 0x2d4, 30)` (`namein_graphic_main.c:262`) = `NameinGraphicWork` (`namein_graphic_main.c:170`). So `Proc.work` of the proc with `overlayId == 194`.
- Sub-state: the screen runs a callback state machine stored at work+0x2A0 (`NameinGraphicCallback`, first word = the current callback function). The callbacks are `CallbackWaitDraw` (wait for first draw), `CallbackFadeIn`, `CallbackGridAction` (live input, `namein_graphic.c:1680`), `CallbackWaitPrint` (nickname reveal text), `CallbackFadeOut`, `CallbackFinish`, `CallbackPauseSave` (`namein_graphic.c:1592-1780`). The keyboard accepts input exactly while the callback is `CallbackGridAction`.
- Who starts it: the player name (`ov088/startmenu.c:2850`, kind 0, max 7), a Pokemon nickname (`ov021/scrcmd_sub_proc_events.c:186`, kind 1, max 10; capture/PDC/egg paths `ov092/event_battle_return.c:402`, `ov227/event_pdc_return.c:245`, `ov215/egg_demo_main.c:297`), a box name (`ov170/box2_main.c:3099`, kind 2, max 8), a Wi-Fi friend name (`ov128/wf2dmap_touch.c:267`, kind 4, max 7). Scripts start it as "mode 5/6" through `EVENT_NAMEIN_Create` (`ov021/scrcmd_proc_fld.c:1997`); the kind each of those becomes is unverified.

### State

The caller's argument block is `NameInArgs` (`ov010/namein_args.h:17`, 0x2C bytes), at `NameinGraphicWork.args` (+0x2C4):

| Offset | Field |
|---|---|
| 0x00 | `kind`: 0 player name, 1 Pokemon nickname, 2 box name, 3 rival name, 4 friend name, 5 greeting, 6 thanks, 7 message (the prompt text for each kind is message bank 0x51 entries 0..7, decoded: "Your name?", "{species}'s nickname?", "Box name?", "Your rival's name?", "Your friend's name?", "How do you greet passersby?", "How do you thank passersby?", "What's your message to passersby?"; the index is the kind: `namein_graphic.c:1263` `PrintMsg(value)` with value = kind from `namein_graphic_main.c:289`). Kinds 5..7 as used by callers is unverified. |
| 0x04 | species (kind 1) or gender (kind 0) |
| 0x08 | `mon` (kind 1, a Pokemon record or 0) |
| 0x18 | `maxLen` |
| 0x1C | `canceled` / accepted flag (written by `CommitName`, `namein_graphic.c:1565`) |
| 0x20 | `name`: the `Strbuf *` result |

Live screen state, inside `NameinGraphicWork` (`namein_graphic_main.c:170-190`):

| Offset | Field |
|---|---|
| 0x068 | `inputField` (`NameinGraphicInputField`, `namein_graphic_field.c:101`): `+0x00 primary[11]` the typed text as u16 DS characters (end = 0xFFFF), `+0x16 secondary[3]` pending candidate text, `+0x1E primaryLength`, `+0x20 secondaryLength`, `+0x22 maxLength`. Absolute: text at work+0x68, length at work+0x86, max at work+0x8A. |
| 0x0FC | `grid` (`NameinGraphicGrid`, `namein_graphic.c:287`): `+0xD8/+0xDC` selected cell x,y (work+0x1D4/+0x1D8), `+0xE4 state` (0 normal, 1 commit, 2 page animation), `+0xE8 action`, `+0x100 mode` (page, work+0x1FC), `+0x104 index` (shift state 0/1, work+0x200), `+0x114 targetMode`, `+0x110 animActive`. |
| 0x230 | prompt message box |
| 0x264 | icon (portrait of the Pokemon or player), tap target `UpdateIconTouch` |
| 0x2C4 | `args` |

Pages. The page set is read from archive 0x77 (`a/1/1/9`, `ov194/namein_data.c:87`; `CreateMembers` loads members 0..4, `namein_data.c:101`). Each member is `u16 width, u16 height` then `width*height` u16 cells (`namein_data.c:GetGridCell`). `0x3000` (ideographic space) marks an empty cell in the decomp's reading; in the English data the blank cells hold 0x0020 and type a space (seen in eidos, 2026-10-26). The page index is `GridIndex(mode, index, variant)`: for modes 0..2 it is the mode, for mode 3 it is `3 + index + 2*variant` (`namein_graphic.c:GridIndex`, `variant` is not seen to change, so mode 3 shows member 3 when `index` = 0 and member 4 when `index` = 1). Mode 4 as a saved start page exists in the code (`namein_graphic_main.c:259-262` clamps >= 5 to 0) but `InitPicker` has no case for it (`namein_graphic_field.c:500-510`), so mode 4 is probably never stored (unverified).

This build's character data (read from the extracted `a/1/1/9`; North American layout; there are no kana pages in this ROM):

Page 0, "ABC" (member 0, 13 columns x 5 rows; `_` = empty 0x3000 cell):

```
A B C D E F G H I J _ , .
K L M N O P Q R S T _ ' -
U V W X Y Z _ _ _ _ _ M F      (M = male sign, code 0x246D; F = female sign, code 0x246E)
_ _ _ _ _ _ _ _ _ _ _ _ _
0 1 2 3 4 5 6 7 8 9 _ _ _
```

Page 1, "abc" (member 1): the same layout in lower case.

```
a b c d e f g h i j _ , .
k l m n o p q r s t _ ' -
u v w x y z _ _ _ _ _ M F
_ _ _ _ _ _ _ _ _ _ _ _ _
0 1 2 3 4 5 6 7 8 9 _ _ _
```

Page 2, symbols (member 2, 13 x 5):

```
,  .  :  ;  !  ?  _  _  _  _  M  F  _
"  "  '  '  (  )  _  _  _  _  _  _  _        (U+201C, U+201D, U+2018, apostrophe, parentheses)
0x246C  U+FF65  ~  #  %  +  -  *  /  =  _  _  _
0x2474 0x2475 0x2476 0x2477 0x2478 0x246F 0x2471 0x2472 0x2470 0x2473 0x2479 _ _
0x247A 0x247B 0x247C 0x247D 0x247E 0x247F 0x2480 0x2481 0x2484 0x2482 0x2483 _ _
```

The codes 0x246C and 0x246F-0x2484 are the game's own glyphs (symbols such as the ellipsis, heart, card suits, arrows, music note and similar); the tool `tools/assets/text.py` only names 0x246D and 0x246E (male, female), so the identity of the other glyph codes is unverified. The recipe should draw them from the game font (they are font code points, not Unicode).

Page 3, QWERTY (members 3 and 4, 10 x 3, `*` = code U+203B, an unused filler):

```
member 3 (index 0, lower case)      member 4 (index 1, upper case)
q w e r t y u i o p                 Q W E R T Y U I O P
a s d f g h j k l *                 A S D F G H J K L *
z x c v b n m - * *                 Z X C V B N M - * *
```

Special controls (cell values, not characters): page tabs 2, 3, 4, 5 select pages 0, 1, 2, 3 (`namein_graphic.c:1108-1135`, `ApplyKanaAction` case 2..5 calls `SetGridMode(code-2)`); 6 backspace (action 5); 7 OK/confirm (action 7); 8 shift on the QWERTY page (toggles `index ^ 1`, `namein_graphic.c:1108-1135` case 8); 9 space bar on the QWERTY page (appends code 32). `member 8` of the archive is a table of case pairs ("A a _", "B b _"... 26 rows) used by candidate conversion (`namein_data.c:NextPairChar`).

Validation: kinds 0 and 3 (player, rival) refuse an empty or all-space name (`namein_graphic.c:1584` `IsNameValid`, prompt message 8 "Please enter the name."); a name with more than four digits is refused (message 9, `namein_graphic.c:1680-1730`, `PROF_WORD_CheckDigitCount`).

### Input

Touch uses `TOUCHPANEL_GetPointTrg` and the picker's cell boxes in tile units times 8 (`namein_graphic_field.c:530-540`, `FindCellAtPoint`). The game tests the point against these rectangles in the bottom screen (name entry has main on the bottom, `BG_SYS_SetLcdSwap(0)`, `namein_graphic_main.c:274`).

Pages 0, 1, 2 (13 x 6 picker; `KanaGetBox`, `namein_graphic_field.c:622-640`; `ComputeGridValue`, `:763-767`):

- Character cell (column cx 0..12, row cy 0..4): x = 16*cx + 24 to +16, y = 24*cy + 40 to +24. Centre: (16*cx + 32, 24*cy + 52). Every cell, including empty ones, is a value-1 cell; tapping an empty cell appends the 0x3000 code (`AppendChar` has no filter, `namein_graphic_field.c:359-367`; unverified whether the game blocks it elsewhere).
- Page tabs (row 5): tab i (0..3) is x = 24*(i+1) to +24, y = 160 to 184 (centres x = 36, 60, 84, 108; y = 172).
- Backspace (value 6): x 120-176, y 160-184 (centre 148, 172).
- OK (value 7): x 176-232, y 160-184 (centre 204, 172).

Page 3 (QWERTY, 11 x 4 picker; `SymbolGetBox`, `namein_graphic_field.c:640-665`; `SymbolGetValue`, `namein_graphic.c:826-840`). Rows use tile offsets from overlay data `d_021F5544 = {5, 6, 7}` (x tile of column 0 per row) and `d_021F5550 = {8, 12, 16}` (y tile per row):
- Letter key at grid column c of row r (0..2): x = 8*({5,6,7}[r] + 2*c), width 16; y = 8*{8,12,16}[r], height 24. So row 0 (q w e r t y u i o p) starts at x = 40, row 1 (a s d ...) at x = 48, row 2 (z x c ...) at x = 56, with y = 64, 96, 128. Centre of key c of row r: (x + 8, y + 12).
- Backspace (value 6): tiles (25, 8) size 4 x 3: x 200-232, y 64-88.
- OK (value 7): tiles (24, 12) size 5 x 3: x 192-232, y 96-120.
- Shift (value 8): left tiles (3, 16) size 4 x 3: x 24-56, y 128-152; right tiles (23, 16): x 184-216, y 128-152.
- Space bar (value 9): tiles (15, 20) size 12 x 3: x 120-216, y 160-184.
- Page tabs: tab i (0..3) x = 24*(i+1) to +24, y = 160 to 184.
- Note for row 2: picker x 0 and x 9 are shift keys, picker x 1..8 are the letters at grid columns 0..7 (`SymbolGetValue` case 2, `namein_graphic.c:826-840`).

Icon tap: tapping the portrait/icon (centre = the icon actor's surface position, rectangle +-16 px, `namein_graphic.c:1474-1495`) fills the field with a suggested name (action 0, `GetNameForAction`, `namein_graphic.c:1680-1730`). Its position is a run-time value (unverified).

Buttons (`KanaKeyAction` `namein_graphic.c:1070-1097`, `KanaKeyMove` `:1038`): d-pad moves the cursor, A types / presses the control under the cursor, B backspace (cell value 6), Select cycles the page (0 -> 1 -> 2 -> 3 -> 0), X jumps the cursor to the OK cell (value 7), R (0x100) reverts the last conversion (action 10). On the QWERTY page A and Select work, the d-pad moves (`SymbolKeyMove/SymbolKeyAction`, `:1137-1197`).

---

## 14. Xtransceiver (ov172, "CTVT")

### Detection

- Overlay 172 loaded; proc init `COMM_TVT_SYS_ProcInit` (`ov172/comm_tvt_sys.c:511`), main `ProcMain` (`:543`), exit `ProcExit` (`:531`).
- Work: `CtvtWork`, 0x1FC bytes (`PROCSYS_AllocProcWork(proc, 0x1fc, 109)`, `comm_tvt_sys.c:516`; struct `ov172/ctvt_types.h`, search `CtvtWork`). It is `Proc.work`.
- Sub-state: `CtvtWork.screen` (+0x08) = `CTVT_SCREEN_TALK` 1, `CALL` 2, `DRAW` 3, `CAM_SETUP` 4, `CAM_WIPE` 5 (`ctvt_types.h`). The screens are: the call/connect menu (`ctvt_call.c`), the talk screen (`ctvt_talk.c`), the drawing board (`ctvt_draw.c`). `nextScreen` (+0x0C) is the pending change.

### State

- `CtvtWork`: `memberCount` +0x18 (connected people, 1..4), `selfSlot` +0x19, `halfRes` +0x1C (camera frames at half resolution), `messageActive`, `viewSwapped` +0x24 (toggled by the swap button: swaps the camera layout), `inputLocked`, `endRequested`, `comm` (`CtvtComm *`), `talk` (`CtvtTalk *`), `draw`, `call`, `launch` (`CtvtLaunchParams`, `mode` 0/1 menu, 2 invited, 3 field call). Accessors are in `ov172/comm_tvt_sys.h:21-69` (`COMM_TVT_SYS_GetMemberCount`, `GetSelfSlot`, `GetViewSwapped`, `GetHalfRes`).
- Peers: `CtvtComm.peers[4]` (`CtvtPeerSlot`, stride 0x40: `active`, `connected`, `isSelf`, `hit`, ...), peer names at `CtvtComm.peerName[18]` (three 6-byte records), `currentSlot` = last touched slot.
- Talk screen: `CtvtTalk` (0xAC bytes, `ctvt_types.h`): `state` (+0), `levelIndex` +0x0D (0..8, 4 = centre), `voiceActive` +0x8C, `voiceIconState` +0xA4 (0 idle, 1 sending, 2 receiving), `waveformWin`, `messageWin`. The talk state used for input is 5 (`ctvt_talk.c:727-748`).
- The camera image itself is in the game's own VRAM layer (BG3 main); a recipe cannot read it from these structs (the frame buffer is `CtvtComm.frameBuf` / the camera view `CtvtCamView`, not examined in detail here).

### Input

Talk screen, state 5 (`ctvt_talk.c:727-760, 884-905, 1140-1155`). Tables are overlay-172 data (read from the decompressed overlay). The hit tables all share a 12-byte stride with two rectangles and a terminator, starting at an odd address.

| Control | Where tested | Rectangle (y, x) | Effect |
|---|---|---|---|
| Leave | table 0x021C6F77 entry 0 (`ctvt_talk.c:736`; right = 0 means "to the right edge") | y 168-192, x 232-255 | state 9, leave prompt |
| View swap | table 0x021C6F77 entry 1 | y 104-160, x 216-255 | toggles `viewSwapped` (`:745-749`) |
| Back to talk | table 0x021C6F83 (`:727`) or X (0x400) | entry 0: y 8-88, x 184-240; entry 1: y 40-104, x 208-255 | state 3 / substate 6 (`:727-733`) |
| Talk (mic) | `CTVT_TALK_HitCenter` (`:1140-1155`) | a ring around (128, 104): distance from the centre > 16 and < 72 px, only for y >= 104 | starts sending voice; also L (0x100) or A (0x1) (`:716-722`) |
| Level slider | table 0x021C6F8F (`:884-905`) | entry 0: y 8-40, x 16-72; entry 1: y 32-77, x 0-64 (both raise the level); entry 2: y 77-160, x 0-48 (lowers) | voice level 0..8 |
| Half resolution | L (0x200) on slot 0 (`:769-775`) | none | toggles camera resolution |

Call/connect screen: the back button is one rectangle y 168-192, x 224-248 (`ctvt_call.c:1281`, overlay data 0x021C7040), B (0x2) also leaves. Slot selection by touch is done in `ctvt_call.c:1188-1189` with the same point test used for the member tiles (rectangle per member not tabulated: unverified).

Drawing board (`ctvt_draw.c`): panel buttons `gCtvtDrawPanelButtonRects` (0x021C6FF8), size picker `gCtvtDrawSizePickerRects` (0x021C7014), stamp picker `gCtvtDrawStampPickerRects` (0x021C6FDC) (`ctvt_draw.c:637,677,692`); the drawing area is the touch points themselves (`:782-850`); the bar below y = 0xA8 is the tool bar (`:789`). The rectangle values were not dumped here (unverified).

---

## 15. Title screen and title menu (`ov088`)

### Detection

These scenes run on the global proc stack, before and outside the game system: `rootp = g_ProcSys->current` (section 0.2). All of them are procs of overlay 88 (`rootp->overlayId == 88`), told apart by their ProcData. The ProcData objects are DS data at the addresses below (`ov088/startmenu.c:823-849` externs); the init/main/end function addresses stored at each were checked against `layout/function_addrs.tsv`:

| Proc | ProcData (ov088) | init / main / end | Scene |
|---|---|---|---|
| Logo and copyright screens | `0x021BD8E8` | `STARTMENU_LogoScreenInit/Main/End` | company logo, then copyright (states 0 to 13, `ov088/startmenu.c:3243-3379`) |
| Title screen | `0x021BD574` | `STARTMENU_TitleDemoInit/Main/Exit` (`:1035`, `:1098`, `:1158`) | title with the 3D logo and attract loop |
| Title menu | `0x021BD66C` | `STARTMENU_MenuInit/Main/Exit` (`:1480`, `:1494`) | CONTINUE / NEW GAME / ... |
| New game intro launcher | `0x021BD818` | `STARTMENU_NewGameIntroInit/Main/End` | hands over to ov204 |
| Config prompt | `0x021BD830` | `STARTMENU_ConfigPromptInit/Main/End` | language/gender/battle-style question |
| Save erase | `0x021BD974` | `STARTMENU_SaveEraseInit/Main/End` | hidden save erase |

`Proc.data` is an address inside the overlay, and overlays share RAM ranges, so the same address value can be another overlay's code. Test the pair `(overlayId == 88, data->init == the function above)`; the host build exposes the function pointers, so comparing `proc->data->init` to `STARTMENU_TitleDemoInit` / `STARTMENU_MenuInit` is the robust test. (The opening cinematic is ov179 with ProcData `0x021B68D8`, pushed by `OVERLAY_LAUNCH_Driver`, `arm9/app_power.c:412-442`; the new-game intro is ov204, which this document does not cover.)

How the title is entered: `OVERLAY_LAUNCH_Driver` states 0 to 4 push logo (ov088), cinematic (ov179, three stages), then the title (`PROCSYS_PushGlobal(ov088, 0x021BD574)`, `arm9/app_power.c:442`). The title proc ends with `result` 0 (A or START pressed: continue to the menu) or 1 (timed out: restart the attract loop) or 2/3 (hidden key combos, held keys 0x46 or 0xC80) (`STARTMENU_TitleDemoCheckInput`, `ov088/startmenu.c:1065-1096`). The menu is started by `PROCSYS_ReplaceGlobal(ov088, 0x021BD66C)` (`:1165`).

Sub-states:

* Title screen: `TitleDemoWork` (0x98 bytes, `Proc.work`; `ov088/startmenu_types.h`): `state` 0x00, `result` 0x04, `frameCount` 0x8C, `phase` 0x94. Phase 0: the first 0x29A = 666 frames (the 3D logo, `STARTMENU_TitleDemoUpdateTimer`, `ov088/startmenu.c:1046-1064`); at frame 0x29A the screens swap and phase becomes 1; A, B or START in phase 1 sets phase 2; A or START (`KEY_GetTrg() & 9`) sets `result = 0` and `state = 4` (fanfare and fade to the menu); after 0x1248 = 4680 frames without input `result = 1`.
* Title menu: `StartMenuWork` (0x150 bytes, `Proc.work`, allocated in `STARTMENU_MenuInit`, `ov088/startmenu.c:1480-1493`): `rowIds[8]` 0x120, `cursorIndex` 0x128, `selectedRow` 0x129, `subState` 0x12A, `state` 0x13C (index into `gStartMenuStateTable`: 0 setup, 1 wait start, 2 wait fade, 3 select animation, 4 wait print, 5 select row (the normal state), 6 scroll, 7 connect prompt (CONTINUE with C-Gear restrictions), 8 confirm prompt (NEW GAME with an existing save), 9 info message, 10 unlock notices, 11 begin fade in; the order is inferred from the return values: `STARTMENU_StateSelectRow` returns 5 to stay, 6 to scroll, `BeginSelectAnim(work, 7/8/9/11)` queue the next state, `ov088/startmenu.c:1674-1721`), `saveStatus` 0x148, `noticeIndex` 0x14C.

### State

* The row list is `StartMenuWork.rowIds[]` filled by `STARTMENU_BuildRowList` (`ov088/startmenu.c:2358-2387`): id 0 only if a flash save exists (`SAVE_CONTROL_HasFlashSave`), then ids 1 and 2, id 3 and id 4 only if `MISC_GetSlotState(unlockData, 1)` / `(unlockData, 2)` is set, ids 5 and 6, id 7 only if `MISC_GetSlotState(unlockData, 3)`, and id 8 (blank) as padding up to 8 entries. The labels are bank 179 of `a/0/0/2` (decoded): 0 CONTINUE, 1 NEW GAME, 2 MYSTERY GIFT, 3 NINTENDO WFC SETTINGS, 4 BATTLE COMPETITION, 5 GAME SYNC SETTINGS, 6 RELOCATOR, 7 MIC TEST. The row ids are not the message numbers: the inventory identifies the always-present rows with no save as NEW GAME, MYSTERY GIFT, NINTENDO WFC SETTINGS and MIC TEST, which means ids 5 and 6 print messages 3 and 7, and ids 3, 4 and 7 (the unlock-gated rows) print among messages 4 to 6. Which message each id prints is decided by the code that fills the 14 row windows (`STARTMENU_CreateRowWindows`, `ov088/startmenu.c:2423-`, `gStartMenuWindowTemplates`) and was not traced to the end: unverified. The actions are certain from `STARTMENU_StateSelectRow` (`:1674-1721`): id 0 CONTINUE (next state 7), id 1 NEW GAME (state 8, the overwrite-warning confirm), id 2 and ids 3, 4, 7 and 6 go to state 11 (fade into another proc: Mystery Gift ov121, the battle/competition and relocator procs), id 5 goes to state 9 (info message).
* The cursor row is `rowIds[cursorIndex]`. The CONTINUE row shows save info (player name, play time, Pokedex count, badges: messages 9 to 12 of the same bank, e.g. "Time: h:mm", "Pokedex: n", "Badges: n"), read from the save (`StartMenuWork.saveData`/`playerData`).
* Prompts: messages 17 to 35 of bank 179 (wireless warnings, "Launch C-Gear communications?", "There's already a saved game file...", "A Button: Begin adventure", "B Button: Return to the menu").

### Input

* Title screen: keys only: A or START (`KEY_GetTrg() & 9`) to proceed, A, B or START (`& 0xB`) also act in phase 1 (`ov088/startmenu.c:1065-1096`). No touch. A recipe's "click anywhere" should send START (0x008) for a few frames.
* Title menu: keys only in `STARTMENU_StateSelectRow` (`ov088/startmenu.c:1674-1721`): UP (repeat 0x40) and DOWN (repeat 0x80) move the cursor one row (`STARTMENU_MoveCursor`), A (0x01) activates the row under the cursor, B (0x02) goes back to the title. There is no touch table in the menu code (the only `TOUCHPANEL_HitTrg` in the file is the config prompt, `:3215`). A click on row `k` therefore has to be sent as (`k - cursorIndex`) presses of UP or DOWN, each separated by the key-repeat delay (the state returns 6 to scroll; the next move is accepted after the scroll, 3 steps, `STARTMENU_StateScroll`, `:1727-1747`), then A. The prompts use A and B (`STARTMENU_StateConfirmPrompt`, `:1847-1880`); the connect prompt uses a yes/no menu created with `BMP_MENU_CreateYesNo` at tile (0x18, 0xD) on layer 0 (`STARTMENU_OpenYesNoMenu`, `:1814-1826`), whose touch support was not read: unverified.
* Config prompt (hidden): hit table `d_021BD83C` (ov088 `0x021BD83C`, tested with `TOUCHPANEL_HitTrg`, `ov088/startmenu.c:3215-3220`): entry 0 is answer 1, entry 1 is answer 2; rectangles not dumped here: unverified. Keys: A confirms the highlighted answer.

---

## 16. C-Gear (`ov029`, `ov030` no-gear, `ov031` Union Room)

### Detection

The C-Gear is the field sub-screen. It is never a proc.

| Panel | Test | Overlay |
|---|---|---|
| C-Gear | field up and `sub->mode == 0` (or 8, "with callback", used when another feature starts the C-Gear) | ov029 (id 0x1D, `FIELD_SUBSCREEN_InitCGear`, `GF_OVERLAY_Load(abs_id_1d)`, `ov021/field_subscreen.c` after `Create`) |
| No-Gear (save has no C-Gear, or zone 34 / the 0x4B pair) | `sub->mode == 5` | ov030 (id 0x1E, `FIELD_SUBSCREEN_InitNoGear`, `ov021/field_subscreen.c`) |
| Union Room panel | `sub->mode == 2` | ov031 (id 0x1F) |
| Entralink | `sub->mode == 3` | ov018 |
| Beacon view, dowsing, report, power on/off | modes 4, 6, 7, 9 | ov032, ov033, ov034, ov035 |

(`FIELD_SUBSCREEN_ResolveMode` redirects mode 0 to 5 when there is no C-Gear, `ov021/field_subscreen.c:427-441`.) `FIELD_SUBSCREEN_GetCGear(sub)` returns the C-Gear work pointer only in modes 0 and 8 (`ov021/field_subscreen.c:719-724`). Sub-state: `CGearWork.stepFunc` (+0x000): `C_GEAR_StepIdle` (normal), `C_GEAR_StepOpen`, `C_GEAR_StepClose`, `C_GEAR_StepIntroEnd`, `C_GEAR_StepWaitSlots`, `C_GEAR_NoOp` (a panel event is running) (`ov029/c_gear.c`; `C_GEAR_StepIdle` at the idle test below). The panel only reacts when `C_GEAR_IsIdleForInput`: the wipe is finished, the master brightness is 0 and `FIELDMAP_GetSubscreenBusy` is 0 (`ov029/c_gear.c:1219-1225`). While a field event runs, the panel is updated with `active = 0` (`FIELD_SUBSCREEN_UpdateCGear(work, FIELDMAP_IsEventActive == 0)`, `ov021/field_subscreen.c:Main`) and shows its "off" look (`C_GEAR_Update(work, powerOn)`, `ov029/c_gear.c:2449`); the C-Gear has no touch input during events beyond what `commEvent`/indicators allow.

### State

`CGearWork` (`sub->cgear` in modes 0 and 8; `ov029/c_gear_types.h`, DS offsets): `stepFunc` 0x000, `gameSystem` 0x01C, `hexMap` 0x020 (the C-Gear save block, `CGearSave*`), `hudActors[11]` 0x12C (clock digits 1 to 5, battery 6, icons; indexes 8 and 9 hide when `commEnabled == 0`), `tileMarkerActors[3]` 0x158, `barActors[3]` 0x164 (beacon bars), `beaconColors[17]` 0x1B8, `command` 0x214 (pending command code), `lastCommand` 0x218, `tiles[9][4]` 0x2D0, `grid[9][4]` 0x3F0, `activeIcon` 0x416, `selectedCol`/`selectedRow` 0x452/0x453, `powerOn` 0x454, `phase` 0x455, `paintMode` 0x457 (the paint toggle), `skinIndex` 0x458 (0 to 4), `colorTheme` 0x459, `commEnabled` 0x45A (wireless on), `agbSlotPresent` 0x45B.

* Clock: `C_GEAR_UpdateClock` reads the cached RTC (`GF_STANDARD_GetCachedTime`): hour tens/units, a colon that blinks with `second & 1`, minute tens/units; the battery icon is `C_GEAR_GetBatteryFrame()` on DSi and `UI_GetPowerState() == 5` otherwise (`ov029/c_gear.c:2024-2061`). A recipe can read the same sources.
* The board: 9 columns by 3 or 4 rows of hexagonal cells; the kind of cell (col, row) is `C_GEAR_GetCellKind` = `CGEAR_SAVE_GetPanelValue(hexMap, col, row)` when `commEnabled`, else 0 (`ov029/c_gear.c:2633-2639`); `C_GEAR_IsCellMarked` reads the mark flag. Kinds 1, 2, 3 start commands 1, 5, 6 when tapped and 4 is a special paint kind (`C_GEAR_HandleButton` case 0, `ov029/c_gear.c:1626-1665`); the names of the kinds (which radio feature each is: IR, Wi-Fi, wireless) are not in the C: unverified. Command 5 is the Wi-Fi command (`C_GEAR_IsLastCommandWifi` returns `lastCommand == 5`, `ov029/c_gear.c`).
* The icons along the bottom (x = 26, 64-144, 166, 190, 214): commands 0xB, 7, paint toggle, 0xC, 0xD (below).

### Input

The C-Gear creates one `BUTTON_MAN` (`BUTTON_MAN_Create(d_021F3272, C_GEAR_OnButtonEvent, ...)`, `ov029/c_gear.c:1954`), updated every idle frame (`BUTTON_MAN_UpdateSlots`, `ov029/c_gear.c:2089`); `C_GEAR_OnButtonEvent` acts on the press only (`buttonState == 0`) and not while a splash indicator is shown (`ov029/c_gear.c:1699-1703`). Hit table `d_021F3272` (ov029 `0x021F3272`, dumped):

| Index | Rectangle (x, y) | Tap centre | Action (`C_GEAR_HandleButton`, `ov029/c_gear.c:1626-1697`) |
|---|---|---|---|
| 0 | x 0-255, y 32-160 | cell centres, see below | the board: the touched cell is found with `C_GEAR_HitTestTile` (`:668-694`); in paint mode a tap cycles the cell kind `(kind + 1) % 5`; otherwise kinds 1, 3, 2 set `command` to 1, 6, 5 and kind 4 does nothing |
| 1 | x 166-182, y 162-178 | (174, 170) | toggle paint mode (`paintMode ^= 1`) |
| 2 | x 190-206, y 162-178 | (198, 170) | command 0xC (12) |
| 3 | x 64-144, y 162-178 | (104, 170) | command 7 |
| 4 | x 26-42, y 162-178 | (34, 170) | command 0xB (11) |
| 5 | x 96-160, y 14-30 | (128, 22) | cycle the skin: `skinIndex = (skinIndex + 1) % 5`, saved to the C-Gear save (`CGEAR_SAVE_SetPowerSetting`) |
| 6 | x 214-230, y 162-178 | (222, 170) | command 0xD (13); the only icon (with the board) that works when `commEnabled == 0` |

The commands act in `C_GEAR_HandleCommand` (`:2540-2590`): 1 and 6 start a panel event (or ripple the board when the call tab is active), 5 starts the Wi-Fi panel event, 12 and 13 start panel events with a different sound, 7 and 11 start panel events unless the call tab is active. What each command opens is in `C_GEAR_StartPanelEvent` and was not traced: the command-to-feature names (power, Wi-Fi, IR, Entralink, Union) are unverified, except that 13 is reached by the icon at the far right and 5 by the Wi-Fi tile.

Board cell tap centres: `C_GEAR_HitTestTile` computes `col = (x/8 + 2) / 4`, `parity = col % 2`, `row = (y/8 - rowOffset[parity]) / 4` with the ROM pairs `d_021F333C` = {6, 4} (row offset in tiles) and `d_021F334C` = {3, 4} (rows per column), valid when `col < 9` and `row < rows[parity]` (`ov029/c_gear.c:668-690`). So even columns have 3 rows starting at y = 48, odd columns 4 rows starting at y = 32, every cell 32 by 32 px; the tap centre of cell (col, row) is `x = clamp(32*col, 8, 248)`, `y = 8*rowOffset[col & 1] + 32*row + 16`. The cell's kind must be non-zero for the tap to do something.

Keys: the C-Gear has no key controls on the field except the key-cycle used for the skins and tabs (`keyCycle` 0x44D; `C_GEAR_UpdateTabInput`, `ov029/c_gear.c:746`): unverified.

---

## 17. Overworld player sprite (for drawing the player walking in a menu)

### How the sprite is chosen

1. The player is an MMDL (field model) whose object code comes from a form table indexed by sex and avatar form: `gFieldPlayerFormTable` at ov021 0x021D890C (`ov021/field_player.c:34,172,401-404`, rows `FieldPlayerFormRow`, `ov021/field_player_types.h:43-51`). Overlay data, entry = (objCode u16, formCode u16), 10 forms per sex:

| Form slot | Male objCode (formCode) | Female objCode (formCode) |
|---|---|---|
| 0 normal walking | 1 (0) | 4 (0) |
| 1 cycling | 2 (1) | 5 (1) |
| 2 surfing | 3 (2) | 6 (2) |
| 3 diving | 3 (3) | 6 (3) |
| 4..9 other forms (form code 4) | 0x88, 0x8A, 0x8C, 0x9E, 0xA0, 0x9C | 0x89, 0x8B, 0x8D, 0x9F, 0xA1, 0x9D |

`FIELD_PLAYER_OBJ_CODE_HERO = 1`, `FIELD_PLAYER_OBJ_CODE_HEROINE = 4` (`field_player_types.h:38-39`). The row is `FieldPlayerCore.sex` (+0x18, `field_player_types.h:73`), taken from `MYSTATUS_GetSex` (0 male, 1 female; confirmed by the same test in the field menu icon code, `ov021/field_menu.c:CreateItems`).
2. The object code maps to a parameter record: archive 0x30 (`a/0/4/8`, one member), record = 4 + index * 0x1C bytes, index = `FLDMMDL_ObjCodeToTableIndex(objCode)` (`ov010/fldmmdl.c:2098`, `arm9/fldmmdl_save.c:356`: codes below 0xDF are the index itself, so 1, 4, 0x88.. are their own index). Layout `MmdlObjCodeParam` (`ov010/fldmmdl_types.h:36-53`). Decoded records (read from the extracted `a/0/4/8`):

| objCode | drawType | drawProcNo | texSize | animId | sex | resource id (u32 at +0x10) |
|---|---|---|---|---|---|---|
| 1 hero walking | 1 (billboard) | 1 | 23 | 1 | 0 | 6 |
| 2 hero cycle | 1 | 3 | 23 | 7 | 0 | 7 |
| 3 hero surf/dive | 1 | 4 | 23 | 4 | 0 | 8 |
| 4 heroine walking | 1 | 1 | 23 | 1 | 1 | 9 |
| 5 heroine cycle | 1 | 3 | 23 | 7 | 1 | 10 |
| 6 heroine surf/dive | 1 | 4 | 23 | 4 | 1 | 11 |

`mdlSize` is 0 for all of them, i.e. a 32 x 32 cell (`sBlactCellSize[0]` = 0x20 x 0x20, ov021 0x021D75C4) drawn as a 2.0 x 2.0 world-unit billboard (`sBlactDrawSize[0]` = 0x2000 x 0x2000, 0x021D75D0).
3. The sprite is a billboard (not a 3D model). The resource id is a member of archive 0x31 (`a/0/4/9`, a NARC of 764 members): `FLDMMDL_BLACT_LoadAddRes` opens it with `G3D_SYSTEM_CreateResourceArcHandle(resArcHandle, resId)` (`ov021/fldmmdl_blact.c:681-700`, `ov010/fldmmdl.c:374` opens archive 0x31). Hero walking = member 6, heroine walking = member 9 (each 17448 bytes, an NSBTX `BTX0` texture file whose TEX0 data block is 16384 bytes). The billboard shows cell `frame` of that texture; the cell size is 32 x 32 (sizes above). The exact texel format and the number of cells in the texture were not decoded here (16384 bytes is consistent with 32 cells of 32 x 32 at 4 bits, which would also fit frames 0..23; unverified).
4. The draw procedure for drawProcNo 1 is `FLDMMDL_DRAW_InitDirAnime` / `FLDMMDL_DRAW_DrawDirAnime` (draw proc list entry 1 at ov021 0x021D85B8, pointer table 0x021D770C; `ov021/fldmmdl_draw.c:188-216`). It selects a command list with `listIndex = dirIndex + anime * 4` (`FLDMMDL_DRAW_UpdateDirAnime`, `fldmmdl_draw.c:993-1050`). `dirIndex` is the model's facing relative to the camera (`FLDMMDL_DRAW_GetDirIndex`, `:1057-1064`; 0 up, 1 down, 2 left, 3 right, `fldmmdl_acmd.c:207-210`). `anime` is the model's draw status.

### Walk frames

The command lists of animation id 1 are `sBlactAnimeTable[1]` (ov021 0x021D7F68; `FLDMMDL_BLACT_GetAnimeTable`, `fldmmdl_blact.c:1049`): 48 lists of `BbActCmd { u16 frame:14, flipX:1, flipY:1; u16 arg (duration in frames) }`, control values 0x3FFC JUMP, 0x3FFD LOOP, 0x3FFE SET_LIST, 0x3FFF END (`arm9/billboard_act.c:84-96`, played by `BILLBOARD_ACT_UpdateEntry` `:613-660`). Decoded (no flips are used):

Frame numbers per direction (cell index in the texture; same for all speeds):

| Direction (dirIndex) | stand | step A | step B | run frames (3) |
|---|---|---|---|---|
| up (0) | 0 | 1 | 2 | 12, 13, 14 |
| down (1) | 3 | 4 | 5 | 15, 16, 17 |
| left (2) | 6 | 7 | 8 | 18, 19, 20 |
| right (3) | 9 | 10 | 11 | 21, 22, 23 |

A walking cycle is step A, stand, step B, stand, each held for the tick count below, then JUMP back to the start: for up it is 1, 0, 2, 0. Draw statuses (`fldmmdl_acmd.c:219-231`) and their lists:

| anime (draw status) | Meaning | Lists (dir 0..3) | Ticks per frame |
|---|---|---|---|
| 0 STOP | standing | 0-3 | single frame (0, 3, 6, 9) |
| 1 WALK_32F | | 4-7 | 16 |
| 2 WALK_16F | | 8-11 | 8 |
| 3 WALK_8F | the normal player walk (ACMD 0x0C + dir: `field_player_grid.c:106,1039`; `fldmmdl_acmd.c:770-788`) | 12-15 | 4 |
| 4 WALK_4F | the player's run with running shoes (ACMD 0x10 + dir, `field_player_grid.c:107,1025`) | 16-19 | 2 |
| 5 WALK_2F | the dash (ACMD 0x14 + dir, `field_player_grid.c:108,1021`) | 20-23 | 1 |
| 6 WALK_6F, 7 WALK_3F, 8 WALK_7F, 9 WALK_12F | other NPC speeds | 24-39 | 3/2/4/6 (the two-tick pairs differ per step) |
| 10 DASH_4F, 11 DASH_6F | the 3-frame run frames 12..23 | 40-47 | 4 |

The mapping of which ACMD the player uses for walk, run and dash is from `field_player_grid.c:1019-1040` (unverified for slopes and special tiles: ACMD 0x4C, 0x58, 0xA3, 0xA7 are the other variants, `field_player_grid.c:113-117`). Cycle (object 2/5, draw proc 3, animation id 7) and surf (object 3/6, draw proc 4, animation id 4) use other command lists (`sBlactAnimeTable[7]` and `[4]`, not decoded here).

### Draw it in a menu

A menu that wants "the player walking" needs only: sex (`MYSTATUS` sex: 0 male, 1 female), the texture (`a/0/4/9` member 6 male or 9 female), and the frame sequence for the chosen direction, for example walking down at the normal speed: frames 4, 3, 5, 3, each 4 ticks (a tick is one game frame at 60 Hz in normal play; the game drops to 30 Hz for the field in some cases, `arm9/gamesystem.c:GAMESYSTEM_Main`, `UI_SetFrameRate(0x1e)` while the walk count is enabled). A cell is 32 x 32 px, drawn in world units 2.0 x 2.0 (so a 1:1 pixel draw is the cell itself). The palette comes from the same NSBTX (`BILLBOARD_ACT_UploadSlotPalette`, `arm9/billboard_act.c`, via the slot's resource). The X-menu and Bag icons in the game do not use this sprite, they use the app icon archive (`ov021/field_menu.c:FIELD_MENU_LoadScreen`, `APP_ICON_*`).

Unverified here: the exact NSBTX layout (cells per row, 4 or 8 bits, palette index), the pixel anchor (`offsetX/Y/Z` in the record at +0x0D..+0x0F are 0 for the player records read), and how the field flips left/right (the command lists use no flips, so left and right are separate cells).

---

## 18. What is solid and what is unverified

### Solid (the code or the ROM data settles it)

* Detection of every app that is a proc: `G->procSys->current->overlayId` and `bw_overlay_is_loaded(id)` (bag 70, party list 91, summary 131, Pokedex 212, options 68, PC 170, name entry 194, Xtransceiver 172, battle 93). The work struct is `Proc.work` for each (allocation sites cited per section).
* The field is up exactly when `G->fieldMap != 0` and `mainState == 1`; "nothing open" needs `G->event == 0` and the lower panel idle in mode 0 or 5. The X menu is sub-screen mode 1 with the `FieldMenu` in `sub->cgear`; the save report is mode 7.
* Apps started from the X menu run with the field closed (`fieldMap == 0`) while the shortcut event stays alive.
* The touch hit tables (bag, X menu, Pokedex list and detail bar, party plates and buttons, summary buttons, trainer card, battle action/move/target/rotation/bag/party, PC boxes, name entry, Xtransceiver, C-Gear, options choices and buttons) were dumped from the ROM and agree with the code that reads them. Spot checks repeated by the author of this document on the X menu, bag, options, C-Gear, battle action and move tables and the PC box grid matched.
* Contents: bag slots and pocket order, X-menu layouts and labels, options rows and labels, report lines, party and summary fields (`POKE_TOOL_GetPartyValue` ids), Pokedex seen/caught encoding, battle Pokemon fields and move PP, PC box accessors, name entry pages (decoded from ROM), the battle bag "last item used" location, the player sprite resource chain and walk frame lists.
* Corrections to the inventory: the X-menu button order (Pokemon first, then Pokedex), ov110 is the easy-chat input, the trainer card and Pokedex run private proc stacks, the ov205 screen is the game-clear save.

### Unverified or open

* How `engine/ui` reaches the game's globals: `_GameSystemGlobal` and `g_ProcSys` are game data symbols; whether the port exposes them (or expects a host accessor) is not settled by the decomp. The same holds for including the game headers from `engine/ui`.
* A single "a message window is showing" boolean for the field; the field message layer has per-window state only.
* Poke Mart: it is a script-VM wait function (`ShopTask`/`ShopWork`), not a proc, and has no touch; a port hook is needed to see it (section 12).
* Battle: the bag and party work structs have no global pointer; the only run-time route found is the task list of the battle effect owner. Which side of the 3D field each view position is on, what triggers the counter screen, and the Taunt/Encore restrictions at the move menu (checked after the tap) were not settled (section 7).
* Several button meanings are only partly named in the code: bag side buttons 7 to 10 (actions are certain, art is not), the Pokedex list Y flag, summary Y toggle, trainer card table entries 3, 7, 8, 9 and the badge viewer rectangles, C-Gear commands 5/7/0xB/0xC/0xD to features, PC strip/bar tables.
* Title menu: the mapping of row id to label, and the touch support of the yes/no windows (`BMP_MENU_CreateYesNo`). The title screen and menu have no touch of their own.
* The key press after a touch (`UI_GetInputMode`) only shows the cursor in most apps; a recipe that uses buttons should press twice or use taps.
* All offsets are DS 32-bit layouts; use the real headers.
