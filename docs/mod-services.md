# Mod services

Some things a mod needs are not game functions: putting text on screen,
talking to another player's eidos, a timer. The port provides them as
services. A code mod reaches them through the `services` table it receives at
start-up (`docs/mod-abi.md`, "The entry points"); a rule reaches messages and
peer sends through its `message` and `send` actions
(`docs/mod-rules-language.md`). `docs/mod-modes.md`, section 3, lists which
challenge runs use which service.

Each service below says what it does, its calls, and the design choices it
fixes. The table is `BwModServices` (`sdk/mod_state.h`), and the loader gives
every mod a table of its own (`sdk/mod_loader.c`): the port says which mod is
calling by putting that mod's namespace in force around every call it makes
into a mod, so a service that keeps state keeps it per mod. Every call in the
table is bound, so a mod can call any of them; a mod built against another
version of the surface checks the pointer first, because a call the port does
not have is `NULL` in the table.

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

The service is `sdk/mod_message.c`, the queue and the encoding, and
`sdk/mod_text.c`, the port's own text path that puts the text on screen. The
queue holds eight texts, and one text may be 128 code units; a longer text is
cut at the cap. The game's text is a run of 16-bit code units, one per
character, with 0xFFFF ending a string and 0xFFFE a line break, and the
service encodes the UTF-8 into those units. The answer to "does the game font
have this character" is the font's own glyph table, which the port reads
through the game's own accessor, with the font the text path makes (arc 0x17,
member 0). With no game under the port there is no font, and the answer is
the built-in one the message service keeps: the printable ASCII range. The
showing is the port's text path: it builds the game's string object out of
the code units and prints it with the game's own print queue in the window
the game has open, the field's talk window or the battle's message box.

A confirm waits for the answer. The question goes into the queue with the
other texts, the wait advances the game to the next message point, the port's
text path asks the question in the field's yes/no window, and the answer
comes back to the caller. The wait gives up after 3600 ticks and answers no.
With no text path that can ask, a confirm answers no at once.

## Peer sessions

Connect two or more eidos instances and exchange mod messages.

```c
int  (*session_host)(char *code_out, u32 size);       /* start a session; gets a code */
int  (*session_join)(const char *code);               /* join with a code */
void (*session_leave)(void);
int  (*session_send)(u32 peer, const void *buf, u32 size);   /* peer 0: everyone */
void (*on_session)(BwSessionHandler fn);              /* receive, join, leave */
```

**The flow.** A mod starts a session: it calls `session_host` and the port
connects to the project's relay, which returns a short code. The other
players join with `session_join` and that code, and the relay links them.
This follows Archipelago and the Minecraft "easy multiplayer" style: nobody
opens a port or types an address.

**The blocking call.** `session_host` and `session_join` talk to the relay
and wait for it to answer, so they block the thread that calls them, for up
to three seconds. A mod that calls one of them from a handler holds the
game's frame for that time. The port's own Session section does not: it
makes the call on the port's own worker thread and the screen polls it, so
the game keeps running while the relay answers.

**The relay.** A small server the project runs, which only forwards messages
between the players of one session. The port can also point the client at
another relay, for a LAN or a self-hosted one: the relay's address and its
port are the port's own settings, and the port hands them to the service
before the game runs. The transport under the client is an interface the port
fills (a connect, a send, a receive, a close), and the port fills it with the
plain TCP transport the service provides. A port that installs no transport
gets the default one, which refuses to connect and says "no relay".

**The protocol.** The client speaks it and the relay has to answer it. It is
a stream of frames, each one a type letter, the payload's length in decimal,
a newline, and the payload:

```
<type> <length>\n<payload>
```

The client sends:

| Frame | Payload | Meaning |
|---|---|---|
| `H` | `<version> <token> <name>` | host a session; the relay answers with a code |
| `J` | `<version> <token> <code> <name>` | join a session |
| `D` | `<peer>\n<key>\n<bytes>` | a mod's message, to one peer or to every peer (peer 0) |
| `B` | empty | leave the session |
| `K` | empty | keep-alive: sent after 20 seconds without another frame (the relay drops a client that is silent for 60) |
| `Q` | `<peer>` | the host removes a player; the player is told `E removed` |

and the relay sends:

