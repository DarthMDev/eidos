# SM64 port family & Ship of Harkinian: repo architecture and the rule governing edits to decompiled code

Scope note for the report-writer: this is a technical-architecture study of two mature decompilation-derived ports. All dates are as stated by the cited source. Where I could not verify something I say so explicitly in the Gaps section of the relevant question. The eidos project's rule (compile matched C unchanged; edit only in the decomp repo with a byte-identical re-verify) is treated as the baseline this research compares against.

---

## Q1. sm64: which repo is the port, and how does it relate to the decomp repo (fork / submodule / vendored)? Is the decompiled C edited, and how?

### Takeaway
There are three distinct artifacts: **n64decomp/sm64** is the decompilation (the source of truth, no port code); **sm64-port/sm64-port** is *the* PC port (it is a GitHub **fork** of n64decomp/sm64 that keeps the decomp tree and adds a `src/pc` port layer, while still being able to build the original N64 ROM); **sm64pc/sm64ex** is a **fork of the port**, not of the decomp. The port **does edit decompiled C**, but sparsely and in a guardable way: PC-only code lives in `src/pc/`, divergent behavior is selected by the `TARGET_N64`/`NON_MATCHING`/`AVOID_UB` preprocessor defines, and the N64 matching build path plus SHA-1 ROM verification are retained.

