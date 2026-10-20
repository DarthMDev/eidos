# Events and state

This document owns the event and state API a mod binds to: the stable events,
the handler contract, the order several mods run in, the state accessors and
the raw hooks. `docs/mod-payloads.md` owns the fields of each payload,
`docs/mod-abi.md` the frozen surface and the version rule,
`docs/mod-modes.md` the challenge runs the events are designed for, and
`docs/mod-interface.md` the slot that carries every hook.

## 1. The stable events

The stable tier is the API a mod is written against (`docs/modding.md`,
section 2). Each event is a game moment with a name and a payload. The names
and the payload layouts are frozen with the interface version
(`docs/mod-abi.md`).

The "Game function" column is the port's binding: the slot the event fires
from (`mod/declared_set.txt`, fourth column). The port may move a binding when
aletheia restructures the code, and a mod does not notice. "Refuse" is the
outcome the port produces when a handler refuses; "-" means the event cannot be
refused. "Modify" names the payload fields a handler may change before the
game uses them; "-" means none. A *locate* note names a path of the event
whose binding is still to be read.

Every refused outcome below was read from the game function's callers: what
they do with its result, and what state they expect it to leave. Where a
refusal would leave the game telling the player something false, the event is
not refusable, and a mod reacts after the fact through the state calls of
section 4.

