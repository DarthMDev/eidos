# Unified view: scene and element inventory

This is the code-derived inventory behind [`unified-view.md`](unified-view.md).
It lists every screen or scene that can appear, what each DS screen shows,
and the composition rule for the single 256x192 canvas.

Evidence is `file:line` in the aletheia checkout, or a DS address where the
record lives in overlay data. **Unverified** means the code did not settle
the question; it is not a guess.

The port's own mapping: `POWCNT1` bit 15 set means the main engine is on the
TOP screen (`engine/g2d/hwcompose.c:1421`). `BG_SYS_SetLcdSwap(1)` therefore
means main-on-top; `(0)` means main-on-bottom. BG ids 0-3 are main BGs, 4-7
sub BGs (`aletheia/src/game/arm9/bg_sys.c:1081-1108`).

Sections:

1. Boot, title and opening
2. Field, C-Gear, start menu and text
3. Field menus and apps
4. Battle
5. Special and other screens
6. Cross-cutting rules

## 0. Verification pass (code audit)

A second code-only sweep checked the inventory against every overlay and
`BG_SYS_SetLcdSwap`/`GX_SetDispSelect` call. The sections below are the
detailed inventory. Where this table disagrees with a section, this table
wins; the detailed section is corrected underneath where practical.

### Corrected screen assignments

| Item | Correction | Evidence |
|---|---|---|
| Bag (3.5) | The bag is **main engine routed to the bottom** by `GX_SetDispSelect(GX_DISP_SELECT_SUB_MAIN)`. Main BG0-6 visible; sub planes cleared, only sub OBJ enabled. Top is black+OBJ, not "unwritten/field". | `ov070/itemmenu.c:2727,2772-2778,2856,2877` |
| Options (3.11) | `BG_SYS_SetLcdSwap(0)`: rows/title/labels/message are main -> **bottom**; only the help/description is sub -> **top**. | `ov068/option_menu.c:517,617-619,693` |
| Title attract (1.5) | BG6 and BG7 are **sub** layers, so they are on the **top** while main is bottom. After frame 666, BG6/logo/sub-OBJ hide but BG7 stays visible, so the bottom is not black. | `ov088/startmenu.c:1246-1261,1284,1048-1054`; `arm9/bg_sys.c:1103-1131` |
| New-game intro (1.9) | ov204 enables **sub BG4 and BG6** + sub OBJ; text and menu windows are main BG1. Bottom has BG content; text/menu are not "on the other screen". | `ov204/intro_graphic.c:498-501,511`; `intro_msg.c:326-330` |
| Save/report (3.9) | Save is field-menu **button id 5**, but dispatch **kind 4**. Kind 5 is Options/ov068. | `ov010.s:72773-72780`; `ov021/field_menu.c:833` |
| Field-menu order (2.3) | Layout 0 is `1,2,3,4,5,6,0` = **Pokedex, Pokemon**, then Bag, Trainer, Save, Options. `0x961` adds button 1 (**Pokedex**); `0x962` adds button 2 (**Pokemon**). The earlier text inverted both. | `ov021.s:200982-201008`; `ov021/field_menu.c:769-785` |
| Title-menu state count (1.6) | The state table has **12** entries (0-11); `nextState == 12` exits. | `ov088/startmenu.c:1496-1498`; `ov088.s:9158-9168` |
| Pokedex (3.0/3.7) | ov212 pushes **ov211, ov209, ov210, ov208**; **ov206 is loaded by ov208 detail**, not by ov212. | `ov212/zukan.c:275-282,307-321,366-376,414-428`; `ov208/zukan_detail_info.c:387` |
| ov178 (S21) | ov178 `PARTY_STATUS_*` is a network controller/wrapper that loads ov091 and ov131; it has **no screen of its own**. | `ov178/party_status.c:4-20,292-304` |
| Wi-Fi 2D map (S4) | The map overlay sets `BG_SYS_SetLcdSwap(0)` (main bottom); main BG1-3 and sub BG4-7 both load. The "top map" claim is contradicted; art split unverified. | `ov129/wf2dmap_scene.c:540-553`; `ov128/wf2dmap_friend.c:647` |
| Trainer card (3.8) | `VIEWER_MAIN_InitBg` sets swap 1, then `VIEWER_TEXT_InitVram` sets `GX_DISP_SELECT_SUB_MAIN` (main bottom). The stated card/stats split is **unverified**. | `ov111/viewer_main.c:841,848-878`; `viewer_text.c:997-1000` |
| X-menu reachability | Kinds 6 (ov131 summary) and 7 (ov072 town map) are **not** X-menu buttons; the 8-entry X table is `FF,0,1,2,3,4,5,FF`. They come from the shared Y-shortcut table. | `ov010.s:72773-72780`; `ov010/event_shortcut_menu.c:963` |

### Missing scenes/apps found by the sweep

These are confirmed screens not covered in the detailed sections:

| Scene | What | Evidence |
|---|---|---|
| Player profile **ov071** | Multi-page profile (TITLE/MENU/SUMMARY/STATS/LIST/INFO), 3D + windows | `ov010/scrcmd_proc.c:1996-2041`; `ov071/monolith_tool.c` |
| PDC **ov098** | Single-mon 3D scene, confirm/choose/declined | `ov010/scrcmd_symbol.c:614-617`; `ov098/pdc_main.c` |
| IR compatibility **ov101-106** | Menu, test, touch/rhythm, result, ranking | `ov101/irc_compatible.c:59-63`; ids 0x66-0x6A |
| Intrude comm view **ov122** | Peer/scan list, menu rows, status | `ov010/event_wifi_proc.c:236`; `ov122/intrude_comm_view.c` |
| C-Gear help **ov124** | Paged help/book | `ov124/cg_help.c:495-496` |
| C-Gear power **ov035** | Confirmation/message on sub BG4-6 | `ov035/c_gear_power_onoff.c:233-241` |
| Union drawing **ov023** | Oekaki canvas, palette, cursors, quit/finish | `ov010/union_scene.c:609`; `ov023/oekaki.c` |
| Union spinner **ov137** | Rounds/roles/rewards, 3D; retail name unverified | `ov010/union_scene.c:613`; `ov137/guru2_main.c` |
| Musical dress-up **ov133** | 3D accessory fitting, poses, outfits | `arm9/musical_dressup_sys.c:92`; `ov133/dup_fitting.c` |
| Mail **ov136** | Read/write/view, BGs/windows/3D | `ov010/event_field_proclink.c:491`; `ov136/mailview.c` |
| Pokedex search **ov210** | Five-page search, dialogs, touch | `ov212/zukan.c:366-376`; `ov210/zknsearch_main.c` |
| Catch registration/nickname **ov207** | Registration + nickname after catch/PDC-return | `ov010/event_battle_return.c:391`; `ov227/event_pdc_return.c:228` |
| Beacon detail **ov216/ov219** | Trainer/beacon detail, list/info, 3D, touchbar | `ov010/beacon_view_event.c:46,87`; `ov216/beacon_detail_gra.c` |
| Symbol picker **ov218** | Frame-list picker for the field symbol-map event | `ov010/symbol_map.c:952`; `ov218/un_select.c` |
| Award ceremonies **ov220-222** | Trial-House, regional-Dex, national-Dex awards | `ov010/fld_btl_inst_tool.c:421`; `ov010/scrcmd_proc.c:2123,2129` |
| Wi-Fi battle match **ov177** (+ ov176 flow) | Match graphic/3D scene | `ov099/wifibattlematch_net.c:1133`; `ov177/wifibattlematch_graphic.c` |
| VS Recorder **ov183-187** | Record list/replay, code input, download, search, upload, buttons/3D | `ov183/br_codein.c`; `ov184/br_download.c`; `ov185/br_pokesearch.c`; `ov186/br_graphic.c`; `ov187/br_main.c` |
| Staff roll **ov181** | Credits, 3D + BG rows + windows, main top | `ov010/event_gameclear.c:247`; `ov181/staff_roll.c` |
| The End **ov182** | Ending 3D/actors/camera/windows | `ov010/event_gameclear.c:274`; `ov182/the_end_graphic.c` |
| Error screen **ov017** | Network/system error on main BG1, sub planes off | `ov017/error_screen.c:120-135,220-230` |
| Shutdown screen (arm9) | Power-off message on main BG1 | `arm9/app_power.c:262-282,308-367` |
| DSi battle overlay **ov140** | DSi-only; no source dir, content unverified | `ov093/btl_dsi_overlay.c:42` |