| Frame | Payload | Meaning |
|---|---|---|
| `C` | `<code>` | the session's short code |
| `W` | `<peer> <name>` | you are in; this is your number |
| `P` | `<peer> <name>` | a peer joined |
| `X` | `<peer>` | a peer left, or was dropped |
| `D` | `<peer>\n<key>\n<bytes>` | a mod's message |
| `E` | `<reason>` | an error; the session is over |

The relay's rules, which the client relies on: it gives the host a short code
and tells the host the code; it tells every client its own peer number and
every client about each join and each drop; it forwards a message to one peer
or to all the others, in the order it was sent, once each, and the peer
number in the message it forwards is the sender's own, not the target the
sender named; it keeps a peer's number for the session's lifetime, so the
same token joining the same code again gets the same number back; and it
refuses a join with an unknown code with `E`.

The relay is metaxy (its own repository; its `docs/SPEC.md` is the relay's
side of this protocol). `<version>` is the text `eidos-net/1`. The token is
32 hexadecimal digits from the operating system's random source, one per
session code, kept in `net_tokens.txt` in the save folder, so a player who
comes back to a code (after a drop or a restart) is the same peer. The
client upper-cases a code and drops the spaces and dashes a player types. A
frame of any length, 0 included, is read; a type the client does not know is
read past. A mod with no namespace cannot send (`E_ARG`): its key would be
empty. The client speaks plain TCP; TLS is to come.

The port's own multiplayer messages (the lobby, the saves, the positions)
travel as `D` frames under the key `eidos:port`, which no mod can have: a
mod's key is its namespace, and a namespace has no `:`. The port's handler
(`bw_mod_net_set_port_handler`) gets those messages, every join and leave
before the mods do, and the end of the session with its reason (`lost`, or
the relay's error such as `removed`).

**What the port has to know to reach the relay:** the host (a name or a
dotted address), the port, and the session's code, which the relay hands out
and the players type. The port sets the first two from its own settings and
the third comes from the relay's answer.

**Messages.** A message is up to 4 KB of the mod's own bytes, delivered in
order, at most once, to every player in the session (or one, by peer
number). Each mod receives only messages sent by the same mod on the other
side, keyed by the mod's own name, which is its state namespace. The handler
runs on the game's frame, never on a network thread: the session's reader
thread queues what it read and the frame tick drains the queue and calls the
handlers. A message handler runs with the mod's own namespace in force, so a
state call it makes reaches its own file. A join and a leave are told to
every mod that asked for the session's events, and they run with no mod's
namespace in force.

**Identity and drops.** Each player has a peer number for the session's
lifetime and a display name. A dropped player is reported to the handler; a
rejoin with the same code gets the same number back.

**The answers.** The session calls name their reason, so a mod can tell "the
relay is not there" from "the message was too big" without reading the port's
log: 0 for success, 1 for no relay, 2 for a session already running, 3 for
no session, 4 for the relay refusing or not answering, 5 for a message over
4 KB, 6 for a failed connection, 7 for a bad argument.

## Field avatars

Draw another player's avatar in the local field (`docs/mod-coop.md`).

```c
u32  (*avatar_create)(u32 sprite, s16 x, s16 z, u8 dir);    /* 0 when no room */
void (*avatar_move)(u32 avatar, s16 x, s16 y, s16 z, u8 dir, u8 mode);
void (*avatar_delete)(u32 avatar);
```

An avatar is a field entity the port creates in the game's own entity pool,
with an object id outside the game's range and no events, so the local game
never interacts with it. The port moves it smoothly between the positions a
mod sends. Avatars are deleted at each map change; the mod recreates the ones
in the new zone.

**The pool.** The game's field entity pool holds 64 records, and the port
leaves **eight of them free** for the zone's own people, which the game
creates after the avatars are there. The port asks for the free slot itself
and answers 0 when there is none, rather than taking a slot the game needs.
Its own table holds sixteen avatars, so the pool's headroom, not the port's
table, is what limits a session. An avatar's object id is 0x8000 plus its
index, outside the game's own range (the zone's people have the ids of the
event data and the player has 0xFF).

**The sprite.** The sprite is the game's own object code: 1 is the male hero
and 4 the female heroine. The codes the game takes from the zone's own event
data, 162 to 177, are refused, because the game replaces those with the
sprite the zone names. The direction is the game's own: 0 up, 1 down, 2 left,
3 right.

