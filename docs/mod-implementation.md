# Mod implementation plan

This document is the build plan for the mod layer: where each piece goes, how
the pieces connect, and the order they are built in. The design it builds is
`docs/modding.md` and the documents it points to. The slot seam exists:
`sdk/mod_slot.h` and `sdk/mod_slot.c` hold the `(region, address)` registry,
and the generators emit a slot per row of `mod/declared_set.txt`
(`tools/bootlink.py` and `tools/ovlink.py`, `slot_function`). Everything below
builds on it.

## 1. The event dispatch

### At the slot

Each slot gains two calls around its existing body. The generator emits them
for every row whose fourth column binds a stable event:

```c
int bw_slot_ov093_021CBA40(void *a0, void *a1, u16 a2)
{
    BwEventCall    call = { { (uintptr_t)a0, (uintptr_t)a1, a2 }, 3, 0 };
    BwEventPayload payload;

    if (bw_mod_event_enter(BW_MOD_REGION_OVL(93), 0x021CBA40u, &call, &payload)) {
        return (int)call.ret;          /* refused: the refusal set the result */
    }
    {
        void *h = bw_mod_slot_lookup(BW_MOD_REGION_OVL(93), 0x021CBA40u);
        if (h != NULL && bw_hook_enabled("BTL_SERVER_FLOW_TryThrowBall")) {
            call.ret = ((int (*)(void *, void *, u16))h)(
                (void *)call.a[0], (void *)call.a[1], (u16)call.a[2]);
        } else {
            call.ret = BTL_SERVER_FLOW_TryThrowBall(
                (void *)call.a[0], (void *)call.a[1], (u16)call.a[2]);
        }
    }
    bw_mod_event_leave(BW_MOD_REGION_OVL(93), 0x021CBA40u, &call, &payload);
    return (int)call.ret;
}
```

`BTL_SERVER_FLOW_TryThrowBall(BtlServerFlow *flow, void *thrower, u16
ballItemId)` is the function behind `catch.attempt`; the slot keeps the
definition's prototype. The original is called with `call.a[]`, not with the
incoming arguments, so a handler's modified fields that map onto arguments
reach the game.

`bw_mod_event_enter` looks the pair up in the binding table. For an event that
fires before the function, it builds the payload, runs every handler, and,
when a handler refused, calls the event's refusal function and returns 1.
A refusal replaces the write-back: the refused outcome is what the caller
sees, so the modified fields are not written into a call that is not made.
`bw_mod_event_leave` does the same for an event that fires after the
function, with `call.ret` in the payload. A pair with no binding returns 0
at once, so a raw-hook-only slot costs one table lookup.

### The binding table

```c
typedef struct BwEventBinding {
    unsigned int  region;
    unsigned int  addr;
    u32           kind;      /* BW_EVENT_* */
    u8            point;     /* BW_EVENT_BEFORE or BW_EVENT_AFTER */
    u8            scope;     /* BW_SCOPE_ANY, BW_SCOPE_INSIDE or BW_SCOPE_OUTSIDE */
    unsigned int  scope_region;
    unsigned int  scope_addr; /* the function the scope names */
    BwEventBuild  build;     /* fills the payload from the call */
    BwEventApply  apply;     /* writes modified fields back into the call, or NULL */
    BwEventRefuse refuse;    /* produces the refused outcome, or NULL */
} BwEventBinding;

typedef void (*BwEventBuild)(const BwEventCall *call, BwEventPayload *out);
typedef void (*BwEventApply)(const BwEventPayload *in, BwEventCall *call);
typedef void (*BwEventRefuse)(BwEventCall *call);
```

One row per stable-event binding of `docs/mod-events.md`, section 1. An event
bound to several functions (`heal`, `whiteout`, `mon.receive`,
`battle.action`, `move.learn` and others) has one row per function with the
same `kind`.

**Scope.** Some bindings are general functions that only mean the event when
a particular caller runs them: `BTL_PICK_GetLast` is the player's switch
choice only inside `BTL_CLIENT_StepSelectSwitchTarget`, and
`POKEPARTY_SwapMembers` is a `party.swap` only outside the PC's
`BOX2_MAIN_UpdateBoxSlots`, which fires its own events. The scope function is
itself a slot (a declared row, with or without events), and every slot pushes
its `(region, address)` on the scope stack on entry and pops it on return.
`bw_mod_event_enter` checks the stack against the binding's scope before it
builds anything. The table lives in `sdk/mod_event.c`; the builders, appliers
and refusals live in one file per area (`sdk/mod_event_battle.c`,
`sdk/mod_event_field.c`, `sdk/mod_event_mon.c`, `sdk/mod_event_items.c`,
`sdk/mod_event_session.c`).

