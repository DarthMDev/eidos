# Limits survey: sound

Tags: [V] verified in code or data, [I] inferred, not verified.
Paths: `A:` is the aletheia tree `src/game/` (arm9/ and ov*/). `E:` is eidos.

## 1. Summary

Original counts, read from `wb_sound_data.sdat` (51,533,312 bytes) [V, parsed
with a scratch script this pass; they match aletheia's `docs/ASSETS.md`
"SDAT internal structure"]:

| record | table size | populated | notes |
| --- | --- | --- | --- |
| SEQ | 2076 | 940 | last populated index 2075; 940 have a SYMB name |
| SEQARC | 0 | 0 | |
| BANK | 1986 | 1497 | max bank referenced by a SEQ is 1985 |
| WAVARC | 1986 | 1497 | cries are 1..650 |
| PLAYER | 7 | 7 | players 0..6 |
| GROUP | 1 | 1 | group 0, 38 items, all SEQ with load flag 7 |
| PLAYER2, STRM | 0 | 0 | |
| FAT | 3874 files | | SSEQ 0..885, SBNK 886..2379, SWAR 2380..3873 |

Block sizes: SYMB 91,145 bytes, INFO 59,916, FAT 61,996, FILE 51,320,188.

Unlocking difficulty: low for sequences, banks and wave archives; medium for
cries; the SDAT must be merged at load time.

Answer to the coordinator's claim ("new sounds need no code change"):

- SDAT counts are read from the file: [V]. Every record lookup bounds its
  index against `infoTable->count` read from the INFO block
  (`A:arm9/nns_snd_arc.c:148-175` seq, `:206-233` bank, `:235-262` wave
  archive, `:264-320` player and group; FAT checks at `:322-377`). The loader
  copies INFO and FAT into the sound heap sized by the file's own sizes
  (`A:arm9/nns_snd_main.c:261-289`). eidos's reader does the same
  (`E:engine/audio/sdat.c:119-140`).
- No code-side table is sized by the sequence, bank or wave count: [V] by
  reading all of `nns_snd_arc.c`, `snd_bank.c`, `nns_snd_main.c`, `gfl_use.c`
  (sound part) and `pm_voice.c`. The only count constants are listed in
  section 2. They are not the SDAT counts.
- "Sound effects fit player 2's 10200-byte heap": the number is right. PLAYER
  records 1, 2, 3 and 5 have heap size 0x27D8 = 10200 bytes and one playable
  sequence [V, scratch parse]. "About 1 second" is [I]: 10200 bytes of PCM8
  is about 0.97 s at 10.5 kHz, and about twice that for IMA-ADPCM. A sound
  effect needs its bank, wave archives and sequence all inside that heap
  (`A:arm9/nns_snd_arcplayer.c`, `NNSi_SndArcPlayerStartSeq`: bank with load
  flags 6, then sequence with flag 1, both into the player heap). Player 4
  has 0x7D0 = 2000 bytes, players 0 and 6 have none [V]. A mod effect that is
  too big fails at start and plays nothing; nothing in the code crashes on it
  (`nns_snd_arcplayer.c`, error returns 0).
- "Today a mod with added sounds must ship the whole SDAT": [V]. `E:sdk/vfs.c`
  resolves files whole, and only NARCs have member-level merging
  (`E:sdk/vfs_narc.c`, `E:sdk/vfs.h` header). kenon's `docs/game-data-map.md:444-449`
  says the same ("replaced as whole files"; kenon's `core/src/sdat` can build
  a new SDAT). I did not run kenon's builder and did not test that its output
  loads in the game [not verified]. That kenon "grew an SDAT with old files
  byte-identical" and aletheia's `sdat.py` parses it is the other
  investigation's report; I did not reproduce it.

The 3-5 biggest obstacles, in order:

1. **Cry wave id is the species id.** `PM_VOICE_GetCryIndex` returns the
   species as the WAVARC index with no table
   (`A:arm9/pm_voice.c`, function `PM_VOICE_GetCryIndex`). Species 650 and up
   land on WAVARC 650 (Shaymin Sky cry) and on the sound-effect archives from
   651. A species-to-WAVARC map must replace it.
