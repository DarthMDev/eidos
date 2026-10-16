# Payloads: save, overlay lifecycle and networking

This document specifies the payloads for the system areas of
`docs/mod-events.md`, section 1: the six save entries, the two overlay
lifecycle events, the three link entries, the two net entries, the nine comm
entries, the eleven wm entries, the two dwc entries and the five network
entries. That is forty entries.

A payload is a situation, not an entity. It carries the subject, the owner it
belongs to, the cause that led to it and the circumstances around it, so a
handler can decide and act rather than only log. A field is included when a mod
plausibly wants it; a field removed later is cheap and a field added later is
not.

The port owns each payload struct and fills it at the call
(`docs/mod-events.md`, section 5; `docs/mod-interface.md`, section 4). A mod is
compiled C and reads the struct at a fixed layout, so the game's own structs are
not exposed. Every struct embeds the common header `BwEvent` first
(`u32 size`, `u32 kind`; `docs/mod-events.md`, section 2). The header is not
repeated in the tables below.

## How to read the tables

Each field names where it comes from, with the file and line it was read at,
and the kind of source:

- **arg**: the call's argument.
- **field**: a struct field read through an argument at the call site.
- **call**: the port calls a game function at the call site, or reads a global
  the game keeps there.
- **ret**: the original's return value, read after the original runs.
- **ctx**: the port tracks it, because it is not in scope at the call. The
  context section names what feeds it and where it is cleared.

The two special areas get their own treatment. Networking has no transport
anywhere in the tree, so the wm, dwc, comm and network entries state what the
payload would carry when a transport exists and mark the transport itself still
to build. The overlay events fire around the port's own loader, so each states
what a handler can rely on at that point.

The owner of a system event is the local player. The port fills `owner` with 0
for the local instance. A cross-instance player identity is not obtainable today
and is still to build; the peer of a network event is separate and comes from
the network context.

## 1. Save and load

The save control object is the object every save entry operates on. It is
`Obj11223` at the call (`src/matched/include/Obj11223.h`, lines 3 to 8) and the
same object as `Sub02007168` (`src/matched/arm9/func_02007168.c`, lines 32 to
44): `f3` is the save kind byte, the bytes at +4 and +5 are the guard flags,
+0 and +1 are the completion flags, and +0x10 is the backup handle.

### save.begin (0x02007150, arm9)

`int SAVE_CONTROL_StartSave(int a0)` (`src/matched/arm9/func_02007150.c`,
line 17). The save control object exists and the game has not written yet.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `save_control` | u32 | arg `a0` (func_02007150.c:17) | Names the save object; a mod matches it to `save.done` and `save.load` |
| `backup_handle` | u32 | field `Obj11223.f10` (Obj11223.h:7; func_02007150.c:17) | The backup target the write goes to, so a mod can tell a real save from a slot change |
| `save_kind` | u8 | call `func_02008754(a0)` (func_02007150.c:17) | The kind byte the game stores at +3; separates a full save from a sub-save. The port calls the callee or reads `f3` after the original |
| `guard_flags` | u16 | field bytes +4 and +5 (`Sub02007168.unk04`, `unk05`; func_02007168.c:37 to 38) | Nonzero means the save is a no-op (func_02007168.c:54); a mod should not treat it as a write |
| `completion_flags` | u16 | field bytes +0 and +1 (`Sub02007168.unk00`, `unk01`; func_02007168.c:33 to 34) | The pair the game sets to (0, 1) when a save completes (func_02007168.c:62 to 63); a mod reads the prior state |
| `owner` | u32 | local player, 0 (docs/mod-events.md, the owner framing) | Attributes the save to a player in a soullink or multiplayer game |

Cause, which is one call earlier and is not in scope: the requester of the save.
`GAME_DATA_SaveStart` (0x02012DAC) calls this after it sets the game data's
+0x1CE byte to 1 and sets UI flag 39 (`src/matched/arm9/func_02012DAC.c`,
lines 21) and after `WIH_DWC_PauseTimers`. The port tracks the requester only if
it also hooks 0x02012DAC, which is not in `mod/declared_set.txt`. The port
clears the tracked requester at `save.done` and at process reset.

### save.done (0x02007168, arm9)

`u32 SAVE_CONTROL_MainSave(struct Sub02007168 *p)`
(`src/matched/arm9/func_02007168.c`, lines 50 to 66). The write has run or the
early guard fired.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `save_control` | u32 | arg `p` (func_02007168.c:50) | Names the save object, matching `save.begin` |
| `backup_handle` | u32 | field `Sub02007168.unk10` (func_02007168.c:43, 60) | The backup target the write used |
| `guard_flags` | u16 | field bytes +4 and +5 (func_02007168.c:37 to 38, 54) | Nonzero means the early arm returns 0 and the save did not run |
| `completion_flags` | u16 | field bytes +0 and +1 (func_02007168.c:33 to 34, 62 to 63) | The pair set to (0, 1) on a completed save; a mod persists its state on it |
| `result` | u32 | ret of `SAVE_CONTROL_MainSave` (func_02007168.c:52, 60, 65) | 0 on the early arm, else the backup result; the mod's state write should follow a success |
| `owner` | u32 | local player, 0 | Attributes the save to a player |

The result is ahead at entry. The port obtains it by running the original and
capturing its return before the handlers, or by tracking it. If the port keeps
the entry rule, `result` is absent and a mod reads the completion flags instead.
The save is observation only, so running the original first costs nothing.

### save.load (0x02012C18, arm9)

`void GAME_DATA_LoadFromSave(u8 *p)`
(`src/matched/arm9/func_02012C18.c`, lines 95 to 131). `p` is the game data
object `Obj00054` (`src/matched/include/Obj00054.h`, lines 3 to 46). The load
copies the save's blocks into the game data.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `game_data` | u32 | arg `p` (func_02012C18.c:95) | The game data object the load fills; a mod reads it at `save.done` later |
| `save_control` | u32 | field `Obj00054.f0` (Obj00054.h:4; func_02012C18.c:100, 104) | The save control the blocks come through |
| `skipped` | u8 | call `func_020071E4(save_control) == 1` (func_02012C18.c:100) | The load returns early on this guard; a mod must not read the blocks as freshly loaded when it is set |
| `event_work` | u32 | call `GAME_DATA_GetEventWorkFromSave(save_control)` = block 0x2d (func_02012FE4.c:17) | The save flags and event work a Nuzlocke mod keys its used set on |
| `occupy_list` | u32 | call `func_020071CC(save_control, 0x29)` (func_02012C18.c:109) | The occupy block the load copies into the occupy info |
| `player_work` | u32 | call `GAME_DATA_GetMyPlayerWork(p)` (func_02012C18.c:104) | The player record the load fills, the owner's identity |
| `occupy_info` | u32 | call `GAME_DATA_GetOccupyInfo(p)` (func_02012C18.c:108) | The occupy info the load fills |
| `battle_rec_slot` | u32 | call `GAME_DATA_GetLastBattleRecSlot(p)` (func_02012C18.c:111) | The last battle record the load fills; a mod reads the prior battle |
| `second_union_info` | u32 | call `GAME_DATA_GetSecondUnionInfo(p)` (func_02012C18.c:113) | The union info the load fills; multiplayer state |
| `musical_shot` | u32 | call `MUSICAL_SHOT_GetFromSave(save_control)` (func_02012C18.c:114) | The saved musical data the load copies |
| `wifi_list` | u32 | call `GAME_DATA_GetWifiList(p)` (func_02012C18.c:122) | The saved WiFi list the load copies; a soullink mod reads it |
| `play_time` | u32 | call `func_02008748(save_control)` (func_02012C18.c:129) | The saved play time the load copies into `f6AC` |
| `play_time_dest` | u32 | field `Obj00054.f6AC` (Obj00054.h:45; func_02012C18.c:130) | The destination the play time lands in |
| `owner` | u32 | local player, 0 | Attributes the load to a player |

Cause, not in scope: which slot or menu requested the load. The port tracks it
from the save menu if it hooks it; the catalogue does not declare such a point.
The port clears it at the next `save.begin`.

### save.flag (0x02012BF8, arm9, state)

`int GAME_DATA_CheckSaveFlag(int a0, int a1)`
(`src/matched/arm9/func_02012BF8.c`, line 18). A state accessor, not a slot.

| Accessor | Inputs | Output | Where it comes from | Why a mod wants it |
|---|---|---|---|---|
| `bw_save_flag` | `a0` object, `a1` flag id | int | `RECENT_ID_LIST_SAVEDATA_ContainsId(RECENT_ID_LIST_SAVEDATA_GetBlockFromSys(GAME_DATA_GetSaveControlWork(a0)), a1)` (func_02012BF8.c:18) | Tests a save flag by id; Nuzlocke and Soullink read their flags here |

The flag id is the caller's `a1`. The port's wrapper takes the id and returns
the boolean. The block behind it is the recent-id list block
(`src/matched/arm9/unit_02012BE0.c`, lines 45 to 47).

### save.block (0x020071CC, arm9, override)

`void *SAVE_CONTROL_GetBlock(struct Sub020071CCCtrl *ctrl, u32 id)`
(`src/matched/arm9/unit_020071CC.c`, lines 58 to 61). An override point.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `save_control` | u32 | arg `ctrl` (unit_020071CC.c:58) | Names the save object |
| `block_id` | u32 | arg `id` (unit_020071CC.c:58) | Which block is asked for; the money and badges block is 0x34 |
| `backup_work` | u32 | field `Sub020071CCCtrl.unk10` (unit_020071CC.c:43 to 54, 60) | The backup work the block comes from |
| `block` | u32 | ret of `BACKUP_SYSTEM_GetRecordWork(ctrl->unk10, id)` (unit_020071CC.c:60) | The block pointer; a wrap handler may replace or inspect it |

### save.misc (0x02012F08, arm9, override)

`void *GAME_DATA_GetMiscSave(int *p)` (`src/matched/arm9/func_02012F08.c`,
lines 16 to 20). An override point.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `game_data` | u32 | arg `p` (func_02012F08.c:17) | The game data object whose first word is the save control |
| `save_control` | u32 | field `*p` (func_02012F08.c:19) | The save control the block comes through |
| `block` | u32 | ret of `BEACON_GetBlock(*p)` = block 0x34 (unit_0200C0E8.c:126 to 128; func_02012F08.c:19) | The misc block that holds money at +0x00 and the badge flags at +0x04 |

