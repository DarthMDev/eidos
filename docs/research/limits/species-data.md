# Limits survey: species, data and logic

Tags: [V] verified in code or data, [I] inferred, not verified.
Paths: aletheia sources are under `src/game/`. Data is the extracted NitroFS tree.
Archive id = x*100+y*10+z for `a/x/y/z` [V: 0x10 = a/0/1/6, 0xC2 = a/1/9/4, 0x7B = a/1/2/3 match the callers below].

## 1. Summary

Original count: species 1..649. Ids 650 and 651 are the egg and bad egg. The species name bank has 652 strings, 0..651 [V: `a/0/0/2` member 0x46 header says count 652].

Difficulty: high. Species are u16 almost everywhere, so ids past 651 fit. The trouble is that the id is used directly as a member index, and the members past 649 are already taken.

Biggest obstacles:

1. **Id collision with form records (the plan assumption "new species start at 652" breaks).** Personal `a/0/1/6` has 669 members: 0 sentinel, 1..649 species, 650..667 alternate-form records, 668 the regional dex table. Learnsets `a/0/1/8` (668) and evolutions `a/0/1/9` (668) have the same layout. The sprite archives put the egg at 650/651 and alternate forms from 652 on. A new species 652 would read a form record, a form sprite and a form learnset. [V: scan of `a/0/1/6`, species 386 has `formStatsIndex` 650, forms end at member 667]. The port needs one species-to-member remap used by every archive lookup (section 2a), or the forms must move.
2. **Silent `> 0x289` filters** (about 25 sites, 2b and 2c). Dex registration, scripts, trade beacons, mystery gift, battle record and rules drop or zero the species.
3. **Fixed save and ARM9 data tables.** Dex bitsets hold 672 bits (84 bytes): species 650..672 fit, 673+ overflow into the next array with no check. Language flags stop at 493. The form-count table, the icon palette table and the dex form bits (9 bytes) are fixed.
4. **Regional dex table has 650 entries** (`a/0/1/6` member 668, indexed by species). Dex code reads `table[species]` with no bounds check. `a/0/9/8` members are also sized by count.
5. **Cries.** The wave id is the species, an index into the SDAT wave archive list. A new species needs new SDAT entries (section 6).

Also: `sdk/mod_export.c` refuses species above 493. That constant is stale (the game has 649).

## 2. Site table

Classes: ID check, TABLE (size), SAVE, WIDTH (storage width), ARC (index arithmetic), TEXT, OTHER.

### 2a. Core species and personal data

