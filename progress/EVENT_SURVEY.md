# Event survey: every mechanic a mod might hook

An event is a moment the port can tell a mod about. A mechanic is a system the
game runs. This survey lists the mechanics a mod might touch and the moments
inside each one. It comes from community randomizers, difficulty hacks, quality
of life patches, save editors, accessibility scripts, and the game's own script
command tables. It names no addresses.

The purpose is breadth. A moment that is wrong or unused is cheap to drop later.
A moment that is missing is expensive to add once mods bind to names.

## How to read this survey

The **Kind** column names how the moment exists in the game, which is what
decides the surface a mod can reach it on:

- **call**: a function the game calls. It can host an event or an override.
- **field**: a script command or a field interaction the game runs. It can host
  an event or an override.
- **table**: a data table or an asset the game reads. A mod replaces it through
  the data layer (a whole file or a NARC member, `docs/mods.md`).
- **flag**: a save flag, a variable, or a misc value. A mod reads or writes it
  as state.
- **state**: a live value in a struct. A mod reads or writes it as state.

The **Surface** column names what the moment can become for a mod: an **event**,
a piece of **state**, an **override** point, or a **data** override. A call or a
field can host an event and an override at once; the surface column names the
one the community use needs.

The **Source** column is a key into the source list at the end. Every mechanic
below is attributed to the source it was read from, with a URL for a web source
and a path for a file in this repository.

## 1. Randomizers

Sources: [UPR], [UPRZX], [POKERANDO], [PK3DS], [RANDX], [TYPECHART].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Wild encounter table read for a zone and slot | Replace the species and level per zone and slot | table | data | wild encounters [UPR] |
| Wild encounter slot roll | Reroll or weight which of the slots is chosen | call | event | encounter slots 0 to 11 [SMOGONRNG] |
| Wild encounter created | Replace the species or suppress the encounter | call | event | encounter.start [MODEVENTS] |
| Wild encounter level | Scale the level by a multiplier or a percentage | state | state | percentage level modifier [UPR] |
| Dark grass double encounter | Force or forbid the double battle in dark grass | flag | state | dark grass catch rate note [UPRZX] |
| Shaking grass, dust and ripple spot | Choose the special species (Audino, Drilbur) or force the spot | table | data | shaking grass [PSYPOKESFULL] |
| Fishing encounter table | Replace the rod encounter species | table | data | grass, surf and rod encounters [DSPRE] |
| Surfing encounter table | Replace the surf encounter species | table | data | surf encounters [DSPRE] |
| Swarm encounter table | Replace the daily swarm species | table | data | swarms [SEREBIISWARM] |
| Hidden Grotto contents | Replace the grotto species, ability or item | table | data | hidden grotto contents [BULBAGROTTO] |
| Static overworld Pokemon | Replace the species, level or form of a static | table | data | static Pokemon [UPR] |
| Roaming Pokemon | Replace the roamer species or its route set | flag | state | roamers in PID generation [PIDRNG] |
| Gift Pokemon from an NPC | Replace the species, level or held item of a gift | field | data | gifted Pokemon [UPR] |
| Starter choice | Replace the three starters or their level | field | data | starter Pokemon choices [UPR] |
| Starter sprite and cry | Replace the sprite and the cry the picker plays | table | data | starter graphics and cries [UPR] |
| Trainer party species | Replace the species on a trainer's team | table | data | trainer Pokemon [UPR] |
| Trainer party level | Scale a trainer's levels or set them outright | table | data | trainer level [POKERANDO] |
| Trainer class and name | Replace the class and the display name | table | data | trainer names and classes [UPR] |
| Trainer held items | Give or remove a trainer Pokemon's held item | table | data | trainer items [PK3DS] |
| Trainer movesets | Replace the four moves a trainer Pokemon has | table | data | trainer moves [PK3DS] |
| Trainer AI flags | Make a trainer's AI smarter or dumber | table | data | trainer AI flags [DSPRE] |
| Pokemon base stats | Change a species' base stats | table | data | base stats [UPR] |
| Pokemon types | Change a species' types | table | data | elemental types [UPR] |
| Pokemon abilities | Change the ability slots of a species | table | data | abilities [UPR] |
| Hidden ability slot | Change or enable the hidden ability | table | data | hidden abilities [PK3DS] |
| Evolution target and method | Change what a species evolves into and how | table | data | evolutions [UPR] |
| Trade evolution to level up | Replace a trade evolution with a level or item method | table | data | trade-only evolutions [UNOVADEX2] |
| Move properties | Change a move's power, accuracy, type or PP | table | data | move properties [UPR] |
| Level-up learnset | Change the moves a species learns by level | table | data | level up moves [UPR] |
| Egg move pool | Change the moves a species inherits | table | data | egg move randomization [POKERANDO] |
| TM contents | Change which move a TM teaches | table | data | TM contents [UPR] |
| TM and HM compatibility | Change which species can learn a machine | table | data | TM and HM compatibility [UPR] |
| Move tutor moves | Change what a tutor teaches | table | data | move tutor moves [UPR] |
| Wild held item table | Change the items a wild Pokemon can hold | table | data | wild held items [UPR] |
| Ground item ball contents | Change what an item ball holds | table | data | items on the ground [UPR] |
| Hidden item spots | Change what a hidden spot holds or where it is | table | data | hidden spots [UPR] |
| In-game trade Pokemon | Change the given and wanted Pokemon of a trade | table | data | in-game trades [UPR] |
| Static or legendary special music | Play or suppress the special battle music | table | data | fix music for statics [UPR] |
| Legendary inclusion toggle | Include or exclude legendaries from a pool | flag | state | legendary toggle [POKERANDO] |
| Species catch rate | Change the base catch rate of a species | table | data | minimum catch rate [UPRZX] |
| Type chart matchups | Change the whole effectiveness table | table | data | type chart randomizer [TYPECHART] |
| Shiny odds | Change the shiny rate or add a shiny charm effect | state | state | shiny charm and Masuda method [IGNBW2SHINY] |
| Encounter RNG seed | Read or set the seed that drives the roll | state | state | initial seed [SMOGONRNG] |
| Shop inventory randomization | Replace a mart's stock | table | data | shops randomized [RANDX] |
| Given item randomization | Replace the item an NPC or ball gives | table | data | found and given items [RANDX] |

## 2. Difficulty and challenge