### Missing battle sub-states

| State | Evidence |
|---|---|
| Rotation menu (mode 5), three candidates + fight | `ov095/btlv_scd_types.h:119-126`; `btlv_scd.c:748-760`; `ov094/btlv_input.c:1314-1338` |
| Counter screen (mode 6) | `ov095/btlv_scd_types.h`; `btlv_scd.c:1126-1130` |
| Extra confirm screen (mode 7), PDC-only | `ov098/pdc_main.c:322`; `ov094/btlv_input.c:1384-1391` |

### Missing composition elements

- **Text/window stack**: `BMP_WIN`, `BGWINFRM`, `PRINTSYS`, `GF_FONT`,
  `WORDSET`; windows may live on any BG id, so one screen can mix main and
  sub windows.
- **Shared widgets**: `TOUCHBAR_*`, `YESNO_MENU_*`, `FRAME_LIST_*`,
  `MSGSEARCH_*` (all ov067).
- **Cursors**: `APP_KEYCURSOR`, `BMP_CURSOR` hand/blink sprites.
- **Fades/wipes**: `FADE_*`, `WIPE_*`, `WIPE_SUB_*`, HBlank window masks;
  per-screen master brightness (main vs sub separately).
- **DOUBLE3DDISP**: 12 sub 64x64 sprites duplicating the 3D frame, used by
  HOF and Demo3D.
- **Sub-OBJ-only layers**: bag, party list, title attract; keep the OBJ,
  drop the black BG.
- **Display-select routing**: bag/party/summary/trainer card/PMS use
  `GX_SetDispSelect`, not a fixed top/bottom assumption.
- **Field HUD**: INFOWIN 16-px strip, WM icon at (240,0), clock/battery.
- **Encounter effects ov073-083**, **weather ov024-028**, **Entralink
  scoreboard ov020**, CTVT window split.

### Priority corrections before implementation

1. Routing: bag, Options, title attract, intro ov204, Wi-Fi 2D map.
2. Identity/table errors: Save kind 4, Pokedex ov210/ov206, field-menu
   order/unlock, title state count 12, ov178 wrapper.
3. Missing large apps: VS Recorder, PDC, profile, IR compatibility, mail,
   dress-up, GURU2, Oekaki.
4. Missing pickers/awards: Pokedex search, catch registration, beacon
   detail, symbol picker, awards, staff roll/end, C-Gear help/power,
   error/shutdown.
5. Battle sub-states: rotation, counter, PDC confirm; ov177 renderer.
6. Shared elements above.

**Unverified IDs with no source dir**: 5, 66, 84-89, 108, 113, 126, 130,
138-169, 189-193, 200-202, 226, 229, 232-237. They cannot be audited from
this tree; that is not evidence they have no screens.

## 1. Boot, title and opening

Screen routing: `POWCNT1` bit 15 set = main engine on TOP; cleared =
main on BOTTOM (`engine/g2d/hwcompose.c:1421`). `BG_SYS_SetLcdSwap(1)`
sets it (`aletheia/src/game/arm9/bg_sys.c:3686-3690`). The engine varies
by scene: field and battle are main-on-top; the title attract is
main-on-bottom; name entry is main-on-bottom.

### 1.1 Power-on / boot init

**Evidence:** `NitroMain` 0x02005124 -> `GFL_USE_InitGameSystems`
0x0200523C (`func_0200523C.c:95-120`); root proc 0x020A0480
(`app_power.c:410-416`).

- No game display state yet; first display write is
  `STARTMENU_ResetDisplayState` (brightness -16) then the logo screen.
- **Unified:** a black canvas frame 0; do not show firmware/DS-menu
  screens.

### 1.2 Company logos

**Evidence:** ov088 proc 0x021BD8E8; `STARTMENU_LogoScreen` states 0-6
(`startmenu.c:3243-3316`). VRAM `d_021BD944`, graphics mode
`d_021BD8F4`, BG1 template `d_021BD904`, BG5 template `d_021BD924`;
brightness +16; `BG_SYS_SetLcdSwap(1)` -> main top.

- Archive `a/0/2/6`: member 0xC char + 0xD palette to BG1 (main/top); same
  char + 0xE palette to BG5 (sub/bottom); member 0xB whole palette to banks
  0/4 (`:3296-3305`).
- Both screens receive a logo layer. No NSCR/map upload is visible in this
  function, so which art lands on BG1 vs BG5 is **unverified**.
- Timing: fade in ~2 f, hold 90 f, A/B/START skip (`:3317-3322`), fade out,
  30 f gap.
- **Unified:** one logo per canvas, composited from both logo layers;
  centre on 256x192; preserve the white fade; drop the second screen.

### 1.3 Copyright screen

**Evidence:** `STARTMENU_LogoScreen` states 7-13 (`startmenu.c:3340-3379`);
archive `a/1/6/4`; member 1 char + 2 palette -> BG1, member 3 char + 4
palette -> BG5; `BG_SYS_PushTile(1/5)`; swap=1.

- Fade in from white or black, hold 90 f, fade out, 30 f; no key skip.
- Which member is the copyright text is **unverified**.
- **Unified:** one full-canvas copyright panel, both BG layers as one
  composite; drop the second screen; optional port skip.

### 1.4 Opening cinematic

**Evidence:** ov179 proc 0x021B68D8; `cdemo_main.c:75-147`,
`cdemo_scene.c:110-216`, `cdemo_wipe.c:42-93`; `BG_SYS_SetLcdSwap(1)`.

- Main top: two double-buffered affine BG2/BG3 layers (0x18000-byte screen
  blocks), palette fades and wipes; main planes 0xC on, others off.
- Bottom: `BG_SYS_SetSubVisiblePlanes(0)` -> **black placeholder**.
- Input: A/B/START skips once active; no touch; no text.
- **Unified:** no recomposition: the main composite is already 256x192.
  Hide the black bottom; optional port SKIP hint; keep the game's wipes.

### 1.5 Title screen / attract

**Evidence:** ov088 proc 0x021BD574 (`startmenu.c:1035-1171`);
`CaptureInit` sets **`BG_SYS_SetLcdSwap(0)`** -> main on **BOTTOM**.

- Layers:
  - BG0 = 3D title scene, model/camera/ICA animation (Black 233 frames,
    loop ~5619) (`:1352-1412`);
  - BG3 = logo art from `a/0/2/6` members 9/10/8 (`:1246-1259`);
  - BG7 = second art layer, members 0/1/2;
  - BG6 = scrolling parallax (`:1282-1291`);
  - BG5 = a 32x2-tile text window at y=152-168, but `LogoUpdate` hides
    BG5 every frame (`:1264,1276-1278,1284`); the visible "PRESS START" is
    not code text;
  - main OBJ sprites (CLACT, arc 0x1A members 5/4/6, actor at 0x80,0x60),
    sub OBJ enabled; which engine the CLACT actor uses is **unverified**.
- Phases:
  - phase 0-1 (first 666 f): sub engine = only a sub-OBJ sprite over
    black (top); bottom has 3D+logo+parallax+sprites.
  - at frame 666: `FADE_Start`, `BG_SYS_SetLcdSwap(1)` (main -> top),
    hides BG6/BG3/sub OBJ; top becomes 3D+BG7+sprites; bottom black.
  - A/B/START in phase 1: swap back, show BG3/BG6/sub OBJ, 3D jumps to
    frame 5301; A/START: result 0, fanfare, 3D frame 5001, fade to title
    menu.
  - no input after 4680 f: result 1 -> fade out; attract loop re-enters
    the logo proc in mode 1 -> copyright -> opening.
  - held keys 0x46 -> save erase; held 0xC80 -> mic test (hidden debug).
- **Unified:** use the **main composite** (3D+BG7/BG3+BG6) as a full
  canvas, independent of the swap bit. If the sub-OBJ sprite is the
  version mascot, place it as a 96x96 accent at lower-right (x~152-248,
  y~88-184), not a second screen. Logo centred in the upper band
  (y~16-88). PRESS START in the game's hidden BG5 slot (y152-168, full
  width, centred), drawn by the port with the game font, pulsing; hit the
  whole strip/canvas. Keep the 666-frame timing but not the swap.
  Use pinned art behind; do not repeat the two-screen flicker.

