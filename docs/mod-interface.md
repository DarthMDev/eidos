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

The declared set is derived from aletheia's own vocabulary: the `function:`
and `address:` header of each matched source under `src/matched/`, and the
SDK symbol table `layout/symbols_sdk.txt`. The table lists the addresses the
four example mods need. Nuzlocke abbreviates to N, Soullink to S, Nuzlocke
plus Soullink to NS, Multiplayer to MP. A mod in the S or NS column needs
every N address too, because both are Nuzlocke supersets (`docs/modding.md`,
"What the four example mods need").

| Address | Function | Region | Mods |
|---|---|---|---|
| 0x0216E7C8 | EVENT_BATTLE_CreateScriptedBattle | ov010 | N S NS |
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
which `tools/bootlink.py` measures at about 5,700 boot translation units before
the overlays.

The table above is the prose account. The set is published as a machine
readable artifact, `mod/declared_set.txt`, so the generators have one source
of truth to read rather than a table in a document. It has one row per
address with four tab separated columns: the address (the even function
start), the aletheia function name, the region (`arm9` or the overlay id),
and the example mods that need it (`nuzlocke`, `soullink`,
`nuzlocke-soullink`, `multiplayer`). `#` starts a comment. The artifact is
what `tools/bootlink.py` and `tools/ovlink.py` will read: the `arm9` rows go
to the boot link and the overlay rows to the overlay links, each building the
slot of section 2 (still-to-build item 1). The table is kept in step with the
artifact by hand until a tool generates the table.

Every row was checked against aletheia's per-packet manifests under
`layout/matched/` (`pkt_*.json`), at revision
`80f06a261e8731f2db94c1a66627bb351ad47673`, not copied from this table. All
193 rows match a manifest function name and region. One address is shared by
two overlays that occupy the same RAM window: `0x021C5200` is
`BTL_SERVER_FLOW_ProcFaintRewards` in ov093 and
`ZUKAN_DETAIL_FORM_SignalAnimDone` in ov208. The declared row is the ov093
function; the overlay id disambiguates and the bare address does not.

The table above is the 44 rows the four example mods need. The artifact has
grown to 193 rows, because the event catalogue of
`docs/mod-events.md`, section 1, was rebuilt from the game's own command
tables: the field script VM's 609-entry table at `d_0217062C` and the battle
effect VM's 78-entry table at `0x02209E88`. 136 rows came from that rebuild,
and 13 more came from the merge with `progress/EVENT_SURVEY.md`: 3 events and
10 override points (`docs/mod-events.md`, section 0). A row with an empty
fourth column is one no example mod names; it covers a concept the port must be
able to reach. A state accessor that a mod reads and writes through an
accessor, and that no event or override binds, is not declared here;
`docs/mod-events.md`, section 4, lists it.

The evidence for each original row is the mod's need in `docs/modding.md`; the
evidence against a row is only its maintenance cost.

## 2. How a slot is generated and reached

Four mechanisms were considered.

**A slot for every cross-unit call.** Uniform, any address overridable, one
indirect call per site. This needs `tools/bootlink.py` to rename every
external callee to a slot and `tools/ovlink.py` to do the same inside each
overlay. Every call in the corpus becomes indirect, and the generator carries
a slot for every external name. It is the widest reach and the largest cost,
and it makes every address a maintenance item on the next aletheia wave. Not
chosen.

