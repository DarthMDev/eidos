# Function overrides

This is the design for overriding an individual game function, the third
capability a mod needs after adding files and editing data. It is a design,
not an implementation: nothing here is built yet, and the work still to do is
listed at the end.

What it must do: a mod names a game function and
supplies a replacement, and the game calls the replacement instead. The
existing code hooks (`docs/hooks.md`) already do this for port code compiled
into the build. This design is about a mod doing it, and about the key being
the function's DS address rather than its name.

## The decisions

### Override by DS address, never by name

An override is keyed on the function's DS address, which is fixed and stable
for the life of the ROM.

The reason is the project the port consumes. aletheia renames functions and
re-splits units on every cleanup wave; the port already lives with that. In
`tools/ovlink.py` the renames are visible as the about one thousand names
that repeat across overlays that share a RAM window (`func_02155244` is in
ov009 and another in ov010; `docs/overlays.md`, "The problem"). And
`sdk/aliases.c` exists precisely because two matched files spell one address
two different ways, and its header records that the fix is "a naming
convention in src/matched/", the integrator's call.

A name is not stable; the address is. The matched code is byte-verified
against the ROM and sits at the same DS address on every build, so an
address is a durable key. An override keyed on a name breaks on the next
wave with no warning; one keyed on an address does not.

This is the shape the whole repository already uses: `tools/closure.py` and
`tools/bootlink.py` resolve names by address, `sdk/overlay.c` and
`sdk/armdata.c` walk the DS address space, and `tools/ovlink.py`'s
trampolines ask `bw_ovl_resident_at` which overlay is resident over an
address. Overriding by address is the same key those pieces already trust.

### The base is never edited, and that is why the design looks like this

eidos never patches a matched file (`README.md`, portability rule 1). The
matched C is compiled unchanged, and its byte-identity with the ROM is the
port's core asset.

That rule is also the reason an override can be a wholly separate object.
Because the base is untouched, an override is purely additive: a mod
contributes one more definition at one address, and with no mod present the
link is exactly the link that exists today. The base stays provably the
original. Every decision below follows from keeping that property:

- the override is a host object of its own, never an edit;
- it wins by linker precedence or by an explicit exclusion list, never by
  rewriting a matched file;
- a build with no mods must produce the same definition for every address as
  a build with the override machinery absent.

### The mechanism: a mod's object wins by linker symbol precedence

The port links a mod's replacement object before the game objects, so the
replacement wins. This is the mechanism N64Recomp and Zelda64Recomp use.
N64Recomp's README states it directly: because ordinary linkers pull a
symbol only if it is not already defined, "providing the recompiled patches
to the linker before providing the original recompiler output will result in
the patches taking priority"
(`reports/SM64 and Ship of Harkinian ports.md`, "Moddability mechanism").
Zelda64Recomp marks a patched function `RECOMP_PATCH`.

eidos already relies on this rule. `stubs/` is one symbol per file on
purpose: "a static-library member is only pulled in when nothing else
defines its symbol, which is how `tests/test_stats.c` supplies its own
record accessors without displacing anything else"
(`CMakeLists.txt`, the `BW_STUB_SOURCES` comment; the substitution itself is
described in README.md, "What is substituted, and why"). An override is the
same trick with a mod's object in place of a test's.

Where the objects come from, for a source build:

| Code | How the game's function is built | How an override wins |
|---|---|---|
| ARM9 | the matched C under `src/matched/` compiled into the `eidos` static archive (`CMakeLists.txt`, `add_library(eidos STATIC ...)`) | the mod's object is passed to the linker before the `eidos` archive, so the archive member that also defines the symbol is never pulled |
| boot | `tools/bootlink.py` writes one wrapper per matched file and the generated `boot_link.c`; all of it is in the `eidos_boot_game` OBJECT library, so every object is in the link (`cmake/boot.cmake`) | linker precedence does not apply to an OBJECT library: the matched file must be excluded from the source list, the way `EIDOS_BOOT_SHIM_REPLACED` excludes a file a shim replaces |
| overlay NNN | `tools/ovlink.py` compiles the overlay's matched files under the `ovNNN_` namespace and generates the trampolines | the matched file at the address is left out of the overlay's unit list and the override is compiled as that overlay's unit, which is what `tools/ovlink.py` already does for a ` * replaces:` port reimplementation (`_port_standins`) |