## 2. Overlay lifecycle

The port owns the loader. `sdk/overlay_image.c` installs a provider through
`bw_overlay_set_provider` (`sdk/overlay.h`, lines 137 to 143): `provider_load`
copies the file to the window, binds the names and relocates the pointers
(`sdk/overlay_image.c`, lines 226 to 253), and `provider_start` runs the static
initialisers (lines 255 to 266). The descriptor is `BwOverlayDesc`
(`sdk/overlay.h`, lines 52 to 69). The DS struct is `FSOverlayInfo`
(`src/matched/arm9_sdk/func_0207902C.c`, lines 38 to 56).

What a handler can rely on and cannot, because the events fire around the
port's own loader:

- Before the load work, the descriptor is known and the overlay is not
  resident. The window still holds the previous overlay's bytes and the new
  overlay's data is not there yet. A handler cannot read the new overlay's data
  or call its functions.
- After the load work and before the static initialisers, the image is copied,
  bound and relocated, so a handler can read the fresh data, but the overlay's
  own initialisers have not run.
- After the static initialisers, both the data and the initialised state are
  there.
- The port must fix where the handler runs relative to these steps
  (`docs/mod-events.md`, still-to-build item 10). The tables below give the
  fields that are known at either point.

### overlay.load (0x02078EB0, arm9)

`BOOL FS_LoadOverlayImage(FSOverlayInfo *p_ovi)`
(`src/matched/arm9_sdk/func_02078EB0.c`, lines 73 to 91).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `overlay_id` | u32 | field `FSOverlayInfo.id` (func_0207902C.c:40; func_02078EB0.c:79) | Which overlay loads; a mod keys per-overlay work on it |
| `ram_address` | u32 | field `ram_address` (func_0207902C.c:41; func_02078EB0.c:84) | The window the image lands in; the key to which overlay it displaces |
| `ram_size` | u32 | field `ram_size` (func_0207902C.c:42) | The code and initialised data extent |
| `bss_size` | u32 | field `bss_size` (func_0207902C.c:43) | The zeroed extent after the image |
| `sinit_init` | u32 | field `sinit_init` (func_0207902C.c:44) | The first static initialiser, a DS address |
| `sinit_init_end` | u32 | field `sinit_init_end` (func_0207902C.c:45) | The initialiser list end |
| `sinit_count` | u32 | call `(sinit_init_end - sinit_init) / 4` (func_0207902C.c:117 to 118) | How many initialisers run at `overlay.start` |
| `file_id` | u32 | field `file_id` (func_0207902C.c:46) or call `FS_GetOverlayFileID(p_ovi)` (func_02078EB0.c:79) | The ROM file the image is read from |
| `compressed` | u32 | field `compressed` (func_0207902C.c:47) | The low 24 bits are the compressed size; a mod sees the encoding |
| `flag` | u8 | field `compressed >> 24` (func_0207902C.c:48) | Bit 0 is compressed, bit 1 carries the digest check (func_0207902C.c:94, 111) |
| `target` | u32 | field `FSOverlayInfo.target` (func_0207902C.c:54) | ARM9 or ARM7; the port loads ARM9 overlays |
| `file_size` | u32 | call `FSi_GetOverlayBinarySize(p_ovi)` (func_02078EB0.c:81) | The byte count read into the window |
| `result` | u32 | ret of `FS_LoadOverlayImage` (func_02078EB0.c:75, 86) | 1 when the file opened and read whole; the port owns this outcome |
| `window_was_resident` | u32 | ctx, call `bw_ovl_resident_at(ram_address)` (sdk/overlay_image.c:283 to 313) | The overlay the window held, so a mod can tell a reload from a fresh load |
| `displaces` | u32 | ctx, the resident overlay whose window overlaps (sdk/overlay_image.c:213 to 224) | Which overlay's data is lost; a mod that wrote it must not rely on it |
| `reload` | u8 | ctx, `bw_overlay_is_loaded(overlay_id)` (sdk/overlay.h:146) | A reload resets the overlay's data; a mod re-applies its write |
| `owner` | u32 | local player, 0 | Attributes the load to a player |

### overlay.start (0x0207902C, arm9)

`void FS_StartOverlay(FSOverlayInfo *p_ovi)`
(`src/matched/arm9_sdk/func_0207902C.c`, lines 78 to 128). The image is loaded;
the static initialisers run here.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `overlay_id` | u32 | field `FSOverlayInfo.id` (func_0207902C.c:98) | Which overlay starts |
| `ram_address` | u32 | field `ram_address` (func_0207902C.c:82, 106, 113) | The window the initialisers run over |
| `ram_size` | u32 | field `ram_size` (func_0207902C.c:115) | The extent flushed before the initialisers |
| `bss_size` | u32 | field `bss_size` (func_0207902C.c:43) | The zeroed extent |
| `sinit_init` | u32 | field `sinit_init` (func_0207902C.c:117) | The first initialiser |
| `sinit_init_end` | u32 | field `sinit_init_end` (func_0207902C.c:118) | The list end |
| `sinit_count` | u32 | call `(sinit_init_end - sinit_init) / 4` (func_0207902C.c:117 to 118) | How many initialisers run; a mod knows the count before and after |
| `digest_ok` | u8 | call `FSi_CompareDigest(...)` ret (func_0207902C.c:100) | 1 when the image passed the digest check; a failed check terminates |
| `decompressed` | u8 | field `flag & 1` (func_0207902C.c:111) | Whether the image is decompressed here |
| `twl_autoload` | u8 | call `OS_IsRunOnTwl()` and the address window (func_0207902C.c:82 to 88) | Whether the TWL autoload path is taken; a mod knows which build runs |
| `owner` | u32 | local player, 0 | Attributes the start to a player |

## 3. Link

### link.up, link.down (0x0215CD24, ov009)

`int SOCL_LinkIsOn(void)` returns `WCM_GetApMacAddress() != 0`
(`src/matched/ov_sdk/ov009_func_0215CD24.c`, lines 20 to 23). One address, two
events, distinguished by the return. The predicate is called often, so the port
fires the event only when the state changes and tracks the previous state.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `link_on` | u8 | call `WCM_GetApMacAddress() != 0` (ov009_func_0215CD24.c:22) | The new link state; `link.up` when set and `link.down` when clear |
| `previous_on` | u8 | ctx, the last link state | Distinguishes a change from a poll; a mod must not act on every call |
| `changed` | u8 | ctx, `link_on != previous_on` | A mod acts only on a real change |
| `ap_mac` | u8[6] | call `WCM_GetApMacAddress()` (ov009_func_0215CD24.c:18, 22) | The access point address, the one identifier the predicate offers |
| `ap_present` | u8 | call, the pointer is not 0 (ov009_func_0215CD24.c:22) | The same bit as `link_on`, kept for a mod that logs the pointer |
| `link_level` | u32 | call `WCM_GetLinkLevel()` (ov009_func_021666C8.c:42) | The signal level at the change; a soullink mod warns on a weak link |
| `rssi` | u8 | call `WCMi_GetRssiAverage()` (ov009_func_021666C8.c:52) | The raw signal behind the level |
| `device_id` | u8 | field `WCMWork.deviceId` (ov009_func_02166710.c:39) | The RF device, which selects the level thresholds |
| `owner` | u32 | local player, 0 | Attributes the link to a player |
| `peer` | u32 | ctx, not obtainable here | Which remote player is on the other end; the AP MAC is not the peer |

The peer is not obtainable at this predicate. `SOCL_LinkIsOn` returns a boolean
over the access point MAC, not a peer identity. The port takes the peer from the
network context, which the WM AID and the DWC profile feed; see section 9. The
port clears `previous_on` at process reset and at `link.down`.

### link.level (0x021666C8, 0x02166710, ov009, state)

`WMLinkLevel WCM_GetLinkLevel(void)` (`src/matched/ov_sdk/ov009_func_021666C8.c`,
lines 42 to 56) and `WMLinkLevel WCM_CalcLinkLevel(unsigned char rssi)`
(`src/matched/ov_sdk/ov009_func_02166710.c`, lines 34 to 64). State, not a slot.

| Accessor | Inputs | Output | Where it comes from | Why a mod wants it |
|---|---|---|---|---|
| `bw_link_level` | none | u32 0 to 3 | `WCM_GetLinkLevel` (ov009_func_021666C8.c:42) | The link quality; a soullink mod acts on a weak link |
| `bw_link_rssi` | none | u8 | `WCMi_GetRssiAverage()` (ov009_func_021666C8.c:52) | The raw signal behind the level |
| `bw_link_phase` | none | u8 | field `WCMWork.phase` (ov009_func_021666C8.c:33, 51) | The level is nonzero only when phase is 9; a mod distinguishes "down" from "not connected" |
| `bw_link_device_id` | none | u8 | field `WCMWork.deviceId` (ov009_func_02166710.c:29, 39) | The device whose thresholds apply |

## 4. Net

### net.send, net.receive (not built)

No transport exists. `sdk/arm7/` has no Wm or DWC responder and the tree has no
sockets (`docs/modding.md`, "Networking seam: missing"; `docs/modding.md`, the
capability matrix, "Transport between two instances: missing"). The names are
fixed so a mod does not rebind when a transport lands (`docs/mod-interface.md`,
lines 622 to 625). The candidate send and receive points are the Wm and DWC
entry points of section 6 and section 7.

What the payload carries once a transport exists:

