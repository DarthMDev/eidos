# Limits survey: text banks

Scope: the message bank format, how the game finds a name from an id, what
happens past a bank's line count, and a port-side design for a mod that
adds lines. Paths starting `src/game/` are in the aletheia tree. Paths
starting `sdk/` are in eidos. Marks: [V] verified in code or data, [I]
inferred and not verified.

## 1. Summary

Original counts (lines per bank, read from the extracted data):

| Kind | Name bank (member of `a/0/0/2`) | Lines |
|---|---|---|
| species | 70 (0x46) | 652 [V] |
| items | 54 (0x36) | 627 [V] |
| moves | 203 (0xcb) | 560 [V] |
| abilities | 182 (0xb6) | 165 [V] |
| trainers | 190 (0xbe) | 616 [V] |
| trainer classes | 191 (0xbf) | 105 [V] |
| place names | 89 (0x59) | 117 [V] |
| types | 199 (0xc7) | 17 [V] |

The id is the line index. There is no second lookup (section 2.2). A bank
member is a plain structure with no per-bank limit on lines except a u16
count (65535 lines), so appending lines is easy to unlock. The hard parts
are:

1. The lines are in many parallel banks per kind (section 3.1). A new
   species needs a line in each of about 20 banks, a new item in 4, a new
   move in 3, a new ability in 3. If one bank stays short, some readers
   return an empty string and others read garbage (obstacle 2).
2. Two readers have no bounds check: `MSGDATA_AllocString` and
   `MSGDATA_ReadUnits` (`src/game/arm9/msgdata.c:403`, `:455`). Only
   `MSGDATA_ReadString` checks the index (`:293`) and returns an empty
   string. The species name path in `poke_tool.c` uses `ReadUnits`. A
   species past a short bank reads string bytes as a descriptor.
3. The species name bank holds two fixed lines at 650 and 651, both "Egg"
   [V]. Code uses 650 as the egg and bad-egg species id and reads line 651
   by number (`src/game/arm9/poke_tool.c:2887`, `:2896`, `:3011`, `:3018`).
   The first usable new species id is 652, so a new species id and its text
   line are the same number only from 652 up.
4. A few hard bounds on ids that feed text: item name reads clamp to 626
   (`file_util.c:430`, `:458`), place name id is a 10 bit field and the
   place name check is `>= 0x75` (`zonedata.c:66`, `field_place_name.c:131`,
   `:763`), trainer class is cast to u8 (`btl_msg.c:283`). The pokedex list
   caches exactly 649 rows (`ov209/zukan_list.c:311`, `:1033`).
5. Text is bounded by string buffers. `STRBUF_SetChars` silently does
   nothing when the string is longer than the buffer
   (`src/game/arm9/strbuf.c:436`). The mon nickname field holds 10 chars
   (`poke_tool.c:3408`). The longest species name in the game is 10 chars,
   so a species name of 11 or more chars is cut when it becomes the default
   nickname [V for the copy, I for the effect].

Difficulty: low for the data side (the existing member merge in
`sdk/vfs_narc.c` already adds members and the format is simple). Medium for
the guard fixes (about 3 small `#ifndef __MWERKS__` edits). The bigger work
is the per-kind parallel bank lists and the easy chat system, which is
better left unchanged for mod content.

## 2. Format and lookup

### 2.1 The bank format [V]

One bank is one member of NARC `a/0/0/2` (archive id 2, 288 members) or
`a/0/0/3` (archive id 3, 472 members). The id to path map comes from the
archive path table in the ARM9 data: id 2 is `a/0/0/2`, id 3 is
`a/0/0/3`, id 0x5a is `a/0/9/0`, id 0x5b is `a/0/9/1`, id 0x17 is
`a/0/2/3` (read from the decrypted ARM9 table, checked against the
`ARC_TOOL_SetTable` note in `src/game/arm9/arc_tool.c:242`).

Header, as the game reads it (`msgdata.c:61` and `:70`, `:529`):

```
0x00 u16 langCount      1 in every member of both archives [V, scanned]
0x02 u16 count          lines in the bank
0x04 u32 size           section size in bytes
0x08 u32 reserved       0
0x0C u32 langOff[langCount]    16 when langCount is 1
```

Section (starts at `langOff[lang]`, `msgdata.c:560`):