The two OBJECT-library cases (boot and overlay) matter and are the reason
the design cannot be only "link the mod object first". An object library has
no pull rule, so a second definition is a duplicate-symbol error, not a win.
Those two seams need an exclusion, and the exclusion already has a shape:
`cmake/boot.cmake` keeps `EIDOS_BOOT_SHIM_REPLACED`, and `tools/ovlink.py`
reads a ` * replaces:` line out of the replacement file's own header. An
override registry fills the same slots, by address instead of by file path.

### Where the hook lives: build on what exists

Two existing pieces are the right places to attach, and neither should be
duplicated.

**`sdk/aliases.c`, the address's canonical name.** `sdk/aliases.c` is the one
place the port reconciles the spellings of a single address. It defines
`func_02034734` as a forwarder to `sub_02034734`, and so on, so both
spellings reach the one definition. An override for an address that already
appears here is the *winning definition* of that address's name: the same
linker precedence that makes `func_02034734` forward today makes an
override's definition win tomorrow, and every spelling of the address
already routes through the one name. An address that has only one spelling
needs no alias; the override defines that spelling.

This is why the address is the key and `aliases.c` is the seam: the port has
already decided that one address is one name, whatever the source files call
it.

**`bw_overlay_set_provider`, the loader's run-time hook.** `sdk/overlay.c`
calls the installed provider's `compiled`, `load` and `start` functions
(`BwOverlayProvider` in `sdk/overlay.h`). `sdk/overlay_image.c` installs one
through `bw_ovl_install`; `FS_LoadOverlayImage` calls `provider->load` in
place of the image hook, and `FS_StartOverlay` calls `provider->start` in
place of the table's static initialisers. A per-address override needs to be
visible at three existing run-time questions:

- which overlay is resident, `bw_ovl_resident_at(addr)`, which every
  trampoline asks (`sdk/overlay_image.c`);
- which host function a code pointer holds, `bw_ovl_code_entry(value)`, which
  `sdk/armdata.c` uses when it relocates the ARM9 data's pointers into a
  window (the save block table points into ov010);
- `provider->load`, where the overlay's own data is bound and relocated, and
  where a definition selected by address could be published.

The provider is a single slot today, owned by `sdk/overlay_image.c` (see the
still to build). The design is an override registry the provider consults,
or a field added to the provider, not a second provider installed over the
first.

### What an override declares

| Field | Meaning |
|---|---|
| address | the DS address of the function, the same value `tools/closure.py` and the overlay tables key on. Where the function is Thumb, state whether this is the raw start or the value a code pointer holds (the Thumb bit the overlay tables carry) |
| signature | the prototype the *caller* expects, not necessarily the replaced function's own. A mismatch must be refused, not called through (see below) |
| mode | **replace** the whole function, or **wrap** it (call the original, then change the return or the side effects) |
| origin | ARM9, or an overlay id, so the registry knows which link seam and which window apply |
| switch | whether an override is always on or named, for the existing on/off mechanism |

The `signature` field has a working precedent: `tools/ovlink.py`'s
trampolines normalise a prototype to types every host passes alike and panic
when the resident overlay's function has another prototype, rather than
calling through the wrong type (`docs/overlays.md`, "Calls into a window").
An override registry should reuse that normaliser, not invent a second one.

The `switch` field has a working precedent too: `sdk/mod_hooks.h` gives a
compiled hook a name, and `bw_hook_enabled(name)` is 1 unless the name is in
`EIDOS_HOOKS_OFF` or was switched off with `bw_hook_set(name, 0)`. An
override that changes behaviour should answer the same question, so one
mod's override can be isolated while debugging.

### How a mod is verified

A test can assert an override without disturbing the port's own tests,
because of the same static-archive rule the mechanism rests on:

- **The replacement wins.** Link a test object that defines one function's
  symbol before the `eidos` archive and assert that the test's version runs.
  This is exactly what `tests/test_stats.c` already does for three matched
  functions, so the pattern is proven.
- **The base is unaffected.** A test that installs no override must get the
  same definition for every address as a build without the registry. The
  port can assert this about its own link (the closure is unchanged, no
  second definition, the base objects are the matched ones). Byte-identity
  with the ROM stays aletheia's job; the port's own assertion is that the
  base link is what it was.
- **The registry answers by address.** Install an override for a synthetic
  address and assert `bw_ovl_resident_at` / `bw_ovl_code_entry` (or their
  registry equivalent) answer it for that address and not for another. This
  needs no game data and no matched code, the way `tests/test_ovlink.c`
  builds a synthetic overlay tree.
