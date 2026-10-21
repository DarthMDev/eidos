# Mod interface: work after the version 4 freeze

What the mod interface does not cover yet, and why each item waits. Every
item is verified against the game's code before it is built; one that
cannot be verified stays here rather than shipping half-working.

## Before the freeze

| Item | Why it waits |
|---|---|
| `heal` source `CENTRE` | The Pokemon Centre nurse and other scripts run the same heal command; telling them apart needs the nurse script's id. |
| `item.add` refusal | Some scripts check for room, then say the item was received even when the add answers 0; each caller's outcome must be checked first. |
| `mon.evolve` before the post-battle scene | The event fires at the evolution check; the post-battle path is still to be read to confirm no scene starts first. |

## After the freeze

| Item | Notes |
|---|---|
| A finer `route.change` `how` | Door or stairs, warp tile, Fly, Dig or Escape Rope, Teleport, whiteout, script, game start. `MapChangeWork.changeKind` (0 to 4) is not decoded yet. |
| `pc.open` screen mode | The box screen's mode (organise, deposit, withdraw, Battle Box) is a script operand of `SCRCMD_PROC_FLD_CallOv170Event`. |
| `item.use` from the bag | `item.use` fires at the party screen's item check only. Field items used from the bag (Escape Rope, Repel, the bike) go another way, not yet found. Fly, Dig and Teleport are field moves: `field.move` refuses them. |
| `shop.buy` quantity | The event fires when an item is picked, before the quantity screen. A purchase is visible as `item.add` and `money.change` (source purchase). |
| Disobedience | A refusable obedience check, for level caps. |
| Type matchups | A writable type effectiveness in a battle. |
| Nicknames | Reading and setting a nickname; `mon.nickname` `force` on the catch screen (it has no applier). |
| Trainers | Trainer class, party size, highest level, whether a trainer was beaten. |
| `battle.end` took-part mask | Which party Pokemon took part. |
| Refusable day care deposit, trade start, Entralink, Dream World sync | New events. |
| `party.add` | A refusable event for any Pokemon joining the party. |
| `mon_give` | A mod-made Pokemon (species, level, egg, shiny, to a box): Egglocke, Wonderlocke. |
| Full party record export and import | Genlocke. `mon_export` and `mon_import` are in the services table. |
| `session.message` | Peer messages reaching rules (Soul Link). |
| Encounter tables | Reading a zone's encounter slots. |
| The link and minigame overlays (105, 107, 136, 137) | Not in the hookable set; their items read source `OTHER`. |
| Moves past 560 | Battle animations: the battle server's effect ids still need names (aletheia). |
| Field object codes | `FLDMMDL_OBJCODE_TABLE_COUNT` is named; the code-to-table remap is the port's. |
| Species to 2047 | Waits on aletheia's names for the species sites. |
| The wasm runtime | The contract is `sdk/api/v4`; the WAMR bridge is not built. |
