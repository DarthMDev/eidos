# Co-op mod enablers

This document says what the port must expose for a co-op mode where players
see each other in the field, and nothing more. The mode itself, its session,
its lobby and its rules are a separate project. `docs/modding.md` says what
the four example mods need and what exists today. `docs/mod-interface.md`
fixes the general mod contract, the declared set, the event API and the trust
model. This document is the field-and-network subset a co-op mode needs on top
of them, with the evidence for each enabler and the gap where it does not
exist.

The co-op shape this document works from is the one a modern co-op game has:
a second player is a body in the same field, drawn by the game's own renderer
and moved by the game's own simulation, with the two instances exchanging
enough state to agree on where each player is. It is not the Nintendo DS link
features (trade, battle, the C-Gear), which the game already contains.

`sm64coopdx` is the closest precedent: a PC port that synchronizes the world
and adds a second player entity through the game's own object system, driven
by a mod API. It is a precedent for the shape, not a source of facts about
this port.

## 1. The state a co-op mode must read and write

A co-op mode reads and writes the field map, the player object and its
position, the camera, the entity list and its spawn and despawn, the warps,
and the party. Each entry below says what exists in aletheia's matched code,
with the file or the address it was read from, and whether the port already
exposes it.

The port exposes none of the field code today. The eidos build list
(`CMakeLists.txt`) carries no `FIELDMAP`, `FLDMMDL`, `FIELD_PLAYER` or `ov021`
row, and `README.md`, "Planned Milestones", leaves Milestone 7 (Field system:
player movement, warps, script VM, encounters) unchecked. The only field
addresses the port names are three rows in `mod/declared_set.txt`:
`FIELD_MAP_CHANGE_SetupZone` 0x021643E8, `FIELD_MAP_CHANGE_ApplyZoneHeader`
0x021643AC and `EVENT_MAPCHANGE_ChangeFieldEvent` 0x021841C0. Those rows are a
published artifact, not compiled code: the slot the artifact drives is
still-to-build item 1 of `docs/mod-interface.md`, so no address in the set is
reachable from a mod today.

### The field map

aletheia: `FIELDMAP_Create` 0x02188030 (ov021) allocates the 0x154-byte field
map object and stores the game system, the player zone id and the area data
(`src/matched/ov/ov021_func_02188030.c`). The accessors are
`FIELDMAP_GetZoneId` 0x02188C9C (the halfword at +0xD8),
`FIELDMAP_GetGameSystem` 0x02188C80 (+0x4), `FIELDMAP_GetMmdlSys` 0x02188C7C
(+0x38), `FIELDMAP_GetCamera` 0x02188C58 (+0x10), `FIELDMAP_GetFieldPlayer`
0x02188CB4 (+0x8C) and `FIELDMAP_GetCtrlWork` 0x02188CA4 (+0x10C).
`FIELDMAP_AddZoneNpcs` 0x021894E0 reads a zone's NPC count and headers and
adds them through `FLDMMDL_AddMmdlHeaderList`.

`docs/mod-interface.md`, "State fields a rule may name", already names the map
and zone accessors: `GAME_DATA_GetPlayerZoneId` 0x02012AC8 and
`GAMESYSTEM_GetFieldMap` 0x0201210C for reading, and `GAME_DATA_SetPlayerZoneId`
0x02012AC4 for writing. The player work behind the zone id is
`GAMESYSTEM_GetMyPlayerWork` 0x020120F8, named in `FIELDMAP_Create`'s symbols
(`src/matched/ov/ov021_func_02188030.c`).

Port: not exposed. No field map accessor is defined.

### The player object and its position

aletheia: `FIELD_PLAYER_Create` 0x021A2E50 (ov021) allocates a 0x10-byte
wrapper whose word at +0x4 is the core (`src/matched/ov/ov021_func_021A2E50.c`).
`FIELD_PLAYER_CORE_Create` 0x021A36E4 allocates the 0x34-byte core, finds the
player's entity by `FLDMMDL_SysSearchObjId(mmdl_sys, 0xff)` and creates one
with `FLDMMDL_AddMmdl` when none exists
(`src/matched/ov/ov021_func_021A36E4.c`). The position is a `VecFx32` at
entity +0x44: `FIELD_PLAYER_GetPos` 0x021A3038 calls `FIELD_PLAYER_CORE_GetPos`
0x021A3898, which calls `FLDMMDL_GetPos` 0x0216D88C
(`src/matched/ov/ov010_func_0216D88C.c`). The setter is
`FIELD_PLAYER_CORE_SetPos` 0x021A38A4, which writes the grid cell through
`FLDMMDL_SetGridNowX/Y/Z` and the position through `FLDMMDL_SetPos` 0x0216D89C.
The grid cell reads through `FIELD_PLAYER_GetNowGridPos` 0x021A35FC,
`FLDMMDL_GetGridNowX` 0x0216D83C and `FLDMMDL_GetGridNowZ` 0x0216D86C. The
player is the entity with obj id 255 (`FLDMMDL_SysSearchPlayer` 0x0216DEE0,
which tail-calls `FLDMMDL_SysSearchObjId` with id 255).

