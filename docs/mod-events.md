# Events and state

This document is the port-side specification of the event and state API a
eidos mod binds to. It fixes what a mod may observe, what a handler receives
and returns, the order several mods run in, the state a mod reads and writes,
and the small ABI surface the port owns and freezes.

It sits under `docs/mod-interface.md`, which names the hookable set, the slot,
the manifest schema and the typed wrapper, and beside `docs/mod-overrides.md`
for the function override keyed on a DS address, `docs/modding.md` for what the
four example mods need, and `docs/mods.md` for the data surface that exists.
The tooling's view of the same events is kenon's `docs/events-and-rules.md`,
whose catalogue is `docs/mod-interface.md`, section 4. This document keeps
those event names unchanged and extends the catalogue.

Every claim below is attributed to the file it was read from. A thing the code
does not settle is stated as not decided, with the evidence on each side, and
carried into "Still to build" at the end.

## 0. How the catalogue was derived

The catalogue is not invented one event at a time. The game's own command
tables are the vocabulary, because every table entry is a named thing the game
does.

- The field script VM dispatches through a 609-entry command table at
  `d_0217062C` (ov010). Its entry points are `SCRIPT_SYS_CreateVm`
  (0x02158BFC) and `SCRIPT_SYS_Main` (0x0215882C), both ov010
  (`src/matched/ov/ov010_func_02158BFC.c`; `src/game/ov010/script_sys.c`,
  `SCRIPT_SYS_CreateVm`, `SCRIPT_SYS_Main`; `docs/mod-rules.md`, section 1).
  Aletheia's `tools/assets/scripts.py` carries, per opcode, the handler
  address its operand-width sweep resolved (`OPERAND_WIDTH`, the third tuple
  field); opcodes 0 to 31 carry the `docs/ASSETS.md` table instead of an
  address. Every opcode's handler is a named function in aletheia's per-packet
  manifests under `layout/matched/**/pkt_*.json`. Opcode 260 is
  `SCRCMD_PROC_HealParty` (0x0215B5A8); opcode 249 is
  `SCRCMD_PROC_SetMiscValue` (0x0215C558); opcode 181 is `SCRCMD_AddItem`
  (0x021AF544).
- The battle effect VM dispatches through a 78-entry command table at
  `0x02209E88` (ov094). Its entry point is `BTLV_EFFVM_Create` (0x021F93F4)
  (`tools/assets/btleff.py`, its header; `layout/data_objects.json`, the object
  `g_02209E88`). Its handlers are the `BTLV_EFFVM_Cmd*` family; the control
  handlers are `BTLV_EFFVM_CmdJumpIf` (0x021FBD44), `BTLV_EFFVM_CmdJumpIfNot`
  (0x021FBD88), `BTLV_EFFVM_CmdJumpScript` (0x021FC334),
  `BTLV_EFFVM_CmdJumpToArchiveScript` (0x021FC374) and
  `BTLV_EFFVM_CompareAndJump` (0x021FBDD4) (`tools/assets/btleff.py`, the
  control-flow opcodes 60, 61, 70, 71, 72, 74, 77).

The catalogue below is derived from those names plus the named system entry
points for the things that are not commands. 77 of the entries are a field VM
command handler and 4 are a battle effect VM handler; the rest are the named
system entry points (a battle setup, a field map change, an overlay load, a
save, a wireless entry point, a talk event, a state accessor). Every address
was checked against the per-packet manifests under `layout/matched/` by
address, function name and region, revision
`80f06a261e8731f2db94c1a66627bb351ad47673`; `mod/declared_set.txt` carries the
same check.

The declared set grew from 44 rows to 180, about 4.1 times, when this catalogue
was built. `progress/SEAM_COST.md` measured the 44-row set as 44 slots and about
73 redirects, against about 8,000 to 9,100 slots and about 12,600 redirects for
a uniform scheme. Scaled by the row count, the 180-row set is about 180 slots
and up to about 300 redirects, still about 2 per cent of the uniform slot count
and about 2.4 per cent of the uniform redirect count. At 250 to 400 bytes a
slot, 180 slots is 45 to 72 KB, still small against the 2 to 3 MB a uniform
scheme adds. The cost measurement holds. One caveat: the redirect count does
not scale linearly, because many new rows are field VM command handlers reached
through the VM's own 609-entry dispatch table rather than a cross-unit call
site. Those rows need the slot consulted by the VM's table lookup and by the
code-pointer path (`docs/mod-interface.md`, still-to-build item 12), not by a
new `#define` per call site.

### The three kinds

An entry is one of three kinds, and the catalogue says which:

- **event.** Something the game did. A slot at the declared address fires the
  event when it is entered, calls the handlers, and then calls the original
  unless a handler blocks (section 2). A mod observes it, and at the events
  that define a block it may forbid the action.
- **state.** Something a mod reads and writes through an accessor, not a call
  it intercepts. The accessor address is the DS address of the game function
  that reads or writes the field (section 4). A state entry is not in
  `mod/declared_set.txt`, because an accessor needs no slot
  (`docs/mod-interface.md`, section 4).
- **override point.** A function a mod replaces or wraps. The slot calls the
  mod's function in place of the original (section 2; `docs/mod-overrides.md`).
  An override point is in `mod/declared_set.txt` but is not an event: it fires
  whenever the game calls it, which is not one semantic moment.

Where a concept spans more than one address, the row says so. A badge, for
example, is a flag write, not a function call, and is stated as state, not as
an event (see "The owner's examples, resolved").

## 1. The event catalogue

The event name is the permanent binding key: renaming one later breaks every
mod bound to it. The declared-set address is the row the port reads from
`mod/declared_set.txt`. The region is `arm9` or the overlay id. The "Mods"
column is the example mods that need the entry (Nuzlocke N, Soullink S,
Nuzlocke plus Soullink NS, Multiplayer MP); "none" means no example mod names
it and the owner asked for it.

### Battle

| Event | Address(es) | Region | Kind | Fires, and what the game has done | Mods |
|---|---|---|---|---|---|
| `encounter.start` | 0x0216E818 | ov010 | event (block) | `EVENT_BATTLE_CreateEncounterTask` entered; the task exists, the species is not yet chosen | N S NS |
| `encounter.start.mon` | 0x0216EAB0 | ov010 | event | `EVENT_BATTLE_CreateEncounterTaskWithMon` entered; the task exists with a given mon | N S NS |
| `encounter.trainer` | 0x021AA5CC | ov021 | event | `FIELD_ENCOUNT_SetupTrainerBattle` entered; the encounter exists | N S NS |
| `encounter.rate` | 0x02188CE8 | ov021 | state | `FIELDMAP_GetEncount` reads a zone's encounter table | N |
| `battle.start` | 0x02013090 | arm9 | event (block) | `BTL_SETUP_Create` entered; the setup struct is allocated and not yet filled | N S NS MP |
| `battle.setup.wild` | 0x0201347C | arm9 | event | `BTL_SETUP_SetupWild` fills the wild setup | N S NS |
| `battle.kind.notify` | 0x0216EE20 | ov010 | event | `EVENT_BATTLE_DispatchBattleKindNotify` dispatches the battle-kind notify | N S NS |
| `battle.end` | 0x0216EE7C | ov010 | event | `EVENT_BATTLE_NotifyBattleEnd` entered; the outcome field exists and the party is read for the result message | N S NS |
| `battle.finish` | 0x0216EFF0 | ov010 | event | `EVENT_BATTLE_FinishBattleTask` runs; it sets the field mode, handles the result and notifies the end | N S NS |
| `battle.result.sound` | 0x0216EF84 | ov010 | event | `EVENT_BATTLE_SetResultSound` picks the result sound | N S NS |
| `battle.result` | 0x021C84D8 | ov093 | event | `BTL_SERVER_FLOW_GetBattleResult` entered; a thunk to `BTL_SERVER_FLOW_CheckBattleOutcome` | N S NS |
| `battle.outcome` | 0x021CC668 | ov093 | event | `BTL_SERVER_FLOW_CheckBattleOutcome` computes the outcome | N S NS |
| `battle.result.message` | 0x021D1290 | ov093 | event | `BTL_CLIENT_StepShowResultMessage` shows the result message | N S NS |
| `catch.attempt` | 0x021CBA40 | ov093 | event (block) | `BTL_SERVER_FLOW_TryThrowBall` entered; the catch roll is ahead | N S NS |
| `catch.result` | the return of 0x021CBA40 | ov093 | event | the throw returns and the result exists | N S NS |
| `catch.rate` | 0x021CBD48 | ov093 | override | `BTL_SERVER_FLOW_GetBallCatchRate` computes the roll | N S NS |
| `battle.faint` | 0x021C5200 | ov093 | event (block) | `BTL_SERVER_FLOW_ProcFaintRewards` entered; the reward step is ahead | N S NS |
| `battle.faint.client` | 0x021D2320 | ov093 | event | `BTL_CLIENT_StepFaintMon` runs the client faint step | N S NS |
| `battle.fainted` | 0x021D5B68 | ov093 | state | `BTL_POKEPARAM_IsFainted` tests a mon; a predicate called often | N S NS |
| `battle.trainer.begin` | 0x021AEABC | ov021 | event | `SCRCMD_StartTrainerBattle` runs from a script | none |
| `battle.trainer.multi` | 0x021AEB3C | ov021 | event | `SCRCMD_StartMultiTrainerBattle` runs from a script | none |
| `battle.trainer.withmon` | 0x021AED24 | ov021 | event | `SCRCMD_StartTrainerBattleWithMon` runs from a script | none |
| `battle.exp` | 0x021D69B8 | ov093 | event | `BTL_POKEPARAM_AddExp` entered; it adds exp and applies a level up in place when the threshold is crossed | none |
| `battle.levelup.pending` | 0x021D67D0 | ov093 | state | `BTL_POKEPARAM_IsLevelUpPending` reads the pending bit at +0x1b | none |
| `battle.levelup` | 0x021BB8BC | ov093 | event | `BTL_SERVER_NotifyLevelUp` runs; it reads party value 0x73 and calls `GAME_BEACON_SetPokeLevelUp` | none |
| `battle.levelup.main` | 0x021BA238 | ov093 | event | `BTL_MAIN_NotifyLevelUp` updates friendship then notifies the level up | none |
| `battle.levelup.learnmoves` | 0x021D2CA8 | ov093 | event | `BTL_CLIENT_StepLearnLevelUpMoves` learns the moves a level up grants | none |
| `battle.move.start` | 0x021BE95C | ov093 | event | `BTL_SERVER_FLOW_DispatchMoveStartEvent` dispatches the move-start event | none |
| `battle.move.done` | 0x021C6564 | ov093 | event | `BTL_SERVER_FLOW_DispatchMoveDoneEvent` dispatches the move-done event | none |
| `battle.move.result` | 0x021BF2EC | ov093 | event | `BTL_SERVER_FLOW_DispatchMoveResultEvent` dispatches the move-result event | none |
| `battle.move.used` | 0x021C1A10 | ov093 | event | `BTL_SERVER_FLOW_RunMoveUsedEvent` runs the move-used event | none |
| `battle.damage.dealt` | 0x021C1910 | ov093 | event | `BTL_SERVER_FLOW_RunDamageDealtEvent` runs the damage-dealt event | none |
| `battle.item.use` | 0x021CB730 | ov093 | event | `BTL_SERVER_FLOW_ProcBagItemUse` entered; the item effect table is walked and the bag is updated | N S NS |
| `battle.item.heal` | 0x021CC2C0 | ov093 | event | `BTL_SERVER_FLOW_ItemEffectRecoverHp` applies HP recovery | none |
| `battle.item.cure` | 0x021C557C | ov093 | event | `BTL_SERVER_FLOW_CureMajorStatusWithMessage` cures a major status | none |
| `battle.item.check` | 0x021CBFA0 | ov093 | event | `BTL_SERVER_FLOW_ItemEffectCheckSlotMessage` picks the item message | none |
| `battle.bag.add` | 0x021B9090 | ov093 | event | `BTL_MAIN_AddItemToBag` returns an item to the bag | none |
| `battle.bag.sub` | 0x021B90BC | ov093 | event | `BTL_MAIN_SubItemFromBag` takes an item from the bag | none |
| `battle.badge.count` | 0x021B85A0 | ov093 | state | `BTL_MAIN_GetBadgeCount` reads the badge count at +0x80 of the battle main object | none |
| `battle.money.loss` | 0x021D7FF8 | ov093 | state | `BTL_CALC_CalcDefeatMoneyLoss` computes the money lost on defeat from the badge count | none |
| `whiteout.begin` | 0x021D1960, 0x02158A20 | ov093, ov010 | event (block) | the party blacks out; the battle-screen half and the field-recover half, one concept over two addresses | N S NS |
| `whiteout.fieldrecover` | 0x02158AD4 | ov010 | event | `SCRIPT_SYS_RunFieldRecoverScript` runs the field recover script | N S NS |
| `battle.effectvm.create` | 0x021F93F4 | ov094 | override | `BTLV_EFFVM_Create` creates a battle effect VM instance | none |
| `battle.effectvm.jumpif` | 0x021FBD44 | ov094 | override | `BTLV_EFFVM_CmdJumpIf` is one branch command of the effect VM | none |
| `battle.effectvm.jumpifnot` | 0x021FBD88 | ov094 | override | `BTLV_EFFVM_CmdJumpIfNot` is one branch command | none |
| `battle.effectvm.jumpscript` | 0x021FC334 | ov094 | override | `BTLV_EFFVM_CmdJumpScript` jumps within a script | none |
| `battle.effectvm.jumptoarchive` | 0x021FC374 | ov094 | override | `BTLV_EFFVM_CmdJumpToArchiveScript` calls an `a/0/6/7` sub-script | none |
| `battle.effectvm.comparejump` | 0x021FBDD4 | ov094 | override | `BTLV_EFFVM_CompareAndJump` compares then branches | none |

