# Mod interface

This document is the interface spec for eidos mods: the contract a mod is
written against. It fixes the parts that are expensive to change once mods
exist. `docs/modding.md` says what the four example mods need,
`docs/mod-overrides.md` designs a function override keyed on a DS address,
and `docs/mods.md` is the guide to the data surface that already exists.
This document answers the questions those three leave open: which addresses
the port exposes, how a call reaches an override, what a manifest may
declare, what a mod observes, and what a mod is trusted with.

The reason this must be settled early is a property of the build. A DS call
site names an address; a host call site names a symbol. `tools/bootlink.py`
rewrites the spellings in each translation unit's forced-include header and
lets the linker bind them ("Names by address", its docstring). `docs/hooks.md`
states the same for overlays: "Calls inside the overlay bind by name". A
dropped-in mod therefore cannot intercept a direct call at run time. Only the
build can put an indirection at a call site, so the port must decide at build
time which addresses get one. That set is the interface.

Every claim below is attributed to a file that was read. Items the code does
not settle are marked as work still to build, with the evidence on each side.

## 1. The hookable set

The port exposes a declared set of DS addresses. For each address in the set
it generates an indirection, a slot (section 2). An address outside the set
is called directly, as the build calls it today, and no mod can reach it.

The candidate set is derived from aletheia's own vocabulary: the `function:`
and `address:` header of each matched source under `src/matched/`, and the
SDK symbol table `layout/symbols_sdk.txt`. The table lists the candidates the
four example mods need. Nuzlocke abbreviates to N, Soullink to S, Nuzlocke
plus Soullink to NS, Multiplayer to MP. A mod in the S or NS column needs
every N address too, because both are Nuzlocke supersets (`docs/modding.md`,
"What the four example mods need").

| Address | Function | Region | Mods |
|---|---|---|---|
| 0x0216E818 | EVENT_BATTLE_CreateEncounterTask | ov010 | N S NS |
| 0x0216EAB0 | EVENT_BATTLE_CreateEncounterTaskWithMon | ov010 | N S NS |
| 0x021AA5CC | FIELD_ENCOUNT_SetupTrainerBattle | ov021 | N S NS |
| 0x02013090 | BTL_SETUP_Create | arm9 | N S NS MP |
| 0x0201347C | BTL_SETUP_SetupWild | arm9 | N S NS |
| 0x0216EE20 | EVENT_BATTLE_DispatchBattleKindNotify | ov010 | N S NS |
| 0x02188CE8 | FIELDMAP_GetEncount | ov021 | N |
| 0x021CBA40 | BTL_SERVER_FLOW_TryThrowBall | ov093 | N S NS |
| 0x021CBD48 | BTL_SERVER_FLOW_GetBallCatchRate | ov093 | N S NS |
| 0x021D5B68 | BTL_POKEPARAM_IsFainted | ov093 | N S NS |
| 0x021C5200 | BTL_SERVER_FLOW_ProcFaintRewards | ov093 | N S NS |
| 0x021D1960 | BTL_CLIENT_StepShowBlackoutMessages | ov093 | N S NS |
| 0x02158A20 | SCRIPT_SYS_RunRecoverScript | ov010 | N S NS |
| 0x02158AD4 | SCRIPT_SYS_RunFieldRecoverScript | ov010 | N S NS |
| 0x0215B5A8 | SCRCMD_PROC_HealParty | ov010 | N S NS |
| 0x02018198 | POKE_TOOL_RecoverAllPP | arm9 | N S NS |
| 0x021643E8 | FIELD_MAP_CHANGE_SetupZone | ov010 | N S NS |
| 0x021643AC | FIELD_MAP_CHANGE_ApplyZoneHeader | ov010 | N S NS |
| 0x021841C0 | EVENT_MAPCHANGE_ChangeFieldEvent | ov020 | N S NS |
| 0x02007150 | SAVE_CONTROL_StartSave | arm9 | N S NS MP |
| 0x02007168 | SAVE_CONTROL_MainSave | arm9 | N S NS MP |
| 0x02012C18 | GAME_DATA_LoadFromSave | arm9 | N S NS MP |
| 0x02012BF8 | GAME_DATA_CheckSaveFlag | arm9 | N S NS MP |
| 0x0215CD24 | SOCL_LinkIsOn | ov009 | S NS MP |
| 0x021666C8 | WCM_GetLinkLevel | ov009 | S NS MP |
| 0x02166710 | WCM_CalcLinkLevel | ov009 | S NS MP |
| 0x0208E010 | WM_Init | arm9 | MP |
| 0x0208F224 | WM_Enable | arm9 | MP |
| 0x0208F84C | WM_StartConnectEx | arm9 | MP |
| 0x0208FC4C | WM_StartMP | arm9 | MP |
| 0x0208FCB4 | WM_SetMPDataToPortEx | arm9 | MP |
| 0x0209004C | WM_StartDataSharing | arm9 | MP |
| 0x02090B18 | WM_GetSharedDataAddress | arm9 | MP |
| 0x020909C8 | WmDataSharingSendDataSet | arm9 | MP |
| 0x020908F8 | WmDataSharingReceiveData | arm9 | MP |
| 0x0208E5BC | WmReceiveFifo | arm9 | MP |
| 0x0208EB0C | WM_SetPortCallback | arm9 | MP |
| 0x02059FA8 | DWCi_Init | arm9 | MP |
| 0x02059F6C | DWC_InitForProduction | arm9 | MP |
| 0x0202627C | GAME_COMM_Update | arm9 | MP |
| 0x02026388 | GAME_COMM_CallHandler | arm9 | MP |
| 0x020263B0 | GAME_COMM_CallKindNotify | arm9 | MP |
| 0x02078EB0 | FS_LoadOverlayImage | arm9 | N S NS MP |
| 0x0207902C | FS_StartOverlay | arm9 | N S NS MP |