| Site | Function | Literal | Guards | Class | Change |
|---|---|---|---|---|---|
| arm9/poke_personal.c:425 | POKE_PERSONAL_GetFormDataId | `(u16)(species+0xFD76) <= 1` (650, 651) | egg and bad egg return member 0 | ID check | keep. Add the remap for species >= 652. [V] |
| arm9/poke_personal.c:428-433 | same | member = species; forms at `formStatsIndex+form-1` | picks personal and learnset member | ARC | species 652..667 collide with form members. Remap. [V] |
| arm9/poke_personal.c:371 | POKE_PERSONAL_LoadFormTable | member 0x29C (668), 650 u16 | regional dex number per species, 999 = none | TABLE | grow to cover new ids. Callers index it unchecked (2b). [V: values 0..155, 156 non-999] |
| arm9/poke_personal.c:106-108, 139 | LoadData, LoadDataDirect | archive 0x10 (60-byte record), 0x12 (learnset) | use GetFormDataId | ARC | covered by the remap. [V] |
| arm9/poke_tool.c:388 | POKE_TOOL_InitSystem | cache species 0x28B | "nothing cached" marker | OTHER | keep 651 unused. [V] |
| arm9/poke_tool.c:3808-3815 | POKE_TOOL_GetPersonalData | cache key (u16 species, u16 form) | record cache | WIDTH | fine. [V] |
| arm9/poke_tool.c:2290, 2312, 3898 | CheckSpeciesTmCompat, CheckSpeciesTutorCompat, NormalizeForm | 0x28A | egg has no TM or form | ID check | fine. [V] |
| arm9/poke_tool.c:2887, 2896 | POKE_TOOL_GetBoxValueRaw | result 650 | egg or bad-checksum species | ID check | fine. [V] |
| arm9/poke_tool.c:3011, 3018 | GetBoxValueRaw | string id 651 | bad-egg name | TEXT | a new name may not use index 651. [V] |
| arm9/poke_tool.c:3244 | SetBoxValueRaw | `species = (u16)value` | no range check on set | WIDTH | u16. [V] |
| arm9/poke_tool_types.h:29, BlockB | struct BoxPokemonBlockA/B | `u16 species`; form 5 bits, gender 2 bits | record storage | WIDTH | species to 65535, forms 0..31. [V] |
| arm9/poke_tool_ext.c:360-381 | POKE_TOOL_CalcGraphicIndex | `species*2+7`; egg `(650+..)*2+7`; forms `(first+form-1)*2+1311` | party icon, archive 7 (`a/0/0/7`, 1431 members) | ARC | species 652 maps to 1311, the first form icon. Remap. [V] |
| arm9/poke_tool_ext.c:455-470 | POKE_TOOL_CalcIconPalette | `_IconPaletteTable[index]`; egg 650; forms `first+form+651` | 4-bit palette per icon | TABLE | ARM9 data at 0x0209E474, about 715 bytes [V: ASSETS notes; layout lists size 27 from a partial datamap]. Needs a larger table and the remap. |
| arm9/pokegra.c:451, 481, 494 | POKEGRA_CalcArcIndex | `monsNo*0x14`; egg `(0x28A+isManaphy)*0x14`; forms `(first+form-1)*0x14+0x32F0` | battle and dex sprites, archive 4 (14285 members, 20 per index) | ARC | 0x32F0 = 652*20, so species 652 hits the first form. Remap. [V] |
| arm9/pm_voice.c:773 | PM_VOICE_GetCryIndex | `species < 1 && species > 649` | dead code, never true | ID check | no real guard; any id passes. [V] |
| arm9/pm_voice.c:778 | same | cry index 650 for species 492 form 1 | Shaymin sky cry | ARC | slot 650 is taken. [V] |
| arm9/pm_voice.c:807 | PM_VOICE_PrepareCry | `ARC_TOOL_ReadMember(record, 194, species)` | cry volume record, `a/1/9/4` (652 x 4 bytes) | ARC | append members from 652. [V] |
| arm9/pm_voice.c:441-447 | PM_VOICE_LoadWave | `NNS_SndArcGetWaveArcInfo(waveId)`, null returns 0 | cry sample from SDAT | ARC | SDAT needs entries for new ids [I: eidos SDAT mapping not checked]. |
| arm9/shinka_check.c:415, 429 | SHINKA_CHECK_GetRowParam, LoadData | archive 0x13, member = raw species | evolution rows | ARC | raw species, not via GetFormDataId. Same collision. [V] |
| ov021/sodateya.c:162, 1490, 1520 | SODATEYA_CanLearnTm, GetBaseSpecies | `SPECIES_COUNT 0x289` | day care | ID check | raise. [V] |
| ov021/sodateya.c:1522 | SODATEYA_GetBaseSpecies | `ARC_TOOL_ReadMemberRange(.., 0x14, species, 0, 2)` | `a/0/2/0`, 650 members | TABLE | append members. [V] |
| ov021/sodateya.c:1304 | SODATEYA_AddEggMoves | `ARC_TOOL_LoadMember(0x7b, species)` | `a/1/2/3`, 650 members | TABLE | append members. [V] |
| arm9/natsuki.c:192 | | `species == 650` | early return | ID check | fine. [V] |
| ov010/mystery_gift_mon.c:328; ov010/scrcmd_proc.c:906, 1633; ov091/plist_plate_types.h:37; ov121/mystery.c:1846; ov121/mystery_album.c:2348; ov121/mystery_net.c:985; ov021/sodateya.c:1472 | | name index 0x28A / 650 | egg name from the name bank | TEXT | fine. [V] |
| arm9/wordset.c:883 | WORDSET_LoadSpeciesNameBank | bank 0x46 of `a/0/0/2` | species names | TEXT | count 652 now. Append names 652+. [V] |
| arm9/msgdata.c:293 | MSGDATA_ReadString | `index >= count` clears the string | name read | TEXT | out of range gives an empty name. [V] |
| arm9/msgdata.c:403-421 | MSGDATA_AllocString | no range check before the entry read | name alloc | TEXT | with a non-resident bank it reads past the table. Count must grow. [V] |

