# Game modes and challenge runs

This document lists the game modes and challenge runs players build on top of
a Pokemon game, and checks that the mod surface covers each one. A mode here
is a ruleset over the normal game: Nuzlocke, monotype, Soul Link and the like.
Content mods (new maps, new species) and randomizers that only replace data
are out of scope; the data layer covers them (`docs/mods.md`).

Section 1 is the list of modes and what each needs. Section 2 points to the
stable event list those needs produce. Section 3 is the port services the
modes need beyond events and state.
`docs/modding.md` is the overview of the surface itself.

## 1. The modes

Each mode is described by its rules, then by what a mod needs to enforce
them. Event names refer to section 2, services to section 3.

### Nuzlocke family

**Nuzlocke.** Catch only the first encounter on each route. A Pokemon that
faints is dead and may not be used again. A whiteout ends the run. Every catch
gets a nickname.
Needs: `route.change`, `encounter.wild`, `encounter.static`, `catch.attempt`
(refuse), `catch.result`, `mon.nickname`, `battle.faint`, `whiteout`,
`box.withdraw` (refuse a dead Pokemon), `battle.action` (refuse a switch to
one), `heal` (modify: the dead stay fainted), `item.use` and `battle.item`
(refuse a revive), `mon_to_box` (state), mod state.

**Nuzlocke clauses.** Optional rules most runs add on top:

| Clause | Rule | Needs |
|---|---|---|
| Dupes | An encounter of a species already owned does not count; the next one does | `encounter.wild` with species, dex read (`dex.register`, state) |
| Shiny | A shiny may always be caught | `encounter.wild` with the shiny flag |
| Species | No two of one species on the team | `mon.receive`, party read |
| Gift and static | A gift or static Pokemon counts as the area's encounter, or is free | `encounter.static`, `mon.receive` with its source |
| Set mode | No free switch after a faint | `config` state write (battle style) |
| Level cap | No Pokemon above the next gym leader's highest level | `mon.exp` (modify: stop at the cap), `item.use` (refuse a Rare Candy), `badge.get` |
| Item limits | No items in battle, or a fixed number of heals | `battle.item` (refuse), `item.use` (refuse), mod state |
| Centre limit | A Pokemon Centre may be used once, or not at all | `heal` with its source (refuse) |
| No running | Escape is not allowed | `battle.escape` (refuse) |

**Hardcore Nuzlocke.** Nuzlocke plus level caps, no items in battle and set
mode. Needs: the union of the clauses above.

**Wedlocke.** Pokemon are paired male with female; a Pokemon may only battle
alongside its partner, and the partner shares its fate. Needs: Nuzlocke,
species gender read, `battle.action` (refuse a switch or a move), mod state
for the pairs.

**Egglocke.** Each encounter is replaced by an egg another player made.
Needs: Nuzlocke, `mon.receive` with source, `egg.hatch`, mon import
(section 3), `mon.create` (modify).

**Wonderlocke.** Each catch is traded away for a random Pokemon from other
players. Needs: Nuzlocke, `catch.result`, mon export and import (section 3),
peer sessions.

**Randomlocke.** Nuzlocke on a randomized game. Needs: Nuzlocke plus the data
layer, or `mon.create` (modify) for a run-time randomizer.

**Genlocke.** One Nuzlocke across several games; survivors move on to the next
game. Needs: Nuzlocke, mon export at the end of a game and mon import at the
start of the next (section 3), `game.cleared`.

**Trashlocke.** Only Pokemon from a weak list may be used. Needs: species read,
`mon.receive`, `box.withdraw` (refuse), `mon_to_box` (state).

### Soul Link family

**Soul Link.** Two (or more) players, each in their own game. The encounters
on the same route are linked across the games. When one dies, its partner
dies. A linked pair must be on the team together or boxed together. Needs:
Nuzlocke, peer sessions, `box.deposit`, `box.withdraw`, `party.swap`, HP write
in the field and in battle (state), mod state per save slot.

**Nuzlocke Soul Link.** Nuzlocke rules on the linked pairs, one shared run
record. Needs: the union.

**Race.** Two players play the same game separately and race to a goal.
Needs: progress events (`badge.get`, `flag.set`, `game.cleared`), peer
channel, timer (section 3).

**Co-op.** Each player plays their own game and sees the other's avatar.
Needs: position and map state, `route.change`, `map.warp`, `player.step`,
peer sessions, field avatars (`docs/mod-coop.md`).

### Team restrictions