Port: not exposed.

### The camera

aletheia: `FIELD_CAMERA_Create` 0x0218E018 and `FIELD_CAMERA_Main` 0x0218E104
(ov021) own the field camera, reached by `FIELDMAP_GetCamera` 0x02188C58. The
readers and writers are `FIELD_CAMERA_GetYaw` 0x0218ED40 and
`FIELD_CAMERA_SetYaw` 0x0218ED48 (the halfword at +0x7A),
`FIELD_CAMERA_GetPitch` 0x0218ED2C and `FIELD_CAMERA_SetPitch` 0x0218ED34,
`FIELD_CAMERA_GetDistance` 0x0218ED54 and `FIELD_CAMERA_SetDistance`
0x0218ED58, and `FIELD_CAMERA_GetG3dCamera` 0x0218EC2C (+0xC).
`FIELDMAP_UpdateCameraAndDraw` 0x021888C8 runs `FIELD_CAMERA_Main` each frame
and applies the pitch scale.

Port: not exposed.

### The entity list, and its spawn and despawn

aletheia: the field entity system is FLDMMDL, the field model system in ov010.
`FLDMMDL_CreateSys` 0x0216CC58 allocates the 0x48-byte system and the entity
pool (`src/matched/ov/ov010_func_0216CC58.c`). `FLDMMDL_SysGetMmdlMax`
0x0216D4F0 returns the pool count from the system record (field +0x4). One
entity is created by `FLDMMDL_AddMmdl` 0x0216CE60 into `FLDMMDL_AddMmdlCore`
0x0216CDC4, which takes a 0x24-byte header, finds a free slot with
`FLDMMDL_SysGetFreeMmdl` 0x0216DEEC and runs the initialisers
(`src/matched/ov/ov010_func_0216CDC4.c`). A whole zone's list is created by
`FLDMMDL_AddMmdlHeaderList` 0x0216CEAC. Deletion is `FLDMMDL_DeleteMmdl`
0x0216CF2C and `FLDMMDL_DeleteAllMmdl` 0x0216CF6C. The list is read by
`FLDMMDL_SysSearchObjId` 0x0216DE94 (by obj id) and `FLDMMDL_SysSearchGridPos`
0x0216DDC0 (by grid cell). A single entity reads and writes
`FLDMMDL_GetPos` 0x0216D88C and `FLDMMDL_SetPos` 0x0216D89C,
`FLDMMDL_GetObjId` 0x0216D588, `FLDMMDL_GetEventId` 0x0216D5B8 and
`FLDMMDL_SetEventId` 0x0216D5B4, and the grid cell through
`FLDMMDL_GetGridNowX` 0x0216D83C, `FLDMMDL_GetGridNowY` 0x0216D854 and
`FLDMMDL_GetGridNowZ` 0x0216D86C.

Port: not exposed.

### Warps

aletheia: the map change system is ov020. `EVENT_MAPCHANGE_ChangeFieldEvent`
0x021841C0 is in the declared set. The warp creators are
`EVENT_MAPCHANGE_CreateWarpEvent` 0x02184D38,
`EVENT_MAPCHANGE_CreateSituationWarpEvent` 0x02184F40 and
`EVENT_MAPCHANGE_CreateZoneStartWarpEvent` 0x02184FD4. The zone setup is ov010:
`FIELD_MAP_CHANGE_SetupZone` 0x021643E8 and
`FIELD_MAP_CHANGE_ApplyZoneHeader` 0x021643AC, both in the declared set. A warp
location resolves through `MAP_REPLACE_WarpIdToIndex` 0x0215A064 and applies
through `MAP_REPLACE_ApplyWarpLocation` 0x02159FA8
(`src/matched/ov/ov010_func_02159FA8.c`).

