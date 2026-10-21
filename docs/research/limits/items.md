# Limits survey: items

Read-only survey. Tags: [V] verified in code or data, [I] inferred.
Paths are under aletheia `src/game/` unless noted. Line numbers are from the
`C:/aletheia-w20` tree.

## 1. Summary

Original count: 627 item ids, 0..626 (0x000..0x272). [V] a/0/2/4 has 627
members; message banks 0x35, 0x36, 0x117, 0x118 of a/0/0/2 each hold 627
strings; arm9/file_util.c:293 states "626 is the last item id".

Difficulty: medium. Item ids are u16 everywhere that matters (bag slot,
held item, shop row, sort key), so the id space itself reaches 65534. The
limits are a small set of explicit clamps (about 20 sites, all easy) plus
four fixed ARM9 data tables that the port must replace or extend.

Biggest obstacles:

1. The item icon table `item_gfx_member` (627 x {u16,u16}, ARM9 data at
   0x0209EB6C) and the 46-row extra table (0x0209E9E8 and 0x020A03C8). The
   icon lookup is a table read, not arithmetic, so new items need a new
   table. The ARM9 image is copied flat into emulated RAM, so the table
   cannot grow in place. Replace `FILE_UTIL_Get` / `FILE_UTIL_LoadItemRecord`.
2. The TM/HM system is hard-coded to two id windows (0x148..0x1A9 and
   0x26A..0x26C) and a 101-entry move table (0x0209EAA0). New TMs need new
   windows, a longer table, a TM compatibility bit beyond the 128 bits in
   the personal record, and the shop "itemId - 327" display number.
3. Battle item effects: `gItemHandlerRegistry` is a 172-entry (0xAC)
   linear table in ov093 data, and `gBagItemEffectTable` has 23 entries.
   A new item with a new effect needs a registry hook.
4. Every id check clamps to 626. Held-item reads in a Pokemon record turn
   ids above 626 into 0, and writes refuse them. Removing the clamp changes
   what a vanilla game sees in a mod save (see section 4).
5. Regulation item bans use a bit array of 0x4C bytes (608 bits) indexed by
   item id with no bound check. It already overreads for ids 608..626 and
   will overread more for mod ids.

## 2. Site table

Class key: ID = id check, TAB = array or table size, SAVE = save layout,
W = id storage width, ARC = asset index arithmetic, TXT = text, OTH = other.

### 2.1 The explicit ceiling checks (626 / 0x272)