An event fires when the game is about to act, before the bound function runs,
except where the row says "the return of": those fire after the function
returns, with its result in the payload. A binding marked "inside F" fires
only while F is running, and one marked "not inside F" never does (the
binding's scope, `docs/mod-implementation.md`, section 1).

### Session

| Event | Game function | Region | Refuse | Modify |
|---|---|---|---|---|
| `game.new` | `EVENT_MAPCHANGE_NewGameStartEvent` 0x02183C9C | ov020 | - | - |
| `save.begin` | `SAVE_CONTROL_StartSave` 0x02007150 | arm9 | - | - |
| `save.done` | `SAVE_CONTROL_MainSave` 0x02007168 | arm9 | - | - |
| `save.load` | `GAME_DATA_LoadFromSave` 0x02012C18 | arm9 | - | - |
| `game.cleared` | `EVENT_GAMECLEAR_Create` 0x0215F8A8, the ending: Hall of Fame (ov180), credits (ov181), save screen (ov205) | ov010 | - | - |

`save.begin` is not refusable: a save is a start step followed by
`SAVE_CONTROL_MainSave` steps, driven by about ten callers (the save menu,
scripts, link and Wi-Fi code), and none of them has a path for a start that
does not happen.

### World

| Event | Game function | Region | Refuse | Modify |
|---|---|---|---|---|
| `route.change` | `FIELD_MAP_CHANGE_SetupZone` 0x021643E8 | ov010 | - | - |
| `map.warp` | `EVENT_MAPCHANGE_ChangeFieldEvent` 0x021841C0 | ov020 | - | destination |
| `player.step` | `FIELD_EVENT_CHECK_CheckStepEvents` 0x0218A9B0 | ov021 | - | - |
| `trainer.spotted` | `EVENT_TRAINER_EYE_Check` 0x021AE0CC | ov021 | the trainer does not see the player (the function returns no event) | - |
| `npc.talk` | `FIELD_EVENT_CHECK_SearchGridFrontMmdl` 0x0218B4B0; on rails `FIELD_EVENT_CHECK_SearchRailFrontMmdl` 0x0218B53C | ov021 | no person is found ahead; the A press goes on to the sign and tile checks | - |
| `menu.open` | `EVENT_FIELD_MENU_Create` 0x0215FC40 (the X-button menu event) and `EVENT_FIELD_MENU_CreateForZone` 0x0215FCE0 | ov010 | the menu does not open (no event is returned) | - |
| `menu.build` | `FIELD_MENU_Create` 0x021A836C | ov021 | - | entries (add, hide) |
| `field.move` | `FIELD_SKILL_CHECK_CallHandler` 0x021644C4 | ov010 | the field move shows as unusable | - |
| `pc.open` | `SCRCMD_PROC_FLD_CallOv170Event` 0x021B4D60, the script command that opens the box screen (the PC, and the battle box with its kind 4) | ov021 | the PC does not open; the script reads a plain exit | - |

`field.move` fires when the game checks which of the ten field moves can be
used (`FIELD_SKILL_CHECK_FillUseFlags` asks for each), so a refusal removes
the move from the choices before the player picks it.

### Encounters

| Event | Game function | Region | Refuse | Modify |
|---|---|---|---|---|
| `encounter.wild` | the return of `FIELD_ENCOUNT_CreateZoneWildMons` 0x021A954C; roamers the return of `FIELD_ENCOUNT_PickRoamer` 0x021A957C | ov021 | no battle; the player keeps walking; the payload names the area, the method (grass, surf, fishing, shaking grass) and the slot | species, level, shiny |
| `encounter.static` | `EVENT_BATTLE_CreateScriptedBattle` 0x0216E7C8 | ov010 | - | species, level |
| `encounter.trainer` | `FIELD_ENCOUNT_SetupTrainerBattle` 0x021AA5CC | ov021 | - | - |

`FIELD_ENCOUNT_CreateZoneWildMons` fills the wild Pokemon after the encounter
roll has passed. A refused result is a count of 0, which
`FIELD_ENCOUNT_CheckWildEncount` already treats as no encounter; its callers
(walking, the shaking-grass spots, Sweet Scent) all handle that result. A
refused roamer is a `FIELD_ENCOUNT_PickRoamer` result of no roamer: the
area's normal encounter follows and fires its own `encounter.wild`.

### Pokemon

| Event | Game function | Region | Refuse | Modify |
|---|---|---|---|---|
| `mon.create` | `POKE_TOOL_CreateEx` 0x02017430; personality `POKE_TOOL_CreatePersonalRnd` 0x020186C8 | arm9 | - | species, level, shiny, nature |
| `mon.receive` | catch `EVENT_BATTLE_RETURN_Main` 0x021B9614 (ov092, the store step); gifts `SCRCMD_PROC_AddMonToParty` 0x0215C510, `SCRCMD_PROC_AddMonToBox` 0x0215C4C8, `SCRCMD_PROC_AddEggToParty` 0x0215C2D0 (ov010); `SCRCMD_POKEMON_FLD_ReceiveGiftMon` 0x021B6F18, daycare egg `SODATEYA_ReceiveEgg` 0x021C32F4 (ov021); in-game trade `FLD_TRADE_CreateEvent` 0x021CC500 (ov021) | - | - | - |
| `mon.nickname` | `EVENT_NAMEIN_Create` 0x021CD2A4 (ov021); after a catch `ZUKAN_NICKNAME_Create` 0x0220B55C (ov207) | - | - | force the prompt |
| `mon.evolve` | `SHINKA_CHECK_CheckEvolution` 0x0201B2B0 | arm9 | no evolution starts; an evolution stone has no effect | target species |
| `mon.exp` | the return of `BTL_SERVER_FLOW_CalcPartyExpShares` 0x021CB2D4 (ov093); daycare `SODATEYA_GrantExp` 0x021C3530 (ov021) | - | - | the amount for each Pokemon |
| `mon.levelup` | `BTL_POKEPARAM_AddExp` 0x021D69B8, inside `BTL_SERVER_FLOW_ApplyPartyExp` 0x021C5350 (the level crossing) | ov093 | - | - |
| `move.learn` | level-up moves `POKE_TOOL_GetLevelUpMove` 0x02018328 (arm9; battle, party screen, evolution); daycare `SODATEYA_LearnMove` 0x021C360C (ov021); TMs, tutors and the relearner *locate*: the party screen learns through `POKELIST_FindMoveSlot` 0x021BA5B4 and `POKELIST_ReplaceMoveSlot` 0x021BA5E0 (ov091), scripts through `SCRCMD_PROC_SetMonMove` 0x0215BD2C (ov010); the refused outcome there is still to read | - | the move is not learned; the payload names the move and the source | - |
| `mon.release` | `BOX2_MAIN_ClearSlot` 0x021DD314, whose only caller is the release step `BOX2_SEQ_StateMoveItemF` 0x021E81D0 | ov170 | the Pokemon stays; the box redraws it | - |
| `box.deposit` | `BOX2_MAIN_UpdateBoxSlots` 0x021DD530, a party Pokemon placed in a box | ov170 | the move is not applied; the box redraws both slots | - |
| `box.withdraw` | `BOX2_MAIN_UpdateBoxSlots` 0x021DD530, a box Pokemon placed in the party | ov170 | as `box.deposit` | - |
| `party.swap` | `POKEPARTY_SwapMembers` 0x0201AB8C, not inside `BOX2_MAIN_UpdateBoxSlots` | arm9 | the order does not change; the party screen redraws from the party | - |
| `mon.helditem` | `POKELIST_SetHeldItem` 0x021BA624 | ov091 | - | - |
| `egg.hatch` | `SCRCMD_POKEMON_FLD_StartEggHatch` 0x021B63D4 | ov021 | - | - |
| `trade.ingame` | `SCRCMD_POKEMON_FLD_StartTradeEvent` 0x021B654C | ov021 | the trade does not happen and the script goes on | - |

Notes from the callers:

- **`mon.receive`** fires after the Pokemon is stored, from the functions that
  give the player a Pokemon. `POKEPARTY_Add` is not a binding: it also builds
  trainer parties, wild battle parties and battle copies. The event is not
  refusable because each receive path prints its own "sent to the PC" or
  "received" text before or after storing; a mod that limits the party moves
  the Pokemon with a state call and says so with a message.
- **`mon.exp`** changes the amounts before the battle server records them, so
  the server, the battle display and the saved Pokemon agree. A level cap
  lowers the amount; refusing `mon.levelup` itself would leave the display
  showing a level the Pokemon does not have.
- **`move.learn`** at `POKE_TOOL_GetLevelUpMove`: the function scans the
  learnset and writes the move into the Pokemon. A refused move is skipped:
  the refusal advances the scan past the entry and returns the "already
  known" code (0xFFFE), which every caller treats as nothing learned.
- **The PC** applies every drop in `BOX2_MAIN_UpdateBoxSlots`, after the icon
  animation. An exchange between a box and the party fires both
  `box.deposit` and `box.withdraw`; refusing either refuses the exchange.
- **`mon.helditem`** is not refusable: each of the five callers prints its
  "was given" text after the call. A mod that forbids held items takes the item
  back with a state call.
- **`mon.evolve`**: the function is a check; its result 0 means no evolution
  in all ten callers (level-up, stones, trades).

### Battle

| Event | Game function | Region | Refuse | Modify |
|---|---|---|---|---|
| `battle.start` | `BTL_SETUP_Create` 0x02013090; formats `BTL_SETUP_SetupTrainer*` 0x02013610 to 0x02013734 | arm9 | - | format, parties, levels; the payload names the trainer ids and whether the battle is wild |
| `battle.action` | fight `BTL_CLIENT_IsMoveSelectionBlocked` 0x021CF5B0; run `BTL_CLIENT_GetEscapeBlockerForSide` 0x021CF918 (inside `BTL_CLIENT_StepSelectRun`); switch the return of `BTL_PICK_GetLast` 0x0689CB1C (ov095, inside `BTL_CLIENT_StepSelectSwitchTarget`) | ov093 | the choice is not accepted and the action menu stays; the payload names the action and, for fight, the move | - |
| `battle.item` | the return of `BTLV_CORE_GetSelectedItem` 0x021EA01C, inside `BTL_CLIENT_StepSelectItem` | ov093 | back to the action menu; the item stays in the bag and the turn is not spent | - |
| `battle.switch` | `BTL_SERVER_FLOW_ProcMemberIn` 0x021BD9D4 | ov093 | - | - |
| `battle.escape` | `BTL_SERVER_FLOW_TryEscape` 0x021BD60C | ov093 | the escape fails as a failed run: "Can't escape!" and the turn is spent | - |
| `battle.move` | `BTL_SERVER_FLOW_RunMoveUsedEvent` 0x021C1A10 | ov093 | - | - |
| `battle.damage` | `BTL_SERVER_FLOW_CalcMoveDamage` 0x021C1E74 | ov093 | - | the amount; the payload names attacker, target, move and the target's HP before |
| `battle.faint` | `BTL_SERVER_FLOW_ProcFaintedPoke` 0x021C4F64 | ov093 | - | - |
| `catch.attempt` | `BTL_SERVER_FLOW_TryThrowBall` 0x021CBA40 | ov093 | the ball is blocked as in a trainer battle: the block message shows, the ball stays in the bag, the turn is spent; the payload names the ball and the target's species and level | catch rate (`BTL_SERVER_FLOW_GetBallCatchRate` 0x021CBD48) |
| `catch.result` | the return of `BTL_SERVER_FLOW_TryThrowBall` | ov093 | - | - |
| `battle.end` | `EVENT_BATTLE_NotifyBattleEnd` 0x0216EE7C | ov010 | - | - |
| `whiteout` | `BTL_CLIENT_StepShowBlackoutMessages` 0x021D1960 (ov093); `SCRIPT_SYS_RunRecoverScript` 0x02158A20 (ov010) | - | - | - |

Notes from the callers:

- **Refusing before the turn.** The battle server runs an action after the
  turn order is set, so a refusal there always spends the turn. The
  selection-time bindings use the game's own checks: a blocked move is the
  path Taunt, Disable and Choice items take (the game's message, the menu
  stays, and a Pokemon with every move blocked uses Struggle); a blocked run
  is the trapped path ("Can't escape!"); a refused item or switch returns to
  the action menu as when the player backs out. The AI asks
  `BTL_CLIENT_IsMoveSelectionBlocked` too, so the payload names the side and a
  mod refuses only the side it means.
- **`catch.attempt`** uses the trainer-battle path of the same function:
  `BTL_SERVER_FLOW_TryThrowBall` returns 0 and pushes the block command
  (0x47), and its caller only takes the ball from the bag on 1. A mod that
  wants the ball and the turn kept refuses `battle.item` for balls instead.
- **`battle.switch`** fires for every send-out, including after a faint and
  forced switches; it cannot be refused there, because the server has already
  committed the slot. A switch the player chooses is refused at
  `battle.action`.

### Items, money and healing

| Event | Game function | Region | Refuse | Modify |
|---|---|---|---|---|
| `item.add` | `MYITEM_SAVEDATA_AddItem` 0x02007E50 | arm9 | - | count |
| `item.use` | `PLIST_DEMO_CheckItemUsable` 0x021BDD78 | ov091 | the game's "it won't have any effect"; the item stays | - |
| `shop.buy` | `SCRCMD_PROC_FLD_PickQuantityOutcome` 0x021B3370 | ov021 | back to the item list; no money changes hands | price |
| `money.change` | `BEACON_AddPoints` 0x0200C278; `BEACON_SubPoints` 0x0200C29C | arm9 | - | the amount |
| `heal` | `SCRCMD_PROC_HealParty` 0x0215B5A8 (ov010, a centre or a script); `GPOWER_UseHpRestore` 0x0201104C (arm9, the HP-restore power) | - | the heal does not happen; the script's own text still plays | which Pokemon heal |

Notes from the callers:

- **`item.add`** is not refusable: its sixteen callers include the shop, which
  has taken the money before it adds the item. A count of 0 leaves the item
  out while the game's text says it was received.
- **`item.use`** binds to the check the party screen runs before it applies an
  item; the "no effect" message is the game's own path for an unusable item.
- **`shop.buy`** binds to the step that checks the money and the bag space
  after the player picks an item. The price is read there, so a changed price
  is the one the shop shows and charges. `SCRCMD_PROC_FLD_PayPrice` is not a
  binding: it runs after the "here you are" text and before the item is added.
- **`heal`**: `POKE_TOOL_HealParty` is not a binding; it also heals link,
  musical and infrared battle parties.

### Progress

| Event | Game function | Region | Refuse | Modify |
|---|---|---|---|---|
| `badge.get` | `BEACON_SetFlag` 0x0200C248 | arm9 | - | - |
| `flag.set` | `ZONEDATA_SetEventFlag` 0x02014314 | arm9 | - | - |
| `dex.register` | `ZUKAN_SAVE_RegisterSeen` 0x0200CFA0; `ZUKAN_SAVE_RegisterCaught` 0x0200CDE0 | arm9 | - | - |

### 1c. Corrected rows

aletheia corrected the function names in its matched corpus, and the port's
catalogue and declared set follow its per-packet manifests
(`layout/matched/**/pkt_*.json`), which name the function at each address. The
rows below were renamed in the port after that check. Each row names the
manifest packet, and the `port_renames.tsv` row when one reported it.

| Address | Region | Port's old name | Corrected name | Source |
|---|---|---|---|---|
| 0x02012BF8 | arm9 | `GAME_DATA_CheckSaveFlag` | `GAME_DATA_ContainsRecentId` | `pkt_02012BF8.json` (Wave 16 rename) |
| 0x0215AFE0 | ov010 | `SCRCMD_WORK_CmdSetZukanFlagA` | `SCRCMD_WORK_CmdEnableNationalDex` | `pkt_0215AFE0.json` (Wave 16 rename) |
| 0x0215B014 | ov010 | `SCRCMD_WORK_CmdSetZukanFlagB` | `SCRCMD_WORK_CmdSetZukanObtained` | `pkt_0215B014.json` (Wave 16 rename) |
| 0x0215C558 | ov010 | `SCRCMD_PROC_SetMiscValue` | `SCRCMD_PROC_AddMiscPoints` | `pkt_ov010_0215C558.json` (Wave 16 rename) |
| 0x0215C584 | ov010 | `SCRCMD_PROC_AddMiscValue` | `SCRCMD_PROC_SubMiscPoints` | `pkt_ov010_0215C584.json` (Wave 16 rename) |
| 0x0215F8A8 | ov010 | `SCRCMD_NETWORK_CreateCommBattleEvent` | `EVENT_GAMECLEAR_Create` | `port_renames.tsv` row 5; `pkt_0215F8A8.json` |
| 0x0215FC40 | ov010 | `SCRCMD_NETWORK_CreateBattleRecordLinkEvent` | `EVENT_FIELD_MENU_Create` | `port_renames.tsv` row 10; `pkt_ov010_0215FC40.json` |
| 0x0215FCE0 | ov010 | `SCRCMD_NETWORK_CreateBattleRecordLinkEventForZone` | `EVENT_FIELD_MENU_CreateForZone` | `port_renames.tsv` row 11; `pkt_ov010_0215FCE0.json` |
| 0x02164A2C | ov010 | `CALENDER_OverlaySequenceEvent` | `BEACON_VIEW_EVENT_Sequence` | `pkt_ov010_02164A2C.json` (Wave 16 rename) |
| 0x021B9090 | ov093 | `BTL_MAIN_AddItemToBag` | `BTL_MAIN_RemoveItemFromBag` | `port_renames.tsv` row 4; `pkt_ov093_021B9090.json` |
| 0x021B90BC | ov093 | `BTL_MAIN_SubItemFromBag` | `BTL_MAIN_AddItemToBag` | `pkt_ov093_021B90BC.json` (Wave 16 rename) |
| 0x021FBD88 | ov094 | `BTLV_EFFVM_CmdJumpIfNot` | `BTLV_EFFVM_CmdJumpIfCondCompare` | `pkt_ov094_021FBD88.json` (Wave 16 rename) |

The names `BTL_MAIN_AddItemToBag` and `BTL_MAIN_SubItemFromBag` were also
swapped: aletheia gives 0x021B9090 `BTL_MAIN_RemoveItemFromBag` and 0x021B90BC
`BTL_MAIN_AddItemToBag`.

Several `port_renames.tsv` rows name a function the port binds or cites but
aletheia has not renamed: `POKE_TOOL_GetLevelUpMove` 0x02018328,
`POKELIST_FindMoveSlot` 0x021BA5B4, `BOX2_MAIN_MoveMonIntoBox` 0x021DD810,
`BOX2_MAIN_MoveMonToParty` 0x021DD330 and `BOX2_SEQ_StateMoveItemF` 0x021E81D0
still carry those names in the manifest, so the port leaves them.

### The rows the newer corpus renamed under the port

The corpus aletheia builds from now (src/game at 4ee960765e) has renamed
twelve of the declared set's functions.  The declared set keeps the port's
spelling, because the spelling is the port's vocabulary for the hook
switch a row carries; the generators resolve a row by its address, so the
slot and the event fire either way.  The corpus's name is the one the
port's own C calls (the build's gamespell header redirects the port's
spelling to it).

