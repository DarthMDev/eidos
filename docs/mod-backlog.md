# Mod interface: work after the version 4 freeze

What the mod interface does not cover yet, and why each item waits. Every
item is verified against the game's code before it is built; one that
cannot be verified stays here rather than shipping half-working.

## Settled at the freeze

| Item | Outcome |
|---|---|
| `heal` from a Pokemon Centre | `heal` carries `script_id`, the id of the script that heals; the Centre's nurse is one script, so a mod tells her heal from the others by its id. A named `CENTRE` source waits until the nurse's id is confirmed from the script data. |
| `item.add` refusal | Not refusable in version 4. Some callers say the item was received whatever the add answers (a script checks for room first; a shop has taken the money), so a refusal would not have a clean outcome. Setting `count` to 0 keeps the item out of the bag; the game's text still says it was received. |
| `mon.evolve` after a battle | Verified: the post-battle step (`EVENT_BATTLE_RETURN_Main`) runs the evolution check for each member that levelled up and starts the scene only when the check answers a species, so a refusal skips that member's scene and the others go on. |

## After the freeze

| Item | Notes |
|---|---|
| A finer `route.change` `how` | Door or stairs, warp tile, Fly, Dig or Escape Rope, Teleport, whiteout, script, game start. `MapChangeWork.changeKind` (0 to 4) is not decoded yet. |
| `pc.open` screen mode | The box screen's mode (organise, deposit, withdraw, Battle Box) is a script operand of `SCRCMD_PROC_FLD_CallOv170Event`. |
| `item.use` from the bag | `item.use` fires at the party screen's item check only. Field items used from the bag (Escape Rope, Repel, the bicycle, the Dowsing Machine) go through the bag's own use step (`ITEMMENU_HandleActionMenuChoice`, action 0) and do not fire it. |
| Field moves on the party screen | Cut, Surf, Fly, Dig, Teleport and the others used from the party screen do not fire any event yet. `field.move` is the field action slot check the bag and the Y menu read (slot 0 the bicycle), not the party screen's moves; its other slots are still to be mapped. |
| `item.add` refusal | Refusable only at callers whose outcome is clean once each one is read (a gift that stays with the person, a held item that stays held). |
| `heal` source `CENTRE` | Once the nurse script's id is confirmed from the script data. |
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
| Wasm overrides | `override` and `original` answer -1 in a wasm mod. A hook of `hooks.json` needs a native function per prototype that calls the module's table entry; the pool is generated from `hooks.json`. |
| Wasm session, command and save handlers | `on_session`, `on_command` and `on_save_received` answer nothing in a wasm mod and say so once. Each needs its payload copied into the module the way an event's is. |
| Wasm limits | The memory cap (`memory = <MiB>`) and a step budget per call are not enforced yet; a module that loops forever stops the game. |