Port: the three declared addresses are rows in `mod/declared_set.txt` only. No
slot is generated and no warp accessor is defined.

### The party

aletheia: `GAME_DATA_GetMyParty` 0x02012934, `POKEPARTY_GetMember` 0x0201AB28,
`POKEPARTY_GetCount` 0x0201AA18 and `POKEPARTY_Add` 0x0201A98C, named in
`docs/mod-interface.md`, "State fields a rule may name". These are state
accessors, not override points, so they are not in the declared set.

Port: `docs/mod-interface.md`, section 4, says a mod reaches them through
`bw_ds_ptr` (`sdk/dsmem.h`) or the matched accessors the port defines. They are
not mod-facing: no mod loader exists (still-to-build item 4 of
`docs/mod-interface.md`), so nothing hands a mod a handle to them.

## 2. The entity problem

Drawing and moving a second player means making the game's own code render and
simulate an entity it did not create. This is the hardest part of the
document, and it is the part that decides whether a co-op mode is possible.

The game's entity system is FLDMMDL, a fixed pool of field model records in
ov010. It is not a general scene graph. Each record carries a draw procedure
and a move procedure, and both are the game's own code: `FLDMMDL_UpdateDraw`,
`FLDMMDL_CallDrawProc`, `FLDMMDL_UpdateMove` and `FLDMMDL_CallMoveProc`
(ov010). The player is not a separate kind of object. `FIELD_PLAYER_CORE_Create`
0x021A36E4 makes the player's MMDL record through `FLDMMDL_AddMmdl` and finds
it afterwards by obj id 255 (`src/matched/ov/ov021_func_021A36E4.c`). NPCs are
MMDL records made from a zone's event data through `FLDMMDL_AddMmdlHeaderList`
(`FIELDMAP_AddZoneNpcs` 0x021894E0). So the record the game renders and
simulates is the same record a mod would create for a second player.

Whether a mod can create an entity at run time. The game's own API creates one.
`FLDMMDL_AddMmdl` 0x0216CE60 into `FLDMMDL_AddMmdlCore` 0x0216CDC4 takes a
header, calls `FLDMMDL_SysGetFreeMmdl` 0x0216DEEC to find a free pool slot, and
runs the draw and move initialisers. The game's code therefore renders and
moves an entity a mod creates, provided a free slot exists. Two things the
evidence read here does not settle:

- the pool headroom. `FLDMMDL_SysGetMmdlMax` 0x0216D4F0 returns the count from
  the system record, and `FLDMMDL_CreateSys` 0x0216CC58 sets it from a runtime
  argument (`src/matched/ov/ov010_func_0216CC58.c`). Whether a field leaves a
  free slot for a second player at any moment is runtime data, not a constant
  in the matched code.
- the model. `FLDMMDL_AddMmdlCore` initialises the record from its header. The
  player's header template is `gFieldPlayerAvatarHeaderTemplate` 0x021D89A0
  and the obj code comes from `FIELD_PLAYER_GetObjCodeOfFormCode` 0x021A31A4,
  both named in `FIELD_PLAYER_CORE_Create`'s symbols
  (`src/matched/ov/ov021_func_021A36E4.c`). A second player needs an obj code
  and its resources loaded through the FLDMMDL resource system
  (`FLDMMDL_LoadObjCodeParam`, `FLDMMDL_BLACT_*`, ov010). Whether two
  instances of the player's own obj code can be resident at once is not settled
  by the evidence read.

What the port would have to expose for it:

- the field overlays compiled in (ov010 and ov021), so the entity code exists
  to call at all. The port builds neither today, and that is Milestone 7.
- a slot on `FLDMMDL_AddMmdlCore` 0x0216CDC4 and `FLDMMDL_DeleteMmdl`
  0x0216CF2C, so a mod can wrap creation and deletion.
- a slot or accessor for the list walk (`FLDMMDL_SysSearchObjId` 0x0216DE94,
  `FLDMMDL_SysSearchGridPos` 0x0216DDC0) and for the per-entity position
  (`FLDMMDL_GetPos` 0x0216D88C, `FLDMMDL_SetPos` 0x0216D89C).
- a network identity for an entity that is not the game's obj id.
  `FLDMMDL_SysSearchObjId` keys on the game's obj id, and the player's is the
  fixed 255, so a co-op mod must add its own mapping.