### 2b. Dex save and dex UI

| Site | Function | Literal | Guards | Class | Change |
|---|---|---|---|---|---|
| arm9/zukan_save.c:182 | ZUKAN_SAVE_IsInvalidSpecies | `species > 0x289` | gate for IsSeen, IsCaught, RegisterSeen, RegisterCaught | ID check | raise. Central. [V] |
| arm9/zukan_save.c:48-63 | struct ZukanSave | `caughtFlags[0x54]`, `seenFlags[4][0x54]`, `viewGenderFlags[4][0x54]` | 672 bits each | SAVE | species 1..672 fit. SetSeenGenderFlag (:333-360) and RegisterCaught (:843) do no bounds check, so 673+ writes into the next array. Cap at 672 or use mod storage. [V] |
| arm9/zukan_save.c:48-49 | | `dexObtained : 10`, `lastViewedSpecies : 10` | bit fields | WIDTH | max 1023. [V] |
| arm9/zukan_save.c:61 | | `languageFlags[0x1B0]` | 7 bits x 493 | SAVE | fixed. |
| arm9/zukan_save.c:207 | ZUKAN_SAVE_SetLanguageFlag | `species > 493` return | language bit | ID check | species 494..649 already have no language flag. [V] |
| arm9/zukan_save.c:1275 | ZUKAN_SAVE_HasLanguageFlag | `species > 0x1ED` return 0 | same | ID check | same. [V] |
| arm9/zukan_save.c:62-63 | | `seenFormFlags[2][9]`, `viewFormFlags[2][9]` | 72 bits per shiny state for all multi-form species | SAVE | no room for new form species. [V] |
| arm9/zukan_save.c:280-300, 694 | GetFormBitBase, GetFormTableIndex, GetFormCount | `sZukanFormTable` (0x0209DE4C), zero-terminated `{id,count}` | form bit offsets | TABLE | ARM9 data. New form species need a new table and bits. [V] |
| arm9/zukan_save.c:406-440 | SetSeenFormFlag | species 0x15F, 0x1A5, 0x22B, 0x288 | special form counts | OTHER | none. [V] |
| arm9/zukan_save.c:218-235 | IsCountedFor*Completion | tables of 15 and 6 species | completion exclusions | TABLE | no limit. [V] |
| arm9/zukan_save.c:882, 898, 1016, 1032, 1181, 1194, 1258 | CountCaught/CountSeen National/Regional (+Counted) | `species <= 0x289` | loop bounds | ID check | raise. The Regional loops read `pFormTable[species]` (650 entries): out of range past 649. [V] |
| arm9/zukan_save.c:1149-1175 | Is*Complete | derived from counts | completion | OTHER | threshold not read [I]. |
| ov206/zukan_info.c:781, 806, 1493, 1686, 1694 | ZUKAN_INFO_* | `species > 493 / 0x1ED` | picks the Gen-5 layout | OTHER | not a limit. [V] |
| ov206/zukan_info.c:953 | ZUKAN_INFO_DrawMainPage | `tab[species]` of the regional table | regional number | TABLE | out of range past 649. [V] |
| ov206/zukan_info.c:1691 | GetSpeciesCellFileId | `v + 5` | cell file in archive | ARC | needs members. [V] |
| ov209/zukan_list.c:311, 1033, 1069 | struct ZukanList | `speciesText[649]`, `itemCount=649`, `i<649` | one string per list row | TABLE | array is in the struct; later field offsets (0xA70...) and raw `+0x4c` access (:1063) depend on it. Needs a larger array. [V] |
| ov212/zukan_search_engine.c:167, 243-245 | | `count == 649`, `used < 649`, return 649 | national list | ID check | raise. [V] |
| ov212/zukan_search_engine.c:274-278 | LoadFormIds | 0x289, buffer 0x512 | national list | TABLE | raise, buffer 2*count. [V] |
| ov212/zukan_search_engine.c:297 | LoadTables | `count1 = 0x28A` | regional table count | TABLE | raise. [V] |
| ov212/zukan_search_engine.c:152-154 | | `v & 1023` species, `(v & 0xFC00) >> 10` form | packed ids from `a/0/9/8` | WIDTH | species max 1023, form max 63. [V] |
| ov212/zukan_search_engine.c:161-162 | | `table1[species]`, `table2[species]` | search filters | TABLE | out of range past entry count. [V] |
| ov212/zukan.c:490, ov206/zukan_info.c:1455 | | fixed species 494 | Victini-class special case | OTHER | none. [V] |

