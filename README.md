# eidos

A native PC port of Pokémon Black (and eventually White), powered by the [aletheia](https://github.com/OukilIl/aletheia) decompilation project.

## Goal

Run Pokémon Black (and eventually White) natively on modern platforms (PC, macOS, Linux) with high performance, arbitrary display resolutions, enhanced audio, modern controls, and an extensible architecture—without emulation overhead, compiled directly from matched C source.

## Relationship with aletheia

`eidos` is the native platform and runtime engine companion to [aletheia](https://github.com/OukilIl/aletheia).

- **[aletheia](https://github.com/OukilIl/aletheia)**: Focuses on clean-room reverse engineering, decompilation, and byte-matching accuracy against the original Nintendo DS ARM9 binary.
- **[eidos](https://github.com/OukilIl/eidos)**: Consumes the matched C source code from aletheia, providing host platform abstraction (SDL2), 2D/3D hardware shims, native audio mixing, and file I/O.

## Planned Milestones

- [ ] **Milestone 1** — Portability pass
  - [ ] Fix pointer width assumptions (32-bit to 64-bit host)
  - [ ] Endianness and struct packing compatibility
  - [ ] Under-declared globals and compiler-specific extensions
  - [ ] Warning-free builds on GCC, Clang, and MSVC
- [ ] **Milestone 2** — Platform layer (SDL2)
  - [ ] Window creation and input handling
  - [ ] Audio device initialization and timing
  - [ ] DS frame emulation loop: task list scheduler running at 60 Hz, vblank callbacks
- [ ] **Milestone 3** — 2D engine shim
  - [ ] BG layers from tile and palette memory
  - [ ] OAM sprites: priorities, flips, affine transformations, double-size
  - [ ] Windowing and alpha blending
  - [ ] Dual-screen rendering layout
  - [ ] Opening cinematic and title screen render correctly
- [ ] **Milestone 4** — 3D engine shim
  - [ ] DS fixed-point geometry engine emulation
  - [ ] Matrix stack and vertex pipeline
  - [ ] Texture formats (A3I5, A5I3, 4x4 texel compressed, etc.)
  - [ ] DS rasterizer quirks (perspective handling, edge rules)
  - [ ] Overworld and map chunks render correctly
- [ ] **Milestone 5** — Audio
  - [ ] SDAT sequencer and mixer (replacing ARM7 audio core)
  - [ ] PCM and ADPCM channels
  - [ ] PSG square wave and noise channels
  - [ ] Music, sound effects, and Pokémon cries play accurately
- [ ] **Milestone 6** — Data pipeline & asset extraction
  - [ ] First-run asset extractor from legally dumped ROM
  - [ ] File layer: VFS with mod overlay support
  - [ ] Engine boots cleanly from extracted assets
- [ ] **Milestone 7** — Gameplay integration
  - [ ] Field system: player movement, warps, script VM, encounters
  - [ ] Battle system
  - [ ] Menus, UI, and save file management
  - [ ] Fully playable from new game to credits
- [ ] **Milestone 8** — Modding & enhancements
  - [ ] Scripting support and documented data formats
  - [ ] Debug and configuration overlay
  - [ ] High-resolution rendering / widescreen support
  - [ ] Stable plugin / mod API
- [ ] **Milestone 9** — Packaging & platforms
  - [ ] Windows, macOS, and Linux standalone builds
  - [ ] Mobile targets (Android / iOS)
- [ ] **Milestone 10** — Pokémon White support
  - [ ] Support for White version assets and script variations

## Legal

This repository contains no game assets, copyrighted Nintendo/Game Freak code, or ROMs. You must provide your own legally obtained ROM to extract required game assets.