The port must compile the field code before any of this exists, and the field
code is not in the build.

## 3. The transport seam

The game's own networking lands on PXI tag 10 (wireless, Wm) and, above it,
DWC. `sdk/arm7/` answers OS, PM, RTC, CTRDG, CARD, FS, SND, mic, pad and tp,
and nothing answers Wm or DWC (`sdk/arm7/arm7.h`; `docs/modding.md`,
"Networking seam: missing"). `sdk/pxi.h` names the tag (`BW_PXI_TAG_WM = 10`),
and `sdk/pxi.c`'s `arm7_dispatch` logs "no ARM7 responder for tag 10" and
returns the word with the error bit.

What the game's Wm code needs behind it:

- `WM_Init` 0x0208E010 into `WmInitCore` 0x0208E038 calls `PXI_Init` and then
  `PXI_IsCallbackReady(10, 1)` once, without spinning. With no tag-10
  responder it returns `WM_ERRCODE_WM_DISABLE` (4), the SDK's "wireless not
  available" path (`sdk/arm7/arm7.h`; `src/matched/arm9_sdk/func_0208E038.c`).
- `WmInitCore` installs `WmReceiveFifo` 0x0208E5BC as the ARM9's tag-10
  callback (`PXI_SetFifoRecvCallback(10, WmReceiveFifo)`). `WmReceiveFifo`
  reads the callback block at the address the ARM7 sent and dispatches it,
  including the per-port receive and send callbacks (`WMPortRecvCallback`,
  `WMPortSendCallback`, `src/matched/arm9_sdk/WmReceiveFifo.c`).
- the Wm command layer sends raw 32-bit buffer addresses to the ARM7.
  `WM_SetMPDataToPortEx` 0x0208FCB4 calls `WMi_SendCommand(WM_APIID_SET_MP_DATA,
  7, sendData, ...)` with `sendData` a buffer address
  (`src/matched/arm9_sdk/WM_SetMPDataToPortEx.c`). `WM_StartConnectEx`
  0x0208F84C, `WM_StartMP` 0x0208FC4C, `WM_StartDataSharing` 0x0209004C,
  `WM_GetSharedDataAddress` 0x02090B18, `WmDataSharingSendDataSet` 0x020909C8,
  `WmDataSharingReceiveData` 0x020908F8 and `WM_SetPortCallback` 0x0208EB0C
  are the rest of the command and port surface.
- DWC sits on Wm and its own stack: `DWCi_Init` 0x02059FA8 and
  `DWC_InitForProduction` 0x02059F6C
  (`src/matched/arm9_sdk/func_02059FA8.c`). The game's comm layer above it is
  `GAME_COMM_Update` 0x0202627C, `GAME_COMM_CallHandler` 0x02026388 and
  `GAME_COMM_CallKindNotify` 0x020263B0 (arm9).

The seam, with two options and the evidence on each side:

- A tag-10 responder in `sdk/arm7/`. The port installs a responder with
  `bw_pxi_arm7_set_callback(BW_PXI_TAG_WM, ...)` (`sdk/pxi.h`) and answers the
  Wm command protocol, so the game's own Wm and DWC run unchanged and the port
  carries the bytes. The cost is the whole WM command protocol, whose words are
  raw 32-bit buffer addresses (`sdk/arm7/arm7.h`), plus a wireless model
  (connect, MP send and receive, data sharing).
- A port transport with the declared set's Wm and DWC slots. A co-op mod wraps
  `WM_SetMPDataToPortEx` 0x0208FCB4 for bytes out and `WmReceiveFifo`
  0x0208E5BC for bytes in, and routes them to its own transport, leaving the
  game's networking disabled. The declared set already names both, plus
  `WM_Init`, `WM_Enable`, `WM_StartConnectEx`, `WM_StartMP`,
  `WM_StartDataSharing`, `WM_GetSharedDataAddress`, `WmDataSharingSendDataSet`,
  `WmDataSharingReceiveData`, `WM_SetPortCallback`, `DWCi_Init` and
  `DWC_InitForProduction` (`mod/declared_set.txt`, the multiplayer rows). The
  cost is a socket layer, which the port has none of today
  (`docs/modding.md`, "Networking seam: missing"; no sockets anywhere in the
  tree).

The port must add a transport either way. The tag-10 responder is the deeper
seam: it is where the game's own bytes already land, and it keeps the game's
Wm path intact.