Sources: [DRAYANO], [BB2], [REDUX], [SUPERKAIZO], [NUZLOCKEPEDIA],
[NUZLOCKEUNI], [NUZLOCKECAP], [NUZLOCKEIN], [SMOGONNUZ], [PLATINUMREDUX],
[BLUEMAZ].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Wild level scaling | Raise or lower wild levels globally | state | state | level boost multiplier [POKERANDO] |
| Trainer level scaling | Raise or lower trainer levels globally | table | data | 60 percent level scaling [SUPERKAIZO] |
| Difficulty mode chosen | Pick easy, normal or challenge | flag | state | three difficulty modes [REDUX] |
| Difficulty key unlock | Unlock the easy and challenge keys | flag | state | unlock key system [ARBW2] |
| Boss party size | Give a gym leader a full six Pokemon | table | data | every gym leader has six [SUPERKAIZO] |
| Boss held items | Give bosses sensible held items | table | data | sensible held items [SUPERKAIZO] |
| Boss movesets | Give bosses competitive moves | table | data | competitive movesets [PLATINUMREDUX] |
| Smart AI per trainer | Turn on the smarter move choice | table | data | smart AI patch [SUPERKAIZO] |
| AI item use in battle | Let the AI use or lose its items | call | override | trainer move selection AI [DSHACK] |
| Battle item ban | Forbid the player from using items in battle | call | event | no items during battle [NUZLOCKEPEDIA] |
| Level cap enforcement | Stop a Pokemon gaining a level past the cap | call | event | enforce level caps [NUZLOCKEPEDIA] |
| Level cap per boss | Set the cap to the next boss's ace | table | data | hardcore level caps [NUZLOCKECAP] |
| Experience from a battle | Scale or zero the experience reward | call | event | experience reward [GAMERGUIDES] |
| EV gain from a battle | Zero or scale the EV yield | table | data | EVs disabled for difficulty [PLATINUMREDUX] |
| First encounter only | Allow one catch per route and no more | call | event | first encounter only [NUZLOCKEUNI] |
| Faint means death | Mark a fainted Pokemon dead for the run | call | event | faint equals death [NUZLOCKEUNI] |
| Dupes clause | Reroll an encounter of an owned species | call | event | duplicates clause [NUZLOCKEUNI] |
| Species clause | Forbid two of one species on a team | call | event | species clause [NUZLOCKEIN] |
| Shiny clause | Allow a free catch of a shiny | call | event | shiny clause [NUZLOCKEUNI] |
| Gift clause | Treat a gift Pokemon as a free encounter | call | event | gift clause [NUZLOCKEPEDIA] |
| Whiteout handling | Continue after a wipe instead of losing the run | call | event | whiteout is not game over [NUZLOCKEPEDIA] |
| Set battle mode | Forbid switching after a faint | flag | state | use set battle mode [NUZLOCKEPEDIA] |
| Nickname requirement | Require a nickname on a catch | call | event | nickname all Pokemon [NUZLOCKEUNI] |
| Soullink pair table | Store which Pokemon are paired across two runs | state | state | soul link pairing [UPRZX] |
| Soullink faint mirror | Faint the paired Pokemon on the other instance | call | event | soullink faint [MODEVENTS] |
| Run state persistence | Keep the dead list and used routes across sessions | state | state | Nuzlocke run state [MODDING] |
| Pokemon Center use limit | Count and cap the number of heals | call | event | limit each Center to one use [NUZLOCKEPEDIA] |
| Team size limit | Cap the active party size | state | state | limit team size [NUZLOCKEPEDIA] |
| Item clause | Require unique held items on a team | state | state | item clause [NUZLOCKEIN] |
| Pokemon ban list | Forbid named species from the run | table | data | ban certain powerful Pokemon [NUZLOCKEPEDIA] |
| Mandatory trainer battles | Make an optional trainer fight unavoidable | table | data | every trainer battle is mandatory [BLUEMAZ] |
| Boss rematch | Reopen a gym leader or rival fight | flag | state | gym leader rematches [BWDELUXE] |
| Battle format per trainer | Set single, double, triple or rotation | table | data | mix of battle types [DRAYANO] |
| Rival keeps starter | Keep the rival's starter through the game | table | data | rival keeps starter [POKERANDO] |
| Type-themed gym | Give every gym one shared type | table | data | type theme to every gym [POKERANDO] |
| Wild AI opponent level curve | Match wild levels to the boss curve | state | state | level curve [DRAYANO] |

## 3. Quality of life

Sources: [ARBW], [ARBW2], [RUNBUN], [BW3GEN], [GAMERGUIDES], [DSHACKTM],
[RADIO], [CRYSTALLEGACY], [WILDSKANTO], [NUZLOCKEIN].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Experience Share held | Give all party members experience | flag | state | Gen 6 EXP Share [GAMERGUIDES] |
| Experience gain to the whole party | Share experience to the bench | call | event | exp share [GAMERGUIDES] |
| Experience formula | Change how experience is computed | call | override | experience reward [GAMERGUIDES] |
| Repel duration | Extend or make repels reusable | state | state | reusable repels [BW3GEN] |
| Repel re-use prompt | Prompt to reuse a repel when it runs out | flag | state | reusable repels [BW3GEN] |
| Running indoors | Allow running inside buildings | flag | state | running indoors [RUNBUN] |
| Movement speed | Raise the walking and running speed | call | override | double walking speed [ARBW] |
| Running shoes from the start | Grant the running shoes early | flag | state | running shoes from the start [BW3GEN] |
| Text speed setting | Change the text speed value | flag | state | text speed [SETTINGS] |
| Instant text | Print dialogue at once | call | override | instant text [ARBW] |
| Battle message speed | Speed up or skip battle messages | call | override | speed up battle messages [ARBW] |
| Skip cutscene | Skip a scripted scene | call | override | skip intro screens [RUNBUN] |
| Skip the intro | Jump the opening to the title | call | override | opening and intro editing [MASTERYESE] |
| Hold to fast battle | Hold a button to run messages and animations | call | override | hold R to speed through [ARBW2] |
| Auto-heal after a battle | Restore the party after each fight | call | event | auto level to cap and auto heal [NUZLOCKEIN] |
| Auto level to the cap | Raise a Pokemon to the level cap | call | event | auto level to cap [NUZLOCKEIN] |
| Box management | Sort, move and release whole boxes | call | state | batch box tools [PKHEX] |
| Bag sorting | Reorder the bag pockets | state | state | pack item sorting [CRYSTALLEGACY] |
| Reusable TMs | Stop a TM being consumed | call | override | reusable TMs [DSHACKTM] |
| Infinite PP | Stop PP being consumed | call | override | infinite PP [RADIO] |
| No low HP music | Suppress the low health music | call | override | no low HP music [ARBW2] |
| Faster save | Remove the artificial save delay | call | override | remove the save delay [CRYSTALLEGACY] |
| HM friend | Remove the need for an HM user in the party | call | override | no HM friends needed [NUZLOCKEIN] |
| Wild encounter suppression | Suppress random encounters in the field | call | event | random-encounter suppression [WILDSKANTO] |
| Force a special spot | Force shaking grass, dust or ripples | call | override | instant shaking grass [ARBW] |
| Visible overworld wilds | Draw wild Pokemon as field entities | call | event | visible overworld Pokemon [WILDSKANTO] |

## 4. Battle mechanics