### 1.6 Title menu

**Evidence:** ov088 proc 0x021BD66C (`startmenu.c:1480-1555,1557-1582,
1727-1747,1674-1725,2006-2076,2114-2248,2262-2315,2322-2408,2428-2572`);
`BG_SYS_SetLcdSwap(1)` -> main top.

- Main top: up to 8 row bars on BG1/BG2 at x24-232 (26 tiles = 208 px),
  heights 24 px (112 px for CONTINUE), first row at y16, packed and
  scrolled; text windows; WFC icon as OBJ at row right; palettes 1 normal,
  2 cyan, 3 flash.
- No-save rows: NEW GAME, MYSTERY GIFT, NINTENDO WFC SETTINGS, MIC TEST
  (+ port SETTINGS). Labels from `a/0/0/2` member 179.
- Bottom: in the boot capture **black**; code enables sub BG0-2 + sub OBJ.
  The CONTINUE/info panel uses `BMP_WIN_Create(4,2,3,30,19)` on sub BG0.
  Whether a save-present menu draws there is **unverified**.
- Input: A/B/up/down; no touch in the row handler.
- Exits: CONTINUE -> BattleFlag proc; NEW GAME -> intro proc 0x021BD818;
  Mystery Gift -> ov121; ov199; ARM9 0x0209DEF0; WFC -> ov016; mic ->
  ov168; settings/relocator -> ov107; B -> title.
- **Unified:** keep native row geometry (x24-232, 24 px pitch, y16) and
  art behind with a 45-55% scrim; shrink the logo to a corner mark while
  the menu is open; hide PRESS START. Replace the 112 px CONTINUE row with
  a 40-48 px save card. WFC icon right-aligned. Bottom prompts become
  centred canvas modals (info 240x152, confirm 240x152, message, yes/no).
  Add touch row rects (x24-232, row top..+24), WFC, modal buttons.
  Scene id: current proc + `work->state` (11 entries).

### 1.7 Title-menu prompts and messages

**Evidence:** `startmenu.c:1754-1973,2637-2777,2805-2810`.

- CONTINUE/C-Gear confirm: info window `BMP_WIN_Create(4,2,3,30,19, pal5)`
  -> sub BG0 (bottom); then message/yes/no.
- NEW GAME confirm: main BG0 30x20 at (2,2), msgs 0x16-0x19, rows slide off;
  yes -> intro, no -> back.
- Info/notice (DSi/TWL only): main BG0 30x20, bank 0xE8.
- Unlock notices: fades main planes 0x1E and sub planes 0x17, prints,
  fades back.
- **Unified:** modals over the canvas; keep game windows/text; centre
  rects; every button gets a hit rect.

### 1.8 Save erase (hidden) and config prompt

- **Save erase 0x021BD974** (`startmenu.c:3391-3704`): one main BG0; no
  new swap, so main stays bottom; 30x4 text at (1,19), two yes/no
  confirmations, then erases save + 9 slots and resets. **Unified:**
  destructive modal or debug-only; full-canvas dim, 240x96 dialog.
- **Config prompt 0x021BD830** (`startmenu.c:3024-3241`): main bottom;
  top sub BG1 question 29x4 at y=152, bottom main BG1 two 20x2 choice
  windows at y=96/128; language/gender/battle style; touch hit boxes
  `d_021BD83C`. Mode 0 reachability **unverified**. **Unified:** full-
  canvas prompt: question top band, two 200x32 buttons at y104/144; touch
  buttons.

### 1.9 NEW GAME intro (ov204) and name entry (ov194)

- **Intro 0x021BD818 -> ov204** (`intro_graphic.c:290-307,373-401,445-460,
  504-520,535-546`; `intro_msg.c:311-334,372-422`): `CreateDisplay(swap=1)`
  -> main top; 3D BG0 + intro BGs/particles/sprites + text window 30x4 at
  y152-184; sub planes cleared, sub OBJ enabled, but no sub actor created
  (bottom black in capture). Text advances with A/B or touch. Phase 7 uses
  a different BG/camera set.
- **Name entry ov194** (`namein_graphic_main.c:252-318,350-369,432-533`;
  `namein_graphic.c:1237-1251,1756-1775,1810-1852`): `CreateDisplay(swap=0)`
  -> main bottom. Bottom: name field 24x2 at tile (8,2) = 192x16 at y16;
  16 glyph actors; keyboard BG1/BG2, grid window 26x15 at (3,5); kana
  13x6, symbols 11x4. Top: prompt 30x4 at y152-184; icon/portrait system
  (engine unverified). Touch grid and icon; A/B.
- **Unified intro:** main composite full canvas; keep text at y152-184;
  drop black bottom; menu list x176-248 as a right panel.
- **Unified name entry:** keyboard composition as canvas; grid x24-232,
  y40-152; name field y16-32; prompt as 2-tile header or toast; icon 48x48
  at x8/y56 if kept; redraw mode/OK buttons; keep touch grid.

### 1.10 Boot/title composition summary

| Scene | Unified recipe | Keep | Hide/drop | Relocate/redraw |
|---|---|---|---|---|
| Logos | BG1+BG5 logo composite, white fade | art, timing | second screen duplicate | centre, crop margins |
| Copyright | BG1+BG5 composite | art, 90 f hold, fade | second screen | centre; optional skip |
| Opening | main BG2/BG3 affine composite | 15 Hz step, wipes, palettes | sub (black) | optional SKIP hint |
| Title attract | main 3D+BG7/BG3+BG6 full canvas | 3D, parallax, fades, timers | sub-OBJ-only screen; BG5 hidden text | mascot 96x96 lower-right; PRESS START at y152-168 |
| Title menu | art (dimmed) + rows + icon + modals | row geometry x24-232, 24 px pitch, palettes, WFC | bottom mirroring; 112 px CONTINUE row | rows y16; save card 40-48 px; touch rects; modal panels |
| Title prompts | modal over dimmed rows | window art/text/yes-no | second-screen placement | info 240x152 at (8,20); button hit rects |
| New game intro | main 3D+BG full canvas + text y152-184 | 3D, BGs, print, touch advance | black bottom | menu list right panel x176-248 |
| Name entry | keyboard main composite + prompt/icon | grid, field, cursor, touch | top split | prompt y0-16; icon 48x48; mode/OK buttons |
| Config prompt | question strip + two buttons | text/windows | two-screen split | question y8-40; answers y104/144; touch |

**Unverified:** logo BG1/BG5 art split; whether the title sub-OBJ is the
mascot; which layer supplies the red/black title-menu backdrop; whether a
save-present title menu draws bottom content; the stock PRESS START
element; config prompt mode 0 caller; intro/name-entry build state.

## 2. Field, C-Gear, start menu and text

One correction that changes the design: **field dialogue, yes/no, the Y
shortcut menu and the place-name banner are TOP-screen elements**, not
bottom-screen. Only the C-Gear/No-Gear panel, the X/start menu and their
HUD are bottom-screen.

### 2.1 Normal field play

**Evidence:** `fieldmap.c:1088-1097`; sub-screen type default 0, mode 5
fallback (`game_data.c:349,1070-1077`, `field_subscreen.c:129-131,427-441`).

- **TOP:** 3D field world; place-name banner when active.
- **BOTTOM:** full 256x192 C-Gear when obtained, otherwise No-Gear static
  panel. During events/menu the C-Gear is dimmed/"off"
  (`c_gear.c:2449-2459,2968-2979,1273-1282`), not black.
- **Input:** field world is pad-only (`field_event_check.c:1012-1040`).
- **Unified:** field full-canvas; drop No-Gear art; C-Gear becomes a 24-px
  bottom bar (clock/battery/Wi-Fi + C-Gear button), hidden during
  text/events. Full C-Gear opens on demand at 1:1.

### 2.2 C-Gear

**Evidence:** modes 0/8; `C_GEAR_Create` 0x021F279C / `C_GEAR_Init`
0x021F251C; sub BG4/5/6; 9x4 hex grid; 20 ripple slots, 11 HUD actors,
3 tile markers, 3 bar actors, 18 splash actors (`c_gear_types.h:105-109`);
cell 32x32 px on a 4-tile stride (`c_gear.c:1204-1209`).

