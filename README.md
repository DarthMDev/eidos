<h1><img src="docs/banner.png" alt="eidos"></h1>

A native PC port of Pokémon Black, built from the
[aletheia](https://github.com/Project-Holon/aletheia) decompilation.

## Status

eidos compiles the game's own decompiled C and runs it natively. It does
not emulate the DS. It reads the game data from your own ROM; it ships no
game data and no game code.

The game plays from the title screen into the opening hours: new game,
the first battles, the field menu, saving and
continuing. Crashes past that point are still expected. Each one is fixed
in aletheia or here.

- 3D drawn on the GPU at a higher internal resolution, with fog, edge
  marking and anti-aliasing
- Fast forward: Tab for 2x, left Shift+Tab for as fast as the computer can
- Keyboard and gamepad, remappable, and a settings screen in the game
- A mod loader (`docs/modding.md`)

## Milestones

- [x] **Milestone 1**: Portability
  - [x] One 32-bit reference build, warning-free on GCC, Clang and MSVC
  - [x] A 64-bit build (needs the 64-bit-ready source from aletheia)
- [x] **Milestone 2**: Platform layer (SDL2)
  - [x] Window, keyboard, gamepads, touch screen with the mouse
  - [x] Audio output and frame pacing at the DS's 59.83 Hz
  - [x] The game's own boot and scheduler run every frame
- [x] **Milestone 3**: 2D engine
  - [x] Backgrounds, sprites, affine layers, windows and blending
  - [x] Both screens drawn from the hardware state the game writes
- [ ] **Milestone 4**: 3D engine
  - [x] Geometry engine, textures, models and maps
  - [x] GPU renderer with fog, edge marking and anti-aliasing
  - [ ] Checked pixel for pixel against a real DS
- [ ] **Milestone 5**: Audio
  - [x] Sequencer, instruments, PCM, ADPCM and PSG channels
  - [x] Music, sound effects and cries from the game's own sound calls
  - [ ] Checked against a real DS's output
- [ ] **Milestone 6**: Data
  - [x] First-run setup: pick your ROM, it is checked and extracted
  - [x] File layer with mod overrides
  - [ ] Readable formats for the extracted data
- [ ] **Milestone 7**: Gameplay
  - [x] Field, battles, menus and saves in the opening hours
  - [ ] Fully playable from new game to credits
- [ ] **Milestone 8**: Modding and enhancements
  - [x] Higher internal resolution
  - [x] Settings screen
  - [ ] Stable mod API and documented data formats
- [ ] **Milestone 9**: Packaging
  - [ ] Windows, macOS and Linux builds
  - [ ] Android and iOS
- [ ] **Milestone 10**: Pokémon White

## Requirements

- A checkout of aletheia next to this one, at the revision in
  `docs/aletheia-pin.md`
- Your own dump of Pokémon Black (USA/Europe)
- CMake 3.16 or later, Ninja, Python 3.11 or later
- A 32-bit compiler: MinGW-w64 GCC (`i686-w64-mingw32-gcc`) or MSVC x86
- SDL2 (`python tools/fetch_sdl.py` downloads it on Windows)

## Building

```bash
cmake -S . -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/ilp32-toolchain.cmake \
    -DALETHEIA_ROOT=../aletheia -DEIDOS_BOOT=ON -DEIDOS_OVERLAYS=ALL
cmake --build build --target eidos_boot
ctest --test-dir build
```

`docs/building.md` has every option, the other compilers and the 64-bit
build.

## Running

Start `eidos_boot`. The first time, a setup window asks for your ROM and
extracts it. Saves go to `%APPDATA%\eidos\saves` on Windows.

| DS | Key | DS | Key |
| --- | --- | --- | --- |
| D-pad | Arrow keys | START | Return |
| A | X | B | Z |
| X | S | Y | A |
| L | Q | R | E |
| SELECT | Right Shift | Touch | Left mouse button |

Tab: fast forward. Alt+Return: fullscreen. F12: screenshot. Escape: quit.

## Documentation

| File | Topic |
| --- | --- |
| `docs/architecture.md` | How eidos is put together, and how it uses aletheia |
| `docs/runtime.md` | Frame loop, scheduler, heap, platform layer, the boot |
| `docs/engine-2d.md` | The 2D engine |
| `docs/engine-3d.md` | The 3D engine |
| `docs/audio.md` | The audio engine and the ARM7 sound driver |
| `docs/files.md` | File layer, overlays, first-run extractor |
| `docs/building.md` | Every build option, compiler and platform |
| `docs/milestones-detail.md` | The milestones item by item, with evidence |
| `docs/status.md` | What is done and what is left |
| `docs/modding.md` | Mods |

## Legal

MIT (`LICENSE`). eidos contains no game data and no game code: it compiles
the source from your aletheia checkout and reads data extracted from your
own ROM. See `LEGAL.md`. Pokémon and Nintendo DS are trademarks of
Nintendo; the game belongs to Nintendo, Creatures Inc. and GAME FREAK inc.