**The move.** The avatar walks to the cell the mod named; it does not jump.
The mode is how the other player moves: 0 walking, 1 running, 2 cycling,
3 surfing. A handle the port does not know is ignored.

## Pokemon export and import

Move a Pokemon between games: from one save to a file, from a file to another
save.

```c
int (*mon_export)(BwMonRef mon, void *buf, u32 size);        /* bytes written */
int (*mon_import)(const void *buf, u32 size, BwMonRef *out); /* into the party, else a box */
```

**The exported form.** The port's own 16-byte header in front of the game's
own 0xDC-byte Pokemon record, so a file is 252 bytes:

| Offset | Size | What |
|---|---|---|
| 0 | 4 | the magic `BWPM` |
| 4 | 2 | the format version, 1 |
| 6 | 2 | the flags, zero |
| 8 | 4 | the record's size, 0xDC |
| 12 | 4 | the checksum of the record's bytes, a CRC-32 |
| 16 | 0xDC | the game's own Pokemon record |

The header is written one field at a time and little-endian, so the file is
the same on every host whatever the compiler does with the struct. The
checksum is the CRC-32 every host library computes, the reflected IEEE
polynomial 0xEDB88320, and it is the same checksum the saved-data layer
uses.

**What an import checks.** The port's own header (the magic, the version and
the checksum), the game's own record checksum (the sum of the record's block
area has to be the checksum word the record carries, the game's own rule),
and the species, the moves and the item: a record the game does not know is
refused, because the game would not be able to read it. The last species of
the game's table is 493 and the last item is 626.

**The record.** The game's own record is a box part of 0x88 bytes and a party
part of 0x54 bytes, and the two areas are masked with the game's own stream
(the linear congruential generator `s = s * 0x41C64E6D + 0x6073`, the middle
halfword of each new state XORed into the area halfword by halfword). The
mask is its own inverse, so masking and unmasking are the same operation,
which is how the game reads a record: it unmasks it, reads it and masks it
again. The record carries the state of its two areas in its own flags, so a
record exported from one game and imported into another is read the same way
in both. The port implements the mask and the checksum itself rather than
calling the game's own functions, so a file can be checked with no game
running at all.

**Where the record goes.** An import puts the Pokemon into the party, and
into a box when the party is full. A call that reaches the game without the
game there reports a failure and never calls into nothing.

## Run-status panel

A small text panel beside the DS screens, filled by the mod.

```c
void (*panel_set)(u32 line, const char *text);   /* lines 0 to 15 */
void (*panel_clear)(void);
```

**Built.** The model is
`sdk/mod_panel.c`: sixteen lines, a line set empty is cleared, the lines are
kept per mod (the mod that calls is the one whose namespace is in force), and
the port draws one panel. The picture is built (`engine/platform/mod_panel_draw.c`):
the port's own font, drawn again only when the model changes, and uploaded
only then. The presenter shows it as a third image beside the two screens,
and the window's arrangement grows by the panel's space — the room exists
exactly when there is a panel to show, so a player without mods sees no
strip, and a hidden panel takes no space at all.

The panel is drawn by the port outside the game's
screens (to the side in the stacked layout, below in the side-by-side
layout), so it never covers the game and is not part of a screenshot of the
DS screens. Typical contents: deaths, encounters by route, the level cap, a
timer. The player can hide it from the port's settings.

## Timer and splits

```c
void (*timer_start)(void);
void (*timer_stop)(void);
u64  (*timer_ms)(void);
void (*split)(const char *name);
```

**The clock.** The timer measures the port's own tick, the SDK's tick the
DS's `OS_GetTick` counts (the system clock divided by 64, 523655.96875 Hz),
and reports the elapsed time in milliseconds. It is therefore the time the
game ran, at the DS's rate, and a headless run that steps frames without
sleeping measures the same as a paced one does.

The timer counts while the game runs and is paused while the port's own menu
is open, which is the settings screen, the one the player enables, disables
and orders the mods in. The pause is exact to the frame: the port samples
every mod's timer once a frame, and an interval that touches the menu is
dropped whole, so a run's time is never made shorter by the port's own menu
being open.