### 2c. Other species-id checks

| Site | Function | Literal | Guards | Class | Change |
|---|---|---|---|---|---|
| arm9/battle_rec.c:773 | BATTLE_REC_GetHeaderParam | `value > 0x289` returns 0 | header species (u16 x 12, battle_rec_types.h:26) | ID check | raise. [V] |
| ov010/game_beacon_accessor.c:587 | | `monsNo > 649` becomes 504 | beacon icon | ID check | raise. [V] |
| ov010/game_beacon_accessor.c:899, 913, 945 | | `> 0x289` | beacon action args | ID check | raise. [V] |
| ov010/mystery_gift_mon.c:200 | | `species > 649` | gift validity | ID check | raise. [V] |
| ov121/mystery.c:1783 | | `species>649` | gift parse | ID check | raise. [V] |
| ov121/mystery_album.c:2228 | | `species <= 0x289` | gift view | ID check | raise species only. [V] |
| ov121/mystery_net.c:849 | | `species > 0x289` | received gift | ID check | raise. [V] |
| ov010/scrcmd_proc.c:1000, 1065 | | `species <= 0x289` | script party tests | ID check | raise. [V] |
| ov010/scrcmd_symbol.c:306 | | 0x28B | unseen-species sentinel | OTHER | fine. [V] |
| ov010/scrcmd_event_mon.c:172-206 | | event species 243..245, 251, 492, 647..649 | event flag map | OTHER | new event species need entries. [V] |
| arm9/regulation.c:219 | | `mustPoke < 0x28B` | battle rule species | ID check | raise. [V] |
| ov135/worldtrade_deposit.c:1129-1134 | | buffer 0x28A, `tbl[i] < 0x28A` | GTS allowed-species flags from archive 0x62 member 4 | TABLE | raise. [V] |
| ov183/br_codein.c:3347 | | `a >= 0x28B` | code input | ID check | raise. Many comments repeat `> 0x289`; only this live site was read. [I] |
| ov123/gsync_disp.c:711, ov123/gsync_state.c:1196 | | `species <= 493` | Global Link sync icons | ID check | raise if in scope. [V] |
| sdk/mod_export.c:196, 631 | species_known | `BW_MOD_MON_MAX_SPECIES 493u` | export refuses above 493 | ID check | stale; follow the game max. It also tests base HP of the personal record, so the remap must serve it. [V] |

### 2d. Storage widths

| Structure | Width | Evidence |
|---|---|---|
| Box and party mon | u16 | arm9/poke_tool_types.h:29 [V] |
| Wild encounter slot | 11 bits | ov021/field_encount_types.h:17, ov010/field_encount_st_types.h:8 [V] |
| Trainer battle instance | 11 bits | ov010/fld_btl_inst_tool_shared_types.h:21 [V] |
| Battle record header | u16 x 12 | arm9/battle_rec_types.h:26 [V] |
| Beacon mon record | u16 | arm9/beacon.c:306 [V] |
| Dex search ids | 10 bits | ov212/zukan_search_engine.c:152 [V] |
| Trainer party data | not read (archives 0x5a-0x5d, 0xd3, 0x30 via arm9/tpoke_data.c) | [I] |

## 3. Data and assets