The battle effect VM's 78-entry table is a second vocabulary of battle effect
commands, but aletheia has not named the 78 handlers (they are unnamed
`func_021F9*`), so only the control family and the VM entry point are
catalogued here. Naming the rest is "Still to build" item 14.

### Field and interaction

| Event | Address(es) | Region | Kind | Fires, and what the game has done | Mods |
|---|---|---|---|---|---|
| `route.change` | 0x021643E8 | ov010 | event | `FIELD_MAP_CHANGE_SetupZone` entered; the field loads a new zone | N S NS |
| `route.header` | 0x021643AC | ov010 | override | `FIELD_MAP_CHANGE_ApplyZoneHeader` applies the zone header | N S NS |
| `field.map.event` | 0x021841C0 | ov020 | override | `EVENT_MAPCHANGE_ChangeFieldEvent` changes the field event | N S NS |
| `field.map.fixedzone` | 0x02184C78 | ov020 | event | `EVENT_MAPCHANGE_RunFixedZoneEvent` runs a fixed-zone event | N S NS |
| `field.map.arrival` | 0x02185694 | ov020 | event | `EVENT_MAPCHANGE_RunSituationWarpArrivalEvent` runs the warp-arrival event | N S NS |
| `field.talk.npc` | 0x021870C4 | ov020 | event | `FIELD_WFBC_DATA_LOCAL_CreatePersonTalkEvent` creates a person talk event | none |
| `field.talk.npc.cmd` | 0x02187098 | ov020 | event | `FIELD_WFBC_DATA_LOCAL_ScrCmdCallPersonTalkEvent` runs from a script | none |
| `field.talk.npc.main` | 0x021871F8 | ov020 | event | `FIELD_WFBC_DATA_LOCAL_PersonTalkEventMain` runs the talk | none |
| `field.talk.comm` | 0x0217FC08 | ov018 | event | `EVENT_COMM_COMMON_CreateTalkEvent` creates a comm talk event | none |
| `field.talk.comm.main` | 0x0217FE6C | ov018 | event | `EVENT_COMM_COMMON_TalkEventMain` runs the comm talk | none |
| `field.talk.search` | 0x02162A54 | ov010 | event | `EVENTDATA_SYSTEM_SearchBgEventScriptIdTalk` looks up the talk script | none |
| `field.event.grid` | 0x02189BD0 | ov021 | event | `FIELD_EVENT_CHECK_CheckGridEvents` checks the grid events in front of the player | none |
| `field.event.rail` | 0x0218A278 | ov021 | event | `FIELD_EVENT_CHECK_CheckRailEvents` checks the rail events | none |
| `field.event.run` | 0x0218A4BC | ov021 | event | `FIELD_EVENT_CHECK_Run` runs the field event check | none |
| `field.msg.talkwin.open` | 0x021B0ADC | ov021 | event | `SCRCMD_MSGWIN_OpenTalkWin` opens the talk window | none |
| `field.greeting.npc` | 0x021D2B54 | ov021 | state | `SCRCMD_GetNpcGreetingMessage` reads an NPC greeting | none |
| `field.warp.position` | 0x021AFCD8 | ov021 | event | `SCRCMD_WarpToPosition` runs from a script | none |
| `field.warp.position.plain` | 0x021AFD44 | ov021 | event | `SCRCMD_WarpToPositionPlain` runs | none |
| `field.warp.position.event` | 0x021AFDAC | ov021 | event | `SCRCMD_WarpToPositionEvent` runs | none |
| `field.warp.position.exit` | 0x021AFE64 | ov021 | event | `SCRCMD_WarpToPositionFixedExit` runs | none |
| `field.warp.fixedzone` | 0x021AFECC | ov021 | event | `SCRCMD_WarpToFixedZone` runs | none |
| `field.warp.position3d` | 0x021AFEFC | ov021 | event | `SCRCMD_WarpToPosition3D` runs | none |
| `field.warp.rail` | 0x021AFF7C | ov021 | event | `SCRCMD_WarpToRailLocation` runs | none |
| `field.ledge.check` | 0x0218B408 | ov021 | event | `FIELD_EVENT_CHECK_CheckLedgeJump` entered; it tests the map attr and the path before it creates the task | none |
| `field.ledge.task` | 0x0218B3DC | ov021 | event | `FIELD_EVENT_CHECK_CreateLedgeJumpTask` creates the jump task | none |
| `field.ledge.main` | 0x0218B32C | ov021 | event | `FIELD_EVENT_CHECK_LedgeJumpTaskMain` runs the jump task | none |
| `field.ledge.path` | 0x0218B6F8 | ov021 | state | `FIELD_EVENT_CHECK_IsLedgePathClear` tests the path | none |
| `field.ledge.foot` | 0x021A4ED8 | ov021 | event | `FIELD_PLAYER_GRID_StartLedgeJumpOnFoot` starts the jump | none |
| `field.ledge.cycle` | 0x021A511C | ov021 | event | `FIELD_PLAYER_GRID_StartLedgeJumpOnCycle` starts the jump on a bicycle | none |
| `field.ledge.nogrid.walk` | 0x021A5F74 | ov021 | event | `FIELD_PLAYER_NOGRID_StartWalkLedgeJump` starts the nogrid jump | none |
| `field.ledge.nogrid.slow` | 0x021A6338 | ov021 | event | `FIELD_PLAYER_NOGRID_StartSlowLedgeJump` starts the slow nogrid jump | none |
| `field.hop.jump` | 0x021C1228 | ov021 | event | `FIELD_PLAYER_GRID_EVENT_CreateJumpTask` creates the jump-up task | none |
| `field.hop` | 0x021C1024 | ov021 | event | `FIELD_PLAYER_GRID_EVENT_CreateHopTask` creates the hop task | none |
| `field.skill.handler` | 0x021644C4 | ov010 | event (block) | `FIELD_SKILL_CHECK_CallHandler` dispatches a field-skill handler by index | none |
| `field.skill.flags` | 0x02164480 | ov010 | event | `FIELD_SKILL_CHECK_FillUseFlags` fills the usable-skill flags | none |
| `field.move.cut` | 0x0215EA6C | ov010 | event | `MOVE_POKEMON_CreateCutEvent` creates the Cut field effect | none |
| `field.move.flash` | 0x0215EC94 | ov010 | event | `MOVE_POKEMON_CreateFlashEvent` creates the Flash field effect | none |
| `field.move.strength` | 0x0215EBD0 | ov010 | event | `MOVE_POKEMON_CreateStrengthEvent` creates the Strength field effect | none |
| `field.move.dive` | 0x0215EC40 | ov010 | event | `MOVE_POKEMON_CreateDiveEvent` creates the Dive field effect | none |
| `field.move.cut.main` | 0x0215EA94 | ov010 | event | `MOVE_POKEMON_CutEventMain` runs the Cut event | none |
| `field.move.flash.main` | 0x0215ECBC | ov010 | event | `MOVE_POKEMON_FlashEventMain` runs the Flash event | none |
| `field.move.strength.main` | 0x0215EBF8 | ov010 | event | `MOVE_POKEMON_StrengthEventMain` runs the Strength event | none |
| `field.move.cut.check` | 0x0215EA58 | ov010 | state | `MOVE_POKEMON_CheckCut` tests whether Cut is usable | none |
| `field.move.flash.check` | 0x0215EC80 | ov010 | state | `MOVE_POKEMON_CheckFlash` tests Flash | none |
| `field.move.strength.check` | 0x0215EBBC | ov010 | state | `MOVE_POKEMON_CheckStrength` tests Strength | none |
| `field.move.waterfall.check` | 0x0215EC6C | ov010 | state | `MOVE_POKEMON_CheckWaterfall` tests Waterfall | none |
| `field.move.iaigiri` | 0x021C4678 | ov021 | event | `SCRCMD_IAIGIRI_StartOnPlayer` starts the Cut effect on the player | none |
| `field.msg.plain` | 0x021B10D0 | ov021 | event | `SCRCMD_MSGWIN_CmdPlainWinMsg` prints a plain window message | none |
| `field.msg.yesno` | 0x021B0130 | ov021 | event | `SCRCMD_MSGWIN_CmdYesNoWin` opens a yes/no window | none |
| `field.msg.infosys` | 0x021B04D8 | ov021 | event | `SCRCMD_MSGWIN_CmdInfoSysWinMsg` prints an info-system message | none |
| `field.msg.show` | 0x021B4894 | ov021 | event | `SCRCMD_PROC_FLD_ShowMessage` shows a field message | none |
| `field.bag.open` | 0x0215C72C | ov010 | event | `SCRCMD_PROC_CallBagScreen` opens the bag from a script | none |
| `field.bag.closed` | 0x0215C700 | ov010 | event | `SCRCMD_PROC_OnBagClosed` runs when the bag closes | none |
| `field.overlayproc.start` | 0x0215C63C | ov010 | event | `SCRCMD_PROC_StartOverlayProc` starts an overlay proc | none |
| `field.overlayproc.wait` | 0x0215C69C | ov010 | event | `SCRCMD_PROC_WaitOverlayProc` waits for it | none |
| `field.random` | 0x0215AABC | ov010 | state | `SCRCMD_WORK_CmdGetRandom` reads a random number | none |
| `field.save.record` | 0x0215AAF4 | ov010 | state | `SCRCMD_WORK_CmdGetSaveRecord` reads a save record | none |
| `field.season` | 0x0215ABD0 | ov010 | state | `SCRCMD_WORK_CmdGetSeason` reads the season | none |
| `field.special.location` | 0x0215ADCC | ov010 | event | `SCRCMD_WORK_CmdSetSpecialLocation` sets the special location | none |

