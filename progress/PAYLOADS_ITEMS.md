# Payloads: inventory, items and moves

The payloads for the inventory, items and moves area of `docs/mod-events.md`,
section 1. The area is the item bag commands, the gifts, the field and battle
item use, the healing, the shops, the money, the move events and the TM and
tutor events.

A payload is a situation, not an entity. Each table below gives the subject, its
owner, the cause and the circumstances a handler needs to decide and act. Every
field is attributed to the call argument, global, struct field or call it comes
from, with the file and line it was read at. A field the port cannot read at the
call is named in "Context the area tracks" and fed from an earlier event.

Addresses, regions and kinds are `docs/mod-events.md`, section 1, and
`mod/declared_set.txt`. The source is aletheia's matched tree under
`<home>\OneDrive\Documents\bw_decomp\src\matched`.

The events specified here: 43.

- bag: `item.add`, `item.remove`, `item.has`, `item.quantity`, `item.canadd`,
  `item.machine`, `item.pocket`, `item.partyuse`.
- gifts: `item.gift.daily`, `item.gift.daily.index`, `item.gift.mystery`,
  `item.gift.postman`, `item.gift.postman.count`.
- field item use: `item.use.field`, `item.use.done`, `item.use.continue`,
  `item.use.calc`.
- healing: `item.heal.party`, `heal.party`.
- battle item and bag: `battle.item.use`, `battle.item.heal`,
  `battle.item.cure`, `battle.item.check`, `battle.bag.add`, `battle.bag.sub`.
- shops: `shop.start`, `shop.pay`.
- money: `money.set`, `money.add`, `money.check`.
- moves: `move.learn`, `move.push`, `move.slot`, `move.set`, `move.get`,
  `move.relearn.list`, `move.relearn`, `move.relearn.count`.
- TM and tutor: `tm.tutor.select`, `tm.tutor.move`, `tm.tutor.status`,
  `tm.tutor.id`, `tm.type`.

## Context the area tracks

Some fields are in scope one call earlier or live in state the game does not
pass. The port tracks them. A stale context field is worse than a missing one,
so each entry names its feeder and where it clears.

### Last item used, and on whom

- What: `item_id` (u16), `item_use_kind` (u32), `target_mon` (party handle),
  `target_party_index` (u32), `target_row` (u32), `user_mon` (party handle or
  battle mon), `user_side` (u8), `in_battle` (bool), `move_slot` (u16),
  `item_removed` (bool).
- Feeds it: `item.use.field` (`PLIST_MENU_StartItemUse` 0x021BC350) reads the
  item id at `Obj00009.f280 + 0x44` and the target mon at `f38`
  (`src/matched/ov/ov091_func_021BC350.c:63`, `:77`), and the kind from
  `PLIST_MENU_GetItemUseKind` (`src/matched/ov/ov091_func_021BBE74.c:23`). The
  item is applied by `PLIST_DEMO_UseItemOnMon` 0x021BE2B4
  (`src/matched/ov/ov091_func_021BE2B4.c:46`). `battle.item.use`
  (`BTL_SERVER_FLOW_ProcBagItemUse` 0x021CB730) has the user mon `a`, the item
  `move` and the target `slot` as arguments
  (`src/matched/ov/ov093_func_021CB730.c:81`).
- Clears: at `field.bag.closed` (`SCRCMD_PROC_OnBagClosed` 0x0215C700) and at
  `item.use.done` / `item.use.continue`. A new `item.use.field` overwrites it.
  The item leaves the bag at `POKELIST_RemoveBagItem` 0x021BA670
  (`src/matched/ov/ov091_func_021B7CC4.c:126`), not at the event.

### The current shop

- What: `shop_id` (u32), the work block `w` (ptr), `bag` (ptr), `misc_save`
  (ptr), `trainer_record` (ptr), `save_block_3a` (ptr), `msg_bg` (ptr),
  `shop_kind` (u8).
- Feeds it: `shop.start` (`SCRCMD_SHOP_StartShop` 0x021B2FD8) allocates `w`
  and stores those pointers (`src/matched/ov/ov021_func_021B2FD8.c:103-111`).
- Clears: when the shop task frees the block, `SCRCMD_PROC_FLD_StepShopTask`
  case 7, `func_020307B0(*slot)`
  (`src/matched/ov/ov021_func_021B3158.c:131`).

### The daily gift index and date

- What: `gift_index` (u32), `month` (u32), `day` (u32), `special_date` (bool).
- Feeds it: `item.gift.daily.index` (`SCRCMD_POKEMON_FLD_GetDailyGiftIndex`
  0x021B73B8) computes all four (`src/matched/ov/ov021_func_021B73B8.c:43-48`).
- Clears: after `item.gift.daily` reads the index; the next index read
  overwrites it.

### The mystery gift card

- What: the card buffer `p` (0xcc bytes), `card_index` (u32), `gift_kind`
  (u8), `handler_id` (u32).
- Feeds it: `item.gift.mystery` (`SCRCMD_POKEMON_FLD_HandleMysteryGift`
  0x021B6BDC) via `PDW_POSTMAN_FindMysteryCard` and
  `PDW_POSTMAN_GetMysteryGiftType`
  (`src/matched/ov/ov021_func_021B6BDC.c:93-95`).
- Clears: the buffer is freed by `MYSTERY_DATA_Delete(h)` at
  `src/matched/ov/ov021_func_021B6BDC.c:135`, so the port copies it at the
  call. Nothing is cleared later.

### The relearn selection

- What: `relearn_mon` (party handle), `relearn_moves` (list ptr), `session`
  (status ptr).
- Feeds it: `move.relearn` (`SCRCMD_PROC_CallMoveRelearn` 0x0215C89C) stores
  them in the `Relearn` record
  (`src/matched/ov/ov010_func_0215C89C.c:104`).
- Clears: the record is owned by the ov173 proc, and no event fires at the
  actual move write. The port either tracks through to the ov173 write or
  accepts the gap.

### The last move written

- What: `move_id` (u16), `move_slot` (u32), `mon` (party handle), `method`
  (set, push or delete).
- Feeds it: `move.learn`, `move.set`, `move.slot`, `move.push`.
- Clears: not needed if read at the call; a later write overwrites it.

## Bag events

### `item.add` (0x021AF544, ov021, `SCRCMD_AddItem`)

