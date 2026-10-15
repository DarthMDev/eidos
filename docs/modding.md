# Modding capabilities

This document is the capability spec for eidos mods. It says what a mod must
be able to do, what the port can do today, and what is missing. It is derived
from four example mods, multiplayer, Nuzlocke, soullink and Nuzlocke soullink:
each capability below is something at least one of the four needs.

Three sibling documents carry the detail this one only points at.
`docs/mods.md` is the user-facing guide to the data surface that exists
today. `docs/mod-overrides.md` is the design for the third action (override
one game function) and is a design, not an implementation. `README.md`, "The
file layer", is the design account of the data surface. Every rule here is
attributed to the file or test that proves it.

## 1. What the four example mods need

The four mods each need to observe things the game does, hold state of its
own, and change or add to what the player sees. The needs are collected in
the table, then spelled out per mod.

| Capability | Nuzlocke | Soullink | Nuzlocke soullink | Multiplayer |
|---|---|---|---|---|
| Override data and assets | yes | yes | yes | yes |
| Override game functions | yes | yes | yes | yes |
| Observe gameplay events | yes | yes | yes | partial |
| Read and write game state | yes | yes | yes | yes |
| Block or allow a game action | yes | yes | yes | yes |
| Show the player something | yes | yes | yes | partial |
| Keep state across sessions | yes | yes | yes | yes |
| Exchange events between instances | no | yes | yes | yes |
| Agree on one shared state | no | yes | yes | yes |
| Carry bytes between instances | no | yes | yes | yes |
| Drive or replace the game's own networking | no | no | no | yes |

### Nuzlocke

Nuzlocke is a self-imposed ruleset, so the mod must see the rule points and
enforce them. It observes a wild encounter starting, a battle starting, a
Pokemon fainting, a catch attempt and its result, a heal, a whiteout, and the
route or area the player is in. It reads party and box contents, species,
level and HP, the current map, and story flags. It blocks an action the
rules forbid: catching more than the first encounter of a route, using or
healing a Pokemon the run has declared dead, and (by rule choice) continuing
after a whiteout. It keeps state across sessions: which route has been used,
and which Pokemon are dead. It shows the player messages, a rules menu, and a
run status. None of this is possible without observing events and holding
state, which is why every other mod below is a superset of the same seam.

### Soullink

Soullink is Nuzlocke for two players whose Pokemon are linked. On top of the
Nuzlocke list it needs a link between one Pokemon in one instance and one in
the other, so that a faint on one side faints the counterpart on the other.
That means two running instances must exchange events and agree on the link
table and the shared run state: the mod must send an event out, receive the
other instance's, and reach the same conclusion on both sides. It also needs
the link table to persist, so a run resumed after a restart keeps its pairs.

### Nuzlocke soullink

This is the union of the two: the Nuzlocke rules applied to the linked set,
across two instances. It needs everything both need, with one shared run
state instead of two independent ones.

### Multiplayer

Multiplayer needs a transport between two or more instances, a session or
lobby around it, and agreement on shared state or at least on the actions
that matter (a battle turn, a trade, a player's position). Because the game
is a single-console design, the mod must either drive the game's own
networking or replace it. The game's networking is matched and available (see
"Networking seam" below), so the raw material is there, but nothing in the
port carries bytes between two machines today. If the mod runs alongside the
game rather than through the game's link menus, it also needs to override
functions at run time and to read state, the same seam Nuzlocke needs.

## 2. What exists today

### Data and asset override: works

A mod is a directory laid out like the data root. There is one base layer
(the data root) and zero or more mod roots in a load order where a later mod
wins (`sdk/vfs.c`: `bw_vfs_file_layer`, `bw_data_path`). Each mod may carry a
manifest, `<mod>/eidos_mod.txt`, with `name` and `order` keys
(`sdk/vfs.c`, `read_manifest`; `sdk/vfs.h`, "The mod list"). A path resolves
to the latest layer holding a regular file there, so a mod ships whole
replacement files (an asset in the data tree, or any whole NARC) with no
repack.

