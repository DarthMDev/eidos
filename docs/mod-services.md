# Mod services

Some things a mod needs are not game functions: putting text on screen,
talking to another player's eidos, a timer. The port provides them as
services. A code mod reaches them through the `services` table it receives at
start-up (`docs/mod-abi.md`, "The entry points"); a rule reaches messages and
peer sends through its `message` and `send` actions
(`docs/mod-rules-language.md`). `docs/mod-modes.md`, section 3, lists which
challenge runs use which service.

Each service below says what it does, its calls, and the design choices it
fixes.

## Messages

Show text to the player in the game's own windows.

```c
void (*message)(const char *text);                    /* one window, A to close */
int  (*confirm)(const char *text);                    /* yes/no; 1 for yes */
```

The text appears in the field's talk window, or in the battle message box
during a battle, in the game's font. It is queued: the port shows it at the
next point where the game would accept a message (after the current script
command, or at the next battle message), so a handler never draws over the
game. `confirm` is only allowed in the field; in battle it returns 0 at once.
Text is UTF-8; characters the game font lacks show as `?`. A message from a
refusal handler appears after the action is refused.

## Peer sessions

Connect two or more eidos instances and exchange mod messages.

```c
int  (*session_host)(char *code_out, u32 size);       /* start a session; gets a code */
int  (*session_join)(const char *code);               /* join with a code */
void (*session_leave)(void);
int  (*session_send)(u32 peer, const void *buf, u32 size);   /* peer 0: everyone */
void (*on_session)(BwSessionHandler fn);              /* receive, join, leave */
```

**The flow.** A player chooses "Start session" in the mod menu, or the mod
calls `session_host`. The port connects to the project's relay, which returns
a short code. The other players enter the code, or the mod calls
`session_join`, and the relay links them. This follows Archipelago and the
Minecraft "easy multiplayer" style: nobody opens a port or types an address.

**The relay.** A small server the project runs, which only forwards messages
between the players of one session. The port can also connect two instances
directly when a player gives an address, for a LAN or a self-hosted relay.

**Messages.** A message is up to 4 KB of the mod's own bytes, delivered in
order, at most once, to every player in the session (or one, by peer number).
Each mod receives only messages sent by the same mod on the other side, keyed
by the mod's `name`. The handler runs on the game's frame, never on a network
thread.

**Identity and drops.** Each player has a peer number for the session's
lifetime and a display name. A dropped player is reported to the handler; a
rejoin with the same code gets the same number back.

## Field avatars

Draw another player's avatar in the local field (`docs/mod-coop.md`).

```c
u32  (*avatar_create)(u32 sprite, s16 x, s16 z, u8 dir);    /* 0 when no room */
void (*avatar_move)(u32 avatar, s16 x, s16 y, s16 z, u8 dir, u8 mode);
void (*avatar_delete)(u32 avatar);
```

An avatar is a field entity the port creates in the game's own entity pool,
with an object id outside the game's range and no events, so the local game
never interacts with it. The port moves it smoothly between the positions a mod
sends. Avatars are deleted at each map change; the mod recreates the ones in the
new zone.

## Pokemon export and import

Move a Pokemon between games: from one save to a file, from a file to another
save.

```c
int (*mon_export)(BwMonRef mon, void *buf, u32 size);        /* bytes written */
int (*mon_import)(const void *buf, u32 size, BwMonRef *out); /* into the party, else a box */
```

The exported form is the game's own 0xDC-byte Pokemon record, with the port's
header in front: a magic, a format version and a checksum. Importing checks
the checksum and the game's own record checksum, and refuses a record whose
species, moves or items the game does not know. A Genlocke carries the record
into the next game; Wonderlocke and Egglocke send it through a peer session.

## Run-status panel

A small text panel beside the DS screens, filled by the mod.

```c
void (*panel_set)(u32 line, const char *text);   /* lines 0 to 15 */
void (*panel_clear)(void);
```

The panel is drawn by the port outside the game's screens (to the side in the
stacked layout, below in the side-by-side layout), so it never covers the game
and is not part of a screenshot of the DS screens. Typical contents: deaths,
encounters by route, the level cap, a timer. The player can hide it from the
port's settings.

## Timer and splits

```c
void (*timer_start)(void);
void (*timer_stop)(void);
u64  (*timer_ms)(void);
void (*split)(const char *name);
```

The timer counts real time while the game runs, paused while the port's own
menu is open. A split records the name and the time; the port shows the splits
in the run-status panel and writes them to a file at the end of the run.

## Run restart

```c
void (*run_restart)(u32 flags);   /* BW_RESTART_NEW_GAME, BW_RESTART_DELETE_SAVE */
```

Resets the game the way the port's own reset does and, with
`BW_RESTART_NEW_GAME`, starts a new game directly, without stopping at the
title. With `BW_RESTART_DELETE_SAVE` the save and the mod's saved data are
removed first. Ironmon uses it after a loss; a permadeath mod uses it with the
delete flag.

## Seeded random source

```c
void (*rand_seed)(u64 seed);
u32  (*rand_next)(void);
```

A random number generator for the mod, separate from the game's own, so a
mod's choices do not change the game's rolls. One per mod. Two players who
share a seed get the same sequence, which lets a draft or a random team be the
same on both sides.

## External control

A local endpoint outside tools connect to: a stream overlay, a chat bot, a
crowd-control service.

```c
void (*on_command)(BwCommandHandler fn);              /* text commands in */
void (*publish)(const char *topic, const char *json); /* events out */
```

The port listens on `127.0.0.1` only, on a port chosen in its settings, and
only while a mod that uses the service is enabled. A command is one line of
text the mod interprets; the handler runs on the game's frame. `publish` sends
a JSON message to every connected tool, for overlays that show the run's
state.

## Input injection

```c
void (*input_press)(u32 buttons, u32 frames);
```

Holds the given DS buttons for the given number of frames, as if the player
pressed them. Real input and injected input combine. A chat-controlled mod
turns votes into presses.

## Frame tick

```c
void (*on_frame)(BwFrameHandler fn);
```

Called once per game frame, after the game's own frame work. Co-op uses it to
send positions and move avatars; a timer display uses it to refresh.

## Save handoff

```c
int (*save_send)(u32 peer);     /* send this save to a peer */
void (*on_save_received)(BwSaveHandler fn);
```

Sends the current game save and the mod's saved data to another player in the
session. The receiver's port shows a confirmation, then installs the save and
reloads. A Taglocke passes the run from player to player this way.

## Mod settings

A mod declares options in a small file named by the manifest's `settings` key:

```
option nuzlocke.dupes   bool    "Dupes clause"         on
option nuzlocke.cap     choice  "Level caps"           off|gym|ace
option nuzlocke.panel   bool    "Show run status"      on
```

The port lists them on its settings screen under the mod's name, stores the
player's choices beside the mod's saved data, and the mod reads them:

```c
int (*setting_get)(const char *key);   /* bool and choice index */
```

## The mod menu

The port's settings screen has a Mods section: the installed mods in load
order, each with an on/off switch, its settings, and "Clear saved data". The
order can be changed. Changes apply when the player leaves the menu, following
the rules of `docs/mod-rules.md`, section 2.

## Related documents

- `docs/mod-modes.md`: which runs use which service.
- `docs/mod-abi.md`: the services table and the version rule.
- `docs/mod-coop.md`: co-op and the field avatars.
- `docs/mod-state.md`: the mod's saved data.
- `docs/mod-rules-language.md`: the `message` and `send` actions.