The command reads the item id and count from the script and adds them to the
local player's bag. It is an action the game has not yet taken at the slot, so
it is a block candidate (`docs/mod-events.md`, section 2).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `item_id` | u16 | first `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF544.c:42` | the subject; which item |
| `count` | u32 | second `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF544.c:43` | how many to add |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021AF544.c:40` | the allocation the bag uses |
| `bag` | ptr | `GAME_DATA_GetMyItem(SCRCMD_WORK_GetGameData(b))`, `src/matched/ov/ov021_func_021AF544.c:41` | the owner's bag; in a soullink game this attributes the add |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(b)`, `src/matched/ov/ov021_func_021AF544.c:41` | the owner record |
| `result` | u16 | return of `func_02007E50(x,u,v,p)`, `src/matched/ov/ov021_func_021AF544.c:45` | how many were actually added |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021AF544.c:44` | where the script reads the result |
| `pocket` | u32 | a call the port can make, `SCRCMD_GetItemPocket` 0x021AF6E8 | which pocket the item lands in |
| `cause` | u32 | the script command being run; the command context `a` | which script granted it |
| `block` | bool | the return contract, `docs/mod-events.md`, section 2 | a block forbids the add |

### `item.remove` (0x021AF594, ov021, `SCRCMD_RemoveItem`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `item_id` | u16 | first `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF594.c:41` | the subject |
| `count` | u32 | second `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF594.c:42` | how many to remove |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021AF594.c:36` | the bag allocation |
| `bag` | ptr | `GAME_DATA_GetMyItem(SCRCMD_WORK_GetGameData(b))`, `src/matched/ov/ov021_func_021AF594.c:40` | the owner's bag |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(b)`, `src/matched/ov/ov021_func_021AF594.c:40` | the owner record |
| `result` | u16 | return of `func_02007F1C(d,e,f,c)`, `src/matched/ov/ov021_func_021AF594.c:44` | how many were actually removed |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021AF594.c:43` | where the script reads the result |
| `quantity_before` | u32 | a call the port can make, `SCRCMD_GetItemQuantity` 0x021AF684 | the count before the remove |
| `cause` | u32 | the command context `a` | which script took it |

### `item.canadd` (0x021AF5E4, ov021, `SCRCMD_CanAddItem`)

A state read. The payload is the query and the answer.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `item_id` | u16 | first `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF5E4.c:41` | the item asked about |
| `count` | u32 | second `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF5E4.c:42` | how many asked about |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021AF5E4.c:36` | the bag allocation |
| `bag` | ptr | `GAME_DATA_GetMyItem(SCRCMD_WORK_GetGameData(b))`, `src/matched/ov/ov021_func_021AF5E4.c:40` | the owner's bag |
| `can_add` | bool | return of `func_02007E3C(d,e,f,c)`, `src/matched/ov/ov021_func_021AF5E4.c:44` | the answer a handler branches on |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021AF5E4.c:43` | where the script reads the answer |

### `item.has` (0x021AF634, ov021, `SCRCMD_HasItem`)

A state read.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `item_id` | u16 | first `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF634.c:41` | the item asked about |
| `count` | u32 | second `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF634.c:42` | the threshold asked about |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021AF634.c:36` | the bag allocation |
| `bag` | ptr | `GAME_DATA_GetMyItem(SCRCMD_WORK_GetGameData(b))`, `src/matched/ov/ov021_func_021AF634.c:40` | the owner's bag |
| `has` | bool | return of `func_02007F68(d,e,f,c)`, `src/matched/ov/ov021_func_021AF634.c:44` | the answer a handler branches on |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021AF634.c:43` | where the script reads the answer |

### `item.quantity` (0x021AF684, ov021, `SCRCMD_GetItemQuantity`)

A state read.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `item_id` | u16 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF684.c:41` | the item asked about |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021AF684.c:36` | the bag allocation |
| `bag` | ptr | `GAME_DATA_GetMyItem(SCRCMD_WORK_GetGameData(b))`, `src/matched/ov/ov021_func_021AF684.c:40` | the owner's bag |
| `quantity` | u16 | return of `func_02007FB8(d,e,c)`, `src/matched/ov/ov021_func_021AF684.c:43` | the count a handler branches on |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021AF684.c:42` | where the script reads the count |

### `item.machine` (0x021AF750, ov021, `SCRCMD_GetItemMachineFlag`)

The thinnest event in the area. One argument, a boolean return, no VM context.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `item_id` | u32 | call argument `a`, `src/matched/ov/func_021AF750.c:26` | the item asked about |
| `is_machine` | bool | return of `FILE_UTIL_Check1288(a)`, `src/matched/ov/func_021AF750.c:28`, `src/matched/arm9/func_02021288.c:28` | whether the item is a TM, HM or similar machine |

### `item.pocket` (0x021AF6E8, ov021, `SCRCMD_GetItemPocket`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `item_id` | u16 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF6E8.c:33` | the item asked about |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021AF6E8.c:32` | the item record allocation |
| `pocket` | u32 | `FILE_UTIL_Get_0FE4(x, 5, y)`, selector 5, `src/matched/ov/ov021_func_021AF6E8.c:35` | which pocket the item belongs to |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021AF6E8.c:35` | where the script reads the pocket |

### `item.partyuse` (0x021AF71C, ov021, `SCRCMD_GetItemPartyUse`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `item_id` | u16 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021AF71C.c:33` | the item asked about |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021AF71C.c:32` | the item record allocation |
| `party_use` | u32 | `FILE_UTIL_Get_0FE4(x, 0xf, y)`, selector 0xf, `src/matched/ov/ov021_func_021AF71C.c:35` | the party-use kind a handler branches on |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021AF71C.c:35` | where the script reads the kind |

## Gifts