| Address | Region | The port's spelling | The corpus's name |
|---|---|---|---|
| 0x0200C248 | arm9 | `BEACON_SetFlag` | `MISC_SetBadge` |
| 0x0200C278 | arm9 | `BEACON_AddPoints` | `MISC_AddMoney` |
| 0x0200C29C | arm9 | `BEACON_SubPoints` | `MISC_SubMoney` |
| 0x02018328 | arm9 | `POKE_TOOL_GetLevelUpMove` | `POKE_TOOL_LearnLevelUpMove` |
| 0x0215AC68 | ov010 | `SCRCMD_WORK_CmdSetMiscFlag` | `SCRCMD_WORK_CmdSetBadge` |
| 0x0215B328 | ov010 | `SCRCMD_WORK_CmdSetEventFlagOffset` | `SCRCMD_WORK_CmdSetTrainerDefeatedFlag` |
| 0x0215B350 | ov010 | `SCRCMD_WORK_CmdResetEventFlagOffset` | `SCRCMD_WORK_CmdClearTrainerDefeatedFlag` |
| 0x0215C558 | ov010 | `SCRCMD_PROC_AddMiscPoints` | `SCRCMD_PROC_AddMoney` |
| 0x0215C584 | ov010 | `SCRCMD_PROC_SubMiscPoints` | `SCRCMD_PROC_SubMoney` |
| 0x0216EF84 | ov010 | `EVENT_BATTLE_SetResultSound` | `EVENT_BATTLE_AddBattleRecord` |
| 0x021DD330 | ov170 | `BOX2_MAIN_MoveMonToParty` | `BOX2_MAIN_SwapBoxAndPartyMon` |
| 0x021DD810 | ov170 | `BOX2_MAIN_MoveMonIntoBox` | `BOX2_MAIN_ResetShayminFormInBox` |