2. **Cry size cap.** A cry file is read into a fixed 0x5C30 = 23,600 byte
   buffer with no bounds check (`PM_VOICE_LoadWave`, `PM_VOICE_BUFFER_SIZE`).
   The largest shipped cry is exactly 23,600 bytes, WAVARC 124 [V, scratch
   parse]. A larger mod cry overruns the buffer.
3. **Sound-effect slot map covers only the original range.**
   `GFL_USE_GetSeSlot` accepts only sequence ids 0x546..0x81A and returns
   slot 1 for everything else. New sequences have ids from 2076 (0x81C), so
   they all share slot 1 and cut each other off.
4. **Whole-file SDAT.** A mod cannot add a sound without the 51 MB file.
5. **Id plumbing.** Sounds are numeric everywhere in game code (no SEQ_*
   constants exist in the C tree [V, grep]). A mod needs names that resolve
   to stable numbers.

## 2. Site table

| Site | Function | Literal / size | Guards | Class | Change needed |
| --- | --- | --- | --- | --- | --- |
| `A:arm9/nns_snd_arc.c:148-175` | `NNS_SndArcGetSeqInfo` | `entryNo >= infoTable->count` | seq id vs INFO count | id check | none: count comes from the file [V] |
| `nns_snd_arc.c:206-233` | `NNS_SndArcGetBankInfo` | same | bank id | id check | none [V] |
| `nns_snd_arc.c:235-262` | `NNS_SndArcGetWaveArcInfo` | same | wave archive id | id check | none [V] |
| `nns_snd_arc.c:322-377` | `NNS_SndArcGetFileSize`, `NNS_SndArcReadFile` | `fileId >= fatBlock->count` | FAT index | id check | none; FAT grows with the file [V] |
| `A:arm9/nns_snd_main.c:261-289` | `NNSi_SndArcLoadTables` | INFO/FAT sizes from header | loads INFO+FAT into the sound heap | table size | none, but heap budget: see below |
| `A:arm9/gfl_use.c:1563-1565` | `GFL_USE_InitSound` | 0x95AD0 = 612,048 | sound heap size; holds INFO (59,916) + FAT (61,996) = 121,912 bytes plus BGM data | array or table size | none for small mods; each added sequence costs 12 + 4 bytes INFO, each added file 16 bytes FAT. Raise the constant in a `#ifndef __MWERKS__` block for very large mods [I] |
| `nns_snd_main.c:261`, `GFL_USE_InitSound` | SDAT open | path `wb_sound_data.sdat`, `loadSymbols = 0` | the game never loads SYMB | other | none; SYMB is for tools only [V] |
| `A:arm9/nns_snd_arc.c` (end), `NNS_SndArcPlayerSetup` | player setup | `for i < 32` | player records 0..31 read | array or table size | 7 players exist; a mod needing more players edits PLAYER in the merge (limit 32) [V] |
| `A:arm9/gfl_use.c:219-221` | `SND_PLAYER_BGM 0`, `SND_PLAYER_FANFARE 6`, `SND_SE_SLOT_COUNT 5` | | player numbers used by code | other | new SE sequences must use player 1..5 in their SEQ record byte +9 [V: `GFL_USE_PlaySeInSlot` passes `slot + 1`] |
| `gfl_use.c`, `GFL_USE_GetSeSlot` | `GFL_USE_GetSeSlot` | `seqNo < 0x546` or `>= 0x81B` returns slot 1; else `param[5] - 1` | which of 5 SE slots a sequence uses | id check | for ids >= 2076 use `param[5] - 1` as well. Note the original range stops at 0x81A, so sequence 2075 (0x81B) already maps to slot 1 [V]. Replace the function (raw hook) or edit in `#ifndef __MWERKS__` |
| `A:arm9/field_sound_system.c:211-215`, `IsKnownSoundId` | `FIELD_SOUND_SYSTEM_IsKnownSoundId` | table `sTable_0209FDD0`, 0x26 u16 ids | while the BGM loader thread runs, only these 38 ids may start (`GFL_USE_IsSeqStartAllowed`, filter set at `FIELD_SOUND_SYSTEM_Create`) | array or table size | new effects are silent during a BGM load (a fraction of a second at a map change). Accept it or extend the table by a hook [V] |
| `field_sound_system.c`, `FIELD_SOUND_SYSTEM_ClaimSlot` | `FindSlotSound`, `IsSlotSoundTracked` (ov021) | tables `g_0209FD90` | which effects are tracked and paused (2 tracked slots, `slotSoundId[2]`) | array or table size | none unless a mod wants its effect paused with the menu; not a limit |
| `A:arm9/gfl_use.c:238` | `SEQ_PRELOADED_BGM` | 0x3F2 (1010) | BGM whose buffers are installed by hand | other | none; fixed original BGM [V] |
| `gfl_use.c` | `SndSlot.bgmId` | u16, 0xFFFF = none (`GFL_USE_PopSndSlot`) | BGM id storage | id storage width | BGM ids up to 65534 work [V] |
| `A:arm9/field_sound_system.c`, `FIELD_SOUND_SYSTEM_GetFieldBgmForZone` | | `0x3F4`, `0x3F5` (ride BGM), `0x45A` (trainer default) | fixed BGM ids | other | none; data, not limits |
| `field_sound_system.c`, `sZoneBgmOverrides[8]` at 0x0209FE94 | `FIELD_SOUND_SYSTEM_FindZoneBgmOverride` | 8 entries (flag, zone, bgm) | story BGM overrides per zone | array or table size | a mod that needs more overrides replaces the function or table [V]; not required for new music (zone data is enough) |
| `field_sound_system.c`, `sTrainerClassBgmTable` at 0x020A75A8 | `FIELD_SOUND_SYSTEM_GetTrainerClassBgm` | terminated by key 0x69; returns 0x45A default | battle BGM per trainer class | array or table size | mod trainer classes need a table replacement (hook) [V] |
| `A:arm9/zonedata.c:54-84`, `ZONEDATA_GetBgmId` | `struct ZoneHeader` | `u16 bgm[4]` at +0x0C, one per season | map music | id storage width | none: u16 holds any new sequence id [V]. White build remaps 0x409 to 0x40A (`zonedata.c`, `VERSION_WHITE`) |
| `A:arm9/bgm_info.c`, `BGM_INFO_Load` | archive 0x57 = `a/0/8/7`, member 0 | count is a `u8`, 3 bytes per entry (u16 bgmId, u8 issMode); file is 106 bytes = 1 + 35 x 3 [V] | per-BGM "ISS" (area sound) mode; unknown id gives mode 0 (`BGM_INFO_GetIssMode`) | id storage width | a new BGM works with no entry (mode 0 = default). Entries for new BGM: replace member 0, at most 255 entries |
| `A:arm9/pm_voice.c`, `PM_VOICE_GetCryIndex` | | `species < 1 && species > 649` (never true), `492/form 1` returns 650, else species | species to WAVARC index | asset index arithmetic | replace through the callback registered at `gfl_use.c:627` (`PM_VOICE_Init(1, &PM_VOICE_GetCryIndex, ...)`) with a species/form to WAVARC map. Hook key `arm9:0x02006C00` [V] |
| `pm_voice.c`, `PM_VOICE_PrepareCry` | | `ARC_TOOL_ReadMember(record, 194, species)`; `*volume = (s8)record[1]` | cry volume from `a/1/9/4`, indexed by species; 652 members of 4 bytes | asset index arithmetic | mod adds members past 651 (NARC merge already supports this). `ARC_TOOL_ReadMember` panics on an index past the count (`arc_tool.c`, `arc_tool_msg_member_bounds`) [V] |
| `pm_voice.c`, `PM_VOICE_PrepareCry` | | species 441 (Chatot), form 0 | recorded-voice special case | other | none |
| `pm_voice.c`, `PM_VOICE_LoadWave` | | `PM_VOICE_BUFFER_SIZE 0x5C30` | cry file size, no check | array or table size | cap cry files at 23,600 bytes in the merge tool, or enlarge the buffers in `PM_VOICE_AllocBuffers` (`GFL_HEAP_Alloc(heapId, 0x5C30, ...)`) and `_VoiceBuffer0` (static, 0x02140534; its size is [I]) |
| `pm_voice.c`, `PM_VOICE_StartChannels` | | `NNS_SndWaveOutStart(..., format 0, ...)` | cry sample format | other | format 0 is PCM8 [V: `nns_snd_waveout.c` switch case 0 shifts by 2]. All 650 shipped cries are single-wave, PCM8, non-individual SWAR [V, scratch parse]. A mod cry must be the same: one PCM8 wave, no loop |
| `pm_voice.c`, `PM_VOICE_LoadWave` | | `waveBytes = fileSize - waveOffset` | assumes the one wave is last in the file | other | same constraint as above |
| `pm_voice.c`, `PM_VOICE_SLOT_COUNT 3` | | 3 voices | simultaneous cries | array or table size | not a content limit |
| `A:arm9/gfl_use.c` (`GFL_USE_SetPlayerVolumes`) | | `playerNo < 5` | players 0..4 only | array or table size | none for content |
| `A:arm9/snd_bank.c`, `SND_AssignWaveArc` | | `waveArcLink[4]` per bank | 4 wave archives per bank | array or table size | SBNK format limit; mod banks use at most 4 wave archives [V] |
| `A:arm9/snd_bank.c`, `SND_GetWaveDataAddress` | | `offset < 0x2000000` means relative | wave pointer storage | other | pointer-width issue on the PC build (host addresses are not above 0x2000000 by rule); [I] see section 6 |
| `E:sdk/arm7/snd_seq.h:35-37` | | 16 players, 32 tracks, 16 channels | ARM7 model limits | array or table size | match the hardware; not a content limit |
| `E:engine/audio/sdat.c:119-140` | `bw_sdat_open` | counts from INFO | offline renderer | other | none; works for any count [V] |

