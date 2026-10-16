# eidos waves

One row per wave, written by `python tools/progress.py --wave N --note ...`.
The figures come from the configure's own plan and the build log.

| wave | date | host surface | compiled | errors | failed targets | note |
|---|---|---|---|---|---|---|
| 0 | 2026-10-15 | 17.07% | 5726 | 16 | 16 | baseline: configure unblocked, Class A (implicit declarations) fixed |
| 1 | 2026-10-15 | 17.07% | 5726 | 0 | 30 | compile errors cleared (Class B); build now compiles everything and fails at link on 163 renamed symbols |
| 5 | 2026-10-15 | 17.07% | 5726 | 0 | 30 | doc pass: meta-commentary, filler and redundancy removed from every doc |
| 6 | 2026-10-15 | 17.07% | 5726 | 0 | 30 | figures refreshed and the two generated docs regenerated; broken references fixed |
| 7 | 2026-10-15 | 17.07% | 5726 | 0 | 30 | doc hygiene: decisions reframed as work, cross-repo references labelled, overlay_table default fixed |
| 2 | 2026-10-16 | 17.07% | 5726 | 0 | 0 | build links: renamed symbols resynced and test link graphs fixed |
