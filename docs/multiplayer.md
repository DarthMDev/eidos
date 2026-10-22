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
| The lobby screen | `engine/platform/settings_ui.c`, Session page | Host, Join, the players, Start |

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

## Not built yet

In the order of SPEC section 18:

3. Save slots and the main menu's Multiplayer line. Until then the lobby is
   the Session page of the port's settings, and the start does not change
   which save each game runs: each player goes on with its own game.
4. The host's save as the truth: the joiners' saves in it, the transfer,
   host-only saving, the joiner's identity secret (keyed by a host id kept
   in the multiplayer save).
5. Positions and event forwarding.
6. The shared table for mods.
7. TLS.
8. Adding players after the first start.

The mods' own session events for the lobby, the start and the pause need
new event kinds and a `session_start` service: they come with the next mod
interface version.

## Tests

`tests/test_mp.c` drives the layer as the host's game and as a joiner's,
with a network of its own: the lobby's checks, the roster, a stranger
removed, a drop within and past the grace, the pause and the restart, the
host's own reconnect, the host gone and its timeout, and a removed joiner.