Sources: [MODEVENTS], [PK3DS], [UPR], [DSHACK], [VEEKUN], [POKETOOLS],
[DRAYANO], [SEREBIIPOKESTAR], [PURERGB].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Battle setup created | Adjust the setup before the game fills it | call | event | battle.start [MODEVENTS] |
| Wild battle setup | Set up a wild battle's species and level | call | event | BTL_SETUP_SetupWild [MODEVENTS] |
| Trainer battle setup | Read the trainer before the battle starts | call | event | encounter.trainer [MODEVENTS] |
| Damage formula | Change the damage calculation | call | override | damage formula [PK3DS] |
| Critical hit roll | Change or remove the critical chance | call | override | critical hit [SUPERKAIZO] |
| Type effectiveness lookup | Change a matchup on the fly | table | data | type chart [TYPECHART] |
| Physical and special split | Change a move's damage category | table | data | physical and special split [REDUX] |
| Status application | Add, block or change a status infliction | call | event | status condition [VEEKUN] |
| Status cure | Change how a status is cured | call | event | status healing [DSPRE] |
| Catch attempt | Block, force or modify a throw | call | event | catch.attempt [MODEVENTS] |
| Catch rate computation | Change the catch rate for a throw | call | override | ball catch rate [MODEVENTS] |
| Ball modifier | Change a ball's multiplier | table | data | ball effects [VEEKUN] |
| Critical capture | Change the critical capture chance | call | override | critical capture [POKETOOLS] |
| Stat stages | Change how a stat stage is applied | call | event | stat stages [VEEKUN] |
| Ability effect | Change or replace an ability's effect | call | override | abilities [UPR] |
| Held item effect | Change or replace a held item's effect | table | data | hold effects [DSPRE] |
| Switching | Observe or change a switch | call | event | switching [WIKIPEDIA] |
| Fainting | Observe a faint before the reward step | call | event | battle.faint [MODEVENTS] |
| Battle end and result | Read the outcome when the battle ends | call | event | battle result [MODEVENTS] |
| Experience and level up | Change the experience and the level gain | call | event | experience [GAMERGUIDES] |
| Evolution after level up | Force or block an evolution | call | override | evolutions [UPR] |
| Learning a move on level up | Change or skip the learn prompt | call | event | learnset [UPR] |
| Forgetting a move | Skip or change the forget prompt | call | event | hide the forget question [PURERGB] |
| TM or HM use | Change what a machine teaches or its use | call | event | TM use [DSHACKTM] |
| Move relearner | Change the relearnable move set | field | event | move relearner [DSHACK] |
| Move deleter | Change what a move deleter allows | field | event | move deleter [DSHACKTM] |
| Battle RNG seed | Read or set the battle's random seed | state | state | initial seed [SMOGONRNG] |
| AI move choice | Change how the AI picks a move | call | override | trainer move selection AI [DSHACK] |
| Battle weather | Set or change the weather in battle | call | event | weather in battle [FANDOMWEATHER] |
| Battle terrain or effect | Set a field effect or a room | call | event | weather effects [SUPERKAIZO] |
| Party heal by script | Block or change a scripted heal | call | event | heal.party [MODEVENTS] |
| PP recovery | Block or change a PP recovery | call | override | RecoverAllPP [MODEVENTS] |
| Battle animation toggle | Turn the battle scene on or off | flag | state | battle scene [SETTINGS] |
| Battle style | Set shift or set mode | flag | state | battle style [SETTINGS] |

## 5. Overworld

Sources: [MODEVENTS], [MODCOOP], [ARBW], [PSYPOKESFULL], [PSYPOKESDAILY],
[SEREBIISWARM], [BULBAGROTTO], [SEREBIIBGM], [DSHACK], [ARSCRIPT], [WILDSKANTO],
[NINTENDOUK].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Map load | Read the new zone when the field loads it | call | event | route.change [MODEVENTS] |
| Zone header applied | Change a zone's header before it applies | call | event | ApplyZoneHeader [MODEVENTS] |
| Field event change | Change the field event list of a zone | call | event | ChangeFieldEvent [MODEVENTS] |
| Warp to a position | Redirect or observe a warp | field | event | SCRCMD_WarpToPosition [SCRCMD] |
| Warp to a fixed zone | Redirect a scripted zone warp | field | event | SCRCMD_WarpToFixedZone [SCRCMD] |
| Warp to a rail location | Redirect a rail or elevator warp | field | event | SCRCMD_WarpToRailLocation [SCRCMD] |
| Talk to an NPC | Intercept the interaction and its script | field | event | NPC interaction [ARSCRIPT] |
| Read a sign or a bookshelf | Intercept a text object's read | field | event | text object [ARSCRIPT] |
| Jump a ledge | Allow, block or observe a ledge jump | field | event | hop edge and rail jump [MAPATTR] |
| Surf | Allow or observe surfing | field | event | surf encounters [DSPRE] |
| Fish | Allow or observe fishing | field | event | rod encounters [DSPRE] |
| Running shoes state | Read or set whether running is allowed | flag | state | running shoes [BW3GEN] |
| Item ball pickup | Change what the ball gives | field | data | item balls [UPR] |
| Hidden item pickup | Change what a hidden spot gives | field | event | hidden item flag [SCRCMD] |
| Wild encounter roll on a step | Suppress or weight the step roll | call | event | FIELDMAP_GetEncount [MODEVENTS] |
| Weather set by script | Set or change the field weather | field | event | setting the weather from a script [DSHACK] |
| Weather of a zone by date | Read the weather a zone has today | call | state | CALENDER_GetWeatherOnDate [CALENDER] |
| Time of day | Read morning, day, evening or night | state | state | daily events [PSYPOKESDAILY] |
| Season change | Read or set the current season | state | state | seasons change [NINTENDOUK] |
| Season display flag | Read whether the season banner shows | state | state | GetSeasonDispFlag [CALENDER] |
| Daily event | Read or reset a once a day event | flag | state | daily events [PSYPOKESDAILY] |
| Weekly event | Read or reset a once a week event | flag | state | weekly events [PSYPOKESDAILY] |
| Seasonal event | Read or reset a seasonal event | flag | state | seasonal events [PSYPOKESDAILY] |
| Swarm spawn | Set or clear the daily swarm | flag | state | swarms change at midnight [SEREBIISWARM] |
| Hidden Grotto regeneration | Force or block a grotto refill | flag | state | grotto regeneration [BULBAGROTTO] |
| Shaking spot spawn | Force a shaking grass or dust spot | field | event | shaking grass [PSYPOKESFULL] |
| Bicycle use | Allow or block the bicycle | flag | state | CheckBicycleUsable [CALENDER] |
| Player position | Read or write the player's field position | state | state | FIELD_PLAYER_GetPos [MODCOOP] |
| Camera | Read or write the field camera | state | state | FIELDMAP_GetCamera [MODCOOP] |
| NPC spawn and despawn | Add or remove a field entity | call | event | FIELDMAP_AddZoneNpcs [MODCOOP] |
| Plate door gimmick | Observe or change a door puzzle | field | event | PLATE_DOORS [SCRCMD] |
| Elevator or lift | Observe or change a lift | field | event | LIFT_ELEVATOR [SCRCMD] |
| Rotating room | Observe or change a rotating room | field | event | ROTATING_ROOM [SCRCMD] |
| Field flash or map effect | Change the map flash or effect | call | override | FIELD_FLASH_Apply [FLASH] |
| Fly to a location | Redirect a fly destination | field | event | fly warp [SCRCMD] |
| Water ripple effect | Observe or change a water ripple | call | event | FLDEFF_RIPPLE [RIPPLE] |
| Area music | Change the field music by area or season | table | data | area music [SEREBIIBGM] |
| Movement-based music | Add or remove the step-driven music layer | call | override | movement-based changes [SEREBIIBGM] |
| Instrument NPC music | Add or remove an instrument layer | field | event | instrument additions [SEREBIIBGM] |