### Cited Findings
- **n64decomp/sm64** is the decompilation only: "This repo contains a full decompilation of Super Mario 64 of the following releases: Japan (jp), North America (us), Europe (eu), Shindou (sh) and iQue Player (cn)"; it builds `sm64.jp.z64`, `sm64.us.z64`, etc. with listed SHA-1 hashes and a `COMPARE` option — [n64decomp/sm64 README](https://github.com/n64decomp/sm64). It contains no `src/pc`; its project structure is `src`, `actors`, `levels`, `lib`, `rsp`, `sound`, `tools`, etc. — [n64decomp/sm64 README, Project Structure](https://github.com/n64decomp/sm64).
- **sm64-port** declares itself "A port of https://www.github.com/n64decomp/sm64 for modern devices" — [sm64-port README](https://github.com/sm64-port/sm64-port/blob/master/README.md). The GitHub repo page displays "forked from [n64decomp/sm64]" — [github.com/sm64-port/sm64-port](https://github.com/sm64-port/sm64-port).
- **sm64-port explicitly retains dual targets**: "Beyond Nintendo 64, it can also target Linux and Windows natively," and "It is possible to build N64 ROMs as well with this repository. See https://github.com/n64decomp/sm64 for instructions." — [sm64-port README](https://github.com/sm64-port/sm64-port/blob/master/README.md).
- **The port layer is a separate source directory**: sm64-port's project structure adds `src/pc: port code, audio and video renderer` on top of the decomp's layout — [sm64-port README, Project Structure](https://github.com/sm64-port/sm64-port/blob/master/README.md). The actual directory contains `pc_main.c`, `ultra_reimplementation.c`, `mixer.c`, `configfile.c`, `dlmalloc.c`, `compat.h`, and `gfx/`, `audio/`, `controller/` subdirs — [sm64-port src/pc](https://github.com/sm64-port/sm64-port/tree/master/src/pc).
- **Selection between decomp and port targets is a Makefile switch**: sm64-port's `Makefile` has `# Build for the N64 (turn this off for ports)` / `TARGET_N64 ?= 0`. When `TARGET_N64=0` it force-sets `COMPILER := gcc`, `NON_MATCHING := 1`, `GRUCODE := f3dex2e`, and appends `SRC_DIRS += src/pc src/pc/gfx src/pc/audio src/pc/controller`; when `TARGET_N64=1` it instead appends `asm lib` and defines `-DTARGET_N64` — [sm64-port Makefile](https://github.com/sm64-port/sm64-port/blob/master/Makefile).
- **The port swaps out the game entry point rather than patching it**: the Makefile does `C_FILES := $(filter-out src/game/main.c,$(C_FILES))` for non-N64 builds, providing the port's own `src/pc/pc_main.c` instead — [sm64-port Makefile](https://github.com/sm64-port/sm64-port/blob/master/Makefile).
- **Divergence from the matching build is expressed through pre-existing decomp defines**, not wholesale rewrites: for PC builds `NON_MATCHING := 1` cascades to `DEFINES += NON_MATCHING=1 AVOID_UB=1` and forces `COMPARE := 0` — [sm64-port Makefile](https://github.com/sm64-port/sm64-port/blob/master/Makefile). A secondary source explains: "`NON_MATCHING`: When set to 1, the preprocessor enables code paths that are functionally equivalent to the original but may not compile to the same assembly. This is mandatory for all non-N64 targets," and "`AVOID_UB` … replaces instances of Undefined Behavior (UB) found in the original source … with safe, portable C code" — [DeepWiki, sm64-port "Regional Versions and Matching Builds"](https://deepwiki.com/sm64-port/sm64-port/1.2-regional-versions-and-matching-builds) (secondary source, useful but not primary).
- **Some decompiled files DO carry port guards.** A direct scan of sm64-port's decompiled sources shows the core game files are mostly untouched by `TARGET_N64` guards (e.g., `src/game/main.c`, `src/engine/graph_node.c`, `src/game/camera.c`, `src/game/level_update.c` contain **zero** `TARGET_N64` occurrences), but at least one game source file does: `src/audio/external.c` contains an `#ifdef TARGET_N64` block — [sm64-port src/audio/external.c](https://github.com/sm64-port/sm64-port/blob/master/src/audio/external.c). The `AVOID_UB` occurrences seen in `main.c` and `camera.c` are decomp-provided non-matching constructs, not port-added code.
- **sm64ex is a fork of the port**: "Fork of [sm64-port/sm64-port] with additional features" — [sm64ex README](https://github.com/sm64pc/sm64ex). A contemporaneous community breakdown states: "SM64 Port is the official project created originally by the developers … The sm64ex fork, on the other hand, was based on the leaked version of the SM64 Port code initially. Since it hasn't been totally in sync with the official project it's not quite as refined" — [r/SM64PC "Super Mario 64 Port and the sm64ex fork [breakdown]"](https://www.reddit.com/r/SM64PC/comments/hhv0h1/super_mario_64_port_and_the_sm64ex_fork_breakdown) (secondary).
- **sm64ex's own contribution rule is branch-based, not fidelity-based**: "Please contribute first to the nightly branch. New functionality will be merged to master once they're considered to be well-tested." — [sm64ex README](https://github.com/sm64pc/sm64ex).

### Inferences
- The sm64 approach is a **fork-with-additive-port-layer**: the decompiled tree is preserved and remains compilable as the matching ROM; PC behavior is concentrated in `src/pc`, and the handful of edits inside decomp files are wrapped so the N64 build does not change. This is architecturally close to eidos's discipline (matched files stay valid) but *not identical*: the port does permit guarded edits inside decompiled files and does not re-verify byte-matching after them because those edits are compiled out on the N64 target.
- sm64ex adds a second fork layer, so upstream decomp fixes must transit decomp → sm64-port → sm64ex, which is exactly the divergence cost the port family accepts to get features.

### Gaps
- I could not obtain the exact GitHub creation timestamp of `sm64-port/sm64-port` (GitHub API rate-limited during this study); one search surfaced `Published: 2020-06-16` for the repo page, but I could not independently confirm it, so treat the precise repo-creation date as unverified.

---

## Q2. Timeline: sm64 decomp completion and each port; OoT decomp completion and Ship of Harkinian

### Takeaway
The sm64 decompilation was declared complete in **mid-July 2019**; the sm64 PC port surfaced in **early May 2020** (~10 months later) and sm64ex followed within days. The OoT decompilation was declared complete for its debug ROM in **late November / early December 2021**; Ship of Harkinian development "began in November 2021, coinciding with the decompilation project reaching completion," and it first released in **March 2022**.

### Cited Findings
- **sm64 decomp complete July 2019**: "Super Mario 64 Has Been Completely Decompiled, Source Code Available," dated **15 July 2019** — [Gaming Reinvented, 2019-07-15](https://gamingreinvented.com/news/super-mario-64-has-been-completely-decompiled-source-code-available/). A Hacker News thread titled "Super Mario 64 has been decompiled" is dated **2019-07-12** — [Hacker News](https://news.ycombinator.com/item?id=20418577). The same coverage notes the code was 100% matched but "only about 65% complete" on *documentation/naming*, i.e. code complete ≠ fully annotated — [Gaming Reinvented](https://gamingreinvented.com/news/super-mario-64-has-been-completely-decompiled-source-code-available/).
- **sm64 PC port release May 2020**: "A fully functioning Mario 64 PC port has been released" — [Video Games Chronicle](https://www.videogameschronicle.com/news/a-full-mario-64-pc-port-has-been-released/); "A fan-made PC port of Super Mario 64 quietly surfaced over the weekend" — [TechSpot](https://www.techspot.com/news/85093-fan-quietly-releases-native-pc-port-super-mario.html). PCGamingWiki lists sm64ex Windows release **May 2, 2020**, Linux **May 5, 2020**, macOS **June 1, 2020** — [PCGamingWiki, Super Mario 64 (sm64ex)](https://www.pcgamingwiki.com/wiki/Super_Mario_64_(sm64ex)).
- **sm64ex repo created 2020-05-07** (default branch `nightly`) — [github.com/sm64pc/sm64ex](https://github.com/sm64pc/sm64ex). A community breakdown dated **2020-06-29** already distinguishes the "official" sm64-port from the sm64ex fork — [r/SM64PC](https://www.reddit.com/r/SM64PC/comments/hhv0h1/super_mario_64_port_and_the_sm64ex_fork_breakdown).
- **OoT decomp complete late Nov / early Dec 2021**: "The final decompiled functions still need to be merged with the ZRET Github repository before the open source project is officially considered 100 percent complete" — [Ars Technica, Nov 2021](https://arstechnica.com/gaming/2021/11/reverse-engineering-team-completes-its-ocarina-of-time-decompilation). Modern Vintage Gamer dates its "fully decompiled" video **2021-12-06** — [MVG, YouTube](https://www.youtube.com/watch?v=kbc7IzgTvuU). OC3D (2021-12-06) qualifies that "Ocarina of Time's decompilation project has only hit 100% with a Master Quest Debug version of the game" — [OC3D, 2021-12-06](https://overclock3d.net/news/software/coders-have-successfully-decompiled-the-legend-of-zelda-orcarina-of-time). The current ZeldaRET progress site lists "Ocarina of Time Code decompiled: 100%" — [zelda.deco.mp](https://zelda.deco.mp).
- **Ship of Harkinian began Nov 2021, released March 2022**: "Development on what would later become Ship of Harkinian began in November 2021, coinciding with the decompilation project reaching completion." … "It was first released in March 2022 for Windows, four months after Ocarina of Time's source code was decompiled and released." — [Wikipedia, Ship of Harkinian](https://en.wikipedia.org/wiki/Ship_of_Harkinian). VGC reports the release on **2022-03-22** — [VGC, 2022-03-22](https://www.videogameschronicle.com/news/zelda-ocarina-of-time-pc-port/).
- **2Ship2Harkinian (Majora's Mask) as a later analogue**: MM fully decompiled Nov 2023, first port release 26 May 2024 — [Wikipedia, Ship of Harkinian](https://en.wikipedia.org/wiki/Ship_of_Harkinian).

### Inferences
- Both ports post-date their decompilation's *code*-completion milestone; neither was a "port the unfinished decomp" effort in the sense of writing missing functions to make a playable port. The decomp work was a prerequisite, not a parallel race.
- The documentation/naming tail matters: sm64 naming "is in progress" long after completion ([n64decomp/sm64](https://github.com/n64decomp/sm64)), so the port was built against a code-complete but still-being-renamed tree — which is itself a source of ongoing merge churn (see Q7).

### Gaps
- For OoT I could not find a single canonical primary date for the exact commit that reached 100% matching; the strongest phrasing is Ars Technica's "still need to be merged … officially considered 100 percent complete" and OC3D's "only hit 100% with a Master Quest Debug version." Treat "late Nov/early Dec 2021" as the accurate range.

---

## Q3. Did the ports wait for a complete decompilation, or start while it was incomplete?

### Takeaway
The mature ports **started at or after completion**, not during it. sm64's port appeared ~10 months after the decomp was declared complete. Ship of Harkinian's development explicitly "coinсid[ed] with the decompilation project reaching completion." I found **no** primary statement of a port beginning on a knowingly incomplete decompilation.

### Cited Findings
- Ship of Harkinian: "Development on what would later become Ship of Harkinian began in November 2021, coinciding with the decompilation project reaching completion." — [Wikipedia, Ship of Harkinian](https://en.wikipedia.org/wiki/Ship_of_Harkinian).
- OoT decomp was announced complete before/during the port's start; the final functions were only "officially considered 100 percent complete" after merge review — [Ars Technica, Nov 2021](https://arstechnica.com/gaming/2021/11/reverse-engineering-team-completes-its-ocarina-of-time-decompilation).
- sm64: decomp complete July 2019 ([Gaming Reinvented](https://gamingreinvented.com/news/super-mario-64-has-been-completely-decompiled-source-code-available/)); port surfaced May 2020 ([VGC](https://www.videogameschronicle.com/news/a-full-mario-64-pc-port-has-been-released/)).
- A ZRET member framed the relationship as decomp-first: "consider that outside … We just decompile the … else will inevitably pick it up and write the PC port," and "We also hope that any future potential PC port author doesn't bundle the game's assets…" — [Ars Technica, May 2020](https://arstechnica.com/gaming/2020/05/beyond-emulation-the-massive-effort-to-reverse-engineer-n64-source-code).
- **Caveat — not the whole story for sm64 feature work:** sm64-port's own `CHANGES` continues to integrate upstream decomp work after the port exists (see Q7), so while the port *began* post-completion, it tracked a decomp that was still being renamed/relabeled.

### Inferences
- The "wait for completion" pattern is the norm for these flagship ports because a port that edits incomplete decomp code would churn constantly. This is directly relevant to eidos: the ports chose to build on a stable, matched base.

### Gaps
- I could not find an explicit statement from either port team saying "we deliberately waited." The conclusion rests on the date ordering plus the SoH Wikipedia wording "coinciding with … completion."

---

## Q4. Ship of Harkinian: relationship to zeldaret/oot and libultraship; does it vendor / edit / keep pristine; where is the boundary?

### Takeaway
SoH is **not** a GitHub fork of zeldaret/oot at the top level. Instead it **vendors a modified copy of the OoT decomp source inside its own tree**, at `soh/` (subdirs `soh/src`, `soh/include`, `soh/assets`, …), committed directly into the Shipwright repo and historically synced from a HarbourMasters-owned fork of the decomp via the `git-subrepo` tool. libultraship is a **separate submodule** that reimplements the N64 SDK. Boundary: `soh/src`+`soh/include` = the decompiled game (edited); `soh/soh` = SoH's own port/enhancement code; `libultraship` = SDK reimplementation; `torch` = asset tooling.

### Cited Findings
- **README**: "Ship of Harkinian (SOH) is built atop a custom library dubbed libultraship (LUS). … In addition, we are dependent on the source code provided by the OOT decompilation project." — [Shipwright README](https://github.com/HarbourMasters/Shipwright/blob/develop/README.md).
- **The decomp source is vendored in-tree**: Shipwright's `soh/` folder contains `src/`, `include/`, `assets/`, `soh/`, `platform/`, `CMake/`, and a `.gitrepo` file — [Shipwright soh/ tree](https://github.com/HarbourMasters/Shipwright/tree/develop/soh). `soh/src` contains `boot`, `buffers`, `code`, `dmadata`, `elf_message`, `libultra`, `overlays` — the exact directory layout of the OoT decompilation — [Shipwright soh/src tree](https://github.com/HarbourMasters/Shipwright/tree/develop/soh/src). Compare the same layout in [zeldaret/oot](https://github.com/zeldaret/oot).
- **The vendored copy is a subrepo from a HarbourMasters fork of the decomp**, not a submodule of zeldaret/oot. `soh/.gitrepo`: `[subrepo] remote = https://github.com/HarbourMasters/soh.git`, `branch = master`, `method = rebase`, `commit = 75ccbade8ba26485266a8c8aa7d81e495d0ca4dd` — [soh/.gitrepo](https://github.com/HarbourMasters/Shipwright/blob/develop/soh/.gitrepo). (The file header reads "This subdirectory is a git 'subrepo', and this file is maintained by the git-subrepo command.")
- **libultraship and torch are declared submodules**: `.gitmodules` lists `libultraship → https://github.com/kenix3/libultraship.git` (branch `port-maintenance`) and `torch → https://github.com/HarbourMasters/Torch.git` (branch `main`) — [Shipwright .gitmodules](https://github.com/HarbourMasters/Shipwright/blob/develop/.gitmodules).
- **libultraship's role**: "libultraship (LUS) is a library meant to provide reimplementations of libultra (n64 sdk) functions that run on modern hardware." — [libultraship README](https://github.com/HarbourMasters/libultraship/blob/main/README.md).
- **The decomp files are compiled from the vendored tree**: the build doc says to pass `-DSUPPRESS_WARNINGS=0` "to prevent suppression of warnings from LUS and decomp (src) files," confirming that `soh/src` ("decomp") is compiled as part of Shipwright — [Shipwright docs/BUILDING.md](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/BUILDING.md). The same doc notes the asset headers are "checked-in headers under `soh/assets`" — [Shipwright docs/BUILDING.md](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/BUILDING.md).
- **The decomp is edited, not pristine**: the vendored source is Where decomp code lives and is compiled with port changes; SoH adds its own layer at `soh/soh` (e.g. `soh/soh/Enhancements/...`) — [Shipwright soh/ tree](https://github.com/HarbourMasters/Shipwright/tree/develop/soh) and [Shipwright soh/soh tree](https://github.com/HarbourMasters/Shipwright/tree/develop/soh/soh).
- **Fidelity discipline in SoH is a *runtime* rule, not a source rule**: "We are careful to put any and all changes to the authentic game behind toggles, so that if desired you can play it in all of its 1998 glory." — [shipofharkinian.com](https://www.shipofharkinian.com).
- **The decomp's own repo explicitly disclaims being a port**: zeldaret/oot's README says "It is not producing a PC port." — [zeldaret/oot README](https://github.com/zeldaret/oot). SoH is a separate downstream consumer.

### Inferences
- SoH's architectural boundary is: decompiled game code (vendored, edited) ↔ SoH port/enhancement code (own tree) ↔ libultraship SDK shim (submodule) ↔ torch asset pipeline (submodule). There is **no enforced pristine decomp layer**; unlike sm64-port, SoH does not keep the decomp as a buildable matching N64 target inside its own repo.
- Because the decomp is a subrepo of a HarbourMasters fork (`HarbourMasters/soh`), SoH can and does diverge from zeldaret/oot and reconcile on its own schedule.

### Gaps
- I could not open `https://github.com/HarbourMasters/soh` (returned 404 via fetch); only the recorded subrepo remote proves the vendored tree descended from a HarbourMasters fork. I could not verify whether `HarbourMasters/soh` is itself a GitHub fork of `zeldaret/oot` or a from-scratch re-host. Treat the precise lineage (fork-of-oot vs re-host) as unverified.
- The `.gitrepo` `commit`/`parent` fields are a historical snapshot; I did not confirm whether current `develop` still syncs via git-subrepo or has switched to plain vendoring.

---

## Q5. Is there a written rule about editing decompiled files ("portability belongs in the port layer" / "no edits to matched files")? Quote verbatim.

### Takeaway
**I found no such rule in either project family.** Neither n64decomp/sm64, sm64-port, sm64ex, zeldaret/oot, nor Shipwright states "do not edit matched files" or "portability belongs in the port layer." The only *written* rules that exist point the opposite way: libultraship's official porting guide **instructs** porters to edit the decompilation (replace includes, stub functions, disable systems). The de facto discipline is structural (put PC code in `src/pc`; guard divergence with `TARGET_N64`/`NON_MATCHING`), not a stated prohibition.

### Cited Findings
- **No editing prohibition found** in n64decomp/sm64's README (Contributing section is generic: "Pull requests are welcome. For major changes, please open an issue first") — [n64decomp/sm64 README](https://github.com/n64decomp/sm64). Same generic text in [sm64-port README](https://github.com/sm64-port/sm64-port/blob/master/README.md) and [zeldaret/oot README](https://github.com/zeldaret/oot).
- **The one written "rule" is permissive and edit-heavy.** libultraship's Porting Guide says LUS "allows developers to build a game's original source code (derived from decompilation) on modern hardware by providing a header-compatible wrapper around modern reimplementations of N64 SDK functions," and "In essence, LUS acts as 'compile-time WINE' for the N64." Its Phase 1 steps are explicitly editing instructions: "Copy the CMake files from the Ghostship template into your decompilation directory"; "Replace N64 SDK includes (e.g., `#include <ultra64.h>`) in the source code with LUS includes: `#include <libultraship/libultraship.h>`"; "**Stubbing:** Comment out or stub functions that LUS handles automatically (e.g., `osCreateThread` …)"; "**Disable Systems:** Temporarily disable complex systems like Audio"; "**64-bit Fixes:** Address pointer size differences" — [libultraship Porting Guide](https://kenix3.github.io/libultraship/PORTING.html).
- **The nearest project-level fidelity rule in SoH is about gameplay, not sources**: "We are careful to put any and all changes to the authentic game behind toggles, so that if desired you can play it in all of its 1998 glory." — [shipofharkinian.com](https://www.shipofharkinian.com).
- **sm64-port's discipline is encoded in the build, not prose**: `TARGET_N64 ?= 0` with PC-only sources appended and decomp sources filtered is the mechanism that keeps divergence separable — [sm64-port Makefile](https://github.com/sm64-port/sm64-port/blob/master/Makefile). The decomp's own `NON_MATCHING`/`AVOID_UB` defines are the sanctioned place for non-matching portability code — [sm64-port Makefile](https://github.com/sm64-port/sm64-port/blob/master/Makefile).

### Inferences
- For eidos, the honest finding is: **the mature ports did not formalize your rule, and the flagship SoH effectively abandoned it** (it vendors and edits the decomp). sm64-port kept a weaker version of it structurally (decomp tree stays N64-buildable) without writing it down. Any claim that these projects have a stated "no edits to matched files" policy would be unsupported by the docs I could find.

### Gaps
- I searched the repos' READMEs/docs and the web for the exact phrases ("portability belongs in the port layer", "no edits to matched files", "do not modify matched") and found no primary statement. There may be Discord-only conventions; Discord content is not indexed/citable here, so a written rule could exist in Discord and simply be uncrawlable. Note as a real gap.

---

## Q6. Does anyone re-verify byte-matching to the original ROM after a port edit? Is the N64 ROM still buildable from the port's tree?

### Takeaway
Split answer. **sm64-port keeps the N64 ROM buildable and SHA-1-verified** (`make TARGET_N64=1` → `COMPARE=1` checks against `sm64.<ver>.sha1`), and it does **not** re-verify after port edits because PC edits are compiled out of the N64 target. **Ship of Harkinian does not build an N64 ROM at all** from its tree and therefore performs no byte-match re-verification; its only hash checking is against the *user's dumped ROM* (`docs/supportedHashes.json`), which validates assets, not source fidelity.

### Cited Findings
- **sm64-port keeps the matching target and the hash check.** `TARGET_N64 ?= 0` is documented as "Build for the N64 (turn this off for ports)"; the N64 branch defines `-DTARGET_N64`, uses IDO, and the `all` target runs `$(SHA1SUM) --quiet -c $(TARGET).sha1` when `COMPARE=1` — [sm64-port Makefile](https://github.com/sm64-port/sm64-port/blob/master/Makefile). The repo ships `sm64.us.sha1`, `sm64.jp.sha1`, `sm64.eu.sha1`, `sm64.sh.sha1` — [sm64-port repo root](https://github.com/sm64-port/sm64-port). README: "It is possible to build N64 ROMs as well with this repository." — [sm64-port README](https://github.com/sm64-port/sm64-port/blob/master/README.md).
- **Port builds force `COMPARE := 0`**: because `NON_MATCHING := 1` on PC, the Makefile sets `COMPARE := 0`, i.e. no ROM-hash check is attempted for port builds — [sm64-port Makefile](https://github.com/sm64-port/sm64-port/blob/master/Makefile).
- **The decomp (n64decomp/sm64) is the byte-match gatekeeper**: its README lists the exact shipped ROM SHA-1s and exposes `COMPARE` (compare ROM hash) and `NON_MATCHING` options — [n64decomp/sm64 README](https://github.com/n64decomp/sm64).
- **SoH ships no N64 build.** Shipwright's build system is CMake; `soh/` contains no `Makefile`, `baseroms/`, `sm64.ld`, or sha1 files, and the decomp source under `soh/src` is compiled via CMake into the PC binary — [Shipwright docs/BUILDING.md](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/BUILDING.md), [Shipwright soh/ tree](https://github.com/HarbourMasters/Shipwright/tree/develop/soh).
- **SoH's hash checks are about the user's ROM dump, not the port's source**: "cross-reference its `sha1` hash with the hashes here (`docs/supportedHashes.json`)" — [Shipwright README](https://github.com/HarbourMasters/Shipwright/blob/develop/README.md); and "Any copyrighted assets are extracted from the ROM and reformatted as a .o2r archive file" — same README.

### Inferences
- The reason sm64-port can keep the matching build is that its divergent code is *separable* (src/pc + defines); eidos's stricter rule (edit only in the decomp repo with a re-verify) would additionally require that any in-tree decomp edit be pushed upstream and re-proven, which sm64-port does not do — it just guards the edit out of the N64 path.
- SoH's edit-the-vendored-decomp model structurally **precludes** byte-match re-verification: once `soh/src` is modified CMake-side with no matching N64 target, there is no artifact to hash against the original ROM.

### Gaps
- I did not verify whether SoH's `soh/src` still compiles under a hypothetical IDO/N64 build; the absence of build files strongly implies it does not, but I did not attempt a build. Flag as inference, not fact.

---

## Q7. Cost of divergence: how hard is it to merge upstream decomp fixes into the port? Any evidence of pain?

### Takeaway
The sm64 port family manages divergence with a disciplined, named **"Refresh"** merge cadence: each port release folds in upstream decomp refactors/relabels, tracked in a `CHANGES` file. The cost is real and visible: sm64ex drifted out of sync with sm64-port (community statement), and adjacent N64 ports have migrated vendored decomp trees into git submodules with dedicated `port-patches` branches precisely to make upstream reconciliation tractable and auditable.

### Cited Findings
- **sm64-port maintains a numbered "Refresh" merge log tying port to decomp:** `CHANGES` runs from "Refresh #1" (initial release) up to "Refresh 14," e.g. "Refresh 11 … Make geo_process_level_of_detail endian-independent," "Refresh #10 … Introduce the NO_SEGMENTED_MEMORY define … Introduce f3dex2e," "Refresh #13 … No more nonmatchings remain for US, JP, or EU builds. Three(!!!) remain for Shindou (SH)," and "Refresh 13 2 - No more nonmatchings remain for all builds including Shindou." — [sm64-port CHANGES](https://github.com/sm64-port/sm64-port/blob/master/CHANGES).
- **Upstream decomp work is explicitly pulled into the port**: e.g. "Refresh #10 … 23.) Bring in some enhancements such as widescreen enhancements (#981)" and many upstream relabel/decompile PRs appear in the same log — [sm64-port CHANGES](https://github.com/sm64-port/sm64-port/blob/master/CHANGES).
- **Port-vs-port drift is documented:** "The sm64ex fork … hasn't been totally in sync with the official project" — [r/SM64PC breakdown, 2020-06-29](https://www.reddit.com/r/SM64PC/comments/hhv0h1/super_mario_64_port_and_the_sm64ex_fork_breakdown).
- **Adjacent, directly relevant evidence of the pain (different project, same problem):** a Super Smash Bros. 64 port PR "#128 Migrate decomp source to a submodule (Milestone 1)" moves the vendored decomp out of the in-tree `src/`+`include/` into a submodule `decomp/` on a `port-patches` branch, deletes 1031 vendored files, adds `tools/upstream_reconcile`, `tools/compare_obj_text.sh`, and `tools/audit_text_diffs.sh`, and reports a semantic-equivalence audit ("446 / 475 TUs byte-identical … 0 structural divergence"). The motivation stated is enabling "future upstream sync + semantic-equivalence auditing." — [JRickey/BattleShip PR #129](https://github.com/JRickey/BattleShip/pull/129).
- **Refresh-cadence semantics (secondary but useful):** "The project maintains a history of 'Refreshes' that track upstream changes from the main decompilation and PC port repositories." — [DeepWiki, mkst/sm64-port "Project History"](https://deepwiki.com/mkst/sm64-port/1.1-project-history-and-features).
- **SoH's own fork strategy is itself a divergence-cost response**: the decomp is a subrepo of a HarbourMasters fork (`HarbourMasters/soh`) with `method = rebase`, i.e. SoH's changes are rebased on top of an upstream decomp lineage rather than merged — [soh/.gitrepo](https://github.com/HarbourMasters/Shipwright/blob/develop/soh/.gitrepo). Modders are likewise told to "rebas[e] your branch on top of the upstream develop branch" — [Shipwright docs/MODDING.md](https://github.com/HarbourMasters/Shipwright/blob/52a30589/docs/MODDING.md).

### Inferences
- The cost of divergence is not zero even in the disciplined sm64 model: it manifests as a recurring manual merge ("Refresh") that grows with each decomp refactor/relabel. SoH's vendored-fork-subrepo model trades that recurring merge for a one-time massive fork and permanent upstream drift.
- For eidos, this is the key trade-off quantified: "compile matched files unchanged + edit upstream" has a merge cost too (you must port upstream churn into your edit), but it avoids the *fork* cost (no vendored divergent tree to reconcile). The sm64/SoH data shows both approaches incur real merge work; the ports' choice was driven by feature velocity, not by proof that the strict rule was unworkable.

### Gaps
- I could not find a first-person "this merge was painful" quote from sm64-port/SoH maintainers (no issue/PR text located in this study); the pain evidence is structural (Refresh logs, "not totally in sync," the BattleShip migration) rather than an explicit maintainer complaint.

---

## Q8. Do the ports carry hand-written stand-ins (stubs) for functions the decomp had not matched? Did they shrink to zero?

### Takeaway
Yes, both carry stand-ins, but of two different kinds. In the decomp/sm64-port family, unmatched functions are represented by `GLOBAL_ASM` blocks and `NON_MATCHING` C alternatives — these **did shrink to zero on the decomp side** over the Refresh series (the port's `CHANGES` records "No more nonmatchings remain"). In SoH/libultraship, stubbing is a **permanent architectural technique** for N64 SDK functions that the port reimplements (e.g. stubbing `osCreateThread`), so those stand-ins do not shrink to zero.

### Cited Findings
- **sm64's non-matching mechanism is a documented part of the tree**: "asm: handwritten assembly code, rom header / └── non_matchings: asm for non-matching sections," and "NON_MATCHING: Use functionally equivalent C implementations for non-matchings." — [n64decomp/sm64 README](https://github.com/n64decomp/sm64). sm64-port adds "enhancements: example source modifications" — [sm64-port README](https://github.com/sm64-port/sm64-port/blob/master/README.md).
- **Non-matchings shrank to zero**: "Refresh 13 — No more nonmatchings remain for US, JP, or EU builds. Three(!!!) remain for Shindou (SH)"; "Refresh 13 2 — No more nonmatchings remain for all builds including Shindou." — [sm64-port CHANGES](https://github.com/sm64-port/sm64-port/blob/master/CHANGES). (The Makefile still supports `GLOBAL_ASM`-based builds via `NON_MATCHING` toggling, but the upstream count reached zero — [sm64-port Makefile](https://github.com/sm64-port/sm64-port/blob/master/Makefile).)
- **SoH/libultraship stubs are permanent, by design**: Porting Guide Phase 1 "Stub Build": "**Stubbing:** Comment out or stub functions that LUS handles automatically (e.g., `osCreateThread` for rumble, as LUS manages threads differently than the N64 CPU)." — [libultraship Porting Guide](https://kenix3.github.io/libultraship/PORTING.html). LUS itself is the stand-in for libultra: "reimplementations of libultra (n64 sdk) functions that run on modern hardware" — [libultraship README](https://github.com/HarbourMasters/libultraship/blob/main/README.md).
- **The port layer also supplies replacements for decomp functions**: sm64-port filters out `src/game/main.c` and links its own `src/pc/pc_main.c` — [sm64-port Makefile](https://github.com/sm64-port/sm64-port/blob/master/Makefile), [src/pc](https://github.com/sm64-port/sm64-port/tree/master/src/pc).

### Inferences
- "Stubs shrank to zero" is true for *decomp matching status* (the non-matchings were eliminated upstream), but false for *port plumbing*: the port layer's SDK reimplementations and entry-point replacements are permanent and are exactly what makes the port a port.
- eidos's analogue: the "console-specific artifacts" are the port layer + SDK shim, which persist; the only thing that shrinks is the count of unmatched/fake-matched decomp functions.

### Gaps
- I did not enumerate the current residual non-matching count in sm64-port/sm64ex's own trees (the "zero" statement is from the decomp's Refresh log as carried in the port CHANGES); a live count could differ. Note as unverified.

---

## Cross-cutting summary for eidos
- **Repo pattern A (sm64-port):** GitHub fork of the decomp; decomp tree preserved; PC code in `src/pc`; divergence guarded by `TARGET_N64`/`NON_MATCHING`/`AVOID_UB`; N64 matching build + SHA-1 check **retained**; recurring "Refresh" merges from upstream. Closest to eidos's discipline, but it does permit guarded edits inside decompiled files and does not re-verify byte-matching after them (because they are compiled out of the N64 target).
- **Repo pattern B (Ship of Harkinian):** vendored, **edited** copy of the decomp (`soh/`, subrepo of a HarbourMasters fork) + own port layer (`soh/soh`) + SDK shim submodule (`libultraship`) + asset-tooling submodule (`torch`); **no N64 matching build**, hence no byte-match re-verification; "authentic game" fidelity enforced by runtime toggles, not source discipline. This is a straight **abandonment** of eidos's rule.
- **No written "no edits" rule exists** in either family's docs; the only written porting rules (libultraship) mandate editing the decomp.
- **Both ports started after (or exactly at) decomp code-completion**, not during it.