### `item.gift.daily` (0x021B7364, ov021, `SCRCMD_POKEMON_FLD_GetDailyGift`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `gift_index` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B7364.c:31` | which daily gift |
| `gift_index_used` | u32 | clamped to 0 when `v >= 11`, `src/matched/ov/ov021_func_021B7364.c:33-35` | the index actually read |
| `gift_item` | u16 | `sDailyGifts[v]`, `src/matched/ov/ov021_func_021B7364.c:36` | the item granted |
| `gift_table` | ptr | `sDailyGifts`, `src/matched/ov/ov021_func_021B7364.c:24` | the whole table, so a handler can read neighbours |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B7364.c:32` | where the script reads the item |
| `date_context` | context | tracked, fed by `item.gift.daily.index` | the day that chose the index |

### `item.gift.daily.index` (0x021B73B8, ov021, `SCRCMD_POKEMON_FLD_GetDailyGiftIndex`)

A state read that computes the index. It feeds the context above.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `month` | u32 | `FIELD_CLOCK_GetMonth(h)`, `src/matched/ov/ov021_func_021B73B8.c:43` | the month the gift depends on |
| `day` | u32 | `FIELD_CLOCK_GetDay(h)`, `src/matched/ov/ov021_func_021B73B8.c:44` | the day the gift depends on |
| `special_date` | bool | `SCRCMD_POKEMON_FLD_IsSpecialDate(x,y)`, `src/matched/ov/ov021_func_021B73B8.c:45` | whether the special gift applies |
| `gift_index` | u16 | 10 on a special date, else `sDailyGiftIndexByDay[y % 10]`, `src/matched/ov/ov021_func_021B73B8.c:46-48` | the index a handler branches on |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(b)`, `src/matched/ov/ov021_func_021B73B8.c:42` | the owner record the clock is read from |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B73B8.c:41` | where the script reads the index |

### `item.gift.mystery` (0x021B6BDC, ov021, `SCRCMD_POKEMON_FLD_HandleMysteryGift`)

One command, eight modes. The mode chooses what the command does: 0 test, 1
can-receive, 2 receive message, 3 follow-up message, 4 receive, 5 delivery NPC
id, 6 delivery NPC loaded, 7 receive type.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `mode` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B6BDC.c:88` | which of the eight actions is asked |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(b)`, `src/matched/ov/ov021_func_021B6BDC.c:90` | the owner record |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(b)`, `src/matched/ov/ov021_func_021B6BDC.c:91` | the mystery-data allocation |
| `mystery_data` | ptr | `MYSTERY_DATA_Create(...)`, `src/matched/ov/ov021_func_021B6BDC.c:92` | the live mystery record |
| `card` | ptr | `PDW_POSTMAN_FindMysteryCard(h, &x, buf)`, `src/matched/ov/ov021_func_021B6BDC.c:93` | the 0xcc-byte card the port must copy |
| `card_index` | u32 | written by `FindMysteryCard` into `x`, `src/matched/ov/ov021_func_021B6BDC.c:93` | which of the 12 card slots |
| `gift_kind` | u8 | `PDW_POSTMAN_GetMysteryGiftType(h)`, `src/matched/ov/ov021_func_021B6BDC.c:94`, `src/matched/ov/ov021_func_021B7864.c:32` | the gift category that picks the handler |
| `handler_id` | u32 | `sMysteryGiftKindHandlers[k].id`, `src/matched/ov/ov021_func_021B6BDC.c:95` | the kind table entry that matches |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B6BDC.c:89` | where the script reads the mode result |
| `out_value` | u16 | `*out` in each mode's case, `src/matched/ov/ov021_func_021B6BDC.c:100-133` | the answer a handler branches on |
| `received` | bool | `SCRCMD_POKEMON_FLD_ReceiveGift(...) == 1`, `src/matched/ov/ov021_func_021B6BDC.c:117` | whether the gift was taken |
| `liberty_pass` | bool | `SCRCMD_POKEMON_FLD_IsLibertyPassGift(p,k)`, `src/matched/ov/ov021_func_021B6BDC.c:119` | whether the Liberty Pass special case fired |
| `field_map` | ptr | `GAMESYSTEM_GetFieldMap(SCRCMD_WORK_GetGameSystem(b))`, `src/matched/ov/ov021_func_021B6BDC.c:126` | the map the delivery NPC is on |
| `gift_handler_table` | ptr | `sMysteryGiftKindHandlers`, `src/matched/ov/ov021_func_021B6BDC.c:36` | the kind table the handler walks |
| `receive_handler_table` | ptr | `d_021D9978`, `src/matched/ov/ov021_func_021B6D98.c:25` | the per-kind receive function |
| `type_handler_table` | ptr | `d_021D997C`, `src/matched/ov/ov021_func_021B6DC0.c:25` | the per-kind receive-type function |

The concrete gift content, an item id or a Pokemon, is not returned. It is
inside the card buffer and the handler table. The port copies the card so a
handler can read it.

### `item.gift.postman` (0x021B76B4, ov021, `PDW_POSTMAN_ReceiveItems`)

One call walks 0x14 container slots and adds each. The payload is the container
and what was taken from it.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `game_data` | ptr | call argument `a0`, `src/matched/ov/func_021B76B4.c:35` | the owner record |
| `key` | u32 | call argument `a1`, `src/matched/ov/func_021B76B4.c:35` | the bag key the add uses |
| `container` | ptr | call argument `a2`, `src/matched/ov/func_021B76B4.c:41-42` | the postman container |
| `slot_count` | u32 | the loop bound `0x14`, `src/matched/ov/func_021B76B4.c:40` | how many slots are scanned |
| `slots` | array | per slot `i`, item id `func_02009310(a2,i)` and count `func_02009330(a2,i)`, `src/matched/ov/func_021B76B4.c:41-42` | the items offered, id and count each |
| `added` | array of bool | `func_02007E50(saved,r6,r2,a1) == 1`, `src/matched/ov/func_021B76B4.c:44` | which items were added |
| `removed` | array of bool | `func_02009364(a2,i)` when added, `src/matched/ov/func_021B76B4.c:45` | which slots the container released |
| `bag` | ptr | `GAME_DATA_GetMyItem(a0)`, `src/matched/ov/func_021B76B4.c:37` | the owner's bag |

### `item.gift.postman.count` (0x021B76F8, ov021, `PDW_POSTMAN_CountUnreceivable`)

A state read over the same container.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `game_data` | ptr | call argument `p`, `src/matched/ov/ov021_func_021B76F8.c:39` | the owner record |
| `key` | u32 | call argument `key`, `src/matched/ov/ov021_func_021B76F8.c:31` | the bag key the test uses |
| `container` | ptr | call argument `q`, `src/matched/ov/ov021_func_021B76F8.c:43-44` | the postman container |
| `slots` | array | per slot `i`, item id and count, `src/matched/ov/ov021_func_021B76F8.c:43-44` | the items tested |
| `count` | u32 | return `n`, `src/matched/ov/ov021_func_021B76F8.c:47-50` | how many cannot be received |
| `unreceivable` | array of bool | `func_02007E3C(ctx,a,b,key) == 0`, `src/matched/ov/ov021_func_021B76F8.c:46` | which items failed |

## Field item use

### `item.use.field` (0x021BC350, ov091, `PLIST_MENU_StartItemUse`)

The party list starts an item use. It is a block candidate
(`docs/mod-events.md`, section 2). The struct is `Obj00009`
(`src/matched/include/Obj00009.h`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `menu` | ptr | call argument `p`, the party list struct, `src/matched/ov/ov091_func_021BC350.c:61` | the screen state |
| `item_id` | u16 | `*(u16 *)(W(0x280) + 0x44)`, `f280 + 0x44`, `src/matched/ov/ov091_func_021BC350.c:63` | the item used |
| `item_use_kind` | u32 | `PLIST_MENU_GetItemUseKind(item_id)`, `src/matched/ov/ov091_func_021BC350.c:63`, `src/matched/ov/ov091_func_021BBE74.c:23` | what the item does; the branch selector |
| `target_mon` | ptr | `W(0x38)`, `Obj00009.f38`, `src/matched/ov/ov091_func_021BC350.c:77` | the Pokemon used on; the subject |
| `target_row` | u32 | `W(0x2c)`, `Obj00009.f2C`, `src/matched/ov/ov091_func_021BC350.c:70` | the party row of the target |
| `party` | ptr | `T(0)`, the list at `f280`, read in the caller `src/matched/ov/ov091_func_021B7698.c:151` | the owner's party |
| `move_slot` | u16 | call argument `v`, `src/matched/ov/ov091_func_021BC350.c:115` | the move slot for a PP item |
| `hp_before` | u16 | `PLIST_PLATE_GetHp(p, mon)` into `H(0x11c)`, `src/matched/ov/ov091_func_021BC350.c:72`, `:101` | the HP before a heal |
| `level_before` | u32 | `POKE_TOOL_CalcLevel(W(0x38))`, `src/matched/ov/ov091_func_021BC350.c:77` | the level before a rare candy |
| `stats_before` | u16[6] | `POKE_TOOL_GetPartyValue(mon, gPlistStatParamIds[i], 0)`, `src/matched/ov/ov091_func_021BC350.c:87-90` | the six stats before an EV item |
| `message_id` | u32 | the id passed to `POKELIST_ShowMessage`, `src/matched/ov/ov091_func_021BC350.c:81` and following | which result message is shown |
| `owner` | ptr | the local player's party, `GAME_DATA_GetMyParty` | attributes the use in a soullink game |

### `item.use.done` (0x021BC2C8, ov091, `PLIST_MENU_ShowItemUsedThenClose`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `menu` | ptr | call argument `p`, `src/matched/ov/ov091_func_021BC2C8.c:30` | the screen state |
| `item_id` | u16 | `*(u16 *)(f280 + 0x44)`, `src/matched/ov/ov091_func_021BC2C8.c:34` | the item that was used |
| `word_set` | ptr | `Obj00009.f150`, `src/matched/ov/ov091_func_021BC2C8.c:33` | the message word set |
| `message_id` | u32 | `0x51`, `src/matched/ov/ov091_func_021BC2C8.c:35` | the message shown |
| `closes` | bool | the callback `POKELIST_EndMessageToExit`, `src/matched/ov/ov091_func_021BC2C8.c:35` | that the list closes after the use |

### `item.use.continue` (0x021BC30C, ov091, `PLIST_MENU_ShowItemUsedThenContinue`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `menu` | ptr | call argument `p`, `src/matched/ov/ov091_func_021BC30C.c:30` | the screen state |
| `item_id` | u16 | `*(u16 *)(f280 + 0x44)`, `src/matched/ov/ov091_func_021BC30C.c:34` | the item that was used |
| `word_set` | ptr | `Obj00009.f150`, `src/matched/ov/ov091_func_021BC30C.c:33` | the message word set |
| `message_id` | u32 | `0x51`, `src/matched/ov/ov091_func_021BC30C.c:35` | the message shown |
| `continues` | bool | the callback `POKELIST_EndMessageToList`, `src/matched/ov/ov091_func_021BC30C.c:35` | that the list stays open |

### `item.use.calc` (0x0201B7B4, arm9, `NATSUKI_CalcUseItem`)

A state accessor that computes an item use. The three effect bytes come from the
item record and are handed to `NATSUKI_Apply`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `mon` | ptr | call argument `a0`, `src/matched/arm9/func_0201B7B4.c:34` | the Pokemon the use applies to |
| `item_id` | u32 | call argument `a1`, used as the record id, `src/matched/arm9/func_0201B7B4.c:34`, `:40` | the item used |
| `effect` | u8[3] | `FILE_UTIL_Get_0FE4(item, i + 0x3C, heap)` for `i` 0..2, `src/matched/arm9/func_0201B7B4.c:39-40` | the three effect selectors, 0x3C, 0x3D, 0x3E |
| `arg` | u16 | call argument `a2`, `src/matched/arm9/func_0201B7B4.c:42` | the caller's extra argument |
| `heap_id` | u32 | call argument `a3`, `src/matched/arm9/func_0201B7B4.c:40` | the record allocation |
| `apply` | ptr | `NATSUKI_Apply`, `src/matched/arm9/func_0201B7B4.c:42` | the function that commits the use |

## Healing

### `item.heal.party` (0x0201BA34, arm9, `POKE_TOOL_HealParty`)

The shared healer, reached by `SCRCMD_PROC_HealParty` and by item use
(`docs/mod-events.md`, section 1a). It heals every occupied party slot.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `party` | ptr | call argument `party`, `src/matched/arm9/unit_0201B9F0.c:89` | the party healed; the subject and the owner |
| `party_count` | u32 | `POKEPARTY_GetCount(party)`, `src/matched/arm9/unit_0201B9F0.c:95` | how many slots are walked |
| `members` | array | `POKEPARTY_GetMember(party, i)` per slot, `src/matched/arm9/unit_0201B9F0.c:97` | the Pokemon healed |
| `occupied` | array of bool | `POKE_TOOL_GetPartyValue(mon, 0xA9, 0) != 0`, `src/matched/arm9/unit_0201B9F0.c:98` | which slots are healed |
| `hp_restored` | array | `POKE_TOOL_SetPartyValue(mon, 0xA0, GetPartyValue(mon, 0xA1))`, `src/matched/arm9/unit_0201B9F0.c:80` | that HP is set to max |
| `status_cleared` | array of bool | `POKE_TOOL_SetPartyValue(mon, 0x9D, 0)`, `src/matched/arm9/unit_0201B9F0.c:81` | that status is cleared |
| `pp_restored` | array | `POKE_TOOL_RestorePP(mon, i, 0x7F)` per slot, `src/matched/arm9/unit_0201B9F0.c:84` | that each move slot's PP is filled |
| `cause` | u32 | context: which caller, `SCRCMD_PROC_HealParty` or item use | a centre heal differs from an item heal |

The party field ids 0xA0, 0xA1, 0xA9 and 0x9D are not named by aletheia as HP
or status (`docs/mod-events.md`, section 4). The port can state the ids but not
"HP" until that mapping is fixed.

### `heal.party` (0x0215B5A8, ov010, `SCRCMD_PROC_HealParty`)

The field-side trigger. It is one of the six block events
(`docs/mod-events.md`, section 2).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(a1)`, `src/matched/ov/func_0215B5A8.c:33` | the owner record |
| `party` | ptr | `GAME_DATA_GetMyParty(v0)`, `src/matched/ov/func_0215B5A8.c:35` | the party healed |
| `command_context` | ptr | call argument `a1`, `src/matched/ov/func_0215B5A8.c:27` | the script command that asked |
| `block` | bool | the return contract, `docs/mod-events.md`, section 2 | a block forbids the heal |
| `heal` | ptr | `POKE_TOOL_HealParty`, `src/matched/ov/func_0215B5A8.c:37` | the tool the payload describes |