## 6. Inventory and economy

Sources: [MODEVENTS], [MODINTERFACE], [DSPRE], [DSHACK], [PKHEX], [VEEKUN],
[PSYPOKESDAILY], [SUPERKAIZO].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Receive an item | Change or block the item given | field | event | item gift [UPR] |
| Use an item in the field | Change what a field use does | call | event | field use function [DSPRE] |
| Use an item in battle | Change what a battle use does | call | event | battle use function [DSPRE] |
| Bag pocket and capacity | Change the pocket or the capacity | state | state | merged bag capacity [POKERANDO] |
| Bag item count | Read or write how many of an item are held | state | state | bag items [PKHEX] |
| Money gain | Add to the player's money | state | state | BEACON_AddPoints [MODEVENTS] |
| Money spend | Subtract from the player's money | state | state | BEACON_SubPoints [MODEVENTS] |
| Money cap | Change the 9,999,999 cap | state | state | money cap [MODINTERFACE] |
| Money display | Change how money is shown | call | override | ITEMMENU_PrintMoney [MODINTERFACE] |
| Shop inventory | Replace a mart's stock | table | data | customise PokeMarts [DSHACK] |
| Shop purchase | Observe or change a purchase | field | event | item purchase event [MODINTERFACE] |
| Shop badge gating | Gate a stock item on a badge | flag | state | badge gating [DSHACK] |
| Shop price | Change an item's price | table | data | item price [DSPRE] |
| Sell price | Change an item's sell value | table | data | item price [DSPRE] |
| Pokemon Center heal | Block or change the party heal | call | event | heal.party [MODEVENTS] |
| Nurse interaction | Observe or change the Center nurse | field | event | Pokemon Center [PSYPOKESFULL] |
| Daycare deposit and withdraw | Observe or change a daycare action | call | event | DAYCARE_SetSlotMon [DAYCARE] |
| Daycare egg ready | Force or block an egg | flag | state | DAYCARE_GetEggReady [DAYCARE] |
| Item from an NPC | Change or block a daily item gift | field | event | daily item gifts [PSYPOKESDAILY] |
| Pickup ability item | Change or block the item Pickup yields | call | event | Pickup ability [SUPERKAIZO] |
| Exchange corner | Change the Battle Point exchange stock | table | data | exchange service corner [SUBWAY] |

## 7. Progression

Sources: [MODEVENTS], [MODINTERFACE], [SULOKU], [PKHEX], [DSPRE], [SEREBIIDEX],
[PSYPOKESDAILY], [UPR], [UNOVADEX2], [SEREBIIGL], [REDUX].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Badge received | Observe or set a badge | flag | state | badges editor [SULOKU] |
| Badge check at a gate | Read whether a badge opens a gate | flag | state | badge gates [SULOKU] |
| Story flag set | Set a story flag | flag | state | save flags [MODEVENTS] |
| Story flag clear | Clear a story flag | flag | state | SAVE_EVENT_Write [MODEVENTS] |
| Variable or misc value | Read or write a script variable | state | state | SetMiscValue [SCRCMD] |
| Pokedex seen | Mark a species seen | state | state | ZUKAN_SAVE_IsCaught [MODINTERFACE] |
| Pokedex caught | Mark a species caught | state | state | dex caught [SEREBIIDEX] |
| Pokedex completion reward | Grant the Oval or Shiny Charm | flag | state | dex completion [SEREBIIDEX] |
| Box access | Read or write a box record | state | state | box accessors [MODEVENTS] |
| Party add and remove | Add or remove a party member | state | state | POKEPARTY_Add [MODEVENTS] |
| Party count | Read the party size | state | state | POKEPARTY_GetCount [MODEVENTS] |
| Trainer card | Read or write the trainer card | state | state | trainer card [MASTERYESE] |
| Trainer name and gender | Read or write the player identity | state | state | trainer information [SULOKU] |
| Play time | Read or write the play clock | state | state | play time [PKHEX] |
| Event or gift Pokemon | Change a scripted gift | field | event | event Pokemon [UPR] |
| In-game trade | Change a trade's Pokemon | table | data | trade editor [DSPRE] |
| Trade evolution | Change a trade evolution | table | data | trade-only evolutions [UNOVADEX2] |
| Version-exclusive encounter | Add the other version's encounter | table | data | version exclusives [UNOVADEX2] |
| Medal or achievement | Grant or revoke a medal | flag | state | medal editor [SULOKU] |
| Key system state | Set the easy or challenge key | flag | state | key system editor [SULOKU] |
| Memory Link | Read or write the BW to BW2 link data | state | state | memory link editor [SULOKU] |
| Join Avenue | Read or write a Join Avenue state | state | state | Join Avenue editor [SULOKU] |
| Entralink forest | Read or write the forest block | state | state | Entralink forest editor [SULOKU] |
| Prop Case | Read or write the prop inventory | state | state | prop case editor [SULOKU] |
| Musical data | Read or write the Musical state | state | state | musical data [SULOKU] |
| C-Gear skin | Read or set the C-Gear skin | state | state | C-Gear skins [SEREBIIGL] |
| Pokedex skin | Read or set the dex skin | state | state | dex skins [SEREBIIGL] |
| Hall of Fame | Read or write the Hall of Fame | state | state | Hall of Fame [REDUX] |
| Berry or item giving NPC | Change a daily gift | field | event | daily gifts [PSYPOKESDAILY] |

## 8. Save and system

Sources: [MODEVENTS], [MODINTERFACE], [SAVEH], [RTC], [FRAME], [SULOKU],
[PKHEX], [SETTINGS], [README], [SMOGONRNG].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Save start | Write mod state before the game writes | call | event | save.begin [MODEVENTS] |
| Save done | Run work after the save completes | call | event | save.done [MODEVENTS] |
| Save load | Read mod state when a save loads | call | event | save.load [MODEVENTS] |
| Save block read and write | Reach a save block by id | state | state | SAVE_CONTROL_GetBlock [MODINTERFACE] |
| Save backup and copy | Copy or restore a save file | call | override | save backup [PKHEX] |
| Save checksum | Verify or correct the checksum | call | override | checksum verifier [SULOKU] |
| Save flag read | Read a save flag | state | state | CheckSaveFlag [MODEVENTS] |
| Save flag write | Write a save flag | state | state | SAVE_EVENT_Write [MODEVENTS] |
| Block dumper and injector | Read or replace a whole save block | state | state | block dumper [SULOKU] |
| RNG seed | Read or set the initial seed | state | state | initial seed [SMOGONRNG] |
| Real-time clock read | Read the host or fixed clock | state | state | RTC read [RTC] |
| RTC alarm | Set or observe an RTC alarm | call | event | alarm registers [RTC] |
| Overlay load | Run a mod hook when an overlay loads | call | event | overlay.load [MODEVENTS] |
| Overlay start | Run a mod hook when an overlay starts | call | event | overlay.start [MODEVENTS] |
| Overlay unload | Run a mod hook when an overlay unloads | call | event | overlay lifecycle [MODINTERFACE] |
| Sound system | Change the SDAT sequencer or mixer | call | override | SDAT sequencer [README] |
| Master volume | Read or set the master volume | state | state | Sound tab [SETTINGS] |
| Input read | Read the pad and touch state | state | state | input [README] |
| Key binding | Remap a pad or key control | state | state | Controls tab [SETTINGS] |
| Frame tick and OS tick | Read the frame count and the OS tick | state | state | frame skeleton [FRAME] |
| Frame pacing and speed | Change the frame rate or speed | call | override | frame pacer [README] |
| Time of day from RTC | Read the clock's time of day | state | state | RTC read [RTC] |
| Console date and time | Set the host date and time | state | state | set console date and time [SMOGONRNG] |
| Save directory | Read or set the save folder | state | state | bw_save_path [SAVEH] |
| Save version | Read the save format version | state | state | save compatibility [MODDING] |
| Settings file | Read or write the port settings | state | state | settings file [SETTINGS] |

