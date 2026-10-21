# Limits survey: species, assets (sprites, icons, Pokedex graphics, cries)

Tags: [V] verified in code or data, [I] inferred, not verified.
Paths: aletheia sources are under `src/game/` (aletheia-w20 tree). Data is the extracted NitroFS tree (read only).
Archive id N is `a/` plus the three decimal digits of N (id 4 is `a/0/0/4`, 0xA7 = 167 is `a/1/6/7`) [V: `docs/ASSETS.md` "The archive-id table"; matches every caller below].
Sibling reports: `species-data.md` (personal data, ids, save), `sound.md` (SDAT, cry buffers). This report does not repeat their tables; it cites them where they meet.

## 1. Summary

Original count: species 1..649, id 650 = egg, 651 = bad egg (also the "nothing cached" marker, `arm9/poke_tool.c:388`). Alternate forms come after, in a separate block.

Every species image comes from two archives and one ARM9 table. The arithmetic is simple and shared, so it is easy to replace. The hard part is that the id space is already full.

- **Battle, summary, Pokedex, party-trade, Hall of Fame, evolution, Pokemon-in-field sprites:** archive 4 (`a/0/0/4`, 14285 members), 20 members per "graphic slot" (section 2a).
- **Party, box, Pokedex-form, trade, Wi-Fi, battle-input icons:** archive 7 (`a/0/0/7`, 1431 members), 2 members per slot, plus a 712-byte palette-selector table in ARM9 data (section 2b).
- **Pokedex list mini icons:** archive 167 (`a/1/6/7`, 655 members), member = species + 5 (section 2c).
- **Cries:** SDAT wave list slot = species, plus a volume record in archive 194 (section 2d, detail in `sound.md`).
- **Footprints:** none. The game has no footprint code or archive [V: no match for footprint in `src/game`; only `fldeff_footmark.c`, footstep marks]. Overworld followers: none [I: `fldeff_namipoke.c` is the surf wave effect, no species key].

How hard: medium. The sprite and icon arithmetic can be left unchanged if mod species use slot numbers n >= 715. The port must still fix four things.

Biggest obstacles:

1. **The slot number space is shared and already used up to 714.** Slot n = species id for species 0..649; egg at 650 and 651; alternate forms at 652..711 (60 form slots); the Substitute doll and the palette-only forms sit at the end of archive 4 and cover the rest of slot 712 to 714. A mod species with id 652..714 would read form, egg or doll members (section 2a, 3). The first slot whose members all lie past the end of archive 4 is 715 (member 14300). For archive 7 the first free slot is 712 (member 1431).
2. **The icon palette selector is a fixed 712-byte table in ARM9 data** (`_IconPaletteTable`, 0x0209E474). It is indexed by slot. A slot >= 712 reads the bytes that follow it (a different table) with no bounds check (section 2b).
3. **Several special cases are keyed to species id literals:** the Manaphy egg (490), Spinda spots (0x147, and NCEC member 0x1994), the Pokedex generation split (0x1ED), two Pokedex form cells (487/1, 492/1), the Shaymin Sky cry (492/1 gives wave 650). New species with their own egg sprite or cry need table lookups instead.
4. **Cries need SDAT wave slots.** WAVARC slots 1..650 are cries, 651 and up are sound effects. A cry for a new species needs a new SDAT wave entry or a port-side cry map (`sound.md`).
5. **Personal data and the id-to-member rule collide earlier than the sprites do** (`species-data.md`, obstacle 1). A species remap is needed anyway; the graphics code can use the same remap result as its slot number.

## 2. Site table

Class codes: ID (id check), TABLE (array or table size), WIDTH (id storage width), ARC (asset index arithmetic), OTHER.

### 2a. Battle and large sprites (archive 4, `a/0/0/4`)

`POKEGRA_GetArcID` returns 4 [V: `arm9/pokegra.c:73`]. All callers go through `POKEGRA_Get*ArcIndex` (which call `POKEGRA_CalcArcIndex`) or through `MCSS_TOOL_MakeAddWorkFromParam`.