## Battle item and bag

### `battle.item.use` (0x021CB730, ov093, `BTL_SERVER_FLOW_ProcBagItemUse`)

The battle item use. It is a block candidate (`docs/mod-events.md`, section 2).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `battle` | ptr | call argument `p`, `src/matched/ov/ov093_func_021CB730.c:81` | the battle state |
| `user_mon` | ptr | call argument `a`, `src/matched/ov/ov093_func_021CB730.c:81` | who used the item |
| `item_id` | u16 | call argument `move`, `src/matched/ov/ov093_func_021CB730.c:81`, `:118` | the item used |
| `arg` | u8 | call argument `arg`, `src/matched/ov/ov093_func_021CB730.c:81`, `:177` | the effect's extra argument |
| `target_slot` | u8 | call argument `slot`, `src/matched/ov/ov093_func_021CB730.c:81`, `:103` | which party slot is targeted |
| `target_mon` | ptr | `BTL_SLOT_GetPoke(party, slot)`, `src/matched/ov/ov093_func_021CB730.c:107` | the target Pokemon |
| `user_side` | u8 | `BTL_MAIN_PokeIdToClientIdThunk(BTL_POKEPARAM_GetPokeId(a))`, `src/matched/ov/ov093_func_021CB730.c:100` | the owner in a link battle |
| `target_mon_id` | u32 | `BTL_POKEPARAM_GetPokeId(mon)`, `src/matched/ov/ov093_func_021CB730.c:108` | the target's identity |
| `condition` | u8 | computed from `slot`, `n` and the rule, `src/matched/ov/ov093_func_021CB730.c:102-114` | whether the target is valid |
| `state` | u32 | `func_021CBA30(p + 0x1ab8, monId)`, `src/matched/ov/ov093_func_021CB730.c:109` | the target's semi-invulnerable or substitute state |
| `rule` | u32 | `BTL_MAIN_GetRule(p + 4)`, `src/matched/ov/ov093_func_021CB730.c:113` | the battle rule |
| `is_ball` | bool | `BTL_CALC_GetMonParamWithHeap(move, 15) == 4`, `src/matched/ov/ov093_func_021CB730.c:119` | that the item is a ball |
| `ball_caught` | bool | `BTL_SERVER_FLOW_TryThrowBall(p,a,move)`, `src/matched/ov/ov093_func_021CB730.c:121` | the ball outcome |
| `effect_entry` | ptr | `gBagItemEffectTable[i]`, `src/matched/ov/ov093_func_021CB730.c:198` | the item's effect row |
| `effect_id` | u16 | `e->id`, `src/matched/ov/ov093_func_021CB730.c:199` | the effect's selector |
| `effect_mode` | u8 | `e->mode`, `src/matched/ov/ov093_func_021CB730.c:146` | how the effect is gated |
| `effect_exact` | bool | `e->exact`, `src/matched/ov/ov093_func_021CB730.c:144` | exact id match or param test |
| `effect_applied` | bool | return of `e->call(p, mon, move, result, arg)`, `src/matched/ov/ov093_func_021CB730.c:200` | whether the effect changed the mon |
| `item_returned` | bool | `BTL_MAIN_AddItemToBag(bag, side, move)`, `src/matched/ov/ov093_func_021CB730.c:209` | that the item was not consumed |
| `friendship_updated` | bool | `BTL_MAIN_UpdateFriendshipUseItem(bag, mon, move)`, `src/matched/ov/ov093_func_021CB730.c:210` | that the item moved friendship |
| `message_id` | u32 | the pushed id, 0x21, 0x24, 0xc4 or 0x44, `src/matched/ov/ov093_func_021CB730.c:118`, `:133`, `:162`, `:186` | which message the client shows |
| `effect_table` | ptr | `gBagItemEffectTable`, `src/matched/ov/ov093_func_021CB730.c:40` | the whole table for a handler |