**One timer per mod.** The port keeps the timer, the splits and the random
source per mod, keyed by the mod's namespace, so two mods that both time
their runs do not share one timer.

**Splits.** A split records the name and the time it was taken at. One run
holds up to 32 splits, more than the panel's sixteen lines, so the file
keeps all of them. The port shows the splits in the run-status panel and
writes them to a file at the end of the run: `<save dir>/splits/<namespace>.txt`,
plain text, a header line, the mod's namespace, and one line per split with
the time in milliseconds and the name. The file is written whole and
atomically, so a crash leaves the old file or the new one and never a torn
one. The panel shows the running time and the splits: the port's own lines
live beside a mod's own lines, and the port's line never takes a line the
mod has used.

## Run restart

```c
void (*run_restart)(u32 flags);   /* BW_RESTART_NEW_GAME, BW_RESTART_DELETE_SAVE */
```

Resets the game the way the port's own reset does and, with
`BW_RESTART_NEW_GAME`, starts a new game directly, without stopping at the
title. With `BW_RESTART_DELETE_SAVE` the save and the mod's saved data are
removed first. Ironmon uses it after a loss; a permadeath mod uses it with the
delete flag.

**What the port does with it.** The decision is the service's: the game save
goes first, then the mod's own saved data, and then the flags say what the
run should do next. The work is the port's, because only the port can reset
the machine and start the game again. The call therefore hands the request to
the port's own handler, and a port that installs none takes it with its own
call when it is ready. The handler runs on the game's thread, where the reset
cannot be done, so the request is recorded and the work happens at the next
safe point of the port's loop. The boot's handler records the request and its
frame loop carries it out: with `BW_RESTART_NEW_GAME` the game goes into a
new game directly, and without it the game boots to the title, the way the
port's own reset leaves it.

**The save.** The game's save is the backup chip's image, and the port keeps
it in a file. The reset writes the chip's image back to that file, so a file
that was removed would come back with the reset. The image is therefore
dropped first, which leaves the game an erased chip, and the file is removed
after it.

## Seeded random source

```c
void (*rand_seed)(u64 seed);
u32  (*rand_next)(void);
```

A random number generator for the mod, separate from the game's own, so a
mod's choices do not change the game's rolls. One per mod. Two players who
share a seed get the same sequence, which lets a draft or a random team be the
same on both sides.

The generator is SplitMix64, the algorithm Steele, Lea and Flood's
("Fast Splittable Pseudorandom Number Generators", OOPSLA 2014): the state
is one 64-bit word, a draw adds 0x9E3779B97F4A7C15 to it and mixes the new
state with two multiplications and three shifts. `rand_next` hands out the
high half of the 64-bit output. The whole algorithm is in the port, so two
players who share a seed get the same sequence on every platform, whatever
the host's C library does. A mod that never seeds draws from a generator
started at a fixed seed, because a seed that changes from run to run is not a
seed two players can share.

## External control

A local endpoint outside tools connect to: a stream overlay, a chat bot, a
crowd-control service.

```c
void (*on_command)(BwCommandHandler fn);              /* text commands in */
void (*publish)(const char *topic, const char *json); /* events out */
```

The port listens on `127.0.0.1` only, on a port chosen in its settings, and
only while a mod that uses the service is enabled: the listener starts when
the first mod binds a command handler and stops at the port's reset. Up to
eight tools may be connected at once. The port's own settings reach the
service: the control's port is one of them, and 0 is off, so a port that
leaves it at 0 has no control endpoint at all.

**The wire format.** The control is plain text over TCP, one line each way.
A tool sends a command as one line of text, ended by a newline (a carriage
return before it is ignored), up to 1024 bytes; the line is handed to the
mod as it is, with the newline removed. A `publish` sends one line to every
connected tool: the topic, a space, and the mod's JSON, and a newline. A
tool reads one line per message.

A command is one line of text the mod interprets; the handler runs on the
game's frame, with the mod's own namespace in force, so a state call it
makes reaches its own file. `publish` sends a JSON message to every
connected tool, for overlays that show the run's state.

## Input injection

```c
void (*input_press)(u32 buttons, u32 frames);
```

