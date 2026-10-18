# Facilities and the wonder trade

This document closes two catalogue gaps. `docs/mod-events.md`, section 7, tests
the catalogue against 27 challenge runs and finds four not expressible:
Wonderlocke, a roguelike run, co-op in the field, and a battle facility. The
roguelike and the co-op run wait on code the catalogue cannot name. The other
two are closable now, because the first is a feature this game does not have and
the second is a facility this game does have and the catalogue misses.

The subject is aletheia, the decompilation of Pokemon Black
(`the aletheia checkout\README.md:3`, "A decompilation
project for Pokemon Black (and eventually White)"). Every address below is
checked by name, address and region against aletheia's per-packet manifests
under `layout/matched/**/pkt_*.json`, revision
`80f06a261e8731f2db94c1a66627bb351ad47673`, and every semantic claim is read
from the matched source the manifest names. No address is invented. The
catalogue's own revision is the same.

## 0. The two answers, in short

- **The wonder trade.** Black has no wonder trade. The manifests and the
  matched sources name no wonder-trade function, and the game predates the
  feature. The nearest flow is the GTS, which aletheia calls the World Trade.
  The catalogue already carries `gts.trade`, the GTS negotiation display, and
  this document adds the GTS entry, negotiation, deposit and receive bindings
  beside it. A Wonderlocke run on Black is a GTS deposit-and-return flow.
- **The battle facility.** Black has one battle facility, the Battle Subway.
  The Battle Institute, the Pokemon World Tournament and the Black Tower or
  White Treehollow are later-game features and are absent from this image. The
  Subway is fully named in aletheia and the catalogue carries none of it; this
  document adds the run entry, the run state, the streak, the opponent
  generation, the rules, the reward and the exit.

## 1. The wonder trade

### 1.1 The feature is absent

A search of aletheia's per-packet manifests and matched sources finds no
wonder-trade function. The only `wonder` names in the tree are the battle
ability Wonder Guard (`HAND_TOKUSEI_GetWonderGuardHandlers`,
`layout/matched/ov093/pkt_ov093_021DAD20.json`), the move Wonder Room
(`HAND_WAZA_AddWonderRoom`,
`layout/matched/ov093/pkt_ov093_021E798C.json`) and the ability Wonder Skin
(`HAND_TOKUSEI_GetWonderSkinHandlers`,
`layout/matched/ov093/pkt_ov093_021DC630.json`). There is no `wondertrade`
symbol, no wonder-trade message, and no wonder-trade search, exchange, receive,
animation or naming function. The catalogue's own note is correct
(`docs/mod-events.md:1236`): aletheia names the GTS negotiation and no
wonder-trade entry.

The absence is the feature's absence, not a naming gap. Wonder Trade is a
later-generation feature; Black predates it. The game's online trade system is
the Global Trade Station, a deposit-and-seek flow that aletheia calls the World
Trade. It has no random exchange.

### 1.2 What it is instead, and the bindings it needs

The GTS is a deposit-and-seek system, not a random exchange. A player deposits
one Pokemon into save block 0x2E and later takes a Pokemon another player
deposited. The flow is named end to end in aletheia:

| Binding | Address | Function | Region | Kind | Evidence |
|---|---|---|---|---|---|
| `gts.entry` | 0x021542C0 | `EVENT_WORLDTRADE_Entry` | ov004 | event | the GTS overlay event; `layout/matched/ov004/pkt_ov004_021542C0.json`; it gathers the save handles (`src/game/ov004/event_worldtrade.c:73-80`) and calls `GAMESYSTEM_CallProc` with ov135's id 0x87 (`:53`) |
| `gts.negotiate` | 0x0215C7E0 | `SCRCMD_PROC_CallGtsNegotiation` | ov010 | event | the script command that launches the negotiation; `layout/matched/ov010/pkt_ov010_0215C7E0.json`; it loads overlay 0x88 and parks the result (`src/matched/ov/ov010_func_0215C7E0.c:53-72`) |
| `gts.negotiate.done` | 0x0215C7C4 | `SCRCMD_PROC_OnGtsNegotiationDone` | ov010 | event | the negotiation callback; `layout/matched/ov010/pkt_ov010_0215C7C4.json`; named by `ov010_func_0215C7E0.c:28` |
| `gts.deposit` | 0x0200AD6C | `WORLDTRADE_GetFromSave` | arm9 | state | reads the GTS deposit record, save block 0x2E, 0xF8 bytes; `layout/matched/arm9/pkt_0200AD6C.json`; `src/game/arm9/worldtrade.c:23-31,41` |
| `gts.deposited` | 0x0200AD8C | `WORLDTRADE_GetIsDeposited` | arm9 | state | reads the deposit flag; `layout/matched/arm9/pkt_0200AD8C.json`; set by `WORLDTRADE_SetIsDeposited` 0x0200AD94 (`src/matched/arm9/func_0200AD94.c`) |
| `gts.receive` | 0x0200AD9C | `WORLDTRADE_CopyPokemon` | arm9 | event | the received mon is copied out of the deposit record; `layout/matched/arm9/pkt_0200AD9C.json`; `src/matched/arm9/WORLDTRADE_CopyPokemon.c:21-24` |

The negotiation display the catalogue already carries is `gts.trade`
(`GTSNEGO_DISP_StartTrade`, 0x021D42D8, ov120), the GTS UI is ov135
(`WORLDTRADE_TITLE_*`, `WORLDTRADE_SEARCH_*`, `WORLDTRADE_LIST_*`,
`WORLDTRADE_DETAIL_*`), and the negotiation state machine is ov001
(`EVENT_GTSNEGO_Entry` 0x02154694, `EVENT_GTSNEGO_Create` 0x0215466C,
`EVENT_GTSNEGO_Main` 0x021542C0, `EVENT_GTSNEGO_InitWork` 0x02154560, all
ov001). The flow's parts, by concept:

- exchange: `EVENT_WORLDTRADE_Entry` (0x021542C0, ov004) and the negotiation
  `EVENT_GTSNEGO_*` (ov001).
- search: `WORLDTRADE_SEARCH_StartDownloadMatchData` (0x021FE6A0, ov135),
  `WORLDTRADE_SEARCH_StateConfirmSearch` (0x021FE60C, ov135).
- receive: `WORLDTRADE_ADAPTER_RegisterReceivedMonB` (0x022023A4, ov135),
  `WORLDTRADE_SERVER_StateFinishTrade` (0x02201CBC, ov135),
  `WORLDTRADE_SERVER_RecordTradeResult` (0x02201B44, ov135).
- naming: `WORLDTRADE_SERVER_StartNameCheck` (0x02201258, ov135),
  `WORLDTRADE_SERVER_WaitNameCheck` (0x02201320, ov135).
- animation: `WORLDTRADE_SUBLCD_*` and `WORLDTRADE_TITLE_*` (ov135).

All of the above are in the per-packet manifests under `layout/matched/ov135`
and `layout/matched/ov001` at the same revision.

### 1.3 What a Wonderlocke run still needs

A Wonderlocke run needs one thing the catalogue does not carry: an event that
fires when the received Pokemon is placed into the party or the box, with the
received mon's identity. The GTS writes the received mon through the box
manager, not through the field script commands `mon.party.add` or `mon.box.add`,
so the catalogue's add events do not fire on this path. `gts.receive`
(`WORLDTRADE_CopyPokemon`, 0x0200AD9C) is the copy, and the placement is the
GTS server step `WORLDTRADE_SERVER_StateFinishTrade` (0x02201CBC, ov135). A
binding on that step is the run's missing piece.

The run also needs the port to build the online path: the GTS is a Wi-Fi
service (WFC), the transport is not built (`docs/mod-events.md:1241`), and the
service is discontinued. A Wonderlocke run on real hardware today needs a
replacement server or a local transport, which is the same missing transport
the networking area already records (`docs/mod-events.md`, "Networking").

## 2. The battle facilities

`progress/EVENT_SURVEY.md:478-489` names four facilities: the Battle Subway,
the Battle Institute, the PWT and the Black Tower or White Treehollow. The
survey is a series-wide list, not a Black-only list. Three of the four are
absent from this image.

### 2.1 The Battle Subway

The Battle Subway exists. Aletheia names it across seven regions: the run
system in ov021 (`bsubway_scr.c`), the facility tool in ov010
(`fld_btl_inst_tool.c`), the save data in arm9 (`bsubway_savedata.c`), the
record list in ov069 (`bsubway_record_list.c`), the train gimmick in ov052
(`field_gimmick_bsubway.c`), the Wi-Fi ranking client in ov116
(`wifi_bsubway.c`), and the multiplayer comm event in ov007
(`event_bsubway_comm.c`). The run work is `BsubwayScrWork`, a 0x7EC-byte
object (`src/game/ov021/bsubway_scr_types.h:283-335`). The run's play, score
and Wi-Fi data are save blocks 0x39, 0x3A and 0x3B
(`src/matched/ov/ov021_func_021C9934.c:117-119`).

| Part | Binding | Address | Function | Region | Kind | Evidence |
|---|---|---|---|---|---|---|
| Run entry | `facility.subway.run.start` | 0x021C9934 | `BSUBWAY_SCR_CreateWork` | ov021 | event | allocates the run work, binds save blocks 0x39/0x3A/0x3B, seeds the streak; `layout/matched/ov021/pkt_ov021_021C9934.json`; `ov021_func_021C9934.c:97-157` |
| Run entry, script side | `facility.subway.run.cmd` | 0x021CA544 | `BSUBWAY_SCR_CmdCreateWork` | ov021 | event | the script command that calls `BSUBWAY_SCR_CreateWork`; `layout/matched/ov021/pkt_ov021_021CA544.json`; `ov021_func_021CA544.c:31-39` |
| Run control | `facility.subway.cmd` | 0x021CA5C4 | `BSUBWAY_SCR_CmdTool` | ov021 | override point | the Subway script-command dispatcher, 137 cases over the work and the save blocks; `layout/matched/ov021/pkt_ov021_021CA5C4.json`; `ov021_func_021CA5C4.c:12-16` |
| Run state | `facility.subway.state` | 0x021CA234 | `BSUBWAY_SCR_GetWinCount` | ov021 | state | reads the work's win counter at +0xE; `layout/matched/ov021/pkt_021CA234.json`; `func_021CA234.c:20-23` |
| Run state | `facility.subway.stage` | 0x021CA2CC | `BSUBWAY_SCR_GetStageNo` | ov021 | state | the round number, `winCount / 7`; `layout/matched/ov021/pkt_021CA2CC.json`; `func_021CA2CC.c:26-29` |
| Run state | `facility.subway.members` | 0x021CA270 | `BSUBWAY_SCR_GetMemberNum` | ov021 | state | the member count per kind; `layout/matched/ov021/pkt_ov021_021CA270.json`; `ov021_func_021CA270.c:20-36` |
| Run state | `facility.subway.party` | 0x021CA210 | `BSUBWAY_SCR_GetParty` | ov021 | state | the party copy the run fights with; `layout/matched/ov021/pkt_ov021_021CA210.json` |
| Streak | `facility.subway.streak` | 0x021CA238 | `BSUBWAY_SCR_SetWinCount` | ov021 | state | writes the win counter; `layout/matched/ov021/pkt_021CA238.json` |
| Streak | `facility.subway.streak.inc` | 0x021CA23C | `BSUBWAY_SCR_IncWinCount` | ov021 | event | saturating increment of +0xE; `layout/matched/ov021/pkt_ov021_021CA23C.json`; `ov021_func_021CA23C.c:21-26` |
| Streak | `facility.subway.streak.clear` | 0x021CA250 | `BSUBWAY_SCR_ClearWinCount` | ov021 | event | zeroes +0xE; `layout/matched/ov021/pkt_021CA250.json`; `func_021CA250.c:26-30` |
| Streak | `facility.subway.streak.round` | 0x021CA258 | `BSUBWAY_SCR_SetWinCountFromRound` | ov021 | event | `round * 7 + wins`, clamped; `layout/matched/ov021/pkt_ov021_021CA258.json`; `ov021_func_021CA258.c:18-25` |
| Streak, win max | `facility.subway.streak.max` | 0x021C9FD0 | `BSUBWAY_SCR_IsWinMax` | ov021 | event | latches once the round count reaches 7; `layout/matched/ov021/pkt_ov021_021C9FD0.json`; `ov021_func_021C9FD0.c:33-41` |
| Opponents | `facility.subway.trainers` | 0x021CA05C | `BSUBWAY_SCR_CreateRoundTrainers` | ov021 | event | draws the round's trainers; `layout/matched/ov021/pkt_ov021_021CA05C.json`; `ov021_func_021CA05C.c:38-65` |
| Opponents | `facility.subway.trainer` | 0x021CBED8 | `BSUBWAY_SCR_CreateTrainer` | ov021 | event | forwards to the ov010 tool; `layout/matched/ov021/pkt_ov021_021CBED8.json`; `ov021_func_021CBED8.c:25-27` |
| Opponents | `facility.subway.trainer.gen` | 0x02168878 | `FLD_BTL_INST_TOOL_CreateTrainer` | ov010 | override point | loads the trainer header and draws the party; `layout/matched/ov010/pkt_ov010_02168878.json`; `fld_btl_inst_tool.c:1191-1197` |
| Opponents | `facility.subway.mon.gen` | 0x02168924 | `FLD_BTL_INST_TOOL_DrawTrainerParty` | ov010 | override point | picks distinct species, applies the item rule, sets the IVs by rank; `layout/matched/ov010/pkt_ov010_02168924.json`; `fld_btl_inst_tool.c:1245-1322` |
| Opponents | `facility.subway.iv` | 0x0216883C | `FLD_BTL_INST_TOOL_GetIvForRank` | ov010 | override point | the IV step table, 3 below rank 50 to 31 at 200; `layout/matched/ov010/pkt_ov010_0216883C.json`; `fld_btl_inst_tool.c:1164-1181` |
| Opponents | `facility.subway.random` | 0x02168824 | `FLD_BTL_INST_TOOL_GetRandomU16` | ov010 | override point | the draw's random source; `layout/matched/ov010/pkt_02168824.json`; `fld_btl_inst_tool.c:1146-1152` |
| Rules | `facility.subway.rules` | 0x021CBC94 | `BSUBWAY_SCR_GetRegulationNo` | ov021 | state | maps the kind to the regulation message id; `layout/matched/ov021/pkt_ov021_021CBC94.json`; `ov021_func_021CBC94.c:20-40` |
| Rules | `facility.subway.rules.data` | 0x0201A354 | `POKE_REGULATION_Load` | arm9 | data table | loads regulation member `a` of archive 0x69; `layout/matched/arm9/pkt_0201A354.json`; `func_0201A354.c:20-23` |
| Rules | `facility.subway.setup` | 0x021CBD08 | `BSUBWAY_SCR_CreateBtlSetup` | ov021 | event | builds the party list and runs the ov010 summary; `layout/matched/ov021/pkt_ov021_021CBD08.json`; `ov021_func_021CBD08.c:49-62` |
| Rules | `facility.subway.setup.gen` | 0x021681C8 | `FLD_BTL_INST_TOOL_CreateBtlSetup` | ov010 | override point | the ov010 battle setup the Subway uses; `layout/matched/ov010/pkt_ov010_021681C8.json` |
| Rules, level bounds | `facility.subway.level` | 0x0201394C | `BTL_SETUP_ChangeToSubway` | arm9 | override point | sets the Subway level bounds 0x46b/0x47d or 0x470/0x480; `layout/matched/arm9/pkt_0201394C.json`; `func_0201394C.c:45-56` |
| Reward | `facility.subway.reward` | 0x021C9DB0 | `BSUBWAY_SCR_AddBattlePoint` | ov021 | event | picks a BP step from the 7x10 table by mode and round, adds it to the score record; `layout/matched/ov021/pkt_ov021_021C9DB0.json`; `ov021_func_021C9DB0.c:42-119` |
| Reward, BP total | `facility.subway.bp` | 0x0200DABC | `GDS_PROFILE_GetScoreRecTotal` | arm9 | state | reads the BP total, the score record's +0x00; `layout/matched/arm9/pkt_0200D6D0.json`; `unit_0200DA64.c:130-133` |
| Reward, BP write | `facility.subway.bp.add` | 0x0200DA80 | `GDS_PROFILE_AddScoreRecTotal` | arm9 | state | adds to the BP total, capped at 9999; `layout/matched/arm9/pkt_0200D6D0.json`; `unit_0200DA64.c:108-117` |
| Reward, streak record | `facility.subway.streak.record` | 0x0200DC0C | `GDS_PROFILE_UpdateScoreRecStreak` | arm9 | state | updates the saved streak byte at +0x02; `layout/matched/arm9/pkt_0200D6D0.json`; `unit_0200DA64.c:14-19` |
| Reward, clear | `facility.subway.clear` | 0x021C9FFC | `BSUBWAY_SCR_SetClearScore` | ov021 | event | commits the streak on a clear and redraws; `layout/matched/ov021/pkt_ov021_021C9FFC.json`; `ov021_func_021C9FFC.c:43-53` |
| Reward, lose | `facility.subway.lose` | 0x021C9D70 | `BSUBWAY_SCR_SetLoseScore` | ov021 | event | commits the streak on a loss and redraws; `layout/matched/ov021/pkt_ov021_021C9D70.json`; `ov021_func_021C9D70.c:39-50` |
| Exit | `facility.subway.exit` | 0x021C9B24 | `BSUBWAY_SCR_DeleteWork` | ov021 | event | frees the party copy and the battle setup, clears the work; `layout/matched/ov021/pkt_ov021_021C9B24.json`; `ov021_func_021C9B24.c:32-47` |
| Exit | `facility.subway.exit.ptr` | 0x021C9924 | `BSUBWAY_SCR_ClearWorkPtr` | ov021 | event | clears the game data's current-event pointer; `layout/matched/ov021/pkt_021C9924.json`; `func_021C9924.c:33-36` |
| Exit | `facility.subway.exit.reset` | 0x021C9D30 | `BSUBWAY_SCR_ResetPlayMode` | ov021 | event | resets the play mode flags; `layout/matched/ov021/pkt_ov021_021C9D30.json`; `ov021_func_021C9D30.c:40-50` |
| Multiplayer entry | `facility.subway.comm` | 0x021542CC | `EVENT_BSUBWAY_COMM_CreateEvent` | ov007 | event | the Subway comm event, `EVENT_BSUBWAY_COMM_Start` 0x021542C0, `EVENT_BSUBWAY_COMM_EventMain` 0x02154310; `layout/matched/ov007/pkt_021542CC.json`; `src/game/ov007/event_bsubway_comm.c` |
| Multiplayer net | `facility.subway.comm.net` | 0x02167AB0 | `BSUBWAY_COMM_NetInit` | ov010 | event | starts the GFL net session; `layout/matched/ov010/pkt_ov010_02167AB0.json`; `ov010_func_02167AB0.c:27-30` |
| History UI | `facility.subway.history` | 0x021B9658 | `BSUBWAY_RECORD_LIST_Main` | ov069 | event | the Subway record list; `layout/matched/ov069/pkt_ov069_021B9658.json`; `src/game/ov069/bsubway_record_list.c` |
| Train gimmick | `facility.subway.train` | 0x021F37C0 | `FIELD_GIMMICK_BSUBWAY_StartTrain` | ov052 | event | the train ride; `layout/matched/ov052/pkt_ov052_021F37C0.json`; `src/game/ov052/field_gimmick_bsubway.c` |
| Wi-Fi ranking | `facility.subway.wifi` | 0x021D6660 | `WIFI_BSUBWAY_Create` | ov116 | event | the ranking upload client; `layout/matched/ov116/pkt_ov116_021D6660.json`; `src/game/ov116/wifi_bsubway.c` |
| Save data | `facility.subway.save` | 0x0200DE5C | `BSUBWAY_SAVEDATA_GetTrainersPresent` | arm9 | state | the Subway save record, its readers and writers; `layout/matched/arm9/pkt_0200DDD0.json`; `src/game/arm9/bsubway_savedata.c:46-56` |
| Save data | `facility.subway.save.stat` | 0x0200DFD8 | `BSUBWAY_SAVEDATA_GetStat` | arm9 | state | the Subway stat table; `layout/matched/arm9/pkt_0200DFD8.json` |
| Data table | `facility.subway.mons` | 0x02168800 | `FLD_BTL_INST_TOOL_ReadMonData` | ov010 | data table | reads archive 214, a/2/1/4, the 1000 battle-facility Pokemon; `layout/matched/ov010/pkt_02168800.json`; `fld_btl_inst_tool.c:1100-1103` |
| Data table | `facility.subway.groups` | 0x0216880C | `FLD_BTL_INST_TOOL_LoadTrainerGroup` | ov010 | data table | reads archive 215, a/2/1/5, the 315 per-group legal Pokemon lists; `layout/matched/ov010/pkt_02168800.json`; `fld_btl_inst_tool.c:1133-1136` |

The archive identifications are `docs/ASSETS.md:10241-10242` (a/2/1/4 and
a/2/1/5, reader `func_02168800`/`func_0216880C` in ov010
`fld_btl_inst_tool.c`). The Subway script/table archive is `a/1/6/5`
(`docs/ASSETS.md:8235`, "Battle Subway (facility) script/table").

### 2.2 The Battle Institute

The Battle Institute is absent. It is a later-game facility. No
`INSTITUTE` name appears in `layout/matched/**/pkt_*.json`, in the matched
sources, or in aletheia's asset survey. `docs/ASSETS.md:10366-10374` records
the archives a/1/6/9 and a/1/6/8 as having "no reader in the image" and says
"the domain question, which facility, has no answer in Black's code", which is
the same negative: the facility the records belong to is not in Black.

### 2.3 The Pokemon World Tournament

The PWT is absent. It is a later-game facility. No `PWT` or tournament name
appears in the manifests or the matched sources. Aletheia's ov114 survey names
"a Pokemon GTS and Battle Subway web client"
(`docs/sessions/2026-09-27/reports/vendor_scan.md:33`), which is the Subway's
Wi-Fi ranking, not a tournament.

### 2.4 The Black Tower and the White Treehollow

Both are absent. They are later-game facilities. No `TOWER`, `TREEHOLLOW` or
`TREEHALL` name appears in the manifests or the matched sources. The Battle
Subway is the only postgame battle facility this image builds.

## 3. The four kinds

Every binding above is one of the four kinds `docs/mod-events.md` fixes:

- **Event, a mod observes or blocks.** `gts.entry`, `gts.negotiate`,
  `gts.negotiate.done`, `gts.receive`; `facility.subway.run.start`,
  `facility.subway.run.cmd`, `facility.subway.streak.inc`,
  `facility.subway.streak.clear`, `facility.subway.streak.round`,
  `facility.subway.streak.max`, `facility.subway.trainers`,
  `facility.subway.trainer`, `facility.subway.setup`,
  `facility.subway.reward`, `facility.subway.clear`,
  `facility.subway.lose`, `facility.subway.exit`,
  `facility.subway.exit.ptr`, `facility.subway.exit.reset`,
  `facility.subway.comm`, `facility.subway.comm.net`,
  `facility.subway.history`, `facility.subway.train`,
  `facility.subway.wifi`. The Subway run events are observation only. Whether
  any defines a block is the port's to fix, the same open question
  `docs/mod-events.md`, section 2, records for the other action events.
- **State entry, a mod reads.** `gts.deposit`, `gts.deposited`;
  `facility.subway.state`, `facility.subway.stage`,
  `facility.subway.members`, `facility.subway.party`,
  `facility.subway.streak`, `facility.subway.rules`,
  `facility.subway.bp`, `facility.subway.bp.add`,
  `facility.subway.streak.record`, `facility.subway.save`,
  `facility.subway.save.stat`. A state entry needs no slot.
- **Override point, a mod replaces or wraps.** `facility.subway.cmd`
  (`BSUBWAY_SCR_CmdTool`), `facility.subway.trainer.gen`,
  `facility.subway.mon.gen`, `facility.subway.iv`,
  `facility.subway.random`, `facility.subway.setup.gen`,
  `facility.subway.level`.
- **Data table, a mod replaces as a whole file or a NARC member.**
  `facility.subway.mons` (a/2/1/4), `facility.subway.groups` (a/2/1/5),
  `facility.subway.rules.data` (regulation archive 0x69), and the Subway
  script/table archive a/1/6/5. A data table has no DS address and takes no
  slot.

## 4. What a run needs beyond the bindings

A binding is a name and an address. It is not a working run. The difference
matters, and it is the same difference `docs/mod-events.md`, section 7, draws
between expressible and built.

A Battle Subway run needs, beyond the bindings above:

- **The field and the script VM to run.** The Subway is driven by its own
  script system in ov021 (`BSUBWAY_SCR_CmdTool`, 0x021CA5C4) over the field
  script VM (`SCRIPT_SYS_CreateVm` 0x02158BFC, `SCRIPT_SYS_Main` 0x0215882C).
  The port must build the field system and the script VM, which is the same
  field work every other run waits on (`progress/EVENT_SURVEY.md`, "What the
  port reaches today"; `docs/mod-events.md`, section 7).
- **The Subway's overlays to load.** ov021 (the run), ov010 (the facility
  tool), ov069 (the record list), ov052 (the train), ov116 (the ranking) and
  ov007 (the comm event). The overlay loader is not built
  (`docs/mod-events.md:1303-1308`).
- **The Subway's zones and script data.** Zones 0x43 to 0x49 are the Subway
  trains (`BSUBWAY_SCR_GetTrainNo`, 0x021CBCC4, `ov021_func_021CBCC4.c:25-53`),
  and the run's scripts are archive a/1/6/5. The port must reach those zones.
- **The battle path.** The generated trainers run through the battle setup
  (`BTL_SETUP_ChangeToSubway` 0x0201394C, `FLD_BTL_INST_TOOL_CreateBtlSetup`
  0x021681C8) and the battle server (ov093). The battle path is the same work
  the battle events wait on.
- **The save blocks.** The run reads and writes save blocks 0x39, 0x3A and
  0x3B (`ov021_func_021C9934.c:117-119`), and the BP total is the score
  record's +0x00 (`unit_0200DA64.c:130-133`). The port must build the save
  path.
- **The transport, for the multiplayer and Wi-Fi parts.**
  `BSUBWAY_COMM_NetInit` (0x02167AB0, ov010) starts a GFL net session and
  `WIFI_BSUBWAY_*` (ov116) uploads to a WFC server. Both need the transport,
  which is not built (`docs/mod-events.md:1241`).

A Wonderlocke run needs the same field work, plus the online path: the GTS is a
WFC service, the transport is not built, and the service is discontinued. The
catalogue's bindings make the flow nameable; the port's transport makes it run.

## 5. The payloads

Each new binding needs a payload, in the shape `docs/mod-payloads.md` fixes: a
port-owned struct with the common `BwEvent` header first, filled from the
call's arguments and the state the port can read there. The outlines below name
the struct and its members; the merge folds them into `docs/mod-payloads.md`.

### 5.1 The GTS

| Event | Struct | Members |
|---|---|---|
| `gts.entry` | `BwEventGtsEntry` | `h`; `save` (the save control the event gathers, `event_worldtrade.c:73-80`); `mode` (the entry argument) |
| `gts.negotiate` | `BwEventGtsNegotiate` | `h`; `vm` (the script VM, the call's `c`); `work` (the script work, `ov010_func_0215C7E0.c:62`); `slot` (the script result slot, `:70`) |
| `gts.negotiate.done` | `BwEventGtsNegotiateDone` | `h`; `result` (the negotiation result the callback receives) |
| `gts.receive` | `BwEventGtsReceive` | `h`; `mon` (a `BwPokeRef` over the copied record, `WORLDTRADE_CopyPokemon.c:21-24`); `box` (the destination box, `WORLDTRADE_GetReturnBox`) |
| `gts.deposit` (state) | no struct | reads the 0xF8-byte save block 0x2E: `pokemon` 0xDC bytes, `depositInfo` 0x10 bytes, `isDeposited`, `returnBox`, `lastTradeId`, `lastSearchId` (`src/game/arm9/worldtrade.c:24-31`) |

### 5.2 The Battle Subway

| Event | Struct | Members |
|---|---|---|
| `facility.subway.run.start` | `BwEventSubwayRunStart` | `h`; `work` (the `BsubwayScrWork` pointer); `mode` (0 new, nonzero resume, `ov021_func_021C9934.c:122`); `kind` (the play kind, `:123`); `play_data`, `score_data`, `wifi_data` (the three save blocks, `:117-119`) |
| `facility.subway.run.cmd` | `BwEventSubwayRunCmd` | `h`; `vm`; `work`; `mode`; `kind` (`ov021_func_021CA544.c:31-39`) |
| `facility.subway.trainers` | `BwEventSubwayTrainers` | `h`; `work`; `round` (the round count, `ov021_func_021CA05C.c:40`); `kind` (`p[9]`) |
| `facility.subway.trainer` | `BwEventSubwayTrainer` | `h`; `trainer` (the `BtlInstTrainer` pointer); `index` (the trainer number); `count` (the party size); `draw` (the `BtlInstDraw`) |
| `facility.subway.setup` | `BwEventSubwaySetup` | `h`; `work`; `party` (the built party list); `mode`; `setup` (the returned `BtlSetup`) |
| `facility.subway.reward` | `BwEventSubwayReward` | `h`; `work`; `kind`; `round` (`GDS_PROFILE_GetScoreRecCountCDec`); `points` (the returned BP step, `ov021_func_021C9DB0.c:118-119`) |
| `facility.subway.clear` | `BwEventSubwayClear` | `h`; `work`; `kind`; `streak` (`BSUBWAY_SCR_GetWinCount`, `ov021_func_021C9FFC.c:46`) |
| `facility.subway.lose` | `BwEventSubwayLose` | `h`; `work`; `kind`; `streak` (`ov021_func_021C9D70.c:41-42`) |
| `facility.subway.streak.inc` | `BwEventSubwayStreak` | `h`; `work`; `streak_before`; `streak_after` (`ov021_func_021CA23C.c:23-25`) |
| `facility.subway.streak.max` | `BwEventSubwayStreakMax` | `h`; `work`; `round` (`GDS_PROFILE_GetFieldRecCount03`, `ov021_func_021C9FD0.c:36`) |
| `facility.subway.exit` | `BwEventSubwayExit` | `h`; `work`; `setup` (the battle setup freed, `ov021_func_021C9B24.c:39-41`) |
| `facility.subway.comm` | `BwEventSubwayComm` | `h`; `mode`; `owner` (the field object, `ov007_func_021542CC.c:40-53`) |

| Override point | Argument set |
|---|---|
| `facility.subway.cmd` | `(void *vm, void *work)`, the Subway script command dispatcher; the case id and the two operands (`ov021_func_021CA5C4.c:12-16`) |
| `facility.subway.trainer.gen` | `(BtlInstTrainer *trainer, u16 index, int count, u16 *exclSpecies, u16 *exclItems, BtlInstDraw *draw, u16 heap)` (`fld_btl_inst_tool.c:1191`) |
| `facility.subway.mon.gen` | `(BtlInstGroup *group, u16 rank, BattleMon *mons, int count, u16 *exclSpecies, u16 *exclItems, BtlInstDraw *draw, u16 heap)` (`fld_btl_inst_tool.c:1245`) |
| `facility.subway.iv` | `(u32 rank)`, returns the IV byte (`fld_btl_inst_tool.c:1164`) |
| `facility.subway.random` | `(void)`, returns a `u16` (`fld_btl_inst_tool.c:1146`) |
| `facility.subway.setup.gen` | `(void *sys, void *party, u32 mode, struct Rec *a, struct Rec *b, int n)` (`fld_btl_inst_tool.c` header; `ov021_func_021CBD08.c:46`) |
| `facility.subway.level` | `(struct ZoneData *o)`, the state word and the trainer kind (`func_0201394C.c:45-56`) |

| State entry | Value and inputs |
|---|---|
| `facility.subway.state` | the win counter, `u16` at the work's +0xE (`func_021CA234.c:20-23`) |
| `facility.subway.stage` | the round, `winCount / 7` (`func_021CA2CC.c:26-29`) |
| `facility.subway.members` | the member count per kind, 0 to 4 (`ov021_func_021CA270.c:20-36`) |
| `facility.subway.party` | the run's party copy, the work's +0x7C8 (`ov021_func_021C9934.c:139`) |
| `facility.subway.streak` | the win counter, written by `SetWinCount` (`pkt_021CA238.json`) |
| `facility.subway.rules` | the regulation message id, 20, 21 or 22, by kind (`ov021_func_021CBC94.c:20-40`) |
| `facility.subway.bp` | the BP total, `u16` at the score record's +0x00, capped at 9999 (`unit_0200DA64.c:108-133`) |
| `facility.subway.streak.record` | the saved streak byte at the score record's +0x02 (`unit_0200DA64.c:14-19`) |
| `facility.subway.save` | the Subway save record, 7 trainer entries of 0xF0 bytes and 30 history entries (`src/game/arm9/bsubway_savedata.c:23-56`) |

## 6. What was not found

- **No wonder-trade entry.** The feature is not in Black. The manifests and
  the matched sources carry no wonder-trade function. The GTS bindings above
  are the nearest flow, and `gts.receive` on
  `WORLDTRADE_SERVER_StateFinishTrade` (0x02201CBC, ov135) is the run's missing
  piece, because the catalogue has no event for the received mon's placement.
- **No Battle Institute, no PWT, no Black Tower or White Treehollow.** All
  three are later-game facilities and are absent from this image. The survey
  lists them because it is a series-wide list, not a Black-only list.
- **No single Subway run event.** The Subway is a script-command system, not
  one function. The run is a set of events and override points over
  `BsubwayScrWork`, not a `facility.subway.run` that fires once.