```
+0  u32 size                    same value as header size [V]
+4  entry[count]                8 bytes each
      u32 offset                from the section start
      u16 length                code units, INCLUDING the end code
      u16 flags                 not read by the game [V grep]
+4+8*count  strings             packed back to back, in index order
```

Checks on real members: member 70 has header size 16132, section first
word 16132, file size 16148, first string offset 5220 which is
`4 + 8*652` [V]. The flags word is constant in a bank (60 in members 70
and 190, 96 in 54, 84 in 203) and is 0 or 60 in the trainer name bank [V].
No game code reads it, so a new line can copy the bank's common value.

Cipher, the whole of it (`msgdata.c:375`):

```
key = (u16)((lineIndex + 3) * 0x2983)
for each unit: unit ^= key; key = rotl16(key, 3)
stop at the unit equal to the end code (0xFFFF)
```

The key depends only on the line index, not on the offset. So a line
keeps its ciphertext when other lines move. Control units after decrypt:
0xFFFF end, 0xFFFE line break, 0xF000 then kind, argument count and that
many argument words (the tag form). Other units are Unicode code points
(accented letters such as U+00E9 are one unit; U+2642/U+2640 are 0x246D and
0x246E) [V in `tools/assets/text.py` and in dumped strings].

The language: `MSGDATA_GetLanguageIndex` returns the config language if it
is below `langCount`, else 0 (`msgdata.c:625`). All members have one
section, so the index is always 0 [V]. The ROM is single language. Other
languages exist only as separate members (for example the per language
pokedex banks, section 3.1), not as extra sections.

Packed trainer names [V]: 569 of 616 lines in bank 190 and 20 of 21 in
bank 176 start with unit 0xF100. The rest of such a line is a 9 bit packed
run (code 0x1FF ends it; codes 0x100 to 0x131 go through a table;
`wordset.c:1084`). Only `WORDSET_AppendUnpackedString` unpacks it
(`wordset.c:1039`, `:1084`), and it copies a plain string unchanged. So a
mod can write plain text in these banks as long as the reader goes through
the word set (the usual path). One raw reader exists: the preset mail
writer name reads line 0x3b of bank 190 with `MSGDATA_AllocString` and
`STRBUF_ToChars` (`src/game/ov020/event_mapchange.c:2432` to `:2437`).

### 2.2 How the game finds a name [V]

Species: `WORDSET_SetSlotSpeciesName(words, slot, speciesId)` reads line
`speciesId` of the species bank (`wordset.c:433`; bank opened once by
`WORDSET_LoadSpeciesNameBank`, member 0x46, `wordset.c:883`). The bank is
open as a stream (resident flag 0), so each read seeks the archive.
Poke tool reads the same bank with `MSGDATA_ReadUnits(bank, species, buf,
0x20)` for default nicknames (`poke_tool.c:660`, `:775`, `:973`, `:3587`,
`:3696`).

Items: `WORDSET_SetSlotFromMsg36(words, slot, itemId)` is
`WORDSET_SetSlotFromMessage(words, slot, 0x36, itemId)` (`wordset.c:476`).
Callers pass the item id unchanged, for example
`ov021/scrcmd_wordset_warp.c:222` and `ov093/btl_msg.c:492`. Descriptions
are bank 0x35 through `FILE_UTIL_ReadStringAlt` (`file_util.c:454`).

Moves: `WORDSET_SetSlotFromMsgCB` (bank 0xcb, `wordset.c:471`), move id
passed unchanged (`btl_msg.c:489`). Abilities: `WORDSET_SetSlotFromMsgB6`
(bank 0xb6, `wordset.c:504`), ability id unchanged (`btl_msg.c:486`).
Trainer names: `WORDSET_SetSlotFromMsgBE` with the trainer id
(`btl_msg.c:297`, `wordset.c:688`) and `TR_TOOL_LoadTrainerInfo`
(`tr_tool.c:146` to `:148`). Trainer class: bank 0xbf with the class cast
to u8 (`btl_msg.c:283`, `wordset.c:673`).

So name = bank member + line index = id. [V]

`WORDSET_SetSlotFromMessage` opens the bank for each call
(`MSGDATA_CreateFromArchive(0, 2, messageId, heap)`, `wordset.c:414`), reads
the line into the word set scratch string, and deletes the bank.