Member-level overrides are the second granularity: a mod may hold a
*directory* at an archive's NitroFS path, and each file named `<index>` or
`<index>.<ext>` replaces or adds one NARC member (`sdk/vfs_narc.c`). Assets
live in the data tree and in NARCs, so this one layer covers both data and
assets. `docs/mods.md` describes the member rules and the merged archive the
port builds.

Proof: `tests/test_vfs.c` (whole-file resolution, no game data) and
`tests/test_vfs_members.c` (member overrides and the load order, three
independent readers; the data half repacks every real archive byte for byte).

### Build-time function override: works

`docs/hooks.md` is the design and it is implemented. A port C file compiled
in place of one matched function overrides it: `stubs/reimpl_<addr>.c` for
ARM9, `stubs/ov/reimpl_ovNNN_func_<addr>.c` for an overlay. For overlays the
`replaces:` header line is mandatory and `tools/ovlink.py` stops without it
(`tools/ovlink.py`, the `replaces` check). A hook is gated at run time by
`bw_hook_enabled` from `sdk/mod_hooks.h`, switched off by `EIDOS_HOOKS_OFF` or
`bw_hook_set` (`sdk/mod_hooks.c`). The `title_settings` hook is a live
example (`docs/hooks.md`, "Hooks in the tree"; `stubs/ov/reimpl_ov088_*.c`;
`sdk/mod_title.c`).

This is a real capability, but it is a build-time one, and its Limits
section constrains the rest of this document (`docs/hooks.md`, "Limits"):

> - A hook is compiled in, so a player-side mod cannot add one at run time. A
>   mod that wants to change data does it through the VFS (member overrides,
>   `sdk/vfs.h`); hooks are for logic. A later step is a data-driven row table
>   (docs/settings-design.md, "What is left") so most menu changes
>   need no hook.
> - A hook replaces a whole function. Two hooks cannot patch the same one.
> - When aletheia renames or re-splits the file, ovlink reports it and the hook
>   must be rebased by hand.

No run-time override exists. `docs/mod-overrides.md` is the design for one
keyed on a DS address.

### Settings and UI: partial

