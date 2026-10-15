# Mod rules

This document settles the two items `docs/mod-interface.md` records as still
to build and that block the rule tooling: the substrate a rule runs on (item
14) and where a packed mod is read (item 13). It reads the game's own script
VM against the worked example, then reads the file layer and the installer
against the container. Every claim below is attributed to the file it was read
from. An item the code does not settle is marked as work still to build, with
the evidence on each side.

## 1. The rule substrate

A rule is code in a small predetermined language, not data (`docs/mod-interface.md`,
"The rule surface"; `docs/events-and-rules.md` of kenon, "How a rule compiles
or interprets"). The worked example is `sub money 1000` at `battle.faint`. The
question is what runs it: the game's own bytecode VM, or a new interpreter in
the port (`docs/mod-interface.md`, item 14).

### What the game's VM is

The VM core is three entry points, `VM_Create` (0x0201121C), `VM_Control`
(0x02011298) and `VM_Delete` (0x02011264) (`docs/mod-interface.md`, section
4). `src/matched/arm9/unit_020111EC.c` holds `VM_Reset` and `VM_Create`, and
`src/matched/arm9/unit_0201126C.c` holds `VM_Init`, `VM_Start`, `VM_End`,
`VM_Control`, `VM_GetContext` and `VM_SetHook`.

`VM_Create` takes a heap id and a 12-byte descriptor
`{ u16 stack_cap; u16 ring_cap; const ScriptCmdFn *cmds; u32 cmd_count; }` and
allocates a 0x30-byte context plus a stack array and a ring array behind it
(`unit_020111EC.c`). `VM_Control` is the interpreter core: it fetches a u16
opcode with `VM_GetU16`, bounds-checks it against the descriptor's
`cmd_count`, runs an optional pre-dispatch hook, then calls
`cmds[opcode](vm, user)` (`unit_0201126C.c`). A handler that returns 1 yields
for the frame; a handler that returns 0 continues into the next opcode
(`unit_0201126C.c`). The operand readers are `VM_GetU16` and `VM_GetU32`, a
little-endian u16 and u32 read off the pc cursor (`unit_02011330.c`).

The game runs two instances of the same engine, each with its own command
table:

- The field script VM. `SCRIPT_SYS_CreateVm` (0x02158BFC) calls `VM_Create`
  with `{0x100, 0x40, 0x0217062C, 609}` and `VM_Init`, then `VM_Start` at the
  loaded script (`src/matched/ov/ov010_func_02158BFC.c`). The table is
  `d_0217062C`, 609 entries, and the count is `d_021705D8`
  (`ov010_func_02158BFC.c`; `unit_0201126C.c` records the 609-entry table at
  0x0217062C). `SCRIPT_SYS_Main` (0x0215882C) calls `VM_Control` once per
  frame (`src/matched/ov/func_0215882C.c`).
- The battle effect VM. `BTLV_EFFVM_Create` (0x021F93F4, ov094) calls
  `VM_Create` with its own descriptor at 0x02209DCC,
  `{0x10, 0x08, 0x02209E88, 0x4E}`, and the table 0x02209E88 has 78 entries
  (`<home>\OneDrive\Documents\bw_decomp\tools\assets\btleff.py`, its
  header).

The bytecode is one little-endian u16 opcode then its operand bytes. The field
VM's opcodes have mixed operand widths; the battle effect VM's 78 opcodes take
4-byte u32 operands only (`tools\assets\scripts.py` and `tools\assets\btleff.py`,
their headers). Aletheia's two tables are `tools/assets/scripts.py` (field)
and `tools/assets/btleff.py` (battle effect). The field VM's stop opcodes are
{2, 29, 140, 342, 359, 378}, opcode 4 is a call, and the flow-end opcodes are
{5, 30, 31} (`scripts.py`, its header). The battle effect VM's opcode 77 is a
stop, 70 a call, 71 a return, and 60, 61, 72 and 74 branches (`btleff.py`, its
header).

### What a script in that VM can reach

A handler receives `(vm, user)`, and the `user` object is the script work
object: `SCRIPT_SYS_CreateVm` builds it with `SCRCMD_WORK_Create` and passes
it to `VM_Init` (`ov010_func_02158BFC.c`). The field VM's handlers reach the
game's own state through that object.

Two field VM commands reach the player money, which is the worked example's
field:

- `SCRCMD_PROC_HealParty` (0x0215B5A8) calls `SCRCMD_WORK_GetGameData`,
  `GAME_DATA_GetMyParty` and `POKE_TOOL_HealParty` (`src/matched/ov/func_0215B5A8.c`).
- `SCRCMD_PROC_SetMiscValue` (0x0215C558) calls `GAME_DATA_GetMiscSave` and
  `BEACON_AddPoints` (`src/matched/ov/ov010_func_0215C558.c`), and
  `SCRCMD_PROC_AddMiscValue` (0x0215C584) calls `GAME_DATA_GetMiscSave` and
  `BEACON_SubPoints` (`src/matched/ov/ov010_func_0215C584.c`).

`BEACON_GetPoints` (0x0200C274), `BEACON_AddPoints` (0x0200C278) and
`BEACON_SubPoints` (0x0200C29C) are the money accessors `docs/mod-interface.md`
names ("State fields a rule may name", the money is save block id 0x34, read
by `BEACON_GetPoints`, written by `BEACON_AddPoints` or `BEACON_SubPoints`).
`GAME_DATA_GetMiscSave` (0x02012F08) is the block accessor the same section
names. So a script in the field VM can read and write the game state the
worked example needs, and can call the accessors `docs/mod-interface.md`
names, because the VM's command handlers are those accessors' own callers.

### What the VM cannot express

- Binding to the port's events. The VM is driven by a pc into a script file:
  `VM_Control` fetches the next opcode from `vm->pc` and calls a handler from
  the command table (`unit_0201126C.c`). No opcode fires a port event slot or
  carries the typed payload of `docs/mod-interface.md`, section 4. The command
  table is the game's, not the port's.
- The mod state namespace. The VM context holds `user`, `data`, `pc`, `stack`
  and `ring` (`unit_020111EC.c`). It has no save path and no call to
  `sdk/save.h`, so a rule that must persist state at `save.begin`
  (`docs/mod-interface.md`, section 4) cannot do it in the VM alone.
- A mod's C handler and the override registry. Nothing in the VM reaches the
  loader or the override seam of `docs/mod-interface.md`, section 2.
- Block and allow as a value. A block is the return of a slot, not a state
  write (`docs/mod-interface.md`, section 4). The VM has no opcode for it.
- The port's field table. The VM's commands act on the game's own objects
  through the script work object (`func_0215B5A8.c`,
  `ov010_func_0215C558.c`), not on the named fields of `docs/mod-interface.md`,
  section 4.

The port has not wired the script VM. `README.md`, "Planned Milestones", lists
"script VM" as unchecked under Milestone 7, and `docs/status.md`, "Milestone
7, gameplay", states the field system including the script VM is matched and
not wired into the port.

This matters for the four example mods. The money capability is present in the
field VM's command set, but the four mods need event binding, blocking, and
per-mod state (`docs/modding.md`, "What the four example mods need"). The VM
expresses none of the three. Those needs fall on the port's event and state
API whichever substrate runs the rule, so the VM can only be a computation
engine behind a port-owned event slot; it is not sufficient on its own.

### The cost of each substrate

Reusing the game's VM:

- Wire the script VM into the port, which is not done (`README.md`, "Planned
  Milestones"; `docs/status.md`, "Milestone 7, gameplay").
- Give a VM instance a command table that reaches the port's field table and
  the event payload, because the game's table reaches game state only
  (`ov010_func_02158BFC.c`). Adding a reachable capability is C in that table.
- Compile the rule language to the VM's bytecode, over the opcode tables of
  `tools/assets/scripts.py` and `tools/assets/btleff.py`. `README.md` plans an
  assembler and disassembler for the two VMs, but the work is not done.
- What a rule may do becomes the game's command set plus whatever commands are
  added, so the surface is not naturally closed to the port's field table.

A new interpreter in the port:

- A small interpreter over a fixed opcode set, over the port's field table and
  the event payload (`docs/mod-interface.md`, sections 3 and 4).
- No VM wiring and no assembler. The rule stays out of host code and the
  surface stays small (`docs/events-and-rules.md` of kenon, "How a rule
  compiles or interprets", shape 3).
- What a rule may do is exactly the opcodes the port defines, so the surface
  is closed by construction.
- It is new port work the four eidos documents do not list
  (`docs/events-and-rules.md` of kenon, shape 3).

The trust is the same on both. `docs/mod-interface.md`, section 5, promises no
sandboxing and no bounded resource use, so a small language is a constraint on
the author, not a safety boundary.

### Decision

A small interpreter in the port runs the rule (the new-interpreter option of
`docs/mod-interface.md`, item 14; shape 3 of kenon `docs/events-and-rules.md`).

Evidence. The rule acts at a port event over the port's field table
(`docs/mod-interface.md`, sections 3 and 4), and the game's VM has no opcode
that reaches an event or the field table (`unit_0201126C.c`; the field
handlers reach game state through the script work object,
`ov010_func_02158BFC.c`). The VM is not wired (`README.md`, "Planned
Milestones"; `docs/status.md`, "Milestone 7, gameplay"). Reusing it costs the
wiring plus a command-table extension plus an assembler, and buys a rule
surface that is the game's command set rather than the port's constrained one.
The interpreter needs none of those and closes the surface to the field table.

What would settle it the other way. If the port wires the script VM for
gameplay (Milestone 7) for its own reasons, the marginal cost of reusing it
falls and the decision should be revisited. If the rule language must express
loops or expressions the interpreter would have to grow, the VM's existing
branch and call opcodes become attractive: the field VM's call opcode 4 and
flow-end opcodes {5, 30, 31} (`tools/assets/scripts.py`, its header), and the
battle effect VM's call 70, return 71 and branches 60, 61, 72 and 74
(`tools/assets/btleff.py`, its header).

### Still to build

1. The rule language and its grammar, which `docs/mod-interface.md`, item 14,
   leaves open: the statements, the actions, and how a rule names the fields.
2. The interpreter seam: where the port runs a rule at a slot, and how it
   reads and writes the field table of `docs/mod-interface.md`, section 4.
3. The field table's completeness. The party record's ids 0x9D and 0x9F to
   0xA6 have no name yet (`docs/mod-interface.md`, item 15), so a rule cannot
   name HP or a stat.

## 2. Where a packed mod is read

A packed single-file mod is one `.bw` container with the same tree and
manifest inside. The current layer reads a directory root
(`docs/mod-interface.md`, section 3), so no `.bw` is read anywhere. The port
owns the packed form: it unpacks a `.bw` at startup into its own cache, or
reads the container directly, and reads a mod directory as it does today.
There is no player launcher. The port manages mods in the game's own menu and
a mod can be enabled without restarting the game. Which of the two, a cache
or a direct read, is still open. The container format, its compression policy
and its extension are kenon's to fix, because kenon writes the container
(kenon `docs/bw-format.md`, "The container format is not decided").

### The existing code

The file layer resolves a directory root.

- `bw_data_path` joins a root and a relative path (`sdk/vfs.c`, `bw_data_path`
  and `join`), and `bw_vfs_file_layer` asks `bw_plat_path_kind` for a regular
  file at the join (`sdk/vfs.c`, `bw_vfs_file_layer`).
- The roots are directories: `bw_set_mod_root` and `bw_add_mod_root` take a
  path (`sdk/vfs.c`, `sdk/vfs.h`, `sdk/port.h`), and `bw_vfs_file_layer` and
  the member collector join root plus relative path (`sdk/vfs.c`,
  `sdk/vfs_narc.c`, `collect`).
- A whole file reaches a reader as a filesystem path. `read_whole` opens it
  with `fopen` (`sdk/vfs_narc.c`, `read_whole`), and the FS shim falls back to
  `bw_data_path` plus `open_window` when no merge applies (`sdk/fs.c`,
  `open_rel_window`).
- The memory path covers a merged NARC only. `bw_vfs_acquire` returns the
  cached image through a handle, but `bw_vfs_build_merged` returns 0 when no
  mod contributes a member (`sdk/vfs_narc.c`, `bw_vfs_build_merged`,
  `bw_vfs_acquire`), and the FS shim then opens the path instead
  (`sdk/fs.c`, `open_rel_window`). The handle is a cached merged archive, not
  a general whole-file resolver.
- The installer already unpacks into the eidos home. `bw_setup_install`
  extracts into `extract_dir`, which defaults to `<home>/extract`
  (`install/setup.c`, `bw_setup_install` and `bw_setup_default_extract`), and
  the mod overlay root is meant to sit beside them as `mods/`
  (`docs/installer.md`, "Where the data goes"; `docs/mods.md`, "A worked
  example").

### Decision

The port owns the packed form. It unpacks a `.bw` at startup into its own
cache, or reads the container directly, and reads a mod directory as it does
today. The choice between the two is still open. Either way the unpacking is
the port's, not another program's: there is no player launcher, and the port
owns the mod list, enable and disable, the load order and conflict reporting
(kenon `docs/bw-format.md`, "The port owns mod management").

Evidence. The reasons for not reading the container directly still stand, and
they are the cost of the direct option. The roots are directories and
`bw_data_path` returns a filesystem path that `fopen` opens, so a directory
root needs no change (`sdk/vfs.c`, `sdk/vfs_narc.c`). The memory handle exists
only for a merged NARC, so the port reading a container directly would need
`bw_data_path` and the resolution to serve whole files from memory, and every
reader that uses `bw_data_path` plus `fopen` would need to go through the
handle (`sdk/vfs.c`, `sdk/fs.c`, `sdk/vfs_narc.c`). The installer already
writes into the eidos home where `mods/` sits, so a cache lives where the port
already writes (`install/setup.c`, `bw_setup_install`,
`bw_setup_default_extract`; `docs/installer.md`, "Where the data goes").

### The two options and what each costs

**The port unpacks the container into its own cache.** The file layer needs no
change: `bw_data_path` joins a root and a relative path, and the roots are
directories (`sdk/vfs.c`, `bw_data_path`; `sdk/vfs.h`, `bw_add_mod_root`,
`bw_set_mod_root`). The unpack is the port's, at startup or at the point the
mod roots are set. The installer already unpacks into the eidos home, which is
where a cache would live (`install/setup.c`, `bw_setup_default_extract`;
`docs/installer.md`, "Where the data goes"). Cost: a new unpack step in the
port, a second copy of the mod on disk, and the container format, its
compression policy and its extension are not fixed (kenon `docs/bw-format.md`,
"The container format is not decided").

**The port reads the container directly.** The port gains a reader that
resolves a NitroFS path inside the container and serves whole files and member
directories the way a directory root does, so `bw_vfs_file_layer` and
`bw_data_path` answer for a packed root too (`sdk/vfs.c`, `bw_vfs_file_layer`,
`bw_data_path`). Cost: a container parser in the file layer, and a memory form
for whole files in `bw_data_path`, which today returns a filesystem path that
`fopen` opens. Every reader that uses `bw_data_path` plus `fopen` would go
through the handle (`sdk/fs.c`, `open_rel_window`; `sdk/vfs_narc.c`,
`read_whole`), and the existing handle serves a merged NARC only
(`sdk/vfs_narc.c`, `bw_vfs_acquire`, `bw_vfs_build_merged`).

### The four constraints of in-game management

The port manages mods in the game's own menu, and a mod can be enabled without
restarting the game. That management implies four constraints.

1. **The mod roots and the override registry are rebuildable at the menu, not
   installed once at startup.** `bw_set_mod_root`, `bw_add_mod_root` and
   `bw_clear_mods` set the roots, and every change drops the cached merged
   archives so the new view applies at the next open (`sdk/vfs.h`, "The mod
   list"; `sdk/vfs.c`, `bw_vfs_flush`; `docs/mods.md`, "Setting the roots").
   Enabling or disabling a mod rebuilds the roots and the override registry.
   The registry is the `(region, address)` table of `docs/mod-interface.md`,
   section 2. The menu that changes the list is not built
   (`docs/mod-interface.md`, item 11; `docs/modding.md`, "Settings and UI:
   partial").
2. **Disabling a code mod reverts its slots before its library is unloaded.**
   The registry holds the mod's handler for a `(region, address)` pair, and a
   code pointer in data resolves through the same lookup
   (`docs/mod-interface.md`, section 2, "The (region, address) key";
   `sdk/overlay_image.c`, `bw_ovl_code_entry`), so the game can hold a pointer
   into the loaded library. The port restores each slot to its original before
   the library is unloaded. The loader is not built (`docs/mod-interface.md`,
   item 4).
3. **A disabled mod's effects persist.** State a mod wrote, and anything it
   changed through an override's return, stays until the game writes over it
   or the port restarts. The port does not arbitrate a write: the game's later
   write wins, and the port restores nothing and offers no transaction
   (`docs/mod-interface.md`, section 4, "Read and write"). An ARM9 save-block
   field written at `save.begin` persists until `bw_ds_reset` or a later game
   write (`docs/mod-interface.md`, section 4). This is normal and standard: a
   disabled mod stops its future effects and does not rewind past ones.
4. **The port starts with no mods.** With no mods the resolver answers from
   the data root alone (`docs/mods.md`, "The layering model"; `sdk/vfs.c`,
   `bw_vfs_file_layer`). `BW_MOD_ROOT` is read once, the first time the list
   is consulted, unless a call set the list first (`sdk/vfs.c`, `mods_init`).
   A `--no-mods` switch, and an enable list the port reads before it loads
   anything, let a player turn off a mod that breaks startup without the game.
   The port's own settings file lives in the eidos home
   (`engine/platform/settings.c`, `eidos_settings_path`; `install/setup.c`,
   `bw_setup_home`), which is where such a list could live. Neither the switch
   nor the list is built.

### Still to build

1. The container format, its compression policy and its extension (kenon
   `docs/bw-format.md`, "The container format is not decided").
2. The unpack step: where the port unpacks a container and how it points a mod
   root at the result. The roots and the flush exist (`bw_add_mod_root`,
   `bw_set_mod_root`, `bw_vfs_flush`; `sdk/vfs.c`, `sdk/vfs.h`).
3. The cache: where the port's cache lives under the eidos home and when it is
   rebuilt or cleared. The installer writes `<home>/extract` and the mod
   overlay root sits beside it (`install/setup.c`, `bw_setup_default_extract`;
   `docs/installer.md`, "Where the data goes"), and every root change drops the
   cached merged archives (`sdk/vfs.h`, `bw_vfs_flush`).
4. The single-file question: whether the player may drop a container with no
   unpack step, which would add the container parser and the `bw_data_path`
   memory form.
5. The in-game management: the menu that rebuilds the roots and the override
   registry, and the settings it writes. The API and the flush exist
   (`sdk/vfs.h`, `sdk/vfs.c`); the menu is not built (`docs/mod-interface.md`,
   item 11; `docs/modding.md`, "Settings and UI: partial").
6. The unload order: revert a disabled mod's slots before its library is
   unloaded. The registry holds the mod's handler (`docs/mod-interface.md`,
   section 2); the loader is not built (`docs/mod-interface.md`, item 4).
7. The no-mods switch and the enable list the port reads before it loads
   anything, so a mod that breaks startup can be turned off without the game.
   `BW_MOD_ROOT` is read once (`sdk/vfs.c`, `mods_init`); no switch or list
   exists.

## Related documents

- `docs/mod-interface.md`: the interface contract, its sections 3 and 4 and
  its still-to-build items 13 and 14.
- `docs/modding.md`: what the four example mods need and what exists today.
- kenon `docs/events-and-rules.md`: the event catalogue and the three shapes a
  rule compiles or interprets.
- kenon `docs/bw-format.md`: the `.bw` container and the manifest.
- `tools/assets/scripts.py` and `tools/assets/btleff.py`: the field script VM
  and the battle effect VM opcode tables.
