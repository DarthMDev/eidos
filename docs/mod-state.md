# Mod state

This document owns a mod's own saved data: where it lives, when the port reads
and writes it, how it stays in step with the game's save, and how the manifest
becomes registrations at load. `docs/mod-interface.md`, section 3, gives the
manifest keys and `docs/mod-abi.md` the calls a mod makes.

## 1. The state namespace

A mod's own state is one folder per mod, named by the manifest's `state`
namespace, under the save directory. The folder holds one port-owned state
file per game save, so the mod's data always belongs to the save it was
written with.
The port owns the file's header; the mod owns the bytes after it. The game's
own state is the cartridge's backup chip (`sdk/save.h`, its header names
`sdk/arm7/fs.c` and `pokeblack.sav`), and the port's own files are the firmware
flash and the user settings file (`sdk/arm7/nvram.c`, `sdk/firmware.c`;
`sdk/save.h`, its header). All of them share the one save folder and the one
write discipline (`sdk/save.h`). The `mods/` subfolder and the
per-mod folder keep a mod's state apart from the game's file, apart from the
other port files, and apart from another mod's state.

### One file per mod, not a shared file

The write discipline is a whole-file atomic write: `bw_save_write_atomic`
writes `<path>.tmp`, syncs it, and replaces the path (`sdk/save.c`;
`sdk/save.h`). The unit of that discipline is one file, so one file
per mod matches the mechanism the port already has. `bw_save_write_atomic`
also creates the folder part of the path with `parent_of` and
`bw_plat_mkdirs` (`sdk/save.c`), so a folder per mod needs
no new folder call.

A shared file was the alternative: one file with one region per mod, keyed by
an offset table. It is not chosen. A region whose size changes moves every
later region, so a mod's format change migrates its neighbours. A bad write in
one region can damage another mod's bytes, and the port restores nothing and
offers no transaction (`docs/mod-events.md`, section 4). The file boundary
is the only isolation the port can give, because a mod runs in the port's own
address space with the port's privilege (`docs/mod-interface.md`, section 5).

`docs/mod-interface.md`, section 3, allows "one file or folder". A folder per
mod keeps the file boundary and lets a mod keep more than one file under one
namespace, a pairing table beside a run history for example, without a second
namespace. A mod that wants raw files of its own asks for the folder path with
`state_dir`.

### Where it lives

The path is `<save dir>/mods/<namespace>/<save>.bin`, where `<save>` is the
name of the game save file the port is running (`pokeblack` for
`pokeblack.sav`). The save directory is `bw_save_dir()`, first match:
`bw_set_save_dir`, then `BW_SAVE_DIR`, then the platform default (`sdk/save.c`;
`sdk/save.h`). A player who keeps several game saves gets one state file per
save, and switching saves switches the mod's data with it.

### The namespace value

The namespace is the manifest's `state` key, an identifier whose default is
derived from `name` (`docs/mod-interface.md`, section 3). The port validates it
as one path component: it must be non-empty, must not contain a separator or a
`.` or `..` component, must not be a reserved device name, and must contain
only a small character set. `bw_save_path` copies the name verbatim
(`sdk/save.c`), so an unvalidated name can leave the `mods/` folder.
A bad namespace is reported and the port falls back to a value derived from the
directory name, the way a bad `order` is reported and the default stands
(`sdk/vfs.c`).

### The envelope and the payload

The file is a port-owned header followed by the mod's opaque payload.

| Offset | Field | Owner |
|---|---|---|
| 0 | magic `BWMS`, 4 bytes | port |
| 4 | `version`, u16, the port's header version | port |
| 6 | `flags`, u16, zero | port |
| 8 | `payload_size`, u32 | port |
| 12 | `payload_crc`, u32 | port |
| 16 | `api_written`, u32, the manifest `api` in force | port |
| 20 | `save_check`, u32, a checksum of the game save written in the same step | port |
| 24 | `reserved`, 8 bytes, zero | port |
| 32 | the payload | mod |

The header is 32 bytes. The port writes and reads it. The mod owns the payload
and its internal format, which is the mod-owns-the-bytes rule of
`docs/mod-interface.md`, section 3. The mod never sees the header; the
port-owned calls of section 2 hand it the payload and its length. A mod that
wants the raw file uses the folder path and owns every byte in it, including
its own version marker.