- **TOP:** nothing; 3D field remains.
- **BOTTOM:** full 256x192 interactive grid and HUD; clock/battery y=22,
  four icons y=170 (`c_gear.c:2024-2061`).
- **Touch:** `C_GEAR_HitTestTile` (`:668-694`); keys 1-6 for paint/commands/
  skin; command meanings unverified.
- **Unified:** full C-Gear as an on-demand full-canvas app, 1:1 and
  unscaled (outer columns clip natively). Ambient play shows only the
  compact bar. C-Gear and start menu never coexist (`field_subscreen.c:
  363-373`).

### 2.3 X / start menu

**Evidence:** mode 1, opened by X (`field_event_check.c:773-776,1038-1040`);
`EVENT_FIELD_MENU_Create` 0x0215FC40 / `EVENT_FIELD_MENU_Main` 0x0215FD0C;
`FIELD_SUBSCREEN_InitMenu` (`field_subscreen.c:629-653`).

- **TOP:** 3D field still drawn but dimmed; main BG1 backdrop and main
  brightness -6 (`field_subscreen.c:707-716`). Movement paused.
- **BOTTOM:** full 256x192 two-column menu: BG4 frame + BG5/6/7 layers;
  seven 64x16 text windows + icon actors; slot 6 is Back with no icon;
  slide-in from y=0xC0 (`field_menu.c:235-239,701-753,804-871,1089-1119`).

**Slots and unlock progression.** `gFieldMenuLayouts` 0x021D8D24 (6 rows x
7 ints); `gFieldMenuMsgIds` 0x021D8B34 = `[7,2,1,3,4,5,6,9]`:
`1=Pokedex, 2=Pokemon, 3=Bag, 5=Save, 6=Options`; `4` is the player-name
button; `7/9` blank (`field_menu.c:815,833`).

| Layout | Visible buttons |
|---|---|
| 0 full | Pokemon, Pokedex, Bag, Trainer, Save, Options, Back |
| 1/4/5 special | Pokemon, Pokedex, Bag, Trainer, Options, -, Back |
| 2 mid | Pokemon, Bag, Trainer, Save, Options, -, Back |
| 3 early | Bag, Trainer, Save, Options, -, -, Back |

`FIELD_MENU_GetLayoutType` chooses: Union/zone-96-pair -> 1; zone 34 -> 4;
intrude -> 5; no flag 0x961 -> 3; no flag 0x962 -> 2; else 0
(`field_menu.c:769-785`). So 0x961 unlocks Pokemon, 0x962 unlocks Pokedex;
Bag/Trainer/Save/Options are in the early layout; Save is absent only in
the special layouts.

**Touch rects** (`gFieldMenuTouchTable` 0x021D8C2C, bottom-screen px):
slots 0/1 y24-64 x8-120/136-248; 2/3 y72-112; 4/5 y120-160; Back y168-191
x224-248 (`field_menu.c:1011-1031`).

- **Unified:** freeze field; do **not** show the native two-column menu.
  Draw a right-side translucent panel ~112 px wide, one column, seven
  24-26 px rows: 24-px icon + game label; use game labels/icons; Back
  becomes the chevron. Hide the C-Gear bar. Keep the `FieldMenu` object
  hidden but live so selection/actions/unlock layouts remain authoritative;
  map touch to the new rows. Scroll if seven rows do not fit.

### 2.4 Field text / dialogue

**Evidence:** `FIELD_MSGBG_Create` 0x021901EC; message system each field
frame (`fieldmap.c:1074-1075`); frame BG1/system BG2 main
(`field_msgbg.c:2278-2311`); standard window 30x4 tiles at (1,19) =
240x32 px at (8,152); rolling variant 224x32 at (16,152)
(`field_msgbg.c:772-774,1716-1730`); speech balloons up to 3
(`:1338-1350,1388-1445`).

- **TOP:** message/system frame and text.
- **BOTTOM:** C-Gear faded off; no black state.
- **Input:** no touch; typewriter advance/skip is key-driven.
- **Unified:** keep the game's own window/printer at native coordinates
  (8,152) or (16,152); do not scale font/window; hide the C-Gear bar; field
  stays full-canvas behind it.

### 2.5 Yes/no windows

**Evidence:** `FIELD_MSGBG_CreateYesNoMenu` 0x02190BE4; system frame BG2
main, 56x32 px at (192,80) (`field_msgbg.c:826-835,1033-1047`).

- **TOP:** yes/no box.
- **BOTTOM:** C-Gear faded off.
- **Input:** pad/list only; touch is a port addition; result polarity
  unverified.
- **Unified:** keep at native (192,80); hide C-Gear bar; optional port
  touch rectangles over the two rows.

### 2.6 Y shortcut menu

**Evidence:** Y, only when shortcuts exist and sub-screen idle
(`field_event_check.c:762-770,958-966`); `EVENT_SHORTCUT_MENU_Create`
0x0215FF54; UI `SHORTCUT_MENU_Create` 0x021C4F7C. With one shortcut it
executes immediately.

- **TOP:** BG1 main; BMP window 30x12 tiles = 240x96 px at (8,96); three
  actors at y=0x60; list from recent IDs; text bank 0xAD; main alpha blend
  (`shortcut_menu.c:269-328,459-481`).
- **BOTTOM:** C-Gear faded off.
- **Input:** pad list only; no touch.
- **Unified:** keep native 240x96 overlay at (8,96), dim field; hide bar;
  never show with start panel (game state keeps them exclusive).

### 2.7 Place-name banner

**Evidence:** `FIELD_PLACE_NAME_OnZoneChange` 0x021BC550; BG3 main; window
256x16 at (0,8); text bank 0x59; letters are OAM actors; 10-frame fade,
one letter per 3 frames, 30-frame hold
(`field_place_name.c:127-135,271-295,754-783,855-905,1088-1092`).

- **TOP:** banner.
- **BOTTOM:** C-Gear active ambient, faded during script events.
- **Unified:** keep native (0,8) strip; suppress while start panel/full
  C-Gear is open; hide bar.

### 2.8 Bottom HUD

- **C-Gear HUD:** clock, colon, battery, four icons (`c_gear_types.h:106`,
  `c_gear.c:2024-2061`); fades to "off" during events.
- **Start-menu info window:** BG4 bottom, only when layout != 1
  (`field_subscreen.c:650-651`); 16-px strip: link icon x=9, x=13,
  battery/clock x=19 (`infowin.c:375-389`).
- **Wireless icon:** bottom (240,0) (`net.c:902-907`, `wm_icon.c:392-395`).

**Unified:** one 16-24-px status strip, preferably the INFOWIN layout
(clock + battery + link); use C-Gear HUD only when full C-Gear is open;
suppress duplicates; hide during dialogue/events.

### 2.9 Field composition summary

| Element | Native screen | Unified 256x192 |
|---|---|---|
| 3D field | top, 256x192 | full canvas; freeze when a mode opens |
| C-Gear | bottom, 256x192 | 24-px bottom bar; full-canvas on demand; 1:1 touch |
| No-Gear | bottom, 256x192 | drop; disabled C-Gear chip only |
| X/start menu | bottom, 256x192 | right panel ~112x192; 7 rows; game labels/icons |
| Dialogue | **top** BG1, 240x32 at (8,152) | keep native; hide bar |
| Yes/No | **top** BG2, 56x32 at (192,80) | keep native; hide bar |
| Y shortcut | **top** BG1, 240x96 at (8,96) | keep native; dim field |
| Place banner | **top** BG3, 256x16 at (0,8) | keep native; suppress if panel open |
| Info window | bottom BG4, 16-px strip | one status strip |
| Wi-Fi icon | bottom (240,0) | fold into status strip |

**Unverified:** event-flag names 0x961/0x962; why special layouts suppress
Save; C-Gear key/panel meanings; yes/no result polarity; whether
non-movement field tasks keep running behind the start menu; whether any
native top window accepts touch; the `FIELD_SUBSCREEN_LoadBackdrop`
comment-vs-layer mismatch (comment says lower, code uses main BG1).