### `battle.item.heal` (0x021CC2C0, ov093, `BTL_SERVER_FLOW_ItemEffectRecoverHp`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `battle` | ptr | call argument `a`, `src/matched/ov/ov093_func_021CC2C0.c:42` | the battle state |
| `mon` | ptr | call argument `m`, `src/matched/ov/ov093_func_021CC2C0.c:42` | the Pokemon healed |
| `item_record` | ptr | call argument `s`, `src/matched/ov/ov093_func_021CC2C0.c:42` | the item record the amount comes from |
| `hp_full` | bool | `BTL_POKEPARAM_IsHpFull(m)`, `src/matched/ov/ov093_func_021CC2C0.c:48` | whether the heal is skipped |
| `fainted` | bool | `BTL_POKEPARAM_IsFainted(m)`, `src/matched/ov/ov093_func_021CC2C0.c:48` | whether the heal is skipped |
| `poke_id` | u8 | `BTL_POKEPARAM_GetPokeId(m)`, `src/matched/ov/ov093_func_021CC2C0.c:49` | the healed mon's identity |
| `amount` | u32 | `BTL_CALC_GetMonParamWithHeap(s, 0x3a)` with 0xff, 0xfe, 0xfd special cases, `src/matched/ov/ov093_func_021CC2C0.c:53-67` | the HP restored |
| `applied` | bool | the return, 1 or 0, `src/matched/ov/ov093_func_021CC2C0.c:71` | whether the heal happened |
| `message_id` | u32 | 387, `src/matched/ov/ov093_func_021CC2C0.c:68` | the message shown |

### `battle.item.cure` (0x021C557C, ov093, `BTL_SERVER_FLOW_CureMajorStatusWithMessage`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `battle` | ptr | call argument `p`, `src/matched/ov/ov093_func_021C557C.c:28` | the battle state |
| `mon` | ptr | call argument `a`, `src/matched/ov/ov093_func_021C557C.c:28` | the Pokemon cured |
| `poke_id` | u8 | `BTL_POKEPARAM_GetPokeId(a)`, `src/matched/ov/ov093_func_021C557C.c:30` | the cured mon's identity |
| `status_kind` | int | call argument `k`, `src/matched/ov/ov093_func_021C557C.c:28`, `:35` | which status is named in the message |
| `with_message` | bool | call argument `flag`, `src/matched/ov/ov093_func_021C557C.c:34` | whether a message is pushed |
| `cleared` | bool | `BTL_POKEPARAM_ClearMajorStatus(a)`, `src/matched/ov/ov093_func_021C557C.c:31` | that the status was cleared |
| `message_id` | u32 | 0x138, 0x126 or 0x156, `src/matched/ov/ov093_func_021C557C.c:37-43` | the message shown |

### `battle.item.check` (0x021CBFA0, ov093, `BTL_SERVER_FLOW_ItemEffectCheckSlotMessage`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `battle` | ptr | call argument `p`, `src/matched/ov/ov093_func_021CBFA0.c:36` | the battle state |
| `mon` | ptr | call argument `q`, `src/matched/ov/ov093_func_021CBFA0.c:36` | the Pokemon checked |
| `poke_id` | u8 | `BTL_POKEPARAM_GetPokeId(q)`, `src/matched/ov/ov093_func_021CBFA0.c:41` | the mon's identity |
| `in_second_pair` | u8 | `BTL_MAIN_IsPokeIdInSecondPair(poke_id)`, `src/matched/ov/ov093_func_021CBFA0.c:41` | which side of the field |
| `message_id` | u32 | 0x88, `src/matched/ov/ov093_func_021CBFA0.c:45` | the message shown |
| `matched` | bool | the return of `func_021CBFF8`, `src/matched/ov/ov093_func_021CBFA0.c:43` | whether the message fires |

### `battle.bag.add` (0x021B9090, ov093, `BTL_MAIN_AddItemToBag`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `battle_main` | ptr | call argument `p`, `src/matched/ov/ov093_func_021B9090.c:20` | the battle main object |
| `side` | u32 | call argument `k`, compared to `p[0x46c]`, `src/matched/ov/ov093_func_021B9090.c:20` | the owner the item returns to |
| `item_id` | u32 | call argument `x`, `src/matched/ov/ov093_func_021B9090.c:20` | the item returned |
| `bag` | ptr | `*(u8 **)p->f0`, `src/matched/ov/ov093_func_021B9090.c:20` | the owner's bag |
| `bag_key` | u16 | `p->f468`, `src/matched/ov/ov093_func_021B9090.c:20` | the bag allocation key |
| `local_side` | u8 | `q[0x23] == 0` gate, `src/matched/ov/ov093_func_021B9090.c:20` | whether the add is for the local player |

### `battle.bag.sub` (0x021B90BC, ov093, `BTL_MAIN_SubItemFromBag`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `battle_main` | ptr | call argument `p`, `src/matched/ov/ov093_func_021B90BC.c:20` | the battle main object |
| `side` | u8 | call argument `k`, compared to `p[0x46c]`, `src/matched/ov/ov093_func_021B90BC.c:20` | the owner the item leaves |
| `item_id` | u32 | call argument `x`, `src/matched/ov/ov093_func_021B90BC.c:20` | the item taken |
| `bag` | ptr | `*(u8 **)p->f0`, `src/matched/ov/ov093_func_021B90BC.c:20` | the owner's bag |
| `bag_key` | u16 | `p->f468`, `src/matched/ov/ov093_func_021B90BC.c:20` | the bag allocation key |
| `local_side` | u8 | `q[0x23] == 0` gate, `src/matched/ov/ov093_func_021B90BC.c:20` | whether the take is for the local player |

## Shops

### `shop.start` (0x021B2FD8, ov021, `SCRCMD_SHOP_StartShop`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `shop_id` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B2FD8.c:95` | which shop is opened |
| `shop_kind` | u8 | `w->b2`, set from `v`, `src/matched/ov/ov021_func_021B2FD8.c:112-133` | the shop category a handler branches on |
| `shop_sub` | u8 | `w->b3`, set from `v`, `src/matched/ov/ov021_func_021B2FD8.c:131` | the second category byte |
| `work` | ptr | `func_02030734(4, 0x214, 1, ...)`, `src/matched/ov/ov021_func_021B2FD8.c:103` | the shop task block, the context for `shop.pay` |
| `bag` | ptr | `GAME_DATA_GetMyItem(s)`, `src/matched/ov/ov021_func_021B2FD8.c:108` | the buyer's bag |
| `misc_save` | ptr | `GAME_DATA_GetMiscSave(s)`, `src/matched/ov/ov021_func_021B2FD8.c:106` | the money block |
| `trainer_record` | ptr | `GAME_DATA_GetTrainerRecord(s)`, `src/matched/ov/ov021_func_021B2FD8.c:110` | the buyer's record |
| `save_block_3a` | ptr | `func_020071CC(save_control, 0x3a)`, `src/matched/ov/ov021_func_021B2FD8.c:109` | the shop's save block |
| `msg_bg` | ptr | `FIELDMAP_GetMsgBg(field_map)`, `src/matched/ov/ov021_func_021B2FD8.c:101` | the message background |
| `union_info` | ptr | `GAME_DATA_GetSecondUnionInfo(s)`, `src/matched/ov/ov021_func_021B2FD8.c:102` | the union room data |
| `field_map` | ptr | `GAMESYSTEM_GetFieldMap(t)`, `src/matched/ov/ov021_func_021B2FD8.c:99` | the map the shop is on |
| `task` | ptr | `SCRCMD_PROC_FLD_StepShopTask`, `src/matched/ov/ov021_func_021B2FD8.c:134` | the step function that later frees the block |
| `facility_enter` | bool | `GAME_BEACON_SEARCH_SetFacilityEnter()` when `v` is not 0xFE or 0xFD, `src/matched/ov/ov021_func_021B2FD8.c:136` | that a facility entry was recorded |
| `owner` | ptr | the local player, `GAME_DATA_GetMyItem` | attributes the purchase |

### `shop.pay` (0x021B3568, ov021, `SCRCMD_PROC_FLD_PayPrice`)