### Removal

The port does not delete a mod's folder when the mod is removed.
`docs/mod-rules.md`, section 2, constraint 3, fixes that a disabled mod's
effects persist, and the port restores nothing (`docs/mod-interface.md`,
section 4). Deleting the state
on removal would rewind a run the player may re-enable, so removal leaves the
folder and a re-enable reads it again. The port reports the orphan at the menu.
A player-facing clear action calls the port's `state_clear`, which deletes the
state file and leaves the folder, or deletes the folder.

## 2. The lifecycle

### The four points

**Created.** The first store creates the file, and `bw_save_write_atomic`
creates the folder. The port writes no empty file at load, so an absent file
and an empty file are the same to a mod, and the first run starts from the
mod's defaults.

**Loaded.** At `save.load` the port reads the state file that belongs to the
save being loaded and hands the payload to the mod. It also compares the
header's `save_check` with the save being loaded. When they differ, the game
save was written without the mod (an older backup, or a save made while the mod
was disabled), and the port tells the mod the state is out of step; the mod
decides what to do. A new game (`game.new`) starts with no state.

**Written.** The port writes the mod's state in the same step as the game's
save. At `save.begin` it asks the mod for its payload; when the game's save
completes (`save.done`) it writes the state file with the new game save's
checksum. A game save that fails leaves the old state file in place, so the two
never drift apart. A mod may also store at any other event for its own
reasons; such a store is written with the checksum of the save currently on
disk.

**Discarded.** A store is the whole file: `bw_save_write_atomic` writes a
temporary and replaces the file, so there is no partial state. A store that
fails is reported once per mod and the old file stays. A file the port cannot
read is moved aside (section 5). A player clear action calls
the port's `state_clear`.

### The in-game management constraints

The four constraints are `docs/mod-rules.md`, section 2.

1. The roots and the override registry are rebuildable at the menu, not
   installed once at startup. `bw_set_mod_root`, `bw_add_mod_root` and
   `bw_clear_mods` set the roots, and every change drops the cached merged
   archives (`sdk/vfs.h`, "The mod list"; `sdk/vfs.c`, `bw_vfs_flush`). The
   state namespace is per mod, so a root rebuild does not move state. Enabling
   a mod adds its root and the load point reads its state. The menu is not
   built (`docs/mod-services.md`).
2. Disabling a code mod reverts its slots before its library unloads. The port
   calls the mod's flush first, then reverts each `(region, address)` pair the
   mod installed to its original, then unloads the library. The registry holds
   the mod's handler, and a code pointer in data resolves through the same
   lookup (`sdk/overlay_image.c`, `bw_ovl_code_entry`), so the revert must
   precede the unload. The loader is not built (`docs/mod-abi.md`).
3. A disabled mod's effects persist. The state file stays and the game bytes
   the mod wrote stay until the game writes over them or the port restarts
   (`docs/mod-rules.md`, section 2, constraint 3). Re-enabling reads the state
   again and resumes. The port restores nothing.
4. The port starts with no mods. With no mod root there is no state read, and
   `BW_MOD_ROOT` is read once (`sdk/vfs.c`, `mods_init`). The state folder is
   untouched.

### The calls

A mod uses its state through the service table it receives at start-up
(`docs/mod-abi.md`, "The entry points"; `BwModServices`). The port knows which
mod is calling, so no call names a namespace:

```c
long (*state_load)(void *buf, u32 size);          /* bytes read, -1 on damage */
int  (*state_store)(const void *buf, u32 size);   /* 0, or -1 */
int  (*state_clear)(void);                         /* 0, or -1 */
int  (*state_info)(BwModStateHeader *out);         /* the header fields */
int  (*state_dir)(char *out, u32 out_size);        /* the mod's folder */
```

The port asks for the payload at `save.begin` through the mod's `save.begin`
handler, which calls `state_store`; the port holds the bytes and writes them at
`save.done`. A mod that keeps its own files uses `state_dir` and the same
whole-file discipline.

## 3. The run-time manifest

### The schema

