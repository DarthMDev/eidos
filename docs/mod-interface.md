# Mod interface

This document owns the contract between a mod and the port at the level of
the build: which game functions a mod can reach, how a call reaches a mod, how
a mod is declared, and what a mod is trusted with. `docs/mod-events.md` owns
the event and state API built on top of it, and `docs/modding.md` is the
overview.

The contract starts from one property of the build. A DS call site names an
address; a host call site names a symbol, and `tools/bootlink.py` and
`tools/ovlink.py` bind each spelling to its definition when the port is built
(`docs/boot.md`, "Names by address"; `docs/overlays.md`). A mod loaded at run
time cannot intercept a call that is already bound. Only the build can put an
indirection at a call site, so the port decides at build time which functions
get one. That list is the declared set.

## 1. The declared set

`mod/declared_set.txt` lists the game functions the port routes through a
slot. A mod can reach a game function only when it is listed there. The
generators read the file directly: `tools/bootlink.py` takes the `arm9` rows
and `tools/ovlink.py` the overlay rows, and each emits one slot per row.

Each row has four tab-separated columns: the address (the even function start
as aletheia's manifests spell it), the aletheia function name, the region
(`arm9` or an overlay id), and the stable events the port binds to the
function. An empty fourth column marks a raw hook only. Every row is checked
against aletheia's per-packet manifests (`layout/matched/**/pkt_*.json`) by
address, name and region. `docs/mod-events.md`, section 5, gives the counts by
region.

**The key is `(region, address)`.** Two overlays can occupy one RAM window, so
one address can be two functions: 0x021C5200 is
`BTL_SERVER_FLOW_ProcFaintRewards` in ov093 and
`ZUKAN_DETAIL_FORM_SignalAnimDone` in ov208. The region decides which.

**The Thumb bit.** A code pointer to a Thumb function holds the address with
the low bit set, and the overlay entry tables key on that value
(`sdk/overlay_image.h`, `bw_ovl_code_entry`). The declared set and the
manifest always use the even start. The port accepts both forms from a mod
and clears the low bit before a lookup.

**Why a declared set, not every function.** A slot per declared function costs
one generated function and one rename per call site. A slot for every
cross-unit call would cost thousands of slots and an indirect call at every
site, and would make every function a maintenance item when aletheia
restructures the code. The declared set keeps that cost to the functions mods
need. Adding a row is cheap: it is one line and a rebuild.

## 2. The slot

For each row the generator emits a slot function and redirects every call
site that is not the function's own unit.

**The rename.** Each translation unit is compiled with a generated header. For
a unit that calls a declared function and does not define it, the header gains
one line:

    #define SAVE_CONTROL_StartSave bw_slot_arm9_02007150

The call then compiles to a call to the slot. The defining unit gets no line,
so its own body still defines the original name. For an overlay row the
spelling becomes `bw_slot_<region>_<address>`, for example
`bw_slot_ov093_021C5200`. A caller outside the overlay's window reaches it
through the cross-overlay trampoline, which consults the same registry.

**The slot.** The slot is generated C in the region's link file
(`boot_link.c` for `arm9`, `ovNNN_link.c` for an overlay), with the
definition's own prototype:

    int bw_slot_arm9_02007150(int a0)
    {
        void *h = bw_mod_slot_lookup(BW_MOD_REGION_ARM9, 0x02007150u);
        if (h != NULL && bw_hook_enabled("SAVE_CONTROL_StartSave")) {
            return ((int (*)(int))h)(a0);
        }
        return SAVE_CONTROL_StartSave(a0);
    }

With nothing installed, or with the hook switched off, the slot calls the
original. A prototype the generator cannot normalise is refused at generation
with the address and the name.

**The registry.** `sdk/mod_slot.h` and `sdk/mod_slot.c` hold one entry per
installed hook: the region, the address, the installed function, the original
and the owning mod. `bw_mod_slot_install` installs a hook; a later install of
one pair wins and the earlier is reported as shadowed. `bw_mod_slot_original`
returns the original, which the build publishes at start-up for `arm9`
(`bw_boot_link_init`) and when an overlay loads (`sdk/overlay_image.c`,
`provider_load`). A `wrap` calls it.

**The switch.** The slot asks `bw_hook_enabled` (`sdk/mod_hooks.h`) with the
row's function name, the same switch the port's compiled hooks use
(`docs/hooks.md`). `EIDOS_HOOKS_OFF` or `bw_hook_set` turns a slot back to the
original, which isolates one mod while debugging.

**Code pointers.** A function the game reaches through a pointer stored in
data does not pass a call site. For overlay windows `bw_ovl_code_entry`
consults the registry before its own table (`sdk/overlay_image.c`), so a
stored pointer into an overlay reaches the installed hook. A stored pointer
into the ARM9 static module does not yet consult it.

**What a slot does not catch.** A call from inside the defining unit binds by
name and does not pass the slot, because a textual rename cannot tell a
reference from the definition. For the functions the stable events bind this
does not matter: each is called from other units. A raw hook on a function
that its own unit also calls misses those calls.

**Checking the seam without a build.** The generators are standalone tools,
so their output can be read without compiling:

    python tools/bootlink.py generate --aletheia-root <aletheia> --plan-dir <build>/boot --shim-sources <build>/boot/shim_sources_gen.txt --port-sources <build>/boot/port_sources.txt --armdata-header <build>/armdata/bw_arm9_data.h --define EIDOS_HEAP_MATCHED=0 --out-dir <scratch>/boot

`<scratch>/boot/boot_link.c` holds one `bw_slot_arm9_` function per `arm9` row
the boot link carries, and the generator reports by name each row it skipped
because nothing in the link defines it.

## 3. The manifest

A mod is a directory laid out like the data root. Whole files and member
directories resolve by path, and a later mod wins (`docs/mods.md`). The
manifest is `<mod>/eidos_mod.txt`: `#` starts a comment, each line is
`key = value`, a line with no `=` is skipped, and an unknown key is ignored so
later versions can add keys (`sdk/vfs.c`, `read_manifest`).

| Key | Value | Default | Meaning |
|---|---|---|---|
| `name` | text | the directory name | display name |
| `order` | integer, -1000000 to 1000000 | 0 | load order; a later mod wins |
| `api` | integer | none | the interface version the mod's code is built against (`docs/mod-abi.md`) |
| `port` | text | none | the port version a mod with raw hooks is built against |
| `code` | relative path | none | the mod's native library for this host |
| `state` | identifier | derived from `name` | the mod's state namespace (`docs/mod-state.md`) |
| `override` | a grammar line, repeatable | none | one raw hook |
| `rule` | a grammar line, repeatable | none | one rule bound to one event |
| `settings` | relative path | none | the mod's settings declaration (`docs/mod-services.md`) |

`name` and `order` are read today; the other keys come with the code loader.
An `order` that is empty, not a number or out of range is reported and the
default stands.

A mod binds its event handlers in its own code, at start-up, through the
registration call (`docs/mod-abi.md`, section 1). The manifest does not list
them.

The `override` grammar:

```
<region>:<address> <mode> <symbol> [sig="<prototype>"] [switch=<name>]
```

- `<region>:<address>` is the key of section 1, for example `ov093:0x021CBA40`.
  A bare address is refused.
- `<mode>` is `replace` or `wrap`. A `wrap` calls the original through
  `bw_mod_slot_original`.
- `<symbol>` is the C symbol the mod's library exports for the hook.
- `sig=` states the prototype the mod expects. When present, the port checks
  it against the slot's prototype and refuses a mismatch.
- `switch=` names the key `bw_hook_enabled` answers. It defaults to the row's
  function name.

The `rule` grammar is `<event> <path>`: a stable event name and a rule file in
the mod (`docs/mod-rules-language.md`).

An example:

```
# Nuzlocke Soul Link
name  = Nuzlocke Soul Link
order = 20
api   = 1
code  = nuzlocke_soullink.dll
state = nuzlocke-soullink

# A raw hook on the catch rate, for a mod that changes the roll.
override = ov093:0x021CBD48 wrap sl_catch_rate

# A rule: lose 1000 money when a Pokemon faints.
rule = battle.faint rules/faint_penalty.rule
```

A packed mod is one `.bw` file with the same tree and manifest inside. The
port unpacks it into its own cache at start-up and reads the directory
(`docs/mod-rules.md`, section 2).

## 4. Events and state

The event and state API is `docs/mod-events.md`: the stable events (section 1),
the handler contract (section 2), several mods on one event (section 3), the
state accessors (section 4) and the raw hooks (section 5). Each stable event
fires from the slot of the function its declared-set row binds.

## 5. The trust model

A mod is native C, loaded into the port's process with the port's full
privilege. It can read and write any memory, call any function, hang and crash
the process. It can defeat the on/off switch and the conflict report, because
both run in the same address space.

The port promises only:

- a deterministic load order, a later-wins rule for raw hooks, and a visible
  conflict report;
- the `bw_hook_enabled` switch over every slot;
- that a build with no mods behaves exactly as a build without the mod
  machinery, because the matched files are never edited and every slot calls
  the original when nothing is installed.

It does not promise isolation, sandboxing, bounded resource use, or validation
of a mod's reads and writes. A mod is trusted like a build of the port by
another author. A mod written only as data and rules (no `code`) runs no native
code and needs no such trust.

## Still to build

1. **The code loader.** Load the `code` library at start-up, call its entry
   point, check `api`, and read the `override` lines (`docs/mod-abi.md`).
2. **ARM9 code-pointer coverage.** Make the ARM9 data relocation consult the
   registry, as `bw_ovl_code_entry` does for overlays (`docs/armdata.md`).
3. **The script command hooks.** One hook on each script VM dispatch, keyed by
   opcode (`docs/mod-events.md`, section 5).

## Related documents

- `docs/modding.md`: the overview of the mod surface.
- `docs/mod-events.md`: the event and state API.
- `docs/mod-overrides.md`: replacing or wrapping one game function.
- `docs/mod-abi.md`: the frozen surface and the interface version.
- `docs/mods.md`: the data layer.
- `docs/hooks.md`: the port's own compiled hooks.
- `docs/overlays.md`: overlay windows, trampolines and the loader.
- `docs/armdata.md`: how DS addresses become host pointers.