### 2.3 Behaviour past the count [V]

| Reader | Past the count |
|---|---|
| `MSGDATA_ReadString` (`msgdata.c:288`) | Check at `:293`: clears the destination and returns. Safe. |
| `MSGDATA_AllocString` (`msgdata.c:403`) | No check. Resident: reads the descriptor from `base + 4 + 8*index`, outside the table. Streamed: seeks and reads 8 bytes of whatever is there. The length then sizes `STRBUF_Create`. Unsafe. |
| `MSGDATA_ReadUnits` (`msgdata.c:455`) | No check, same descriptor read. `maxLen` caps the copy but not the offset. Unsafe. |
| `MSGDATA_GetMessageCount` (`msgdata.c:439`) | Returns the header u16. Used as a guard in `WORDSET_SetSlotFromMsgE7` (`wordset.c:595`) and `WORDSET_SetSlotFromKeyedMessage` (`:632`), as a loop bound in `ov067/msgsearch.c:83` and `ov110/pmsi_search.c:238`. These adapt to a longer bank for free. |

Callers of the unchecked readers: `ReadUnits` in `poke_tool.c` (list above)
and `ZUKAN_INFO` uses `AllocString` with the species number
(`ov206/zukan_info.c:914`, `:964`). Callers of `WORDSET_SetSlotFromMessage`
all land on `ReadString`, so they print an empty name for a line past the
end rather than crash.

### 2.4 Is appending lines enough? [V]

Yes for the bank file itself. The reader uses the header count and the
descriptor table, nothing else. The streamed reader seeks
`origin + langOff + offset` and reads `2 * length` bytes
(`msgdata.c:318` to `:335`); `origin` comes from the archive handle
(`ARC_TOOL_GetHandleMemberOffset`, `msgdata.c:179`), so it follows a merged
NARC that has a different layout. The resident reader allocates
`file->size` bytes (`msgdata.c:186`), so a bigger bank asks for a bigger
heap block. Whether the port's heaps are large enough for the largest
resident bank (the pokedex entries, about 131 KB, section 3.1) is [I] and
not checked.

A bank edit also needs, for each kind, every parallel bank to reach the
same count (section 3.1), or the guard in section 5 must make short banks
safe.

## 3. Site table

Class codes: ID = id check, TAB = array or table size, SAVE = save layout,
W = id storage width, IDX = asset index arithmetic, TXT = text,
OTH = other.