| Site | Function | Literal | Guards | Class | Change needed |
|---|---|---|---|---|---|
| arm9/file_util.c:324 | FILE_UTIL_Get | `item > 626` -> 0 | icon lookups (which 0..4) | ID | raise or read count from mod-aware source [V] |
| arm9/file_util.c:381 | FILE_UTIL_LoadArcMember | `item > 626` -> 0 | member read of a/0/2/4 through an open handle | ID | raise to archive member count [V] |
| arm9/file_util.c:394 | FILE_UTIL_LoadItemRecord | `item > 626` -> 0 | record read (which 0) and icon members (1, 2) | ID | same [V] |
| arm9/file_util.c:430 | FILE_UTIL_ReadString | `entry > 0x272` -> 0 | item name bank, a/0/0/2 member 0x36 | ID, TXT | raise [V] |
| arm9/file_util.c:458 | FILE_UTIL_ReadStringAlt | `entry > 0x272` -> 0 | item description bank, member 0x35 | ID, TXT | raise [V] |
| arm9/poke_tool.c:2903 | POKE_TOOL_GetBoxValueRaw, param 6 | `result > 626` -> 0 | held item read from a Pokemon | ID | raise; see section 4 [V] |
| arm9/poke_tool.c:3246 | POKE_TOOL_SetBoxValueRaw, param 6 | `value > 626` -> no write | held item write | ID | raise [V] |
| arm9/block20_savedata.c:355 | BLOCK20_SAVEDATA_GetEntryId | `id > 0x272` -> 0 | Dream World delivery list (20 x u16 ids at `entryIds`, struct at :73..78); callers in ov021/pdw_postman.c:317,336,361,384 | ID, SAVE | raise [V]. Note `index > 20` at :351 allows index 20 for a 20-entry array (original off-by-one, [V] by reading :77) |
| ov010/mystery_gift_mon.c:212 | gift validation | `gift->item > 626` -> reject | held item of a gifted Pokemon | ID | raise [V] |
| ov010/game_beacon_accessor.c:921 | GAME_BEACON_ACCESSOR_IsActionItemNoInvalid | `value == 0 \|\| value > 0x272` | item id in a beacon (Wi-Fi/IR/local) action | ID | raise [V] |
| ov010/game_beacon_accessor.c:953 | ...IsDistributionItemNoInvalid | `itemNo > 0x272` | distributed item in beacon | ID | raise [V] |
| ov021/field_encount.c:463 | wild mon builder | `heldItem <= 0x272` | wild held item; above 0x272 means "none", 0 rolls species items | ID | raise bound. Also check `sHeldItemWeights` (:279) is per-roll, not per-id [I] |
| ov021/scrcmd_proc_fld.c:1303 | SCRCMD_PROC_FLD_UpdateItemIcon | `itemId <= 0x272` | shop icon draw (hides icon otherwise) | ID | raise [V] |
| ov021/scrcmd_proc_fld.c:1474 | SCRCMD_PROC_FLD_PrintItemDescription | `itemId <= 0x272` | shop description print | ID | raise [V] |
| ov021/scrcmd_pokemon_fld_types.h:34 | `MYSTERY_GIFT_ITEM_MAX` 0x272 | used at ov021/scrcmd_pokemon_fld.c:1732 | mystery gift item card (script) | ID | raise [V] |
| ov021/sodateya.c:169 | `TM_ITEM_LAST` 0x272 | loop `tmItem = 0x148..0x272` at :1388 | day care egg TM-move scan; the loop calls FILE_UTIL_GetMachineMove for every id in the range | ID, OTH | not a count of items: it is the end of a scan range that covers all TM windows. Raise only if new TM windows go above 0x272 [V] |
| ov093/btl_pokeparam.c:336 | battle mon setup | `heldItem > 0x272` -> 0 | held item copied into battle state | ID | raise [V] |
| ov121/mystery_net.c:777 | chk2 | `otIdOrItem <= 0x272` | Wi-Fi gift type 2 item id | ID | raise [V] |
| ov121/mystery_net.c:861 | gift record check | `r->heldItem > 0x272` -> reject | Wi-Fi gift held item | ID | raise [V] |

The coordinator list of 20 sites is covered: file_util.c 324/381/394/430/458,
poke_tool.c 2903/3246, block20_savedata.c 355, mystery_gift_mon.c 212,
game_beacon_accessor.c 921/953, field_encount.c 463, scrcmd_proc_fld.c
1303/1474, scrcmd_pokemon_fld_types.h:34, sodateya.c:169, and the icon table
(row below). New in this survey: btl_pokeparam.c:336, mystery_net.c:777 and
861, scrcmd_pokemon_fld.c:1732 (user of the macro).

Literal search for 627, 0x273, 0x271, 625 found no further item sites [V]
(all other hits were unrelated: RNG 625, effect ids, heap tags).

### 2.2 Fixed tables

