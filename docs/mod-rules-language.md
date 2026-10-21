# The rule language

A rule lets a mod act on an event without writing C: "on a faint, subtract
1000 money", "refuse a second catch on a route". This document fixes the
language: its scope, grammar, names, execution and storage.
`docs/mod-rules.md` records why rules run on a small interpreter in the port.

## 1. Scope

A rule is a guarded action at one event. It has exactly three parts: the event
it binds, zero or more guards, and one or more actions. It has no loop, no
function, no local variable and no branch; it has arithmetic and comparison in
flat expressions. This keeps the interpreter small, makes every rule finish in
a bounded number of steps, and lets the port check a rule completely when it
loads.

A run that needs more (walking the whole party, a search, a fallback chain)
is written as a code mod. The two mix: a mod can ship rules and a library.

## 2. The grammar

A rule is one text file, and a file holds one rule. The manifest binds it to
its event; a mod with several rules has several files and several `rule`
lines:

```
rule = <event> <path>
```

The file:

```
language 1
rule <name> on <event>
    when <expr>
    do <action>
```

- `language 1` is the rule-language version. A rule written for another
  version is refused at load.
- `rule <name> on <event>` names the rule for reports and repeats the event.
  The port refuses the rule when the manifest's event and the file's disagree.
- `when <expr>` is a guard. Several guards are a conjunction. No guard means
  the actions run at every firing.
- `do <action>` is an action. A rule has one or more.

### Actions

| Action | Meaning |
|---|---|
| `refuse` | refuse the event; valid only at an event with a refused outcome (`docs/mod-events.md`, section 1) |
| `set <target> <expr>` | assign a target |
| `add <target> <expr>` | add to a target |
| `sub <target> <expr>` | subtract from a target |
| `message <expr>` | show a line of text to the player (`docs/mod-services.md`) |
| `send <expr>` | send a value to the peers of the current session (`docs/mod-services.md`) |
| `release <expr>` | release a Pokemon of the player's party for good: a mon (a party or a player-side battle reference) or a party position. Applied at the next field step; refused if it would leave no Pokemon that is not an egg (version 4) |
| `give <item> <count>` | put `count` of `item` in the bag, all or nothing (version 4) |

### Targets

A target is a modifiable payload field, a game state field or a mod cell:

```
<target> = "event" "." <field>
         | <state-field> [ "[" <expr> "]" ]
         | "state" "." <ident> [ "[" <expr> "]" ]
```

- `event.<field>` writes a payload field the event marks *mod*
  (`docs/mod-payloads.md`). Writing any other payload field is a load error.
- A state field (section 3) is written through the port's state call. Fields
  that name one Pokemon take a subscript: `hp[event.mon]`.
- `state.<cell>` is the mod's own cell, saved with the game save
  (`docs/mod-state.md`). With a subscript it is a keyed table:
  `state.route_used[zone]`.

### Expressions

```
<expr>    = <or>
<or>      = <and> { "||" <and> }
<and>     = <not> { "&&" <not> }
<not>     = [ "!" ] <cmp>
<cmp>     = <sum> [ ( "==" | "!=" | "<" | "<=" | ">" | ">=" ) <sum> ]
<sum>     = <product> { ( "+" | "-" ) <product> }
<product> = <unary> { ( "*" | "/" | "%" ) <unary> }
<unary>   = [ "-" ] <atom>
<atom>    = <number> | "true" | "false" | <text> | <name> | "(" <expr> ")"
<name>    = "event" "." <field> | <state-field> [ "[" <expr> "]" ]
          | "state" "." <ident> [ "[" <expr> "]" ]
          | "setting" "." <ident>
          | "table" "." <ident> "[" <expr> "]"
```

The two-character operators are `==`, `!=`, `<=`, `>=`, `&&` and `||`. Any
other pair of operator characters reads as two tokens, so `x*-1` and `a<-1`
mean what they look like. `=`, `&` and `|` alone are errors.

- `setting.<key>` reads one of the mod's own settings (the settings
  declaration in its manifest); a key the mod does not declare reads -1.
- `table.<name>[key]` reads the mod's own data table (`docs/mod-tables.md`);
  a key the table does not list reads the table's default. A rule that names
  a table its mod does not declare is refused at load.

### Types

| Type | Values |
|---|---|
| `int` | a 32-bit signed value; every numeric payload and state field |
| `bool` | true or false; comparisons and flag fields |
| `text` | a string literal or a nickname |
| `mon` | a Pokemon reference, a `BwMonRef` payload field |

A comparison needs the same type on both sides; arithmetic needs `int`. A
mismatch is a load error.

### Examples

```
language 1
rule faint_penalty on battle.faint
    when event.side == 0
    do sub money 1000
```

```
language 1
rule one_catch_per_route on catch.attempt
    when state.route_used[zone] == 1
    do refuse
    do message "You already caught a Pokemon on this route."
```

```
language 1
rule mark_route_used on catch.result
    when event.caught == 1
    do set state.route_used[zone] 1
```

```
language 1
rule level_cap on mon.exp
    when level[event.mon] >= state.cap
    do set event.amount 0
```

```
language 1
rule no_evolution on mon.evolve
    do refuse
```

## 3. Names

### Events

A rule binds any stable event of `docs/mod-events.md`, section 1. The event's
payload fields (`docs/mod-payloads.md`) are readable as `event.<field>`; those
marked *mod* are also writable.