## 2. The handler contract

A handler is one C function per event:

```c
typedef int (*BwEventHandler)(void *mod_state, BwEvent *event);

enum {
    BW_EVENT_CONTINUE = 0,   /* the game goes on */
    BW_EVENT_REFUSE   = 1    /* the port produces the event's refused outcome */
};
```

- **`mod_state`** is the mod's own pointer, the value its start-up entry
  returned (`docs/mod-abi.md`, section 5).
- **`event`** points at the event's payload, which starts with the common
  header `BwEvent` (`docs/mod-payloads.md`, section 0). It is valid only for
  the call.
- **Modifying.** A handler may write the payload fields the event's "Modify"
  column names. The port reads them back after the handlers have run and
  applies them before the game function continues. A write to any other field
  has no effect.
- **Refusing.** A handler returns `BW_EVENT_REFUSE` to refuse the action. The
  port then produces the outcome the event's "Refuse" column names, which
  leaves the game consistent: a refused catch keeps the ball and the turn, a
  refused withdrawal leaves the Pokemon in its box. The port never just skips
  the game function, because most game functions return a value or do work
  their callers depend on. A refusal at an event that cannot be refused is
  ignored and reported once in the log.
- **Telling the player.** A refusal does not show anything on its own. A
  handler that wants to say why calls the message service
  (`docs/mod-services.md`), which shows the text in the game's own window.