| Site | Name | Size | Guards | Class | Change needed |
|---|---|---|---|---|---|
| ARM9 0x0209EB6C (extern in arm9/file_util.c:208; used :338, :343, :402, :405) | item_gfx_member | 627 x {u16 lo, u16 hi} = 2508 bytes [I: size from count and use] | per item: tile member `lo` and palette member `hi` in a/0/2/5 | TAB, ARC | replace with a growable table loaded from a mod file; patch FILE_UTIL_Get and FILE_UTIL_LoadItemRecord. Entries outside the table read neighbouring ARM9 data today [V by reading: clamp is the only guard] |
| ARM9 0x0209E9E8 (file_util.c:209; :350, :353) | item_gfx_member_x | 46 rows x {u16,u16} = 184 bytes [V: 0x0209EAA0 - 0x0209E9E8] | alternate icon members for battle bag (`which` 3, 4) | TAB | extend as above; row found by ITEM_GFX_GetRowIndex |
| ARM9 0x020A03C8, arm9/item_gfx.c:19..90 | sItemGfxExtraTable | 0x2E = 46 rows x {u16 itemId, u16 value}; linear search at :70, :86 | row index into item_gfx_member_x; returns row 0 when absent | TAB | add rows for new items that need the alternate icon. Missing row falls back to row 0 (wrong icon, no crash) [V] |
| arm9/file_util.c:686..699 | FILE_UTIL_IsMachineItem | windows 0x148..0x1A9 and 0x26A..0x26C | TM/HM membership; used by bag stack size (myitem_savedata.c:535), sort (:473), shop, day care, item menu | ID | add a window or table-driven test |
| ARM9 0x0209EAA0, file_util.c:727..750 | FILE_UTIL_GetMachineMove / g_0209EAA0 | 101 x u16 (0x0209EAA0 to 0x0209EB6C is 204 bytes, 102 halfwords [V by address]) | item id -> move id for TMs. HMs are entries 0x5C..0x61 (IsHmMove, :754) | TAB | longer table; update the 0x208 / 0x148 subtrahends |
| file_util.c:779 FILE_UTIL_GetId; :797 FILE_UTIL_GetMachineIndex | TM slot byte | returns u8; 0xFF = not a TM | TM number used as compat bit index and sort key; GetMachineIndex accepts only 0x1A4..0x1A9 (the six HM ids) | W | u8 caps TM index at 254; compat bits cap at 128 (below) |
| arm9/poke_tool.c:2286 POKE_TOOL_CheckSpeciesTmCompat; ov021/sodateya.c:1489..1510 | TM compat | personal params 0x27..0x2A, 4 x u32 = 128 bits; index = `tmIndex / 32 + 0x27` | which species learn which TM | TAB | new TMs beyond index 127 need a side table (mod data) [V]. 101 are used, 27 spare |
| ov021/scrcmd_proc_fld.c:1129 and :1749 | shop TM label | `itemId - 327` (0x147) | TM number shown in shop and buy confirm | ARC | derive from FILE_UTIL_GetId |
| ov070/itemmenu.c:3010 | bag TM label | `FILE_UTIL_GetId + 1` | TM number in bag | OTH | fine if GetId is patched |
| ov021/scrcmd_pokemon_fld.c:925 | script TM check | `FILE_UTIL_GetId(0x151)` | hard-coded TM (Hidden Power item) | OTH | none |
| arm9/file_util.c:809..850; ARM9 0x0209E8EC | mail items | 12 x u16 | IsMailItem, GetItemSlot, GetItemId; ~60 caller sites in trade, boxes, party | TAB | only if a mod adds mail; extend table |
| ov093/hand_tokusei_types.h:663 `ITEM_HANDLER_COUNT` 0xAC; ov093/hand_item.c:438, 449 | gItemHandlerRegistry (0x021F1EA4) | 172 entries, linear search | battle held-item effect factory, called by HAND_ITEM_AddFactor (:441) from btl_server_flow.c:2978, 5387, 10416 | TAB | wrap HAND_ITEM_AddFactor to consult a mod registry first. Items not in the table simply have no battle handler (returns 0) [V] |
| ov093/btl_server_flow.c:1602, 11395..11470 | gBagItemEffectTable | 23 entries | battle bag use; data-driven by item record params (BTL_CALC_GetItemParam), plus exact-id entries | TAB | new battle bag effects need a hook; items using the 23 existing param effects work with data only [V] |
| arm9/pms_draw.c:938; ARM9 0x020A02A8 ItemValueTable | ITEM_GetRegisterFlagId | 7 entries; returns 0x19 = not registerable | which key items can be registered to the shortcut | TAB | extend only for new registerable key items |
| arm9/recent_id_list_savedata.c:69..140 | registered-item flags | `u8 ids[26]`, 25 slots, 25 = empty | save block 0x36 stores the registered ids | SAVE | at most 25 distinct register flags; a mod needs flags 0..24 only if it adds registerable key items |
| arm9/palanm.c:96, 219 | sItemBerryTable | u16 table searched by color code | uncertain; looks like a berry/color map [I] | TAB | check if mods add berries (open question) |
| ov010/hidden_item.c:31 | HIDDEN_ITEM_COUNT 177 | 177 hidden item spots | field data, not item ids | OTH | not an item-id limit; map data |
| ov018/event_comm_common.c:578, 579; ov021/effect_encount.c:165, 622 | message id and rare item tables | small fixed | per-event; `sRareItemIds` indexed by roll | TAB | not id-count limits |
| ov093/hand_tokusei.c:1535 | gKlutzExemptItemIds | list | items not suppressed by Klutz | TAB | extend with mod items if wanted |