| file:line | function | literal / size | guards | class | change |
|---|---|---|---|---|---|
| arm9/pokegra.c:451 | POKEGRA_CalcArcIndex | `index = monsNo * 0x14` | base member of the species block (20 members per slot) | ARC | replace with `slot_base(monsNo)` lookup, default `monsNo * 20` for 0..649. Species 650..714 collide (section 3). [V] |
| arm9/pokegra.c:453-471 | same | dir 0 gives +0, dir 1 gives +9; sex offset 0/9 (cgr) or 2/11 (cbr) | front/back and the member probed for a female image | ARC | none, if each block keeps the 20-member layout (section 3). [V] |
| arm9/pokegra.c:473-481 | same | egg: `(monsNo == 0x1ea ? 1 : 0) + 0x28a`, times 0x14 | egg graphic: block 650 for all, block 651 for species 490 (Manaphy) | ARC / ID | add a per-species egg-slot table (mod species may have their own egg sprite). 650 and 651 stay reserved. [V] |
| arm9/pokegra.c:482-496 | same | form: `(formStart + formNo - 1) * 0x14 + 0x32f0`, 0x32f0 = 13040 = slot 652 | alternate-form block. `formStart` = personal byte 0x1F (u16 `formSpriteIndex`), count = byte 0x20, shared flag = 0x23 | ARC | formula works for mod forms if `formStart` is chosen so the block number 652 + formStart + formNo - 1 is >= 715. Form blocks below that overlap originals. [V arithmetic] |
| arm9/pokegra.c:486-488 | same | `formNo >= formCount` falls back to form 0 | invalid form | ID | none. `formCount` is a u8 in the personal record (`poke_personal_types.h`, `formCount`). [V] |
| arm9/pokegra.c:489-492 | same | palette-only forms: `rare + (formStart + formNo - 1) * 2 + 0x37ad` (0x37ad = 14253) | forms that share a sprite but differ in palette (Arceus: personal 493 has 17 forms, shared flag set) | ARC | 16 forms use members 14253..14284 (the last 32 members). Mod palette-only forms need `formStart` large enough to land past member 14300 and clear of other blocks. [V: `a/0/1/6` record 493 has formStart 0, shared 1, count 17; archive 4 members 14253..14284 are all 72 bytes] |
| arm9/pokegra.c:498-513 | same | `ARC_TOOL_GetMemberSize(arcId, index + sexFileOffset + 1)` | female image exists if that member is non-empty | ARC | an index past the archive count makes `ARC_TOOL_SeekMember` assert (`arm9/arc_tool.c`, "range check the member index" comment, `arc_tool_msg_member_bounds`). Mods must keep every probed member inside the merged archive; holes are empty members and are fine. [V comment, I behaviour] |
| arm9/pokegra.c:116-128 | POKEGRA_GetPalArcIndex | `index + 0x12 + rareOffset` | normal palette at +18, shiny at +19; `rare` is 0 or 1 | ARC | none. A mod block must supply both palette members. [V] |
| arm9/pokegra.c:86-195 | POKEGRA_Get{Cgr,Cbr,Cel,Anm,MCel,MAnm,Ncec}ArcIndex | fixed offsets +0, +2, +4, +5, +6, +7, +8 (+dirOffset 0/9, +sex 0/1 for cgr and cbr) | member roles inside a block | ARC | none. [V] |
| arm9/mcss.c:3197-3207 | MCSS_TOOL_MakeAddWorkFromParam | calls the seven functions above | battle/summary/Pokedex animated sprite members | ARC | none if the pokegra functions are fixed. [V] |
| arm9/mcss.c:3284-3290 | MCSS_TOOL_SpindaSpotHook | `cellHeaderMember == 0x1994` (= 327 * 20 + 8, Spinda front NCEC) | draws Spinda's spots | OTHER | stays valid because Spinda's block does not move. Only the front image gets spots (the back NCEC is +9). Do not move block 327. [V] |
| arm9/pokegra.c:700-707, 770 | POKEGRA_LoadCharData, POKEGRA_RegisterCgr | `monsNo == 0x147` | Spinda spot pass on static sprite | OTHER | none. [V] |
| arm9/pokegra.c:311-363, 744 area | POKEGRA_ConvertCharTo2D/1D | 0x1200-byte buffer, 12 tiles per row (0x180 bytes) | only Spinda | OTHER | none. [V] |
| ov135/worldtrade_detail.c:747 | WORLDTRADE_DETAIL_LoadMonGraphics | `GX_LoadOBJ(pal->data, 0x3F80, 0x1200)` | static sprite upload size | TABLE | static (cgr) images are 0x1200 bytes = 96 x 96, 4bpp; mod static sprites must match. [V line, I rule] |
| ov094/btlv_mcss.c:1277-1284 | the substitute branch of the function that ends at `btlv_mcss.c:1292` (name not checked) | members 0x37a0..0x37ac (14240..14252), arc `POKEGRA_GetArcID()` | Substitute doll sprite | ARC | absolute members. Never reuse slot 712 or 713 for a species. [V; archive 4 members 14240..14252 form a 13-member block with sizes unlike a species block] |
| ov094/btlv_mcss.c:2434-2452 | BTLV_MCSS_LookupShadowEntry | 0x30 = 48 entries, key `species : 14` bits (`BtlvMcssAnimEntry`, line 216) | per-species shadow offset, rotation, visibility | TABLE | not found gives the default (found = 0). Species up to 16383 can be keyed. A mod may add entries; no change required for a default shadow. [V] |
| ov208/zukan_detail_form.c:3266-3282 | ZUKAN_DETAIL_FORM_GetModelOffsets | 15 entries `(species, form, side, gender)` | Pokedex model position override | TABLE | falls back to default positions (`data_021C87C0`). No change required. [V] |
| ov021/fld3d_ci.c:1270-1275 | field "ci" Pokemon image | calls Cgr and Pal index functions | cut-in Pokemon in the field | ARC | none. [V] |
| ov112/mus_poke_draw.c:378-384 | musical Pokemon | calls the index functions with egg 0 | Musical | ARC | none. [V] |