**A slot for the declared set only.** The set is section 1. Only the declared
addresses change, so the cost is proportional to the set and the rest of the
link is untouched. The machinery already exists: both generators resolve a
name to its address (`tools/bootlink.py`, `defined_addr`; `tools/ovlink.py`,
`Aletheia`) and both already write a per-unit header. The header is a forced
include for an overlay (`-include`/`/FI`, `cmake/overlays.cmake`) and an
`#include` by a generated wrapper for the boot (`cmake/boot.cmake`, "one
wrapper per translation unit"); the earlier text called both forced, which the
boot's wrapper does not. Chosen.

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
seam. The rest of this section is the plan: what the generator emits per row,
what the slot is, how a mod names it, what changes in each file, and how the
seam is verified without a full build.

### What the generator emits per declared address

`mod/declared_set.txt` is the input. A reader splits a line on tabs, drops a
`#` comment, and takes four columns: the address (the even function start), the
aletheia function name, the region (`arm9` or an overlay id such as `ov093`),
and the mods that need it. The generator builds two things per row.

For an `arm9` row, the generator adds one line to the per-unit header of every
unit that references the address and does not define it (`tools/bootlink.py`,
the `unit.renames` loop in `generate`, line 897):

    #define <spelling> bw_slot_arm9_<addr>

The call site then compiles to a call to the slot. For `0x02007150`
(`SAVE_CONTROL_StartSave`, arm9) the caller is
`src/matched/arm9/func_02012DAC.c` (`GAME_DATA_SaveStart`), which calls
`SAVE_CONTROL_StartSave` at line 21. Today the caller's spelling matches the
definition in `src/matched/arm9/func_02007150.c`, so the generator emits no
line and the call binds by name. Before:

    /* build32/boot/gen/include/src_matched_arm9_func_02012DAC.h: no line */
    t3 = SAVE_CONTROL_StartSave((int)*(int *)&...);

After, the header gains:

    #define SAVE_CONTROL_StartSave bw_slot_arm9_02007150

and the call compiles to:

    t3 = bw_slot_arm9_02007150((int)*(int *)&...);

The defining unit `src/matched/arm9/func_02007150.c` defines the name and gets
no `#define`, so its own body still calls and defines
`SAVE_CONTROL_StartSave`.

For an overlay row, `tools/ovlink.py` already renames every external spelling
to `ovNNN_<name>` in the overlay's header (`st.renames`, written by
`write_overlay_header`; `docs/overlays.md`, "Names: a namespace per overlay").
A declared row sends a non-owning unit to the slot instead:

    #define <spelling> bw_slot_<region>_<addr>

For `0x021C5200` (`BTL_SERVER_FLOW_ProcFaintRewards`, ov093) a caller inside
ov093 that is not the defining unit gets:

    #define BTL_SERVER_FLOW_ProcFaintRewards bw_slot_ov093_021C5200

and its call compiles to `bw_slot_ov093_021C5200(...)`. The defining unit keeps
`ov093_BTL_SERVER_FLOW_ProcFaintRewards`. A caller outside the window does not
use this rename: it goes through a trampoline, which `write_dispatch` makes
consult the registry before it forwards.

A row whose definition the link does not carry has no slot. The generator
reports the row by name and skips it, the way `tools/bootlink.py` already
reports an unbound data name. The boot link defines an arm9 name through a
matched unit, a shim (`sdk/overlay.c` stands in for `FS_LoadOverlayImage` and
`FS_StartOverlay`), or an ARM9 stub; a name nothing defines and nothing calls
is absent. The overlay link defines an overlay row through the owning unit or
its stub. `progress/SEAM_COST.md` section 7 records the fixed coverage this
produces.

### What the slot is

The slot is a generated function, not a table entry. It is emitted into the
generated link C of its region: `boot_link.c` for an arm9 row
(`tools/bootlink.py`, `write_link_c`), and `ovNNN_link.c` for an overlay row
(`tools/ovlink.py`, `write_overlay_c`).

The generator takes the definition's prototype from the defining unit's `heads`
and normalises it with `tools/ovlink.py`'s `signature`, the call
`tools/bootlink.py`'s `write_thunk` already makes (line 364). A prototype that
does not normalise is refused at generation with the address and the name, the
rule the trampoline states with `BW_OVL_WHY_SIGNATURE` (`sdk/overlay_image.c`,
`bw_ovl_dispatch_fail`). For `0x02007150` the normalised signature is
`int (int)`. The emitted slot:

    int bw_slot_arm9_02007150(int a0)
    {
        void *h = bw_mod_slot_lookup(BW_MOD_REGION_ARM9, 0x02007150u);
        if (h != NULL && bw_hook_enabled("SAVE_CONTROL_StartSave"))
            return ((int (*)(int))h)(a0);
        return SAVE_CONTROL_StartSave(a0);
    }

Its initial value, with no mod installed or with the hook switched off, is the
original function: the last line calls the real name. The original stays
reachable because the defining unit is never renamed. The generated slot C
calls the real name, and the port publishes that name's address to the registry
so a `wrap` override can call it. The arm9 publication goes in
`bw_boot_link_init` (`tools/bootlink.py`, `write_link_c`; the start-up caller is
`demo/eidos_boot.c`). The overlay publication goes in `provider_load`
(`sdk/overlay_image.c`) from a per-overlay table in `ovNNN_link.c`. A wrap
calls `bw_mod_slot_original(region, addr)` and casts the result to the
prototype. This answers the first "still to build" item in
`docs/mod-overrides.md` ("Wrapping, and how the original stays callable").