### Inventory and items

| Event | Address(es) | Region | Kind | Fires, and what the game has done | Mods |
|---|---|---|---|---|---|
| `item.add` | 0x021AF544 | ov021 | event | `SCRCMD_AddItem` entered; it reads the item id and count from the script and adds them to the bag | none |
| `item.remove` | 0x021AF594 | ov021 | event | `SCRCMD_RemoveItem` removes an item | none |
| `item.has` | 0x021AF634 | ov021 | state | `SCRCMD_HasItem` tests the bag | none |
| `item.quantity` | 0x021AF684 | ov021 | state | `SCRCMD_GetItemQuantity` reads a count | none |
| `item.canadd` | 0x021AF5E4 | ov021 | state | `SCRCMD_CanAddItem` tests whether the bag can take it | none |
| `item.machine` | 0x021AF750 | ov021 | state | `SCRCMD_GetItemMachineFlag` reads the machine flag of an item | none |
| `item.pocket` | 0x021AF6E8 | ov021 | state | `SCRCMD_GetItemPocket` reads the item's pocket | none |
| `item.partyuse` | 0x021AF71C | ov021 | state | `SCRCMD_GetItemPartyUse` reads the party-use kind | none |
| `item.gift.daily` | 0x021B7364 | ov021 | event | `SCRCMD_POKEMON_FLD_GetDailyGift` hands the daily gift | none |
| `item.gift.daily.index` | 0x021B73B8 | ov021 | state | `SCRCMD_POKEMON_FLD_GetDailyGiftIndex` reads the gift index | none |
| `item.gift.mystery` | 0x021B6BDC | ov021 | event | `SCRCMD_POKEMON_FLD_HandleMysteryGift` handles a mystery gift | none |
| `item.gift.postman` | 0x021B76B4 | ov021 | event | `PDW_POSTMAN_ReceiveItems` receives postman items | none |
| `item.gift.postman.count` | 0x021B76F8 | ov021 | state | `PDW_POSTMAN_CountUnreceivable` counts them | none |
| `item.use.field` | 0x021BC350 | ov091 | event | `PLIST_MENU_StartItemUse` entered; the party list starts an item use | none |
| `item.use.done` | 0x021BC2C8 | ov091 | event | `PLIST_MENU_ShowItemUsedThenClose` closes after the use | none |
| `item.use.continue` | 0x021BC30C | ov091 | event | `PLIST_MENU_ShowItemUsedThenContinue` continues after the use | none |
| `item.use.calc` | 0x0201B7B4 | arm9 | state | `NATSUKI_CalcUseItem` computes an item use | none |
| `item.heal.party` | 0x0201BA34 | arm9 | event | `POKE_TOOL_HealParty` heals the party; reached by `SCRCMD_PROC_HealParty` and by item use | N S NS |
| `shop.start` | 0x021B2FD8 | ov021 | event | `SCRCMD_SHOP_StartShop` starts a shop | none |
| `shop.pay` | 0x021B3568 | ov021 | event | `SCRCMD_PROC_FLD_PayPrice` pays a shop price | none |
| `money.set` | 0x0215C558 | ov010 | event | `SCRCMD_PROC_SetMiscValue` sets the money | none |
| `money.add` | 0x0215C584 | ov010 | event | `SCRCMD_PROC_AddMiscValue` adds to the money | none |
| `money.check` | 0x0215C5B0 | ov010 | state | `SCRCMD_PROC_CheckMiscValueReached` tests the money against a value | none |

### Progression