| Field | Type | Where it would come from | Why a mod wants it |
|---|---|---|---|
| `transport` | u8 | ctx, which of Wm or DWC | The two transports carry different frames |
| `direction` | u8 | ctx, send or receive | One field for both events |
| `peer` | u32 | ctx, the remote station id | Which instance sent or receives; the agreement is per peer |
| `local_aid` | u16 | field `WMArm9Buf.myAid` (WmReceiveFifo.c:72) | The local station id in the multipoint group |
| `port` | u16 | arg `port` (WM_SetMPDataToPortEx.c:74) or field `WMPortRecvCallback.port` (WmReceiveFifo.c:164) | Which port the bytes belong to; the game multiplexes ports |
| `dest_bitmap` | u16 | arg `destBitmap` (WM_SetMPDataToPortEx.c:74) or field `WMPortSendCallback.destBitmap` (WmReceiveFifo.c:144) | Which stations are addressed |
| `send_bytes` | u8[] | arg `sendData`, length `sendDataSize` (WM_SetMPDataToPortEx.c:73) | The bytes that leave; the substance of a send |
| `recv_bytes` | u8[] | field `WMPortRecvCallback.data` and `length` (WmReceiveFifo.c:166 to 167) | The bytes that arrive; the substance of a receive |
| `length` | u16 | arg `sendDataSize` or field `length` (WM_SetMPDataToPortEx.c:74; WmReceiveFifo.c:167) | The byte count |
| `prio` | u16 | arg `prio` (WM_SetMPDataToPortEx.c:74) | The send priority |
| `seq_no` | u16 | field `WMPortSendCallback.seqNo` / `WMPortRecvCallback.seqNo` (WmReceiveFifo.c:153, 170) | Orders the frames; a mod drops a stale one |
| `errcode` | u16 | field `WMCallback.errcode` (WmReceiveFifo.c:88) | Whether the frame succeeded |
| `connected_aid_bitmap` | u16 | field `WMArm9Buf.connectedAidBitmap` (WmReceiveFifo.c:71, 224) | Which stations are in the group |
| `mac` | u8[6] | field `WMPortRecvCallback.macAddress` (WmReceiveFifo.c:169) | The sender's address when the aid is not enough |
| `rssi` | u16 | field `WMPortRecvCallback.rssi` (WmReceiveFifo.c:176) | The signal at receive |
| `max_send`, `max_recv` | u16 | field `WMPortRecvCallback.maxSendDataSize`, `maxRecvDataSize` (WmReceiveFifo.c:177 to 178) | The frame budget; a mod splits a large message |

The send and receive points are still to build, and which declared address is
the send and which the receive point is still to build
(`docs/mod-events.md`, still-to-build item 12).

## 5. Comm

The comm object is the per-frame request state machine. `GAME_COMM_Update`
(0x0202627C) drives it; `GAME_COMM_CallHandler` (0x02026388) and
`GAME_COMM_CallKindNotify` (0x020263B0) dispatch the callbacks
(`src/matched/arm9/func_0202627C.c`, the header comment, lines 15 to 18). The
kind is byte +5 and selects a 0x1C-byte record of callbacks in the table at
0x0209FB5C (`src/matched/arm9/unit_02026454.c`, lines 47 to 54;
func_0202627C.c:16 to 18).

### comm.update (0x0202627C, arm9)

`void GAME_COMM_Update(struct Comm *p)`
(`src/matched/arm9/func_0202627C.c`, lines 74 to 151). The struct is `Comm`
(lines 43 to 59) and the sequence sub-object is `Sub` (lines 35 to 42).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `comm` | u32 | arg `p` (func_0202627C.c:74) | Names the comm object across the comm events |
| `kind` | u8 | field `Comm.kind` (func_0202627C.c:50, 78, 81) | The request kind; selects the callback record and names the action |
| `state` | u8 | field `Sub.state` (func_0202627C.c:38, 87) | The sequence state 0 to 5; a mod sees where in the handshake the request is |
| `changed` | u8 | field `Sub.changed` (func_0202627C.c:39, 82) | Set when the sub-object was refreshed; a mod knows the work reset |
| `flags` | u8 | field `Comm.b0` to `b3` bits (func_0202627C.c:46 to 49, 103) | The per-frame flags; `b0` gates the run step |
| `last_kind` | u8 | field `Comm.f06` (func_0202627C.c:51, 92) | The kind last started |
| `counter` | u32 | field `Comm.f08` (func_0202627C.c:53, 93) | The request counter |
| `work` | u32 | field `Comm.f14` (func_0202627C.c:55, 91) | The first argument the callbacks receive |
| `start_result` | u32 | field `Comm.f18` (func_0202627C.c:56, 91, 139) | The result the start step stored |
| `finish_arg` | u32 | field `Comm.f1c` (func_0202627C.c:57, 137, 142) | The finish callback's first argument |
| `finish_callback` | u32 | field `Comm.f20` (func_0202627C.c:58, 136, 141) | The callback run at state 5 |
| `record` | u32 | call `&GAME_COMM_KindTable[p->kind]` (func_0202627C.c:81) | The callback record for the kind |
| `rec_start`, `rec_wait`, `rec_run`, `rec_stop`, `rec_end` | u32 | fields of `struct Rec` at `record` (func_0202627C.c:62 to 68, 90 to 125) | The five callbacks the kind installs; a mod names the action by them |
| `owner` | u32 | local player, 0 | Attributes the request to a player |

The action behind a kind is still to build. The record's five words are
addresses; aletheia does not name which kind is a battle turn and which is a
trade (`docs/mod-events.md`, still-to-build item 13). The port cannot name the
action from the kind number alone.

### comm.handler (0x02026388, arm9, override)

`void GAME_COMM_CallHandler(struct Obj *p, u32 a)`
(`src/matched/arm9/func_02026388.c`, lines 41 to 50). An override point.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `comm` | u32 | arg `p` (func_02026388.c:41) | Names the comm object |
| `kind` | u8 | field `Obj.f5` (func_02026388.c:29, 44) | The index into `gGameCommHandlers`; the action |
| `state` | u8 | field `Obj.f10` (func_02026388.c:31, 43) | 5 means the handler is skipped |
| `f4` | u8 | field `Obj.f4` (func_02026388.c:28) | The record's second byte, unread here; a mod keeps it for parity with `notify` |
| `work` | u32 | field `Obj.f14` (func_02026388.c:33, 47) | The handler's first argument |
| `start_result` | u32 | field `Obj.f18` (func_02026388.c:34, 47) | The handler's second argument |
| `call_arg` | u32 | arg `a` (func_02026388.c:41, 47) | The handler's third argument |
| `handler` | u32 | call `*(Fn *)(gGameCommHandlers + p->f5 * 0x1C)` (func_02026388.c:44) | The handler pointer; a wrap handler decides before or after it |
| `owner` | u32 | local player, 0 | Attributes the call to a player |

### comm.notify (0x020263B0, arm9)

`void GAME_COMM_CallKindNotify(struct Obj *p, u32 a)`
(`src/matched/arm9/func_020263B0.c`, lines 41 to 50). The shape is the handler's;
the table is `g_0209FB74` (func_020263B0.c:18, 39, 44).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `comm` | u32 | arg `p` (func_020263B0.c:41) | Names the comm object |
| `kind` | u8 | field `Obj.f5` (func_020263B0.c:29, 44) | The index into `g_0209FB74`; the kind notified |
| `state` | u8 | field `Obj.f10` (func_020263B0.c:31, 43) | 5 means the notify is skipped |
| `f4` | u8 | field `Obj.f4` (func_020263B0.c:28) | The record's second byte |
| `work` | u32 | field `Obj.f14` (func_020263B0.c:33, 47) | The notify's first argument |
| `start_result` | u32 | field `Obj.f18` (func_020263B0.c:34, 47) | The notify's second argument |
| `call_arg` | u32 | arg `a` (func_020263B0.c:41, 47) | The notify's third argument |
| `notify` | u32 | call `*(Fn *)(g_0209FB74 + p->f5 * 0x1C)` (func_020263B0.c:44) | The notify pointer |
| `owner` | u32 | local player, 0 | Attributes the notify to a player |

### comm.talk.start (0x0217FB5C, ov018)

`void *EVENT_COMM_COMMON_CreateTalkStartEvent(void *owner, int b, int unused, int d, int e, u16 f)`
(`src/matched/ov/ov018_func_0217FB5C.c`, lines 27 to 39). The work struct is
`Req` (line 20): `f0 = b`, `f4 = d`, `f8 = e`, `fC = f`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event_owner` | u32 | arg `owner` (ov018_func_0217FB5C.c:27, 31) | The game event owner the task hangs from |
| `f0` | u32 | arg `b`, work `f0` (ov018_func_0217FB5C.c:27, 34) | The talk start's first payload word |
| `f4` | u32 | arg `d`, work `f4` (ov018_func_0217FB5C.c:27, 35) | The second payload word |
| `f8` | u32 | arg `e`, work `f8` (ov018_func_0217FB5C.c:27, 36) | The third payload word |
| `fC` | u16 | arg `f`, work `fC` (ov018_func_0217FB5C.c:27, 37) | The fourth payload word |
| `task` | u32 | ret of `func_020122C0` (ov018_func_0217FB5C.c:31, 38) | The task created; a mod tracks the talk |
| `work` | u32 | call `func_020124E8(task)` (ov018_func_0217FB5C.c:32) | The work the payload was written into |
| `main` | u32 | call `EVENT_COMM_COMMON_TalkStartEventMain` (ov018_func_0217FB5C.c:31) | The task main; names the talk |
| `owner` | u32 | local player, 0 | Attributes the talk to a player |

The meaning of `b`, `d`, `e` and `f` is not named. A mod that wants the partner
or the topic reads them as words; the port cannot name them yet.

### comm.talked (0x0217FD34, ov018)

`void *EVENT_COMM_COMMON_CreateTalkedEvent(int a, int b, u8 *c, int d, int e, u16 f)`
(`src/matched/ov/ov018_func_0217FD34.c`, lines 36 to 46).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event_owner` | u32 | arg `a` (ov018_func_0217FD34.c:36, 42) | The game event owner |
| `b` | u32 | arg `b` (ov018_func_0217FD34.c:36, 42) | Forwarded to `InitCommonWork` |
| `d` | u32 | arg `d` (ov018_func_0217FD34.c:36, 42) | Forwarded to `InitCommonWork` |
| `e` | u32 | arg `e` (ov018_func_0217FD34.c:36, 42) | Forwarded to `InitCommonWork` |
| `f` | u16 | arg `f` (ov018_func_0217FD34.c:36, 42) | Forwarded to `InitCommonWork` |
| `source` | u32 | arg `c` (ov018_func_0217FD34.c:36, 42 to 44) | The record the talked data is read from |
| `talked_byte` | u8 | field `c[0x11c]` copied to work +0x41 (ov018_func_0217FD34.c:43) | The talked flag byte |
| `talked_block` | 13 u32 words | field `*(S52 *)(c + 0x120)` copied to work +0x44 (ov018_func_0217FD34.c:23 to 25, 44) | The talked record, the substance of the talk |
| `task` | u32 | ret of `func_020122C0` (ov018_func_0217FD34.c:38) | The task created |
| `work` | u32 | call `func_020124E8(r)` (ov018_func_0217FD34.c:39) | The work the payload was written into |
| `main` | u32 | call `EVENT_COMM_COMMON_TalkedEventMain` (ov018_func_0217FD34.c:38) | The task main |
| `owner` | u32 | local player, 0 | Attributes the talk to a player |

