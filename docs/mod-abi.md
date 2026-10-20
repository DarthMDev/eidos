# Mod ABI

A mod with code is a native library compiled against the port's headers. It
reads the port's structs at a fixed layout and calls the port's functions with
fixed signatures, so the port owns a binary interface. This document lists
that surface, what is outside it, how the interface version works and what the
port promises across versions. `docs/mod-events.md` owns the events the
surface carries and `docs/mod-payloads.md` the payload fields.

## 1. The surface

The surface lives in two headers: `sdk/mod_api.h` for the types and the entry
points, and `sdk/mod_state.h` for the state and service calls. Everything a
mod compiles against is in them, and nothing else is part of the ABI.

### The vocabulary

```c
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed int     s32;
```

Every payload struct uses these fixed-width types and holds no host
pointer, so a payload's layout is the same on every host the port supports.
The three tables a mod is handed (`BwModHost`, `BwModState`,
`BwModServices`) hold function pointers, so *their* layout follows the
pointer width; a mod is a native library built for the port's platform, so
it shares that width, and the tables are reached through pointers rather
than copied.

### The event header

```c
typedef struct BwEvent {
    u32 size;   /* sizeof the concrete payload, as the port built it */
    u32 kind;   /* the BW_EVENT_* id of the event */
} BwEvent;
```

Every payload starts with `BwEvent`. A handler reads `kind` to know the event
and `size` to know how much of the payload the port filled; a payload only
grows at the end within a version.

### The Pokemon reference

```c
typedef struct BwMonRef {
    u32 handle;   /* opaque; passed back to the state calls, never read */
    u8  place;    /* BW_MON_PARTY, BW_MON_BOX, BW_MON_BATTLE, BW_MON_WILD */
    u8  side;     /* in battle: 0 the player's side, 1 the opponent's */
    u8  box;      /* in a box: the box number */
    u8  slot;     /* the position in the party, the box or the battle side */
} BwMonRef;
```

A payload names a Pokemon with a `BwMonRef`. `handle` is the game's own
reference (a party or box record, or a battle record) and is only ever passed
back to the state calls, so the game's record layout stays out of the ABI.
`place`, `side`, `box` and `slot` say where the Pokemon is, so a mod can
compare two references and store one in its own data.

### The payloads

One struct per stable event, `BwEvent` first (`docs/mod-payloads.md`). Its
fields are frozen within a version, and new fields are only added at the end.

### The handler and its return

```c
typedef int (*BwEventHandler)(void *mod_state, BwEvent *event);

enum {
    BW_EVENT_CONTINUE = 0,
    BW_EVENT_REFUSE   = 1
};
```

The contract is `docs/mod-events.md`, section 2: a handler may write the
fields its event marks as modifiable, and returns `BW_EVENT_REFUSE` to refuse
an event that defines a refused outcome.

### The event ids

One `BW_EVENT_*` constant per stable event, named after the event in upper
case: `battle.faint` is `BW_EVENT_BATTLE_FAINT`. An id never changes and is
never reused.

### The entry points

A mod library exports two functions:

```c
int  eidos_mod_start(const BwModHost *host, void **mod_state);
void eidos_mod_stop(void *mod_state);
```

`eidos_mod_start` runs once when the port loads the mod, after the save is
chosen and before the game runs. It receives the host table and stores its own
state pointer; that pointer is the `mod_state` every handler receives. It
returns 0, or a non-zero value to tell the port to unload the mod.
`eidos_mod_stop` runs when the port unloads the mod or exits.

`BwModHost` is a table of function pointers the port fills: the interface
version, the registration call, the state calls and the service calls.

```c
typedef struct BwModHost {
    u32  api;                                   /* the port's interface version */
    int  (*on)(u32 kind, BwEventHandler fn);    /* bind a handler to an event */
    const struct BwModState    *state;          /* sdk/mod_state.h */
    const struct BwModServices *services;       /* docs/mod-services.md */
} BwModHost;
```

A mod calls `host->on` from `eidos_mod_start` for each event it handles. The
table is the only way into the port, so a mod links against nothing but the
host executable's exported table, and one library works with any port build
of the same version.

### The state and service calls

`sdk/mod_state.h` declares `BwModState`, one function per value of
`docs/mod-events.md`, section 4: party and box reads and writes, a Pokemon's
fields by `BwMonRef` and field id, money, badges, flags, the map and the
player position. `BwModServices` declares the port services of
`docs/mod-services.md`: messages, peer sessions, the mod's saved data, the
timer and the others. Each name and signature is frozen within a version.

## 2. What is not in the surface

| Not in the surface | Why |
|---|---|
| The game's own structs (party and box records, battle records, save blocks) | aletheia restructures them; a mod reaches them only through the state calls |
| Raw hooks and their arguments | they are the game's functions, so they change with aletheia; a mod with raw hooks is pinned to one port version (`docs/mod-events.md`, section 5) |
| The DS addresses behind the stable events | the port's binding, which may move without a mod noticing |
| The rule language | it has its own version (`docs/mod-rules-language.md`) |
| The `.bw` container format | kenon owns it; the port only reads it |

## 3. The version

`api` is one integer, the major version of the surface. A mod states the
version it was built against in its manifest, and the port also reads it from
the mod's start call. The port loads the mod when the two match and refuses it
otherwise, with a message naming both numbers. A mismatch means the mod reads a
struct at a layout the port does not have, which fails silently, so a warning
is not enough.

The number cannot be forgotten. A test hashes the surface: every struct's size
and field offsets, every enum value, every event id and name, and every state
and service signature. The expected hash is stored beside the version number.
A change to the surface fails the test until the number is raised and the hash
updated in the same change. A comment or a change outside the headers does not
move the hash.

The three tables hold function pointers, so their layout follows the pointer
width and the hash follows it: the number beside the version number is the
32-bit one, the port's reference configuration, and the test carries the other
widths' numbers as well, so a change fails on every width the port is built
at. The payload structs, the ids and the enums hold no pointer and hash the
same everywhere.

## 4. What the port promises

- **Within a version** a mod runs unchanged, with no rebuild: event names,
  ids, payload layouts, the handler type and every state and service call stay
  as they are. Fields may be added at the end of a payload.
- **Across versions** a mod rebuilds against the new headers. Its source
  survives where it uses the stable parts: event names, `BwMonRef`, and the
  state calls. Only code that reads a changed payload field needs an edit.
- **aletheia's restructuring** does not reach a mod that uses only the stable
  tier. Renames and re-splits move the port's bindings and the game's structs,
  both of which the surface hides. It reaches raw hooks, which is why those
  are pinned to a port version.

## Still to build

1. **The loader.** Load the library, check `api`, call `eidos_mod_start`, and
   bind the handlers (`docs/mod-implementation.md`).

## Related documents

- `docs/mod-events.md`: the stable events, the handler contract and the state
  values.
- `docs/mod-payloads.md`: the fields of each payload.
- `docs/mod-services.md`: the port services.
- `docs/mod-interface.md`: the slot, the manifest and the trust model.
- `docs/pointer-width.md`, `docs/lp64.md`: the integer widths the port uses.