### Refusal functions

A refusal function produces the outcome the event's "Refuse" column names. It
runs instead of the game function and leaves the game in the state its caller
expects: it sets `call.ret` to the value a refused action returns, and does
the minimum work the caller depends on. Each refusal is read from the game function's callers (`docs/mod-events.md`,
section 1, and its notes):

| Event | What the refusal function does |
|---|---|
| `trainer.spotted` | returns 0, no event |
| `npc.talk` | returns 0, no person ahead |
| `menu.open` | the function answers no event, and the refusal makes the return 0, so the field check goes on |
| `field.move` | returns 0, the move is unusable |
| `pc.open` | writes 0 to the command's result variable, the value a plain exit leaves, and returns 1 without starting the box process |
| `encounter.wild` | sets the count to 0 at the return of `FIELD_ENCOUNT_CreateZoneWildMons`; for a roamer, makes the return of `FIELD_ENCOUNT_PickRoamer` no roamer |
| `mon.evolve` | returns 0, no evolution |
| `move.learn` | level-up: advances the learnset index past the entry and returns 0xFFFE; daycare: returns without writing; a script: returns 0 without writing, and the command answers 0 in every path of its own, so the script cannot tell the difference |
| `mon.release` | returns without clearing; the release step then redraws the slot |
| `box.deposit`, `box.withdraw` | returns without applying the move; the box screen's own next step redraws the icons, because the redraw inside the move (`BOX2_UI_DrawSlot`) is the overlay's own function and the port's refusal runs in the ARM9 |
| `party.swap` | returns 0 without swapping |
| `trade.ingame` | returns 1 without calling the trade event; the script goes on |
| `battle.action` | fight: returns 1 and fills the message with the game's own "the Pokemon cannot use this move", with the Pokemon and the move as its two arguments; run: returns a blocker with no ability, which takes the "Can't escape!" path; switch: makes the return 6, the "backed out" value |
| `battle.item` | makes the return 0, the "backed out" value |
| `battle.escape` | returns 0, a failed run |
| `catch.attempt` | pushes the block command (0x47) for the target and returns 0, so the ball is not taken from the bag |
| `item.use` | returns 0, "it won't have any effect" |
| `shop.buy` | sets the shop's state to the list again, with no message of its own |
| `heal` | returns 0 without healing (`SCRCMD_PROC_HealParty`); answers 0xFF, the "no power active" value (`GPOWER_UseHpRestore`), and the power is not consumed |

Building each event includes checking its refusal in a running game
(section 6, step 4).

### Fan-out

The dispatch runs every handler bound to the event, in load order
(`docs/mod-events.md`, section 3). Each handler sees the payload as the ones
before it left it. The result is refused when any handler returned
`BW_EVENT_REFUSE`. A refusal at an event with no refusal function is ignored
and reported once in the log.

## 2. Payloads and contexts

A builder fills the payload from the call's arguments, from accessor calls the
port makes at the slot, and from the contexts (`docs/mod-payloads.md`). A
context is a port-owned record of state that is not in scope at the call, for
example the move that caused a faint. The battle context resets at
`battle.start`, the field context at `save.load` and at each map change, and
every context at `bw_port_reset`. A context field with no feeder in the current
situation reads 0, never a value from an earlier situation. The contexts live
in `sdk/mod_context.h` and `sdk/mod_context.c`.

A field that points at memory the game frees later is copied into the
payload, never stored as a pointer.

## 3. The loader

`sdk/mod_loader.c` loads each mod with a `code` key, in load order, after the
save is chosen and before the game runs:

1. Read the manifest and apply it: the `api`, and the `port` version for a
   mod with raw hooks, are checked there, and a mod that does not match is
   refused and never loaded. A mod with code and no `api` is refused too.
2. Open the library: `LoadLibraryW` on Windows, `dlopen` elsewhere.
3. Find `eidos_mod_start` and `eidos_mod_stop`.
4. Call `eidos_mod_start` with the host table (`docs/mod-abi.md`). The mod
   binds its handlers through `host->on`, and the loader holds the bindings
   until the start entry has returned its state pointer.
5. Register the handlers the mod asked for, and install the manifest's
   `override` lines with `bw_mod_slot_install_mode` in the row's mode, after
   the declared check (`docs/mod-state.md`, section 4).

On exit, or when the player disables the mod, the loader reverts the slots the
mod installed (each of the mod's own hooks comes out of the pair's chain,
even one a later mod has shadowed), calls `eidos_mod_stop`, and unloads the
library, in that order.
The namespace is in force for the stop entry, so a state call the mod makes
there reaches its own file and is where a mod flushes its data. The library
is last because a code pointer into it is still live until then.