The manifest is `<mod>/eidos_mod.txt` (`sdk/vfs.c`, `BW_MOD_MANIFEST`;
`docs/mod-interface.md`, section 3). `read_manifest` parses `#` comments and
`key = value` lines, ignores a line with no `=`, and ignores an unknown key on
purpose so a later version can add keys (`sdk/vfs.c`). It
reads `name` and `order` today. The run-time schema is the manifest of
`docs/mod-interface.md`, section 3: `name`, `order`, `api`, `port`, `code`,
`state`, `override`, `rule` and `settings`.

The port parses the file into one record per mod, `BwModManifest`, and turns
the record into registrations. The keys and what each becomes:

| Key | Carried as | Registration |
|---|---|---|
| `name` | text | the mod name in every report, and the source of the default namespace |
| `order` | signed integer | the load order, ascending; a later mod wins (`sdk/vfs.h`, "The mod list") |
| `api` | integer | the interface version the mod is built against; a mismatch refuses the mod (`docs/mod-abi.md`, section 3) |
| `port` | text | the port version a mod with raw hooks is built against; a mismatch refuses the mod |
| `code` | relative path | the library the loader opens |
| `state` | identifier | the state namespace of section 1 |
| `override` | a list of pairs | one `bw_mod_slot_install` per pair, after the declared check of section 4 |
| `rule` | a list of event and path | a rule binding for the interpreter |
| `settings` | relative path | the settings declaration |

An `override` value is `<region>:<address> <mode> <symbol> [sig=...]
[switch=...]` (`docs/mod-interface.md`, section 3). The record keeps the region
constant, the address in both the even-start and the Thumb-tagged form, the
mode (`replace` or `wrap`), the symbol, the optional signature and the optional
switch name. The region token resolves the way `docs/mod-implementation.md`,
section 6, fixes: `arm9` to `BW_MOD_REGION_ARM9` and `ovNNN` to
`BW_MOD_REGION_OVL(NNN)` (`sdk/mod_slot.h`).

### Where it comes from in a packed mod

In a packed mod the manifest is `eidos_mod.txt` at the root of the tree inside
the `.bw` container. The port unpacks a `.bw` into its own cache at start-up
and reads the directory, so the reader opens `<root>/eidos_mod.txt` the same
way for a packed and an unpacked mod.

### How it becomes registrations