**Monotype.** Only Pokemon of one type. Needs: species type read (state),
`mon.receive` with `mon_to_box` for an off-type Pokemon, `box.withdraw`
(refuse), `mon.evolve` (an evolution can change type; refuse or allow).

**Single Pokemon, solo run.** One Pokemon for the whole game. Needs:
`mon.receive` with `mon_to_box`, `box.withdraw` (refuse).

**Starter only.** Only the starter and its evolutions. Needs: `mon.receive`
with `mon_to_box`, `box.withdraw` (refuse).

**No evolution.** No Pokemon may evolve. Needs: `mon.evolve` (refuse).

**Little Cup, unevolved only.** Only first stages. Needs: `mon.evolve`
(refuse), species read.

**Shiny only.** Only shiny Pokemon may be used. Needs: `mon.receive` with the
shiny flag, `mon.create` (modify odds).

**Ironmon (Kaizo Ironmon).** One randomized starter, no other Pokemon, no
healing items in battle, limited items overall; a loss ends the run and a new
seed starts. Needs: `mon.receive` with `mon_to_box`, `battle.item` (refuse),
`item.use` (refuse), `battle.end`, `whiteout`, run restart (section 3),
run-status display (section 3).

### Battle restrictions

**Itemless.** No items at all. Needs: `item.use`, `battle.item`, `shop.buy`
(all refuse), `mon.helditem` with `mon_take_item` (state).

**No Pokemon Centre, no heal.** Healing only by items, or not at all. Needs:
`heal` (refuse), `item.use` (refuse).

**No PC.** The PC may not be used. Needs: `pc.open` (refuse).

**Minimum battles, pacifist.** Fight only the battles the game forces. Needs:
`encounter.wild` (refuse, as a repel), `trainer.spotted`, `battle.end`, mod
state for the count.

**Low level.** Win with the lowest possible levels. Needs: `mon.exp` (modify
to zero on demand), `battle.end`.

**Hitless, no damage.** A run fails when a Pokemon takes damage. Needs:
`battle.damage` with the target side.

**Doubles only, triples only.** Every trainer battle in one format. Needs:
`battle.start` (modify the format).