It is a block candidate (`docs/mod-events.md`, section 2). The struct is
`Obj05049` (`src/matched/include/Obj05049.h`).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `work` | ptr | call argument `w`, the shop block, `src/matched/ov/ov021_func_021B3568.c:32` | the shop paying |
| `price` | u32 | call argument `b`, `src/matched/ov/ov021_func_021B3568.c:32`, `:35` | the amount paid |
| `pays_money` | bool | `w[0x163] == 0`, `src/matched/ov/ov021_func_021B3568.c:34` | whether money or score is spent |
| `misc_save` | ptr | `w->fC`, `src/matched/ov/ov021_func_021B3568.c:35` | the money block |
| `score_record` | ptr | `w->f18`, `src/matched/ov/ov021_func_021B3568.c:38` | the profile score block |
| `stat_record` | ptr | `w->f14`, `src/matched/ov/ov021_func_021B3568.c:36` | the record the spend is counted in |
| `money_before` | u32 | a call the port can make, `BEACON_GetPoints(misc_save)` 0x0200C274 | the money before the pay |
| `money_after` | u32 | `money_before` minus `price` | the money after the pay |
| `score_subtracted` | u32 | `GDS_PROFILE_SubScoreRecTotal(score_record, b)`, `src/matched/ov/ov021_func_021B3568.c:38` | the score spent |
| `counter` | u32 | `func_02008DAC(stat_record, 0x16 or 0x22, b)`, `src/matched/ov/ov021_func_021B3568.c:36`, `:39` | the purchase counter |
| `commit` | u32 | `func_02008DF0(stat_record, 0x15)`, `src/matched/ov/ov021_func_021B3568.c:41` | the commit step |
| `block` | bool | the return contract, `docs/mod-events.md`, section 2 | a block forbids the pay |
| `owner` | ptr | the local player, via the shop context | attributes the purchase |

## Money

The money is the misc save block 0x34, offset +0x00
(`docs/mod-events.md`, section 4).

### `money.set` (0x0215C558, ov010, `SCRCMD_PROC_SetMiscValue`)

The name says set, the code adds. `BEACON_AddPoints` is called.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `amount` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov010_func_0215C558.c:32` | the value added |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(b)`, `src/matched/ov/ov010_func_0215C558.c:29` | the owner record |
| `misc_save` | ptr | `GAME_DATA_GetMiscSave(p)`, `src/matched/ov/ov010_func_0215C558.c:33` | the money block |
| `player_work` | ptr | `GAME_DATA_GetMyPlayerWork(p)`, `src/matched/ov/ov010_func_0215C558.c:31` | the player record |
| `operation` | u32 | `BEACON_AddPoints(misc_save, k)`, `src/matched/ov/ov010_func_0215C558.c:33` | that the value is added |
| `money_before` | u32 | a call the port can make, `BEACON_GetPoints` 0x0200C274 | the money before |
| `money_after` | u32 | `money_before` plus `amount`, capped | the money after |
| `cause` | u32 | the command context `a` | which script changed the money |

### `money.add` (0x0215C584, ov010, `SCRCMD_PROC_AddMiscValue`)

The name says add, the code subtracts. `BEACON_SubPoints` is called.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `amount` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov010_func_0215C584.c:32` | the value subtracted |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(b)`, `src/matched/ov/ov010_func_0215C584.c:29` | the owner record |
| `misc_save` | ptr | `GAME_DATA_GetMiscSave(p)`, `src/matched/ov/ov010_func_0215C584.c:33` | the money block |
| `player_work` | ptr | `GAME_DATA_GetMyPlayerWork(p)`, `src/matched/ov/ov010_func_0215C584.c:31` | the player record |
| `operation` | u32 | `BEACON_SubPoints(misc_save, k)`, `src/matched/ov/ov010_func_0215C584.c:33` | that the value is subtracted |
| `money_before` | u32 | a call the port can make, `BEACON_GetPoints` 0x0200C274 | the money before |
| `money_after` | u32 | `money_before` minus `amount` | the money after |
| `cause` | u32 | the command context `a` | which script changed the money |

### `money.check` (0x0215C5B0, ov010, `SCRCMD_PROC_CheckMiscValueReached`)

A state read.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `threshold` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov010_func_0215C5B0.c:32` | the value tested against |
| `game_data` | ptr | `SCRCMD_WORK_GetGameData(b)`, `src/matched/ov/ov010_func_0215C5B0.c:33` | the owner record |
| `misc_save` | ptr | `GAME_DATA_GetMiscSave(q)`, `src/matched/ov/ov010_func_0215C5B0.c:35` | the money block |
| `money` | u32 | `BEACON_GetPoints(misc_save)`, `src/matched/ov/ov010_func_0215C5B0.c:35` | the current money |
| `reached` | bool | `v <= points`, `src/matched/ov/ov010_func_0215C5B0.c:35` | the answer a handler branches on |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov010_func_0215C5B0.c:31` | where the script reads the answer |

## Move events

### `move.learn` (0x021C360C, ov021, `SODATEYA_LearnMove`)

The learn used by a TM and by a tutor. It writes a free slot or pushes.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `mon` | ptr | call argument `a`, `src/matched/ov/ov021_func_021C360C.c:32` | the Pokemon that learns |
| `move_id` | u32 | call argument `b`, `src/matched/ov/ov021_func_021C360C.c:32` | the move learned |
| `knows_move` | bool | `SODATEYA_MonKnowsMove(a,b)`, `src/matched/ov/ov021_func_021C360C.c:37` | whether the learn is skipped |
| `free_slot_ids` | u32[4] | `d_021DA8C8`, `src/matched/ov/ov021_func_021C360C.c:34` | the four slot field ids tested |
| `slot` | u32 | the `i` written by `POKE_TOOL_SetMoveSlot(a, b, i)`, `src/matched/ov/ov021_func_021C360C.c:40` | which slot the move lands in |
| `pushed` | bool | `POKE_TOOL_PushMove(a, b)` when no slot is free, `src/matched/ov/ov021_func_021C360C.c:44` | that the move went through the push path |
| `moves_before` | u16[4] | a call the port can make, `POKE_TOOL_GetPartyValue(mon, 0x36+i, 0)` 0x02017E1C | the four moves before the learn |
| `cause` | u32 | context: the tutor flow or the TM flow | which flow taught it |
| `owner` | ptr | the mon's owner; the party in the field | attributes the learn |

### `move.push` (0x020180F4, arm9, `POKE_TOOL_PushMove`)

An override point. It is a tail thunk that drops both arguments at the callee,
so the port must capture them at the call.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `mon` | ptr | call argument `p`, `src/matched/arm9/func_020180F4.c:30` | the Pokemon pushed into |
| `move_id` | int | call argument `move`, `src/matched/arm9/func_020180F4.c:30` | the move pushed |
| `target` | ptr | `POKE_TOOL_PushBoxMove` 0x020180FC, `src/matched/arm9/func_020180F4.c:28-32` | the callee that sees nothing of the arguments |
| `method` | u32 | context: the caller is `SODATEYA_LearnMove` or `SCRCMD_PROC_SetMonMove` | why the move was pushed |
| `owner` | ptr | the mon's owner | attributes the push |

### `move.slot` (0x020181F4, arm9, `POKE_TOOL_SetMoveSlot`)

An override point. It is a tail thunk to `POKE_TOOL_SetBoxMoveSlot`
0x020181FC, which writes the move, clears the PP-up and sets the fresh PP.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `mon` | ptr | call argument `p`, `src/matched/arm9/unit_020181A0.c:77` | the Pokemon written |
| `move_id` | u16 | call argument `move`, `src/matched/arm9/unit_020181A0.c:77` | the move written |
| `slot` | u32 | call argument `slot`, `src/matched/arm9/unit_020181A0.c:77` | the slot written |
| `pp_up_cleared` | bool | `POKE_TOOL_SetBoxValue(p, 0x3E+slot, 0)`, `src/matched/arm9/unit_020181A0.c:87` | that the PP-up count is reset |
| `pp_set` | u32 | `WAZA_TOOL_GetMaxPP(move, 0)` or 0, `src/matched/arm9/unit_020181A0.c:88-91` | the new PP |
| `move_before` | u16 | a call the port can make, `POKE_TOOL_GetPartyValue(mon, 0x36+slot, 0)` | the move replaced |
| `owner` | ptr | the mon's owner | attributes the write |