The addresses are the even function starts aletheia records in each source's
`address:` line. The overlay tables key on the value a code pointer holds,
which carries the Thumb low bit (`sdk/overlay_image.h`, `bw_ovl_code_entry`;
`tools/ovlink.py` builds `entries[a | (1 if a in st.thumb else 0)]`). The
manifest must state which form it uses and accept both. See still-to-build
item 2.

The last two rows are lifecycle, not gameplay. Every overlay load goes through
`FS_LoadOverlayImage` and every start through `FS_StartOverlay`
(`sdk/overlay.c`). An overlay's data is reset on every load, so a mod that
writes overlay data needs both a load hook and a start hook. Nuzlocke plus
Soullink and multiplayer need this for their shared state.

The multiplayer rows are the transport seam. `docs/modding.md` records that
`sdk/arm7/` has no Wm or DWC responder and no sockets, so the transport is
not built; the game's own Wm and DWC code is matched and is the raw material.
A multiplayer mod either drives these entry points or replaces them.

The cost of a set that is too small is that a mod cannot do what it needs,
which returns every capability to the build-time-only limit of `docs/hooks.md`.
The cost of a set that is too large is per-address maintenance: each declared
address is one more name the generator resolves, one more rename at every call
site, and one more thing aletheia's next cleanup wave can move under the
build (`docs/hooks.md`, "Limits": a rename or re-split must be rebased by
hand). A uniform set is that cost for every cross-unit call in the corpus,
which `tools/bootlink.py` measures at about 1400 boot translation units before
the overlays.

The table is a superset, and the port fixes which rows are declared
(still-to-build item 1). The evidence for each row is the mod's need in
`docs/modding.md`; the evidence against a row is only its maintenance cost.

## 2. How a slot is generated and reached

Four mechanisms were considered.

**A slot for every cross-unit call.** Uniform, any address overridable, one
indirect call per site. This needs `tools/bootlink.py` to rename every
external callee to a slot and `tools/ovlink.py` to do the same inside each
overlay. Every call in the corpus becomes indirect, and the generator carries
a slot for every external name. It is the widest reach and the largest cost,
and it makes every address a maintenance item on the next aletheia wave. Not
chosen.