The event layer holds the handlers a mod bound and has no call to drop the
handlers of one mod, so the port resets the event layer and binds the port's
own rows again before it loads the mods that are still wanted.

## 4. State and services

The state calls of `docs/mod-events.md`, section 4, are thin wrappers over the
game accessors, in `sdk/mod_state.c`, published through `BwModState`. A call
that takes a `BwMonRef` resolves its handle to the game's record and calls the
game's own getter or setter. The mod's saved data is `docs/mod-state.md`. The
port services are `docs/mod-services.md`; each lands in its own file under
`sdk/` and is published through `BwModServices`.

## 5. The script command hooks

The field script VM and the battle effect VM each dispatch commands through
one table. The port routes each dispatch through one hook that looks up the
opcode in a registry and calls a mod's handler when one is installed for that
opcode, else the game's handler (`docs/mod-events.md`, section 5). This needs
one port-side stand-in for each VM's dispatch function, compiled in place of
the matched one, the way the port's own compiled hooks replace a function
(`docs/hooks.md`).

The registry, the two opcode tables and the hook are `sdk/mod_script.c`; the
stand-in that takes the game's dispatch function's place is
`stubs/reimpl_vm_control.c`. The two VMs are told apart by the size of their
command table: the field VM's has 609 entries and the battle effect VM's 78.
The game's two other VMs, the trainer AI's and the station act script's, have
tables of their own and are left alone.

## 6. The build order

Each step has a test that runs without game data unless it says so.

1. **The ABI headers.** `sdk/mod_api.h` and `sdk/mod_state.h`: the types, the
   handler, the entry points, `BwModHost`, `BwModState`, `BwModServices`, and
   the surface hash test (`docs/mod-abi.md`, section 3). Everything else
   compiles against them.
2. **The dispatch core.** `sdk/mod_event.c`: the binding table, enter, leave,
   the fan-out, the registration call. Test: a fake binding and two handlers
   check the order, the chaining of a modified field, and that one refusal
   refuses.
3. **The generator change.** `tools/bootlink.py` and `tools/ovlink.py` emit the
   enter and leave calls for rows with an event, and call the original through
   `call.a[]`. Test: generate into a scratch directory and read the output
   (`docs/mod-interface.md`, section 2).
4. **The events, area by area.** For each stable event: the builder, the
   applier when it has modifiable fields, the refusal function when it is
   refusable, and the binding row. Session and save events first (they need no
   field code), then Pokemon, items, battle and world. Test: per area, a test
   drives the slot with a recorded call and checks the payload; the refusal
   tests run the game function's callers on a test state where the matched
   code allows it.
5. **The state calls.** `sdk/mod_state.c`. Test: read and write a party member
   in a test state.
6. **The mod's saved data and the manifest.** `docs/mod-state.md`, section 6.
7. **The loader.** `sdk/mod_loader.c`. Test: a tiny test mod library that binds
   one event and records it.
8. **The services.** Messages first, because every refusal uses them; then the
   run-status panel, the timer, Pokemon export and import, peer sessions,
   external control and the rest (`docs/mod-services.md`).
9. **The script command hooks.** Section 5.
10. **The rule interpreter.** `docs/mod-rules-language.md`.

Steps 2, 3 and 5 can run in parallel after step 1. Step 4 needs 2 and 3.
Steps 6 to 10 need 1 and can run in parallel with step 4.

### What is built

All ten steps are built.

- **The headers.** `sdk/mod_api.h` (the types, the events, the script
  commands, the host table and the entry points), `sdk/mod_state.h` (the state
  calls and the services table), and the port's own `sdk/mod_event.h`,
  `sdk/mod_context.h`, `sdk/mod_script.h`, `sdk/mod_slot.h`,
  `sdk/mod_hooks.h`, `sdk/mod_rule.h`, `sdk/mod_text.h`,
  `sdk/mod_message.h`, `sdk/mod_net.h`, `sdk/mod_avatar.h`,
  `sdk/mod_export.h`, `sdk/mod_frame.h`, `sdk/mod_handoff.h`,
  `sdk/mod_manifest.h`, `sdk/mod_loader.h`, `sdk/mod_timer.h`,
  `sdk/mod_panel.h`, `sdk/mod_input.h`, `sdk/mod_menu.h` and
  `sdk/mod_title.h`.
- **The dispatch.** `sdk/mod_event.c` (the binding table, the handler table,
  the scope stack, the fan-out and the refusals), `sdk/mod_context.c` (the two
  contexts), and the five area files that hold the rows:
  `sdk/mod_event_session.c`, `sdk/mod_event_items.c`, `sdk/mod_event_mon.c`,
  `sdk/mod_event_field.c` and `sdk/mod_event_battle.c`.