### `move.set` (0x0215BD2C, ov010, `SCRCMD_PROC_SetMonMove`)

A script command that sets, pushes or deletes a move. A move slot past 4 pushes;
a zero move deletes and shifts the later moves down.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `party_slot` | u16 | first `SCRIPT_WORK_ReadWorkValue(c,vm)`, `src/matched/ov/ov010_func_0215BD2C.c:54` | which party member |
| `move_slot` | u16 | second `SCRIPT_WORK_ReadWorkValue(c,vm)`, `src/matched/ov/ov010_func_0215BD2C.c:55` | which move slot |
| `move_id` | u16 | third `SCRIPT_WORK_ReadWorkValue(c,vm)`, `src/matched/ov/ov010_func_0215BD2C.c:56` | the move written |
| `mon` | ptr | `SCRCMD_PROC_GetPartyMember(vm, slot, &mon)`, `src/matched/ov/ov010_func_0215BD2C.c:57` | the Pokemon written |
| `method` | u32 | the branch, `moveSlot > 4` push, `move == 0` delete, else set, `src/matched/ov/ov010_func_0215BD2C.c:61-82` | which action was taken |
| `shifted_moves` | u16[3] | `POKE_TOOL_GetPartyValue(mon, i+0x37, 0)` loop, `src/matched/ov/ov010_func_0215BD2C.c:67-80` | the moves shifted down on a delete |
| `param_before` | u32 | `POKE_TOOL_DecodePartyParam(mon)`, `src/matched/ov/ov010_func_0215BD2C.c:60` | the record state before the write |
| `param_after` | u32 | `POKE_TOOL_EncodePartyParam(mon, saved)`, `src/matched/ov/ov010_func_0215BD2C.c:84` | the record state after the write |
| `found` | bool | `SCRCMD_PROC_GetPartyMember(...) == 0` returns early, `src/matched/ov/ov010_func_0215BD2C.c:57` | whether the slot resolved |
| `cause` | u32 | the command context `c` | which script set it |

### `move.get` (0x0215BCE8, ov010, `SCRCMD_PROC_GetMonMove`)

A state read.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `party_slot` | u32 | first `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `src/matched/ov/ov010_func_0215BCE8.c:36` | which party member |
| `move_slot` | u32 | second `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `src/matched/ov/ov010_func_0215BCE8.c:37` | which move slot |
| `mon` | ptr | `SCRCMD_PROC_GetPartyMember(a1, k, &v)`, `src/matched/ov/ov010_func_0215BCE8.c:38` | the Pokemon read |
| `move_id` | u16 | `POKE_TOOL_GetPartyValue(v, p + 0x36, 0)`, `src/matched/ov/ov010_func_0215BCE8.c:39` | the move at the slot |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a0,a1)`, `src/matched/ov/ov010_func_0215BCE8.c:35` | where the script reads the move |
| `found` | bool | `SCRCMD_PROC_GetPartyMember(...) == 1`, `src/matched/ov/ov010_func_0215BCE8.c:38` | whether the slot resolved; 0 when not |

### `move.relearn.list` (0x0216F948, ov010, `WAZA_OSHIE_CreateMoveList`)

Builds the list of level-up moves a mon could relearn.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `mon` | ptr | call argument `mon`, `src/matched/ov/ov010_func_0216F948.c:70` | the Pokemon the list is for |
| `species` | u16 | `POKE_TOOL_GetPartyValue(mon, 5, 0)`, `src/matched/ov/ov010_func_0216F948.c:84` | the species the learnset comes from |
| `form` | u8 | `POKE_TOOL_GetPartyValue(mon, 0x6F, 0)`, `src/matched/ov/ov010_func_0216F948.c:85` | the form the learnset comes from |
| `level` | u8 | `POKE_TOOL_GetPartyValue(mon, 0x9E, 0)`, `src/matched/ov/ov010_func_0216F948.c:86` | the level that gates the list |
| `current_moves` | u16[4] | `POKE_TOOL_GetPartyValue(mon, 0x36+i, 0)`, `src/matched/ov/ov010_func_0216F948.c:87-88` | the moves already known |
| `moves` | u16[26] | the `out` buffer, `src/matched/ov/ov010_func_0216F948.c:90`, `:111` | the relearnable moves |
| `count` | u32 | entries before the 0xFFFF terminator, `src/matched/ov/ov010_func_0216F948.c:92-113` | how many moves are offered |
| `heap_id` | u32 | call argument `heap`, `src/matched/ov/ov010_func_0216F948.c:89` | the list allocation |
| `owner` | ptr | the mon's owner | attributes the list |

### `move.relearn` (0x0215C89C, ov010, `SCRCMD_PROC_CallMoveRelearn`)

It opens the relearn screen. The move write happens later in ov173, not at this
event.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(c,vm)`, `src/matched/ov/ov010_func_0215C89C.c:98` | where the script reads the result |
| `party_index` | u32 | `SCRIPT_WORK_ReadWorkValue(c,vm)`, `src/matched/ov/ov010_func_0215C89C.c:99` | which party member |
| `mon` | ptr | `POKEPARTY_GetMember(party, idx)`, `src/matched/ov/ov010_func_0215C89C.c:100` | the Pokemon the screen is for |
| `session` | ptr | `GAME_DATA_GetMyStatus(w)`, `src/matched/ov/ov010_func_0215C89C.c:101` | the status record stored in the record |
| `moves` | ptr | `WAZA_OSHIE_CreateMoveList(mon, 4)`, `src/matched/ov/ov010_func_0215C89C.c:102` | the relearnable list |
| `record` | ptr | `WAZA_OSHIE_Create(4)` with mon, sess, sys, moves, f19, `src/matched/ov/ov010_func_0215C89C.c:103-104` | the screen record |
| `callback` | ptr | `SCRCMD_PROC_OnMoveRelearnDone`, `src/matched/ov/ov010_func_0215C89C.c:108` | the done callback |
| `write_gap` | bool | no event fires at the ov173 write, `src/matched/ov/ov010_func_0215C89C.c:108` | that the payload cannot name the move written |

### `move.relearn.count` (0x0215BFD4, ov010, `SCRCMD_PROC_GetRelearnableMoveCount`)

A state read.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a0,a1)`, `src/matched/ov/func_0215BFD4.c:44` | where the script reads the count |
| `party_index` | u32 | `SCRIPT_WORK_ReadWorkValue(a0,a1)`, `src/matched/ov/func_0215BFD4.c:48` | which party member |
| `heap_id` | u16 | `SCRCMD_WORK_GetHeapId(a1)`, `src/matched/ov/func_0215BFD4.c:52` | the list allocation |
| `mon` | ptr | `SCRCMD_PROC_GetPartyMember(a0, a1, &v0)`, `src/matched/ov/func_0215BFD4.c:58` | the Pokemon read |
| `count` | u32 | `WAZA_OSHIE_IsListEntryValid(buf)`, `src/matched/ov/func_0215BFD4.c:66-68` | the count a handler branches on |
| `found` | bool | `SCRCMD_PROC_GetPartyMember(...) == 1`, `src/matched/ov/func_0215BFD4.c:60` | whether the slot resolved |

## TM and tutor

### `tm.tutor.select` (0x021B61E8, ov021, `SCRCMD_POKEMON_FLD_SelectTutorMon`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `mode` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B61E8.c:45` | which tutor group |
| `candidate_mask` | u8 | `SCRCMD_POKEMON_FLD_FindTutorCandidates(a,b,mode,&c,0)`, `src/matched/ov/ov021_func_021B61E8.c:50`, `src/matched/ov/ov021_func_021B6048.c:36` | the party members that can be tutored |
| `candidate_result` | int | return of `FindTutorCandidates`, 0, 1 or 2, `src/matched/ov/ov021_func_021B6048.c:65-69` | none, some or all candidates |
| `tutor_value` | u32 | the switch on `mode`, 0xfffe, 0xfffd, 0x1b2 or 0xa5, `src/matched/ov/ov021_func_021B61E8.c:53-66` | the tutor's selector |
| `out_ptr1` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B61E8.c:46` | the first script output |
| `out_ptr2` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B61E8.c:47` | the second script output |
| `party` | ptr | `GAME_DATA_GetMyParty` inside `FindTutorCandidates`, `src/matched/ov/ov021_func_021B6048.c:45` | the owner's party |
| `select_event` | ptr | `EVENT_POKE_STATUS_CreatePokeSelectMoveEvent`, `src/matched/ov/ov021_func_021B61E8.c:67` | the selection event created |