**A slot for the declared set only.** The set is item 1. Only the declared
addresses change, so the cost is proportional to the set and the rest of the
link is untouched. The machinery already exists: both generators resolve a
name to its address (`tools/bootlink.py`, `defined_addr`; `tools/ovlink.py`,
`Aletheia`) and both already write a forced-include header per translation
unit. Chosen.

**Load-time patching of the loaded image.** This cannot work in the port.
`sdk/overlay_image.c` copies the DS bytes to a window as *data*
(`provider_load`, `bw_data_load`), and the executable runs host code, not the
DS code. There is no host code image to patch, and patching the copied bytes
would not change a host call. It would need an interpreter, which the port is
not. Not chosen.

**Dispatch-only.** Cross-overlay calls and ARM9 calls into a window already
go through trampolines, which ask `bw_ovl_resident_at` which overlay is
resident (`tools/ovlink.py`, `write_dispatch`; `sdk/overlay_image.c`,
`bw_ovl_resident_at`). Code pointers in data go through `bw_ovl_code_entry`.
The coverage is free but limited: a trampoline exists only for an address
named from outside its own window, and an overlay's internal calls bind by
name and never reach one (`docs/hooks.md`). Nuzlocke's battle functions live
inside ov093 and are called internally, so dispatch-only cannot reach them,
and ARM9-internal calls bind directly. Not chosen alone, reused where it
exists.

The chosen mechanism is the declared set, layered on the existing dispatch
seam. For a declared address:

1. The port holds the set as a generated list of DS addresses.
2. `tools/bootlink.py`, in `generate`, resolves the address to its defining
   name and unit (it already computes `func_at` and `defined_addr`) and emits
   a slot function `bw_slot_<addr>` in the generated link C (`boot_link.c` or
   a new generated file). The slot asks the registry and calls either the
   override or the real name. The generator adds
   `#define <spelling> bw_slot_<addr>` to the forced-include header of every
   unit that references the address and does not define it. The defining unit
   keeps the real name.
3. `tools/ovlink.py` does the same inside an overlay: a slot for the declared
   address, and the per-overlay rename already turns every external spelling
   into `ovNNN_<name>` (`docs/overlays.md`, "Names: a namespace per overlay"),
   so a reference outside the owning unit is redirected to the slot. For a
   caller outside the window, `write_dispatch` makes the trampoline ask the
   registry before it forwards.
4. A new registry keeps address to override function. `sdk/overlay_image.c`'s
   `bw_ovl_code_entry` consults it, so a code pointer in data resolves to the
   override. The typed check reuses the trampoline normaliser that already
   panics with `BW_OVL_WHY_SIGNATURE` on a prototype mismatch
   (`tools/ovlink.py`, `signature`; `sdk/overlay_image.c`,
   `bw_ovl_dispatch_fail`).
5. The on/off switch reuses `sdk/mod_hooks.h`. The slot asks
   `bw_hook_enabled("<name>")`; with the hook off the slot calls the original.
   This is exactly the rule `docs/hooks.md` step 4 gives a compiled hook, so a
   slot and a compiled hook answer one mechanism, not two.

The slot keeps the original callable under its real name, so a `wrap` mode
calls it. This answers the first "still to build" item in
`docs/mod-overrides.md` ("Wrapping, and how the original stays callable").

Two limits are inherent to the chosen mechanism and must be stated to modders.

- An intra-unit call is not redirected when the address is defined in the same
  unit. A textual `#define` cannot distinguish a reference from a definition,
  so the generator excludes the defining unit. A function that only calls
  itself within its own file, or an intra-unit call to another declared
  function, does not reach a slot for that unit. Whether the defining unit's
  calls are also redirected, by renaming the definition and letting the slot
  define the address, is still-to-build item 3.
- A function reached only through a code pointer is covered where
  `bw_ovl_code_entry` (overlay windows) or the ARM9 data relocation consults
  the registry (`docs/armdata.md`). A pointer built at run time and passed to
  the game is not covered. The port decides whether both relocation paths
  consult the registry (still-to-build item 12).