- **The state calls.** `sdk/mod_state.c`, over the game's own accessors.
- **The manifest and the mod's own saved data.** `sdk/mod_manifest.c` (the
  record, the parse, the namespace rules, the declared-address check and the
  apply) and `sdk/mod_savedata.c` (the file, the 24-byte envelope, the
  CRC-32, the versions and the per-mod "in step" answer).
- **The loader.** `sdk/mod_loader.c`: the library, the host table, the
  version check, the handler bindings, the override installation and the way
  back down.
- **The services.** `sdk/mod_message.c` and `sdk/mod_text.c` (the messages),
  `sdk/mod_timer.c` (the timer, the splits and the mod's own random source),
  `sdk/mod_export.c` (a Pokemon's export and import), `sdk/mod_net.c` (the
  peer sessions and the external control), `sdk/mod_frame.c` (the frame tick),
  `sdk/mod_avatar.c` (the field avatars), `sdk/mod_restart.c` (the run
  restart), `sdk/mod_handoff.c` (the save handoff), `sdk/mod_panel.c` (the
  run-status panel), `sdk/mod_input.c` (the input injection) and
  `sdk/mod_menu.c` (the mod menu's model). Every call of the services table
  is bound, and the loader binds them all into each mod's own table
  (`sdk/mod_loader.c`).
- **The script command hooks.** `sdk/mod_script.c` (the registry, the two
  opcode tables and the hook) and `stubs/reimpl_vm_control.c`, the stand-in
  that takes the game's dispatch function's place.
- **The rule interpreter.** `sdk/mod_rule.c`: the parse of a rule file, the
  actions, the cells and their own file, and the peer send, which calls the
  session service when the port has installed no other call.
- **The declared set at run time.** The boot link emits the table of every row
  and `sdk/mod_slot.c` answers from it.

The tests are `tests/test_mod_surface.c` (the surface hash),
`tests/test_mod_event.c` (the dispatch), `tests/test_mod_state.c` (the state
calls), `tests/test_mod_manifest.c`, `tests/test_mod_savedata.c`,
`tests/test_mod_loader.c` (which loads a real library),
`tests/test_mod_declare.c`, `tests/test_mod_messages.c`,
`tests/test_mod_text.c`, `tests/test_mod_script.c`, `tests/test_mod_rule.c`,
`tests/test_mod_timer.c`, `tests/test_mod_export.c`, `tests/test_mod_panel.c`,
`tests/test_mod_input.c`, `tests/test_mod_menu.c`, `tests/test_mod_avatar.c`,
`tests/test_mod_handoff.c`, `tests/test_mod_net.c` and
`tests/test_mod_restart.c`.

The script command's view carries the operands as bytes, sixty of them, which
is the widest command of the two tables (the battle effect VM's opcode 8, at
60 bytes; the field VM's widest is 22), so a handler reads and writes every
operand, including the odd-width ones a word view cannot round-trip.  The
mod's own saved data answers whether it is in step with the save in force
through a service call, because that answer is per mod and the `save.load`
payload is built once for all the handlers of the event.

What is still to build:

1. **The nickname's force.** `mon.nickname`'s `force` is marked, but the
   row that carries it has no applier, so a write to it has no effect
   (`docs/mod-payloads.md`, section 0). The game has no call that forces
   the prompt, so the hook the port would need does not exist.
2. **The relay server.** The protocol is fixed (`docs/mod-services.md`,
   "Peer sessions"), but the server that answers it is not part of the port.

The port's own seams are in place: the boot installs the run restart's
handler, the save handoff's reload and the handoff's confirmation
(`demo/eidos_boot.c`), it hands the network service the transport, the
relay's address, the player's name and the control's port, and the boot's
frame hook runs the mod layer's frame tick, so the mods' handlers and the
network's queued events reach the mods on the game's frame.

What the areas owe the boot: the battle half of the state API and
`FIELD_PLAYER_GetPos` are not in the boot's closure, and the port has to bind
the game's live objects (the game data, the battle, the field player) where
it learns them.

## Related documents

- `docs/modding.md`: the overview.
- `docs/mod-events.md`: the events, the handler contract and the state values.
- `docs/mod-payloads.md`: the payload fields and the contexts.
- `docs/mod-abi.md`: the headers and the entry points.
- `docs/mod-state.md`: the mod's saved data and the manifest at run time.
- `docs/mod-services.md`: the port services.
- `docs/mod-interface.md`: the slot and the declared set.