| Event | Address(es) | Region | Kind | Fires, and what the game has done | Mods |
|---|---|---|---|---|---|
| `move.learn` | 0x021C360C | ov021 | event | `SODATEYA_LearnMove` writes the move into a free slot or pushes it | none |
| `move.push` | 0x020180F4 | arm9 | override | `POKE_TOOL_PushMove` pushes a move into the party | none |
| `move.slot` | 0x020181F4 | arm9 | override | `POKE_TOOL_SetMoveSlot` writes a move slot | none |
| `move.set` | 0x0215BD2C | ov010 | event | `SCRCMD_PROC_SetMonMove` sets a mon move | none |
| `move.get` | 0x0215BCE8 | ov010 | state | `SCRCMD_PROC_GetMonMove` reads a mon move | none |
| `tm.tutor.select` | 0x021B61E8 | ov021 | event | `SCRCMD_POKEMON_FLD_SelectTutorMon` selects the mon to teach | none |
| `tm.tutor.move` | 0x021B6124 | ov021 | state | `SCRCMD_POKEMON_FLD_GetMonTutorMove` reads the tutor move | none |
| `tm.tutor.status` | 0x021B6164 | ov021 | state | `SCRCMD_POKEMON_FLD_CheckMonTutorStatus` reads the tutor status | none |
| `tm.tutor.id` | 0x021B5F24 | ov021 | state | `SCRCMD_POKEMON_FLD_GetTutorMoveId` reads the tutor move id | none |
| `tm.type` | 0x021B62C8 | ov021 | state | `SCRCMD_POKEMON_FLD_GetMonTypeForTm` reads the TM type | none |
| `move.relearn.list` | 0x0216F948 | ov010 | event | `WAZA_OSHIE_CreateMoveList` builds the relearn list | none |
| `move.relearn` | 0x0215C89C | ov010 | event | `SCRCMD_PROC_CallMoveRelearn` runs the relearn screen | none |
| `move.relearn.count` | 0x0215BFD4 | ov010 | state | `SCRCMD_PROC_GetRelearnableMoveCount` reads the count | none |
| `mon.party.add` | 0x0215BB50 | ov010 | event | `SCRCMD_PROC_AddMonToPartySimple` adds a mon to the party | none |
| `mon.party.add.detailed` | 0x0215BBD0 | ov010 | event | `SCRCMD_PROC_AddMonToPartyDetailed` adds a mon with full detail | none |
| `mon.box.add` | 0x0215C180 | ov010 | event | `SCRCMD_PROC_AddMonToBoxSimple` adds a mon to the box | none |
| `mon.box.add.detailed` | 0x0215C200 | ov010 | event | `SCRCMD_PROC_AddMonToBoxDetailed` adds a mon with full detail | none |
| `egg.add` | 0x0215C2D0 | ov010 | event | `SCRCMD_PROC_AddEggToParty` adds an egg | none |
| `egg.hatch` | 0x021B63D4 | ov021 | event | `SCRCMD_POKEMON_FLD_StartEggHatch` starts an egg hatch | none |
| `trade.start` | 0x021B654C | ov021 | event | `SCRCMD_POKEMON_FLD_StartTradeEvent` starts a trade | S NS MP |
| `trade.check` | 0x021B647C | ov021 | state | `SCRCMD_POKEMON_FLD_CheckTradeMon` checks the trade mon | S NS MP |
| `mon.select` | 0x021B631C | ov021 | event | `SCRCMD_POKEMON_FLD_StartSelectEvent` starts a party select | none |
| `mon.select.party` | 0x021B5B4C | ov021 | event | `SCRCMD_POKEMON_FLD_SelectPartyMon` selects a party mon | none |
| `zukan.seen.register` | 0x0215AEE8 | ov010 | event | `SCRCMD_WORK_CmdRegisterZukanSeen` marks a species seen | none |
| `zukan.seen.count` | 0x0215AF40 | ov010 | state | `SCRCMD_WORK_CmdGetZukanSeenCount` reads the seen count | none |
| `zukan.flag.a` | 0x0215AFE0 | ov010 | event | `SCRCMD_WORK_CmdSetZukanFlagA` sets a dex flag | none |
| `zukan.flag.b` | 0x0215B014 | ov010 | event | `SCRCMD_WORK_CmdSetZukanFlagB` sets a dex flag | none |
| `zukan.caught` | 0x0200CDE0 | arm9 | event | `ZUKAN_SAVE_RegisterCaught` marks a species caught | none |
| `zukan.seen` | 0x0200CFA0 | arm9 | event | `ZUKAN_SAVE_RegisterSeen` marks a species seen | none |
| `zukan.is.caught` | 0x0200CED4 | arm9 | state | `ZUKAN_SAVE_IsCaught` tests the dex | none |
| `research.mission.start` | 0x0216B63C | ov010 | event | `SCRCMD_RESEARCH_CmdStartMission` starts a research mission | none |
| `research.rank.raise` | 0x0216B538 | ov010 | event | `SCRCMD_RESEARCH_CmdRaiseResearchRank` raises the research rank | none |
| `research.mission.clear` | 0x0216B6DC | ov010 | event | `SCRCMD_RESEARCH_CmdClearMission` clears a mission | none |
| `flag.set` | 0x02159728 | ov010 | event | `SCRIPT_WORK_CmdSetEventFlag` sets an event flag | N S NS |
| `flag.reset` | 0x02159750 | ov010 | event | `SCRIPT_WORK_CmdResetEventFlag` resets one | N S NS |
| `flag.check` | 0x02159778 | ov010 | state | `SCRIPT_WORK_CmdCheckEventFlag` tests one | N S NS |
| `flag.set.offset` | 0x0215B328 | ov010 | event | `SCRCMD_WORK_CmdSetEventFlagOffset` sets one at an offset | N S NS |
| `flag.reset.offset` | 0x0215B350 | ov010 | event | `SCRCMD_WORK_CmdResetEventFlagOffset` resets one at an offset | N S NS |
| `flag.check.offset` | 0x0215B378 | ov010 | state | `SCRCMD_WORK_CmdCheckEventFlagOffset` tests one at an offset | N S NS |
| `flag.write` | 0x0202F190 | arm9 | override | `SAVE_EVENT_Write` writes an event flag to the save block | N S NS |
| `flag.misc.set` | 0x0215AC68 | ov010 | event | `SCRCMD_WORK_CmdSetMiscFlag` sets a misc flag; it calls `BEACON_SetFlag` and stamps the badge date | none |
| `flag.misc.check` | 0x0215AC3C | ov010 | state | `SCRCMD_WORK_CmdCheckMiscFlag` tests a misc flag | none |
| `flag.fixed.set` | 0x0215B068 | ov010 | event | `SCRCMD_WORK_CmdSetFixedEventFlag` sets a fixed event flag | none |
| `misc.byte.set` | 0x0215B138 | ov010 | event | `SCRCMD_WORK_CmdSetMiscByte` sets a misc byte | none |
| `misc.byte.check` | 0x0215B158 | ov010 | state | `SCRCMD_WORK_CmdCheckMiscByte` tests a misc byte | none |
| `misc.byte.get` | 0x0215B104 | ov010 | state | `SCRCMD_WORK_CmdGetMiscByte` reads a misc byte | none |
| `event.work` | 0x0201296C, 0x02012FE4 | arm9 | state | `GAME_DATA_GetEventWork`, `GAME_DATA_GetEventWorkFromSave` read save block 0x2d | N S NS |
| `badge.state` | 0x0200C234 | arm9 | state | `BEACON_GetFlag` reads one badge bit; the eight badges are bits 0..7 of the flag word at misc block +0x04 | none |
| `badge.count` | 0x0200C258 | arm9 | state | `BEACON_CountFlags` counts the set bits | none |
| `badge.write` | 0x0200C248 | arm9 | override | `BEACON_SetFlag` sets one flag bit; the game awards a badge by setting the bit, not by a call | none |
| `badge.date` | 0x02008A7C | arm9 | event | `TRCARD_SIGN_SetBadgeDate` stamps the badge date on the trainer card | none |
| `badge.profile` | 0x0216F40C | ov010 | state | `TRCARD_SYS_BuildProfile` builds the badge mask from bits 0..7 | none |

### Save and load

| Event | Address(es) | Region | Kind | Fires, and what the game has done | Mods |
|---|---|---|---|---|---|
| `save.begin` | 0x02007150 | arm9 | event | `SAVE_CONTROL_StartSave` entered, before the game writes | N S NS MP |
| `save.done` | 0x02007168 | arm9 | event | `SAVE_CONTROL_MainSave` reached | N S NS MP |
| `save.load` | 0x02012C18 | arm9 | event | `GAME_DATA_LoadFromSave` entered | N S NS MP |
| `save.flag` | 0x02012BF8 | arm9 | state | `GAME_DATA_CheckSaveFlag` reads a save flag | N S NS MP |
| `save.block` | 0x020071CC | arm9 | override | `SAVE_CONTROL_GetBlock` returns a save block by id | none |
| `save.misc` | 0x02012F08 | arm9 | override | `GAME_DATA_GetMiscSave` returns the misc block (id 0x34), which holds the money and the badge flags | none |

### Overlay lifecycle

| Event | Address(es) | Region | Kind | Fires, and what the game has done | Mods |
|---|---|---|---|---|---|
| `overlay.load` | 0x02078EB0 | arm9 | event | `FS_LoadOverlayImage` runs; the provider's load copies the file to the window, binds the names and relocates the pointers | N S NS MP |
| `overlay.start` | 0x0207902C | arm9 | event | `FS_StartOverlay` runs the overlay's static initialisers | N S NS MP |

### Networking

| Event | Address(es) | Region | Kind | Fires, and what the game has done | Mods |
|---|---|---|---|---|---|
| `link.up`, `link.down` | 0x0215CD24 | ov009 | event | `SOCL_LinkIsOn` returns; one address, two events, distinguished by the return | S NS MP |
| `link.level` | 0x021666C8, 0x02166710 | ov009 | state | `WCM_GetLinkLevel`, `WCM_CalcLinkLevel` read or compute the link level | S NS MP |
| `net.send`, `net.receive` | the Wm and DWC send points | not built | event | bytes leave or arrive; the transport is not built (`docs/modding.md`, "Networking seam: missing") | MP |
| `comm.update` | 0x0202627C | arm9 | event | `GAME_COMM_Update` runs the game's comm update | MP |
| `comm.handler` | 0x02026388 | arm9 | override | `GAME_COMM_CallHandler` calls a comm handler | MP |
| `comm.notify` | 0x020263B0 | arm9 | event | `GAME_COMM_CallKindNotify` dispatches a kind notify | MP |
| `wm.init` | 0x0208E010 | arm9 | event | `WM_Init` initialises the wireless manager | MP |
| `wm.enable` | 0x0208F224 | arm9 | event | `WM_Enable` enables it | MP |
| `wm.connect` | 0x0208F84C | arm9 | event | `WM_StartConnectEx` starts a connection | MP |
| `wm.mp.start` | 0x0208FC4C | arm9 | event | `WM_StartMP` starts multipoint | MP |
| `wm.mp.send` | 0x0208FCB4 | arm9 | event | `WM_SetMPDataToPortEx` sends multipoint data | MP |
| `wm.share.start` | 0x0209004C | arm9 | event | `WM_StartDataSharing` starts data sharing | MP |
| `wm.share.addr` | 0x02090B18 | arm9 | state | `WM_GetSharedDataAddress` reads the shared-data address | MP |
| `wm.share.send` | 0x020909C8 | arm9 | event | `WmDataSharingSendDataSet` sends a data set | MP |
| `wm.share.recv` | 0x020908F8 | arm9 | event | `WmDataSharingReceiveData` receives a data set | MP |
| `wm.fifo` | 0x0208E5BC | arm9 | event | `WmReceiveFifo` receives a FIFO frame | MP |
| `wm.callback` | 0x0208EB0C | arm9 | override | `WM_SetPortCallback` installs a port callback | MP |
| `dwc.init` | 0x02059FA8 | arm9 | event | `DWCi_Init` initialises the WiFi connection | MP |
| `dwc.production` | 0x02059F6C | arm9 | event | `DWC_InitForProduction` initialises the DWC production mode | MP |
| `network.wait` | 0x0215F1C8 | ov010 | state | `SCRCMD_NETWORK_CmdWaitCommState` reads the comm state | MP |
| `network.value` | 0x0215F214 | ov010 | state | `SCRCMD_NETWORK_CmdGetCommValue` reads a comm value | MP |
| `network.start` | 0x0215F228 | ov010 | event | `SCRCMD_NETWORK_CmdStartAutoComm` starts auto comm | MP |
| `network.exit` | 0x0215F238 | ov010 | event | `SCRCMD_NETWORK_CmdRequestCommExit` requests a comm exit | MP |
| `network.restart` | 0x0215F280 | ov010 | event | `SCRCMD_NETWORK_CmdRestartAutoComm` restarts auto comm | MP |
| `comm.talk.start` | 0x0217FB5C | ov018 | event | `EVENT_COMM_COMMON_CreateTalkStartEvent` creates a talk start | MP |
| `comm.talked` | 0x0217FD34 | ov018 | event | `EVENT_COMM_COMMON_CreateTalkedEvent` creates a talked event | MP |
| `comm.support` | 0x02181D8C | ov018 | event | `EVENT_COMM_COMMON_CreateSupportMsgEvent` creates a support message | MP |
| `comm.offer` | 0x02182150 | ov018 | event | `EVENT_COMM_COMMON_OfferEventMain` runs an offer | MP |
| `comm.buy` | 0x0218261C | ov018 | event | `EVENT_COMM_COMMON_ItemPurchaseEventMain` runs a purchase | MP |
| `comm.sell` | 0x021823A8 | ov018 | event | `EVENT_COMM_COMMON_ItemSaleEventMain` runs a sale | MP |