## 3. Several mods on one event

Mods load in ascending manifest `order`, and mods with equal `order` keep the
order they were added in (`sdk/vfs.h`, "The mod list").

- **Every handler runs.** All handlers bound to an event run, in load order.
  A refusal does not stop the later handlers.
- **Modifications chain.** Each handler sees the payload as the handlers
  before it left it. The last write to a field wins.
- **Any refusal refuses.** If one handler refuses, the action is refused.
- **Raw hooks are exclusive.** Two mods that both replace one raw hook: the
  later mod in the load order wins and the port reports the earlier one as
  shadowed (`docs/mod-overrides.md`). A `wrap` calls the original, so wraps
  from several mods nest in load order.

## 4. The state API

A mod reads and writes game state through accessor functions the port wraps,
never through the game's structs, so the game's layout can change behind them.
The port's wrapper is a fixed name and signature over the game accessor
(`docs/mod-abi.md`, section 1). The values the challenge runs use, and the game
accessor behind each:

| Value | Accessor |
|---|---|
| A party Pokemon's fields | `POKE_TOOL_GetPartyValue` 0x02017E1C, `POKE_TOOL_SetPartyValue` 0x02017E40; species 0x05, held item 0x06, exp 0x08, level 0x9E, HP 0xA0, max HP 0xA1, stats 0xA2 to 0xA6, types 0xAE and 0xAF |
| An in-battle Pokemon | `BTL_POKEPARAM_GetValue`; HP to zero `BTL_POKEPARAM_ZeroHp` 0x021D5FB8. The battle works on a copy, so a write to the party record waits for the battle to end |
| Party | `GAME_DATA_GetMyParty` 0x02012934, `POKEPARTY_GetMember` 0x0201AB28, `POKEPARTY_GetCount` 0x0201AA18, `POKEPARTY_RemoveMember` 0x0201A9C8 |
| Boxes | `BOX_SAVEDATA_GetPokemon` 0x02007A18, `BOX_SAVEDATA_AddPokemonToBox` 0x0200752C, `BOX_SAVEDATA_ClearPokemon` 0x02007664, `BOX_SAVEDATA_SetBoxName` 0x02007898 |
| Species data | `POKE_PERSONAL_GetParam` 0x0201AE1C (types, stats, catch rate), `POKE_TOOL_GetPersonalGender` 0x02019C7C |
| Shiny | `POKE_TOOL_IsShiny` 0x02017EAC |
| Dex | `ZUKAN_SAVE_IsCaught` 0x0200CED4 |
| Badges and money | `BEACON_GetFlag` 0x0200C234, `BEACON_CountFlags` 0x0200C258, `BEACON_GetPoints` 0x0200C274 |
| Battle style | `CONFIG_GetBattleStyle` 0x020083C8 |
| Position and map | `FIELD_PLAYER_GetPos` 0x021A3038, `GAME_DATA_GetPlayerZoneId` 0x02012AC8 |
| Moves between places | port calls built on the rows above: `mon_to_box` (a party Pokemon to the first free box slot: `BOX_SAVEDATA_AddPokemon` 0x020074D8, then `POKEPARTY_RemoveMember`), `mon_take_item` (the held item back to the bag: `POKE_TOOL_SetPartyValue` field 0x06, then `MYITEM_SAVEDATA_AddItem` 0x02007E50) |