| # | File:line | Function | Literal or size | What it guards | Class | Change needed |
|---|---|---|---|---|---|---|
| 1 | `src/game/arm9/msgdata.c:293` | `MSGDATA_ReadString` | `index >= count` | Empty string past the end | TXT | None. Good behaviour. |
| 2 | `msgdata.c:403` | `MSGDATA_AllocString` | none | Reads descriptor out of table | TXT | Add `if (index >= count)` guard returning an empty string, in `#ifndef __MWERKS__`. |
| 3 | `msgdata.c:455` | `MSGDATA_ReadUnits` | none | Same | TXT | Add the same guard: write `{EOS}` and return. |
| 4 | `msgdata.c:61`, `:70` | `MsgEntry`, `MsgFile` | `u16 count`, `u16 length`, `u32 offset` | At most 65535 lines, 65535 units per line | TXT | None needed. Note as the format limit. |
| 5 | `src/game/arm9/strbuf.c:436` | `STRBUF_SetChars` | `charCount > capacity` returns | A line longer than the buffer leaves the old text, no error | TXT | None; mod lines should respect the buffers in rows 6 to 9. A debug log in a PC only branch helps authors. |
| 6 | `src/game/arm9/wordset.c:289`, `:299` | `WORDSET_Create` | `textCapacity` per caller | Slot and scratch size: 0x20 (`gf_font.c:1282`), 0x40 (`ov010/script_work.c:189`), 200 (`ov100/ircbattlematch.c:1109`) | TXT | None; units of text a name or description may use. |
| 7 | `src/game/ov010/script_work.c:190`, `:191` | script work | `STRBUF_Create(0x500)` x2 | Longest field script line (largest in `a/0/0/3` is 813 units, member 462 line 151) | TXT | None. |
| 8 | `src/game/ov093/btl_msg.c:250` | `BTL_MSG_Init` | `STRBUF_Create(0x100)`, 8 banks via `sBtlMsgBankFileIds` | Battle text length and bank count | TXT / TAB | None for names. The 8 banks are fixed (battle text only). |
| 9 | `src/game/arm9/poke_tool.c:3408` to `:3415`, `:3018` to `:3020` | `POKE_TOOL_SetBoxValue` / `GetBoxValue` | nickname 11 units | Nickname holds 10 chars + end; `WORDSET_CopyString` forces the last unit to the end code (`wordset.c:986`) | SAVE | Mod species names should stay at 10 chars or the default nickname is cut. No code change. |
| 10 | `src/game/arm9/poke_tool.c:2887`, `:2896` | `POKE_TOOL_GetBoxValue` | `650` | Species returned for an egg or checksum failure | W / ID | Species id 650 and 651 stay reserved. First new id is 652. (Owned by the species survey.) |
| 11 | `poke_tool.c:3011`, `:3018` | nickname read | line `651` | Name shown for a failed mon: reads line 651 of the species bank by number | TXT | Keep lines 650 and 651 as they are when a mod appends. |
| 12 | `src/game/arm9/file_util.c:430`, `:458` | `FILE_UTIL_ReadString`, `FILE_UTIL_ReadStringAlt` | `entry > 0x272` | Item name (bank 0x36) and description (bank 0x35) reads fall back to line 0 for any id above 626 | ID | Raise the bound with the item count. In `#ifndef __MWERKS__` use a runtime count (`MSGDATA_GetMessageCount` of the bank). |
| 13 | `src/game/arm9/poke_tool.c:2903`, `:3246` | held item get / set | `> 626` | Held item above 626 reads as 0 | ID | Item survey. Listed because the name reader depends on it. |
| 14 | `src/game/ov021/scrcmd_proc_fld.c:1303`, `:1474`; `ov021/sodateya.c:169` | TM item checks | `<= 0x272`, `TM_ITEM_LAST` | TM and machine item ranges | ID | Item survey. |
| 15 | `src/game/arm9/zonedata.c:66` | `ZoneHeader` | `u16 placeNameId : 10` | Place name id is 10 bits (max 1023) | W | Fine up to 1023 places. Larger needs a wider field and a new layout. |
| 16 | `src/game/ov021/field_place_name.c:131`, `:763` | place name popup | `FIELD_PLACE_NAME_PLACE_COUNT 0x75` | Place name id `>= 117` becomes 0 (no popup) | ID | Raise or replace with `MSGDATA_GetMessageCount` of bank 0x59. |
| 17 | `src/game/arm9/wordset.c:716` to `:719` | `WORDSET_SetSlotFromMsgA0` | `>= 0x30` | Bank 0xa0 (48 lines), clamp | ID | Fixed small bank; not id data. Leave. |
| 18 | `wordset.c:733` to `:737` | `WORDSET_SetSlotFromMsg00` | `> 0x91`; u8 argument | Bank 0 (146 lines) | ID / W | Fixed small bank. Leave. |
| 19 | `wordset.c:765` to `:768` | `WORDSET_SetSlotMonth` | `> 0xC` | 12 months | ID | Leave. |
| 20 | `src/game/ov093/btl_msg.c:283` | `BTL_MSG_SetClientValue` | `(u8)` cast | Trainer class passed as u8, so class ids stop at 255 | W | Wider cast only if classes pass 255. Bank 0xbf has 105 lines. |
| 21 | `src/game/arm9/tpoke_data.c:304` to `:318` | `TPOKE_DATA_ReadRecordString` | bank 0xbd (1762 lines), u16 `rec[0]` offset | Trainer talk lines: line index is `recordOffset >> 2` in a/0/9/0 member 0 | IDX | Data side (section 4.3). `rec[0]` is u16, so the record table must stay under 64 KB (16384 records). |
| 22 | `src/game/arm9/pms_word.c:175` | `PMS_WORD_SplitWordId` | `wordId &= 0x7ff` | Easy chat word id is 11 bits, 13 banks summed from `g_pmsWordBankSizes` | W / TAB | Do not grow these banks (section 3.3). |
| 23 | `pms_word.c:105`, `:113` | `PMS_WORD_Create`/`Delete` | 13 banks | Easy chat bank count | TAB | Leave. |
| 24 | `src/game/ov110/pmsi_search.c:74` to `:78`, `:260` | easy chat search | 27 initials, `PMSI_SEARCH_RESULT_MAX 264`, `u16 words[265]` | Per initial result cap; stack arrays | TAB | Leave. A letter list longer than 264 overflows the stack array. |
| 25 | `src/game/ov067/msgsearch.c:83` | `MSGSEARCH_Search` | count read at run time | Search over banks | TXT | Adapts. |
| 26 | `src/game/ov209/zukan_list.c:311`, `:1033`, `:1069` | pokedex list | `speciesText[649]`, `itemCount=649` | List rows, one string per species | TAB | Pokedex survey. Rows past 649 need the array and loops widened. |
| 27 | `src/game/ov206/zukan_info.c:781`, `:806` | pokedex info | `species > 493`, `0x1ed` | Foreign language pages exist only for species 0 to 493 | ID | Not a limit for mods: new species use the main page only. |
| 28 | `src/game/ov206/zukan_info.c:1000` to `:1005` | `ZUKAN_INFO_DrawPage` | `0x1e7`, `0x1ec` to `0x1ee`, `0x1ef` | Height and weight lines for alternate forms are lines 494 and 495 in the 496 line banks | IDX | Pokedex survey. |
| 29 | `src/game/ov010/scrcmd_work.c:589`, `ov021/scrcmd_msgwin.c:1078` | script message load | `msgFileId == 0x400` | Sentinel for "the zone's own bank" in `a/0/0/3` | OTH | Member index 0x400 of `a/0/0/3` is shadowed by the sentinel as a script operand. Avoid new members at 1024 only if a script names them by that id. |
| 30 | `src/game/arm9/zonedata.c:61` | `ZoneHeader.msgArcId` | u16 | Zone text member of `a/0/0/3` | W | Fine to 65535. |