## 3. Field menus and apps

### 3.0 Overlay corrections from the code

| Common assumption | Code reality |
|---|---|
| Bag is ov198 | ov198 is the **battle** party list (`b_plist_*`); the **field bag is ov070** (id 0x46); battle bag is ov197. |
| Party list ov090/ov091 | **ov091** is the field party list (`POKELIST_*`); ov090 is the link/trade party list. |
| Summary ov131/ov173 | **ov131** is the field summary (`P_STATUS_*`); ov173 is the move-relearner summary. |
| Pokedex ov208 | The field menu opens **ov212**, a wrapper that pushes ov209 (list), ov208 (detail), ov206 (info) and ov211 (search). |
| Trainer card | **ov111** (`VIEWER_MAIN_*`), edit sub-screen **ov110**. |
| Xtransceiver | **ov172** (`CTVT_*`); ov175 is SAKE/Wi-Fi records, ov114 is DWC networking/crypto. |

### 3.1 X field menu dispatch

`EVENT_FIELD_MENU_GetProcKindOfButton` reads `SCRCMD_NETWORK_sProcKindOfButton`
0x021712AC = `{-1,0,1,2,3,4,5,-1}`. The proc table `d_021712CC` maps:

| kind | overlay | app |
|---|---|---|
| 0 | 0x5B ov091 | party list |
| 1 | 0xD4 ov212 | Pokedex wrapper |
| 2 | 0x46 ov070 | bag |
| 3 | 0x6F ov111 | trainer card |
| 4 | 0 | field save/report event |
| 5 | 0x44 ov068 | options |
| 6 | 0x83 ov131 | summary |
| 7 | 0x48 ov072 | town map |

### 3.2 Party list (ov091)

**Evidence:** `POKELIST_Init/Main` 0x021B6240; `SetupDisplay` clears both
planes, selects sub-main, loads BG0-7 (`pokelist.c:1291-1319`). Six plates
2x3; tile positions `(0,1),(16,2),(0,7),(16,8),(0,13),(16,14)`, each
16x6 tiles (`plist_plate.c:197-233`); cursor and back/switch actors at
(0xE0,0xA8)/(0xC4,0xA8) (`pokelist.c:1385-1412`).

- **TOP/BOTTOM:** both engines populated; plates/windows on main, displayed
  on lower LCD via display select. No black placeholder.
- **Unified:** full-canvas 2x3 plates, normalized rows; back/switch to
  top-right or 24 px footer; context menu as centered 160x64 overlay.

### 3.3 Pokemon summary (ov131)

**Evidence:** `P_STATUS_InitGraphics` 0x021D4240; clears main+sub, selects
sub-main, loads BG1-7, 3D model via G3D/MCSS (`p_status.c:629-660`);
8 button actors at y=168, 2 type icons, font 23, bank 158
(`:799-839`); touch table `_TouchHitRects` (`:861`).

- **TOP/BOTTOM:** both populated; no placeholder.
- **Unified:** full-canvas; model upper 2/3, 8-button strip bottom 24 px,
  type icons beside it.

### 3.4 Move-relearner summary (ov173)

**Evidence:** `POKE_SUMMARY_MAIN_InitScreen` 0x021B6100; BG2-7, LCD swap 1
(`poke_summary_main.c:443,466-473`); 16 BMP windows.

- **TOP/BOTTOM:** two-engine composition; no explicit black placeholder.
- **Unified:** separate full-canvas page, or 256x120 overlay over the
  summary page; keep separate from ov131.

### 3.5 Bag (ov070)

**Evidence:** `ITEMMENU_*` 0x021B6100; overlay 0x46; display select sub-main
(`itemmenu.c:2717-2767`); BG0-6, both plane sets visible (`:2877-2878`).
Windows (`:3073-3081`): title layer3 (0x12,9) 12x3; rows (0x11,0xD)/(0x11,0xF)
11x2; item name layer6 (0,0x13) 32x5; count (7,5) 18x3; price (0x14,8)
6x3; description (2,0xC) 29x7. Five pockets; right-edge side buttons x
{0,0x78,0xAD,0xC0,0xE0}.

- **TOP/BOTTOM:** full-screen bag on sub engine; other engine not written
  (unverified whether black or field remains).
- **Unified:** full-canvas; keep 4 windows; pocket tabs as 24 px right
  rail; crop item-name window to 28 tiles if bordered.

### 3.6 Pokedex list (ov209 via ov212)

**Evidence:** ov212 `ZUKAN_StartProc209` (`zukan.c:321`); `ZUKAN_LIST_*`
0x021BC240; BG0-7, main/sub planes 0xF (`zukan_list.c:857-869`); sub
windows (`:957-961`); window defs `gZukanListWindowDefs` 0x021BE990
(header 17x3 at (1,0), seen 13x3 at (2,3), caught 15x3 at (0x10,3));
touchbar labels y=4.

- **TOP/BOTTOM:** both populated; lower is windowed list/touch region.
- **Unified:** full-canvas list + 24-32 px bottom touchbar; keep 3 header
  windows; national toggle top-right.

### 3.7 Pokedex detail (ov208) + info (ov206)

**Evidence:** detail init 0x021BCD68; clears both, 8 BG layers, 3D model,
head bar layer5 (0,0) 16x1 (`zukan_detail_headbar.c:253`); info/voice
windows (`zukan_detail_voice.c:902,1082`); push from ov212
(`zukan.c:428`). No black placeholder.

- **Unified:** full-canvas; model upper ~144 px, 48 px info/touch strip;
  keep head bar 16 px; crop voice waveform to 30x6 if needed.

### 3.8 Trainer card (ov111) / edit (ov110)

**Evidence:** `VIEWER_MAIN_Create` 0x021C3CC0; BG2/3 main, BG4-7 sub
(`viewer_main.c:839-879`); 3D card `PMS_DRAW_Create`; name at (0x88,8)
(`viewer_text.c:358`); 8 stat/signature windows; signature pad
BG6/7 (`viewer_main.c:701-703`). Edit via ov110.

- **TOP/BOTTOM:** both populated; card top, stats/signature bottom.
- **Unified:** full-canvas card top 112 px, rows/signature bottom 80 px;
  keep 4 stat rows + signature pad >=200x48; page arrows in header.

### 3.9 Save/report (ov034 + ov010 report event)

**Evidence:** save button kind 5 -> `EVENT_FIELD_PROCLINK_StartConfigEvent`
0x02161350 -> `REPORT_EVENT_Main` 0x02169AB8; `REPORT_Create` 0x021EFEC0
initializes only sub BG4/5/6 (`report.c:382-412`); 8 record lines + party
icons; text bank 0xA8 (`:458-459`).

- **TOP:** field remains.
- **BOTTOM:** report/save panel; no black placeholder.
- **Unified:** this is genuinely bottom-only: render as a centered
  240x160 panel over a dimmed field; keep 8 lines + party icons; drop the
  separate background.

### 3.10 PC / boxes (ov170)

**Evidence:** `BOX2_MAIN_*` 0x021DC860; `InitBgSys` loads BG0-6, main
planes 0-3 visible, sub plane 7 visible (`box2_main.c:1130-1165`). Box
grid 24 slots (`box2_ui.c:449`); party icons from window 7, step
(0x20,0x18) (`:349-360`); 6 party rows at y=i*0x22 (`:396-410`); close at
(0x4b*4,0x22).

- **TOP:** box grid; **BOTTOM:** party/info.
- **Unified:** full-canvas top 2/3 box grid, bottom 1/3 party icons + one
  24 px name strip; drop duplicate party rows.

### 3.11 Options (ov068)

**Evidence:** `OPTION_MENU_Init` 0x021B95A0; six 24-px bars from tile row 2;
help window 30x4 at y=19 (`option_menu.c:617`); title 16x3; message 30x4;
label windows (`:662-693`); message bank 29.

- **TOP:** option bars/values/cursor; **BOTTOM:** help text.
- **Unified:** full-canvas: six rows 144 px + 32 px help + 16 px header;
  value columns 112/160/208 (3-choice), 112/184 (2-choice); use the 6th
  grey bar as page selector.

### 3.12 Xtransceiver (ov172)

