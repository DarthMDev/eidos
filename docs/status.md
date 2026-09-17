# Port status: done, open, and blocked on the decompilation

README.md's "Planned Milestones" is the detailed checklist, grouped by
milestone. This file is the same work grouped by **dependency**, which is
what tells a reader what can be worked on while aletheia's decompilation is
still in progress and what has to wait for it.

Maintained by hand: move an item when its status changes, in the same commit
that changes it. A "blocked" item names the aletheia function or subsystem
it needs, so the day that lands, the block is easy to revisit. Where an item
is only partly done, it says which part.

## Done, no decompilation involved

- **engine/g2d**: OBJ and BG compositors, affine OBJ and BG, windows and the
  colour special effects. Pixel-identical to aletheia's Python renderers on
  the oracle sets (`test_g2d`, `test_bg`, `test_bg_affine`, `test_window`).
- **engine/g3d**: fx32 geometry pipeline, matrix stack, display-list
  interpreter, all seven texture formats, NSBMD/NSBTX containers, the MDL0
  shape dictionary and node dictionary (record layout, translation, full
  rotation, scale, parent/child tree, shape binding; the **packed rotation
  completion is provisional**, see `docs/g3d-nodes.md`), and a software
  rasteriser with synthetic tests. Geometry oracle-exact; the rasteriser is
  not oracle-checked (no DS frame exists in the tree).
- **engine/audio**: SDAT, SBNK, SWAR/SWAV, SSEQ interpreter, 16-channel
  mixer. Bit-identical to aletheia's `sseqsynth.py` on three sequences
  (`test_audio`).
- **engine/frame and engine/platform**: the vblank frame loop with the DS OS
  tick, and the SDL2 window, input, audio device and pacer.
- **sdk/**: the SDK-name shims (`fs`, `mi`, `os_alloc`, `os_irq`, `gx`,
  `os_tick`) plus the port's own `narc`, `vfs`, `game_globals`, `aliases`
  and `panic`.
- **Milestone 6, file layer (file granular)**: `sdk/vfs.c` owns the data
  root and an optional mod overlay root and resolves a NitroFS path through
  them (`test_vfs`).
- **Milestone 4, 2D/3D integration**: `G2dLayer3d` puts the 3D output inside
  `g2d_frame_stack` as an ordinary layer, so a 2D BG can sit in front of or
  behind it, a window can hide it, and the colour effects blend with a 3D
  pixel. `g3d`-independent (the layer is a raw RGBA buffer); `test_compose3d`
  is the synthetic check. `g3d_compose_over` remains for the backdrop case.

## Open, no decompilation dependency

- **Milestone 4, 3D gaps**: the texture matrix (`TEXIMAGE_PARAM` bits 30-31)
  and the toon/highlight polygon modes are unimplemented; lighting registers
  are parsed but not interpreted. None of these has shown a map model that
  needs it, so implementing them is speculative until a model is found.
- **Milestone 4, per-shape display-list preamble**: each shape's declared
  range opens with a short non-command preamble the loader locates by a
  self-consistency scan. Cracking it removes the heuristic.
- **Milestone 4, rasteriser oracle gap**: not decompilation, but there is no
  DS frame in the tree to compare a picture against, so the rasteriser's two
  approximations (affine per-span texture interpolation, a conventional
  top-left fill rule) stay unverified. A frame captured from an emulator
  would close it.
- **Milestone 6, first-run asset extractor**: the headline item of the
  milestone. Not written; it either orchestrates aletheia's `tools/` or
  reimplements the unpack, which is a design decision not yet made.
- **Milestone 6, member-level mod overlay**: overriding one member inside a
  NARC (an editable decoded asset) needs the engine to load the decoded form
  and repack it, or a member-level side table. Only whole-file overlay
  exists today.
- **Milestone 1, portability sweep**: the under-declared-globals sweep, and
  warning-free builds on Clang and MSVC (GCC is clean, with and without
  SDL2).
- **Milestone 5, oracle breadth**: more sequences in the audio oracle is
  mechanical coverage, not accuracy.
- **Milestone 8+**: high-resolution/widescreen rendering, the mod API,
  packaging.

## Blocked on aletheia (needs decompiled game code)

- **Milestone 2, boot from the game's own lists**: the port builds the task
  list and allocates from the matched heap, but the lists a real boot makes
  come from game code. Specific blockers named in README.md: the heap
  environment constructor at `0x0202FC48` (the arena), and the boot path.
- **Milestone 3, opening cinematic**: needs the cinematic's own code.
- **Milestone 5, audio accuracy**: the ADSR curve, the volume and pan laws,
  Tie, portamento, modulation and the pitch wheel are documented
  approximations of the hardware's; correcting them needs the DS sound
  driver, which is not decompiled.
- **Milestone 5, the game's own sound calls**: nothing in `src/matched/`
  plays a sequence yet, so the demo chooses the SEQ id. Needs the matched
  sound callers.
- **Milestone 6, engine boots cleanly from extracted assets**: needs the
  boot path (system init, the overlay loader, and the title and field
  managers).
- **Milestone 7, gameplay**: field system (movement, warps, script VM,
  encounters), battle, menus, save. All game code.

## How to use this when a decompiled piece lands

An aletheia function becoming `EXACT` and reachable turns a "blocked" row
into "open". The boot-path blockers are worth re-reading in `docs/HANDOFF.md`
at each integration: the heap arena (`0x0202FC48`), the overlay loader, and
the title/field managers are the ones that unblock the largest part of the
port at once.