The on/off switch reuses `sdk/mod_hooks.h`. The slot asks
`bw_hook_enabled("<name>")`, the row's function name, and with the hook off the
slot calls the original. This is exactly the rule `docs/hooks.md` step 4 gives
a compiled hook, so a slot and a compiled hook answer one mechanism, not two.

### The (region, address) key

The key is the pair `(region, address)`, never the address alone. Two overlays
share one RAM window: `0x021C5200` is `BTL_SERVER_FLOW_ProcFaintRewards` in
ov093 and `ZUKAN_DETAIL_FORM_SignalAnimDone` in ov208 (section 1;
`mod/declared_set.txt`). The overlay id disambiguates; the bare address does
not.

A mod names a hook by the pair, spelled `<region>:<address>` with the region
spelling `mod/declared_set.txt` uses: `arm9:0x02007150`, `ov093:0x021C5200`.
The `override` grammar of section 3 carries the region token for this reason,
and the reader that consumes it is section 3's work (still-to-build item 6). A
bare address that is a declared overlay address is refused. The slot name
encodes the pair too, `bw_slot_<region>_<addr>`, so the generated code and the
manifest cannot disagree about which overlay a slot is for.

At call time the generated slot embeds the region constant and the address and
calls `bw_mod_slot_lookup(region, addr)`. The registry is a small fixed table
in a new `sdk/` file with one entry per installed hook: `{ region, address,
override, original, owner }`. A later install of one pair wins and the earlier
is reported shadowed, the rule `docs/mod-overrides.md` ("Two mods on one
address") fixes.

`sdk/overlay_image.c`'s `bw_ovl_code_entry` consults the same lookup, so a code
pointer in data resolves to the override for a declared `(region, address)`.
The typed check reuses the trampoline normaliser that panics with
`BW_OVL_WHY_SIGNATURE` on a prototype mismatch (`tools/ovlink.py`, `signature`;
`sdk/overlay_image.c`, `bw_ovl_dispatch_fail`).

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

### What changes in each file

| File | Change |
|---|---|
| `tools/bootlink.py` | in `generate`, read `mod/declared_set.txt` and, for each arm9 row, set `unit.renames[spelling] = "bw_slot_arm9_<addr>"` for a non-owning unit (the loop at line 897); in `write_link_c`, emit the slot and the originals publication in `bw_boot_link_init` |
| `tools/ovlink.py` | in `_resolve`, read the same file and set `st.renames[spelling] = "bw_slot_<region>_<addr>"` for a non-owning unit; in `write_overlay_c`, emit the slot and the per-overlay originals table; in `write_dispatch`, make each declared trampoline call `bw_mod_slot_lookup` before it forwards |
| `sdk/mod_slot.h`, `sdk/mod_slot.c` (new) | the registry and its region constants: `bw_mod_slot_lookup`, `bw_mod_slot_original`, `bw_mod_slot_install`, `bw_mod_slot_publish` |
| `sdk/overlay_image.c` | `provider_load` publishes the overlay's declared originals; `bw_ovl_code_entry` consults `bw_mod_slot_lookup` before its table lookup |
| `CMakeLists.txt` | add `sdk/mod_slot.c` to `BW_SDK_SOURCES`, beside `sdk/mod_hooks.c` (line 330) |
| `sdk/mod_hooks.h` | unchanged; the slot reuses `bw_hook_enabled` |
| `sdk/aliases.c` | unchanged for a declared address; the generator's rename is the seam |
| `sdk/overlay.h` | unchanged; the registry is a module `provider_load` calls, not a provider field |

### The verification path

The generators are standalone tools, so the seam is proved without a full
build. The plan the configure step wrote is read, and the generated files go to
a scratch out-dir. The boot:

    python tools/bootlink.py generate --aletheia-root ../bw_decomp --plan-dir build32/boot --shim-sources build32/boot/shim_sources_gen.txt --port-sources build32/boot/port_sources.txt --armdata-header build32/armdata/bw_arm9_data.h --define EIDOS_HEAP_MATCHED=0 --out-dir %TEMP%\seam_check\boot

Look for:

- `%TEMP%\seam_check\boot\boot_link.c` holds one `bw_slot_arm9_02007150`, and
  one `bw_slot_arm9_` per arm9 row the boot link carries (35 rows; section 1;
  `mod/declared_set.txt`):
  `findstr /C:"bw_slot_arm9_" %TEMP%\seam_check\boot\boot_link.c`. The report
  names any row the link does not define and the generator skipped.
- `%TEMP%\seam_check\boot\include\src_matched_arm9_func_02012DAC.h` holds
  `#define SAVE_CONTROL_StartSave bw_slot_arm9_02007150`.
- `%TEMP%\seam_check\boot\include\src_matched_arm9_func_02007150.h` does not
  hold that line.

The overlays:

    python tools/ovlink.py --aletheia-root ../bw_decomp --overlays "9;10;88" --link-sources build32/ovl/boot/link_sources.txt --set-name check --out-dir %TEMP%\seam_check\ovl --summary

Look for:

- `%TEMP%\seam_check\ovl\ov010_link.c` holds one `bw_slot_ov010_` per declared
  ov010 row.
- `%TEMP%\seam_check\ovl\include\ov010.h` holds the renamed spelling.
- `%TEMP%\seam_check\ovl\ovl_dispatch.c` holds a `bw_mod_slot_lookup` in each
  trampoline whose address is declared.

A declared row whose overlay is not in the run (`ov093`, `ov020`) yields no
slot in this output. That is the fixed coverage `progress/SEAM_COST.md`
section 7 records, not a failure. Both commands write only their out-dirs and
read only aletheia's metadata and the port's sources, so neither needs a
compiler or a game byte.

### What stays out

The plan is the seam only. Five pieces are later, and none blocks it.

- The loader (still-to-build item 4). A dropped-in mod is a native shared
  library loaded at run time, and no loader exists (the only `dlopen` calls are
  the extraction tool's D-Bus portal, `install/picker.c`; the only
  `LoadLibrary` call is a diagnostic, `tools/lp64/lowmem_probe.c`). The seam
  does not need it: the slot and the registry are host code the build produces,
  and a mod's object installs into the registry by the same call a test would.
- The ABI and the interface version policy (still-to-build items 4 and 5). Both
  are checks on a loaded mod and its manifest, not on the call path.
- The events and the typed wrappers (section 4; still-to-build items 7, 8, 9).
  An event name is a binding on top of a slot; the slot fires with or without
  one.
- The per-mod state (still-to-build item 10). It is the `state` namespace of
  section 3, not the call path.
- The `.bw` container (still-to-build item 13). It is a packaging question for
  the file layer, not the seam.

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
consumer is three-sided: `sdk/vfs.c` reads the data keys, the code loader and
override registry read the code keys, and the rule interpreter reads the
`rule` key.

| Key | Value | Default | Meaning |
|---|---|---|---|
| `name` | text | the directory name | display name |
| `order` | signed integer, -1000000 to 1000000 | 0 | load order key; a later mod wins |
| `api` | integer | 1 | the interface version the mod is built against |
| `code` | relative path | none | the mod's native library for this host |
| `state` | identifier | derived from `name` | the per-mod state namespace |
| `override` | a grammar line, repeatable | none | one (region, address) override |
| `rule` | a grammar line, repeatable | none | one rule bound to one event |
| `subscribe` | an event name, repeatable | none | a bound event, declarative only |
| `settings` | relative path | none | the mod's settings declaration (still-to-build item 11) |

`order` uses the existing rule: a value that is empty, not a number, or out of
range is reported on stderr and ignored, and the default stands, never a
guessed value (`sdk/vfs.c`, `read_manifest`; `docs/mods.md`, "The per-mod
manifest").

The `override` value grammar is:

```
<region>:<address> <mode> <symbol> [sig="<prototype>"] [switch=<name>]
```

- `<region>:<address>` is the pair `(region, address)` of section 2. `<region>`
  is `arm9` or an overlay id, the spelling `mod/declared_set.txt` uses, and
  `<address>` is a `0x` prefixed DS address. Both the even start and the value
  a code pointer holds (the Thumb low bit) are accepted (still-to-build
  item 2). The bare address is not a key: two overlays share one RAM window,
  so `0x021C5200` alone names two functions (section 1).
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
layer already has: mods load in ascending `order`, and for one `(region,
address)` pair the later mod wins and the earlier is reported as shadowed
(`docs/mod-overrides.md`, "Two mods on one address"). The manifest is where
that report gets the mod name and the pair.

A complete example manifest:

```
# Nuzlocke plus Soullink
name  = Nuzlocke Soullink
order = 20
api   = 1
code  = nuzlocke_soullink.dll
state = nuzlocke-soullink

# One line per declared pair.  The region:address pair is the key; the bare
# address is not, because two overlays share one RAM window (section 2).
override = ov010:0x0216E818 wrap    sl_encounter_start sig="int f(int,int,int,int,int,int,int)" switch=encounter
override = ov093:0x021CBA40 replace sl_try_ball        switch=catch
override = ov093:0x021C5200 wrap    sl_faint           switch=faint
override = arm9:0x02007150  wrap    sl_save_begin      switch=save

# Declarative; the mod binds its handlers in code (still-to-build item 7).
subscribe = encounter.start
subscribe = catch.attempt
subscribe = battle.faint
subscribe = save.begin

# A rule, code in the small language of "The rule surface" (section 4), bound
# to one event.  This one subtracts 1000 from the player money on a faint.
rule = battle.faint rules/faint_penalty.rule
```

The `rule` value grammar is:

```
<event> <path>
```

- `<event>` is one of the event names in section 4.
- `<path>` is a relative path to a rule source in the mod, read the way every
  other mod file is.

A rule is code in a small predetermined language, not data: a rule file is a
sequence of statements over the named state fields of section 4 and the event
payload. `sub money 1000` is the worked example. The port runs a mod's rules
at the event slot that fires them, the point section 4 fixes. The exact
grammar is still-to-build item 14.

The state namespace names one file or folder under the save directory. The
port hands the mod a path and the mod owns the bytes; the write discipline is
the existing one, whole and atomic (`sdk/save.h`, `bw_save_path`,
`bw_save_read`, `bw_save_write_atomic`; `docs/modding.md`, "Per-mod state
persistence"). The layout under the save directory is still-to-build
item 10.

A packed single-file mod is a player-facing thing: one `.bw` container with
the same tree and manifest inside. The current layer reads a directory root,
because `bw_data_path` joins a root and a relative path (`sdk/vfs.c`,
`bw_data_path`), so no `.bw` is read anywhere in the tree. The container, where
the port reads it, and what each of the two options costs are
`docs/mod-rules.md`, section 2, which owns the decision.

Neither is built, so the port must fix which one it is (still-to-build item
13).

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
| `encounter.start` | 0x0216E7C8 | a wild or scripted battle task is created | N S NS |
| `encounter.trainer` | 0x021AA5CC | a trainer battle is set up | N S NS |
| `battle.start` | 0x02013090 | the battle setup is created | N S NS MP |
| `catch.attempt` | 0x021CBA40 | a ball is thrown | N S NS |
| `catch.result` | the return of 0x021CBA40 | the throw returns | N S NS |
| `battle.faint` | 0x021C5200 | a faint is processed | N S NS |
| `heal.party` | 0x0215B5A8 | a script heals the party | N S NS |
| `whiteout.begin.battle` | 0x021D1960 | the player blacks out, the battle-screen half | N S NS |
| `whiteout.begin.field` | 0x02158A20 | the player blacks out, the field-recover half | N S NS |
| `route.change` | 0x021643E8 | the field loads a new zone | N S NS |
| `save.begin` | 0x02007150 | a save starts | N S NS MP |
| `save.done` | 0x02007168 | the save completes | N S NS MP |
| `save.load` | 0x02012C18 | a save loads | N S NS MP |
| `link.up` | 0x0215CD24 | the link becomes on | S NS MP |
| `link.down` | 0x0215CD24 | the link becomes off | S NS MP |
| `net.send` | the Wm and DWC send points | bytes leave the process | MP |
| `net.receive` | the Wm and DWC receive points | bytes arrive | MP |

These seventeen names are the catalogue of this section and stay unchanged.
`docs/mod-events.md`, section 1, extends it to 359 entries (160 events, 27
override points, 81 state entries and 91 data tables), derived from the game's
own command tables and from `progress/EVENT_SURVEY.md`, and binds each to a
declared address, an accessor or a data table. The names here are the subset
the four example mods name.

Exact semantics and the accepted writes:

- `encounter.start`. Fires once when `EVENT_BATTLE_CreateScriptedBattle` is
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
- `whiteout.begin.battle`, `whiteout.begin.field`. Fire when the party blacks
  out, the battle-screen half (0x021D1960) and the field-recover half
  (0x02158A20). The catalogue splits the one concept into two events because
  one event is one address (`docs/mod-events.md`, section 0). A block
  suppresses the return to the last heal point, which Nuzlocke uses to continue
  after a whiteout. See still-to-build item 8.
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
state write (`docs/modding.md`, still-to-build item 7).

### The rule surface

The C API above is one way to act on an event. The port also offers a
constrained surface over the same capabilities, so a mod that does not write
C can express a rule like "on a faint, subtract 1000 from the player money".
The manifest declares it under the `rule` key (section 3), and the port runs
it at the event slot.

A rule is code in a small predetermined language, not data. A rule file is a
sequence of statements over the named state fields below and the event
payload; `sub money 1000` is the worked example. The port runs the rules a
mod declared for an event at the slot that fires the event, in the same place
it runs the mod's C handlers, so a rule and a handler see the same typed
payload. The exact grammar is still-to-build item 14.

The game already carries a bytecode VM the port could use as the substrate
rather than write a second interpreter. `VM_Create` (0x0201121C),
`VM_Control` (0x02011298) and `VM_Delete` (0x02011264) are the VM core
(`src/matched/arm9/unit_020111EC.c`, `unit_0201126C.c`). The field script VM
drives it through `SCRIPT_SYS_CreateVm` (0x02158BFC) and `SCRIPT_SYS_Main`
(0x0215882C, `src/matched/ov/func_0215882C.c`), and the battle effect VM
through `BTLV_EFFVM_Create` (0x021F93F4, ov094). A rule compiled to that VM
would run on the game's own command set; the alternative is a small
interpreter in the port. Whether the port reuses the VM or writes its own is
still-to-build item 14.

### State fields a rule may name

A rule names state fields. A field is a named slot the port reads and writes;
the port resolves it to the game's bytes. The worked example is the player
money.

| Field | Where | Size | Read | Write |
|---|---|---|---|---|
| `money` | save block id 0x34, offset +0x00 | u32 | `BEACON_GetPoints` 0x0200C274 | `BEACON_AddPoints` 0x0200C278, `BEACON_SubPoints` 0x0200C29C |

The money is the save block with id 0x34. `SAVE_CONTROL_GetBlock`
(0x020071CC) returns `BACKUP_SYSTEM_GetRecordWork(ctrl->unk10, id)` plus a
base (`src/matched/arm9/unit_020071CC.c`,
`src/matched/arm9/func_020319A4.c`), so the block is at a run-time address,
not a fixed DS address. The port reaches it through `GAME_DATA_GetMiscSave`
(0x02012F08), which is `BEACON_GetBlock` (`SAVE_CONTROL_GetBlock(sys,
0x34)`). The accessor family is `BEACON_*` (`src/matched/arm9/unit_0200C0E8.c`):
`BEACON_GetPoints` returns `p->unk00`, `BEACON_AddPoints` and
`BEACON_SubPoints` change it, and both cap it at 9,999,999. The game's own
money displays read the same field: the bag (`ITEMMENU_PrintMoney`
0x021BAFC4), the field money window (`SCRCMD_PROC_FLD_PrintMoneyWindow`
0x021B40D4), and the shop (`EVENT_COMM_COMMON_ItemPurchaseEventMain`
0x0218261C), which compares the price against `BEACON_GetPoints` and
subtracts with `BEACON_SubPoints`. A mod reads the field through
`BEACON_GetPoints` and writes it through `BEACON_AddPoints` or
`BEACON_SubPoints`; a rule names `money` and the port makes the same call.
The money accessors are not in the declared set (section 1): they are state
accessors, not override points, and a mod reaches them the way section 4
lets a mod reach any matched accessor.

The other state a mod plausibly needs, in the same form:

| Field | Address | Where | Read | Write |
|---|---|---|---|---|
| game data object | 0x020120F4 | `GAMESYSTEM_GetGameData` | same | - |
| party | 0x02012934 | `GAME_DATA_GetMyParty` | `POKEPARTY_GetMember` 0x0201AB28, `POKEPARTY_GetCount` 0x0201AA18 | `POKEPARTY_Add` 0x0201A98C |
| box | 0x0201293C | `GAME_DATA_GetBoxManager` | `POKE_TOOL_GetBoxValueRaw` 0x02018E34 | `POKE_TOOL_SetBoxValueRaw` 0x02019388 |
| status | 0x02012944 | `GAME_DATA_GetMyStatus`, a 0x20-byte record (`MYSTATUS_GetSize` 0x02008468) | the record | the record |
| map | 0x02012AC8, 0x0201210C | `GAME_DATA_GetPlayerZoneId`, `GAMESYSTEM_GetFieldMap` | same | `GAME_DATA_SetPlayerZoneId` 0x02012AC4 |
| save flags | 0x0201296C, 0x02012FE4 | `GAME_DATA_GetEventWork`, `GAME_DATA_GetEventWorkFromSave` (save block 0x2d) | `GAME_DATA_CheckSaveFlag` 0x02012BF8 | `SAVE_EVENT_Write` 0x0202F190 |
| a party mon field | 0x02017E1C, 0x02017E40 | `POKE_TOOL_GetPartyValue`, `POKE_TOOL_SetPartyValue` | by field id | by field id |

The party and box records are the 0xDC-byte record
`src/matched/arm9/sub_02018E34.c` maps: a seed word at +0x00, three flag bits
and a checksum at +0x04, an 0x80-byte masked block area at +0x08, and a
0x54-byte masked party area at +0x88. `POKE_TOOL_GetPartyValue` takes a field
id: 0x05 is species, 0x06 the held item, 0x08 experience, and 0x9E the level
(the party record's +0x8C byte; the box value for 0x9E is derived from
experience). The party area's other ids, 0x9D and 0x9F to 0xA6, map to record
offsets +0x88 to +0x9A, but aletheia does not name which is HP or a stat, so
that mapping is still-to-build item 15.

The save flags and the event work are a save block, id 0x2d
(`GAME_DATA_GetEventWorkFromSave`), reached the same way as the money block.
The dex is a separate block (`ZUKAN_SAVE_GetFromGameData` 0x0200CA1C,
`ZUKAN_SAVE_IsCaught` 0x0200CED4).

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

1. **Read the declared set in the generators.** The set is fixed and published
   at `mod/declared_set.txt` (section 1). The work is to make
   `tools/bootlink.py` and `tools/ovlink.py` read it and emit the slot of
   section 2 per row, so the artifact drives the indirection rather than a
   hand-kept list. The evidence for each row is the mod's need in
   `docs/modding.md`; the cost of a row is one rename site and one maintenance
   item per aletheia wave (`docs/hooks.md`, "Limits").
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
8. **Bind the two `whiteout.begin` events.** The catalogue carries two events,
   `whiteout.begin.battle` (0x021D1960, ov093) and `whiteout.begin.field`
   (0x02158A20, ov010), because one event is one address (`docs/mod-events.md`,
   section 0). The evidence is that the two halves are in different modules
   (`ov093`, `ov010`). The work is to fix how the port binds the two names.
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
13. **Build the packed form, and fix where it is read.** A `.bw` container is
    a player-facing thing and no reader exists. The port must fix whether it
    reads the container directly (a parser in the file layer, which already
    serves a merged NARC from memory through a handle) or a launcher unpacks
    it to a directory before the port starts (`install/setup.c`,
    `bw_setup_install`). Section 3 carries the evidence on each side.
14. **Build the rule surface.** The `rule` key (section 3) and the entry
    point (section 4) are fixed, and the language is not. `docs/mod-rules.md`,
    section 1, fixes the substrate: a small interpreter in the port, not the
    game's bytecode VM. The work is to fix the rule grammar and how a rule
    names the state fields of section 4.
15. **Name the party-area fields.** The party record's ids 0x9D and 0x9F to
    0xA6 map to record offsets +0x88 to +0x9A, but aletheia does not name
    which is HP or a stat (`src/matched/arm9/sub_02018E34.c`,
    `sub_02018DB4.c`). The port must fix the id-to-name mapping before a rule
    can name them.

## Related documents

- `docs/modding.md`: what the four mods need and what exists today.
- `docs/mod-overrides.md`: the function override keyed on a DS address.
- `docs/mods.md`: the data and asset surface that exists today.
- `docs/hooks.md`: the compiled-in hooks and their limits.
- `docs/overlays.md`: per-overlay namespaces, trampolines and the loader.
- `docs/armdata.md`: how DS addresses become host pointers.
- `sdk/save.h`: the state write discipline.