Rows 2 and 3 are the only behaviour fixes the text side needs in game
code. Rows 12 and 16 are id checks that sit in front of a name read.

## 3.1 Parallel banks per kind

Counts were read from the extracted data. The "role" column is inferred
from the first lines and from the call sites above. Member numbers are in
`a/0/0/2`.

Species (id = line; 650 and 651 are the two "Egg" lines):

| Member | Lines | Role | Evidence |
|---|---|---|---|
| 70 (0x46) | 652 | names | `wordset.c:883` [V] |
| 281 (0x119) | 652 | names with article tag | `WORDSET_SetSlotFromMsg119` (`wordset.c:449`) [V] |
| 284 (0x11c) | 652 | upper case names, easy chat word list | first entry of `g_pmsWordBankFileIds` [V] |
| 235, 236 (0xeb, 0xec) | 650 | pokedex entry text, two versions | content [I] |
| 237 to 243 | 494 | pokedex entry text, other languages | content [V] |
| 253 to 259 (0xfd to 0x103) | 494 | names, other languages | content [V] |
| 260 (0x104) | 650 | pokedex category line | `ov208/zukan_detail_info.c:461` [V] |
| 261 to 267 | 494 | category, other languages | [V] |
| 244, 245, 268 (0xf4, 0xf5, 0x10c) | 727 | pokedex data banks used with form lines | `zukan_detail_info.c:462`, `:463` [V], role [I] |
| 246 to 252, 269 to 275 | 496 | height and weight, other languages | [V] |

Items: 54 (0x36) names, 53 (0x35) descriptions, 279 (0x117) names with
article, 280 (0x118) plural names, all 627 lines [V].

Moves: 203 (0xcb) names, 202 (0xca) descriptions, 286 (0x11e) upper case
names (easy chat), all 560 lines [V].

Abilities: 182 (0xb6) names, 183 (0xb7) descriptions, 285 (0x11d) upper
case names (easy chat), all 165 lines [V].

Types: 199 (0xc7) 17 lines; upper case 287 (0x11f) 17 lines [V by count,
role from `g_pmsWordBankFileIds`].

Trainers: 190 (0xbe) names, 616 lines; 189 (0xbd) talk lines, 1762;
191 (0xbf) class names, 105; 283 (0x11b) class names with article, 105
[V].