**Evidence:** `COMM_TVT_SYS_*` 0x021BDFDC; camera fills BG3 main; window
split via WIN0H/WIN1H (`ctvt_camera.c:228-235`); 1-4 participant layouts
(`:273`); talk/menu BMP windows (`ctvt_talk.c`, `ctvt_draw.c`); no-signal
asset; touch talk/mic/confirm/slider.

- **TOP/BOTTOM:** camera full-screen main; names/talk/menu over it. No
  placeholder.
- **Unified:** full-canvas camera grid, participant names at tile top;
  talk/mic controls 32 px bottom bar.

### 3.13 Mystery Gift (ov121), WFC (ov016), Town Map (ov072), shortcuts

- **Mystery Gift ov121:** full-screen card/scene + menu/detail windows;
  exact layer split unverified. Unified: 224x160 card panel + 24 px menu
  footer.
- **WFC settings ov016:** full-screen DWC scene/wizard; exact engine split
  unverified. Unified: 240x176 modal with 2 buttons if needed.
- **Town Map ov072:** map full-screen, both engines; top 168 px map/scroll,
  bottom touch/button area; place-name 24x3 at top. Unified: full-canvas
  map + 24 px bottom touchbar.
- **Shortcut list (ov021 `shortcut_menu.c`):** bottom list over field;
  reuse the X-menu panel (5-6 rows at 24 px, 16 px header).
- **Field subscreen modes (C-Gear, No-Gear, Union, Intrude, Beacon,
  Dowsing, Report, Power):** all bottom-only; one panel slot; one 16 px
  header strip. C-Gear is the only one kept as an on-demand full canvas.

### 3.14 Field menu/app summary

| Screen | Canvas | Keep | Move/crop | Redraw |
|---|---|---|---|---|
| X field menu | right panel over field | 2x3 apps, labels, icons | Back to chevron; drop scroll | one panel BG, 8x2 cells |
| Party list | full | 2x3 plates, 6 labels, context menu | back/switch top-right; normalize stagger | 224x24 message |
| Bag | full | list/name/count/price/desc | tabs to 24 px rail | borders/palette |
| Options | full | 6 rows + values + help | none | page bar |
| Summary ov131 | full | model + 8 buttons + 2 type icons | button row 24 px | spacing |
| Move summary ov173 | full/overlay | move list/detail | separate page | 16 windows -> 4-6 panels |
| Pokedex list | full + 32 px bar | list + 3 headers | counts top-right | touchbar |
| Pokedex detail | full | model + head bar | touch bar 48 px | head bar 16 px |
| Trainer card | full | card + 4 rows + signature | arrows to header | signature >=200x48 |
| Save report | 240x160 panel | 8 lines + party icons | drop background | centered panel |
| PC boxes | full | 6x4 grid + 6 party icons | merge duplicate party rows | 24 px name strip |
| Xtransceiver | full | camera grid + names | controls 32 px bar | participant tiles |
| Town map | full | map + place name | touchbar 24 px | scroll area |
| Union/Intrude/C-Gear | bottom panel | one header + rows | share panel | one header strip |

**Unverified:** exact touch rectangles stored in overlay data; whether the
bag's other engine is black or field; exact Mystery Gift/WFC layer splits;
`RELOCATOR` label vs `mb_*` code naming.

## 4. Battle

### B1. Battle scene / view

**Evidence:** `ov093` `BTLV_CORE_InitDisplay` 0x021E8DC4
(`btlv_core.c:541-560`); `ov094` stage 0x021F6560, field 0x021F6B04,
camera 0x021F6E3C, MCSS 0x021FEAC0, gauges 0x02206BC8; `ov093`
`btlv_scu.c` 0x021EAF00.

- **TOP:** 3D stage/field, Pokemon sprites, camera, HP/EXP gauges, name
  plates, status icons, message window (BG1 30x4 at y=152), name plate
  (BG2/3), main OBJ.
- **BOTTOM:** with no menu open, only the wait/backdrop plane; this is the
  command surface, not a second scene.
- **Input:** keys plus `BTLV_INPUT_ProcessTouchInput` and
  `TOUCHPANEL_HitTrg`.
- **Unified:** keep the battle scene full-canvas. Redraw HP plates as two
  compact plates at the top corners; keep a 240x32 message slot at
  y~152; the command controls occupy the bottom 40-56 px as a translucent
  overlay. Cover the lower scene rather than scaling it.

### B2. Action menu (Fight / Bag / Pokemon / Run)

**Evidence:** mode 1 `BTLV_INPUT_ChangeScreen` (`btlv_input.c:1229-1268`);
`CreateActionButtons`, `DrawRunButtonText`, `CreateStatusIcons`; hit tables
`gBtlvScdActionRegionsA/B` and `gBtlvScdActionButtonByRegion` in ov095
(0x0689D828/0x0689D8D4/0x0689DF64); `BTLV_SCD_StartActionMenu`.

- **TOP:** battle scene and gauges unchanged.
- **BOTTOM:** four action buttons, run text, status icons, pointing-hand
  cursor; BG4/5 on, BG7 off.
- **Unified:** redraw as a 2x2 pad or one row of four 56x24 buttons in the
  bottom 56 px. Fight primary; Bag/Pokemon/Run secondary.

### B3. Move menu

**Evidence:** mode 2 (`btlv_input.c:1269-1297`); `DrawWazaButtons` 0x...;
`CreateWazaCursorActors`; `IsWazaSelectable`; `UpdateWazaRow`;
`BTLV_SCD_StartMoveMenu`.

- **TOP:** battle scene.
- **BOTTOM:** up to four move rows, type icon, name, PP, cursor; disabled
  rows dimmed.
- **Unified:** 2x2 grid of 112x34 move cards in the bottom 80 px; PP
  right-aligned; selected move info as one 240x28 strip above the grid.

### B4. Target menu

**Evidence:** mode 3 (`btlv_input.c:1298-1305`); `DrawTargetButtons`;
region tables `gBtlvScdTargetRegionsSingleA/B`, `DoubleA/B`,
`gBtlvScdViewPosByRegion` (0x0689E560-0x0689E704).

- **TOP:** scene.
- **BOTTOM:** target buttons with name/state, cursor.
- **Unified:** draw target chevrons on the scene over each battler; a
  compact 2-3 item target list in the bottom 40 px.

### B5. Battle text / messages / name plates

**Evidence:** `BTL_MSG_*` 0x021EEC78; `BTLV_SCU` message/name windows
0x021EAF00; bottom prompt mode 4 (`btlv_input.c:1306-1313`);
`DrawMessageText`.

- **TOP:** primary 30x4 message box at y=152, name plate.
- **BOTTOM:** short prompt/confirm text, two lines.
- **Unified:** one text slot at the bottom of the canvas; route both top
  battle text and bottom prompts there; suppress the duplicate bottom
  prompt. Name plates stay with the HP plates.

### B6. Battle bag

**Evidence:** `ov197` `B_BAG_MAIN_*` 0x0220B120 (`b_bag_main.c`);
`InitBg` initialises sub BG4-7 and sub planes 15 + OBJ
(`:1902-1914`); exit disables sub planes (`:1916-1923`).

- **TOP:** untouched battle scene.
- **BOTTOM:** full bag UI: header, pocket tabs, item grid/list,
  description, cursor, item actors; modes list/selected/count/message.
- **Unified:** full-canvas panel: 56 px category column, 3xN item grid,
  40 px description strip. Keep a 24-32 px scene strip only if the header
  allows it; otherwise drop the scene.

### B7. Party switch / battle party list

**Evidence:** `ov198` `B_PLIST_MAIN_*` 0x0220F060 (`b_plist_main.c`);
`InitBg` sub BG4-7 + sub planes/OBJ (`:1337-1356`). `ov223` `PSEL_*`
0x021B95A0 is the 3D party select; `PSEL_GRAPHIC_Create` calls
`BG_SYS_SetLcdSwap` (`psel_graphic.c:1097-1116`).

- **TOP (B_PLIST):** battle scene unchanged.
- **BOTTOM (B_PLIST):** six-row party list with icon, name, level, HP/EXP,
  status, held item; summary/move pages layered in the same area.
- **PSEL:** 3D models on one engine; list/selector on the other.
- **Unified:** one party screen: six 240x26 rows in the upper 156 px, a
  96x96 selected-mon preview or model pane, command row at the bottom.
  Use B_PLIST as the base; PSEL's 3D stage is optional.