Rules a mod relies on:

- **The battle works on a copy.** During a battle each Pokemon is a battle
  record built from the party record when the battle starts. A write to the
  party record has no effect on the battle and is overwritten when the battle
  writes its results back. A mod that changes a Pokemon in battle writes the
  battle record (`BTL_POKEPARAM_*`).
- **The game's later write wins.** The port does not arbitrate a write. A
  value the game recomputes is changed at the event the game derives it from
  (a "Modify" field), not by writing it afterwards.
- **Overlay data is reloaded.** An overlay's data is copied in again every time
  the overlay loads (`sdk/overlay_image.c`, `provider_load`). Save-block values
  persist; overlay data does not.
- **Moves wait for a quiet moment.** `mon_to_box` and `mon_take_item` change
  the party, which a battle, the party screen and the PC hold open. The port
  queues them and applies them when none of those is running, in the order
  the mod made them.
- **A mod's own data** lives in its state namespace, per save slot
  (`docs/mod-state.md`).

## 5. Raw hooks

The raw tier reaches any function in `mod/declared_set.txt` by
`(region, address)`, in `replace` or `wrap` mode (`docs/mod-overrides.md`;
the manifest grammar is `docs/mod-interface.md`, section 3). Its arguments are
the game's own, so a raw hook can break when aletheia restructures the code. A
mod that uses raw hooks states the port version it was built against, and the
port refuses to load it on another version.

