# Multiplayer

How eidos plays one game across several computers. The design is agreed
with kenon and lives in metaxy's `docs/SPEC.md`: Part A is the relay, Part B
is the game's side. This page is the game's side as it is built. The work
goes in the order of SPEC section 18; each part below says whether it is
built.

The relay (metaxy) only forwards messages. Every decision is the host's
game's, as in a Minecraft world opened to LAN.

## The layers

| Layer | File | What it does |
|---|---|---|
| The relay client | `sdk/mod_net.c` | the connection, the frames of `eidos-net/1`, the mods' messages |
| Multiplayer | `sdk/mp.c` | the lobby, the start, the grace after a drop, the pause |
| The held game | `demo/eidos_boot.c`, `boot_mp_hold` | holds the game's thread and draws the waiting screen |
| The lobby screen | `engine/platform/settings_ui.c`, Multiplayer tab | Host, Join, the players, your name, Start |

## The port's messages

The games talk with `D` frames under the key `eidos:port`. No mod can
have that key: a mod's key is its namespace, and a namespace has no `:`.
`bw_mod_net_port_send` sends under it, and the handler that
`bw_mod_net_set_port_handler` sets gets the messages. That handler also
gets every join and leave (before the mods do), and the end of the session
with its reason (`lost`, or the relay's error such as `removed`).

The messages are lines of text:

| Message | From | Meaning |
|---|---|---|
| `hello <v> <game> <mods>` | a joiner, to the host | it is in: the version of these messages (1), its game (`B` or `W`), its multiplayer mods (`ns=n;ns=n`, or `-`) |
| `roster <state>` + one line per player | the host, to everyone | the lobby: `<peer>\t<connected>\t<on roster>\t<game>\t<mods ok>\t<name>`; state is `lobby`, `playing`, `grace` or `paused` |
| `start` | the host, to everyone | the game runs |
| `pause` | the host, to everyone | every game is held until the next `start` |

A joiner says `hello` when it joins, when it joins again after a drop, and
when the host comes back. The host sends the roster after every change, and
`start` to a player who comes back while the game runs.

## The Multiplayer screen (built)

- The title menu's MULTIPLAYER line opens it: Your name, Host and Join.
- Your name, the join code and the name of a new game are typed on the
  game's own keyboard (the name entry screen, overlay 194). The name uses
  the player's kind (up to 7 letters, the game's own question); the code
  and the game name use the plain kind with the port's own question.
- Host lists the host's games, newest first, and New game. New game asks
  for a name first, then hosts. Join asks for the code at once and joins
  when it is typed.
- A joiner that missed the host's `hostid` (it came before the screen
  attached the layer) asks again with `who`.
- A value too long for its row moves along while the row is selected.
- In a multiplayer New Game, the professor's name entry opens with the
  player's multiplayer name typed in; the player can still change it.

## The lobby and the start (built)

- The lobby lists the players: here or missing, the game, and whether the
  multiplayer mods match the host's.
- The host starts the game when every player is here, every player's game
  has said hello, and every player's multiplayer mods match the host's.
  The lobby says what it waits for ("waiting for Cheren", "Cheren: mod soul
  is another multiplayer version").
- The first start sets the roster: the players here then. A player who is
  not on the roster and joins later is removed (`Q`). Adding players after
  the first start is SPEC step 8.

## Multiplayer mods

A mod that plays a part in multiplayer states a compatibility number in its
manifest:

```
multiplayer = 3
```

A number from 1. Only the number has to match between the players, so a
mod may update freely as long as its multiplayer messages do not change.
Mods without the key do not count for the lobby's check. Rules mods cannot
use multiplayer for now (SPEC section 17).

## Drops and the pause (built)

- A player drops while the game runs: the others play on for 15 seconds
  (`BW_MP_GRACE_MS`). A player back within that time changes nothing.
- After the grace the host pauses every game. The game's own thread is
  held in its frame hook, so nothing of the game runs; the network, the
  timers and the window go on. The screens show the last frame, darkened,
  with the line "Waiting for players: <names>".
- When everyone is back, the host's screen says "Everyone is back. Press
  START to play." The host's START (or the lobby's Start game row) starts
  every game again.
- The host drops: every joiner's game is held ("Waiting for the host to
  come back") for up to 10 minutes (`BW_MP_HOST_WAIT_MS`). The host's game
  joins its own code again as peer 1 with its kept token and pauses every
  game until it starts again. A host that does not come back ends the
  session, and each joiner's game goes back to the title.
- A game that loses the relay joins the same code again every 5 seconds
  (`BW_MP_REJOIN_EVERY_MS`), held meanwhile. Its token is kept per code
  (`net_tokens.txt` in the save folder), so it is the same peer, even after
  a restart of the game.

- The relay restarts (it keeps nothing, so the code is gone): the host's
  game sees `unknown-code` when it joins again, opens a new session at
  once, and shows "the relay restarted: the new code is XYZ" on its held
  screen. Its lobby stays: a player who joins the new code takes its
  roster place again by its name, until the identity of step 4 replaces
  the name. A joiner cannot learn the new code by itself: its session ends
  with "the relay restarted: ask the host for the new code", its game goes
  back to the title, and the player joins the new code.
- The timers are `#ifndef` in `sdk/mp.h`, so a test build can shorten them.

## The saves (built, first part)

- The host keeps each game in a folder of its own,
  `<save folder>/multiplayer/games/<name>`: its own save (`host.sav`),
  one save per joiner named by the joiner's fingerprint (the last three
  versions stay as `.1` to `.3`), and its own lasting id.