## 9. Multiplayer and online

Sources: [MODEVENTS], [MODDING], [MODCOOP], [SEREBIICGEAR], [SEREBIIWIFI],
[SEREBIIENTRA], [WIIMMFI], [PKMNCLASSIC], [PLATINUMMP], [PKGBA].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Link up | Open the exchange when the link turns on | call | event | link.up [MODEVENTS] |
| Link down | Close the exchange when the link turns off | call | event | link.down [MODEVENTS] |
| Link level | Read the link level | state | state | WCM_GetLinkLevel [MODEVENTS] |
| Net send | Observe or change bytes leaving the process | call | event | net.send [MODEVENTS] |
| Net receive | Observe or change bytes arriving | call | event | net.receive [MODEVENTS] |
| Wm init and enable | Drive or replace the wireless manager | call | override | WM_Init [DECLAREDSET] |
| Wm connect and MP | Drive or replace a wireless session | call | override | WM_StartConnectEx [DECLAREDSET] |
| Wm data sharing | Drive or replace shared data | call | override | WM_StartDataSharing [DECLAREDSET] |
| Wm receive FIFO | Drive or replace the receive path | call | override | WmReceiveFifo [DECLAREDSET] |
| DWC init | Drive or replace the WiFi library | call | override | DWCi_Init [DECLAREDSET] |
| GAME_COMM update and handler | Drive or replace the comm handler | call | override | GAME_COMM_Update [DECLAREDSET] |
| Trade request | Agree on a trade offer | call | event | a trade [MODDING] |
| Trade exchange | Move the traded Pokemon | call | event | trade support [PKGBA] |
| Battle turn agreement | Agree on a shared battle turn | call | event | a battle turn [MODDING] |
| Player position sync | Share each player's field position | call | event | a player's position [MODDING] |
| Co-op second player | Add a second body to the field | call | event | see each other in-game [PLATINUMMP] |
| C-Gear enable | Turn the C-Gear on or off | flag | state | C-Gear [SEREBIICGEAR] |
| Entralink entry | Enter or leave the Entralink | field | event | Entralink [SEREBIIENTRA] |
| Pass Power | Read or set a Pass Power | state | state | Pass Powers [SEREBIIENTRA] |
| Game Sync | Sync to the Global Link | call | override | Game Sync [SEREBIIWIFI] |
| GTS deposit and search | Observe or change a GTS trade | call | event | GTS [SEREBIIWIFI] |
| Random Match | Observe or change a random battle | call | event | Random Match [SEREBIIWIFI] |
| Xtransceiver | Start or stop a video call | call | event | Xtransceiver [SEREBIIWIFI] |
| Wireless and IR exchange | Observe a local exchange | call | event | IR options [SEREBIICGEAR] |
| WiFi server address | Redirect the online service | call | override | custom DNS [WIIMMFI] |
| GTS and battle video server | Point the game at a community server | call | override | community server [PKMNCLASSIC] |

## 10. Cosmetics and data

Sources: [MODS], [SETTINGS], [SEREBIIBGM], [SEREBIIGL], [SMASHMUSIC],
[SOUNDFONT], [FONT], [ARSCRIPT], [MASTERYESE], [TYPECHART], [DSPRE].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Pokemon textures and sprites | Replace a species sprite | table | data | sprites [MODS] |
| Pokemon 3D models | Replace a species model | table | data | NSBMD utilities [DSPRE] |
| Overworld sprites | Replace an overworld sprite | table | data | overworld sprites [MASTERYESE] |
| Trainer sprites and VS faces | Replace a trainer sprite or face | table | data | trainer sprites [MASTERYESE] |
| Pokemon icons | Replace a party or dex icon | table | data | Pokemon icons [MASTERYESE] |
| Music and fanfares | Replace a track or a fanfare | table | data | music [SMASHMUSIC] |
| Sound effects and cries | Replace a cry or an effect | table | data | Pokemon cries [MASTERYESE] |
| Soundfont and instruments | Change an instrument or sample | table | data | BW soundfont [SOUNDFONT] |
| Fonts | Replace the text font | table | data | change the font [FONT] |
| UI text | Replace the port's own UI strings | table | data | UI text [SETTINGS] |
| Message text | Replace a message bank member | table | data | labels [SETTINGS] |
| Menu graphics and palettes | Replace a menu background or palette | table | data | menu palette [DSPRE] |
| Title screen | Replace the title art or text | table | data | title screen [BWDELUXE] |
| Type chart | Replace the effectiveness table | table | data | type chart [TYPECHART] |
| Data tables in general | Replace any table by NARC member | table | data | member override [MODS] |
| C-Gear skin | Replace the C-Gear art | table | data | C-Gear skins [SEREBIIGL] |
| Pokedex skin | Replace the dex art | table | data | dex skins [SEREBIIGL] |
| Battle backgrounds | Replace a battle backdrop | table | data | battle sprites [MASTERYESE] |
| Text colour and alignment | Change a message's colour or align | field | data | colour and buffering [ARSCRIPT] |
| Seasonal music arrangement | Replace a season's music arrangement | table | data | seasonal changes [SEREBIIBGM] |
| Weather graphics | Replace a weather effect's art | table | data | weather from a script [DSHACK] |

## 11. Accessibility and tools

Sources: [POKEMONACCESS], [CRYSTALACCESS], [RUNBUN], [EONTIMER], [POKELUA],
[MELONDSLUA], [SINISTER], [README], [PKHEX].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Text size | Scale the dialogue and menu text | table | data | font [FONT] |
| Text colour and contrast | Raise the contrast of text | table | data | high contrast [SINISTER] |
| Screen reader text output | Speak the on-screen text | call | event | read text on screen [CRYSTALACCESS] |
| Read the map name | Speak the current map | state | state | read current map name [CRYSTALACCESS] |
| Read the player position | Speak the player's coordinates | state | state | read current position [CRYSTALACCESS] |
| Read the surrounding tiles | Speak the tiles around the player | state | state | read surrounding tiles [CRYSTALACCESS] |
| Read enemy health | Speak the opponent's HP | state | state | read enemy health [CRYSTALACCESS] |
| Pathfinding | Walk the player to a target | call | event | pathfind [POKEMONACCESS] |
| Camera movement | Move the camera for a screen reader | state | state | camera keys [CRYSTALACCESS] |
| Audio cue | Play a sound for a game event | call | event | audio cues [POKEMONACCESS] |
| Debug menu | Open a debug or configuration overlay | call | override | debug overlay [README] |
| Speedrun timer | Show a run timer on screen | call | state | speedrun timing [EONTIMER] |
| RNG display | Show the seed and the frame | state | state | RNG Lua script [POKELUA] |
| RNG timer | Show a precision timer for RNG | state | state | precision timer [EONTIMER] |
| Input recording | Record and replay input | state | state | input functions [MELONDSLUA] |
| Script hooks | Run a host script on a frame | call | event | Lua scripting [MELONDSLUA] |
| Memory viewer | Watch a RAM address | state | state | RAM watch [ARSCRIPT] |
| Frame counter | Show the frame count | state | state | frame counter [MELONDSLUA] |
| Speed display | Show the emulator speed | state | state | show speed [CRYSTALACCESS] |
| Bag capacity merge | Raise the merged bag capacity | state | state | merged bag capacity [POKERANDO] |
| Save state | Save and restore a snapshot | call | override | save states [MELONDSLUA] |