### State fields

The state fields are the values of `docs/mod-events.md`, section 4, under rule
names:

| Rule name | Value | Direction |
|---|---|---|
| `money` | the player's money | read, write |
| `badges` | the number of badges | read |
| `badge[n]` | whether badge `n` is held | read |
| `zone` | the current zone | read |
| `party_count` | Pokemon in the party | read |
| `species[m]` | a Pokemon's species | read |
| `level[m]` | a Pokemon's level | read |
| `hp[m]`, `max_hp[m]` | a Pokemon's HP and maximum HP | read; `hp` write |
| `type1[m]`, `type2[m]` | a Pokemon's types | read |
| `shiny[m]` | whether a Pokemon is shiny | read |
| `held_item[m]` | a Pokemon's held item | read |
| `personality[m]`, `ot_id[m]`, `exp[m]`, `friendship[m]`, `ability[m]` | a Pokemon's own fields (version 4) | read |
| `move1[m]` .. `move4[m]` | a Pokemon's moves (version 4) | read |
| `is_egg[m]`, `gender[m]`, `form[m]`, `nature[m]`, `has_nickname[m]` | (version 4) | read |
| `place[m]`, `side[m]` | where a reference points: `BW_MON_*`, and in a battle the side (0 the player's) (version 4) | read |
| `species_type1[s]`, `species_type2[s]` | a species' types | read |
| `species_bst[s]` | a species' base stat total | read |
| `species_stage[s]`, `catch_rate[s]`, `gender_ratio[s]`, `growth[s]`, `egg_group1[s]`, `egg_group2[s]` | a species' data (version 4) | read |
| `caught[s]` | whether a species is registered caught | read |
| `owned[s]` | whether the party or a box holds a Pokemon of species `s` that is not an egg (version 4) | read |
| `party_max_level`, `party_alive` | the highest level in the party; how many party Pokemon have HP, eggs left out (version 4) | read |
| `party_has_type[t]` | whether a party Pokemon (not an egg) has type `t` (version 4) | read |
| `item_count[i]` | how many of item `i` the bag holds | read |
| `item_pocket[i]` | the bag pocket of item `i` (version 4) | read |
| `move_type[v]`, `move_power[v]`, `move_class[v]` | a move's type, power and damage class, as the move data stores them (version 4) | read |
| `area` | the current area: the place name the game shows on entry; every floor of a cave and every building of a town share it (version 4) | read |
| `area_of[z]` | the area of zone `z` (version 4) | read |
| `wild_battle` | the battle in progress is a wild one (version 4) | read |
| `play_seconds` | the play time in seconds (version 4) | read |
| `battle_style` | shift or set | read |
| `in_battle` | a battle is in progress | read |

`m` is a Pokemon reference, usually `event.mon`, or a party position (0 to
5); `s` a species; `i` an item; `v` a move; `t` a type; `z` a zone. A name not
in the table is a load error.

`battle_style` is read-only: the battle reads it once when it starts, so a
write would not reach the battle in progress.

## 4. Execution

A rule is a handler like any other. The port compiles it at load and binds it
to its event in the mod's load order (`docs/mod-events.md`, section 3). It sees
the same payload as a C handler, and a `refuse` counts like a handler's refusal.
Writes to `event.<field>` chain with the other handlers' writes.

**Totality.** A loaded rule cannot fail while it runs. Division or modulo by
zero gives 0. An unset mod cell reads 0. Every name, type and target was
checked at load.

**The step budget.** Each firing of a rule may execute at most 256 steps; a
step is one operator or one action. A rule that reaches the budget stops for
that firing without refusing, and the port reports it once. With no loops a
rule rarely comes near the budget; it exists so the bound is stated.

**Writes the game overrides.** The port does not arbitrate a write, and the
game's later write wins (`docs/mod-events.md`, section 4). A change that must
stick is made through a modifiable payload field at the event the game derives
the value from.

## 5. Storage and loading

A rule is stored as text in the mod and compiled by the port at load. The
compiled form exists only in memory, so it is not part of the ABI and can
change between port versions. kenon checks rules against this grammar and the
names when it packs a mod, so a rule that would fail at load fails at pack
time instead.

A mod's cells live in its saved data (`docs/mod-state.md`), so they follow the
game save. A keyed cell holds up to 1024 keys.

## 6. The implementation

| Piece | File | Test |
|---|---|---|
| The header: compiled form, values, errors | `sdk/rule.h` | a compile-only size check |
| Parser and checker | `sdk/rule_parse.c` | the examples above, and one malformed rule per error class |
| The name table: events, payload fields, state fields | `sdk/rule_names.c` | every name of section 3 resolves |
| The interpreter | `sdk/rule_vm.c` | guards, actions, refusal, the budget, division by zero |
| Binding to the event layer | `sdk/rule_run.c` | two rules on one event: order and refusal |
| Mod cells | `sdk/rule_state.c` | read and write a cell over a temporary save folder |

The header lands first; the parser, the name table, the interpreter and the
cells are then independent; the binding wires them.

## Related documents

- `docs/mod-rules.md`: why a small interpreter in the port.
- `docs/mod-events.md`: the events, the state values and the handler contract.
- `docs/mod-payloads.md`: the payload fields a rule reads and writes.
- `docs/mod-state.md`: where a mod's cells are saved.
- `docs/mod-services.md`: messages and peer sessions.