### comm.support (0x02181D8C, ov018)

`void *EVENT_COMM_COMMON_CreateSupportMsgEvent(void *a, void *b)`
(`src/matched/ov/ov018_func_02181D8C.c`, lines 45 to 58).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event_owner` | u32 | arg `a` (ov018_func_02181D8C.c:45, 51) | The game event owner |
| `arg` | u32 | arg `b` (ov018_func_02181D8C.c:45, 54) | The record `MYSTATUS_Create` copies |
| `pms_work` | u32 | call `GAME_DATA_GetPmsWork(GAMESYSTEM_GetGameData(a))` (ov018_func_02181D8C.c:51) | The player support work the active record comes from |
| `status` | u32 | call `MYSTATUS_Create(b)` (ov018_func_02181D8C.c:54) | The 0x20-byte status record of the supporter; a mod reads the owner |
| `active_support` | u32 | call `COMM_PLAYER_SUPPORT_GetActiveRecord(pms_work)` (ov018_func_02181D8C.c:40, 55) | The support record copied into the status; the substance of the support message |
| `task` | u32 | ret of `func_020122C0` (ov018_func_02181D8C.c:52) | The task created |
| `work` | u32 | call `func_020124E8(r)` (ov018_func_02181D8C.c:53) | The work the payload was written into |
| `main` | u32 | call `EVENT_COMM_COMMON_SupportMsgEventMain` (ov018_func_02181D8C.c:52) | The task main |
| `owner` | u32 | local player, 0 | Attributes the support to a player |

### comm.offer (0x02182150, ov018)

`int EVENT_COMM_COMMON_OfferEventMain(void *a, int *state, int c)`
(`src/matched/ov/ov018_func_02182150.c`, lines 38 to 59).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event_owner` | u32 | arg `a` (ov018_func_02182150.c:38, 48) | The game event owner |
| `state` | u32 | field `*state` (ov018_func_02182150.c:38, 45) | The offer state 0 or 1; a mod sees the pause and the answer |
| `offer` | u32 | arg `c` (ov018_func_02182150.c:38, 48) | The offer payload passed to the answer event |
| `game_system` | u32 | call `GAME_EVENT_GetGameSystem(a)` (ov018_func_02182150.c:43) | The game system the offer runs in |
| `field_map` | u32 | call `GAMESYSTEM_GetFieldMap(h)` (ov018_func_02182150.c:44) | The field map the movement pauses on |
| `mmdl_sys` | u32 | call `FIELDMAP_GetMmdlSys(r)` (ov018_func_02182150.c:47) | The model system paused and resumed |
| `answer_event` | u32 | call `EVENT_COMM_COMMON_CreateOfferAnswerEvent(h, c)` (ov018_func_02182150.c:48) | The answer event created; a mod tracks the offer exchange |
| `paused` | u8 | field `*state == 0` (ov018_func_02182150.c:46 to 49) | The movement is paused; a mod knows the field is frozen |
| `resumed` | u8 | field `*state == 1` and `FIELDMAP_IsReady` (ov018_func_02182150.c:51 to 54) | The movement resumed; the offer resolved |
| `result` | u32 | ret of `EVENT_COMM_COMMON_OfferEventMain` (ov018_func_02182150.c:54, 58) | 1 when the offer step finished |
| `owner` | u32 | local player, 0 | Attributes the offer to a player |

The offer's content and the peer are inside `c`, which the offer answer event
reads. The port cannot name them from this function alone.

### comm.buy (0x0218261C, ov018)