**Built.** The port's own input layer holds the given DS buttons for the
given number of frames, as if the player pressed them, and the real input and
the injected input combine: the injected buttons are a third source beside
the host's and the script's, so the player's own keys are untouched and the
game sees both. Only the game's twelve buttons may be pressed; a press of
zero frames does nothing; a press longer than the port allows is refused
with a report. The port's ceiling is a minute at the DS's 60 fps, and the
player may lower it in the settings (`[input]`, `max_press`; 0 turns the
injection off). The press ends on its own, so it does not depend on the mod
layer still running. A chat-controlled mod turns votes into presses.

## Frame tick

```c
void (*on_frame)(BwFrameHandler fn);
```

Called once per game frame, after the game's own frame work. Co-op uses it to
send positions and move avatars; a timer display uses it to refresh.

**How the tick runs.** The port's frame hook runs the mods' own frame
handlers in the order the mods bound them, which is the load order, and then
the network's frame, where the queued session events and the commands the
network's threads read reach the mods' handlers, and then the timer's frame,
the avatars' step and the save handoff's pieces. Each handler runs with its
own mod's namespace in force, so a mod's state calls reach its own file
while it runs. A handler that binds another handler does not make the new one
run in the same frame: the count of handlers is taken before the first one
runs. The table holds sixty-four handlers.

## Save handoff

```c
int (*save_send)(u32 peer);     /* send this save to a peer */
void (*on_save_received)(BwSaveHandler fn);
```

Sends the current game save and the mod's saved data to another player in the
session. The receiver's port shows a confirmation, then installs the save and
reloads. A Taglocke passes the run from player to player this way.

**What travels.** The current game save (the backup chip's image under the
save folder) and the mod's own saved data are packed into one blob with a
32-byte head of its own, and the blob is cut into pieces of the size a
session message may carry (4 KB less a 20-byte head), each piece with its own
number and the length of the whole, so a piece that never arrives is seen.
The pieces go out over the frames, a few a frame, because the peer session
keeps what it read in a queue of 64 KB and drops the oldest records when it
is full. The handoff is the port's own traffic, so the pieces travel under
the port's own key and the port's own session handler takes them in; a mod's
own messages keep their own key.

**On the other side.** The blob is put back together, the port's own
confirmation is asked for, and the save is installed: the game save written,
and the mod's own data written beside it with the checksum of the save that
was installed, so the mod's file is in step with the save it belongs to. The
port's confirmation is the port's own: it asks the player in the game's own
yes/no window, and a save the player does not take is reported and dropped,
and nothing is installed. A port that installs no confirmation reports every
save that arrives and drops it. The reload behind it is the port's own: the
boot's handler starts the run again, and it loads the save that is now in
force.

**The answers.** `save_send` answers 0 when the save is on its way, and one
of the peer session's own answers when it was not: no session, another save
still on its way, the save does not fit, or the connection failed.

## The mod's own saved data

A mod keeps its own data in the port's state file, one per mod, beside the
game's save (`docs/mod-state.md`).

```c
s32  (*state_load)(void *buf, u32 size);    /* bytes read, -1 on damage */
int  (*state_store)(const void *buf, u32 size);
int  (*state_clear)(void);
int  (*state_info)(BwModStateHeader *out);
int  (*state_dir)(char *out, u32 out_size);
int  (*state_in_step)(void);                 /* 1 in step, 0 out of step, -1 unknown */
```

The port owns the file's 24-byte header and the mod owns the bytes after it.
The header carries a CRC-32 of the payload, and the payload may be up to
4 MiB; a bigger store is refused with a report. `state_dir` gives the mod
the path of its own folder, for a mod that keeps files of its own.

**The "in step" answer.** `state_in_step` answers for the mod that calls it:
whether the mod's own file was written with the game save in force. The
question is per mod, so it cannot be the `save.load` payload's byte: that
payload is built once for every handler of the event, and one mod's file may
be in step while another's is not. The answer is 1 when the file's
`save_check` is the checksum of the save in force, 0 when they differ (the
game save was written without the mod), and -1 when the question cannot be
answered (no state file, or the save in force cannot be read).

## Script command hooks

The field script VM and the battle effect VM each run their commands through
one table, and the port hooks the dispatch, so a mod can watch or replace any
script command by its opcode without one declared row per command
(`docs/mod-events.md`, section 5).