Trainer sprites use the same archive family but a different table (not species):

| file:line | function | literal | guards | class | change |
|---|---|---|---|---|---|
| arm9/pokegra.c:529-535 | POKEGRA_GetTrainerArcID | 0x48 (72, `a/0/7/2`, 760 members) front, 0x49 (73, `a/0/7/3`, 48 members) back | trainer sprite archives | ARC | none. [V] |
| arm9/pokegra.c:537-648 | POKEGRA_GetTrainer*ArcIndex | `trType > 0x69` gives sprite 0; else `TrainerGraIndexTable[trType]`; member = sprite * 8 + 0..7 | 106 trainer classes map to 95 sprites | TABLE | the table is 106 bytes in ARM9 data (0x020A023C, max value 94 [V]). New trainer classes need a larger table. Flag `flags & 1` uses `(u8)trType` directly (player sprites). [V] |

### 2b. Icons (archive 7, `a/0/0/7`)

| file:line | function | literal / size | guards | class | change |
|---|---|---|---|---|---|
| arm9/poke_tool_ext.c:360 | POKE_TOOL_CalcGraphicIndex | `species * 2 + 7` | character member of the icon | ARC | replace `species` with the slot number. Slot 712 gives member 1431, the first member past the end. [V] |
| arm9/poke_tool_ext.c:361-362 | same | egg: `((species == 490 ? 1 : 0) + 650) * 2 + 7` | egg icon, Manaphy has its own | ARC / ID | per-species egg slot table. [V] |
| arm9/poke_tool_ext.c:363-373 | same | form: `(firstFormIndex + form - 1) * 2 + 1311` (1311 = 652 * 2 + 7) | alternate-form icon | ARC | same unified slot space as sprites (section 3). Keep `firstFormIndex` so the slot is >= 712. [V] |
| arm9/poke_tool_ext.c:374-387 | same | `ARC_TOOL_GetMemberSize(7, index + 1)` | female icon exists | ARC | holes are fine. [V] |
| arm9/poke_tool_ext.c:446-471 | POKE_TOOL_CalcIconPalette | `index = species`; egg `650 or 651`; form `firstFormIndex + form + 651`; `_IconPaletteTable[index]`, low nibble male, high nibble female | which of three icon palettes to use | TABLE | **table is 712 bytes** (slots 0..711). Slots >= 712 read past it. Replace with a table that grows (PC build), default from the ROM table. [V: the 712 bytes end where another table starts; max nibble value in the table is 2] |
| arm9/poke_tool_ext.c:503-506 | POKE_TOOL_GetIconPaletteArcId | returns 0 | palette member of archive 7 | OTHER | member 0 is one NCLR with three 16-color palettes (96 data bytes) [V: parse]. Selector values above 2 pick colors that do not exist. Callers upload 0x60 bytes (`ov021/party_select_list.c:316`). A mod icon must use one of the three palettes unless the port changes the upload size at every caller [I]. |
| arm9/poke_tool_ext.c:508-546 | POKE_TOOL_GetIconCell* | 2/4/6 and 1/3/5 | cell bank by OBJ mapping mode | OTHER | none. Archive 7 members 1..6 are cell banks and animations. [V] |
| ov021/party_select_list.c:320,325; ov034/report.c:541; ov091/plist_battle.c:263; ov091/plist_plate.c:346; ov097/vs_multi_list.c:845; ov099/wifi_club.c:1655; ov106/irc_ranking.c:1098; ov119/pokemontrade_2d.c:1271,1547; ov121/mystery_album.c:1505; ov135/worldtrade_box.c:1306; ov183/br_codein.c:1630,3190; ov194/namein_graphic.c:1364; ov198/b_plist_draw.c:2463; ov208/zukan_detail_form.c:1687 | callers | all call `POKE_TOOL_CalcGraphicIndex` or `GetGraphicIndex` | party list, box (ov170 `box2_seq.c` through the same calls), battle input (`ov094/btlv_input.c:3062`), trade, Wi-Fi, name entry | ARC | none after the two functions are fixed. [V: grep of callers, section 3] |
| ov135/worldtrade_box.c:1311 | WORLDTRADE_BOX_* | `CalcIconPalette(...) + 5` | adds the palette bank | OTHER | none. [V] |
| ov198/b_plist_draw.c:2689, ov194/namein_graphic.c:1414 | callers | `CalcIconPalette` with a form byte `(u8)iconForm` | form passed as u8 | WIDTH | form fits in a byte; fine. [V] |