**Level scaling.** Opponents scale to the player's team. Needs:
`battle.start` (modify the opposing party's levels), party read.

**Inverse battles.** The type chart is inverted. Needs: `battle.damage`
(modify), or the type chart replaced through the data layer.

### Information and control

**Blind run.** The player sees no levels, stats or move details. Needs: a
display override for the battle and summary screens (raw hooks), or text
replacement through the data layer.

**Chat-controlled, Twitch Plays.** Viewers vote on inputs or choices. Needs:
input injection and external control (section 3).

**Crowd control.** Viewers trigger effects: heal, poison, spawn an encounter,
give money. Needs: external control (section 3), state writes, `heal`,
`encounter.wild` (force).

**Speedrun.** Fastest time to a goal, with splits. Needs: timer and splits
(section 3), progress events (`badge.get`, `flag.set`, `game.cleared`,
`route.change`).

**Shiny hunting.** Repeated encounters for a shiny. Needs: `encounter.wild`
with the shiny flag, an encounter counter in mod state, `mon.create` (modify
the odds, for a charm).

**Living dex, completion.** Own one of every species. Needs: `dex.register`,
dex read, run-status display.

### More runs

Further rulesets from the Nuzlocke and challenge-run communities, and
inventive ones that follow the same pattern. Each is one more combination of
the events and services above; the third column names what it adds.

| Run | Rule | Needs |
|---|---|---|
| No catching (Giftlocke) | Only the starter and gift Pokemon | `battle.item` (refuse every ball), `mon.receive` with source |
| No evolution | No Pokemon may evolve | `mon.evolve` (refuse) |
| No level up | Levels are frozen at the level obtained | `mon.exp` (modify to zero), `item.use` (refuse a Rare Candy) |
| BallLocke | One catch per ball type | `catch.attempt` with the ball |
| Monolocke, Apocalocke | One type, or a type set a theme picks | species type read, `mon.receive`, `box.withdraw` |
| Uniquelocke | No two team members share a type | party read with types, `box.withdraw`, `party.swap` |
| Snaplocke | Half the species are banned, chosen by vote | `mon.receive`, `box.withdraw`, external control for the vote |
| Routelocke | Only Pokemon from one chosen route | `encounter.wild` with the area |
| Loserlocke | Only one- or two-stage lines, only plain Poke Balls | species read, `catch.attempt` with the ball |
| NOHKOlocke | A Pokemon that knocks out a full-HP foe in one hit is boxed | `battle.damage` with the target's HP before and after |
| Starlocke | One Pokemon of each type, no more; swap only after a faint | `mon.receive`, `battle.action` (refuse switch), `battle.faint` |
| Chesslocke | Each team member has a role that limits when it may fight | `battle.switch`, `battle.action` (refuse a switch or a move) |
| Zombielocke | A dead Pokemon returns as a zombie; healing one costs another | HP and party writes, `heal` (modify which Pokemon heal) |
| Tribelocke | Pokemon are caught in groups that fight together | `battle.switch`, mod state |
| Draftlocke | Players draft types in turn, then race | peer sessions, species type read |
| Taglocke | Players take turns on one run, relay style | save handoff (section 3) |
| Sleeplocke | The run must finish in one sitting | timer, `save.load` |
| Professor Oak challenge | Catch every species available before each gym | encounter table read, dex read, `badge.get` |
| Bingo | Complete a card of tasks during the run | any events; the card lives in mod state |
| No super-effective moves | Only neutral or weaker moves | `battle.action` with the move chosen (refuse), type effectiveness read |
| STAB only, no status moves | Damage only from same-type moves; no status moves | `battle.action` with the move chosen, move data read |
| No TMs | Moves only from level-up and eggs | `move.learn` with its source (refuse a TM), `item.use` (refuse a TM) |
| No marts, poverty | Nothing bought; or a money cap | `shop.buy` (refuse), `money.change` |
| No grinding | Fight only required trainers | `battle.start` with the trainer id, `encounter.wild` (refuse) |
| Capped catches | No catch above the current badge level; first six encounters only | `catch.attempt` with the target's level, `badge.get` |
| BST cap | Only species under a base stat total | species stats read |
| Max three | At most three Pokemon on the team | party read, `mon.receive`, `box.withdraw` |
| Mirror match | Every trainer uses a copy of the player's team | `battle.start` (modify the opposing party) |
| Chaos battles | A random ability or type each battle | `battle.start` (modify the setup), in-battle state writes |
| Permadeath save | A loss deletes the save | `whiteout`, run restart (section 3) |

## 2. The stable events

The runs above produce the stable event list. `docs/mod-events.md`, section 1,
owns it: each event, the game function it binds, its refused outcome and the
payload fields a handler may modify. The state values the runs read and write
are `docs/mod-events.md`, section 4.

## 3. Port services the modes need

Some needs are not game functions. The port provides them; each is designed in
`docs/mod-services.md`.

| Service | What it does | Modes |
|---|---|---|
| Messages to the player | Show text in the game's text window; a yes/no prompt | every refusal; all modes |
| Peer sessions | Connect two or more eidos instances and exchange mod messages. The intended flow follows Archipelago: a player starts a session, a relay the project runs hands back an address or code, and the other players join with it | Soul Link, Race, Co-op, Wonderlocke |
| Mon export and import | Write a Pokemon record to a file, and add one from a file to the party or a box | Genlocke, Egglocke, Wonderlocke |
| Run-status panel | A small panel beside the DS screens the mod fills: deaths, encounters by route, level cap, timer | Nuzlocke family, Ironmon, Living dex, Speedrun |
| Timer and splits | A run timer and named splits the mod marks at events | Speedrun, Race |
| Run restart | Start a new game from the mod (new seed), without returning to the title screen | Ironmon, roguelike runs |
| External control | A local endpoint an outside tool sends commands to (chat votes, crowd control) | Twitch Plays, crowd control |
| Input injection | Feed button presses into the game as if the player made them | Twitch Plays |
| Frame tick | A call every frame, for timers and position updates | Co-op, Speedrun |
| Save handoff | Pass the current save from one player's eidos to another's through a peer session | Taglocke |
| Seeded random source | A random number source for the mod, seeded and shareable, apart from the game's own | random teams, chaos battles, Snaplocke, generators |
| Mod settings | Options the mod declares, listed on the port's settings screen | all modes with options |


## Sources

- [Nuzlocke University, Nuzlocke variants](https://nuzlockeuniversity.ca/nuzlocke-variants/)
- [Poketools, challenge run generator](https://www.poketools.com/challenge-generator)
- [Bulbapedia, Nuzlocke Challenge](https://bulbapedia.bulbagarden.net/wiki/Nuzlocke_Challenge)
- [Bulbagarden, Super Kaizo IronMON](https://bulbagarden.net/threads/a-new-version-of-the-popular-ironmon-challenge-dubbed-super-kaizo-debuts-on-twitch-streamer-and-ironmon-creator-iateyourpies-channel.301057/)