The general rule for when an event fires is the slot's: a slot at a declared
address fires the event when it is entered, calls the handlers, and then calls
the original unless a handler blocks (`docs/mod-interface.md`, section 4). A
field VM command's event fires when the VM dispatches that opcode, which is one
command in one script frame.

## 1a. The owner's examples, resolved

Each requested concept, and what it turned out to be. The evidence is the
function named.

- **Leveling up.** A battle event, but not one function. `BTL_POKEPARAM_AddExp`
  (0x021D69B8, ov093) adds exp and, when the exp crosses the threshold for the
  next level, applies the level up in place: it writes exp, calls
  `POKE_TOOL_RecalcParam`, updates the level at party value 0x9E, raises HP and
  returns 1 (`src/matched/ov/ov093_func_021D69B8.c`). The caller loops, so one
  call is at most one level. The notify is `BTL_SERVER_NotifyLevelUp`
  (0x021BB8BC) and `BTL_MAIN_NotifyLevelUp` (0x021BA238); the pending test is
  `BTL_POKEPARAM_IsLevelUpPending` (0x021D67D0, bit at +0x1b). The event binds
  `battle.exp`; a mod that wants "a level was gained" reads the return of
  `BTL_POKEPARAM_AddExp` or binds `battle.levelup`.
- **Talking to an NPC.** A field interaction, and not a single function. The
  player's A press runs the field event check
  (`FIELD_EVENT_CHECK_CheckGridEvents` 0x02189BD0 and
  `FIELD_EVENT_CHECK_CheckRailEvents` 0x0218A278, ov021), which runs the event
  object's script; the script's talk lookup is
  `EVENTDATA_SYSTEM_SearchBgEventScriptIdTalk` (0x02162A54, ov010) and the talk
  window is `SCRCMD_MSGWIN_OpenTalkWin` (0x021B0ADC, ov021). The special person
  talk is `FIELD_WFBC_DATA_LOCAL_CreatePersonTalkEvent` (0x021870C4, ov020).
  The hook is a set of events, `field.event.grid`, `field.event.rail`,
  `field.talk.search`, `field.msg.talkwin.open`; there is no single
  `talk.to.npc`.
- **Receiving an item.** An event. `SCRCMD_AddItem` (0x021AF544, ov021) reads
  the item id and count from the script and adds them to the bag through
  `GAME_DATA_GetMyItem` and `func_02007E50`
  (`src/matched/ov/ov021_func_021AF544.c`). The catalogue binds `item.add`. A
  mystery gift and a postman gift are the separate events `item.gift.mystery`
  (`SCRCMD_POKEMON_FLD_HandleMysteryGift` 0x021B6BDC) and `item.gift.postman`
  (`PDW_POSTMAN_ReceiveItems` 0x021B76B4).
- **Getting a badge.** A flag write, not a function call, and so state rather
  than an event. `TRCARD_SYS_BuildProfile` (0x0216F40C, ov010) builds the
  trainer card's badge mask by looping `badgeIndex` 0..7 and testing
  `BEACON_GetFlag(badges, badgeIndex)`, where `badges` is
  `GAME_DATA_GetMiscSave` (save block 0x34)
  (`src/game/ov010/trcard_sys.c`). The flag write is `BEACON_SetFlag`
  (0x0200C248, arm9), reached from a script through
  `SCRCMD_WORK_CmdSetMiscFlag` (0x0215AC68, ov010), which calls it and stamps
  the badge date (`src/matched/ov/ov010_func_0215AC68.c`). The count is
  `BEACON_CountFlags` (0x0200C258); the battle reads it as `badgeCount`
  (`src/game/arm9/btl_setup.c`; `BTL_MAIN_GetBadgeCount` 0x021B85A0). The
  catalogue binds `badge.state`, `badge.count`, `badge.write`, `badge.date` and
  `badge.profile`. A mod that wants "a badge was awarded" binds
  `flag.misc.set` and reads which bit was set, or watches `badge.state`.
- **Winning a battle.** An event. `EVENT_BATTLE_NotifyBattleEnd` (0x0216EE7C,
  ov010) runs after a battle with the outcome in `st->outcome`; it reports the
  lead mon's state and dispatches by mode and outcome
  (`src/matched/ov/ov010_func_0216EE7C.c`). `EVENT_BATTLE_FinishBattleTask`
  (0x0216EFF0) sets the field mode and calls it. The server-side result is
  `BTL_SERVER_FLOW_GetBattleResult` (0x021C84D8) and
  `BTL_SERVER_FLOW_CheckBattleOutcome` (0x021CC668). The catalogue binds
  `battle.end`, `battle.finish`, `battle.result` and `battle.outcome`.
- **Using an item.** Two events, one in the field and one in battle. In the
  field, `PLIST_MENU_StartItemUse` (0x021BC350, ov091) starts the use from the
  party list. In battle, `BTL_SERVER_FLOW_ProcBagItemUse` (0x021CB730, ov093)
  walks the bag item effect table and updates the bag
  (`src/matched/ov/ov093_func_021CB730.c`). The catalogue binds
  `item.use.field` and `battle.item.use`.
- **Healing, a Pokemon Center.** The heal is the event `heal.party` at
  `SCRCMD_PROC_HealParty` (0x0215B5A8, ov010); the centre's own recovery
  animation is `EVENT_RECOVERY_CreatePartyEvent` (0x021C0AA0, ov021) and
  `EVENT_RECOVERY_CreateBallsEvent` (0x021C0820, ov021), whose task main is
  `EVENT_RECOVERY_RunParty` (0x021C0AC0). The catalogue binds `heal.party` and
  the two recovery events; the heal itself is one event over the party, not one
  per centre.
- **Healing, a healing item.** An event. The tool is `POKE_TOOL_HealParty`
  (0x0201BA34, arm9), the same tool `SCRCMD_PROC_HealParty` calls. In battle
  the HP recovery is `BTL_SERVER_FLOW_ItemEffectRecoverHp` (0x021CC2C0,
  ov093); in the field the item use is `PLIST_MENU_StartItemUse` and the
  calculation is `NATSUKI_CalcUseItem` (0x0201B7B4, arm9). The catalogue binds
  `item.heal.party`, `battle.item.heal` and `item.use.field`. The two heal
  paths, a centre and an item, share `POKE_TOOL_HealParty`.
- **Using a TM.** An event. A TM teaches a move. The tutor selection is
  `SCRCMD_POKEMON_FLD_SelectTutorMon` (0x021B61E8, ov021); the tutor move is
  `SCRCMD_POKEMON_FLD_GetMonTutorMove` (0x021B6124); the TM type is
  `SCRCMD_POKEMON_FLD_GetMonTypeForTm` (0x021B62C8); the learn itself is
  `SODATEYA_LearnMove` (0x021C360C, ov021), which writes a free slot through
  `POKE_TOOL_SetMoveSlot` or pushes through `POKE_TOOL_PushMove`
  (`src/matched/ov/ov021_func_021C360C.c`). The catalogue binds `tm.tutor.select`,
  `tm.tutor.move`, `tm.type` and `move.learn`. A TM is not a distinct function;
  it is the tutor flow plus the learn.
- **Using an HM.** An event, but a dispatch rather than one function per HM.
  `FIELD_SKILL_CHECK_CallHandler` (0x021644C4, ov010) calls entry `i` of the
  field-skill handler table at 0x021714DC and returns 1 when the slot is empty
  (`src/matched/ov/ov010_func_021644C4.c`). Each HM is one field-move event:
  `MOVE_POKEMON_CreateCutEvent` (0x0215EA6C), `MOVE_POKEMON_CreateFlashEvent`
  (0x0215EC94), `MOVE_POKEMON_CreateStrengthEvent` (0x0215EBD0),
  `MOVE_POKEMON_CreateDiveEvent` (0x0215EC40), with the checks
  `MOVE_POKEMON_CheckCut` (0x0215EA58), `MOVE_POKEMON_CheckFlash`
  (0x0215EC80), `MOVE_POKEMON_CheckStrength` (0x0215EBBC) and
  `MOVE_POKEMON_CheckWaterfall` (0x0215EC6C). The catalogue binds
  `field.skill.handler`, `field.skill.flags` and the per-move events. The HM
  index, not the move id, selects the handler.
- **Jumping over a ledge.** An event, and a set of them. The check is
  `FIELD_EVENT_CHECK_CheckLedgeJump` (0x0218B408, ov021): it tests the map
  attribute and the path, then calls `FIELD_EVENT_CHECK_CreateLedgeJumpTask`
  (0x0218B3DC) (`src/matched/ov/ov021_func_0218B408.c`). The player-side start
  is `FIELD_PLAYER_GRID_StartLedgeJumpOnFoot` (0x021A4ED8) or
  `FIELD_PLAYER_GRID_StartLedgeJumpOnCycle` (0x021A511C) on a bicycle, with the
  nogrid starts `FIELD_PLAYER_NOGRID_StartWalkLedgeJump` (0x021A5F74) and
  `FIELD_PLAYER_NOGRID_StartSlowLedgeJump` (0x021A6338). The catalogue binds
  `field.ledge.check`, `field.ledge.task`, `field.ledge.main`,
  `field.ledge.foot`, `field.ledge.cycle` and the two nogrid starts. A block at
  `field.ledge.check`, if the port defines one, forbids the jump; the later
  starts are observation only.