### 2c. Pokedex graphics

| file:line | function | literal / size | guards | class | change |
|---|---|---|---|---|---|
| ov206/zukan_info.c:1669-1672 | ZUKAN_INFO_GetTypeArchiveId | returns 0xA7 (`a/1/6/7`, 655 members) | list mini-icon archive (name in code says "type", content is 650 species icons per `docs/ASSETS.md` "Icon archive a/1/6/7") | ARC | none. [V id, I role] |
| ov206/zukan_info.c:1674-1677 | ZUKAN_INFO_GetSpeciesCellFileId | `v + 5` | member for species v | ARC | species v >= 650 maps to member v + 5 >= 655, past the end. Mod appends members; holes are empty. Species 652 gives member 657 (655, 656 empty). [V] |
| ov206/zukan_info.c:1680-1697, 1473-1481, 1514-1517 | ZUKAN_INFO_GetCellAnimFileId / Mode, CreateSpeciesActor | `v <= 0x1ED` (493) gives file 2 mode 1, else file 4 mode 3 | generation 1-4 versus 5 cell bank | ID | species 494+ and every new species use the Gen 5 bank. No change. [V] |
| ov206/zukan_info.c:1700-1703, 1705-1712 | ZUKAN_INFO_GetIconArchiveId, GetFormFlag | 0xE9 (`a/2/3/3`, 2 members); (487, form 1) gives member 0, (492, form 1) gives member 1, else 0xFFFF | special Pokedex form silhouettes | ARC | only Giratina Origin and Shaymin Sky. A mod form silhouette needs a lookup table instead. Default is the species cell. [V] |
| ov208/zukan_detail_form.c:1661, 1687 | ZUKAN_DETAIL_FORM_* | `ARC_TOOL_CreateHandle(7, ...)`, `POKE_TOOL_CalcGraphicIndex` | form page icons | ARC | covered by 2b. [V] |
| ov208/zukan_detail_form.c:1525, 1860 | ZUKAN_DETAIL_FORM_* | species 0x147 | Spinda | OTHER | none. [V] |
| ov208/zukan_detail_area.c:778, 2261-2265 | area page | archive 0xB2 (178, `a/1/7/8`, 649 members), member = entry - 1 | habitat data per species | ARC | data topic (`species-data.md`). Members past 648 need appending. [V line, I topic] |