Places: 89 (0x59) names, 117 lines [V]. Banks 90 to 92 (place name
variants) are used with the summary screen (`ov131/p_sta_info.c:198` to
`:201`); their counts were not checked.

A kind's mod entry must therefore name the whole group. A simple rule that
avoids the unsafe readers is for the port to pad every bank of a group
to the largest line count among them (an empty string for any line the mod
did not supply). Section 5 describes this.

## 3.2 Archive 3 (`a/0/0/3`)

The 472 members are the field script texts (zone dialogue, shops, events).
A script names a member by the zone header `msgArcId` or by an operand,
and reads line `msgId` (`script_sys.c:642`, `scrcmd_msgwin.c:1078`). The
format is the same. A mod that adds scripts supplies its own members
(index past 471) and its zone header refers to them, so text is a pure
member add with no line merge. Sentinel: operand value 0x400 means the
zone's own bank (row 29).

## 3.3 Easy chat and search tables

Easy chat (the Pokemon word and phrase system) stores 11 bit word ids. The
id is the sum of the sizes of the earlier banks plus the line
(`pms_word.c:151`, `:170`). The size table is in ARM9 data at
`0x0209F792`: 13 u16, values 652, 560, 17, 165, 44, 38, 48, 103, 47, 32,
26, 10, 61 (sum 1803, so 244 ids are free below the 2047 cap) [V, read
from the decrypted ARM9]. The file id table is at `0x0209F7AC` and starts
0x11c, 0x11e, 0x11f, 0x11d [V]. Saved sentences keep these ids, so
growing the first bank would shift every later id and break existing
saves. Recommendation: do not add mod content to the easy chat banks.
(Row 22 to 24.) A mod that wants its species in easy chat needs a design
of its own (a fourteenth bank with an appended id range) and is out of
scope here.

No fixed species count was found in the Wi-Fi trade search code
(`ov135/*.c`): the GTS species list loads bank 0x46 (`worldtrade.c:432`)
and no 649 / 650 / 493 literal appears in `ov135` [V by grep].

## 4. Data and assets

### 4.1 What holds the strings

Banks are members of `a/0/0/2` and `a/0/0/3` (section 2.1). Both are
plain NARCs. The member format has no checksum and no global index. The
merge in `sdk/vfs_narc.c` already builds a NARC with replaced or added
members (`bw_vfs_build_merged`, `vfs_narc.c:455`), so adding a whole new
bank is already unlocked. Replacing a bank to add one line, however, makes
every mod that touches that bank conflict (`docs/mods.md`, member
overrides). Hence the line level merge in section 6.

### 4.2 Tools that exist

aletheia `tools/assets/text.py`: reads a member (`MessageFile`, header and
descriptor table), decodes one line (`decode`), re-encodes (`encode`, same
transform), renders a line to display text (`render`, tags shown as
`{F000:KIND:args}`, line break as `\n`), and a round trip check that
rebuilds a member byte for byte. It has no parser from text back to
units, no bank builder for new content, no 9 bit packer, and no
`check9bit` command (the module text and `docs/ASSETS.md` section on the
9 bit hypothesis are out of date; the 0xF100 marker is real in banks 190
and 176, see 2.1). I did not run the round trip command, I only used the
decoder to read counts, strings and the header words.

eidos has a runtime encoder for short mod messages: `bw_mod_message_encode`
(`sdk/mod_message.c:239`). It maps UTF-8 to one unit per code point, `\n`
to 0xFFFE, tab to space, and anything the font lacks to `?`
(`encode_char`, `mod_message.c:224`). It has no tag syntax and no cipher.
A bank builder needs the tag syntax and the cipher on top.

### 4.3 Trainer talk table (not a bank edit alone)

Trainer talk lines are found through two tables, not by trainer id
(`tpoke_data.c:304` to `:318`):

- `a/0/9/1` member 0: one u16 per trainer, the byte offset of that
  trainer's first record. 616 entries (1232 bytes) [V].
- `a/0/9/0` member 0: records of 4 bytes (trainer id u16, kind u16). 1762
  records (7048 bytes) [V]. The line in bank 189 is the record's ordinal.
  The scan runs from the trainer's offset to the end of the member and
  matches id and kind.

