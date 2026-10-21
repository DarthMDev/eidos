# Function overrides

A raw hook lets a mod replace or wrap one game function. It is the unstable
tier of the mod surface (`docs/modding.md`, section 2): it reaches any
function in `mod/declared_set.txt`, with the game's own arguments. This
document owns how a raw hook is keyed, installed, combined with others and
checked. The slot that carries it is `docs/mod-interface.md`, section 2.

## Keyed by DS address, never by name

A raw hook names its function by `(region, address)`, for example
`ov093:0x021CBD48`. A DS address is fixed for the life of the ROM; a name is
not. aletheia renames functions and re-splits files as its cleanup goes on, and
the port already resolves by address everywhere: the generators bind each
spelling to the definition at its address, and the cross-overlay trampolines
ask which overlay is resident over an address. A hook keyed on a name would
break on the next rename with no warning; one keyed on an address does not.

The region is part of the key because two overlays can share one RAM window:
0x021C5200 is a battle function in ov093 and a Pokedex function in ov208.

The address may be written either way: the even function start the declared
set holds, or the Thumb-tagged value a code pointer carries. The manifest
masks the low bit before the check, so the two spell the same function.

## How a hook is installed

The mod's manifest declares each raw hook with an `override` line
(`docs/mod-interface.md`, section 3): the key, the mode, the symbol the mod's
library exports, an optional prototype and an optional switch name. At load the
port checks that the key is in the declared set, resolves the symbol in the
library and installs the hook with the row's mode
(`bw_mod_slot_install_mode`, `sdk/mod_slot.c`). From then on the function's
slot calls the mod's function.

**Why at run time.** A player installs a mod without rebuilding the port, so a
hook cannot win by being linked in. The slot is the indirection the build puts
at each declared call site for this purpose.

**Modes.**

- `replace`: the mod's function runs instead of the game's. It takes the slot
  for itself: the hooks under it do not run.
- `wrap`: the mod's function runs instead of the game's and calls the next
  function in the chain itself, through `bw_mod_slot_original`. It can act
  before and after, change the arguments, or change the result.

The two modes install the same way: the registry holds a chain of hooks for
one `(region, address)` pair, and every install puts the new hook in front of
the chain, so the chain is the load order reversed. A wrap nests in that
chain; a replace takes the slot for itself, and the links under it are
reported as shadowed.

**The prototype is checked.** The manifest's `sig=` states the
prototype the mod expects, and the port compares it with the function's own:
the generators publish every declared row's name and its normalised
prototype (`tools/bootlink.py`'s `bw_proto_rows[]`, and the overlays' own
table in `tools/ovlink.py`'s `ovl_dispatch.c`), and the apply refuses an
override whose stated prototype differs from it, naming both:

```
eidos: mod mod-e: override arm9:0x02007150 states the prototype 'int (int)',
but the function's is 'int (void *)'
```

A mismatch refuses the override, not the mod: the mod still loads, the way a
declared-address mismatch refuses the row it is on. A prototype the port
cannot read (a game-specific type name) is reported and not compared, so
there is no false refusal. A row the build does not carry publishes no
prototype, and there is nothing to compare.

The normalisation is the same on both sides: `"<return type> (<parameter
types>)"`, the parameters separated by `", "`, `"void"` when there are none.
Qualifiers, the function's name and the parameters' names are dropped; any
pointer, array or function pointer becomes `void *`; an `enum` becomes
`int`; a struct or union by value becomes `unsigned int`; the game's
integer typedefs become their C types. A manifest's writer states the
prototype in that form.

**The switch.** Every hook answers to a switch, and the registry asks
`bw_hook_enabled` (`sdk/mod_hooks.h`) with it, per hook, at the dispatch: a
hook whose switch is off is skipped and the walk goes on inward, so a mod can
be isolated while debugging without the whole chain stepping past the
function. The switch's name is the manifest's `switch=` when the mod states
one, and the declared row's own function name when it does not.

## Several mods on one function

Mods load in ascending manifest `order`.

- **Replaces are exclusive.** When two mods replace one function, the later mod
  wins: the replace takes the slot for itself, the links under it do not run,
  and the port reports them as shadowed, naming the key and both mods. The
  port does not try to combine two replacements, because it cannot know which
  one the player meant.
- **Wraps chain.** Wraps on one function nest in load order: the last-loaded
  wrap runs first, and each wrap's "original" is the next one inward, down to
  the game's own function. A mod that needs to sit on top of another wraps
  rather than replaces. A wrap under a replacement does not run while the
  replacement holds the slot, and runs again once the replacement is taken
  out.

The chain of one pair holds at most eight hooks; a pair that reaches the cap
refuses the install and says so. The way down is the mirror of the way up:
the port takes the mod's own hook out of the chain, so the hooks of the other
mods stay where they are.

**One question, one answer.** The chain is walked from the head on every
call, and `bw_mod_slot_original` does not say which link is asking, so a wrap
that asks twice in one call reaches the second link inward the second time. A
wrap that asks once per call is exact.

Stable events are separate from raw hooks: an event's handlers all run
(`docs/mod-events.md`, section 3), and an event still fires when a raw hook is
installed on the same function.

## Version pinning

A raw hook's arguments are the game's own. When aletheia restructures a
function, its prototype or meaning can change. A mod with raw hooks therefore
states the port version it was built against (the manifest's `port` key), and
the port refuses to load it on another version. The stable tier has no such
pin (`docs/mod-abi.md`, section 4).

The port's own version is a build setting, `-DBW_MOD_PORT_VERSION`, and
`0.0.0-dev` when the build does not set one. The pin is checked for a mod that
has raw hooks and states one: a mod that states no pin is loaded, and a port
that does not know its own version reports the pin and does not check it.

## What the port checks

- **No mod, no change.** With nothing installed every slot calls the game's
  function, so a build with no mods behaves exactly as a build without the mod
  machinery.
- **The registry answers by key.** A hook installed for one `(region, address)`
  is returned for that key only, including for the same address in another
  overlay.
- **The declared set answers too.** A hook for a pair that is not in
  `mod/declared_set.txt` is reported and skipped, and the mod still loads
  (`docs/mod-state.md`, section 4).
- **A wrap reaches the next link.** A wrap's call through
  `bw_mod_slot_original` runs the next hook inward, down to the game's own
  function.
- **Shadowing is reported** with both mods named.

## Limits

- **A call inside the defining file** binds by name and does not pass the slot,
  so a hook misses those calls (`docs/mod-interface.md`, section 2).
- **Code pointers into the ARM9 static module** do not consult the registry
  yet; code pointers into overlays do (`docs/mod-interface.md`, section 2).
- **A prototype the port cannot read** is reported and not compared, so a
  game-specific type name is never a false refusal.
- **An overlay row's prototype** is published only when that overlay is
  compiled in.

## Still to build

1. **ARM9 code-pointer coverage**, as `docs/mod-interface.md`, "Still to
   build", item 3.
2. **The game's own type names** in a prototype: the port resolves the plain
   types and the integer typedefs, but not names like `WMErrCode`, so those
   rows are reported and not compared.

## Related documents

- `docs/mod-interface.md`: the declared set, the slot and the manifest grammar.
- `docs/mod-events.md`: the stable events and the raw hook list by region.
- `docs/mod-abi.md`: versions and what each tier promises.
- `docs/hooks.md`: the port's own compiled hooks, which use the same switch.
- `docs/overlays.md`: overlay windows and trampolines.