### 2d. Cries (pm_voice)

| file:line | function | literal / size | guards | class | change |
|---|---|---|---|---|---|
| arm9/gfl_use.c:627 | startup | `PM_VOICE_Init(1, &PM_VOICE_GetCryIndex, &PM_VOICE_PrepareCry)` | installs two callbacks | OTHER | the two callbacks are the replacement points. [V] |
| arm9/pm_voice.c:771-782 | PM_VOICE_GetCryIndex | `species == 492 && form == 1` gives 650; else species | species/form to WAVARC slot | ARC | replace with a (species, form) to wave map; default unchanged. [V] |
| arm9/pm_voice.c:589, 441-474 | PM_VOICE_Prepare, PM_VOICE_LoadWave | `NNS_SndArcGetWaveArcInfo(waveId)`, null returns 0 | wave file for the cry | ARC | a wave slot past the SDAT list gives no cry. [V null path; I list bounds] |
| arm9/pm_voice.c:795-801 | PM_VOICE_PrepareCry | `ARC_TOOL_ReadMember(record, 194, species)`; volume = second halfword low byte | per-species cry volume, archive 194 (`a/1/9/4`, 652 members of 4 bytes) | ARC | mod appends member `species` (holes empty = volume 0 [I]). Panics if the member is past the count. [V sizes: members 0..651 are 4 bytes each] |
| arm9/pm_voice.c:805-812 | PM_VOICE_PrepareCry | species 441, form 0 | recorded-voice special case | OTHER | none. [V] |
| arm9/pm_voice.c:341 | PM_VOICE_AllocBuffers | 0x5C30-byte buffer per voice | cry file size | TABLE | see `sound.md`. [V] |

### 2e. Other Pokemon pictures (all go through the sites above)

| user | calls | note |
|---|---|---|
| Summary screen `ov173/poke_summary_actor.c:217-219` | `POKEGRA_RegisterCgrPP`, `RegisterPalettePP`, `RegisterCellAnimPP` | static sprite from archive 4. [V] |
| Summary 3D/battle-like view `ov131/p_sta_sub.c:999,1010` | `MCSS_TOOL_AddPokeMcss` | animated sprite from archive 4. [V] |
| PC box zoom `ov170/box2_seq.c:10091-10093` | Register*PP | archive 4; box slot icons use archive 7. [V] |
| Hall of Fame `ov180/hall_of_fame_scene.c:574,1241-1246` | MakeAddWorkFromPP, Register*PP | archive 4. [V] |
| Evolution `ov195/shinka_demo_view.c:782-794,1129-1134` | AddPokeMcss, Cgr/Pal index | archive 4. [V] |
| Trade `ov119/pokemontrade_3d.c:700,702`, Mystery Gift `ov121/mystery.c:1858-1862`, `mystery_album.c:2377-2383`, World Trade `ov135/worldtrade_detail.c:735-754` | Register*PP / LoadCharDataPP | archive 4. [V] |
| Intro `ov204/intro_g3d.c:237` | MakeAddWorkFromPP | archive 4. [V] |
| Field script icon window `ov021/scrcmd_misc.c:1345-1349, 1682-1686` | POKEGRA_Register* | archive 4 static image. [V] |
| Trainer card | uses trainer sprites (2a trainer table), not species | [I: no species-keyed call found] |

## 3. Data and assets

### Archive 4, `a/0/0/4` (14285 members)

Layout, from the index arithmetic (2a) and checked against member sizes:

| members | content |
|---|---|
| slot n = 0..649, members 20n .. 20n+19 | one species block |
| 13000..13019 (slot 650) | normal egg block |
| 13020..13039 (slot 651) | Manaphy egg block |
| 13040 .. 14239 (60 blocks, slots 652..711) | alternate forms. Block k (0-based) is `13040 + 20k`. `formSpriteIndex` in personal data gives k. Highest k in the data is 59 (record 649 has index 56 with 5 forms). [V: scan of `a/0/1/6`] |
| 14240..14252 | Substitute doll (13 members), used by `btlv_mcss.c:1277-1284` |
| 14253..14284 | palette-only forms, 2 palettes (normal, shiny) per form, 16 forms |