The declared set has 219 rows: 57 carry a stable event, the rest
are raw hooks a mod or a stable event may need later. By region:

| Region | Rows |
|---|---|
| arm9 | 44 |
| ov009 | 3 |
| ov010 | 53 |
| ov018 | 8 |
| ov020 | 7 |
| ov021 | 54 |
| ov070 | 1 |
| ov091 | 4 |
| ov093 | 33 |
| ov094 | 6 |
| ov120 | 1 |
| ov170 | 4 |
| ov207 | 1 |

**Script command hooks.** The field script VM dispatches every script command
through one table (609 entries at `d_0217062C`, ov010, driven by
`SCRIPT_SYS_Main` 0x0215882C), and the battle effect VM through another (78
entries at 0x02209E88, ov094, created by `BTLV_EFFVM_Create` 0x021F93F4). The
port puts one hook on each dispatch, keyed by opcode, so a mod can observe or
replace any script command by its opcode without one declared row per command.
The opcode-to-handler map is aletheia's `tools/assets/scripts.py`
(`OPERAND_WIDTH`) for the field VM and `layout/data_objects.json` (object
`g_02209E88`) for the battle effect VM.

## Appendix: data tables

A data table is replaced through the data layer, as a whole file or a NARC
member (`docs/mods.md`); it takes no slot. These are the tables the challenge
runs and randomizers change. A mod that must also read a value at run time
uses the state accessor beside it (section 4).