The port parses first and applies second. The parse is `bw_mod_manifest_parse`.
The apply is `bw_mod_manifest_apply`, one call per mod, in load order. The
apply runs after the declared table is registered (section 4) and after the
event layer is reset (`docs/mod-implementation.md`, section 6), so a later mod
wins over an earlier one for one pair (`docs/mod-overrides.md`, "Two mods on
one address").

The apply does four things per mod:

1. Check `api` (and `port`, when the mod has raw hooks) against the port's
   version, and refuse the mod on a mismatch.
2. Derive the namespace from `state`, or from `name` when `state` is absent.
3. For each `override`: check the pair against the declared table, resolve the
   symbol in the loaded library, and call
   `bw_mod_slot_install(region, addr, symbol, owner)` (`sdk/mod_slot.h`). The later-wins rule and the shadow report are the registry's.
4. Store each `rule` and the `settings` path for the interpreter and the
   settings surface.

The install waits for the loader, which resolves the symbol
(`docs/mod-implementation.md`, section 6). Without the loader the apply stores
the override list on the mod record, and a test installs a known pointer
directly.

## 4. The declared-address check

The set is `mod/declared_set.txt`. `tools/bootlink.py` reads it at build time
with `read_declared_set` (`docs/mod-implementation.md`, section 6) and
`tools/ovlink.py` reads it too, and nothing carries it at run time. The port
must know the set at load, when a mod's manifest names its addresses, so the
build emits a table of the set beside the slot publications.

### The emitted table

The build emits one table of the whole set, `bw_declared_rows[]`, from every
row of `mod/declared_set.txt`, the `arm9` rows and the overlay rows together.
The boot link emits it beside the slot publications, into `boot_link.c`
(`tools/bootlink.py`, `write_link_c`; `docs/mod-interface.md`, section 2), and
`bw_boot_link_init` registers it (`tools/bootlink.py`, `write_link_c`). Each row is
a pair:

```c
/* sdk/mod_slot.h */
typedef struct BwModDeclared {
    unsigned int region;   /* BW_MOD_REGION_ARM9 or BW_MOD_REGION_OVL(id) */
    unsigned int addr;     /* the even function start */
} BwModDeclared;

void bw_mod_declare_table(const BwModDeclared *rows, unsigned int count);
int  bw_mod_slot_declared(unsigned int region, unsigned int addr);
```

`bw_mod_declare_table` stores the rows in a declared set in `sdk/mod_slot.c`.
`bw_mod_slot_declared` answers from it. The set holds a few hundred rows, so
the table is sorted by region and address and searched by bisection.

The table carries the whole set, not only the rows the link defines. A declared
row whose definition the link does not carry has no slot
(`docs/mod-interface.md`, section 2), and an overlay row's slot does not exist
until that overlay loads. The check runs at mod load, which is before any
overlay loads, so a table that only carried published rows would answer
"not declared" for a declared overlay address. One table at start-up makes the
answer independent of what has loaded, and leaves `tools/ovlink.py` with no
change.

### The check

`sdk/mod_manifest.c` calls `bw_mod_slot_declared(region, addr)` for each
`override` pair. The address is normalised before the lookup: the low bit is
masked, so the even start (`arm9:0x02007150`) and the Thumb-tagged value a code
pointer holds (`arm9:0x02007151`) both answer declared
(`docs/mod-interface.md`, section 1).

A pair that answers declared is installed once the loader resolves the symbol.
A pair that answers not declared is reported and skipped, the shape
the port uses for every manifest problem:

```
eidos: mod <name>: override <region>:<addr> is not a declared address
```

### An address that is not declared

The mod still loads and its other keys apply. The report fires once per pair,
the way `read_manifest` reports a bad `order` once and lets the default stand
(`sdk/vfs.c`). The port does not fail the mod, because the trust
model validates nothing and the check is a diagnostic, not a sandbox
(`docs/mod-interface.md`, section 5). No slot consults the address, because no
slot exists for it, and the override is never installed.

## 5. A version story for the state

A mod's state file outlives a port version. Two versions meet in it.

- The port's header version, `BwModStateHeader.version` of section 1. The port
  owns it and bumps it when the header layout changes.
- The mod's payload version. The mod owns it and puts its own version in the
  payload. The port cannot interpret it.

The header also carries `api_written`, the manifest `api` in force when the
state was written (`docs/mod-abi.md`, section 3). The port reports a
mismatch to the mod at load and the mod decides.

### An older file

The port reads a header older than the current version, locates the payload
with the old header's `payload_size`, hands the mod the payload, and writes the
current version on the next store. The payload is opaque, so a header change
that keeps the payload locatable loses nothing. If the old header cannot locate
the payload, the port moves the file to `<save>.bin.v<old>` and starts fresh,
reporting it. The moved file is kept, not deleted, so a player can recover it.

### A newer file

A version newer than the port's is not read. The port reports
`eidos: mod <ns>: state version <n> is newer than <m>`, does not hand the
payload to the mod, moves the file to `<save>.bin.v<n>`, and starts fresh.
Reading a newer layout would misinterpret the payload, and the moved file lets
a newer port read it again.

### A damaged file

The magic is wrong, the file is shorter than the header, `payload_size` exceeds
the file, or `payload_crc` does not match: the port reports
`eidos: mod <ns>: state file is damaged`, does not hand the payload to the mod,
moves the file to `<save>.bin.bad`, and the mod starts from defaults. The crc
catches a hand edit or a disk fault. `bw_save_write_atomic` already rules out a
torn write, because it writes a temporary and replaces it (`sdk/save.c`, `sdk/save.h`).

### The mod's own migration

The port hands the header fields to the mod through `state_info`, so the
mod can migrate its payload when `version` or `api_written` changed. A mod that
cannot migrate calls `state_clear` and starts fresh. The port does not
migrate a payload it cannot read.

## 6. The implementation split

The steps below split into workstreams with disjoint files. Step 1 is the
shared header and lands first; steps 2 to 5 are then parallel; step 6 wires
them.

**Step 1. Freeze the state header and the state API.** `sdk/mod_state.h`:
`BwModStateHeader`, the namespace helpers, and the `bw_mod_state_*` prototypes.
One writer. It is the interface the other steps compile against.
Test: a compile-only check in `tests/test_mod_state.c` that includes the header
and asserts the header size and offsets.

**Step 2. The state layer.** `sdk/mod_state.c`: the namespace validation and
default, the path, the envelope, `bw_mod_state_load`, `bw_mod_state_store`,
`state_clear`, `state_info`, `state_dir`. Disjoint file.
Test: `tests/test_mod_state.c`, a temp directory set with `bw_set_save_dir`,
like `tests/test_vfs.c`. It stores and loads a payload, checks the header, the
crc, an older version, a newer version, a damaged file, and the namespace
default. No game data is needed. Register it with the `foreach` list
(`CMakeLists.txt`, the test list).

**Step 3. The declared set at run time.** `sdk/mod_slot.h` and
`sdk/mod_slot.c`: `BwModDeclared`, `bw_mod_declare_table`,
`bw_mod_slot_declared`, and the low-bit mask.
Test: `tests/test_mod_declare.c` registers a small table and checks a declared
pair, an undeclared pair, and the Thumb-tagged form. Register it like step 2.

**Step 4. The manifest reader.** `sdk/mod_manifest.h` and
`sdk/mod_manifest.c`, and the one call from `read_manifest` (`sdk/vfs.c`). It depends on step 3 for the declared check.
Test: `tests/test_mod_manifest.c`, a temp tree like `tests/test_vfs.c`, with
one declared and one undeclared `override` line and a bad `api`. It checks the declared pair is stored, the undeclared pair is
reported and skipped, and the namespace default derives from `name`.

**Step 5. The generator change.** `tools/bootlink.py` emits `bw_declared_rows[]`
from `mod/declared_set.txt` and the `bw_mod_declare_table` call in
`bw_boot_link_init` (`tools/bootlink.py`, `write_link_c`).
Test: the seam verification path of `docs/mod-interface.md`, section 2. The
generated `boot_link.c` is read in a scratch out-dir for the table and the
call. `tests/test_ovlink.py` shows the generator-test pattern. No compiler and
no game byte are needed.

**Step 6. Wiring.** Add `sdk/mod_state.c` and `sdk/mod_manifest.c` to
`BW_SDK_SOURCES` (`CMakeLists.txt`, beside `sdk/mod_slot.c`), add the
tests, and reset the manifest layer from `bw_port_reset` (`sdk/port_reset.c`;
`docs/mod-implementation.md`, section 6).
Test: the full suite and the boot with a mod that declares one state namespace
and one override.

The dependency chain is step 1, then steps 2 to 5 in parallel, then step 6.
Steps 2, 3, 4 and 5 touch disjoint files. Step 5 touches only the generator.

## Still to build

1. **The namespace validation table.** The reserved names, the character set
   and the length cap, so a namespace can never leave the `mods/` folder.
2. **The payload size cap.** A cap, and the report when a mod stores more.
3. **The in-game mod menu.** Enabling and disabling a mod, the state clear
   action and the orphan report (`docs/mod-services.md`).
4. **The disable order.** Flush the state, revert the mod's slots, then unload
   the library (constraint 2), built with the loader.

## Related documents

- `docs/mod-interface.md`: the manifest schema, the state namespace, the event
  and state API, and the trust model.
- `docs/mod-events.md`: the event catalogue, the state API and what a mod may
  write.
- `docs/mod-implementation.md`: the event and state implementation plan, the
  declared-set run-time check, and the build order.
- `docs/mod-rules.md`: the rule substrate and the four constraints of in-game
  management.
- `docs/modding.md`: the overview of the mod surface.
- `docs/mod-abi.md`: the entry points and the service table.
- `sdk/save.h`, `sdk/save.c`: the save folder and the atomic whole-file write.
- `sdk/vfs.c`, `sdk/vfs.h`: the mod list, the load order and the manifest
  reader.
- `sdk/mod_slot.h`, `sdk/mod_slot.c`: the `(region, address)` registry the
  declared table and the install join.