- Who a joiner is: each of the host's games has a lasting id
  (`host_id.txt`) and the host sends it
  (`hostid <id>`) when a player joins. The joiner keeps one random secret
  per host id (`secrets.txt`) and says hello with its fingerprint, never
  the secret itself: `hello <v> <game> <mods> <fingerprint> <in game>`.
- The start: the host's game goes into its own save (the title menu's
  Continue, or New Game without a save). Each joiner gets its save from the
  host, in pieces (`save <total> <offset> <crc32>` and up to 3840 bytes, six
  pieces a frame), or `save 0 0 0` when the host has none for it yet; the
  joiner's game then boots on it (or on a new game). The joiner keeps no
  save of its own: the file is `multiplayer/session.sav`, and the joiner's
  own save is the chip's file again when it leaves.
- When a joiner's game writes its save, the file goes to the host, which
  keeps it as that player's save.
- The port's reset keeps the session while a multiplayer game runs.

## Saving (built)

- Only the host saves. A joiner's field menu has no Save (the label stays,
  the button does nothing). When the host's game writes its save, the host
  sends `savenow`; each joiner's game runs the game's own save at its next
  quiet moment (the first frame where the field runs and no field event
  does) and the file goes to the host.
- The host's own game in a multiplayer game is the game folder's
  `host.sav`, not one of the single-player slots.
- The host counts the saves to come after it saves; a joiner that leaves
  is not waited for. Closing the host's window while saves are to come
  keeps the window, darkened ("Saving everyone's games"), until they are
  in, for up to 30 seconds.
- One file at a time goes to a peer (its `saveack` names no file); a new
  one replaces the one on its way, and a hello said again while a save is
  on its way does not start it over.
- The pause screen shows the game's code again.
- A save belongs to one game: the host keeps `<fingerprint>.sav.game`
  (`B` or `W`), and the lobby will not start while a player plays the
  other game than their save's.

## Save slots (built)

- Up to 8 single-player saves: slot 1 is `pokeblack.sav`, slots 2 to 8
  are `slots/save<n>.sav`; `slot.txt` holds the slot in force.
- The Continue card shows `< 2/3 >`; left and right switch the save.
- New Game takes a free slot, with no warning. With all 8 used, the
  Continue card asks "Replace 3/8?": left and right choose the save, A
  takes it (then the game's own warning), B gives up.
- The title menu's MULTIPLAYER line (in the Mic Test's place, third)
  opens Host / Join.

## Positions (built)

- Each game sends its player's place to every other player
  (`pos <zone> <x> <y> <z> <dir> <mode> <sprite>`) when it changes, and
  every 2 seconds when it does not.
- Each game shows the players in its own zone as field avatars
  (sdk/mod_avatar.h): the hero or the heroine, walking to each new cell.
- A map change, a battle or a menu that leaves the field empties the
  game's entity pool; the avatars are made again when the field is back.
  A player who leaves takes its avatar with it.

## Not built yet

1. Event forwarding.
2. The shared table for mods.
3. TLS.
4. Adding players after the first start.

The mods' own session events for the lobby, the start and the pause need
new event kinds and a `session_start` service: they come with the next mod
interface version.

## Tests

A two-game check without typing: `EIDOS_MP_TEST=host:<file>` makes a game
host at frame 300, write its code to the file and start as soon as the
lobby is ready; `EIDOS_MP_TEST=join:<file>` makes another join with that
code. Run both headless against a relay (each with its own `BW_SAVE_DIR`
and `EIDOS_SETTINGS`, and START pressed to reach the title menu): both
games go into the game when the host starts. A game run with `BW_SAVE_DIR` and no
`EIDOS_SETTINGS` keeps its settings in that folder (`settings.ini`), so a
second game on one computer has its own name and window. With
`EIDOS_SAVE_TEST_AT=<frame>` the host's game saves at its first quiet
moment after that frame, as if the player had saved, so the joiners' saves
follow it. The relay's refusals carry a text after the reason word; the
screen shows it on the help line.


`tests/test_mp.c` drives the layer as the host's game and as a joiner's,
with a network of its own: the lobby's checks, the roster, a stranger
removed, a drop within and past the grace, the pause and the restart, the
host's own reconnect, the host gone and its timeout, and a removed joiner.