## 12. Categories the brief misses

### Breeding and daycare

Sources: [IGNBW], [IGNBW2SHINY], [DAYCARE], [PSYPOKESDAILY], [POKEBEACH].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Daycare step count | Read or change the steps an egg needs | state | state | DAYCARE_AddSteps [DAYCARE] |
| Egg generation | Force, block or reroll an egg | call | event | egg ready [DAYCARE] |
| Egg shiny roll | Change the shiny roll of an egg | call | override | Masuda method [IGNBW2SHINY] |
| Egg move inheritance | Change the inherited moves | table | data | egg moves [POKERANDO] |
| Egg hatch steps | Halve or change the hatch steps | call | override | Flame Body [POKEBEACH] |
| Nature and ability inheritance | Change what an egg inherits | call | override | breeding inheritance [PKHEX] |
| Daycare level up | Change whether a daycare Pokemon levels | call | override | daycare levels up [IGNBW2SHINY] |

### Story and scripting

Sources: [MODINTERFACE], [SCRCMD], [ARSCRIPT], [MASTERYESE].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Script VM step | Observe or replace a script command | call | event | VM_Create [MODINTERFACE] |
| Script command dispatch | Replace a command's behaviour | field | override | script command table [SCRCMD] |
| Cutscene start | Skip or change a scripted scene | field | event | script editing [MASTERYESE] |
| Field message window | Change a message window | field | event | message window [SCRCMD] |
| Yes or no prompt | Observe or change a choice | field | event | yes no menu [SCRCMD] |
| Overlay sequence event | Run a field overlay sequence | call | event | CALENDER_OverlaySequenceEvent [CALENDER] |
| Story flag gate | Read a flag that gates a scene | flag | state | story flags [MODEVENTS] |

### Postgame and battle facilities

Sources: [SUBWAY], [UNOVACHALLENGE], [SEREBIIPOKESTAR], [IGNBW], [REDUX].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Battle Subway run | Change the run rules or rewards | call | event | Battle Subway [SUBWAY] |
| Battle Subway streak | Read or set the win streak | state | state | win streak [SUBWAY] |
| Black Tower or White Treehollow floor | Change the floor or the gate trainer | call | event | Unova Challenge [UNOVACHALLENGE] |
| Battle Institute | Change the Institute rules | call | event | battle facilities [UNOVACHALLENGE] |
| PWT tournament | Change the tournament bracket | call | event | Pokemon World Tournament [SEREBIIPOKESTAR] |
| Facility BP reward | Change the Battle Point reward | state | state | BP reward [SUBWAY] |
| Postgame quest | Change a postgame story step | flag | state | expanded postgame [REDUX] |
| Legendary encounter | Change a postgame legendary | table | data | expanded legendary encounters [REDUX] |

### Minigames and side activities

Sources: [SEREBIIPOKESTAR], [WIKIPEDIA], [SEREBIIENTRA].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Pokemon Musical | Change the Musical or its props | call | event | Pokemon Musicals [WIKIPEDIA] |
| PokeStar Studios movie | Change a movie's script or reward | call | event | PokeStar Studios [SEREBIIPOKESTAR] |
| Royal Unova trip | Change the cruise or its fights | call | event | Royal Unova [WIKIPEDIA] |
| Entralink mission | Change a mission or its prize | call | event | Entralink missions [SEREBIIENTRA] |
| Funfest Mission | Change a Funfest mission | call | event | Funfest Missions [BULBAGROTTO] |
| Battle Box or team set | Read or write a saved team | state | state | battle box [MODINTERFACE] |

### Save editing and legality

Sources: [PKHEX], [SULOKU], [POKEMONRNG].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Legality check | Check or legalize a Pokemon | call | override | legality checker [PKHEX] |
| Batch box edit | Heal, shiny or sort whole boxes | call | state | batch editor [PKHEX] |
| Pokemon transfer | Convert between generations | call | override | transfer [PKHEX] |
| Mystery Gift import | Add a wonder card | state | state | Mystery Gift files [PKHEX] |
| Encrypted block decrypt | Decrypt or re-encrypt a save block | state | state | block decryption [SULOKU] |
| Trainer Shiny Value | Read the TSV and PSV | state | state | TSV [POKEMONRNG] |

### Online revival

Sources: [WIIMMFI], [PKMNCLASSIC], [NINTENDOUK].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Online service address | Point the game at a replacement server | call | override | custom DNS [WIIMMFI] |
| GTS listing | Publish or fetch a GTS trade | call | event | GTS [PKMNCLASSIC] |
| Battle video upload | Upload or fetch a battle video | call | event | battle videos [PKMNCLASSIC] |
| WFC shutdown check | Report the service as on or off | call | override | WFC discontinued [NINTENDOUK] |

### Emulation and performance

Sources: [MELONDSLUA], [README], [SETTINGS].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Frame skip | Drop or repeat frames | call | override | frame pacer [README] |
| Fast forward | Run the game faster | call | override | speed [README] |
| Texture scaling | Scale the 3D textures | state | state | filter and scale [SETTINGS] |
| Widescreen | Render wider than 4:3 | call | override | widescreen [README] |
| Save state slot | Read or write a snapshot slot | state | state | save states [MELONDSLUA] |
| Touch and pad injection | Inject a touch or a pad press | state | state | input functions [MELONDSLUA] |

### Localisation and text

Sources: [FONT], [ARSCRIPT], [SETTINGS], [PROJECTPOKEMON].

| Moment | What a mod does | Kind | Surface | Mechanic (source) |
|---|---|---|---|---|
| Text encoding | Change the character table | table | data | text handling [FONT] |
| Text insertion | Replace a message bank | table | data | translation text [PROJECTPOKEMON] |
| UI label | Replace a port UI label | table | data | UI text [SETTINGS] |
| Region and language | Read or set the game language | state | state | language [MODINTERFACE] |
| Text colour code | Change a message's colour code | field | data | colour and buffering [ARSCRIPT] |

## What the port reaches today

The port exposes a data and asset layer today, and a compiled-in hook. It
exposes no run-time event API, no run-time override, no field code, and no
transport. Each row above therefore lands in one of three groups.