### B8. Battle animations and dual-screen effects

**Evidence:** `ov094` effect VM 0x021F93F4 (`btlv_effvm.c`), `BTLV_EFFECT_*`
0x021F73F0, MCSS 0x021FEAC0, visibility mode
(`btlv_effect.c:1588-1598`). `DOUBLE3DDISP` 0x0204E8C0 is used by HOF/demo,
not battle.

- **TOP:** all battle animation, particles, tints, camera.
- **BOTTOM:** no animation scene; menu layer may be hidden during effects.
- **Unified:** keep effects on the scene layer; clamp particles to
  256x192; never letterbox the scene for controls.

### B9. Battle entry / exit / return

**Evidence:** `BTLV_SCU_StartEntryScene`, trainer intro, send-out/withdraw,
fades (`btlv_scu.c` 0x021EAF00); `ov092` `EVENT_BATTLE_RETURN_*`
0x021B95A0; `BTLV_CORE_ExitDisplay` (`btlv_core.c:518-525`).

- **TOP:** entry/3D/intro/send-out/message.
- **BOTTOM:** input module exists only after the scene is ready; before
  that blank/placeholder (unverified whether black).
- **Unified:** entry is a scene-level transition; do not show an empty
  bottom panel. Fade the canvas, then bring in the command overlay.

### B10. Link / multi-battle side screens

- `ov097` vs_multi_list 0x021F6560, `SetLcdSwap(1)`: VS presentation top,
  list/confirm bottom (split unverified).
- `ov199` Wi-Fi battle match 0x02212560 (net, no obvious rendering).
- `ov213` comm battle demo, `ov214` battle record select: 3D + BG,
  `SetLcdSwap`, touch (split unverified).
- `ov100` IR battle match inherits the battle/IR screens.

**Unified:** battle recipe plus a modal list/confirm layer; no new layout
per mode.

## 5. Special and other screens

### S1. Trade (ov119)

**Evidence:** `POKETRADE_*` 0x021E3D04 (`pokemontrade_2d.c`), proc
0x021D8980, 3D 0x021E2D70. Main BG1-3 + 3D; sub BG4-7; 26 BMP windows at
`gSubScreenWindowPos` on layer 6 (`:1609-1632`); touchbar.

- **TOP:** 3D trade scene, animation, summary background.
- **BOTTOM:** box grid, party/box lists, offer slots, 26 info windows,
  touchbar.
- **Unified:** full-canvas trade scene on top 112 px; bottom 80 px as a
  single-row box carousel with a 2x4 icon grid and touchbar actions. Drop
  the 26 separate windows; redraw one info panel.

### S2. Trade demo / link trade (ov117/ov118) and GTS negotiation (ov120)

**Evidence:** ov117/ov118 `POKETRADE_IRCDEMO_*`/`POKETRADE_DEMO_*`
0x021D4664; ov120 `GTSNEGO_DISP_*` 0x021D4240.

- **TOP/BOTTOM:** same split as trade (3D scene / lists + windows); exact
  ov117/118 cutscene unverified.
- **Unified:** reuse S1; selection modal for GTS, scene as a 112 px
  backdrop.

### S3. Mystery Gift (ov121)

**Evidence:** `MYSTERY_GRAPHIC_*` 0x021D9624 (`mystery_graphic.c`),
`SetLcdSwap(arg)` (`:255`).

- **TOP/BOTTOM:** 3D gift scene on the engine selected by `arg`; menus on
  the other. Exact split unverified.
- **Unified:** one scene panel + 3-row menu at the bottom; keep the gift
  animation as a 128x112 window if the list needs room.

### S4. Wi-Fi Club (ov099) and Wi-Fi 2D map (ov127-ov129)

**Evidence:** `WIFICLUB_*` 0x021BA81C (`wifi_club.c`): main BG0-3 and sub
BG4-7 (`:3719-3748`), main mask 0xF + sub mask 0xD (`:3944-3954`),
`SetLcdSwap(1)` (`:3666`). Map: ov127/128/129.

- **TOP:** club room/2D map with trainers.
- **BOTTOM:** menus (invite, rules, party, leave, voice chat), lists,
  touchbar; map edit/menu overlays.
- **Unified:** map/club full-canvas; compact 3-button touchbar at the
  bottom; menus as a right panel or bottom sheet; keyboard as a modal
  sheet.

### S5. Wi-Fi login (ov115)

**Evidence:** `WIFILOGIN_DISP_*` 0x021D42A8, `SetLcdSwap(1)` (`:752`).

- **TOP:** profile card/connection animation; **BOTTOM:** card slots,
  menus, messages (exact split unverified).
- **Unified:** full-canvas connection screen; profile card top, four slots
  bottom; scanline only over the card.

### S6. Game Sync / Dream World account (ov123)

**Evidence:** `GSYNC_DISP_*` 0x021D5D94 (main BG0/1/3 + OBJ, sub BG4-7);
`GSYNC_MENU_*` 0x021D4240 (sub BG4-7, button windows, message BG5);
`SetLcdSwap(1)`.

- **TOP:** sync animation, Pokemon icon, progress/particles.
- **BOTTOM:** buttons, confirmation/message, touch.
- **Unified:** animation/progress on top 120 px, one action button +
  message below. Dream World itself is server-side; no scene to invent.

### S7. World Trade (ov135)

**Evidence:** `WORLDTRADE_TITLE_*` 0x021FFDA8; main/sub plane setup,
menu hit test, touchbar.

- **TOP/BOTTOM:** trade title/list scene / menu/detail/touchbar (exact
  split unverified).
- **Unified:** 96 px header scene, 96 px list, 40 px action bar.

### S8. Download Play (ov107)

**Evidence:** `MB_COMM_SYS_*` 0x021EE740; `MbParentUi.textOnSubScreen`
(`mb_comm_sys.h:46`); `POWCNT1 |= 0x8000` (`mb_parent_sys.c:929`).

- **TOP/BOTTOM:** download-play title/progress and text plane; the text
  plane can move between screens (`textOnSubScreen`).
- **Unified:** title/progress top, child list/status below; ignore the
  movable text plane and use a fixed slot.

### S9. Battle Subway (ov069, ov224, ov116)

**Evidence:** ov069 `BSUBWAY_RECORD_LIST_*`; ov224 `SUBWAY_MAP_GRAPHIC_*`
(`SetLcdSwap(swap)` at `:394`); ov116 Wi-Fi subway.

- **TOP/BOTTOM:** 3D subway map / record list; messages, yes/no, save.
  Exact split unverified.
- **Unified:** map 160x112 + 6-row record list; one message/yes-no strip.

### S10. Musical (ov112 + ov132)

**Evidence:** `MUSICAL_MCSS_*` 0x021F55F8, `STA_ACTING_*`
0x021F8480; `SetupBg` enables BG0 + G3D, both OBJ; audience/BG scroll.

- **TOP:** 3D stage, Pokemon, audience, effects.
- **BOTTOM:** performance buttons, audience reaction, messages.
- **Unified:** stage full-canvas; four large performance buttons (2x2) in
  the bottom 72 px; score/message 240x24 strip; audience reaction as a
  top-corner overlay.

### S11. Entralink (ov018 + ov021 mode 3)

**Evidence:** `FIELD_SUBSCREEN` mode 3 comment
(`field_subscreen.c:591`); `INTRUDE_SUBDISP_*` 0x0217C758; touch rects,
town/player actors, info windows.

- **TOP:** field.
- **BOTTOM:** Entralink panel: 8 town icons, palace/player/mark icons,
  title/info, touch rectangles.
- **Unified:** field as a 96 px strip or backdrop; Entralink panel as the
  main canvas: 4x2 town grid + info + action button.

### S12. Union Room panel and C-Gear (ov031, ov029/ov030)

**Evidence:** `UNION_SUBDISP_*` 0x021EFEC0 (five appeal buttons, chat
window, three log lines, scroll knob); C-Gear ov029/ov030 via
`FIELD_SUBSCREEN` modes 0/5/8.

- **TOP:** field.
- **BOTTOM:** Union Room appeal/chat panel or C-Gear panel.
- **Unified:** C-Gear becomes a compact bottom bar; Union Room becomes a
  full-canvas chat panel with a 5-button appeal row.