## 4. Authority and determinism

A co-op mode must decide who owns an entity, what happens when two instances
disagree, and how much of the simulation must match. The port's evidence bounds
the options.

Who owns an entity. The game owns its MMDL pool and the zone NPCs. An entity is
identified by the game's obj id (`FLDMMDL_GetObjId` 0x0216D588,
`FLDMMDL_SysSearchObjId` 0x0216DE94), and the player's is the fixed 255
(`FLDMMDL_SysSearchPlayer` 0x0216DEE0). The game has no network identity for an
entity, so a co-op mod adds its own id mapping. One option is one instance per
entity, the owner simulating and the peer rendering a puppet; another is one
authority for the whole world. Neither is decided by the matched code.

Conflicts. The game does not arbitrate a write. `docs/mod-interface.md`,
section 4, says "The port does not arbitrate a write", "the game's later write
wins", and the port "restores nothing and offers no transaction". A co-op mode
therefore owns authority itself.

How much must match. The field simulation is not built for lockstep. The player
moves off the key input (`FIELD_PLAYER_CORE_ChooseMoveDirFromKeys`,
`FIELD_PLAYER_GRID_Move`) and the MMDL move procedures run once per frame
(`FLDMMDL_UpdateMove`). Randomness is global and unshared:
`FLDMMDL_MOVECODE_GetRandomTableEntry` 0x0219B0F0 calls `func_020056EC`
0x020056EC (`src/matched/ov/func_0219B0F0.c`), and the standard generator is
MT19937 `GF_STANDARD_Rand` 0x0203F0A8
(`src/matched/arm9/func_0203F0A8.c`). `docs/modding.md`, still-to-build item 5,
states the choice: soullink needs only event agreement, multiplayer may need
lockstep. The options:

- Event agreement only. Each instance runs its own world and the mod sends
  semantic events (position, warp, party). This is the cheapest and is what
  soullink needs.
- Entity ownership. One instance owns each entity, simulates it and
  broadcasts, and the peer renders a puppet. This needs the entity seam of
  section 2 and a mod-owned network id.
- Lockstep. Both instances run the same simulation from the same inputs. This
  needs a shared random source and a fixed frame. The port provides neither,
  and every global RNG call above would have to be routed through the shared
  source.

The document states the options rather than choosing. The choice depends on the
entity seam and on whether the port adds a shared random source.

## 5. What the port must expose

The concrete additions, so a co-op project can start.

**A. Compile the field code.** Add the field overlays (ov010, ov021) and their
ARM9 closure to the boot source list. The evidence is the eidos build list,
which carries no `FIELDMAP`, `FLDMMDL`, `FIELD_PLAYER` or `ov021` row, and
`README.md`, "Planned Milestones", which leaves Milestone 7 unchecked. Without
this the addresses below are not callable at all.

**B. Declared-set additions.** One row per address, in the format of
`mod/declared_set.txt`. Each is one more override point, and its reason is the
entity or warp step a co-op mode intercepts. The tag is `co-op`, a new value
the artifact's vocabulary does not have yet; `multiplayer` is the closest
existing tag.

| Address | Function | Region | Reason |
|---|---|---|---|
| 0x02188030 | FIELDMAP_Create | ov021 | field map create |
| 0x021894E0 | FIELDMAP_AddZoneNpcs | ov021 | zone NPC spawn |
| 0x0216CDC4 | FLDMMDL_AddMmdlCore | ov010 | entity create |
| 0x0216CF2C | FLDMMDL_DeleteMmdl | ov010 | entity delete |
| 0x021A36E4 | FIELD_PLAYER_CORE_Create | ov021 | player create |
| 0x0218E104 | FIELD_CAMERA_Main | ov021 | camera step |
| 0x02184D38 | EVENT_MAPCHANGE_CreateWarpEvent | ov020 | warp create |
| 0x02159FA8 | MAP_REPLACE_ApplyWarpLocation | ov010 | warp apply |

The Wm and DWC rows are already in the set (`mod/declared_set.txt`).

**C. A state API.** A named accessor set over the field map, the player
position, the camera, the entity list, the warps and the party, with a read and
write contract, in the shape `docs/mod-interface.md`, section 4, describes. The
accessors exist in aletheia (section 1 above) and are reached through
`bw_ds_ptr` (`sdk/dsmem.h`) or the matched accessors the port defines. What is
missing is the field subset and a mod-facing handle, because no mod loader
exists (still-to-build item 4 of `docs/mod-interface.md`).