- **Reachable as data today.** A whole-file override and a NARC member override
  work (`docs/mods.md`, `sdk/vfs.c`, `sdk/vfs_narc.c`). Every `table` row is a
  data override. This is the only fully working surface.
- **Reachable as a build-time hook today.** A port C file compiled in place of
  one matched function works (`docs/hooks.md`, `stubs/reimpl_*`,
  `tools/ovlink.py`). Every `call` row can be an override this way, but a
  player-side mod cannot add one at run time.
- **Not reachable today.** A run-time event, a run-time override, per-mod
  state, the field system, the battle system, and the transport are all still
  to build (`docs/mod-interface.md`, `docs/modding.md`, `docs/mod-coop.md`,
  `README.md`, Milestone 7 and Milestone 8).

The port cannot support a moment today when the code that runs it is not
compiled. The field system, the battle system, the script VM and the field
script commands are outside the current build list (`docs/mod-coop.md`,
"the port exposes none of the field code today"; `README.md`, Milestone 7).
Every overworld, battle, progression and scripting moment above depends on
Milestone 7. The transport moments depend on a Wm or DWC responder, which
`sdk/arm7/` does not have and the tree has no sockets for (`docs/modding.md`,
"Networking seam: missing").

Three narrower limits shape a design:

- An overlay's data is reset on every load, so a write into overlay data does
  not survive the next load (`docs/mod-interface.md`, section 4).
- The party record's field ids for HP and the stats are not named by aletheia,
  so a mod cannot read them through the accessor yet
  (`docs/mod-interface.md`, still-to-build item 15).