eidos has its own settings screen and a title-menu hook that reaches it
(`docs/settings-design.md`; `engine/platform/settings_ui.c`;
`engine/platform/mod_options_proc.c`; `sdk/mod_title.c`). It is built through
the game's own background, bar tiles, font and print queue, and its model is
separate from its drawing (`docs/settings-design.md`, "What the screen does
now"). What is missing for a mod is the mod-facing part: the row table and the
UI text are still eidos's own, and the text is not yet "a message member mods
can replace" (`docs/settings-design.md`, "What is left"). A mod can replace
or add whole NARC members today (`docs/settings-design.md`, "Labels"), but
there is no mod-supplied screen or menu.

### Networking seam: missing

`sdk/arm7/` is the stand-in for the DS's ARM7. It answers OS, the power
manager, the RTC, the Slot-2 cartridge, the Slot-1 card, the backup chip, the
firmware flash, sound, the microphone, the touch panel and the keypad
(`sdk/arm7/arm7.h`; the directory holds `arm7.c`, `pm.c`, `rtc.c`, `tp.c`,
`ctrdg.c`, `card.c`, `fs.c`, `nvram.c`, `mic.c`, `snd*.c`, `pad.c`).

Nothing answers Wm (wireless, PXI tag 10) or DWC (Nintendo WiFi Connection),
and that is deliberate on the wireless side:

> Wireless (tag 10) has no responder, on purpose. WM_Init's core
> (arm9_sdk/func_0208E038.c) asks PXI_IsCallbackReady(10, 1) once, without
> spinning, and answers WM_ERRCODE_WM_DISABLE when the ARM7 has no wireless
> component: that is the SDK's own "wireless not available" path, the one a
> console with wireless switched off takes, and it is what the port wants the
> game to see. (`sdk/arm7/arm7.h`)

A search of `sdk/` for `Wm`, `WM_`, `DWC`, `WFC` and `NHTTP` finds no
responder and no transport. The only `send`/`recv` in `sdk/` is PXI
(`sdk/pxi.c`: `bw_pxi_arm9_send`, `bw_pxi_arm7_send`), which is the
intra-process ARM9-to-ARM7 wire, not a network. There are no sockets
anywhere in the tree. So two eidos processes cannot talk today.

### The game's own networking, matched and available

The game's networking is decompiled in aletheia and can be hooked or replaced,
which is the raw material a multiplayer mod would use. Counts, measured in the
aletheia working copy (`<home>\OneDrive\Documents\bw_decomp`, revision
`80f06a261e8731f2db94c1a66627bb351ad47673`); they grow as the decompilation
proceeds:

| Search in `src/matched` (`*.c`) | Files |
|---|---|
| `DWC` (any spelling) | 2257 |
| `DWCi_` (the DWC interface symbols) | 1666 |
| `dwc_` (the DWC library internals) | 533 |
| `WM_` (wireless manager) | 260 |
| `NHTTP` (the HTTP client) | 263 |
| a `Wm`-prefixed identifier | 45 |

Concrete examples: `src/matched/arm9_sdk/func_02059FA8.c` is `DWCi_Init`;
`src/matched/arm9_sdk/WmReceiveFifo.c`, `WM_SetMPDataToPortEx.c`,
`WM_StartDataSharing.c`, `WM_StartConnectEx.c` and `ov009_func_021679C0.c` are
the wireless manager; `src/matched/arm9_sdk/nhttpi_initrequestinfo_02061df0.c`
and the `ov114` files are NHTTP. ov009 is "ov009's network library"
(`docs/overlays.md`), and ov016 is WFC (`docs/overlays.md` and
`docs/settings-design.md`, the WFC menu row). None of it is reachable from a
mod today.

### Per-mod state persistence: partial

A writable user directory exists. The save folder, first match wins, is
`bw_set_save_dir`, then `BW_SAVE_DIR`, then the platform default
(`sdk/save.h`, `sdk/save.c`; the default is `%APPDATA%\eidos\saves` on
Windows and the equivalent under the home folder elsewhere, `sdk/plat_save.c`).
The folder is created on demand and files are written whole and atomically
(`bw_plat_mkdirs`, `bw_save_write_atomic`, `sdk/save.c`, `sdk/plat_save.c`).
The port's own settings file lives in the eidos home next to the extraction
(`engine/platform/settings.c`, `eidos_settings_path`; the home is
`install/setup.c`, `bw_setup_home`, `install/setup.h`).

So the mechanism a mod would use to keep state exists: `bw_save_path`,
`bw_save_read` and `bw_save_write_atomic` are in `sdk/save.h`. But it is not
mod-facing. Nothing namespaces one mod's state from another's, nothing hands a
mod the path, and there is no load or unload event at which a mod could read
or write it. A mod that is only data and assets cannot call it at all.

## 3. The capability matrix

| Capability | State today | Proof |
|---|---|---|
| Whole-file data override | works | `sdk/vfs.c`, `sdk/vfs.h`; `tests/test_vfs.c` |
| Asset override (data tree and NARCs) | works | same layer; `sdk/vfs_narc.c`; `tests/test_vfs_members.c` |
| NARC member override (replace or add) | works | `sdk/vfs_narc.c`; `sdk/vfs.h`; `tests/test_vfs_members.c` |
| Mod load order and manifest | works | `sdk/vfs.c`, `read_manifest`; `tests/test_vfs_members.c` |
| Build-time function override | works | `docs/hooks.md`; `stubs/reimpl_*`; `tools/ovlink.py`; `stubs/ov/reimpl_ov088_func_021BB4B4.c` |
| Hook on/off switch | works | `sdk/mod_hooks.c`, `sdk/mod_hooks.h`; `docs/hooks.md` |
| Run-time function override by a dropped-in mod | missing | `docs/hooks.md`, "Limits"; `docs/mod-overrides.md` (design only) |
| Observe gameplay events from a mod | missing | no event API; only a compiled hook can |
| Read and write game state from a mod | missing | no mod API; only a compiled hook can. The field addresses are named in `docs/mod-interface.md`, "State fields a rule may name": the player money is save block id 0x34, offset +0x00, read by `BEACON_GetPoints` 0x0200C274 |
| A constrained rule surface over the event and state API | missing | `docs/mod-interface.md`, "The rule surface"; the `rule` key and the event slot are fixed, the language is not |
| Block or allow a game action | partial | a compiled hook can (`docs/hooks.md`); not run time |
| Per-mod state across sessions | partial | `sdk/save.h`, `sdk/save.c`, `sdk/plat_save.c`; no mod API or namespace |
| Show the player a mod-supplied screen or text | partial | `engine/platform/settings_ui.c`, `engine/platform/mod_options_proc.c`, `sdk/mod_title.c`; message member append planned (`docs/settings-design.md`) |
| Transport between two instances | missing | `sdk/arm7/` has no Wm or DWC responder; no sockets in the tree |
| Exchange events and agree on shared state | missing | same |
| Drive or replace the game's own networking | missing | raw material is matched: `src/matched/arm9_sdk/func_02059FA8.c` (`DWCi_Init`), `WM_*`, `NHTTP*`, ov009, ov016, ov114 |
| Two mods on one function | decided | later in the load order wins and the conflict is reported, `docs/mod-overrides.md` |

### The gap list, in priority order

1. **A run-time seam.** Load a dropped-in mod and let it run code, subscribe
   to events, and act, with no rebuild. Every one of the four mods needs this
   and none of it exists. The build-time hook is the same idea with the wrong
   timing.
2. **A transport between two instances, and a session around it.** Soullink,
   Nuzlocke soullink and multiplayer all need bytes to move between two
   processes. There is nothing today. The game's own Wm and DWC code is the
   raw material; a port transport is the alternative.
3. **A gameplay event and state API.** The observation Nuzlocke is built on
   (encounter, faint, catch, heal, whiteout, route change) has no seam. The
   mod must be able to subscribe and to read party, box, map and flags.
4. **Per-mod persistent state, with a namespace and a lifecycle.** The write
   mechanism exists (`sdk/save.h`); what is missing is a per-mod handle and a
   load and unload point.
5. **A mod-facing UI driven by data.** Messages and menus a mod supplies,
   rather than eidos's own screen (`docs/settings-design.md`, "What is left").
6. **Composition.** Two mods on one function, with the load order and
   later-wins rule the file layer already has, instead of the one hook per
   function limit.
7. **A function override keyed on a DS address.** The design exists
   (`docs/mod-overrides.md`); it depends on items 1 and 6 to be a player-side
   feature rather than a patch build.
8. **A constrained rule surface.** The C API is the only way to act on an
   event. A mod that does not write C needs a surface over the same
   capabilities, so a rule like "on a faint, subtract 1000 from the player
   money" needs no C. The `rule` manifest key and the event slot are fixed
   (`docs/mod-interface.md`, "The rule surface" and "The manifest and pack
   schema"); the language is not.

The single biggest gap is item 1. Because a dropped-in mod cannot run code or
observe events, data and assets are the only thing a player can add today, and
a Nuzlocke mod needs much more than data. The networking seam (item 2) is the
hardest piece, but it is a subset of the same missing run-time seam.

## 4. The constraints that shape any design

These are decisions with reasons, not preferences. They constrain every
option in the gap list.

### Matched files are never edited

The base is compiled unchanged. Because of that, every mod is purely additive
and the base stays provably the original game, which is the port's main
modding asset.

Attribution: `README.md`, "Portability rules", rule 1: "The strong preference
is not to touch `src/matched/` at all: portability belongs in the port layer,
as a shim, a wrapper include that predeclares something before the matched
file's own extern, or an alias forwarder." `docs/mod-overrides.md`, "The base
is never edited, and that is why the design looks like this", draws the
consequence: an override is a host object of its own, it wins by linker
precedence or an explicit exclusion, and a build with no mods must define
every address the way a build with no override machinery does. The port
already works this way: `tools/ovlink.py` leaves a matched file out and
compiles a replacement in its place, it never rewrites one
(`docs/overlays.md`, "What the build lacks: stubs").

### An override is keyed on a DS address, never on a name

A DS address is fixed for the life of the ROM. A name is not: aletheia's
cleanup waves rename and re-split files constantly, so a name-based key
breaks on the next wave with no warning. The address is the key the rest of
the repository trusts: `tools/closure.py` and `tools/bootlink.py` resolve by
address, and `tools/ovlink.py`'s trampolines ask `bw_ovl_resident_at` which
overlay is resident over an address (`docs/overlays.md`, "Calls into a
window"). `docs/mod-overrides.md`, "Override by DS address, never by name",
carries the full argument, including the about one thousand names that repeat
across overlays that share a RAM window and the `sdk/aliases.c` precedent.

The one wrinkle: a Thumb function's code-pointer value carries the low bit,
and the overlay tables key on that form (`docs/overlays.md`, entries;
`docs/mod-overrides.md`, still-to-build item 6). An address-keyed override
must state which form it uses and accept both.

### A player-side mod should not require recompiling the port

The Limits section quoted above states the obstacle: a hook is compiled in,
so a player-side mod cannot add one at run time. A design that only adds
another compiled-in hook does not meet the goal. The data path shows the
target: a mod is a directory a player drops in and `BW_MOD_ROOT` points at
(`README.md`, "The file layer"). Function and event overrides need a seam
with that timing.

### Two mods on one function are a compatibility matter, and modders manage it

Today one hook replaces one whole function and "two hooks cannot patch the
same one" (`docs/hooks.md`, "Limits"). Two mods wanting the same function is
not a port problem to solve, it is a compatibility question the modders
manage. The port's job is only to make the outcome deterministic and visible,
and the rule is the one the file layer already uses for data: a load order, a
`later wins` rule and the manifest `order` key (`sdk/vfs.h`, `sdk/vfs.c`). The
later mod is installed and the earlier one is reported as shadowed. The port
does not compose two replacements. See `docs/mod-overrides.md`, "Two mods on
one address".

## 5. What is still to build

1. **A run-time dispatch seam.** Every capability in the gap list depends on
   this, and the decision is made: a mod's override runs at run time, because a
   user drops it in. Link precedence is build time by nature and would not
   catch a direct intra-overlay call, which binds by name at link time. So the
   work is a run-time dispatch path the game calls through, keyed on the DS
   address (`docs/mod-overrides.md`, "Run time, because a player drops a mod
   in").
2. **Name and key the events.** "A wild encounter is starting" is a
   semantic concept that may span several functions; an address is durable but
   the concept is not one address. The work is to fix how a mod subscribes: by
   address, by a semantic event name, or both.
3. **Authority for soullink.** The work is to fix which instance decides a
   shared outcome, how two conflicting events are ordered, and what happens
   when one instance is ahead.
4. **The transport's shape.** Hook the game's own Wm and DWC, or add a port
   transport and leave the game's networking disabled. The game's networking
   is matched and available, which argues for hooking it; nothing in the port
   carries bytes, which argues for a transport of its own.
5. **Determinism.** The work is to fix how much of two instances' simulation
   must match for multiplayer, and whether the RNG must be shared. Soullink
   needs only event agreement; multiplayer may need lockstep.
6. **Save compatibility across two linked instances.** The work is to fix
   whether a linked run can be resumed if one side's save is older, and how
   the link table survives a version change.
7. **What "block an action" means.** A mod that blocks a catch or a heal must
   either stop the game cleanly, show a message, or revert a change. The work
   is to fix how a block composes with a second mod's block.
8. **Per-mod state namespaces and versions.** The work is to fix how one
   mod's state file is named and isolated, and what happens when a mod changes
   its state format.
9. **Trust and sandboxing.** A mod that runs code is arbitrary host code. If
   the run-time seam runs mod code, the work is to fix whether it is trusted
   or limited.
10. **Composition with the OBJECT-library links.** The boot and overlay links
    have no linker pull rule, so a second definition is an error rather than a
    win (`docs/mod-overrides.md`, "The mechanism"; its still-to-build items 3 and
    4). The work is to fix how a mod's override joins those links.

## Related documents

- `README.md`, "The file layer" and "Portability rules": the data surface and
  the rule that matched files are never edited.
- `docs/mods.md`: the user-facing guide to the data surface that exists today.
- `docs/mod-overrides.md`: the design for a function override keyed on a DS
  address, and its own list of work.
- `docs/hooks.md`: the compiled-in code hooks, the `replaces:` line, and the
  Limits quoted above.
- `docs/settings-design.md`: the settings screen and what a mod-facing UI
  still needs.
- `docs/overlays.md`: the per-overlay namespaces and the loader hook a
  run-time override would attach to.