Nothing else in the sound path compares an id against a literal count.
Searches: `0x546`, `0x81B`, `1350`, `2076`, `1986`, `650`, `649`, `0x3F2`
across `A:` found only the sites above (the 1350 hits in other files are
unrelated arithmetic).

## 3. Data and assets

- File: `wb_sound_data.sdat`, one file at the NitroFS root. The game opens it
  by path (`GFL_USE_InitSound`, `_SndArcPath`, `gfl_use.c:1565`). It is not a
  NARC, so the NARC member merge does not apply.
- Header (0x40 bytes): magic, file size at +8, then SYMB, INFO, FAT, FILE
  offset and size pairs at +0x10..+0x2C [V]. The game reads the first 0x30
  bytes only (`NNSi_SndArcLoadTables`), then seeks to the INFO and FAT
  offsets the header gives. Block order in the file is therefore free.
- INFO: eight record tables (SEQ, SEQARC, BANK, WAVARC, PLAYER, GROUP,
  PLAYER2, STRM), each `u32 count` plus `count` pointers (0 = absent slot).
  SEQ record 12 bytes: `u32 fileId, u16 bank, u8 volume, u8 channelPressure,
  u8 priority, u8 player, 2 pad` (`E:engine/audio/sdat.c:215-226`, and the
  game's `SndArcPlayerEntry` in `nns_snd_arcplayer.c`). BANK record: `u32
  fileId, u16 waveArc[4]` (0xFFFF = none). WAVARC record: `u32` with bits 0-23
  the file id and bit 24 the "load waves individually" flag
  (`NNSSndArcWaveArcInfo`, `nns_snd_arc_types.h`). PLAYER: `u8 seqMax, u8,
  u16 channelMask, u32 heapSize`. GROUP: `u32 count` then 8-byte items (type,
  flag, itemNo).
- FAT: 16 bytes per file (absolute offset, size, two runtime words), file ids
  in three runs (SSEQ, SBNK, SWAR). Offsets are absolute in the SDAT file
  (`NNS_SndArcReadFile`, `nns_snd_arc.c:332-377`: seek to `entry.offset`).
  Only 1497 of 1986 bank slots and 940 of 2076 sequence slots have records;
  empty slots are 0 pointers. Several slots share one file [V: 940
  populated sequences, 886 sequence files].
- Adding past the end is enough for sequences, banks, wave archives: append
  records, grow the three counts and the INFO/FAT blocks, append the new
  files. Existing ids and file ids keep their numbers if new files are
  appended after the existing 3874 (so existing records stay valid) [V by
  construction, [I] not run].
- File-id order caveat: the FAT is "all SSEQ, then all SBNK, then all SWAR"
  only by convention of the build tool [V, aletheia scan]. No code relies on
  it (lookups go through INFO fileId). So appended files of any kind may
  follow the last existing id [I, nothing in `A:` reads the file class from
  the position].
- Cries: WAVARC slot N (N = 1..649) is the cry of species N, WAVARC 650 is
  species 492 form 1 [V: `PM_VOICE_GetCryIndex`; naming from ASSETS.md
  "PV###" / "PMWB_###"]. BANK slot N is in lockstep but is not used by the
  cry path (cries load only the SWAR file via the WAVARC record) [V:
  `PM_VOICE_LoadWave` uses only `NNS_SndArcGetWaveArcInfo`).
  Per-species volume: `a/1/9/4` (archive id 194, `pm_voice.c`), 652 members of
  4 bytes; the second u16's low byte is the volume (97..127 in the shipped
  data per `pm_voice.c` comments and ASSETS.md line 8265).
- Music: BGM = SEQ id. Zone BGM ids are in the zone header (section 2).
  BGM ids shipped are 1000..1178 for `SEQ_BGM_*` [from kenon's
  `game-data-map.md:333`, [I] for the exact range].

## 4. Save data

[V] Nothing sound-related is stored per id in the save in code read here:

- Cries come from species, form and the optional recorded voice
  (`PerapVoice`, Chatot only).
- BGM state (`SndSlot`, `FieldSoundSystem`) is runtime memory.
- Sound ids appear in scripts and zone data, not in save structures.

I did not search the save blocks exhaustively for stored BGM ids (for
example a remembered field BGM); [not verified]. If a mod stores its own
ids, they go to `sdk/mod_savedata.c` as names, not numbers (see section 5).

## 5. What the mod author supplies, what the port unlocks, and the merge design

Mod author supplies: SSEQ/SBNK/SWAR files (or source converted by a tool),
names, and for cries one single-wave PCM8 SWAR under 23,600 bytes per
species/form plus a volume byte per species in `a/1/9/4` members.

Port must unlock: the SDAT merge, the species-to-cry map, the SE slot map
(section 2), and optionally the heap constant and the BGM-load filter.

### Merge design (start from the other investigation's finding)

Counts are read from the file and no code table depends on them, so the merge
is a data problem only.

1. **Mod layout.** A mod ships `snd/` with one manifest file (names, kinds,
   priorities) and the raw sub-files:
   `snd/seq/<name>.sseq`, `snd/bank/<name>.sbnk`, `snd/wave/<name>.swar`. The
   manifest lists for each sequence: name, bank name, volume, priority,
   player (1..5 for effects, 0 for BGM, 6 for fanfare), and for each bank its
   wave archive names (up to four). Optional: PLAYER overrides.
2. **Merge at load, in memory, no 51 MB copy.** Build a virtual file in
   `sdk/vfs.c` for the SDAT path, as `vfs_narc.c` does for NARCs. Layout:
   original bytes 0..EOF unchanged, then appended: new files (4-byte aligned),
   new INFO, new FAT, new SYMB, then a patched 0x40 header (copy of the
   original with the INFO/FAT/SYMB offsets and sizes and the file size
   changed). Because the game and `bw_sdat_open` locate blocks through the
   header only, the old FILE block stays where it is and old FAT offsets stay
   valid (the new FAT keeps every old entry identical and adds entries
   after id 3873). This needs the FS shim to serve the base file from disk
   plus an in-memory tail (an "overlay file" with two segments), not a full
   memory image. A plain memory image is possible but costs 51 MB of RAM plus
   the mod files [I]. Overlay segments are a new feature of `sdk/fs.c`
   (`open_rel_window` at `fs.c:638` only knows host files or whole memory
   images today [V]).
3. **Numbering.** New SEQ ids start at the original table size (2076) and
   run on; new BANK and WAVARC ids start at 1986. Ids are assigned in mod load
   order and name order inside a mod, so they are stable for a fixed mod list
   but change when the list changes. Never reuse the empty original slots (they
   may be used by a later patch of the base game; and holes would make
   numbering depend on the base file).
   For stability across list changes, keep a persistent name-to-id registry in
   per-save mod storage (`sdk/mod_savedata.c`) only if some saved data holds
   sound ids; section 4 found none, so assigned-at-load is enough [I].
4. **Naming.** A mod names a sound `modname:soundname`. The merge records the
   name in the new SYMB block (so tools and `bw_sdat_find`,
   `E:engine/audio/sdat.c:182`, find it) and in an eidos table for rules and
   scripts: `bw_snd_id("modname:soundname")` returns the id. The game never
   reads SYMB [V], so SYMB has no runtime cost. Data a mod ships that holds a
   sound id (zone BGM u16, trainer BGM) is written by the mod as a name in a
   text sidecar and patched to the number at merge time; or the mod uses the
   raw number of a base-game sound.
5. **Cries.** A mod declares `cry species=<id> form=<n> wave=<name> volume=<v>`.
   The merge appends the SWAR as a WAVARC (non-individual flag) and a BANK
   record for it (lockstep convention; not needed by the cry path [V]); builds
   an eidos map `(species, form) -> WAVARC id` and a member of `a/1/9/4` for
   the volume. The port replaces `PM_VOICE_GetCryIndex` by an override that
   consults the map first and falls back to the original rule. Validation at
   merge time: one wave, PCM8, no loop, file size <= 23,600.
6. **Effects.** Validation at merge time: bank + wave archives + sequence
   (each plus 0x20 bytes of heap header per block, `NNSi_SndArcLoadFile`) must
   fit the player heap (10200 bytes for players 1, 2, 3 and 5; 2000 for
   player 4). The merge tool reports the budget. A mod may raise a PLAYER
   record's heap size, paid from the sound heap at `NNS_SndArcPlayerSetup`
   [V]. Add the SE slot override (section 2) so new effects get distinct slots.
7. **Sound heap.** Report the INFO/FAT growth against the 612,048 byte heap.
   A mod that adds thousands of files may need the constant at
   `gfl_use.c:1563` raised on the PC build [I].
8. **Several mods.** Two mods adding sounds both append; the later one's ids
   come after. A mod that replaces an original sound (same name as a base
   SYMB name) swaps the FAT entry of that sound's file to a new appended file;
   the sequence id is unchanged.

## 6. Open questions

- kenon's SDAT output: whether the game boots with it, and whether it keeps
  the old FAT offsets valid (the coordinator's report). Look at
  `kenon/core/src/sdat/container.rs` and run the port with its output.
- Size of the static `_VoiceBuffer0` (0x02140534): 0x5C30 is inferred from
  `PM_VOICE_AllocBuffers`; read the layout map to confirm.
- `SND_GetWaveDataAddress` treats offsets below 0x2000000 as relative. On
  the PC build with 32-bit or 64-bit host pointers (`docs/pointer-width.md`),
  check that wave addresses stored by `SND_SetWaveDataAddress` cannot fall
  below that value or lose bits. Not verified here.
- Whether any save block stores a BGM id or sound id (section 4). Search
  `A:` save structures for fields set from `GFL_USE_GetBgmNo`.
- Battle BGM selection by species and trainer (`ENCEFF_SelectEffectAndBgm`,
  `ENCEFF_SelectEffectAndBgmForTrainer`, ov-file name not recorded here):
  not read; check whether it indexes a table by species or trainer id.
- `FindSlotSound` tables in ov021 (`g_0209FD90`): not read in full.
- Archive id to path: `a/0/8/7` for archive 0x57 is from ASSETS.md line 8158;
  `a/1/9/4` for 194 is from the `pm_voice.c` comment. I could not read the
  archive path table from the decrypted image (the table address lies past
  the end of the ARM9 file I opened).
- The "about 1 second" effect length is arithmetic from the heap size, not a
  measurement of shipped effects.