| Table | What a mod replaces |
|---|---|
| Wild encounter table read for a zone and slot | Replace the species and level per zone and slot |
| Shaking grass, dust and ripple spot | Choose the special species (Audino, Drilbur) or force the spot |
| Fishing encounter table | Replace the rod encounter species |
| Surfing encounter table | Replace the surf encounter species |
| Swarm encounter table | Replace the daily swarm species |
| Hidden Grotto contents | Replace the grotto species, ability or item |
| Static overworld Pokemon | Replace the species, level or form of a static |
| Gift Pokemon from an NPC | Replace the species, level or held item of a gift |
| Starter choice | Replace the three starters or their level |
| Starter sprite and cry | Replace the sprite and the cry the picker plays |
| Trainer party species | Replace the species on a trainer's team |
| Trainer party level | Scale a trainer's levels or set them outright |
| Trainer class and name | Replace the class and the display name |
| Trainer held items | Give or remove a trainer Pokemon's held item |
| Trainer movesets | Replace the four moves a trainer Pokemon has |
| Trainer AI flags | Make a trainer's AI smarter or dumber |
| Pokemon base stats | Change a species' base stats |
| Pokemon types | Change a species' types |
| Pokemon abilities | Change the ability slots of a species |
| Hidden ability slot | Change or enable the hidden ability |
| Evolution target and method | Change what a species evolves into and how |
| Trade evolution to level up | Replace a trade evolution with a level or item method |
| Move properties | Change a move's power, accuracy, type or PP |
| Level-up learnset | Change the moves a species learns by level |
| Egg move pool | Change the moves a species inherits |
| TM contents | Change which move a TM teaches |
| TM and HM compatibility | Change which species can learn a machine |
| Move tutor moves | Change what a tutor teaches |
| Wild held item table | Change the items a wild Pokemon can hold |
| Ground item ball contents | Change what an item ball holds |
| Hidden item spots | Change what a hidden spot holds or where it is |
| In-game trade Pokemon | Change the given and wanted Pokemon of a trade |
| Static or legendary special music | Play or suppress the special battle music |
| Species catch rate | Change the base catch rate of a species |
| Type chart matchups | Change the whole effectiveness table |
| Trainer level scaling | Raise or lower trainer levels globally |
| Boss party size | Give a gym leader a full six Pokemon |
| Boss held items | Give bosses sensible held items |
| Boss movesets | Give bosses competitive moves |
| Smart AI per trainer | Turn on the smarter move choice |
| Level cap per boss | Set the cap to the next boss's ace |
| EV gain from a battle | Zero or scale the EV yield |
| Pokemon ban list | Forbid named species from the run |
| Mandatory trainer battles | Make an optional trainer fight unavoidable |
| Battle format per trainer | Set single, double, triple or rotation |
| Rival keeps starter | Keep the rival's starter through the game |
| Type-themed gym | Give every gym one shared type |
| Physical and special split | Change a move's damage category |
| Ball modifier | Change a ball's multiplier |
| Held item effect | Change or replace a held item's effect |
| Area music | Change the field music by area or season |
| Shop inventory | Replace a mart's stock |
| Shop price | Change an item's price |
| Sell price | Change an item's sell value |
| Exchange corner | Change the Battle Point exchange stock |
| Version-exclusive encounter | Add the other version's encounter |
| Pokemon textures and sprites | Replace a species sprite |
| Pokemon 3D models | Replace a species model |
| Overworld sprites | Replace an overworld sprite |
| Trainer sprites and VS faces | Replace a trainer sprite or face |
| Pokemon icons | Replace a party or dex icon |
| Music and fanfares | Replace a track or a fanfare |
| Sound effects and cries | Replace a cry or an effect |
| Soundfont and instruments | Change an instrument or sample |
| Fonts | Replace the text font |
| UI text | Replace the port's own UI strings |
| Message text | Replace a message bank member |
| Menu graphics and palettes | Replace a menu background or palette |
| Title screen | Replace the title art or text |
| Type chart | Replace the effectiveness table |
| C-Gear skin | Replace the C-Gear art |
| Pokedex skin | Replace the dex art |
| Battle backgrounds | Replace a battle backdrop |
| Seasonal music arrangement | Replace a season's music arrangement |
| Weather graphics | Replace a weather effect's art |
| Text size | Scale the dialogue and menu text |
| Text colour and contrast | Raise the contrast of text |
| Legendary encounter | Change a postgame legendary |
| Text encoding | Change the character table |

The wild encounter table of a zone is selected by the zone record's u16 at
+0x14, an index into `a/1/2/6` (`ZONEDATA_GetEncountId`; the value 0xFFFF
means the zone has no table).

## Related documents

- `docs/modding.md`: the overview of the mod surface.
- `docs/mod-modes.md`: the challenge runs and the services they need.
- `docs/mod-payloads.md`: the fields of each payload.
- `docs/mod-abi.md`: the frozen surface and the interface version.
- `docs/mod-services.md`: the port services, including messages.
- `docs/mod-state.md`: a mod's own saved data.
- `docs/mod-interface.md`: the declared set, the slot and the manifest.
- `docs/mod-overrides.md`: replacing or wrapping one game function.