For a new trainer the mod appends records and lines in step and replaces
member 0 of both archives (members are not mergeable at record level).
The offset table must reach the new trainer id. Past its end the read
(`ARC_TOOL_ReadMemberRange(..., key*2, 2)`) is out of range. Owned by the
trainer survey; noted because it carries a text index.

## 5. Save data

Text banks hold no per-id save data. The save stores ids (species, item,
move, ability u16/u8 as the other surveys list), and the names come from
the banks at run time. Text related save fields:

| Field | Size | Where | Note |
|---|---|---|---|
| mon nickname | 11 units | `poke_tool.c:3408` | 10 chars + end code [V] |
| original trainer name | 8 units | `poke_tool.c:3442`, `:3046` | 7 chars + end code [V] |
| easy chat sentences | 20 sentences x 8 bytes, word id 11 bit | `pms_word.c:198` to `:240` (`GetSaveSize` returns 0xA8) | ids are prefix sums, see 3.3 |
| mail text | game's own | not examined | [I] |

Nothing here needs the eidos per-save mod storage. A mod that keeps its
own name for a new species needs none either: the name is a bank line. The
only candidate is a custom default nickname table, and it is not needed.

If a mod renames a base species by replacing a line, saved mons that kept
the default nickname show the new name only when the game derives the name
from the species. The game compares the nickname with the species name
(`poke_tool.c:3587` to `:3588`) to decide whether the mon has a nickname
[V by reading, effect I]. A rename therefore flips `hasNickname` for mons
that kept the old default. Mods should add lines, not rename existing
ones, or accept this.

## 6. What the mod author supplies, and the port design

### 6.1 Mod supplies

For each new id, one text per bank in the kind's group, as plain text
(UTF-8). Lines are addressed by bank alias and line index. The author
never touches the cipher, offsets, header or count.

### 6.2 Port unlocks

1. Guards in game code, `#ifndef __MWERKS__` only (rows 2, 3, 12, 16).
2. A text line merge in the member layer (below).
3. Alias names so authors write `species_name` instead of member 70.

### 6.3 Member level text merge

Add a second kind of file to a mod's archive directory. In
`sdk/vfs_narc.c` the name rule (`member_index`) is `<index>` or
`<index>.<ext>`. Reserve the extension `msg` (a file `70.msg` in
`mods/<m>/a/0/0/2/`) to mean "text lines to apply to member 70", not "the
member". Order, per member index, in load order:

1. The base member, or the latest whole member replacement (a file named
   `70` or `70.bin`) from a mod that loads at or before the patch.
2. Every `.msg` patch from mods that load after that point, in order.

A whole replacement from a later mod discards earlier patches (same rule as
today: the later layer wins). This fits the collect step: today
`collect_one` records one override per index. It would keep a list per
index (kind: whole or patch) and `bw_vfs_build_merged` would run the chain
before `bw_narc_pack`.

Patch file format (UTF-8, one line per entry, easy to diff):

```
# comment
652 = Newmon
653 = Other {F000:0101:0000} name
655 = first line\nsecond line
```

- `index = text`. Index in decimal. Lines not mentioned stay as they are.
- `\n` is the line break unit 0xFFFE. `\\` is a backslash.
- `{F000:KIND:A,B}` writes the tag exactly as `text.py` renders it
  (hex, 4 digits; the argument count is derived). `{XXXX}` writes one raw
  unit in hex. This makes dump, edit and merge a round trip.
- Plain text is one unit per code point (U+0000 to U+FFFF). A code point
  above U+FFFF is an error. The port logs a warning for a character the
  game font lacks (`bw_mod_message_font_has`, `mod_message.h:211`) and
  writes it anyway, because the bank is not the place to replace it with
  `?`.

Build step for a bank, given the current member bytes and the lines:

1. Parse the header. Take `count`, the descriptor table and the string
   area. Keep every untouched line as raw ciphertext (the key depends on
   its index only, which does not change).