**D. The transport seam.** One of the two options of section 3: a tag-10 Wm
responder in `sdk/arm7/`, or a port transport with the Wm and DWC slots. The
port must fix which, and add a socket layer if it is the second.

**E. An entity-create entry point for a mod.** A way to add an MMDL entity with
a chosen obj code and grid position, a per-frame position driver, and a
mod-owned network id. This depends on A and B.

**F. A shared clock and random source, if lockstep is chosen.** The evidence is
the global MT19937 (`GF_STANDARD_Rand` 0x0203F0A8) and the move-code random
lookups (`FLDMMDL_MOVECODE_GetRandomTableEntry` 0x0219B0F0).

Everything else is the co-op project's work, not the port's.

## Still to build

1. **Compile the field overlays into the boot.** ov010 and ov021 and their
   ARM9 closure join the source list, so `FIELDMAP_*`, `FLDMMDL_*`,
   `FIELD_PLAYER_*` and `FIELD_CAMERA_*` are callable. Evidence: the eidos
   build list has no such row and `README.md` leaves Milestone 7 unchecked.
2. **Add the field addresses to the declared set and wire the slots.** The
   rows of section 5B join `mod/declared_set.txt`, and the slot of
   `docs/mod-interface.md`, section 2, is generated for each. Evidence: the
   artifact exists but no generator reads it (still-to-build item 1 of
   `docs/mod-interface.md`).
3. **Expose the field state accessors as a mod API.** The field map, the player
   position, the camera, the entity list, the warps and the party get named
   accessors and a read and write contract. Evidence: the accessors exist in
   aletheia (section 1) and `docs/mod-interface.md`, section 4, names the party
   only, with no field subset.
4. **Fix the transport seam.** The port chooses the tag-10 Wm responder or the
   port transport with the Wm and DWC slots, and builds the socket layer the
   second needs. Evidence: `sdk/arm7/` has no Wm or DWC responder and the tree
   has no sockets (`docs/modding.md`, "Networking seam: missing").
5. **Provide the entity-create entry point.** A mod can add an MMDL entity with
   a chosen obj code and grid position, drive its position each frame, and name
   it on the wire. Evidence: `FLDMMDL_AddMmdl` 0x0216CE60 and
   `FLDMMDL_AddMmdlCore` 0x0216CDC4 create one, but no slot or accessor reaches
   them and the entity carries only the game's obj id.
6. **Settle the entity pool headroom.** The pool size is runtime data from
   `FLDMMDL_CreateSys` 0x0216CC58 through `FLDMMDL_SysGetMmdlMax` 0x0216D4F0,
   and whether a free slot exists for a second player is not fixed by the
   matched code read here. Evidence: `FLDMMDL_SysGetFreeMmdl` 0x0216DEEC
   returns 0 when the pool is full.
7. **Settle the second player's model.** A second instance of the player's obj
   code needs its resources loaded. Evidence: the player header is
   `gFieldPlayerAvatarHeaderTemplate` 0x021D89A0 and the resource system is
   `FLDMMDL_LoadObjCodeParam` and the FLDMMDL_BLACT functions (ov010); whether
   two player obj codes can be resident at once is not settled by the evidence
   read.
8. **Fix the authority and determinism model.** The port fixes which instance
   owns an entity, how two conflicting writes are ordered, and whether the
   simulation is event-agreement or lockstep. Evidence: the game does not
   arbitrate a write (`docs/mod-interface.md`, section 4) and
   `docs/modding.md`, still-to-build items 3 and 5, leave both open.
9. **Provide a shared random source if lockstep is chosen.** Every global RNG
   call in the field simulation routes through one source. Evidence:
   `GF_STANDARD_Rand` 0x0203F0A8 and `FLDMMDL_MOVECODE_GetRandomTableEntry`
   0x0219B0F0 both draw from the process-global generator.

## Related documents

- `docs/modding.md`: what the four example mods need, what exists today, and
  the networking and determinism gaps.
- `docs/mod-interface.md`: the declared set, the slot, the event and state API,
  and the trust model.
- `mod/declared_set.txt`: the hookable set, the artifact the generators read.
- `docs/overlays.md`: the per-overlay namespaces, the trampolines and the
  loader a run-time override attaches to.
- `README.md`: the engine, the SDK layout, and the milestones that carry the
  field code.
