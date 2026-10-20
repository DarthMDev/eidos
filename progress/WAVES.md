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