- A DS address is the durable key; a function name is not, because aletheia
  renames and re-splits files (`docs/modding.md`, "An override is keyed on a DS
  address").

## Source list

Web sources, by key.

- [UPR] Dabomstew Universal Pokemon Randomizer readme, https://github.com/Dabomstew/universal-pokemon-randomizer/blob/master/readme.txt
- [UPRZX] Ajarmar Universal Pokemon Randomizer ZX, Wild Pokemon wiki, https://github.com/Ajarmar/universal-pokemon-randomizer-zx/wiki/Wild-Pokemon
- [POKERANDO] ArcanoxDragon PokeRandomizer, https://github.com/ArcanoxDragon/PokeRandomizer
- [PK3DS] kwsch pk3DS, https://github.com/kwsch/pk3DS
- [RANDX] Randomizer X, Eevee Expo, https://eeveeexpo.com/resources/1457
- [TYPECHART] NPO-197 PokemonTypeChartRandomizer, https://github.com/NPO-197/PokemonTypeChartRandomizer
- [DRAYANO] Drayano Blaze Black and Volt White, https://projectpokemon.org/home/forums/topic/13244-pok%C3%A9mon-blaze-black-pok%C3%A9mon-volt-white
- [BB2] Drayano Blaze Black 2 and Volt White 2, https://projectpokemon.org/home/forums/topic/24126-pok%C3%A9mon-blaze-black-2-pok%C3%A9mon-volt-white-2
- [REDUX] Blaze Black 2 and Volt White 2 Redux, PokeHackDB, https://pokehackdb.com/hacks/pokemon-bb2-vw2-redux
- [SUPERKAIZO] SuperKaizoIronMON rules, https://github.com/PyroMikeGit/SuperKaizoIronMON
- [NUZLOCKEPEDIA] Nuzlocke University optional rules, https://nuzlockeuniversity.ca/optional-rules
- [NUZLOCKEUNI] Nuzlocke University rules, https://nuzlockeuniversity.ca/nuzlocke-rules
- [NUZLOCKECAP] Nuzlocke University hardcore level caps, https://nuzlockeuniversity.ca/2022/01/18/hardcore-nuzlocke-level-caps-by-generation
- [NUZLOCKEIN] nuzlocke.in rules, https://www.nuzlocke.in/rules
- [SMOGONNUZ] Smogon introduction to Nuzlockes, https://www.smogon.com/articles/introduction-nuzlockes
- [UNOVADEX2] Black 2 and White 2 Complete Unova Pokedex Edition, https://www.romhacking.net/hacks/8981
- [BW3GEN] Pokemon Black and White 3 Genesis, https://www.pokemoncoders.com/pokemon-black-and-white-3-genesis
- [RUNBUN] Hacking a ROM hack, Run and Bun accessible, https://www.reddit.com/r/PokemonROMhacks/comments/1vrottd/hacking_a_rom_hack_finally_making_runbun
- [ARBW] Project Pokemon Black and White Action Replay codes, https://projectpokemon.org/home/forums/topic/12573-pok%C3%A9mon-black-and-white-ue-action-replay-codes
- [ARBW2] Blaze Black 2 and Volt White 2 documentation, emulatorhacks, https://www.emulatorhacks.com/2021/08/blaze-black-2-and-volt-white-2-documentation.html
- [DSPRE] DS Pokemon ROM Editor Reloaded, https://github.com/DS-Pokemon-Rom-Editor/DSPRE
- [DSHACK] DS Pokemon Hacking wiki, getting started, https://ds-pokemon-hacking.github.io/docs/generation-iv/guides/getting_started/
- [DSHACKTM] DS Pokemon Hacking wiki, enabling reusable TMs, https://ds-pokemon-hacking.github.io/docs/generation-iv/guides/pt_hgss-reusabletms
- [VEEKUN] veekun pokedex item prose, https://github.com/veekun/pokedex/blob/master/pokedex/data/csv/item_prose.csv
- [POKETOOLS] PokeTools catch rate calculator, http://www.poketools.com/catch-rate
- [SEREBIIBGM] Serebii Black and White background music changes, https://www.serebii.net/blackwhite/bgmusic.shtml
- [SEREBIISWARM] Serebii Black and White swarms, https://www.serebii.net/blackwhite/swarms.shtml
- [SEREBIIDEX] Serebii Black 2 and White 2 Pokedex completion, https://www.serebii.net/black2white2/dexcompletion.shtml
- [SEREBIICGEAR] Serebii Black and White C-Gear, https://www.serebii.net/blackwhite/cgear.shtml
- [SEREBIIWIFI] Serebii Black and White WiFi features, https://www.serebii.net/blackwhite/wifi.shtml
- [SEREBIIENTRA] Serebii Black and White Entralink, https://www.serebii.net/blackwhite/entralink.shtml
- [SEREBIIGL] Serebii Global Link C-Gear and Musical downloads, https://www.serebii.net/blackwhite/globallinkdownloads.shtml
- [SEREBIIPOKESTAR] Serebii Black 2 and White 2 PokeStar, https://www.serebii.net/black2white2/pokestar.shtml
- [PSYPOKESDAILY] Psypokes Black and White calendar, daily events, https://www.psypokes.com/bw/calendar.php?view=daily
- [PSYPOKESFULL] Psypokes Black and White walkthrough, seasons and shaking grass, https://www.psypokes.com/bw/walkthrough.php?part=2
- [BULBAGROTTO] Bulbapedia Hidden Grotto, https://bulbapedia.bulbagarden.net/wiki/Hidden_Grotto
- [IGNBW] IGN Black and White cheats and secrets, https://www.ign.com/wikis/pokemon-black-and-white/Pokemon_Black_and_White_NDS_Cheats_and_Secrets
- [IGNBW2SHINY] IGN Black 2 and White 2 shiny Pokemon, https://www.ign.com/wikis/pokemon-black-white-version-2/Shiny_Pokemon
- [WIKIPEDIA] Wikipedia, Pokemon Black and White, https://en.wikipedia.org/wiki/Pok%C3%A9mon_Black_and_White
- [SMOGONRNG] Smogon Black and White RNG manipulation guide, https://www.smogon.com/forums/threads/the-bw-rng-manipulation-guide.3452348/
- [PIDRNG] Project Pokemon PIDRNG, https://projectpokemon.org/home/forums/topic/11889-pidrng-an-rng-abuse-tool-for-pokemon-black-and-whitegen-5
- [POKEMONRNG] Pokemon RNG, Black and White Entralink, https://www.pokemonrng.com/emulator-bw-entralink
- [EONTIMER] DasAmpharos EonTimer, https://github.com/DasAmpharos/EonTimer
- [POKELUA] Real96 PokeLua, https://github.com/Real96/PokeLua
- [MELONDSLUA] NPO-197 melonDS-lua Lua documentation, https://github.com/NPO-197/melonDS-lua/blob/master/tools/LuaScripts/Lua_Docs.md
- [SULOKU] suloku BW_tool, https://github.com/suloku/BW_tool
- [PKHEX] kwsch PKHeX, https://github.com/kwsch/PKHeX
- [POKEMONACCESS] nuive pokemon-access, https://github.com/nuive/pokemon-access
- [CRYSTALACCESS] tspivey pokecrystal-access, https://github.com/tspivey/pokecrystal-access
- [WIIMMFI] Wiimmfi, https://wiimmfi.de/
- [PKMNCLASSIC] Poke Classic Network, https://pkmnclassic.net/
- [PLATINUMMP] Project Platinum Multiplayer, https://www.reddit.com/r/PokemonROMhacks/comments/1vlgnlw/project_platinum_multiplayer_looking_for_more
- [PKGBA] PK-GBA Multiplayer, https://www.pokecommunity.com/threads/pk-gba-multiplayer.484949
- [WILDSKANTO] Wilds of Kanto overworld spawn mod, https://github.com/YoDrehDenSwagAuf/overworld-spawn-mod
- [MASTERYESE] Project Pokemon DS rom hacking tutorials, https://projectpokemon.org/home/forums/topic/41883-pokemon-ds-rom-hacking-tutorials
- [FONT] PokeCommunity how to change the font, https://www.pokecommunity.com/threads/how-to-change-the-font.266885/
- [SMASHMUSIC] Smash Custom Music, Pokemon Black and White, https://smashcustommusic.net/game/277
- [SOUNDFONT] Musical Artifacts, Pokemon Black and White soundfont, https://musical-artifacts.com/artifacts/4126
- [ARSCRIPT] Project Pokemon Black and White overworlds and scripts, https://projectpokemon.org/home/forums/topic/21641-pok%C3%A9mon-black-and-white-overworlds-and-scripts
- [PLATINUMREDUX] PokeCoders best DS rom hacks, Platinum Redux and Vintage White, https://www.pokemoncoders.com/best-pokemon-ds-rom-hacks
- [GAMERGUIDES] Gamer Guides Black and White training and EXP Share, https://www.gamerguides.com/pokemon-black-white/guide/gameplay
- [CRYSTALLEGACY] cRz-Shadows Pokemon Crystal Legacy, https://github.com/cRz-Shadows/Pokemon_Crystal_Legacy
- [SINISTER] Pokemon Sinister Crystal, https://switch-b.itch.io/pokemon-sinister-crystal
- [PURERGB] Vortyne pureRGB features, https://github.com/Vortyne/pureRGB/blob/master/FEATURES.md
- [BLUEMAZ] Blue Kaizo TAS, TASVideos, https://tasvideos.org/8765S
- [RADIO] Radio Times Pokemon Black cheats, https://www.radiotimes.com/technology/gaming/pokemon-black-cheats-full/
- [BWDELUXE] Project Pokemon Black and White Deluxe, https://projectpokemon.org/home/forums/topic/15347-pok%C3%A9mon-black-white-deluxe-v1-released
- [UNOVACHALLENGE] IGN Black 2 and White 2 Unova Challenge, https://www.ign.com/wikis/pokemon-black-white-version-2/Unova_Challenge
- [SUBWAY] PokemonLP Battle Subway, https://pokemonlp.fandom.com/wiki/Appendix:Pok%C3%A9mon_Black_and_White_Walkthrough/Battle_Subway
- [POKEBEACH] Pokebeach shiny and Masuda guide, https://www.pokebeach.com/forums/threads/my-guide-on-how-to-get-shiny-pok%C3%A9mon.90570
- [FANDOMWEATHER] Pokemon Wiki list of weather conditions, https://pokemon.fandom.com/wiki/List_of_weather_conditions
- [PROJECTPOKEMON] Project Pokemon Black and White translation files, https://github.com/projectpokemon/Pokemon-Black-White-Translation-Files
- [NINTENDOUK] Nintendo UK, Black and White seasons and WFC shutdown, https://www.nintendo.com/en-gb/Games/Nintendo-DS/Pokemon-Black-Version-272332.html

Repository and aletheia sources, by key.

- [MODEVENTS] `docs/mod-events.md`, the event catalogue and the declared-set binding.
- [MODINTERFACE] `docs/mod-interface.md`, the hookable set, the state fields and the ABI.
- [MODDING] `docs/modding.md`, what the four example mods need and what is missing.
- [MODCOOP] `docs/mod-coop.md`, the field and network enablers for a co-op mode.
- [MODS] `docs/mods.md`, the data and asset surface.
- [SETTINGS] `docs/settings-design.md`, the settings screen and the Options rows.
- [HOOKS] `docs/hooks.md`, the compiled-in hook and its limits.
- [DECLAREDSET] `mod/declared_set.txt`, the hookable set artifact.
- [README] `README.md`, the port plan, the frame skeleton, the audio, the milestones.
- [SAVEH] `sdk/save.h`, the state write discipline.
- [RTC] `sdk/arm7/rtc.c`, the ARM7 real-time clock.
- [FRAME] `engine/frame/`, the per-frame skeleton and the OS tick.
- [SCRCMD] aletheia `src/game/ov021/scrcmd_*.c` and `src/matched/ov/ov021_func_*.c`, the field script commands.
- [CALENDER] aletheia `src/game/ov010/calender.c`, weather, hidden items and the calendar.
- [DAYCARE] aletheia `src/game/arm9/battle_rec.c`, the daycare save and egg state.
- [MAPATTR] aletheia `src/game/ov021/event_fishing.c` (`MAP_ATTR_IsLongHopEdge`) and `src/game/ov021/fieldmap_ctrl_hybrid.c` (`RAIL_ATTR_IsJumpStart`), the map attributes behind a ledge or rail jump.
- [FISHING] aletheia `src/game/ov021/event_fishing.c`, the field fishing event.
- [FLASH] aletheia `src/game/ov021/field_flash.c`, the field flash and map effect.
- [RIPPLE] aletheia `src/game/ov021/fldeff_ripple.c`, the water ripple effect.