`int EVENT_COMM_COMMON_ItemPurchaseEventMain(void *a, int *state, Outer *c)`
(`src/matched/ov/ov018_func_0218261C.c`, lines 93 to 207). The work struct is
`Outer` (lines 49 to 59): `p10` is the word-set, `b42` the message index,
`h58[2]` the item id and price, `f78` the buyer status, `f98` the no-work flag.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event_owner` | u32 | arg `a` (ov018_func_0218261C.c:93, 103) | The game event owner |
| `state` | u32 | field `*state` (ov018_func_0218261C.c:93, 119) | The purchase state; a mod sees the choice and the reply |
| `item_id` | u16 | field `Outer.h58[0]` (ov018_func_0218261C.c:55, 106, 122) | The item bought; the substance of the purchase |
| `price` | u16 | field `Outer.h58[1]` (ov018_func_0218261C.c:55, 106, 123) | The price; a mod checks it against the money |
| `buyer_status` | u32 | call `GAME_DATA_GetMyStatus(GAMESYSTEM_GetGameData(sub))` (ov018_func_0218261C.c:124) | The buyer's status record; the owner of the item |
| `money` | u32 | call `BEACON_GetPoints(GAME_DATA_GetMiscSave(x))` (ov018_func_0218261C.c:146) | The money before the purchase |
| `money_after` | u32 | call `BEACON_SubPoints(...)` (ov018_func_0218261C.c:168) | The money after the purchase |
| `can_add` | u32 | call `func_02007E3C(GAME_DATA_GetMyItem(x), h[0], 1, 0x15)` (ov018_func_0218261C.c:149) | Whether the bag can take the item |
| `item_added` | u32 | call `func_02007E50(GAME_DATA_GetMyItem(x), h[0], 1, 0x15)` (ov018_func_0218261C.c:169) | The item handed over |
| `choice` | u32 | call `EVENT_COMM_COMMON_PollChoice(...)` (ov018_func_0218261C.c:141) | The player's yes or no |
| `reply` | u32 | call `INTRUDE_COMM_SendReply(v, v[0x154], code, v[0x158])` (ov018_func_0218261C.c:167, 175, 181, 187) | The answer sent to the peer; the code is the outcome |
| `peer_token` | u32 | field `v[0x154]` (ov018_func_0218261C.c:167) | The request token identifying the peer request |
| `intrude_work` | u32 | call `INTRUDE_COMM_GetWorkIfRunning(t)` (ov018_func_0218261C.c:107) | The intrusion work; 0 means the peer is gone |
| `comm` | u32 | call `GAMESYSTEM_GetGameComm(sub)` (ov018_func_0218261C.c:105) | The comm object behind the purchase |
| `game_data` | u32 | call `GAMESYSTEM_GetGameData(sub)` (ov018_func_0218261C.c:104) | The game data the money and bag come from |
| `message_index` | u8 | field `Outer.b42` (ov018_func_0218261C.c:53, 125) | The message row the purchase shows |
| `no_work` | u8 | field `Outer.f98` (ov018_func_0218261C.c:58, 115) | The flag set when the peer work is gone |
| `result` | u32 | ret of `EVENT_COMM_COMMON_ItemPurchaseEventMain` (ov018_func_0218261C.c:204) | 1 when the purchase step finished |
| `owner` | u32 | local player, 0 | Attributes the purchase to a player |
| `peer` | u32 | field `v[0x154]` (ov018_func_0218261C.c:167) | The peer request, from the intrusion work |

### comm.sell (0x021823A8, ov018)

`int EVENT_COMM_COMMON_ItemSaleEventMain(void *a, int *state, Outer *c)`
(`src/matched/ov/ov018_func_021823A8.c`, lines 86 to 195). The work struct is
`Outer` (lines 44 to 56): `h58[2]` the item id and count, `f98` and `f9C` the
result flags, `fA0` the answer flag.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `event_owner` | u32 | arg `a` (ov018_func_021823A8.c:86, 92) | The game event owner |
| `state` | u32 | field `*state` (ov018_func_021823A8.c:86, 105) | The sale state; a mod sees the proposal and the answer |
| `item_id` | u16 | field `Outer.h58[0]` (ov018_func_021823A8.c:50, 108) | The item offered; the substance of the sale |
| `count` | u16 | field `Outer.h58[1]` (ov018_func_021823A8.c:50, 110) | The offered count |
| `seller_status` | u32 | call `GAME_DATA_GetMyStatus(GAMESYSTEM_GetGameData(sub))` (ov018_func_021823A8.c:111) | The seller's status record; the owner of the item |
| `request_result` | u32 | call `INTRUDE_COMM_TakeRequestResult(v)` (ov018_func_021823A8.c:117) | The peer's answer code |
| `send_answer` | u32 | call `INTRUDE_COMM_SendAnswer(v[0x154], v[0x158])` (ov018_func_021823A8.c:132) | The answer sent to the peer |
| `send_proposal` | u32 | call `INTRUDE_CMD_SendProposal(v, v + 0x1AC)` (ov018_func_021823A8.c:146) | The proposal sent to the peer |
| `join_status` | u32 | call `INTRUDE_COMM_GetJoinStatus(v, v + 0x1AC)` (ov018_func_021823A8.c:151) | Whether the peer joined |
| `peer_token` | u32 | field `v[0x154]` (ov018_func_021823A8.c:132) | The request token identifying the peer request |
| `intrude_work` | u32 | call `INTRUDE_COMM_GetWorkIfRunning(GAMESYSTEM_GetGameComm(sub))` (ov018_func_021823A8.c:93) | The intrusion work; 0 means the peer is gone |
| `comm` | u32 | call `GAMESYSTEM_GetGameComm(sub)` (ov018_func_021823A8.c:93) | The comm object behind the sale |
| `game_data` | u32 | call `GAMESYSTEM_GetGameData(sub)` (ov018_func_021823A8.c:111) | The game data the status comes from |
| `message_index` | u8 | field `Outer.b42` (ov018_func_021823A8.c:48, 112) | The message row the sale shows |
| `no_work` | u8 | field `Outer.f98` (ov018_func_021823A8.c:53, 101) | The flag set when the peer work is gone |
| `mission_result` | u8 | field `Outer.f9C` (ov018_func_021823A8.c:54, 152, 183) | The flag that routes to the mission result event |
| `answered` | u8 | field `Outer.fA0` (ov018_func_021823A8.c:55, 134, 187) | The flag that routes to a disconnect without an answer |
| `result` | u32 | ret of `EVENT_COMM_COMMON_ItemSaleEventMain` (ov018_func_021823A8.c:194) | 1 when the sale step finished |
| `owner` | u32 | local player, 0 | Attributes the sale to a player |
| `peer` | u32 | field `v[0x154]` (ov018_func_021823A8.c:132) | The peer request, from the intrusion work |

## 6. Wm

No responder answers Wm, and that is deliberate (`docs/modding.md`, "Networking
seam: missing", lines 149 to 158). The wm payloads are specified against the
matched sources so the struct is frozen before the transport lands.

The wireless manager keeps its work in `WMArm9Buf`
(`src/matched/arm9_sdk/WmReceiveFifo.c`, lines 59 to 74): the local station id,
the connected bitmap, the callback table and the port callback table and
arguments. The status is `WMStatus` (`WM_Init.c`, lines 38 to 42).

### wm.init (0x0208E010, arm9)

`WMErrCode WM_Init(void *wmSysBuf, u16 dmaNo)`
(`src/matched/arm9_sdk/WM_Init.c`, lines 81 to 89).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `wm_sys_buf` | u32 | arg `wmSysBuf` (WM_Init.c:81) | The caller's work buffer the manager uses |
| `dma_no` | u16 | arg `dmaNo` (WM_Init.c:81) | The DMA channel the manager takes |
| `buf_size` | u32 | call `WmInitCore(..., 0xF00)` (WM_Init.c:83) | The size the core is given; a mod checks the layout |
| `scan_only` | u16 | field `WMArm9Buf.scanOnlyFlag` (WM_Init.c:51, 86) | Cleared on success; a mod reads whether the manager scans only |
| `result` | u32 | ret of `WM_Init` (WM_Init.c:83, 88) | `WM_ERRCODE_SUCCESS` when the manager initialised, else a code |
| `owner` | u32 | local instance, 0 | Attributes the init to an instance |

### wm.enable (0x0208F224, arm9)

`int WM_Enable(char *path)` (`src/matched/arm9_sdk/func_0208F224.c`, lines 21
to 24). It tail-calls `WMi_EnableEx(path, 0)`.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `path` | u32 | arg `path` (func_0208F224.c:21) | The firmware or data path the enable reads |
| `format_media` | u32 | arg literal 0 (func_0208F224.c:23) | The enable does not format |
| `result` | u32 | ret of `WMi_EnableEx` (func_0208F224.c:23) | The enable result |
| `owner` | u32 | local instance, 0 | Attributes the enable to an instance |

### wm.connect (0x0208F84C, arm9)

`WMErrCode WM_StartConnectEx(WMCallbackFunc callback, WMBssDesc *pInfo, u8 *ssid, BOOL powerSave, u16 authMode)`
(`src/matched/arm9_sdk/func_0208F84C.c`, lines 95 to 139). The beacon is
`WMBssDesc` (lines 66 to 73) and the request is `WMStartConnectReq` (lines 75 to
83).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `callback` | u32 | arg `callback` (func_0208F84C.c:95, 122) | The callback the connect result reaches |
| `bss_desc` | u32 | arg `pInfo` (func_0208F84C.c:95, 116) | The beacon the connect joins; a mod reads the network identity |
| `bssid` | u8[6] | field `WMBssDesc.bssid` (func_0208F84C.c:69) | The access point address |
| `beacon_ssid` | u8[32] | field `WMBssDesc.ssid` (func_0208F84C.c:71) | The network name |
| `beacon_ssid_length` | u16 | field `WMBssDesc.ssidLength` (func_0208F84C.c:70) | The name length |
| `rssi` | u16 | field `WMBssDesc.rssi` (func_0208F84C.c:68) | The signal of the beacon |
| `length` | u16 | field `WMBssDesc.length` (func_0208F84C.c:67, 116) | The beacon's word count, used for the store |
| `ssid` | u8[24] | arg `ssid`, copied (func_0208F84C.c:95, 127) | The requested network name |
| `power_save` | u32 | arg `powerSave` (func_0208F84C.c:95, 131) | Whether power save is requested |
| `auth_mode` | u16 | arg `authMode` (func_0208F84C.c:95, 132) | The authentication mode |
| `apiid` | u16 | field `WMStartConnectReq.apiid` = 12 (func_0208F84C.c:76, 124) | The command id; a mod matches the callback |
| `my_aid` | u16 | field `WMArm9Buf.myAid` set to 0 (func_0208F84C.c:62, 119) | The station id resets on connect |
| `connected_aid_bitmap` | u32 | field `WMArm9Buf.connectedAidBitmap` set to 0 (func_0208F84C.c:61, 120) | The group resets on connect |
| `result` | u32 | ret of `WM_StartConnectEx` (func_0208F84C.c:101, 134, 138) | `WM_ERRCODE_OPERATING` when the command is sent |
| `owner` | u32 | local instance, 0 | Attributes the connect to an instance |

### wm.mp.start (0x0208FC4C, arm9)

`WMErrCode WM_StartMP(WMCallbackFunc callback, u16 *recvBuf, u16 recvBufSize, u16 *sendBuf, u16 sendBufSize, u16 mpFreq)`
(`src/matched/arm9_sdk/func_0208FC4C.c`, lines 74 to 84). The temporary
parameters are `WMMPTmpParam` (lines 59 to 69).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `callback` | u32 | arg `callback` (func_0208FC4C.c:74, 83) | The callback the multipoint frames reach |
| `recv_buf` | u32 | arg `recvBuf` (func_0208FC4C.c:74) | The receive buffer; a mod reads frames there |
| `recv_buf_size` | u16 | arg `recvBufSize` (func_0208FC4C.c:74) | The receive budget |
| `send_buf` | u32 | arg `sendBuf` (func_0208FC4C.c:74) | The send buffer |
| `send_buf_size` | u16 | arg `sendBufSize` (func_0208FC4C.c:74) | The send budget |
| `mp_freq` | u16 | arg `mpFreq` (func_0208FC4C.c:74, 80 to 81) | The frequency, also the minimum and the set frequency |
| `mask` | u32 | field `WMMPTmpParam.mask` = 3 (func_0208FC4C.c:60, 79) | The parameters set |
| `frequency` | u16 | field `WMMPTmpParam.frequency` (func_0208FC4C.c:62, 81) | The chosen frequency |
| `min_frequency` | u16 | field `WMMPTmpParam.minFrequency` (func_0208FC4C.c:61, 80) | The lower bound |
| `default_retry` | u16 | field `WMMPTmpParam.defaultRetryCount` (func_0208FC4C.c:64) | The retry count |
| `result` | u32 | ret of `WMi_StartMP` (func_0208FC4C.c:83) | The start result |
| `owner` | u32 | local instance, 0 | Attributes the multipoint start to an instance |

### wm.mp.send (0x0208FCB4, arm9)

`WMErrCode WM_SetMPDataToPortEx(WMCallbackFunc callback, void *arg, u16 *sendData, u16 sendDataSize, u16 destBitmap, u16 port, u16 prio)`
(`src/matched/arm9_sdk/WM_SetMPDataToPortEx.c`, lines 73 to 119). The status is
`WMStatus` (lines 48 to 60).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `callback` | u32 | arg `callback` (WM_SetMPDataToPortEx.c:73, 113) | The callback the send result reaches |
| `arg` | u32 | arg `arg` (WM_SetMPDataToPortEx.c:73, 114) | The callback's argument |
| `send_data` | u32 | arg `sendData` (WM_SetMPDataToPortEx.c:73, 101, 113) | The bytes that leave |
| `send_bytes` | u8[] | arg `sendData`, length `sendDataSize` (WM_SetMPDataToPortEx.c:73, 111) | The substance of the send |
| `send_data_size` | u16 | arg `sendDataSize` (WM_SetMPDataToPortEx.c:73, 104, 107, 113) | The byte count, capped at 512 |
| `dest_bitmap` | u16 | arg `destBitmap` (WM_SetMPDataToPortEx.c:74, 113) | Which stations are addressed |
| `port` | u16 | arg `port` (WM_SetMPDataToPortEx.c:74, 113) | The port the bytes go to |
| `prio` | u16 | arg `prio` (WM_SetMPDataToPortEx.c:74, 113) | The send priority |
| `aid` | u16 | field `WMStatus.aid` (WM_SetMPDataToPortEx.c:59, 86 to 87) | The local station id; 0 means the parent |
| `child_bitmap` | u16 | field `WMStatus.child_bitmap` (WM_SetMPDataToPortEx.c:57, 88 to 89) | The children, when the station is the parent |
| `mp_ready_bitmap` | u16 | field `WMStatus.mp_readyBitmap` (WM_SetMPDataToPortEx.c:55, 90) | The children ready to receive |
| `send_buf` | u32 | field `WMStatus.mp_sendBuf` (WM_SetMPDataToPortEx.c:51, 101) | The manager's own buffer; a mod must not alias it |
| `apiid` | u32 | call `WMi_SendCommand(WM_APIID_SET_MP_DATA, ...)` = 15 (WM_SetMPDataToPortEx.c:42, 113) | The command id; a mod matches the callback |
| `result` | u32 | ret of `WM_SetMPDataToPortEx` (WM_SetMPDataToPortEx.c:81, 82, 115, 118) | `WM_ERRCODE_OPERATING` when the command is queued |
| `owner` | u32 | local instance, 0 | Attributes the send to an instance |

### wm.share.start (0x0209004C, arm9)

`WMErrCode WM_StartDataSharing(WMDataSharingInfo *dsInfo, u16 port, u16 aidBitmap, u16 dataLength, BOOL doubleMode)`
(`src/matched/arm9_sdk/WM_StartDataSharing.c`, lines 85 to 160). The sharing
state is `WMDataSharingInfo` (lines 56 to 71).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `ds_info` | u32 | arg `dsInfo` (WM_StartDataSharing.c:85, 112) | The sharing state object |
| `port` | u16 | arg `port` (WM_StartDataSharing.c:85, 118) | The port sharing runs on |
| `aid_bitmap` | u16 | arg `aidBitmap` (WM_StartDataSharing.c:85, 121) | The stations in the group |
| `data_length` | u16 | arg `dataLength` (WM_StartDataSharing.c:85, 117) | The per-station data size |
| `double_mode` | u16 | arg `doubleMode` (WM_StartDataSharing.c:85, 119) | The double-buffer mode |
| `my_aid` | u16 | call `WM_GetAID()` (WM_StartDataSharing.c:107) | The local station id; 0 means the parent |
| `connected_aids` | u16 | call `WM_GetConnectedAIDs()` (WM_StartDataSharing.c:109) | The connected stations |
| `is_parent` | u8 | call `WM_GetAID() == 0` (WM_StartDataSharing.c:108) | The parent installs the receive callback and sends the first sets |
| `station_number` | u16 | call `MATH_CountPopulation(aidBitmap)` (WM_StartDataSharing.c:123, 125) | How many stations share |
| `data_set_length` | u16 | call `dataLength * count + 4` (WM_StartDataSharing.c:124, 130) | The whole data set size |
| `state` | u16 | field `WMDataSharingInfo.state` = 1 (WM_StartDataSharing.c:69, 131) | The sharing state; a mod sees it start |
| `callback` | u32 | call `WM_SetPortCallback(port, ...)` (WM_StartDataSharing.c:140, 157) | The receive callback installed |
| `result` | u32 | ret of `WM_StartDataSharing` (WM_StartDataSharing.c:94, 152, 159) | The start result |
| `owner` | u32 | local instance, 0 | Attributes the sharing to an instance |

### wm.share.addr (0x02090B18, arm9, state)

`u16 *WM_GetSharedDataAddress(WMDataSharingInfo *dsInfo, WMDataSet *receiveData, u16 aid)`
(`src/matched/arm9_sdk/WM_GetSharedDataAddress.c`, lines 63 to 84). A state
accessor.

| Accessor | Inputs | Output | Where it comes from | Why a mod wants it |
|---|---|---|---|---|
| `bw_share_addr` | sharing info, data set, `aid` | u32 pointer | `WmGetSharedDataAddress(dsInfo, aidBitmap, receiveData->data, aid)` (WM_GetSharedDataAddress.c:83) | The address of one station's contribution; a mod reads the peer's shared data |

Fields the accessor exposes: `aid` (arg, line 63), `aid_bitmap` (field
`WMDataSet.aidBitmap`, line 75), `received_bitmap` (field
`WMDataSet.receivedBitmap`, line 76), `data_length` (field
`WMDataSharingInfo.dataLength`, line 51), `station_number` (field, line 52),
`port` (field, line 54), `double_mode` (field, line 55), `write_index` (field,
line 47), `read_index` (field, line 49).

### wm.share.send (0x020909C8, arm9)

`void WmDataSharingSendDataSet(WMDataSharingInfo *dsInfo, BOOL delayed)`
(`src/matched/arm9_sdk/WmDataSharingSendDataSet.c`, lines 79 to 119).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `ds_info` | u32 | arg `dsInfo` (WmDataSharingSendDataSet.c:79, 107) | The sharing state |
| `delayed` | u8 | arg `delayed` (WmDataSharingSendDataSet.c:79, 102) | Whether the parent's own bit is cleared |
| `write_index` | u16 | field `WMDataSharingInfo.writeIndex` (WmDataSharingSendDataSet.c:55, 83, 91) | The set being written |
| `next_write_index` | u16 | call `(writeIndex + 1) % 4` (WmDataSharingSendDataSet.c:92, 100) | The set prepared next |
| `connected_aids` | u16 | call `WM_GetConnectedAIDs()` (WmDataSharingSendDataSet.c:90) | The stations to send to |
| `aid_bitmap` | u16 | field `WMDataSharingInfo.aidBitmap` (WmDataSharingSendDataSet.c:58, 99, 109) | The sharing group |
| `double_mode` | u16 | field `WMDataSharingInfo.doubleMode` (WmDataSharingSendDataSet.c:63, 93) | The double-buffer mode |
| `port` | u16 | field `WMDataSharingInfo.port` (WmDataSharingSendDataSet.c:62, 109) | The port the set goes to |
| `data_set_length` | u16 | field `WMDataSharingInfo.dataSetLength` (WmDataSharingSendDataSet.c:61, 108) | The set size |
| `dest_bitmap` | u16 | call `aidBitmap & connectedAIDs` (WmDataSharingSendDataSet.c:109) | Which stations receive |
| `bytes` | u8[] | field `ds[oldWI]`, length `dataSetLength` (WmDataSharingSendDataSet.c:101, 108) | The data set that leaves; the substance of the send |
| `result` | u32 | ret of `WM_SetMPDataToPortEx` (WmDataSharingSendDataSet.c:107, 110 to 114) | The send result; `NO_ENTRY` advances the send index |
| `send_index` | u16 | field `WMDataSharingInfo.sendIndex` (WmDataSharingSendDataSet.c:56, 112) | The next set to send |
| `seq_num` | u16 | field `WMDataSharingInfo.seqNum[oldWI]` (WmDataSharingSendDataSet.c:54, 111) | The sequence number, 0xFFFF on no entry |
| `state` | u16 | field `WMDataSharingInfo.state` = 5 on error (WmDataSharingSendDataSet.c:65, 114) | The error state |
| `owner` | u32 | local instance, 0 | Attributes the send to an instance |

### wm.share.recv (0x020908F8, arm9)

`void WmDataSharingReceiveData(WMDataSharingInfo *dsInfo, u16 aid, u16 *data)`
(`src/matched/arm9_sdk/WmDataSharingReceiveData.c`, lines 75 to 107).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `ds_info` | u32 | arg `dsInfo` (WmDataSharingReceiveData.c:75, 96) | The sharing state |
| `aid` | u16 | arg `aid` (WmDataSharingReceiveData.c:75, 77) | The station that contributed; the peer |
| `data` | u32 | arg `data` (WmDataSharingReceiveData.c:75, 97) | The contribution, or 0 to clear |
| `recv_bytes` | u8[] | arg `data`, length `dataLength` (WmDataSharingReceiveData.c:98) | The bytes received; the substance of the receive |
| `write_index` | u16 | field `WMDataSharingInfo.writeIndex` (WmDataSharingReceiveData.c:53, 85) | The set written |
| `double_mode` | u16 | field `WMDataSharingInfo.doubleMode` (WmDataSharingReceiveData.c:61, 87) | The double-buffer mode |
| `data_length` | u16 | field `WMDataSharingInfo.dataLength` (WmDataSharingReceiveData.c:57, 98, 100) | The contribution size |
| `aid_bitmap` | u16 | field `WMDataSharingInfo.aidBitmap` (WmDataSharingReceiveData.c:56, 82, 96) | The sharing group |
| `received_bitmap` | u16 | field `WMDataSet.receivedBitmap` after the update (WmDataSharingReceiveData.c:45, 105) | Which stations have contributed |
| `peer` | u32 | arg `aid` (WmDataSharingReceiveData.c:75) | The contributing station |
| `owner` | u32 | local instance, 0 | Attributes the receive to an instance |

### wm.fifo (0x0208E5BC, arm9)

`void WmReceiveFifo(int tag, u32 fifo_buf_adr, BOOL err)`
(`src/matched/arm9_sdk/WmReceiveFifo.c`, lines 195 to 343). The base callback is
`WMCallback` (lines 86 to 91); the port receive is `WMPortRecvCallback` (lines
160 to 179), the port send is `WMPortSendCallback` (lines 137 to 158), the
multipoint start is `WMStartMPCallback` (lines 121 to 135), and the parent and
connect callbacks are at lines 93 to 119.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `tag` | u32 | arg `tag` (WmReceiveFifo.c:195) | The PXI tag, 10 for wireless |
| `fifo_buf` | u32 | arg `fifo_buf_adr` (WmReceiveFifo.c:195, 197) | The frame buffer the callback reads |
| `err` | u8 | arg `err` (WmReceiveFifo.c:195, 200) | The error flag; the handler returns at once when set |
| `apiid` | u16 | field `WMCallback.apiid` (WmReceiveFifo.c:87, 212) | The callback id, which selects the variant |
| `errcode` | u16 | field `WMCallback.errcode` (WmReceiveFifo.c:88, 214, 239, 245) | The result code |
| `wl_cmd_id` | u16 | field `WMCallback.wlCmdID` (WmReceiveFifo.c:89) | The wireless command id |
| `wl_result` | u16 | field `WMCallback.wlResult` (WmReceiveFifo.c:90) | The wireless result |
| `port` | u16 | field `WMPortRecvCallback.port` (WmReceiveFifo.c:164, 222) | The port the frame belongs to |
| `recv_buf` | u32 | field `WMPortRecvCallback.recvBuf` (WmReceiveFifo.c:165, 225) | The receive buffer |
| `recv_bytes` | u8[] | field `WMPortRecvCallback.data`, `length` (WmReceiveFifo.c:166 to 167) | The bytes received; the substance of a receive |
| `aid` | u16 | field `WMPortRecvCallback.aid` (WmReceiveFifo.c:168, 311) | The sending station; the peer |
| `mac` | u8[6] | field `WMPortRecvCallback.macAddress` (WmReceiveFifo.c:169, 316) | The sender's address |
| `seq_no` | u16 | field `WMPortRecvCallback.seqNo` (WmReceiveFifo.c:170, 314) | The frame sequence |
| `port_arg` | u32 | field `WMPortRecvCallback.arg` (WmReceiveFifo.c:171, 223, 328) | The callback's argument, from the port table |
| `my_aid` | u16 | field `WMArm9Buf.myAid` (WmReceiveFifo.c:72, 302) | The local station id |
| `connected_aid_bitmap` | u16 | field `WMArm9Buf.connectedAidBitmap` (WmReceiveFifo.c:71, 224, 313) | The group after the update |
| `ssid` | u8[24] | field `WMPortRecvCallback.ssid` (WmReceiveFifo.c:174, 318) | The network name on a state change |
| `reason` | u16 | field `WMPortRecvCallback.reason` (WmReceiveFifo.c:175, 315) | The reason code on a state change |
| `rssi` | u16 | field `WMPortRecvCallback.rssi` (WmReceiveFifo.c:176) | The signal |
| `max_send`, `max_recv` | u16 | field `WMPortRecvCallback.maxSendDataSize`, `maxRecvDataSize` (WmReceiveFifo.c:177 to 178, 322 to 323) | The frame budget |
| `state` | u16 | field `WMStartParentCallback.state` or `WMStartConnectCallback.state` (WmReceiveFifo.c:98, 112, 275, 285) | The connect state; 7, 9 and 26 are the change points |
| `src_mac` | u8[6] | field `WMStartConnectCallback.macAddress` (WmReceiveFifo.c:116, 288) | The parent's address |
| `dest_adrs` | u8[6] | field `WMStartMPCallback.destAdrs` (WmReceiveFifo.c:129) | The multipoint destination |
| `src_adrs` | u8[6] | field `WMStartMPCallback.srcAdrs` (WmReceiveFifo.c:130) | The multipoint source |
| `poll_bitmap` | u16 | field `WMStartMPCallback.pollbmp` (WmReceiveFifo.c:133) | The multipoint poll set |
| `time_stamp` | u16 | field `WMStartMPCallback.timeStamp` (WmReceiveFifo.c:127) | The frame time |
| `rate_rssi` | u16 | field `WMStartMPCallback.rate_rssi` (WmReceiveFifo.c:128) | The rate and signal |
| `send_data` | u32 | field `WMPortSendCallback.data` (WmReceiveFifo.c:148) | The bytes a send completed with |
| `dest_bitmap` | u16 | field `WMPortSendCallback.destBitmap` (WmReceiveFifo.c:144) | The send targets |
| `sent_bitmap` | u16 | field `WMPortSendCallback.sentBitmap` (WmReceiveFifo.c:146) | The stations already sent to |
| `rest_bitmap` | u16 | field `WMPortSendCallback.restBitmap` (WmReceiveFifo.c:145) | The stations left |
| `direction` | u8 | ctx, apiid 0x82 is receive and 0x81 is send (WmReceiveFifo.c:220 to 233) | One field for both directions |
| `peer` | u32 | field `aid` (WmReceiveFifo.c:168) | The station the frame is about |
| `owner` | u32 | local instance, 0 | Attributes the frame to an instance |

### wm.callback (0x0208EB0C, arm9, override)

`WMErrCode WM_SetPortCallback(u16 port, WMCallbackFunc callback, void *arg)`
(`src/matched/arm9_sdk/WM_SetPortCallback.c`, lines 98 to 137). An override
point.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `port` | u16 | arg `port` (WM_SetPortCallback.c:98, 110, 128) | The port the callback is for |
| `callback` | u32 | arg `callback` (WM_SetPortCallback.c:98, 128) | The callback installed, or 0 to clear |
| `arg` | u32 | arg `arg` (WM_SetPortCallback.c:98, 115, 129) | The callback's argument |
| `apiid` | u16 | field `WMPortRecvCallback.apiid` = 0x82 (WM_SetPortCallback.c:69, 107) | The init notification id |
| `state` | u16 | field `WMPortRecvCallback.state` = 25 (WM_SetPortCallback.c:71, 109) | The init state; a mod distinguishes the init call from a frame |
| `mac` | u8[6] | call `OS_GetMacAddress(...)` (WM_SetPortCallback.c:90, 117) | The local address |
| `connected_aid_bitmap` | u16 | call `WM_GetConnectedAIDs()` (WM_SetPortCallback.c:95, 131) | The group at install |
| `my_aid` | u16 | call `WM_GetAID()` (WM_SetPortCallback.c:96, 132) | The local station id |
| `result` | u32 | ret of `WM_SetPortCallback` (WM_SetPortCallback.c:121, 124, 136) | The install result |
| `owner` | u32 | local instance, 0 | Attributes the install to an instance |

## 7. Dwc

`DWCi_Init` (0x02059FA8) is the WiFi interface initialiser and
`DWC_InitForProduction` (0x02059F6C) is the production wrapper. No responder
answers DWC (`docs/modding.md`, "Networking seam: missing").

### dwc.init (0x02059FA8, arm9)

`int DWCi_Init(int authserver, const char *gameName, u32 gameCode, DWCAllocEx alloc, DWCFreeEx free)`
(`src/matched/arm9_sdk/func_02059FA8.c`, lines 73 to 116).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `authserver` | u32 | arg `authserver` (func_02059FA8.c:73, 105, 110) | Which authentication server; 0 is development and 1 production |
| `game_name` | u32 | arg `gameName` (func_02059FA8.c:73, 108) | The game name copied into the global |
| `game_code` | u32 | arg `gameCode` (func_02059FA8.c:73, 106) | The game code, a WiFi identity; a mod reads it for a session |
| `alloc` | u32 | arg `alloc` (func_02059FA8.c:73, 80) | The allocator the interface uses |
| `free` | u32 | arg `free` (func_02059FA8.c:73, 80) | The deallocator |
| `work` | u32 | call `DWC_Alloc(...)` (func_02059FA8.c:84) | The init work buffer |
| `backup_result` | u32 | call `DWC_BM_Init(work)` (func_02059FA8.c:87) | The backup manager result |
| `wifi_id_created` | u8 | call `DWC_Auth_CheckWiFiIDNeedCreate()` (func_02059FA8.c:88, 90) | Whether a WiFi ID was made; a mod must not duplicate it |
| `stats_host` | u32 | call, the `StatsServerHostname` global (func_02059FA8.c:69, 110 to 113) | The statistics host chosen by the auth server |
| `result` | u32 | ret of `DWCi_Init` (func_02059FA8.c:93 to 103, 115) | 0 to 4, where 0 is a clean init and 1 to 3 report a created ID or a failure |
| `owner` | u32 | local instance, 0 | Attributes the init to an instance |

### dwc.production (0x02059F6C, arm9)

`int DWC_InitForProduction(const char *gameName, u32 gameCode, DWCAllocEx alloc, DWCFreeEx free)`
(`src/matched/arm9_sdk/func_02059F6C.c`, lines 31 to 35). It forwards to
`DWCi_Init` with the production auth server.

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `game_name` | u32 | arg `gameName` (func_02059F6C.c:31, 34) | Forwarded to `DWCi_Init` |
| `game_code` | u32 | arg `gameCode` (func_02059F6C.c:31, 34) | Forwarded to `DWCi_Init` |
| `alloc` | u32 | arg `alloc` (func_02059F6C.c:31, 34) | Forwarded to `DWCi_Init` |
| `free` | u32 | arg `free` (func_02059F6C.c:31, 34) | Forwarded to `DWCi_Init` |
| `authserver` | u32 | call, `DWC_AUTHSERVER_PRODUCTION` = 1 (func_02059F6C.c:25, 34) | The production server, so a mod distinguishes it from `dwc.init` |
| `result` | u32 | ret of `DWCi_Init` (func_02059F6C.c:34) | The init result, as in `dwc.init` |
| `owner` | u32 | local instance, 0 | Attributes the init to an instance |

## 8. Network script commands

The five entries are the field VM's network commands. The comm handle they read
is `GAMESYSTEM_GetGameComm` (0x02012138) and the kind is byte +5
(`src/matched/arm9/unit_02026454.c`, lines 47 to 54).

### network.wait (0x0215F1C8, ov010, state)

`int SCRCMD_NETWORK_CmdWaitCommState(void *a, void *b)`
(`src/matched/ov/ov010_func_0215F1C8.c`, lines 33 to 47). A state accessor: it
sets the VM to wait on `SCRCMD_NETWORK_WaitCommState` (0x0215F168).

| Accessor | Inputs | Output | Where it comes from | Why a mod wants it |
|---|---|---|---|---|
| `bw_comm_wait_state` | script, VM | u32 state | `SCRCMD_NETWORK_WaitCommState` reads `FIELDMAP_CheckCommExit(comm)` (func_0215F168.c:53, 62) | The comm exit state 0, 1 or 2 a script waits on |

Fields exposed by the accessor: `script` (arg `b`, ov010_func_0215F1C8.c:33),
`work_ptr` (call `SCRIPT_WORK_ReadWorkPointer`, line 40), `slot` (call
`SCRIPT_WORK_GetUserWorkPtr`, line 39), `wait_fn` (call, the wait function
pointer, line 45), `comm` (call `GAMESYSTEM_GetGameComm`, func_0215F168.c:53),
`field_map` (call `GAMESYSTEM_GetFieldMap`, func_0215F168.c:56), `exit_state`
(call `FIELDMAP_CheckCommExit`, func_0215F168.c:62).

### network.value (0x0215F214, ov010, state)

`int SCRCMD_NETWORK_CmdGetCommValue(int a0, int a1)`
(`src/matched/ov/func_0215F214.c`, lines 26 to 35). A state accessor.

| Accessor | Inputs | Output | Where it comes from | Why a mod wants it |
|---|---|---|---|---|
| `bw_comm_value` | script, VM | u16 value | `APP_ENV_InitParentalControl()` stored at work `f0` (func_0215F214.c:31 to 33) | The value a script reads from the comm work |

Fields: `work_ptr` (call `SCRIPT_WORK_ReadWorkPointer`, line 29), `value` (call
`APP_ENV_InitParentalControl`, line 31, stored at `Obj17703.f0`, line 33).

### network.start (0x0215F228, ov010)

`int SCRCMD_NETWORK_CmdStartAutoComm(int a0, int a1)`
(`src/matched/ov/func_0215F228.c`, lines 25 to 35).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `script` | u32 | arg `a1` (func_0215F228.c:25, 29) | The script frame that runs the command |
| `game_system` | u32 | call `SCRCMD_WORK_GetGameSystem(a1)` (func_0215F228.c:30) | The game system the comm starts in |
| `start_result` | u32 | call `GAMESYSTEM_TryStartAutoComm(v0)` (func_0215F228.c:32) | 1 when auto comm started; a mod sees the start |
| `result` | u32 | ret of the command (func_0215F228.c:34) | Always 1; the command itself succeeded |
| `owner` | u32 | local player, 0 | Attributes the start to a player |

`GAMESYSTEM_TryStartAutoComm` returns 1 only when the run state is 0, the kind
is 0 and `GAMESYSTEM_CanStartAutoComm` is 1, then calls
`GAME_COMM_Start(handle, 1, p)` (`src/matched/arm9/unit_020121CC.c`, lines 47 to
60). The port reads those three tests from the tracked comm context.

### network.exit (0x0215F238, ov010)

`int SCRCMD_NETWORK_CmdRequestCommExit(void *a, void *b)`
(`src/matched/ov/ov010_func_0215F238.c`, lines 33 to 54).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `script` | u32 | arg `b` (ov010_func_0215F238.c:33, 38) | The script frame that runs the command |
| `game_system` | u32 | call `SCRCMD_WORK_GetGameSystem(b)` (ov010_func_0215F238.c:38) | The game system the comm exits from |
| `comm` | u32 | call `GAMESYSTEM_GetGameComm(t)` (ov010_func_0215F238.c:39) | The comm object |
| `kind` | u32 | call `GAME_COMM_GetKind(s)` (ov010_func_0215F238.c:40) | The request kind; 1, 2 and 5 exit, 0, 3 and 4 do not |
| `exited` | u8 | call, the switch on kind (ov010_func_0215F238.c:41 to 45) | Whether the exit was requested; a mod mirrors it to the peer |
| `blocked` | u32 | call `GAMESYSTEM_SetCommBlocked(t, 1)` (ov010_func_0215F238.c:51) | The comm is blocked after the exit |
| `check_flag` | u32 | call `SCRIPT_WORK_SetCheckFlag(0xb)` (ov010_func_0215F238.c:52) | The script flag the exit sets |
| `result` | u32 | ret of the command (ov010_func_0215F238.c:53) | Always 1 |
| `owner` | u32 | local player, 0 | Attributes the exit to a player |

### network.restart (0x0215F280, ov010)

`int SCRCMD_NETWORK_CmdRestartAutoComm(void *a0, void *a1)`
(`src/matched/ov/func_0215F280.c`, lines 33 to 43).

| Field | Type | Where it comes from | Why a mod wants it |
|---|---|---|---|
| `script` | u32 | arg `a1` (func_0215F280.c:33, 35) | The script frame that runs the command |
| `game_system` | u32 | call `SCRCMD_WORK_GetGameSystem(a1)` (func_0215F280.c:35) | The game system the comm restarts in |
| `unblocked` | u32 | call `GAMESYSTEM_SetCommBlocked(p, 0)` (func_0215F280.c:37) | The comm is unblocked |
| `start_result` | u32 | call `GAMESYSTEM_TryStartAutoComm(p)` (func_0215F280.c:38) | 1 when auto comm started again |
| `check_flag` | u32 | call `SCRIPT_WORK_ResetCheckFlag(0xB)` (func_0215F280.c:39) | The script flag the restart clears |
| `result` | u32 | ret of the command (func_0215F280.c:41) | Always 1 |
| `owner` | u32 | local player, 0 | Attributes the restart to a player |

## 9. The network context

The networking payloads share state that no single call carries. The port keeps
one `BwNetContext` and fills it from the events that do carry the pieces. A
stale field is worse than a missing one, so each field names its feed and its
clear point. The context is cleared at `bw_ds_reset` and at process start
(`sdk/dsmem.h`, `bw_ds_reset`), and the link fields are cleared when
`link.down` fires.

| Context field | Type | Fed by | Cleared at | Why the payloads need it |
|---|---|---|---|---|
| `link_on` | u8 | the `SOCL_LinkIsOn` return at 0x0215CD24 (ov009_func_0215CD24.c:22) | reset, `link.down` | Distinguishes `link.up` from `link.down` and a poll from a change |
| `ap_mac` | u8[6] | `WCM_GetApMacAddress()` at 0x0215CD24 (ov009_func_0215CD24.c:18) | reset, `link.down` | The only peer-side identifier the link predicate offers |
| `link_level` | u32 | `WCM_GetLinkLevel()` (ov009_func_021666C8.c:42) | reset, `link.down` | The signal at a link event |
| `rssi` | u8 | `WCMi_GetRssiAverage()` (ov009_func_021666C8.c:52) | reset, `link.down` | The raw signal behind the level |
| `device_id` | u8 | field `WCMWork.deviceId` (ov009_func_02166710.c:39) | reset | Selects the level thresholds |
| `wm_initialized` | u16 | field `g_02153058.wmInitialized` (WM_Init.c:70, 76) | reset | Tells a live manager from a disabled one |
| `my_aid` | u16 | field `WMArm9Buf.myAid`, set at `wm.connect` to 0 and at `wm.fifo` from the callback (func_0208F84C.c:119; WmReceiveFifo.c:302) | reset | The local station id every frame needs |
| `connected_aid_bitmap` | u32 | field `WMArm9Buf.connectedAidBitmap`, updated at `wm.fifo` (WmReceiveFifo.c:298 to 301) | reset | Which stations are in the group |
| `mp_recv_buf_size` | u16 | field `WMStatus.mp_recvBufSize` (WmReceiveFifo.c:56) | reset, `wm.mp.start` | The receive budget the frames are sized to |
| `port_callback[16]` | u32[16] | field `WMArm9Buf.portCallbackTable`, set at `wm.callback` (WM_SetPortCallback.c:128) | reset | Names the handler a port frame reaches |
| `port_arg[16]` | u32[16] | field `WMArm9Buf.portCallbackArgument`, set at `wm.callback` (WM_SetPortCallback.c:129) | reset | The handler's argument |
| `comm_kind` | u8 | field `Comm.kind` at `comm.update` (func_0202627C.c:50) and `Obj.f5` at the dispatch (func_02026388.c:44) | reset, `network.exit` | The action behind a comm frame |
| `comm_state` | u8 | field `Sub.state` at `comm.update` (func_0202627C.c:38) | reset, `network.exit` | The handshake position |
| `comm_last_kind` | u8 | field `Comm.f06` at `comm.update` (func_0202627C.c:51) | reset | The kind last started |
| `comm_counter` | u32 | field `Comm.f08` at `comm.update` (func_0202627C.c:53) | reset | The request counter |
| `comm_work` | u32 | field `Comm.f14` at `comm.update` (func_0202627C.c:55) | reset | The work the callbacks share |
| `comm_blocked` | u8 | `GAMESYSTEM_SetCommBlocked` at `network.exit` and `network.restart` (ov010_func_0215F238.c:51; func_0215F280.c:37) | reset | Tells an exit from a live comm |
| `dwc_initialized` | u8 | `dwc.init` and `dwc.production` results (func_02059FA8.c:93 to 103) | reset | Tells a live WiFi interface from a disabled one |
| `game_code` | u32 | field `dwci_gamecode`, set at `dwc.init` (func_02059FA8.c:106) | reset | The WiFi game identity |
| `stats_host` | u32 | the `StatsServerHostname` global at `dwc.init` (func_02059FA8.c:110 to 113) | reset | The statistics host |
| `peer` | u32 | the `aid` of `wm.fifo` and `wm.share.recv` (WmReceiveFifo.c:168; WmDataSharingReceiveData.c:77) | reset, `link.down` | The remote station a frame is about |
| `bytes_in_flight` | ring of `{port, aid, seq, dir, len, bytes}` | the send and receive points of section 4 | reset | Orders and replays the last frames; no transport exists to feed it yet |

The `bytes_in_flight` ring has no feed today. The transport is missing, so the
ring stays empty and `net.send` and `net.receive` do not fire.

## 10. Fields a mod wants that cannot be obtained

Each of these is named so the port can add it when the evidence arrives, rather
than let a mod bind to an absent field.

- **The peer identity at `link.up` and `link.down`.** `SOCL_LinkIsOn` returns a
  boolean over the access point MAC (ov009_func_0215CD24.c:22). It carries no
  peer. The port takes the peer from the WM AID or the DWC profile, and neither
  is a stable cross-instance identity without a session.
- **The cause of a save.** The requester is one call earlier in
  `GAME_DATA_SaveStart` (0x02012DAC), which is not in the declared set. The port
  cannot fill a cause field without a second hook.
- **The result of a save at `save.done` entry.** The return is ahead. The port
  must run the original first or track it.
- **The new overlay's data at `overlay.load`.** The load has not run. A handler
  cannot read the overlay's data or call its functions at the entry point.
- **The action behind a comm kind.** The kind is a number and the callback
  record's words are addresses (func_0202627C.c:62 to 68). Aletheia does not
  name which kind is a battle turn and which is a trade
  (`docs/mod-events.md`, still-to-build item 13).
- **The bytes for `net.send` and `net.receive`.** No transport exists, so the
  bytes never reach the payload (`docs/modding.md`, "Networking seam: missing").
- **The DWC profile and friend code.** `DWCi_Init` takes a game code and two
  allocators (func_02059FA8.c:73). The login and the profile happen later and
  are not at the init call.

## 11. The thinnest payloads

These carry the least, so a mod that needs more must read state through an
accessor or wait for a wider field.

- `save.flag`: a flag id and a boolean (func_02012BF8.c:18).
- `save.block` and `save.misc`: a block id and a pointer (unit_020071CC.c:60;
  func_02012F08.c:19).
- `link.level`: one number from 0 to 3 (ov009_func_021666C8.c:42).
- `dwc.production`: the four forwarded arguments and the auth server constant
  (func_02059F6C.c:31 to 34).
- `wm.enable`: a path and a result (func_0208F224.c:21 to 23).
- `overlay.start`: the descriptor and the digest result (func_0207902C.c:78 to
  128).
- `network.value`: one `u16` (func_0215F214.c:31 to 33).
- `comm.handler` and `comm.notify`: the object, a kind and a callback pointer
  (func_02026388.c:41 to 49; func_020263B0.c:41 to 49).

## 12. Still to build

1. **The transport.** `sdk/arm7/` has no Wm or DWC responder and the tree has no
   sockets (`docs/modding.md`, "Networking seam: missing"). The `net.send` and
   `net.receive` payloads of section 4 have no feed until it exists.
2. **The send and receive points.** Which declared Wm, DWC or `GAME_COMM_*`
   address is the send and which the receive point is not fixed
   (`docs/mod-events.md`, still-to-build item 12).
3. **The overlay event ordering.** Where the handler runs relative to the port's
   copy, bind, relocate and static initialisers is not fixed
   (`docs/mod-events.md`, still-to-build item 10). The fields of section 2 are
   known at every point; the data is not.
4. **The comm action names.** The kind to action map is not named
   (`docs/mod-events.md`, still-to-build item 13).
5. **A cross-instance player identity.** No accessor gives a stable player id
   for the `owner` and `peer` fields. The port must fix one before a soullink
   rule can attribute a payload.
6. **The save cause.** The port needs a second hook on `GAME_DATA_SaveStart`
   (0x02012DAC) to fill the cause of `save.begin`, or it must state the cause as
   absent.
7. **The peer identity at the link events.** The link predicate carries only the
   access point MAC; the peer must come from the WM or DWC context.
