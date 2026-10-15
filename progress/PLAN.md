# eidos wave plan

The same shape as aletheia's waves, applied to a port instead of a
decompilation.  A wave has a scope, one writer per file, and a measurement
recorded with `python tools/progress.py --wave N --note "..."`.

## The rules

1. **Subagents are always background subagents.**  Never dispatch a blocking
   one.  The orchestrator stays available while a wave's workstreams run.
2. **Matched files are never edited.**  Everything under the aletheia checkout
   (`src/matched/`) is byte-verified against the ROM.  Portability lives in the
   port layer: `sdk/`, `stubs/`, `tools/`, `cmake/`, `CMakeLists.txt`.  If a
   matched file genuinely has to change, that change belongs in aletheia, in the
   same commit as a byte-identical re-verify.
3. **Workstreams run in parallel, and each holds a claim.**  `tools/claims.py`
   is the registry.  A workstream edits only paths inside its claim, and builds
   in its own `build-<id>` directory, so parallel work does not fight over one
   build tree.  Two workstreams never hold overlapping claims; `claim` refuses
   one before the work starts rather than leaving it to a merge.
4. **Every wave ends with a measurement**, even if the wave is partly done.
   `progress/WAVES.md` is the log; `progress/status.json` is the latest block.
5. **A wave that cannot finish records what blocked it** rather than leaving the
   row out.
6. **Docs are self-contained and informative.**  No meta-commentary: do not
   describe the conversation that produced the document, do not say what it is
   "answering", and do not state the obvious.  A reader who joins fresh should
   get the facts, not the process.

## What "advancement" means here

aletheia measures matched bytes.  eidos measures how much of the matched corpus
the host build actually compiles, and whether it builds:

- **host surface**: matched sources compiled for the host, of all matched
  sources.  This is the number to move.
- **configure**: pass or fail, with the boot plan's numbers.
- **build**: failed targets and errors, by class.
- **tests**: the ctest summary.

## The waves

| wave | scope | files |
|---|---|---|
| 0 | baseline.  Configure unblocked, Class A (implicit declarations) fixed | `cmake/boot.cmake`, `tools/closure.py` |
| 1 | clear the build: the 16 conflicting-type errors (libc header conflicts, unparsed declarators) | `tools/closure.py`, `tools/bootlink.py` |
| 2 | retire the four obsolete stand-ins so aletheia's matched C wins | `cmake/boot.cmake`, `CMakeLists.txt`, `sdk/asm/`, `stubs/`, `tests/test_asmsdk.c` |
| 3 | docs truth pass: `docs/status.md` is stale about aletheia's overlay coverage and points at a file that does not exist; `README.md` links a 404 | `docs/status.md`, `README.md` |
| 4 | the mod layer: document the existing data mod surface, add the address-keyed override registry, define a mod pack format | `sdk/vfs.c`, `sdk/vfs_narc.c`, `docs/`, `tests/` |
| 5 | doc pass: remove meta-commentary, filler and redundancy from every doc, so each reads for a reader who joins fresh | all of `docs/`, `progress/PLAN.md` |
| 6 | stale figures: two docs are generated and must be regenerated from their tools, and the hand-written figures were taken against an older aletheia. Also fix the dangling doc reference in `tools/overlay_table.py`, which points at a `docs/MATCHING.md` that does not exist | regenerate `docs/dsaddr-residue.md` (`tools/dsaddr.py --report`) and `docs/title-closure.md` (`tools/title_closure.py`); hand-refresh `docs/pointer-width.md`, `docs/boot.md`, `docs/overlays.md`, `docs/armdata.md`; `tools/overlay_table.py` |
| 7 | leftover doc hygiene: reframe the "Open decisions" section of `docs/mod-interface.md` as work, qualify the unlabelled cross-repo doc references (they point into the aletheia checkout, not this repository), and fix `tools/overlay_table.py`'s `--aletheia-root` default, which points at a `../aletheia` that does not exist in this layout | `docs/mod-interface.md`, `README.md`, `docs/settings-design.md`, `sdk/fs.c`, `stubs/reimpl_02045A98.c`, `tools/overlay_table.py` |
| 8 | close the four contract gaps the tooling side found: publish the declared set as a real artifact rather than a superset, add a rule surface to the manifest and the contract, name the game state fields and accessors a rule may touch (player money is the worked example and has no field or address anywhere), and say where the packed form is read | `docs/mod-interface.md`, `docs/modding.md`, `tools/` |
| 9 | the seam itself: emit a slot for each address in the declared set in `tools/bootlink.py` and `tools/ovlink.py`, and the registry that answers it in `sdk/`, so a dropped-in mod can replace or wrap a function at run time. The evidence is `progress/SEAM_COST.md`: 44 slots and about 73 redirects, against about 8,600 slots and 2 to 3 MB for a uniform scheme | `tools/bootlink.py`, `tools/ovlink.py`, `sdk/` |

Cross-cutting, no wave of its own: configure takes about 11 minutes because
`closure.py` and `bootlink.py` plan over 33,541 sources on every run.  Worth
caching the plan.