```c
int (*script)(u32 vm, u16 opcode, BwScriptHandler fn);
```

**The command's view.** The operands are bytes, and the view holds every byte
of every command of both VMs: the port's own width table gives the width of
each command in bytes, and the widest is 60 (the battle effect VM's opcode
8; the field VM's widest is 22), so a handler reaches the last operand of
the widest command as well as the first of the narrowest. The game keeps the
operands as bytes and reads them off its cursor a halfword or a word at a
time, and eighteen of the field VM's commands carry an odd number of bytes,
so some commands read a byte
or two straight off the cursor; the view holds the bytes as they are and the
port writes them back exactly, with nothing rounded to a word. The program
counter in the view is a number, not a pointer, so the view reads the same at
every pointer width.

Every handler bound to a command runs, in load order, and each one sees the
command as the ones before it left it, so the writes to the operands chain. A
handler that replaces the command does not stop the later handlers; when any
of them replaced it, the game's own handler does not run. A handler is handed
the mod's own state, the value its start-up entry returned, and it runs with
the mod's namespace in force, so a state call it makes reaches its own file.

## Mod settings

A mod declares options in a small file named by the manifest's `settings` key:

```
option nuzlocke.dupes   bool    "Dupes clause"         on
option nuzlocke.cap     choice  "Level caps"           off|gym|ace
option nuzlocke.panel   bool    "Show run status"      on
option nuzlocke.lives   int     "Lives"                0 10 3
```

The kinds:

- `bool`: on or off; the default is `on` or `off`. `setting_get` answers 1
  or 0.
- `choice`: one of the names separated by `|`; the default is the first.
  `setting_get` answers the index of the chosen name.
- `int`: a whole number; the value is `<min> <max> <default>`. The screen
  steps it by one and stops at the bounds. `setting_get` answers the
  number.

The file's rules:

- A key is one word with no space, `=`, `#` or `"`, under 64 characters.
  Dots are allowed, and a rule reads the key as written:
  `setting.nuzlocke.dupes`.
- `#` starts a comment, except inside a quoted label.
- A bool's value is `on`, `off`, `true`, `false`, `1` or `0`; anything else
  is reported and the option is ignored.
- A key declared twice is reported, and the second line is ignored.
- A mod may declare any number of options, and a choice may have any
  number of names (each name up to 31 characters).

The port lists them on its settings screen under the mod's name, stores the
player's choices beside the mod's saved data, and the mod reads them:

```c
int (*setting_get)(const char *key);   /* bool and choice index */
```

**Bound.** The loader binds the call into the services table
(`bw_mod_menu_setting_get`), and the mod that calls it is the one whose
namespace is in force, so the answer is the mod's own. The manifest layer
parses the `settings` key and keeps the path in the mod's record, and the
menu's model
(`sdk/mod_menu.c`) parses the options and holds the player's choices beside
the mod's saved data. The screen that lists them is the port's own Mods
section: a mod's options are rows under the mod's own row, and the choice the
player makes there is written beside the mod's saved data.

## The mod menu

**The installed mods folder.** The player installs a mod by putting it in
`<app data>/eidos/mods` (`EIDOS_MODS_DIR` names another folder): a folder
with an `eidos_mod.txt`, or a `.bw` package. A mod found there that the
player has not seen yet joins the list switched on. The Mods section shows
the folder's path and a **Reload mods** row: it reads the folder and every
mod again and applies the list at once, so a mod dropped in, removed or
changed is picked up without leaving the game (a mod whose files changed
is stopped and loaded again, and the cached merged archives are dropped;
game data already on screen shows the change the next time the game loads
it). A mod whose folder is gone shows as missing and stays out of the list.

The port's settings screen has a Mods section: the installed mods in load
order, each with an on/off switch, its settings, "Clear saved data", and a
"Restart the run" row. The order can be changed. Changes apply when the
player leaves the menu, following the rules of `docs/mod-rules.md`, section 2.

**The model and the screen.** `sdk/mod_menu.c` is the model
behind the section: it holds the player's list of mods in
`<save dir>/mods.ini`, answers the screen's questions about each of them (its
name, its namespace, its switch, whether it is running, its settings file and
its options), applies the player's changes when the player leaves the menu,
clears a mod's saved data and reports the saved-data folders on the disk that
belong to a mod that is no longer installed. The screen draws it: the port's
own Mods section on the settings screen (`engine/platform/settings_ui.c`),
where each mod is a row with its switch, its settings are rows under it, and
"Clear saved data" is a row of its own. The player reorders the list with L
and R on a mod's row, and the order the player chose is the order the mods
load in (`docs/mods.md`, "Setting the roots").