2. Apply the patch. A line index at or above `count` extends the bank. An
   index gap is filled with an empty line (one unit, 0xFFFF encrypted with
   that line's key, length 1).
3. Encode a new or replaced line: units, then 0xFFFF, XOR with
   `key = (u16)((index + 3) * 0x2983)`, `key = rotl16(key, 3)` per unit.
   `length` = units + 1.
4. Write the member: `u16 1`, `u16 count`, `u32 size`, `u32 0`, `u32 16`,
   then `u32 size`, the table (`offset = 4 + 8*count + running bytes`,
   `length`, `flags`), then the strings in index order. `size` is
   `4 + 8*count + string bytes`; the member length is `16 + size`. The
   `flags` of a new line is the most common flags value in the bank (60,
   96 or 84 in the banks measured), 0 for a new bank.
5. Hand the result to `bw_narc_pack` in place of the member.

Encoding the port must produce: the layout above. UTF-16 code units
little endian, the cipher above, end code 0xFFFF, length counting the end
code. Packed (0xF100) strings need not be produced: they are only read
through the word set, which passes plain strings through (2.1). The
flags word is not read by the game (3.1 row 4).

Group padding (recommended): a mod manifest entry lists the bank alias
group of a kind; after patching, the builder pads every bank of the group
to the same count with empty lines. With rows 2 and 3 fixed this is not
required for safety, but it keeps the pokedex and battle readers from
showing blank names for a bank the author forgot. It should warn on a
padded bank so the author sees the gap.

Aliases (names for the banks in 3.1) belong in a table the port ships.
Suggested set: `species_name` 70, `species_name_article` 281,
`species_name_upper` 284, `item_name` 54, `item_desc` 53,
`item_name_article` 279, `item_name_plural` 280, `move_name` 203,
`move_desc` 202, `move_name_upper` 286, `ability_name` 182,
`ability_desc` 183, `ability_name_upper` 285, `trainer_name` 190,
`trainer_talk` 189, `trainer_class` 191, `place_name` 89, plus
`dex_entry_a` 235 and `dex_entry_b` 236, `dex_category` 260. The
`msg` file may also be named by alias in place of the member number
(`species_name.msg`), resolved by the port. [Design, not implemented.]

Caching: the merged archive is already cached per path and flushed on any
change to the roots (`vfs_narc.c`, `bw_vfs_flush`). The text merge fits in
the same cache; the rebuild cost is one decode of the base header and a
copy, small compared to the NARC repack.

Authoring tool: extend `tools/assets/text.py` (aletheia) or add an
eidos tool with `dump` to the patch format, `check` (parse a patch and
report overruns of buffers in rows 6 to 9), and `merge` (apply to a
member for tests). The runtime builder in C is the one the game uses.

### 6.4 Not covered by this design

Easy chat words, pokedex list rows (649), place name field width, trainer
talk tables, and the item, move, ability and species id fields. Those are
other surveys.

## 7. Open questions

1. The exact role of members 244, 245 and 268 (727 lines each) in the
   pokedex, and the table `tbl_021BC1C4` that lists the 35 bank ids per
   pokedex page (`ov206/zukan_info.c:433`, `:1615`). The table is in
   overlay 206 data, which I did not read. Look at the overlay 206 image
   for the 35 u16 values.
2. Whether the port's heap arenas can hold the largest resident bank after
   it grows (pokedex entry banks, about 131 KB, loaded resident at
   `ov208/zukan_detail_info.c:461` to `:463`). Check `GFL_HEAP` sizing in
   `sdk/os_alloc.c` and `os_arena.c`.
3. `sPmsiSearchBankFileIds` and the 27 letter banks were not decoded. They
   are the easy chat letter lists (first entries "CACNEA", "MACHAMP").
4. Banks 90 to 92 (place name variants for the summary screen) were not
   counted.
5. Whether any other reader of a bank bypasses `MSGDATA_*`. I searched for
   archive ids 2 and 3 in `ARC_TOOL_LoadMember` / `ReadMember` /
   `CreateHandle` calls and found none; `MSGDATA_CreateFromMemory` has
   three callers (musical data), none for `a/0/0/2` [V by grep]. Dynamic
   ids passed by variable (for example `scrcmd_work.c:589`,
   `field_msgbg.c:590`, `gtsnego_message.c:796`) were not followed to every
   source.
6. The 9 bit packed lines: the table `gPackedCharTable` (`wordset.c:1114`)
   is not read, so a packer for authors who want the original packed form
   is not specified. The design does not need one.
7. Nickname effect of renaming (section 5) is read from code, not run.
