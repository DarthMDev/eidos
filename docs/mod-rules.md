# Mod rules: the two decisions

This document records two decisions the rule and packaging design rests on,
with the evidence behind each: what runs a rule, and how a packed mod is read
and managed. `docs/mod-rules-language.md` is the language itself.

## 1. A small interpreter in the port runs rules

### The alternative: the game's own script VM

The game has a bytecode VM. Its core is `VM_Create` (0x0201121C), `VM_Control`
(0x02011298) and `VM_Delete` (0x02011264), in aletheia's
`src/matched/arm9/unit_020111EC.c` and `unit_0201126C.c`. `VM_Control` fetches
a u16 opcode, checks it against the table's size, and calls the handler from a
command table. The game runs two instances of it:

- the field script VM: `SCRIPT_SYS_CreateVm` (0x02158BFC) creates it with the
  609-entry table at `d_0217062C`, and `SCRIPT_SYS_Main` (0x0215882C) runs it
  once per frame;
- the battle effect VM: `BTLV_EFFVM_Create` (0x021F93F4, ov094) creates it with
  the 78-entry table at 0x02209E88.

Its handlers reach game state through the script work object, and several
reach the values a rule needs: `SCRCMD_PROC_SetMiscValue` (0x0215C558) adds
money through `BEACON_AddPoints`, `SCRCMD_PROC_AddMiscValue` (0x0215C584)
subtracts it through `BEACON_SubPoints`, `SCRCMD_PROC_HealParty` (0x0215B5A8)
heals the party.

### Why not the VM

What a rule needs and the VM cannot do:

- **Run at a port event.** The VM is driven by a script's program counter. No
  opcode fires at a port event or reads a typed payload.
- **Refuse an action and modify a payload.** These are results the port's event
  layer produces; the VM has no opcode for either.
- **Use the mod's saved data.** The VM context holds a stack and a ring buffer,
  with no access to the mod's state.
- **Stay a closed surface.** A rule on the VM can do whatever its command table
  does, which is the game's command set plus anything added to it.

Reusing the VM would still need the port's event and state layer, plus a
command table that reaches it, plus an assembler for the VM's bytecode. A small
interpreter over the port's events, payloads and state fields needs none of
that, and what a rule may do is exactly what the port defines.

## 2. A packed mod is unpacked by the port and managed in the port

### The container

A packed mod is one `.bw` file holding the mod's directory tree and its
manifest. kenon writes the format. The port unpacks a `.bw` into its own cache
at start-up and reads the directory, exactly like an unpacked mod; the data
layer, the manifest reader and the loader need no container code
(`docs/mods.md`; `docs/mod-state.md`, section 3).

### In-game management

Players enable, disable and order mods from the port's own menu, with no
separate launcher. That puts four constraints on the port:

1. **The mod list is rebuildable while the port runs.** The data roots and the
   slot registry are rebuilt when the player changes the list, not installed
   once at start-up. The roots already support this: `bw_set_mod_root`,
   `bw_add_mod_root` and `bw_clear_mods` set them, and every change drops the
   cached merged archives (`sdk/vfs.h`, "The mod list").
2. **A code mod is disabled in order.** The port stores the mod's state,
   reverts the slots the mod installed, calls `eidos_mod_stop`, then unloads the
   library. The revert comes before the unload because the registry holds
   pointers into the library (`docs/mod-implementation.md`, section 3).
3. **A disabled mod's effects stay.** Game values the mod changed stay until
   the game changes them, and the mod's saved data stays on disk. The port
   restores nothing; re-enabling the mod reads its data again.
4. **The port starts with no mods** unless the player enabled some. A mod never
   runs because it is merely present in the folder.

## Related documents

- `docs/mod-rules-language.md`: the rule language.
- `docs/mod-events.md`: the events and state a rule reads and writes.
- `docs/mod-state.md`: the manifest at run time and the mod's saved data.
- `docs/mods.md`: the data layer and the mod directory layout.