## 1b. Declared addresses with no event

The declared set is the hookable set, not the event set. Several rows are a
concept a mod reaches by a function override or a state read rather than an
event, and several are part of a concept above whose binding is undecided.
Every row below is in `mod/declared_set.txt` and was checked against the
per-packet manifests.

| Address | Function | Region | Why it is declared | Bound to |
|---|---|---|---|---|
| 0x0216EAB0 | EVENT_BATTLE_CreateEncounterTaskWithMon | ov010 | N S NS | `encounter.start.mon` |
| 0x0201347C | BTL_SETUP_SetupWild | arm9 | N S NS | `battle.setup.wild` |
| 0x0216EE20 | EVENT_BATTLE_DispatchBattleKindNotify | ov010 | N S NS | `battle.kind.notify` |
| 0x02188CE8 | FIELDMAP_GetEncount | ov021 | N | `encounter.rate` (state) |
| 0x021CBD48 | BTL_SERVER_FLOW_GetBallCatchRate | ov093 | N S NS | `catch.rate` (override) |
| 0x021D5B68 | BTL_POKEPARAM_IsFainted | ov093 | N S NS | `battle.fainted` (state) |
| 0x02158AD4 | SCRIPT_SYS_RunFieldRecoverScript | ov010 | N S NS | `whiteout.fieldrecover` |
| 0x02018198 | POKE_TOOL_RecoverAllPP | arm9 | N S NS | `heal.party` (undecided) |
| 0x0201BA34 | POKE_TOOL_HealParty | arm9 | N S NS | `item.heal.party` |
| 0x021643AC | FIELD_MAP_CHANGE_ApplyZoneHeader | ov010 | N S NS | `route.header` (override) |
| 0x021841C0 | EVENT_MAPCHANGE_ChangeFieldEvent | ov020 | N S NS | `field.map.event` (override) |
| 0x021666C8 | WCM_GetLinkLevel | ov009 | S NS MP | `link.level` (state) |
| 0x02166710 | WCM_CalcLinkLevel | ov009 | S NS MP | `link.level` (state) |
| 0x02012BF8 | GAME_DATA_CheckSaveFlag | arm9 | N S NS MP | `save.flag` (state) |
| 0x0200C234 | BEACON_GetFlag | arm9 | none | `badge.state` (state) |
| 0x0200C248 | BEACON_SetFlag | arm9 | none | `badge.write` (override) |
| 0x0200C258 | BEACON_CountFlags | arm9 | none | `badge.count` (state) |
| 0x02008A7C | TRCARD_SIGN_SetBadgeDate | arm9 | none | `badge.date` |
| 0x0216F40C | TRCARD_SYS_BuildProfile | ov010 | none | `badge.profile` (state) |
| 0x020071CC | SAVE_CONTROL_GetBlock | arm9 | none | `save.block` (override) |
| 0x02012F08 | GAME_DATA_GetMiscSave | arm9 | none | `save.misc` (override) |
| 0x0200CDE0 | ZUKAN_SAVE_RegisterCaught | arm9 | none | `zukan.caught` |
| 0x0200CFA0 | ZUKAN_SAVE_RegisterSeen | arm9 | none | `zukan.seen` |
| 0x020180F4 | POKE_TOOL_PushMove | arm9 | none | `move.push` (override) |
| 0x020181F4 | POKE_TOOL_SetMoveSlot | arm9 | none | `move.slot` (override) |
| 0x0202F190 | SAVE_EVENT_Write | arm9 | N S NS | `flag.write` (override) |

The rows come from `mod/declared_set.txt`. The "Bound to" column is this
document's reading. A state accessor is not in the declared set because an
accessor needs no slot (`docs/mod-interface.md`, section 4); it appears in
section 4 below instead.

## 2. The event contract

A handler is one C function per event. `docs/mod-interface.md`, section 4,
fixes its shape:

```c
typedef int (*BwEventHandler)(void *mod_state, const BwEvent *event);
```

- **`mod_state`.** The mod's own opaque pointer, the same value at every call.
  The port sets it when it loads the mod's code; the code loader is not built
  (`docs/mod-interface.md`, still-to-build item 4), so how it is set is not
  decided (still-to-build item 11).
- **`event`.** A pointer to the event's typed struct. It is valid only for the
  call: a handler must not keep it, and the port may reuse the storage at the
  next event (`docs/mod-interface.md`, section 4).
- **The return.** An `int`, one of the two values the port defines
  (`docs/mod-interface.md`, section 4):

```c
enum {
    BW_EVENT_CONTINUE = 0,   /* the slot calls the original */
    BW_EVENT_BLOCK    = 1    /* the slot does not call the original */
};
```

`BW_EVENT_CONTINUE` means the handler allows the game's own work and the slot
proceeds to call the original. `BW_EVENT_BLOCK` means the handler forbids the
action and the slot does not call the original. A block is the return of a
block on a slot that defines one, not a state write (`docs/mod-interface.md`,
section 4; `docs/modding.md`, "What block an action means").

Two returns are defined today. No third return exists. A return value other
than the two is not defined, and the port must fix what it does with one
(still-to-build item 2).

**A block at an event that defines none.** The events that define a block are
`encounter.start`, `battle.start`, `catch.attempt`, `battle.faint`, `heal.party`
and `whiteout.begin`, the six the earlier catalogue named (section 1; kenon's
`docs/events-and-rules.md`, "What a rule may do"). The others are observation
only. Whether a new event such as `item.add`, `battle.item.use`,
`item.use.field`, `shop.pay` or `field.ledge.check` also defines one is the
port's to fix, because each is an action the game has not yet taken at the
slot. A handler that returns `BW_EVENT_BLOCK` at an event with no block has no
defined meaning; whether the port refuses it, reports it, or treats it as
continue is not decided (still-to-build item 2).

**The typed wrapper.** The port owns one struct per event and freezes its
layout (section 5). The common header is the port's, so a handler can read the
event's identity and size without knowing the concrete type:

```c
typedef struct BwEvent {
    u32 size;   /* sizeof the concrete struct, as the port built it */
    u32 kind;   /* the BW_EVENT_* id of the concrete struct */
} BwEvent;
```

The concrete structs embed `BwEvent` first. Their members carry what
`docs/mod-interface.md`, section 4, says the payload names. The one struct the
document spells out is the encounter:

```c
typedef struct BwEventEncounterStart {
    BwEvent h;
    u32     task;         /* the task the game created */
    u32     battle_kind;  /* the battle kind */
    int     slot;         /* the call's first argument */
} BwEventEncounterStart;
```

The rest, each a member set the port must confirm (still-to-build item 3):

| Event | Struct | Members the payload names |
|---|---|---|
| `encounter.trainer` | `BwEventEncounterTrainer` | the trainer |
| `battle.start` | `BwEventBattleStart` | the setup struct |
| `battle.end` | `BwEventBattleEnd` | the outcome, the mode, the result |
| `catch.attempt` | `BwEventCatchAttempt` | the ball, the target |
| `catch.result` | `BwEventCatchResult` | the result |
| `battle.faint` | `BwEventBattleFaint` | the fainted Pokemon |
| `battle.exp` | `BwEventBattleExp` | the mon, the exp added, whether a level was gained |
| `heal.party` | `BwEventHealParty` | none named |
| `whiteout.begin` | `BwEventWhiteout` | which half, the two halves of section 1 |
| `route.change` | `BwEventRouteChange` | the map |
| `save.begin`, `save.done` | `BwEventSave` | none named |
| `save.load` | `BwEventSaveLoad` | none named |
| `link.up`, `link.down` | `BwEventLink` | the new state |
| `net.send`, `net.receive` | `BwEventNet` | the bytes, the direction |
| `overlay.load`, `overlay.start` | `BwEventOverlay` | the overlay id and its window, from `BwOverlayDesc` |
| a field VM command | `BwEventFieldCmd` | the opcode, the command name, the script work |
| a talk | `BwEventTalk` | the person, the script id, the message id |
| an item | `BwEventItem` | the item id, the count, the pocket |

The identity of a Pokemon in a payload, an index or a handle, and the type of
the result, are not fixed by the evidence and are the port's to fix
(still-to-build item 3). The overlay struct's window fields come from
`sdk/overlay.h`, `BwOverlayDesc` (`ram_address`, `ram_size`, `bss_size`).

**How a handler binds.** Two forms are named and the port must fix which it
reads (`docs/mod-interface.md`, still-to-build item 7): a handler registers in
the mod's own code, or a `subscribe` line in the manifest declares the binding
so the port can report a conflict before the mod runs. The `subscribe` value
is an event name and is declarative only (`docs/mod-interface.md`, section 3;
`sdk/vfs.c`, `read_manifest`). The event name is the permanent binding key, so
a mod bound by name does not rebind when an address moves in aletheia.

## 3. Fan-out and order