### 2.3 Id storage width

| Site | Field | Width | Note |
|---|---|---|---|
| arm9/myitem_savedata.c:52, struct BagSlot (types header) | itemId, quantity | u16, u16 [V: used as `{u16 itemId; u16 quantity;}` in the file comment] | fits ids to 65534; 0xFFFF is a sentinel in the bag UI (ov070/itemmenu.c:758 `shownItemId = 0xFFFF`) and in FILE_UTIL_Get (:331) |
| Pokemon record block A held item | u16 (poke_tool.c:2738) | fits |
| ov021/field_encount.c:140 | `u16 heldItem` | fits |
| ov070/itemmenu.c:2044 | sort key `(iconKind << 28) + (partyUseOption << 16) + itemId` | itemId occupies the low 16 bits; fits |
| arm9/file_util.c:110 ItemRecord.pocket | 4 bits (16 pockets) | fits; only 5 used |
| arm9/file_util.c ItemRecord.sortGroup | 5 bits | fits |
| arm9/regulation_types.h:21 | `vetoItemBit[0x4C]` read at arm9/regulation.c:244 (`bit / 8`) from poke_regulation.c:212, 392 | 608 bits, no bound check | ids 608..626 already read past the array into `mustPoke` etc. [V by layout]. Mod ids read further. Guard with `item < 608` and treat higher ids as not banned |
| arm9/bag_param.c:44, ov010/event_field_proclink.c:140 | `itemCheck[0x18]` 24 bytes | purpose not settled; copied as an opaque block into the bag screen param [I] likely per-use flags, not per-id |
| ov070/itemmenu_types.h:48, :89 | work copy `BagSlot items[310]` | sized by the largest pocket (see section 4), not by item count |

### 2.4 Icon, archive and text arithmetic

| Site | What | Note |
|---|---|---|
| arm9/file_util.c:359..361 FILE_UTIL_GetItemCount | returns 0x19 | this is archive id 25 (a/0/2/5), the icon archive. It is not an item count despite its name [V]. Callers pass it to ARC_TOOL_CreateHandle: ov170/box2_seq.c:10178, ov197/b_bag_main.c:1090..1135 |
| file_util.c:374 FILE_UTIL_CreateItemArcHandle | opens archive 0x18 = a/0/2/4 | handle is reused by ov070/itemmenu.c:2039, 3239, ov096/tr_ai.c:330, 2239 (AI item reads) |
| ov096/tr_ai.c:2239 | `FILE_UTIL_LoadArcMember(ai->itemArchive, itemId, heapId)` | goes through the 626 clamp at file_util.c:381 |
| a/0/2/5 member math | member = `item_gfx_member[item].lo/hi` | no arithmetic from the item id; members 0..1004 [V: 1005 members] |
| ov070/itemmenu.c:2879 | `ARC_TOOL_CreateHandle(0x53, ...)` | archive 83, not item data (UI) [I] |
| text: WORDSET_SetSlotFromMsg36 (arm9/wordset.c:476), Msg117 (:481), Msg118 (:486), WORDSET_SetSlotFromBank (:491) | item name (0x36), name with article (0x117), plural (0x118) | all index by item id into banks of 627 strings; no clamp in wordset itself, so a missing string is an out-of-range index into the bank [I] |
| ov021/scrcmd_proc_fld.c:1193, ov021/pdw_postman.c:236, ov021/scrcmd_proc_fld.c:1150 | direct MSGDATA_CreateFromArchive(0, 2, 0x36 / 54) | direct bank use; bank extension covers them |
| ov070/itemmenu.c:3021, ov197/b_bag_main.c:738 | FILE_UTIL_ReadStringAlt | goes through the 0x272 clamp |
| bank 0x37 | 5 pocket names | not per item |