Inside a block (20 members): +0 static front image (male), +1 static front (female, empty if none), +2 MCSS texture front (male), +3 same female, +4 cell bank, +5 cell animation, +6 multi-cell, +7 multi-cell animation, +8 NCEC header. +9..+17 are the same nine for the back view. +18 normal palette, +19 shiny palette. [V: offsets from `pokegra.c:86-195, 116-128, 453-471`; sample species 649 shows a zero-size female image at +1 and +3 and 72-byte palette pair at +18, +19; the compression and file types are in `docs/ASSETS.md`, "Battle sprite colors"]

What a mod author adds for a new species: one 20-member block (or fewer if female and back variants are absent: members may be empty but the palette pair and front images must exist). For a form: one block, plus a personal `formSpriteIndex`. For a palette-only form: two palette members.

Does appending members work? The mod layer supports it: a member index past the archive end extends it, holes are empty members, the format limit is 0xFFFF members (`docs/mods.md`, "Member overrides", lines 141-174, 157-159, 319). That leaves room for (65535 - 14300) / 20 = 2561 more blocks [V arithmetic].

Would species ids past 651 land on valid indices? Only from 715 on.

| species id | block member | result |
|---|---|---|
| 650, 651 | 13000, 13020 | egg blocks (the egg ids are species ids in the game) |
| 652..711 | 13040 .. 14220 | form blocks of existing species |
| 712 | 14240 | Substitute doll (13 members) and the first palette-only members |
| 713, 714 | 14260, 14280 | palette-only form members |
| >= 715 | >= 14300 | past the end, appended blocks work with the arithmetic unchanged [V arithmetic] |

The cleaner fix is one function `graphic_slot(species)`, used by every site that computes `species * 20` or `species * 2 + 7` or indexes `_IconPaletteTable`. For species 0..649 it returns the species. For a mod species it returns a mod-assigned slot n >= 715. A form's slot is `652 + formStart + formNo - 1` today, so a mod form needs `formStart` >= 63 to reach slot 715.

### Archive 7, `a/0/0/7` (1431 members)

Members 0..6: member 0 icon palettes (3 x 16 colors), members 1..6 cell banks and animations for the three OBJ mapping modes. Then slot n has members `7 + 2n` (male or common icon) and `8 + 2n` (female, empty if none). Slots 0..711 = 7 + 2 * 712 = 1431 [V: sizes at 1300..1315 and 1420..1430 alternate 0 and 1072 bytes]. Slot 650 and 651 are eggs, 652..711 forms. A new icon pair is appended at member `7 + 2n`; for n = 712 that is 1431, the first free member.

### Archive 167, `a/1/6/7` (655 members)

Member 0 palette, 1..4 cell data, 5..654 one mono icon per species 0..649 (`docs/ASSETS.md` "Icon archive a/1/6/7" lines 2549-2575). New species append at member `species + 5`. The two form silhouettes are in archive 233 (`a/2/3/3`, 2 members, 86 and 99 bytes) [V].

### Archive 194, `a/1/9/4` (652 members of 4 bytes)

Cry volume per species, member = species (0..651). Append at `species`.

### Palette selector table (code, not data)

`_IconPaletteTable`, ARM9 0x0209E474, one byte per slot, 712 bytes, two 4-bit values (male, female) each 0..2 [V: parsed from the decrypted ARM9 image]. It is a data symbol in the compiled game (`poke_tool_ext.c:75`, extern). A mod cannot extend it with a data file; the port must replace the lookup (PC-only code in `poke_tool_ext.c` inside `#ifndef __MWERKS__`) or serve a bigger array.

### Cries

WAVARC slot N is the cry of species N, slot 650 is Shaymin Sky; 651+ are sound effects (`docs/ASSETS.md` lines 540-560; `sound.md`). Cry wave files are single-wave PCM8 (`sound.md`).

## 4. Save data

Nothing in the sprite, icon, Pokedex-graphic or cry paths is stored in the save by graphic id. The species, form, gender, shiny and egg values come from the Pokemon record. Shiny comes from the id/personality (`POKE_TOOL_IsBoxShiny`), form is read as value 0x6F, gender 0x6E, egg 0x4C (`pokegra.c:665-668`). Width limits on the species and form fields are in `species-data.md`. The form value is passed as u8 or u32 by the callers (2b); I found no narrower width in this path. Nothing here needs eidos's per-save mod storage.