### S13. Easy-chat / word input (ov110)

**Evidence:** `PMS_INPUT_*` 0x021BD484, `PMSIV_CATEGORY_*` 0x021BB290;
LCD swap in `pms_input_view.c:523-531`.

- **TOP:** sentence/word preview and message; **BOTTOM:** 12 category
  buttons, kana grid, word list, confirm/cancel.
- **Unified:** full-canvas picker: categories left 64 px, word grid centre,
  preview/sentence bottom 40 px; keyboard as a modal sheet.

### S14. Name entry (ov194)

**Evidence:** `NAMEIN_GRAPHIC_*` 0x021F2F8C; `CreateDisplay(0, ...)` puts
main on bottom (`namein_graphic_main.c:274`); kana grid BMP on layer 3,
message box layer 4.

- **TOP:** prompt/preview/icon; **BOTTOM:** kana/symbol grid, cursor, name
  field.
- **Unified:** name field 240x32 at top, 13x6 kana grid below (cell
  ~16x18), mode tabs on the right; prompt as one status line. Fits
  256x192 without scaling.

### S15. Intro / new game (ov204)

**Evidence:** `INTRO_GRAPHIC_*` 0x021BDA40, `SetLcdSwap(swap)` (`:383`);
`INTRO_MSG_*` text/menu windows (`:326`, `:330`).

- **TOP/BOTTOM:** 3D/BG cutscene on main; second BG/OBJ/text/menu on the
  other; authored across both. Exact per-shot split unverified.
- **Unified:** each intro shot is a full-canvas scene; menu/text overlay
  uses the bottom 64 px; for shots that use both, crop the less important
  screen or crossfade on the script's timing.

### S16. Hall of Fame (ov180) + viewer (ov171) + double-3D

**Evidence:** `HALL_OF_FAME_*` 0x021B6254; `DOUBLE3DDISP` 0x0204E8C0
duplicates the 3D frame with 12 sub 64x64 sprites; `SetLcdSwap(0)`
(`:873`); viewer `DENDOU_VIEW_*` 0x021DC860.

- **TOP/BOTTOM:** both show the HOF 3D scene; one live, one sprite copy.
  Viewer: ring/list and member info on opposite screens.
- **Unified:** one HOF scene full-canvas; overlay names as a 240x32 strip.
  Viewer: ring main canvas, member info side panel.

### S17. Demo 3D (ov203)

**Evidence:** `DEMO3D_GRAPHIC_*` 0x021BDDD8; `DOUBLE3DDISP_Init/Exit`;
`SetLcdSwap(swap)` (`:262`).

- **TOP/BOTTOM:** both show the 3D demo via the double-3D copy.
- **Unified:** one 3D viewport full-canvas; drop the copy.

### S18. Evolution demo (ov195)

**Evidence:** `SHINKA_DEMO_GRAPHIC_*` 0x022058F8; main/sub BG, 3D, CLACT;
`SetLcdSwap`.

- **TOP:** 3D evolution scene; **BOTTOM:** sub BG backdrop/effect, no
  controls.
- **Unified:** full-canvas evolution scene; sub BG as border/backdrop or
  dropped.

### S19. Egg demo (ov215)

**Evidence:** `EGG_DEMO_GRAPHIC_*` 0x021F63CC; `SetLcdSwap(mode)`;
main/sub planes; menu in `egg_demo_menu.c`.

- **TOP/BOTTOM:** 3D egg/habitat on one engine, BG/effect on the other
  (split unverified).
- **Unified:** one egg scene full-canvas with a small bottom menu strip.

### S20. Save screen (ov205)

**Evidence:** `SAVE_SCREEN_*` 0x021B95A0; `SetLcdSwap(1)` (`:368`); only
main BG0 + main OBJ (`:392`, `:469`, `:503`); no sub BG/BMP.

- **TOP:** save background, "Saving..." text window, party icons.
- **BOTTOM:** explicit placeholder/black: never initialised.
- **Unified:** full-canvas save screen: background + centred text + party
  row; no bottom strip.

### S21. Summary / status (ov173, ov131, ov178)

**Evidence:** `POKE_SUMMARY_MAIN_*` 0x021B6100; `P_STATUS_*` 0x021D4240;
`PARTY_STATUS_*` 0x021E5440; all init sub BG/BMP layers.

- **TOP:** Pokemon model/portrait + name/level/types.
- **BOTTOM:** stat/move/ribbon/condition pages, move list/detail, buttons.
- **Unified:** full-canvas summary: model/portrait 96x96 left, stats/moves
  160 px right, page tabs top/bottom.

### S22. Pokedex (ov206-ov212)

**Evidence:** `ZUKAN_DETAIL_*` 0x021BCD68/0x021BCA68; `ZUKAN_LIST_*`
0x021BC240; `ZKNSEARCH_*` 0x021BC3F0; `zukan.c` 0x021C8E80.

- **TOP:** 3D/2D model or list page.
- **BOTTOM:** list rows, search filters, info/description, touchbar.
- **Unified:** full-canvas dex: model 96x96 left, list/search 152 px
  right, description 32 px bottom; list and detail share the model pane.

### S23. Research / survey (ov217)

**Evidence:** `RESEARCH_TOP_*`/`RESEARCH_GRAPH_*` 0x021BD5F4; BG6/7,
BG2/3, OAM, touch rects.

- **TOP/BOTTOM:** list or graph / detail/labels/touch; exact split
  unverified.
- **Unified:** graph full-canvas with a 4-row legend/detail panel on the
  right.

### S24. Manual (ov225)

**Evidence:** `MANUAL_*` 0x021B95A0; dialog/window system, touchbar,
`SetLcdSwap(swap)` (`manual_graphic.c:372`).

- **TOP:** manual page/illustration; **BOTTOM:** contents/list/explain/
  touchbar.
- **Unified:** reader: 160 px page image, 88 px text column, page arrows;
  24 px touchbar strip.

### S25. CTVT / photo, local TV, mic test (ov172, ov174, ov168)

**Evidence:** `CTVT_DRAW_*` 0x021BA280 (`ctvt_draw.c`), `messageActive`
(`ctvt_types.h:83`); `LOCAL_TVT_*`; `MICTEST_*` 0x02183B60.

- **CTVT:** TOP camera/photo canvas; BOTTOM drawing tools + message.
- **Unified:** main content full-canvas, tools in a 48-56 px bar; message
  one 240x32 slot.

### S26. Other both-screen overlays

- `ov021` field sub-screen manager 0x021A0CF4: lower panel for C-Gear,
  menu, Union, Entralink, beacon, dowsing, report, power. Top field.
- `ov072` town map: top map, bottom town description.
- `ov088` start menu: title/field with menu; multiple `SetLcdSwap`
  (`:1050`, `:1068`, `:1081`, `:1214`, `:1227`, `:1567`); the mock's
  right-side panel replaces it.
- `ov170` PC box, `ov171` HOF viewer, `ov069` subway records, `ov034`
  report, `ov068` options: top content + bottom controls; full-canvas app
  with bottom action bar.
- `ov109` idle screen: unused reference for a two-screen scene, not a
  shipped screen.

## 6. Cross-cutting rules from the battle/special inventory

1. Classify by engine and process state, not by picture: `POWCNT1` bit 15,
   `BTLV_INPUT_GetScreen`, `BTLV_SCD_*` states, `FIELD_SUBSCREEN_GetMode`.
2. Battle hit tables are the source of truth for actions, but their
   rectangles are the old 256x192 bottom layout; map, do not copy.
3. One text slot per scene; suppress duplicate bottom prompt windows.
4. Keep the game's fade/wipe timing; crossfade layers only.
5. Save (ov205) and pre-input battle are true placeholders; most other
   bottom screens are real UI and must be re-laid out, not hidden.

**Unverified in this section:** exact per-shot TOP/BOTTOM split for ov121,
ov115, ov135, ov224, ov195, ov215; retail names for ov137 ("GURU2") and
ov217 ("research"); whether ov097/ov213/ov214 use both screens
simultaneously; whether ov117/118 add a both-screen cutscene; exact pixel
rectangles of all touch tables (overlay data, not matched C).