### 2.5 Bag pockets and sorting

| Site | What |
|---|---|
| arm9/myitem_savedata.c:31..51, 66..73 | pockets: items 0x136 (310), key items 0x53 (83), TM/HM 0x6D (109), medicine 0x30 (48), berries 0x40 (64) slots; block size 0x9C0 bytes [V]; max stack 999 (:51), TM/HM stack 1 (:535) |
| myitem_savedata.c:183..215, 499..530 | pocket switches; unknown pocket gives a NULL / uninitialised count. Pocket comes from the item record field `pocket` (4 bits) via FILE_UTIL_GetItemParam(item, 5) at :216. A mod item chooses its pocket by data only |
| myitem_savedata.c:444..478 | berries sort by id; TM/HM sort by FILE_UTIL_GetId. New TMs must be machine items or they sort wrong |
| ov070/itemmenu.c:2030..2056 | per-pocket display sort uses record params 0xF (partyUse as icon kind) and 0x11 |
| ov070/itemmenu.c:1441 | `selectedItemId >= 0x89 && <= 0x94` hard-coded item range (12 ids, mail range) for an action [V] |

### 2.6 PC item storage, shops, berries, key items, mail, field use

- PC item storage: no separate item PC structure found. The player's PC item
  box is the same bag (pocket 0 items). Not verified by a dedicated search
  for a "PC" item array [I].
- Shops: fixed shops `d_021DE164` (ShopItem {itemId, price}) with counts
  `d_021D9430` (BuildItemListFixed, ov021/scrcmd_proc_fld.c:1190..1205);
  badge-tiered shops `d_021DE1A8` (u16 lists) with counts `d_021D95DE`
  (:1149..1180). The runtime shop list holds at most 40 entries
  (`ShopItem items[40]`, scrcmd_proc_fld_types.h:121). These tables are ARM9 or
  overlay data, so a mod that adds a shop or stock must replace them [V].
  Prices come from the item record (`price * 10`, file_util.c:489 case 0).
- Berries: pocket 3, sorted by id. No berry-growth table keyed by item was
  found in the files searched (field berry planting is outside this scan)
  [unverified].
- Key items: record field `preventToss`, pocket 4. Registering (Y-style
  shortcut) is limited as above.
- Item use: the bag use path reads record fields (`fieldUse`, `partyUse`,
  `partyUseParam`, `battleUse`) through FILE_UTIL_GetItemParam (file_util.c:538
  cases 6, 7, 15..17 and ext 0x12..0x3E). So potions, status cures, EV items
  and stat items work by data alone [V]. Items with special field effects
  (Repel, bikes, Escape Rope) are handled by id in ov070/itemmenu.c
  (e.g. :1159 `ITEMMENU_IsRepelItem`) [V for Repel]; a new special field
  item needs code.
- Evolution items: ovlerlay data keyed by id; evolution uses held item
  `holdEffect` params via natsuki.c:98, 195 and shinka_check.c:140, 307 [V].
- Trade items, mail: mail ids in the 12-entry table above. Trade code only
  reads the held item through POKE_TOOL_GetPartyValue (clamped at 626).
- TM item to move: ARM9 table above. Move tutors are not item based.

## 3. Data and assets

- a/0/2/4 (item data): NARC with 627 members, one record per item [V:
  members counted; bw_decomp tools/assets/items.py header]. Record is
  described in arm9/file_util.c:110..145 (`ItemRecord`, 0x21 bytes of data,
  36-byte stride per items.py) with a header of 17 bytes plus an extension
  block at +0x10 (struct ItemExt, file_util.c:54..105). Adding members past
  the end is enough for the data itself: eidos `docs/mods.md` "Member
  overrides" (lines 141..183) says a member index past the base count
  extends the archive [V]. The game then reads the new member once the 626
  clamps are lifted.
- a/0/2/5 (icons): 1005 members. Item `n` uses members
  `item_gfx_member[n].lo` (tiles) and `.hi` (palette); one shared cell and
  animation member sits at fixed positions (the code reads
  `FILE_UTIL_GetBankCount()` = 1 and `FILE_UTIL_GetBaseIndex()` = 0 for the
  cell animation, file_util.c:364..372). A mod appends two members per new
  item and supplies the table rows. Appending members works; the table is
  the blocker.