`sdk/aliases.c` is not a separate mechanism here. It reconciles two spellings
of one address for the linker (`sdk/aliases.c`, its header). For a declared
address the generator's per-unit renames do the same job, and the address, not
the name, is the durable key (`docs/mod-overrides.md`, "Override by DS
address, never by name").

What changes in each generator:

| Generator | Change |
|---|---|
| `tools/bootlink.py` | read the declared-address list; emit one slot per ARM9 address in the generated link C; add the reference rename to each non-owning unit's forced-include header |
| `tools/ovlink.py` | read the same list; emit a slot per overlay address; rename references in non-owning units; make `write_dispatch`'s trampolines consult the registry |
| `sdk/overlay_image.c` | the provider, `bw_ovl_resident_at` and `bw_ovl_code_entry` consult the registry |
| `sdk/overlay.h` | the provider or a registry field the provider reads |
| `sdk/mod_hooks.h` | unchanged; the slot reuses `bw_hook_enabled` |
| `sdk/aliases.c` | unchanged for a declared address; the generator's rename is the seam |

## 3. The manifest and pack schema

A mod is a directory laid out like the data root. Whole files and member
directories resolve by path, and a later mod wins (`README.md`, "The file
layer"; `docs/mods.md`). That directory layout *is* the pack schema; assets
and data need no manifest field.

The manifest is `<mod>/eidos_mod.txt`. `read_manifest` parses `#` comments,
`key = value` lines, ignores a line with no `=`, and ignores an unknown key
on purpose so a later version can add keys (`sdk/vfs.h`, "The per-mod
manifest"; `sdk/vfs.c`, `read_manifest`). Today it reads two keys, `name` and
`order`. The schema below adds keys within that forward-compatible rule. The
consumer is two-sided: `sdk/vfs.c` reads the data keys, and the code loader
and override registry read the code keys.

| Key | Value | Default | Meaning |
|---|---|---|---|
| `name` | text | the directory name | display name |
| `order` | signed integer, -1000000 to 1000000 | 0 | load order key; a later mod wins |
| `api` | integer | 1 | the interface version the mod is built against |
| `code` | relative path | none | the mod's native library for this host |
| `state` | identifier | derived from `name` | the per-mod state namespace |
| `override` | a grammar line, repeatable | none | one address override |
| `subscribe` | an event name, repeatable | none | a bound event, declarative only |
| `settings` | relative path | none | the mod's settings declaration (still-to-build item 11) |

`order` uses the existing rule: a value that is empty, not a number, or out of
range is reported on stderr and ignored, and the default stands, never a
guessed value (`sdk/vfs.c`, `read_manifest`; `docs/mods.md`, "The per-mod
manifest").

The `override` value grammar is:

```
<address> <mode> <symbol> [sig="<prototype>"] [switch=<name>]
```

- `<address>` is a `0x` prefixed DS address. Both the even start and the
  value a code pointer holds (the Thumb low bit) are accepted (still-to-build
  item 2).
- `<mode>` is `replace` or `wrap` (`docs/mod-overrides.md`, "What an override
  declares"). `wrap` calls the original, which the slot keeps callable.
- `<symbol>` is the C symbol the mod exports for this address.
- `sig=` names the prototype the caller expects, quoted. It is optional; when
  present the generator checks it with the same normaliser the trampolines
  use and refuses a mismatch rather than calling through the wrong type
  (`tools/ovlink.py`, `signature`; `docs/mod-overrides.md`, "What an override
  declares").
- `switch=` names the on/off key `bw_hook_enabled` answers. It defaults to a
  name derived from the address (`sdk/mod_hooks.h`).

A repeated `override` line is the mechanism for the conflict rule the data
layer already has: mods load in ascending `order`, and for one address the
later mod wins and the earlier is reported as shadowed (`docs/mod-overrides.md`,
"Two mods on one address"). The manifest is where that report gets the mod
name and the address.

A complete example manifest:

```
# Nuzlocke plus Soullink
name  = Nuzlocke Soullink
order = 20
api   = 1
code  = nuzlocke_soullink.dll
state = nuzlocke-soullink

# One line per address.  The address is the key tools/closure.py and the
# overlay tables already use.
override = 0x0216E818 wrap    sl_encounter_start sig="int f(int,int,int,int,int,int,int)" switch=encounter
override = 0x021CBA40 replace sl_try_ball        switch=catch
override = 0x021C5200 wrap    sl_faint           switch=faint
override = 0x02007150 wrap    sl_save_begin      switch=save

# Declarative; the mod binds its handlers in code (still-to-build item 7).
subscribe = encounter.start
subscribe = catch.attempt
subscribe = battle.faint
subscribe = save.begin
```

The state namespace names one file or folder under the save directory. The
port hands the mod a path and the mod owns the bytes; the write discipline is
the existing one, whole and atomic (`sdk/save.h`, `bw_save_path`,
`bw_save_read`, `bw_save_write_atomic`; `docs/modding.md`, "Per-mod state
persistence"). The layout under the save directory is still-to-build
item 10.

A packed single-file mod (one archive with the same tree and manifest inside)
is not a thing the current layer reads: `bw_data_path` joins a root and a
relative path, so the root is a directory (`sdk/vfs.c`, `bw_data_path`). A
packed form is still-to-build item 13.

## 4. The event and state API

An event is a semantic concept that maps onto one or more addresses in the
hookable set plus a typed wrapper. A slot at a declared address fires the
event when it is entered, calls the mod's handler with a fixed struct, and
then calls the original unless the handler blocks. The event name is the
permanent binding key: renaming one later breaks every mod bound to it. The
names below are chosen to describe the concept and to need no rename when an
address moves in aletheia.

| Event name | Source address(es) | When it fires | Mods |
|---|---|---|---|
| `encounter.start` | 0x0216E818 | a wild or scripted battle task is created | N S NS |
| `encounter.trainer` | 0x021AA5CC | a trainer battle is set up | N S NS |
| `battle.start` | 0x02013090 | the battle setup is created | N S NS MP |
| `catch.attempt` | 0x021CBA40 | a ball is thrown | N S NS |
| `catch.result` | the return of 0x021CBA40 | the throw returns | N S NS |
| `battle.faint` | 0x021C5200 | a faint is processed | N S NS |
| `heal.party` | 0x0215B5A8 | a script heals the party | N S NS |
| `whiteout.begin` | 0x021D1960, 0x02158A20 | the player blacks out | N S NS |
| `route.change` | 0x021643E8 | the field loads a new zone | N S NS |
| `save.begin` | 0x02007150 | a save starts | N S NS MP |
| `save.done` | 0x02007168 | the save completes | N S NS MP |
| `save.load` | 0x02012C18 | a save loads | N S NS MP |
| `link.up` | 0x0215CD24 | the link becomes on | S NS MP |
| `link.down` | 0x0215CD24 | the link becomes off | S NS MP |
| `net.send` | the Wm and DWC send points | bytes leave the process | MP |
| `net.receive` | the Wm and DWC receive points | bytes arrive | MP |

Exact semantics and the accepted writes:

- `encounter.start`. Fires once when `EVENT_BATTLE_CreateEncounterTask` is
  entered. The payload names the battle kind and the task. A handler may
  block, which suppresses the encounter; a `replace` changes the species the
  task builds. This is Nuzlocke's "first encounter of a route" point.
- `encounter.trainer`. Fires when a trainer battle is set up. The payload
  names the trainer. Read-only; the encounter already exists.
- `battle.start`. Fires when `BTL_SETUP_Create` is entered. The setup struct
  is not yet filled, so a `wrap` handler runs before or after the original
  and may adjust the fields the original derives.
- `catch.attempt`. Fires when a ball is thrown. The payload names the ball and
  the target. A block forbids the catch, which is Nuzlocke's rule against a
  second catch on a route. The game's own catch rate is
  `BTL_SERVER_FLOW_GetBallCatchRate` (0x021CBD48), declared in the set if a mod
  must change the roll.
- `catch.result`. Fires when the `catch.attempt` slot's original returns. The
  payload is the result. Read-only.
- `battle.faint`. Fires when a faint is processed. The payload names the
  fainted Pokemon. A block may stop the reward step. Soullink mirrors the
  faint to the peer here.
- `heal.party`. Fires when a script heals the party. A block forbids the
  heal, which is Nuzlocke's rule against healing a Pokemon the run has
  declared dead.
- `whiteout.begin`. Fires when the party blacks out. It spans two addresses,
  the battle-screen half (0x021D1960) and the field-recover half (0x02158A20),
  because one semantic concept is not one address (`docs/modding.md`, "Name
  and key the events"). A block suppresses the return to the last heal point,
  which Nuzlocke uses to continue after a whiteout. See still-to-build item 8.
- `route.change`. Fires when the field loads a new zone. The payload names the
  map. Read-only. Nuzlocke keys the used-route set on it.
- `save.begin`, `save.done`. Fire around a game save. A mod writes its state
  here, through the namespace of section 3.
- `save.load`. Fires when a save loads. A mod reads its state here.
- `link.up`, `link.down`. Fire when the link state changes, from the one
  predicate `SOCL_LinkIsOn`. Soullink and multiplayer use them to open and
  close the exchange.
- `net.send`, `net.receive`. Fire at the Wm and DWC entry points. These are
  the multiplayer transport seam and are not built (`docs/modding.md`,
  "Networking seam: missing"); the names are fixed here so a mod does not
  rebind when the transport lands.

A typed wrapper is one C struct per event and one handler signature. For
example:

```c
typedef struct BwEventEncounterStart {
    u32   ds_task;   /* the task the game created */
    u32   kind;      /* the battle kind */
    int   slot;      /* the call's first argument */
} BwEventEncounterStart;

typedef int (*BwEventHandler)(void *mod_state, const void *event);

enum { BW_EVENT_CONTINUE = 0, BW_EVENT_BLOCK = 1 };
```

The event pointer is valid only for the call. A handler returns
`BW_EVENT_CONTINUE` or `BW_EVENT_BLOCK`. The slot calls every handler bound to
the event, then calls the original unless a handler blocked. Blocks compose.
Whether a block needs any handler to block or every handler to allow is
still-to-build item 9.

Read and write. A mod may read the game state the port exposes: party and box
records, species, level and HP, the current map, and the save flags. It
reaches them through the DS address registry (`bw_ds_ptr`, `sdk/dsmem.h`) or
through the matched accessors the port defines (`POKE_TOOL_*`, `GAME_DATA_*`).
A mod may write the same bytes. The port does not arbitrate a write.

When a mod writes something the game then contradicts, the game's later write
wins. The port restores nothing and offers no transaction. Two cases are worth
stating because they change what a mod can rely on:

- An ARM9 save-block field written at `save.begin` persists until
  `bw_ds_reset` or a later game write. It is durable state.
- An overlay's data is reset on every load: `provider_load` copies the file to
  the window, binds the names and relocates the pointers on each
  `FS_LoadOverlayImage` (`sdk/overlay_image.c`, `provider_load`). A write into
  an overlay's data does not survive the next load of that overlay or of any
  overlay sharing its window.

So a change that must stick is made at the event the game derives the state
from, or through the override's return, not as a write the game recomputes. A
"block an action" is the return of a block on a slot that defines one, not a
state write (`docs/modding.md`, "What block an action means").

State and lifecycle. A mod's state lives under its `state` namespace, written
whole and atomically (`sdk/save.h`). The load and unload points are the code
entry points of the mod and the overlay lifecycle events of section 1. The
port must fix the exact callback that reads state before the game runs and
writes it at `save.begin`; that is still-to-build item 10.

## 5. The trust model

A mod is native C. It is loaded into the port's process and runs with the
port's full privilege. It can read and write any memory the process holds,
call any function, corrupt the game's state or the port's, hang, and crash the
process. A mod can defeat the on/off switch and the conflict report, because
both are in the same address space it runs in.

The port promises, and only promises:

- a deterministic load order, a later-wins rule, and a visible conflict report
  (`sdk/vfs.h`, "The mod list"; `docs/mod-overrides.md`, "Two mods on one
  address");
- the `bw_hook_enabled` switch over a slot, so one mod's override can be
  isolated while debugging (`sdk/mod_hooks.h`);
- that a build with no mods defines every address the way a build without the
  override machinery does, because matched files are never edited
  (`README.md`, portability rule 1; `docs/mod-overrides.md`, "The base is
  never edited").

The port does not promise isolation, sandboxing, bounded resource use,
validation of a mod's reads or writes, or recovery from a bad write. It does
not promise a stable ABI across `api` versions: a mismatch is refused, not
translated (still-to-build item 5). The correct model is that a mod is a
build of the port's own code by another author, trusted to the same degree.

## Still to build

1. **Fix the declared set.** The table in section 1 is a superset, and the
   port must fix which rows it declares. The evidence for each row is the
   mod's need in `docs/modding.md`; the evidence against a row is its cost,
   one rename site and one maintenance item per aletheia wave (`docs/hooks.md`,
   "Limits").
2. **Fix the address form.** The overlay tables key on the value a code
   pointer holds, with the Thumb bit (`sdk/overlay_image.h`,
   `bw_ovl_code_entry`); aletheia's `address:` line is the even start. The
   work is to accept both, or fix one and convert.
3. **Cover intra-unit calls in the defining unit.** The generator excludes the
   defining unit because a `#define` cannot separate a reference from a
   definition. The work is to rename the definition and let the slot define
   the address, which covers it at the cost of the file's own symbol.
4. **Build the code loader and settle the ABI.** `docs/modding.md` gap 1 and
   `docs/hooks.md` "Limits" require a dropped-in mod to run code with no
   rebuild, so the mod is a native shared library loaded at run time. No such
   loader exists in the tree (the only `dlopen` calls are the extraction
   tool's D-Bus portal, `install/picker.c`; the only `LoadLibrary` call is a
   diagnostic, `tools/lp64/lowmem_probe.c`). The loader is another
   workstream's work.
5. **Fix the interface version policy.** `api` is the only compatibility
   statement, and the port must fix what a mismatch does, refuse or warn.
6. **Fix the override grammar.** A repeated `override` key with the grammar
   above fits the existing line parser, and a section form is the alternative.
   The evidence is that `read_manifest` reads one key per line and ignores
   unknown keys (`sdk/vfs.c`).
7. **Fix where events are declared.** A mod can register a handler in its
   code, and a `subscribe` line lets the port report a conflict before the mod
   runs. The work is to fix which form the port reads.
8. **Fix how `whiteout.begin` spans two addresses.** One concept, two
   functions. The port must fix whether one event name binds both or two
   events exist. The evidence is that the two halves are in different modules
   (`ov093`, `ov010`).
9. **Fix block composition.** The port must fix whether any handler blocks or
   every handler must allow. The conservative rule is any-blocks, and the port
   reports both.
10. **Build the state path and lifecycle.** The save directory exists
    (`sdk/save.h`), but the per-mod layout and the read-before-run and
    write-at-save callbacks are not built (`docs/modding.md`, "Per-mod state
    persistence: partial").
11. **Build the settings surface.** A mod-facing settings surface is not built
    (`docs/modding.md`, "Settings and UI: partial"; `docs/settings-design.md`).
    The port must fix whether `settings` names a file, a row table, or
    nothing.
12. **Fix code-pointer coverage.** The port must fix whether
    `bw_ovl_code_entry` and the ARM9 data relocation both consult the
    registry, so a stored code pointer resolves to an override
    (`docs/armdata.md`; `docs/mod-overrides.md`, "still to build" item 5).
13. **Build a packed single-file mod.** The current layer reads a directory
    root (`sdk/vfs.c`, `bw_data_path`). Whether a mod may also be one archive
    with the tree and manifest inside needs a reader that does not exist.

## Related documents

- `docs/modding.md`: what the four mods need and what exists today.
- `docs/mod-overrides.md`: the function override keyed on a DS address.
- `docs/mods.md`: the data and asset surface that exists today.
- `docs/hooks.md`: the compiled-in hooks and their limits.
- `docs/overlays.md`: per-overlay namespaces, trampolines and the loader.
- `docs/armdata.md`: how DS addresses become host pointers.
- `sdk/save.h`: the state write discipline.