## 5. Mod author versus port

The mod author supplies:

- For a species: one 20-member sprite block (section 3), a 2-member icon pair, a Pokedex mini icon, a cry wave file and a volume, and (optionally) shadow/model-position entries.
- For a form: one sprite block and one icon pair, a personal `formSpriteIndex` that lands past the originals, and a Pokedex silhouette if it should differ from the species.
- A graphic-slot assignment per species (n >= 715), an egg slot if the egg should look different, and the icon palette selector values (0..2).
- Files in the formats the game uses (`docs/mods.md` "Keep the member files in the game's own format").

The port must unlock:

1. **A species-to-slot function** used at `pokegra.c:451`, `pokegra.c:473-481` (egg), `poke_tool_ext.c:360-362` and `poke_tool_ext.c:453-455`, defaulting to the arithmetic above. A raw hook on `POKEGRA_CalcArcIndex` (arm9 0x0202D4A8) can do it without touching game code, or a PC-only block in aletheia.
2. **A growable palette selector** replacing `_IconPaletteTable[...]` at `poke_tool_ext.c:468-470`.
3. **A cry map** (`PM_VOICE_GetCryIndex`, `PM_VOICE_PrepareCry`) and SDAT entries (`sound.md`).
4. **Pokedex tables:** member `species + 5` in archive 167 works by appending; the two hard-coded form silhouettes (`zukan_info.c:1705-1712`) and the 48-entry shadow table (`btlv_mcss.c:2434`) only matter for special cases.
5. **A trainer-graphic table** if new trainer classes are wanted (106-entry table, `pokegra.c:545`).
6. **Guard rails in the merge tool:** warn when a mod slot is below 715 (collision with originals), when a probed member (`index + 1`, `+3`, `+10`, `+12`) would be past the merged archive, and when a static sprite is not 0x1200 bytes [I].

## 6. Open questions

1. **MCSS size limits.** I did not check whether the MCSS loader has a maximum texture or cell size per sprite (`arm9/mcss.c`, `MCSS_LoadResource`, around lines 2400-2515, and the work buffers). A mod sprite larger than the originals may overflow. Look there first.
2. **Empty-member behaviour in the loaders.** A hole in the merged archive is an empty member. I did not trace what `ARC_UTIL_LoadScreenDataFromArchive` and the cell bank loaders do with a size 0 member for a front image. The female probe handles it (`pokegra.c:502-506`). The static image and palette members must not be empty.
3. **ARC_TOOL_SeekMember range failure.** I read the comment (`arc_tool.c`, "range check the member index", assertion with count and member) but did not run it. Whether the PC build panics or returns is unverified.
4. **Species width in the Pokemon record and Pokedex bit arrays** are `species-data.md` topics. This report assumes species ids up to about 2000 fit; the graphics tables above do not limit that.
5. **Who else reads `_IconPaletteTable` or `TrainerGraIndexTable`.** `layout/data_objects.json` lists the table as a 27-byte object (the object boundaries are wrong there). Only `poke_tool_ext.c:468-470` and `pokegra.c:546` read them in `src/game` [V: grep]; ARM9 code not yet decompiled would not show up. I did not check `layout` for unnamed readers.
6. **Hall of Fame, Mystery Gift, Wi-Fi code that copies sprite state across the network** (for example `ov121/mystery_album.c:2377`) passes the record, not a slot, so a remote player without the same mod would see wrong graphics. That is a mod-network question, not a port question.
7. **a/1/6/7 role.** Code names archive 0xA7 "type" (`zukan_info.c:1669`) while `docs/ASSETS.md` decodes 650 mono species icons in it and the code uses member `species + 5`. I take the usage as the truth: species icons for the Pokedex list. Not seen on screen.
8. **Substitute and other absolute members in archive 4.** I found only `btlv_mcss.c:1277-1284`. Other absolute indices could exist in overlays I did not read in full (grep for 0x37a0..0x37ac and 14240..14284 found only this site) [I].