- a/0/0/2 (messages): banks 0x35 (descriptions), 0x36 (names), 0x117 (name
  with article), 0x118 (plural), each 627 strings [V: counted from the NARC
  and decoded sample entries]. A mod appends strings to each of the four
  banks; the bank is one NARC member holding all strings, so the mod must
  replace the whole member (or eidos must merge text). Message member index
  is the item id; no hashing.
- Machine move table (ARM9 0x0209EAA0) is not a file: it lives in the ARM9
  image. Same for the icon tables, mail table, handler registries and shop
  lists. eidos's `docs/armdata.md` says the image is copied flat at boot, so
  replacing them means eidos-side function replacement or relocating data.

## 4. Save data

- Bag: 0x9C0 bytes, five fixed-size pocket arrays of {u16 id, u16 count}
  (myitem_savedata.c:66..73). A new item id is storable as is, as long as the
  pocket has a free slot. Size does not change, so the original save format
  stays compatible. The only per-id save fact is the id itself.
- Pokemon held item: u16 in block A (poke_tool.c:2738). A mod id above 626
  is saved as is in the Pokemon blob. A vanilla build reads it back as 0
  (:2903), so the item is lost if the save returns to the original game. That
  is acceptable but worth stating. If compatibility is required, store the
  mod item in the mod save and write 0 to the Pokemon.
- Dream World delivery list: 20 x u16 ids (block20_savedata.c:77).
- Registered items: save block 0x36, 26 bytes (recent_id_list_savedata.c:69).
- Nothing in the original save tracks "item seen" or "item obtained"
  per id (no bitset found) [I; not exhaustively searched]. Per-item extra
  data a mod needs (for example a custom counter) goes to eidos
  `sdk/mod_savedata.c`.
- The bag pocket capacity (614 slots total) does not grow. If a mod needs
  more room it must keep overflow in mod storage or refuse the add.

## 5. Mod author supplies vs port unlocks

Mod author supplies:
- item records (member 627+ of a/0/2/4), item name, description, article and
  plural strings, icon tile and palette members, the icon table rows, TM
  move mappings, shop stock, and C code for any new battle or field effect.

Port must unlock:
1. Lift the 626 / 0x272 clamps at every site in section 2.1 to a runtime
   count (archive member count, or a mod-declared maximum).
2. Replace FILE_UTIL_Get / LoadItemRecord icon lookup with a table the mod
   can extend (item_gfx_member, item_gfx_member_x, sItemGfxExtraTable).
3. Make TM tests and the TM move table extensible (IsMachineItem,
   GetMachineMove, GetId, shop label, compat bits beyond 128).
4. Add a mod entry point to HAND_ITEM_AddFactor (battle held item) and to
   the bag-use effect table.
5. Guard the regulation item-ban array against ids >= 608.
6. Extend or replace shop tables and the registerable key item table.
7. Merge or extend the four message banks.

## 6. Open questions

- ITEM `item_gfx_member` size: the 627-entry size is inferred from the clamp
  and address (0x0209EB6C), not from a symbol end. Check
  `layout/data_objects.txt` neighbours, or the ARM9 bytes.
- `sItemBerryTable` (arm9/palanm.c:96, 219): purpose unknown; check callers
  of the function at palanm.c:219.
- `itemCheck[0x18]`: what it flags (bag_param.c:44).
- Field berry planting, apricorn/berry trees, and fishing/hidden item tables
  keyed by item: not searched. Look for scripts and `ov010/hidden_item.c`.
- PC item storage: confirm there is no separate array (grep the PC overlay).
- Callers that pass non-item values to FILE_UTIL_Get: ov121/mystery_album.c:1495
  and ov123/gsync_disp.c:736 pass values named species or arcId. They may use
  the same function for non-item icons through a shared archive; do not
  change the clamp semantics without reading them.
- Whether any scripted `ADD_ITEM`-style script command checks the id
  (scrcmd_trainer_audio_item.c:1118..1135 reads params but no bound seen
  there); scan the script command handlers for `0x272` literals if a hidden
  folded compare exists (the literal search found none).