- **A wrong signature is refused.** Feed an override whose prototype differs
  from the matched caller's and assert a refusal (a build error, or a panic
  naming the address and both prototypes), as the trampoline normaliser
  already does for a resident overlay.
- **A wrap calls the original.** If wrapping is built (the wrapping item below),
  a test asserts the original ran, by a side effect or a recorded call, in
  addition to the override's change.

### Two mods on one address: later in the load order wins, and the conflict is reported

Two mods overriding one address is a compatibility matter, not a port problem,
and the modders manage it. The port's job is to make the outcome deterministic
and visible, not to compose two replacements automatically.

- **Deterministic**: the rule the data layer already uses applies. Mods load in
  ascending `order` (the `eidos_mod.txt` manifest, `sdk/vfs.h`), and a later
  mod wins. Two overrides of one address are therefore not a duplicate symbol
  at link time: the later one is installed, and the earlier one is reported as
  shadowed.
- **Visible**: the conflict is reported at load or pack time, naming the
  address, the winning mod and the shadowed one, so a modder sees it without
  reading a link map.
- **Not composed**: the port does not chain two replacements. There is no
  general way to know whether the second wants to run before, after or instead
  of the first. A mod that needs to sit on top of another depends on it
  explicitly and wraps it, which is the wrapping item below.

## Still to build

1. **Wrapping, and how the original stays callable.** Replacing a symbol is
   straightforward. Wrapping needs the original function to still exist
   under some symbol, so the override can call it. Options include keeping
   the matched object and giving its symbol a private name (the way
   `tools/bootlink.py` and `tools/ovlink.py` give a second copy of a name a
   private name), or routing the caller through a function pointer the
   override can chain. The port must choose one.
2. **Extend the dispatch seam to a run-time override.** The decision is above:
   a mod's override runs at run time, so the dispatch path must consult a
   registry. The work is to extend the seam to the addresses a mod needs to
   reach, through `bw_ovl_resident_at` and `bw_ovl_code_entry` for the overlay
   windows and the equivalent for the ARM9 image, and to decide what a call
   that binds directly inside one translation unit does, since the linker
   resolves it and it never reaches a registry.
3. **Provider composition.** `bw_overlay_set_provider` holds one provider and
   `sdk/overlay_image.c` owns it through `bw_ovl_install`. A mod's registry
   must attach without displacing the loader, as a field on the provider or
   as a registry the provider calls.
4. **A generated alias without editing port code.** `sdk/aliases.c` is
   hand-written port code. Routing an override's address through the
   canonical name may need a generated forwarder or a new generated file,
   since adding a line to `sdk/aliases.c` per mod is not additive and not
   something a mod should do.
5. **Code pointers in data, and the Thumb bit.** A code pointer stored in
   ARM9 or overlay data is relocated by address (`sdk/armdata.c`,
   `bw_ovl_code_entry`). An override must be what such a pointer resolves to,
   and Thumb functions store the address with the low bit set. The registry
   must state which form it keys on and match both.
6. **What the port verifies about the base.** aletheia proves byte-identity
   against the ROM. The port must fix what it asserts about an override build:
   that the closure is unchanged, that no matched object was replaced except
   by an override, and that the count of definitions at each address is one.
7. **Naming and the next wave.** If an override is a mod object and the
   registry maps address to symbol, aletheia's next cleanup wave can rename
   or re-split the file without moving the address. The port must detect a
   wave that moves something the override depends on (the same report
   `tools/ovlink.py` already writes for a ` * replaces:` file that replaces
   nothing).
8. **One symbol per file.** `stubs/` is one symbol per file for the
   static-archive rule to work. Two overrides in one object would break it.
   The port must fix whether the registry requires one object per address or
   generates a separate object per override.
9. **The switch, and isolation.** `sdk/mod_hooks.h` switches a compiled hook
    off by name. The port must decide whether an override reuses that exact
    mechanism and how a mod names its override.

## Related documents

- `README.md`, "The file layer" and "Portability rules": the mod data surface
  and the rule that matched files are never edited.
- `docs/hooks.md`: the existing compiled-in code hooks, their ` * replaces:`
  line, and the limit that a player-side mod cannot add one at run time.
- `docs/overlays.md`: the per-overlay namespace, the trampolines, and the
  `bw_overlay_set_provider` loader hook.
- `docs/armdata.md`: how DS addresses become host pointers, which is what a
  code-pointer override has to join.
- `docs/mods.md`: the file and data surface an override sits beside.