### `tm.tutor.move` (0x021B6124, ov021, `SCRCMD_POKEMON_FLD_GetMonTutorMove`)

A state read.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `tutor_sel` | u32 | first `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B6124.c:37` | the tutor selector |
| `party_index` | u32 | second `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B6124.c:38` | which party member |
| `mon` | ptr | `SCRCMD_PROC_GetPartyMember(b, y, &rec)`, `src/matched/ov/ov021_func_021B6124.c:40` | the Pokemon read |
| `move_id` | u16 | `SCRCMD_POKEMON_FLD_GetTutorMoveId(rec, x)`, `src/matched/ov/ov021_func_021B6124.c:41` | the tutor move |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B6124.c:39` | where the script reads the move |
| `found` | bool | `SCRCMD_PROC_GetPartyMember(...) == 1`, `src/matched/ov/ov021_func_021B6124.c:40` | whether the slot resolved |

### `tm.tutor.status` (0x021B6164, ov021, `SCRCMD_POKEMON_FLD_CheckMonTutorStatus`)

A state read.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `tutor_sel` | u32 | first `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B6164.c:44` | the tutor selector |
| `party_index` | u32 | second `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B6164.c:45` | which party member |
| `mon` | ptr | `SCRCMD_PROC_GetPartyMember(b, y, &rec)`, `src/matched/ov/ov021_func_021B6164.c:48` | the Pokemon checked |
| `in_group` | bool | `SCRCMD_POKEMON_FLD_CheckTutorGroup(rec, x)`, `src/matched/ov/ov021_func_021B6164.c:50` | whether the mon can be tutored |
| `knows_move` | bool | `SCRCMD_PROC_CheckMonKnowsMove(rec, GetTutorMoveId(rec, x))`, `src/matched/ov/ov021_func_021B6164.c:52` | whether the mon already knows it |
| `friendship` | u32 | `POKE_TOOL_GetPartyValue(rec, 9, 0)`, `src/matched/ov/ov021_func_021B6164.c:54` | the mon's friendship |
| `friendship_required` | int | `SCRCMD_POKEMON_FLD_GetTutorFriendship(x)`, `src/matched/ov/ov021_func_021B6164.c:54`, `src/matched/ov/func_021B5FB4.c:25` | the friendship needed |
| `status` | u16 | 0 ready, 1 no group, 2 low friendship, 3 knows, `src/matched/ov/ov021_func_021B6164.c:47-58` | the answer a handler branches on |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B6164.c:46` | where the script reads the status |

### `tm.tutor.id` (0x021B5F24, ov021, `SCRCMD_POKEMON_FLD_GetTutorMoveId`)

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `mon` | ptr | call argument `a`, `src/matched/ov/ov021_func_021B5F24.c:24` | the Pokemon tested |
| `tutor_sel` | int | call argument `sel`, `src/matched/ov/ov021_func_021B5F24.c:24` | the tutor selector |
| `move_id` | u32 | 0x152, 0x133, 0x134, 0x1b2, 0x208, 0x207 or 0x206, `src/matched/ov/ov021_func_021B5F24.c:27-46` | the tutor move |
| `compat` | u32 | `POKE_TOOL_CheckTutorCompat(a, v)`, `src/matched/ov/ov021_func_021B5F24.c:28-43` | the compatibility test that picked the move |

### `tm.type` (0x021B62C8, ov021, `SCRCMD_POKEMON_FLD_GetMonTypeForTm`)

A state read. It resolves the TM id 0x151, then the mon's compatibility and
Hidden Power type.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `h` | `BwEvent` | the port's common header | identity and size |
| `party_index` | u32 | `SCRIPT_WORK_ReadWorkValue(a,b)`, `src/matched/ov/ov021_func_021B62C8.c:43` | which party member |
| `tm_id` | u32 | `FILE_UTIL_GetId(0x151)`, `src/matched/ov/ov021_func_021B62C8.c:44` | the TM item id |
| `mon` | ptr | `SCRCMD_PROC_GetPartyMember(b, v, &x)`, `src/matched/ov/ov021_func_021B62C8.c:45` | the Pokemon read |
| `compat` | u32 | `POKE_TOOL_CheckTmCompat(x, w)`, `src/matched/ov/ov021_func_021B62C8.c:47` | whether the mon can take the TM |
| `type` | u16 | `POKE_TOOL_GetHiddenPowerType(x)`, `src/matched/ov/ov021_func_021B62C8.c:50` | the type; 0xFFFF when not compatible |
| `out_ptr` | ptr | `SCRIPT_WORK_ReadWorkPointer(a,b)`, `src/matched/ov/ov021_func_021B62C8.c:42` | where the script reads the type |

## Fields a mod would want that cannot be obtained

- **An item name.** No event carries a name. The names are in the game's message
  table, not reachable at these call sites. A mod that wants a name needs a
  name table the port builds.
- **A move name.** Same as an item name; the move events carry the id only.
- **The concrete mystery gift.** `item.gift.mystery` returns a mode result and
  the gift kind, not the item or species. The content is inside the card buffer
  and the handler table `d_021D9978` (`src/matched/ov/ov021_func_021B6D98.c:25`).
  The port can copy the card, but decoding it needs the card format.
- **The HP, status and stat identity of the party field ids.** `item.heal.party`
  uses 0xA0, 0xA1, 0xA9 and 0x9D (`src/matched/arm9/unit_0201B9F0.c:80-98`).
  aletheia does not name which is HP or a stat (`docs/mod-events.md`, section
  4), so the port can state the ids but not "HP restored" as a number.
- **The shop's item list.** `shop.start` carries the shop id, not the list. The
  list is loaded later by `SCRCMD_PROC_FLD_StepShop` 0x021B35A4
  (`src/matched/ov/ov021_func_021B3158.c:110`).
- **The move the relearner writes.** `move.relearn` opens the screen; the write
  is in the ov173 proc, and no event fires there
  (`src/matched/ov/ov010_func_0215C89C.c:108`).
- **The postman record's origin.** `item.gift.postman` receives the container
  pointer, not the record that filled it.
- **The money getter.** `BEACON_GetPoints` (0x0200C274) is the read used by
  `money.check` and `shop.pay`, but no matched source is in the tree. The port
  calls the DS address, not a matched wrapper.

## Thinnest payloads

- `item.machine` (0x021AF750): one argument, a boolean return, no VM context
  (`src/matched/ov/func_021AF750.c:26-28`).
- `item.partyuse` (0x021AF71C) and `item.pocket` (0x021AF6E8): an item id and
  one property.
- `money.check` (0x0215C5B0): a threshold and a boolean.
- `battle.item.check` (0x021CBFA0): a mon and a message id.
- `move.push` (0x020180F4) and `move.slot` (0x020181F4): tail thunks that drop
  their arguments at the callee, so the payload exists only at the call.
- `item.has` (0x021AF634) and `item.canadd` (0x021AF5E4): an item id, a count
  and a boolean.