**The restart row.** The last row of the section is "Restart the run": the
port's own restart, the one the run restart service records and the boot's
frame loop carries out. A restart ends the run, so the player is asked
first, and the menu asks its own question: the screen is drawn at the title,
where the game's own yes/no window is not available. Left and right choose
the answer, A takes the answer that is chosen and B says no, and the answer
starts at "no".

## Multiplayer

Interface version 5. A mod takes part in multiplayer when its manifest
states `multiplayer = <n>` (`docs/multiplayer.md`).

- **The other players' events.** Every event a joiner's game fires goes to
  the host, and the host runs the multiplayer mods' handlers over it. The
  handler is the same one that receives this game's events.
  `services->event_player()` says whose event it is: 0 for this game's
  own, else the player's number in the session. Another player's event
  cannot be refused or changed, because its game has already acted on it.
  A Pokemon reference in another player's payload names a Pokemon of that
  game, not of this one.
- **The shared table.** Numbers every player's mods see: Soul Link pairs,
  shared route records, counters. `shared_get(key, &value)`,
  `shared_set(key, value)`, `shared_add(key, delta)` and
  `shared_cas(key, expected, value)`; the keys are the mod's own (the port
  puts the mod's namespace in front). The host's copy is the real one and
  is kept with its game. A joiner's change goes to the host, which makes
  the changes in the order they come and sends each result to every
  player, so a joiner reads its own change once the host has sent it back.
  The host makes `add` and `cas` itself, so two players' changes at the
  same moment are both kept: a counter uses `add`, not `get` then `set`.
  `cas` sets the value only when it is `expected` at that moment; the
  mod reads the key to learn whether it did. `get` answers 0 when the key
  is set, 1 when it is not (the value is 0), -1 outside a multiplayer
  game. `set`, `add` and `cas` answer 0 when the change was made (the
  host) or sent (a joiner), -1 outside a multiplayer game or for a key
  that cannot be sent (empty, longer than the limit, or with a space).

## The port's own session

The settings screen has a Session section beside the Mods one, and it is the
port's own rather than a mod's. The section holds the two codes of a
session: the code in force (the one the relay handed out when the player
hosted a session, or the one the player joined a session with) and the code
the player types to join one. They are kept apart, so that hosting a
session does not overwrite what the player has typed, and both are settings
of the port's own (`[session]`, `code` and `join_code` in the settings
file), so they are reverted and committed with the rest of the settings and
the section can put them back or clear them.

The rows in the middle are the player's own actions: host a session, join
one with the typed code, and leave. The rows after them are the players of
the session: this player's own number, and one row for every other player,
with the number the relay gave them. The port's own handler collects them:
the service tells every handler that asked for the session's events about a
join and about a drop, and the port's own handler keeps the numbers it was
told about under a lock, because the frame that drains the queue is not the
thread that draws the screen.

The section drives the peer session service through the port's own calls
(`engine/platform/settings.c`), not through the services table a mod gets.
The port's calls do three things the mod-facing ones do not: they hand the
service the relay and the player's name that the settings hold, so a change
the player made in the menu is the one the next session uses; they keep the
state of the work in flight; and they do not block the frame thread. A host
or a join waits for the relay, for up to three seconds, and the settings
screen is drawn on the frame thread, so the call is made on the port's own
worker. The worker publishes its answer under a lock, the screen takes it
on the frame it polls, and the code goes into the model, so the model is
only ever written by the thread that draws it.

## Related documents

- `docs/mod-modes.md`: which runs use which service.
- `docs/mod-abi.md`: the services table and the version rule.
- `docs/mod-coop.md`: co-op and the field avatars.
- `docs/mod-state.md`: the mod's saved data.
- `docs/mod-rules-language.md`: the `message` and `send` actions.
- `docs/mod-events.md`: the events, and the script command hooks.