| Archive | Members | Record | Species indexing | Is appending enough? |
|---|---|---|---|---|
| `a/0/1/6` personal | 669 | 60 bytes; member 0 is 56 bytes; member 668 is 1300 bytes (650 u16 regional dex order) | member = species up to 649, forms 650..667 | No. Members 652..667 are forms. Remap, or put new species in members 669+. Member 668 grows. [V] |
| `a/0/1/8` learnsets | 668 | (move, level) u16 pairs, ends 0xFFFF,0xFFFF | via GetFormDataId | Same collision. [V] |
| `a/0/1/9` evolutions | 668 | 42 bytes, 7 rows of (method, param, species) | raw species | Same collision. [V] |
| `a/0/2/0` base species | 650 | one u16 | raw species | Yes, append. [V] |
| `a/1/2/3` egg moves | 650 | u16 count then moves | raw species | Yes, append. [V] |
| `a/1/9/4` cry record | 652 | 4 bytes | raw species; 650 is Shaymin sky | Append from 652. [V] |
| `a/0/0/4` battle sprites | 14285 | 20 members per index | species*20; egg 650/651; forms from 652*20 | No. Collision. [V] |
| `a/0/0/7` party icons | 1431 | 2 members per index, base 7 | species*2+7; forms from 1311 | No. Collision. [V] |
| ARM9 icon palette table | about 715 bytes | 2 nibbles per byte | same index | No, ARM9 data. [V] |
| `a/0/0/2` bank 0x46 | 652 strings | species names | index = species | Yes, append. Strings 650 and 651 stay. [V] |
| `a/0/0/2` banks 0xEB, 0xEC, 0x104 (650), 0xF4, 0xF5, 0x10C (727), 0x119, 0x11C (652) | as listed | not decoded | probably per-species dex text | Probably appendable. Role inferred from counts only. [I] |
| `a/0/9/8` dex search | 10 | m0 727 u16, m1 650 bytes, m2 727 bytes, m3-5 649 u16, m6-9 725 u16 | species or form id | No: the mod must replace whole members. [V] |
| `a/1/6/7` icons | 655 | LZ icons, 650 | not reached by code read here | [I] |

Form-having species and their extra personal members (data scan) [V]: 351 -> 662, 386 -> 650, 413 -> 653, 479 -> 657, 487 -> 656, 492 -> 655, 550 -> 665, 555 -> 666, 648 -> 667.

## 4. Save data

- Dex (`struct ZukanSave`, arm9/zukan_save.c:46-63): caught 84 bytes, seen 4 x 84, view gender 4 x 84, seen forms 2 x 9, view forms 2 x 9, language 432 bytes, Spinda personality. Species to 672 fit without a layout change. [V]
- Beyond 672, language flags above 493, and new multi-form species: use eidos per-save mod storage (sdk/mod_savedata.c, one file per save) for mirrored flags. [I: design suggestion]
- Mon records hold species as u16, so the original save format carries new ids. [V]
- Dex bit fields tolerate ids to 1023. [V]

## 5. Mod author supplies vs port unlocks

Port unlocks:
- A species-to-member remap for personal, learnset, evolution, battle sprite, party icon and palette lookups, and a raised max species.
- Every `> 0x289` and `> 649` site in 2b and 2c.
- Dex: bitset bounds (672 cap or mod storage), `ZukanList` array size, regional table reads, language and form bits.
- Replacement for ARM9 data tables: icon palette and `sZukanFormTable`.
- Cry wave lookup (SDAT) and message banks sized by count.
- eidos: `mod_export.c` max species.

Mod author supplies: personal record, learnset, evolution row, egg-move list, base-species entry, cry record and wave, battle sprite set (20 members), party icon (2 members) and palette nibble, names and dex texts, dex slots and search lists (whole members), form data.

## 6. Open questions

1. Can the mod layer extend the SDAT wave list? Look at `NNS_SndArcGetWaveArcInfo` in aletheia `arm9/nns_snd_arc.c` and eidos `engine/audio/sdat.c`.
2. What `ARC_TOOL_ReadMember` and handle member offset do for a member past the NARC end (eidos `sdk/vfs_narc.c` decides).
3. Trainer party species widths and any 650 egg handling in battle setup (ov093, ov095 not searched).
4. Which `a/0/0/2` banks are dex text: read `tbl_021BC1C4` in `ov206/zukan_info.c`.
5. Footprint, height, weight and cry pitch tables: not found by a species literal. Likely in dex archives (`a/0/9/8`, 0x9F) or the 727-count banks. Not read.
6. `ov183/br_codein.c` comment sites and `ov186/br_pokesearch.c` species tables: live code not fully read.
7. GTS and trade species checks other than `worldtrade_deposit.c:1134` were not searched.
8. Exact length of `_IconPaletteTable` in the ARM9 image (715 from ASSETS notes, 27 from the layout).
9. Decision: remap new species, or move the 18 form records out. Moving keeps ids dense but changes more archives.