Two mods may both want one event. The rule follows the data layer, which is
the rule the whole port already uses (`docs/mod-overrides.md`, "Two mods on one
address"; `sdk/vfs.h`, "The mod list").

- **Load order is the order.** Mods load in ascending manifest `order`; mods
  with equal `order` keep the order they were added in (`sdk/vfs.h`; `sdk/vfs.c`,
  `read_manifest`). Index 0 loads first and wins least; the last index wins
  most (`sdk/vfs.h`, `bw_mod_root_at`).
- **A replacement is later-wins.** Where two mods both replace one address,
  the later mod is installed and the earlier is reported as shadowed, naming
  the address, the winning mod and the shadowed one (`docs/mod-overrides.md`,
  "Two mods on one address"). The port does not compose two replacements.
- **An observation is a fan-out.** Where two mods both subscribe to one event,
  every subscriber runs. There is no winner for an observation: the slot calls
  every handler bound to the event, then calls the original unless a handler
  blocked (`docs/mod-interface.md`, section 4). Subscribers run in load order,
  index 0 first.
- **A block does not stop the later handlers.** The slot calls every handler
  before it decides, and "blocks compose" (`docs/mod-interface.md`, section 4).
  A handler that returns `BW_EVENT_BLOCK` does not prevent a later subscriber
  from running, and a later subscriber's `BW_EVENT_CONTINUE` does not undo an
  earlier block. Whether the block is the disjunction (any handler blocks) or
  the conjunction (every handler must allow) is not decided
  (`docs/mod-interface.md`, still-to-build item 9). The conservative rule is
  any-blocks, and the port reports both.

An event slot therefore has two things bound to it: the subscribers, all of
which run, and the address's override, of which one wins by load order
(`docs/mod-overrides.md`, "The mechanism"). The slot keeps the original
callable under its real name, so a `wrap` mode calls it
(`docs/mod-interface.md`, section 2).

## 4. The state API

A mod reads and writes game state through **accessor functions**, not raw
struct access, so the layout can change behind the accessor. The accessor
address is the DS address of the game function that reads or writes the field,
the form `docs/mod-interface.md`, section 4, names. A mod reaches the accessor
through the DS address registry (`bw_ds_ptr`, `sdk/dsmem.h`) or through the
matched accessor the port defines. The port's mod-facing wrapper, a name and a
fixed signature over the game's accessor, is the ABI the mod calls
(section 5).

The state entries of section 1 are the same accessors in the areas they
belong to. The table below is the consolidated list.

| Port field name | Accessor (game function) | DS address | Type | Read/Write |
|---|---|---|---|---|
| `money` | `BEACON_GetPoints` | 0x0200C274 | u32 | read |
| `money` | `BEACON_AddPoints` | 0x0200C278 | u32 | write (add) |
| `money` | `BEACON_SubPoints` | 0x0200C29C | u32 | write (subtract) |
| `badges` | `BEACON_GetFlag` | 0x0200C234 | boolean by bit | read |
| `badges` | `BEACON_SetFlag` | 0x0200C248 | bit | write |
| `badges` | `BEACON_CountFlags` | 0x0200C258 | u32 count | read |
| `badge_mask` | `TRCARD_SYS_BuildProfile` | 0x0216F40C | u8 mask | read |
| `game_data` | `GAMESYSTEM_GetGameData` | 0x020120F4 | opaque pointer | read |
| `party` | `GAME_DATA_GetMyParty` | 0x02012934 | opaque party handle | read |
| `party` | `POKEPARTY_GetMember` | 0x0201AB28 | opaque mon handle | read |
| `party` | `POKEPARTY_GetCount` | 0x0201AA18 | u32 | read |
| `party` | `POKEPARTY_Add` | 0x0201A98C | mon handle | write (add) |
| `box` | `GAME_DATA_GetBoxManager` | 0x0201293C | opaque box handle | read |
| `box` | `POKE_TOOL_GetBoxValueRaw` | 0x02018E34 | u32 by field id | read |
| `box` | `POKE_TOOL_SetBoxValueRaw` | 0x02019388 | u32 by field id | write |
| `status` | `GAME_DATA_GetMyStatus` | 0x02012944 | 0x20-byte record | read, write |
| `map` | `GAME_DATA_GetPlayerZoneId` | 0x02012AC8 | u32 zone id | read |
| `map` | `GAMESYSTEM_GetFieldMap` | 0x0201210C | opaque field map | read |
| `map` | `GAME_DATA_SetPlayerZoneId` | 0x02012AC4 | u32 zone id | write |
| `save_flags` | `GAME_DATA_GetEventWork` | 0x0201296C | event work block | read |
| `save_flags` | `GAME_DATA_GetEventWorkFromSave` | 0x02012FE4 | save block 0x2d | read |
| `save_flags` | `GAME_DATA_CheckSaveFlag` | 0x02012BF8 | boolean by flag id | read |
| `save_flags` | `SAVE_EVENT_Write` | 0x0202F190 | flag id | write |
| `party_mon_field` | `POKE_TOOL_GetPartyValue` | 0x02017E1C | u32 by field id | read |
| `party_mon_field` | `POKE_TOOL_SetPartyValue` | 0x02017E40 | u32 by field id | write |
| `level` | `POKE_TOOL_GetPartyValue` id 0x9E | 0x02017E1C | u32 | read |
| `exp` | `POKE_TOOL_GetPartyValue` id 0x08 | 0x02017E1C | u32 | read |
| `level_up_pending` | `BTL_POKEPARAM_IsLevelUpPending` | 0x021D67D0 | boolean | read |
| `fainted` | `BTL_POKEPARAM_IsFainted` | 0x021D5B68 | boolean | read |
| `item_count` | `SCRCMD_GetItemQuantity` | 0x021AF684 | u32 | read |
| `item_pocket` | `SCRCMD_GetItemPocket` | 0x021AF6E8 | u32 | read |
| `dex_caught` | `ZUKAN_SAVE_IsCaught` | 0x0200CED4 | boolean | read |
| `link_level` | `WCM_GetLinkLevel` | 0x021666C8 | u32 | read |
| `link_level` | `WCM_CalcLinkLevel` | 0x02166710 | u32 | read |
| `encounter_table` | `FIELDMAP_GetEncount` | 0x02188CE8 | table | read |

Every original row is `docs/mod-interface.md`, section 4, "State fields a rule
may name" and "The other state a mod plausibly needs". The money is the worked
example: the save block with id 0x34, offset +0x00, read by
`BEACON_GetPoints` and written by `BEACON_AddPoints` or `BEACON_SubPoints`,
both capped at 9,999,999. The block is at a run-time address, not a fixed DS
address, reached through `GAME_DATA_GetMiscSave` (0x02012F08), which is
`BEACON_GetBlock` of `SAVE_CONTROL_GetBlock(sys, 0x34)`
(`docs/mod-interface.md`, section 4).

The badges are the same block's flag word. `TRCARD_SYS_BuildProfile`
(0x0216F40C, ov010) builds the trainer card badge mask by looping
`badgeIndex` 0..7 and testing `BEACON_GetFlag(badges, badgeIndex)`, where
`badges` is `GAME_DATA_GetMiscSave` (`src/game/ov010/trcard_sys.c`). The
eight badges are bits 0..7 of the flag word at +0x04. `BEACON_CountFlags`
counts the set bits, and `BTL_SETUP` copies that count into `badgeCount`
(`src/game/arm9/btl_setup.c`). So a badge is a flag write, and the catalogue
states it as state, not as a function call.

One evidence point disagrees. kenon's `docs/events-and-rules.md`, "The
example, worked", states that player money is not named in the four eidos
documents, is not in the declared set, and has no decided field or address.
`docs/mod-interface.md`, section 4, names the field `money`, the save block id
0x34, the offset +0x00, and the read and write accessors above. This document
follows the port document, which is the port side of the contract.

Types the document fixes: the money is a `u32`; the status is a 0x20-byte
record (`MYSTATUS_GetSize` 0x02008468); a party or box record is the 0xDC-byte
record `src/matched/arm9/sub_02018E34.c` maps, a seed word at +0x00, three flag
bits and a checksum at +0x04, an 0x80-byte masked block area at +0x08 and a
0x54-byte masked party area at +0x88. The party field ids are 0x05 species,
0x06 held item, 0x08 experience and 0x9E level; the box value for 0x9E is
derived from experience (`docs/mod-interface.md`, section 4). The party area's
other ids, 0x9D and 0x9F to 0xA6, map to record offsets +0x88 to +0x9A, but
aletheia does not name which is HP or a stat, so that mapping is still to
build (`docs/mod-interface.md`, still-to-build item 15; still-to-build item 4
here). The save flags and the event work are save block 0x2d
(`GAME_DATA_GetEventWorkFromSave`).

The accessors are not in the declared set: they are state accessors, not
override points (`docs/mod-interface.md`, section 4). A mod reaches them the
way it reaches any matched accessor. The port's mod-facing wrapper is a thin
call over the game accessor, so the game's struct layout is not in the ABI
(section 5).

### Writes, and when the game contradicts one

The port does not arbitrate a write. A mod may write the same bytes it reads.
When a mod writes something the game then contradicts, the game's later write
wins; the port restores nothing and offers no transaction
(`docs/mod-interface.md`, section 4; kenon's `docs/mod-contract.md`, "What a
mod may do"). Two cases change what a mod can rely on
(`docs/mod-interface.md`, section 4):

- An ARM9 save-block field written at `save.begin` persists until
  `bw_ds_reset` or a later game write. It is durable state (`sdk/dsmem.h`,
  `bw_ds_reset`).
- An overlay's data is reset on every load: `provider_load` copies the file to
  the window, binds the names and relocates the pointers on each
  `FS_LoadOverlayImage` (`sdk/overlay_image.c`, `provider_load`). A write into
  an overlay's data does not survive the next load of that overlay or of any
  overlay sharing its window.

So a change that must stick is made at the event the game derives the state
from, or through the override's return, not as a write the game recomputes
(`docs/mod-interface.md`, section 4). A "block an action" is the return of a
block on a slot that defines one, not a state write (`docs/modding.md`, "What
block an action means").

### A mod's own state

A mod's own state lives under its `state` namespace, one file or folder under
the save directory, written whole and atomically (`sdk/save.h`,
`bw_save_path`, `bw_save_read`, `bw_save_write_atomic`;
`docs/mod-interface.md`, section 3). The write mechanism exists and is not
mod-facing: nothing namespaces one mod's state from another's, nothing hands a
mod the path, and the load and unload points are not built (`docs/modding.md`,
"Per-mod state persistence: partial"). The port must fix the layout under the
save directory and the callback that reads state before the game runs and
writes it at `save.begin` (`docs/mod-interface.md`, still-to-build item 10;
still-to-build item 5 here).

## 5. The ABI surface

A mod is compiled C and reads the port's structs at a fixed layout, so the
port owns and freezes a small ABI. The list is deliberately small: a small
surface is what makes a stable ABI cheap. The game's own structs are
explicitly not part of it, because aletheia is still restructuring them
(`docs/mod-interface.md`, section 4, the 0xDC-byte record and the party-area
ids it does not name). A mod reaches game state only through the accessor
functions of section 4, so the game's layout can change behind them.

The ABI is:

- `BwEvent`, the common header (`u32 size`, `u32 kind`).
- One event struct per event, `BwEvent` first, with the payload members of
  section 2.
- `BwEventHandler`, the handler type of section 2.
- The return enum, `BW_EVENT_CONTINUE` and `BW_EVENT_BLOCK`.
- The event names of section 1, the permanent binding key.
- The state accessor wrappers of section 4, one fixed signature per field,
  over the game accessor's DS address.

Nothing else is in it. In particular, none of these is in it:

- the game's structs the accessors return, the party record, the box record,
  the 0x20-byte status record and the save blocks;
- the DS addresses of the declared set or of the accessors, which are the
  game's, not the port's;
- the rule language and its field names, which are kenon's work
  (`docs/events-and-rules.md`, "What is not decided");
- the transport, the state file layout, the settings surface and the manifest
  keys beyond `api`.

**The version number.** `api` is the manifest key, an integer, default 1, the
interface version the mod is built against (`docs/mod-interface.md`,
section 3; kenon's `docs/bw-format.md`, "The manifest"). It covers the
port-owned ABI above: the event structs and their layout, the handler type,
the return values, the event names and the accessor prototypes. It does not
cover the game's own structs behind the accessors, the DS addresses, the rule
language, the transport or the state file layout. A mismatch is refused, not
translated; the port must fix what a mismatch does, refuse or warn
(`docs/mod-interface.md`, section 5 and still-to-build item 5). The reason the
distinction matters is that a game struct can change under aletheia's waves
without a mod rebuild, because the accessor hides it, while a change to a
port-owned struct is a new `api`.

The catalogue grew from 18 events to 216 entries (152 events, 17 override
points and 47 state entries), so the ABI grew with it: the event names of
section 1 are part of it, and each new event needs its struct frozen before a
mod binds it. A mod built against a smaller `api` sees fewer names; the port
must fix how a name a mod asks for that the port does not define is handled
(still-to-build item 15).

## 6. The trust model

A mod is native C. It is loaded into the port's process and runs with the
port's full privilege. It can read and write any memory the process holds,
call any function, corrupt the game's state or the port's, hang, and crash the
process. A mod can defeat the on/off switch and the conflict report, because
both are in the same address space it runs in (`docs/mod-interface.md`,
section 5; kenon's `docs/mod-contract.md`, "What the port promises and does
not promise").

The port promises, and only promises (`docs/mod-interface.md`, section 5):

- a deterministic load order, a later-wins rule for a replacement, and a
  visible conflict report (`sdk/vfs.h`, "The mod list";
  `docs/mod-overrides.md`, "Two mods on one address");
- the `bw_hook_enabled` switch over a slot, so one mod's override can be
  isolated while debugging (`sdk/mod_hooks.h`);
- that a build with no mods defines every address the way a build without the
  override machinery does, because matched files are never edited
  (`README.md`, portability rule 1; `docs/mod-overrides.md`, "The base is never
  edited").

The port does not promise isolation, sandboxing, bounded resource use,
validation of a mod's reads or writes, or recovery from a bad write. It does
not promise a stable ABI across `api` versions: a mismatch is refused, not
translated (`docs/mod-interface.md`, section 5). The correct model is that a
mod is a build of the port's own code by another author, trusted to the same
degree (`docs/mod-interface.md`, section 5).

The consequence for the event and state API is that the port validates
nothing. A handler that returns a value other than the two defined, a handler
that writes a field the game then contradicts, and a handler that corrupts the
event pointer are all the mod's responsibility, and the port's promise is only
that the fan-out and the load order are deterministic (section 3).

## Still to build

1. **Fix which addresses each event binds.** `whiteout.begin` is the worked
   case: one concept, two functions, the battle-screen half and the
   field-recover half, and whether one event name binds both or two events
   exist is not decided (`docs/mod-interface.md`, still-to-build item 8;
   `docs/events-and-rules.md`, "What is not decided"). The work is to fix one
   name to many addresses or a second name, for every such concept.
2. **Fix the return contract.** The port defines `BW_EVENT_CONTINUE` and
   `BW_EVENT_BLOCK` and no third return (`docs/mod-interface.md`, section 4).
   The work is to fix what a return value other than the two does, and what a
   `BW_EVENT_BLOCK` at an event that defines no block does. The evidence for
   the two returns is section 4; the evidence against a third is that no mod in
   `docs/modding.md` needs one, because a replacement is the `override` key and
   not a handler return (`docs/mod-interface.md`, section 3).
3. **Freeze the event structs.** Only `BwEventEncounterStart` is spelled out
   (`docs/mod-interface.md`, section 4). The catalogue has 216 entries, so the
   work is to fix each concrete struct's members, the identity of a Pokemon in
   a payload (an index or a handle), the type of `catch.result`, the field-VM
   command payload and the talk and item payloads. The evidence for the
   members is the payload each event's entry names; the evidence for the types
   is not yet in the document.
4. **Name the state accessors and their types.** `docs/mod-interface.md`,
   section 4, names the accessors and their DS addresses. The work is to fix
   the port's mod-facing wrapper name and signature for each, and the type each
   returns, where the document fixes only the money (`u32`), the status
   (0x20 bytes) and the party record (0xDC bytes). The party-area ids 0x9D and
   0x9F to 0xA6 map to record offsets +0x88 to +0x9A and aletheia does not name
   which is HP or a stat (`docs/mod-interface.md`, still-to-build item 15), so
   the port cannot name them before that.
5. **Build the state path and lifecycle.** The save directory and the atomic
   write exist (`sdk/save.h`). The per-mod layout, the namespace, and the
   callback that reads state before the game runs and writes it at `save.begin`
   are not built (`docs/modding.md`, "Per-mod state persistence: partial";
   `docs/mod-interface.md`, still-to-build item 10).
6. **Build the code loader and settle the ABI version.** A dropped-in mod is a
   native shared library loaded at run time, and no such loader exists in the
   tree (`docs/mod-interface.md`, still-to-build item 4). The `api` key is the
   only compatibility statement, and the port must fix whether a mismatch is
   refused or warned (`docs/mod-interface.md`, still-to-build item 5).
7. **Fix how a handler binds.** A mod can register a handler in its code, and
   a `subscribe` line lets the port report a conflict before the mod runs. The
   work is to fix which form the port reads (`docs/mod-interface.md`,
   still-to-build item 7; `docs/mod-contract.md`, "The event declaration").
8. **Fix block composition.** The port must fix whether any handler blocks or
   every handler must allow. The conservative rule is any-blocks, and the port
   reports both (`docs/mod-interface.md`, still-to-build item 9;
   `docs/events-and-rules.md`, "What is not decided").
9. **Fix the link level.** `WCM_GetLinkLevel` (0x021666C8) and
   `WCM_CalcLinkLevel` (0x02166710) are declared for S NS MP
   (`mod/declared_set.txt`) and are bound to the state `link.level`. The work
   is to fix whether the link level is a state read, an event, or an override
   point.
10. **Fix the overlay lifecycle event shape.** `overlay.load` and
    `overlay.start` are in `docs/mod-interface.md`, section 1, which names the
    two lifecycle rows. They are not in the section 4 event table. The work is
    to fix whether they are events or a separate callback, and whether the
    handler runs before or after the port's own load work (`sdk/overlay.h`,
    `bw_overlay_set_provider`; `sdk/overlay_image.c`, `provider_load`).
11. **Fix how `mod_state` is set.** The handler takes the mod's own opaque
    pointer (`docs/mod-interface.md`, section 4). The work is to fix who
    allocates it, when it is set, and how the mod's code entry points receive
    it. It depends on the loader (item 6).
12. **Fix the transport names' meaning.** `net.send` and `net.receive` fire at
    the Wm and DWC entry points and are not built (`docs/mod-interface.md`,
    section 4; `docs/modding.md`, "Networking seam: missing"). The work is to
    fix what bytes and which direction the payload carries, and which of the
    declared Wm, DWC and `GAME_COMM_*` addresses is the send and which the
    receive point. The catalogue adds the Wm, DWC, `GAME_COMM_*` and
    `SCRCMD_NETWORK_*` rows as the transport vocabulary.
13. **Fix the multiplayer action events.** `docs/modding.md`, "Multiplayer",
    names agreement on a battle turn, a trade and a player's position. The
    catalogue has a trade event (`trade.start`), the comm talk, offer, buy and
    sell events, and the move dispatch events; the work is to fix which of them
    is the agreed action and how two instances order them.
14. **Name the battle effect VM's 78 handlers.** `tools/assets/btleff.py` and
    `layout/data_objects.json` give the 78-entry table at 0x02209E88 and the
    operand widths, and the control handlers are named, but the other 74
    handlers are unnamed `func_021F9*`. The work is to name them, so the
    battle effect VM's vocabulary becomes a catalogue like the field VM's.
15. **Fix how a mod binds a name the port does not define.** The catalogue is
    the port's vocabulary, and a mod built against an older `api` may name an
    event the port does not carry. The work is to fix whether the port refuses
    the manifest, reports the unknown name, or ignores it, the way
    `read_manifest` ignores an unknown key (`sdk/vfs.c`, `read_manifest`).

## Related documents

- `docs/mod-interface.md`: the hookable set, the slot, the manifest schema,
  the event and state API, and the trust model.
- `docs/mod-overrides.md`: the function override keyed on a DS address and the
  later-wins conflict rule.
- `docs/modding.md`: what the four example mods need and what exists today.
- `docs/mods.md`: the data and asset surface that exists today.
- `docs/mod-rules.md`: the rule substrate and the field script VM.
- `docs/armdata.md`: how DS addresses become host pointers, and the overlay
  data reset.
- `docs/hooks.md`: the compiled-in hooks and their limits.
- `sdk/dsmem.h`: `bw_ds_ptr` and the DS address registry.
- `sdk/save.h`: the state write discipline.
- `sdk/overlay.h`, `sdk/overlay_image.h`: the loader and the overlay window.
- kenon `docs/events-and-rules.md`: the tooling's view of the same event
  catalogue.
- kenon `docs/mod-contract.md`: the tooling side of the port contract.
