# eidos waves

One row per wave, written by `python tools/progress.py --wave N --note ...`.
The figures come from the configure's own plan and the build log.

| wave | date | host surface | compiled | errors | failed targets | note |
|---|---|---|---|---|---|---|
| 0 | 2026-10-15 | 17.07% | 5726 | 16 | 16 | baseline: configure unblocked, Class A (implicit declarations) fixed |
| 1 | 2026-10-15 | 17.07% | 5726 | 0 | 30 | compile errors cleared (Class B); build now compiles everything and fails at link on 163 renamed symbols |
| 2 | 2026-10-16 | 17.07% | 5726 | 0 | 0 | build links: renamed symbols resynced and test link graphs fixed |
| 5 | 2026-10-15 | 17.07% | 5726 | 0 | 30 | doc pass: meta-commentary, filler and redundancy removed from every doc |
| 6 | 2026-10-15 | 17.07% | 5726 | 0 | 30 | figures refreshed and the two generated docs regenerated; broken references fixed |
| 7 | 2026-10-15 | 17.07% | 5726 | 0 | 30 | doc hygiene: decisions reframed as work, cross-repo references labelled, overlay_table default fixed |
| 8 | 2026-10-16 | 17.07% | 5725 | 0 | 0 | the contract gaps: the declared set published as an artifact, the rule surface, the state fields, and the packed form |
| 9 | 2026-10-16 | 17.07% | 5726 | 0 | 0 | the mod seam: slots in the generators and the registry in sdk |
| 10 | 2026-10-16 | 17.07% | 5725 | 0 | 0 | the runtime made healthy: the name-collision segfaults, the duplicate stub, the map file and the boot |

Waves 3 and 4 have no row. The log has no record for either.
| 11 | 2026-10-16 | 17.07% | 5725 | 0 | 0 | the member-address stubs: the boot runs past frame 2 |
| 12 | 2026-10-20 | 31.22% | 429 | 0 | 0 | src/game: the boot and the overlays compile aletheia's merged units, and the host surface is measured against the tree the build used |
| 13 | 2026-10-20 | 31.22% | 429 | 0 | 0 | the events and the state API in sdk/: the dispatch, the state calls, the slots that fire the events, and the session and items areas |
| 14 | 2026-10-20 | 31.22% | 429 | 0 | 0 | the loader path: the manifest, the loader, a mod's own saved data, and the declared set at run time |
| 15 | 2026-10-20 | 31.22% | 429 | 0 | 0 | the rest of the event areas (pokemon, world, battle) and the message service |
| 16 | 2026-10-20 | 31.22% | 429 | 1 | 1 | the text path, the script command hooks and the rule interpreter; one file of ov013 does not compile: the new aletheia headers give one function two spellings with different prototypes, and the port merges the spellings |
| 17 | 2026-10-20 | 31.22% | 429 | 0 | 1 | the naming fix, the script VMs' stand-in, the message window hooks, and the timer, random and export services; the link misses two functions the new aletheia headers renamed |
| 18 | 2026-10-21 | 31.22% | 429 | 0 | 0 | the port against the new tree: the spelling header follows the tree's content, the renaming survives the new header style, and the boot's closure carries the state API's accessors |
| 19 | 2026-10-21 | 31.22% | 429 | 0 | 0 | the script command's operands and the per-mod answer in the mod's surface, and the peer sessions and the external control |
| 20 | 2026-10-21 | 31.22% | 429 | 0 | 0 | the frame tick, the field avatars, the run restart and the save handoff, and the rule layer's peer send |
| 21 | 2026-10-21 | 31.22% | 429 | 0 | 0 | the boot's restart and the handoff's reload, the mod menu's model, and the mod layer's documentation |
| 22 | 2026-10-21 | 31.22% | 429 | 0 | 0 | the restart entry point, the save window from the game's own events, the wrap chain, and the last four documents |
| 23 | 2026-10-21 | 31.22% | 429 | 0 | 0 | the mod menu's screen, the player's order, the switch name and the prototype check, and the save window's last gaps |
| 24 | 2026-10-21 | 31.22% | 429 | 0 | 0 | the run-status panel, the input injection, the ARM9's code pointers reach the registry, and the game's own type names in a prototype |
