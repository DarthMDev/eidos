# eidos

A native PC port of Pokémon Black (and eventually White), powered by the [aletheia](https://github.com/OukilIl/aletheia) decompilation project.

## Goal

Run Pokémon Black (and eventually White) natively on modern platforms (PC, macOS, Linux) with high performance, arbitrary display resolutions, enhanced audio, modern controls, and an extensible architecture, without emulation overhead, compiled directly from matched C source.

## Relationship with aletheia

`eidos` is the native platform and runtime engine companion to [aletheia](https://github.com/OukilIl/aletheia).

- **[aletheia](https://github.com/OukilIl/aletheia)**: Focuses on clean-room reverse engineering, decompilation, and byte-matching accuracy against the original Nintendo DS ARM9 binary.
- **[eidos](https://github.com/OukilIl/eidos)**: Consumes the matched C source code from aletheia, providing host platform abstraction (SDL2), 2D/3D hardware shims, native audio mixing, and file I/O.

`eidos` does not vendor any of aletheia's source: `ALETHEIA_ROOT` (see
"Building" below) points at a sibling checkout, and every matched source,
manifest and asset tool this repository uses is read from there at configure
or build time. The declared end state is an open-source native port of
Pokemon Black: one executable, cross-platform, moddable, with every asset
extracted from the player's own ROM the first time it runs. What is in this
repository today is the skeleton that proves the loop is real, end to end,
on a small slice: matched game C, compiled for the host, running over assets
pulled out of the ROM by aletheia's `tools/assets/`, checked against
expected values computed independently.

Nothing here is a rewrite of the game. The only game code eidos builds is
the byte-verified C aletheia keeps under `src/matched/`, compiled unchanged.

**A matched source is edited only in aletheia, and only together with the DS
re-verify.** eidos never patches a matched file to make it build here:
portability belongs in the port layer (the shim, a wrapper include, an alias
forwarder). If a matched source genuinely has to change, that change is made
in the aletheia repository, in the same commit as a byte-identical re-verify
against the ROM (aletheia's `port/README.md` history and "Portability
rules" below document why).

## Architecture

Three layers, and the boundary between them is the whole design.

**1. Matched translation units, compiled in place.** `CMakeLists.txt` names
files under `${ALETHEIA_ROOT}/src/matched/` directly. The same files are
compiled by aletheia's `tools/csrc.py` with mwccarm and spliced into the DS
image, where they must still produce the original bytes. There is no copy,
no fork and no port-specific variant: one file, two compilers, and a rule
(below) about what may be done to it.

**2. A shim standing where the SDK was**, under `sdk/`. On the DS the game
links against TwlSDK: `FS_ReadFile`, `OS_AllocFromHeap`, the `MI_Cpu*`
family and so on. aletheia's `layout/symbols_sdk.txt` identifies those by
address, which is how the matched sources reach them (an `extern
func_02078B70` *is* `FS_ReadFile`). The shim is eidos's own code, written
against nothing but those names and the matched call sites that use them.
One file per SDK module:

| file | what it stands in for |
| --- | --- |
| `sdk/types.h` | the scalar vocabulary: u8..s64, fx32, BOOL, the fixed-point macros, endian-clean readers |
| `sdk/port.h` | the port's own API: data root, archives, panic |
| `sdk/fs.c` | FS: `FS_SeekFile`, `FS_ReadFile`, `FS_CloseFile` onto stdio |
| `sdk/os_alloc.c` | OS heap: `OS_AllocFromHeap`/`OS_FreeToHeap` and the game's debug-allocator wrappers `func_02030734`/`func_020307B0`, onto malloc |
| `sdk/mi.c` | MI: the `MI_CpuCopy*` / `MI_CpuFill*` family onto memcpy/memset |
| `sdk/narc.c` | the data root, the NARC container reader, and the ARM9 rodata window |
| `sdk/game_globals.c` | the ARM9 *objects* the matched code names (`g_Sink`, `g_0214652C`, `g_02146270`, `func_02019B04_Table`, the assert strings) |
| `sdk/aliases.c` | forwarders for addresses two matched files spell two different ways |
| `sdk/panic.c` | the one place the port gives up |

The SDK headers under aletheia's `sdk/` at its repository root are reference
for declarations only. Nothing from them is copied here, and eidos must
stay buildable by somebody who has their own ROM and nothing else.

**3. Stubs**, under `stubs/`, one symbol per file, in two shapes:

- `stub_ADDRESS.c` aborts naming the DS address. This is the honest shape for
  a game function that is not decompiled yet and whose behaviour is not
  attested anywhere. If a test reaches one, the test was wrong about its
  closure and says so loudly.
- `reimpl_ADDRESS.c` is a hand-written reimplementation whose header cites
  where the behaviour is attested: a matched caller, an aletheia
  `docs/notes/` entry, an asset tool that round-trips against it. Never a
  guess. These are *not* claimed to be byte-matching; when the real function
  is decompiled, the file is deleted and the matched source takes its place
  in the source list.

One symbol per file is load bearing: eidos is a static archive, so a stub
is only pulled in when nothing else defines its symbol. That is what lets
`tests/test_stats.c` supply its own substitute for one function without
displacing any other.

**Assets** come from the player's own ROM. aletheia's `tools/ndsrom.py
extract` writes `extract/`, and eidos opens files under `extract/data` (the
NitroFS tree) and `extract/arm9_dec.bin` (for the constant tables that live
in the ARM9 image rather than in a file). No game data is in this
repository, and none is produced by a build.

## Growing the source list

`CMakeLists.txt` carries a curated list of matched TUs. It is grown
mechanically, not by guessing:

```
python tools/closure.py --seed-func func_02018A7C
python tools/closure.py --seed-func func_02018A7C --cmake
python tools/closure.py --list-closed
```

`tools/closure.py` reads the manifest shards in aletheia's `layout/matched/`
and the `extern` declarations and `symbols:` header blocks of the matched
sources, walks the call graph through matched files only, and sorts every
remaining reference into one class:

- `matched`, another matched source defines it: link that one too.
- `sdk` / `runtime`, an SDK library symbol or an MSL runtime helper: it
  belongs in `sdk/`.
- `alias`, a matched function referenced under a different name prefix than
  the file that defines it uses. The DS build resolves relocations by
  address and does not care; a host linker resolves by name and does. Each
  one needs a forwarder in `sdk/aliases.c`.
- `open`, a game function at a known address that nothing in aletheia's
  `src/matched/` defines yet: it needs a `stubs/` entry.
- `data`, a global object: it needs a `sdk/game_globals.c` entry.

The point of the script is that the closure report and the linker always
agree. A name in the report that has no home is a link error waiting to
happen, and a link error that the report did not predict is a bug in the
script.

## Building

Requirements: CMake 3.16+, a C99 compiler (GCC, Clang or MSVC), Python 3.11+
for the fixture generator, a sibling checkout of aletheia, and an
`extract/` (in the aletheia checkout, by default) produced from your own
ROM. The standard library is the only dependency. SDL2 will be needed once
there is something to draw; this lane needs none of it.

```
# in the aletheia checkout:
python tools/ndsrom.py extract "<your-rom>.nds" -o extract

# in this repository:
cmake -S . -B build -DALETHEIA_ROOT=../aletheia
cmake --build build
ctest --test-dir build --output-on-failure
```

`ALETHEIA_ROOT` defaults to `../aletheia` (a sibling checkout next to this
repository) and is documented in `CMakeLists.txt`; pass it explicitly when
your aletheia checkout lives somewhere else, for example
`-DALETHEIA_ROOT=../bw_decomp` for a checkout that still carries its old
folder name. `BW_DATA_ROOT` defaults to `${ALETHEIA_ROOT}/extract/data` and
is resolved against `ALETHEIA_ROOT` when given as a relative path. The
tests also accept the data root as `argv[1]` and fall back to the
`BW_DATA_ROOT` environment variable, so they can be run by hand.

If CMake is not available, `tools/hostbuild.py` builds the same source
lists (read straight out of `CMakeLists.txt`, so the two cannot drift) with
whatever compiler it can find:

```
python tools/hostbuild.py --probe               # report what is installed
python tools/hostbuild.py --aletheia-root ../aletheia            # native build and run
python tools/hostbuild.py --aletheia-root ../aletheia --check    # cross compile and link only
```

`--check` uses the `arm-none-eabi-gcc` already in aletheia's
`tools/gcc-arm/`. It proves every source compiles clean and that the symbol
closure is complete, because the link fails on the first missing name. It
cannot run the tests: what comes out is an ARM ELF and there is no runner
for it.

## Portability rules

These are not style preferences. Breaking any of them either breaks the DS
build or makes the port quietly compute different numbers.

**1. A matched file may not change without the DS re-verify in the same
commit, in the aletheia repository.** If a matched source genuinely has to
change to build on a host, the change is only allowed together with, in the
same commit, in aletheia:

```
python tools/build.py --split --pack
python tools/ndsrom.py build extract --override build/pack -o build/pokeblack.nds
python tools/ndsrom.py verify extract build/pokeblack.nds
```

and a PASS. The strong preference is not to touch `src/matched/` at all:
portability belongs in the port layer, as a shim, a wrapper include that
predeclares something before the matched file's own extern, or an alias
forwarder.

**2. 32-bit types are explicit.** The DS is LP32; the host may be LP64 or
LLP64. Port code uses `u8`/`u16`/`u32`/`s32` from `sdk/types.h`, which are
defined from `stdint`, never bare `int` or `long` for a value that has a
width in the game's data.

**3. Fixed point stays as it is.** `fx32` is 1.19.12 integer arithmetic, and
the game's numbers are decided by its truncation and rounding. Never convert
a fixed-point value through a float to "simplify" it; `FX_MUL` and `FX_DIV`
keep the 64-bit intermediate the DS runtime helper provides.

**4. Reads out of extracted files are endian-clean.** Every byte in
`extract/` is little-endian whatever the host is. Use `bw_rd16`/`bw_rd32`;
never cast a `const u8 *` into a `const u32 *` and dereference it, which is a
big-endian bug and an unaligned-access bug at once.

**5. No pointer may be stored in a struct the game's layout owns.** The
matched `struct ArcFile` is `u8 file[0x48]; u32 imageBase; u16 fatOffset;
u16 count;`, four-byte aligned. The FS shim owns those 0x48 bytes and stores
an *index* into a table of open `FILE *`, not the pointer, so the struct
neither grows nor needs eight-byte alignment on a 64-bit host. `sdk/fs.c`
asserts the size at compile time.

**6. `char` is signed.** `-char signed` is part of the pinned DS compiler
configuration; the CMake build passes `-fsigned-char` (`/J` on MSVC) so the
host agrees.

## What is substituted, and why

`tests/test_stats.c` runs the real `func_02018A7C`, the real archive chain and
the real nature table, but supplies its own `func_02017E1C` (get field by id),
`func_02017E40` (set field by id) and `func_02018598` (nature) over a flat
id-indexed array. All three are matched; their closure reaches the Pokemon
record's encryption layer (`func_02018D10` / `func_02018D6C`) and the two
large field dispatchers, and building a valid encrypted party record is a
milestone of its own. The interface `func_02018A7C` uses is exactly those
three functions, so the formula under test is untouched by the substitution.
The next milestone closes it.

## Known type mismatches

Two definitions in `sdk/game_globals.c` deliberately differ from the matched
file's own declaration. Both are findings about the matched code, not
conveniences, and both are recorded in that file's header:

- `g_02146270` is declared in `src/matched/unit_02019C54.c` with a 0x10-byte
  payload, but `src/matched/unit_0201ADA4.c` writes 0x3C bytes into it (the
  personal record's size). The declaration is an under-estimate that is
  harmless on the DS, where the object is a global with the rest of the
  record after it. The port defines it at the true size.
- `func_02019B04_Table` is declared `const` in
  `src/matched/unit_02019B04.c`. The port cannot define it const, because its
  contents are game data copied in at run time from the player's own
  `extract/arm9_dec.bin`. A definition whose qualifiers differ from another
  translation unit's declaration is outside what C99 guarantees; it links and
  behaves correctly on every toolchain this port targets, and the alternative
  is editing a matched file.

A third, not yet load bearing: some matched sources declare
`OS_AllocFromHeap` / `OS_FreeToHeap` with all-`u32` parameters, because on the
DS a pointer is a u32 and the matcher only needs the relocation. On an LP64
host such a declaration cannot carry a pointer. Neither function is in the
current closure; when a TU that declares them that way enters it, the fix is a
wrapper include in the port layer, never an edit to the matched file.

## Host toolchain on Windows without an installer

eidos builds with any C99 compiler. On a machine without Visual Studio or
MSYS2, the layout used here is three portable archives kept in the aletheia
checkout, under `tools/`, all gitignored there (the same pattern as the ARM
toolchain in aletheia's `tools/gcc-arm/`):

    <aletheia>/tools/gcc-host/xpack-mingw-w64-gcc-<ver>/   MinGW-w64 GCC (xpack release zip)
    <aletheia>/tools/cmake/cmake-<ver>-windows-x86_64/      portable CMake (Kitware release zip)
    <aletheia>/tools/ninja/ninja.exe                        Ninja (release zip)

Configure and test, from this repository's root (Git Bash or PowerShell
equivalents), with `ALETHEIA` pointing at the aletheia checkout:

    ALETHEIA=../aletheia
    GCC=$ALETHEIA/tools/gcc-host/xpack-mingw-w64-gcc-14.2.0-1/bin/x86_64-w64-mingw32-gcc.exe
    $ALETHEIA/tools/cmake/cmake-3.31.6-windows-x86_64/bin/cmake -S . -B build -G Ninja \
        -DALETHEIA_ROOT=$ALETHEIA \
        -DCMAKE_C_COMPILER=$GCC -DCMAKE_MAKE_PROGRAM=$ALETHEIA/tools/ninja/ninja.exe \
        -DBW_DATA_ROOT=$ALETHEIA/extract/data
    $ALETHEIA/tools/cmake/cmake-3.31.6-windows-x86_64/bin/cmake --build build
    $ALETHEIA/tools/cmake/cmake-3.31.6-windows-x86_64/bin/ctest --test-dir build --output-on-failure

First run on this layout (2026-09-16), after the port moved into this
repository: 32 objects, both tests pass (test_text 5/5 strings, test_stats
3 species).

## Planned Milestones

- [ ] **Milestone 1**: Portability pass
  - [ ] Fix pointer width assumptions (32-bit to 64-bit host)
  - [ ] Endianness and struct packing compatibility
  - [ ] Under-declared globals and compiler-specific extensions
  - [ ] Warning-free builds on GCC, Clang, and MSVC
- [ ] **Milestone 2**: Platform layer (SDL2)
  - [ ] Window creation and input handling
  - [ ] Audio device initialization and timing
  - [ ] DS frame emulation loop: task list scheduler running at 60 Hz, vblank callbacks
- [ ] **Milestone 3**: 2D engine shim
  - [ ] BG layers from tile and palette memory
  - [ ] OAM sprites: priorities, flips, affine transformations, double-size
  - [ ] Windowing and alpha blending
  - [ ] Dual-screen rendering layout
  - [ ] Opening cinematic and title screen render correctly
- [ ] **Milestone 4**: 3D engine shim
  - [ ] DS fixed-point geometry engine emulation
  - [ ] Matrix stack and vertex pipeline
  - [ ] Texture formats (A3I5, A5I3, 4x4 texel compressed, etc.)
  - [ ] DS rasterizer quirks (perspective handling, edge rules)
  - [ ] Overworld and map chunks render correctly
- [ ] **Milestone 5**: Audio
  - [ ] SDAT sequencer and mixer (replacing ARM7 audio core)
  - [ ] PCM and ADPCM channels
  - [ ] PSG square wave and noise channels
  - [ ] Music, sound effects, and Pokémon cries play accurately
- [ ] **Milestone 6**: Data pipeline & asset extraction
  - [ ] First-run asset extractor from legally dumped ROM
  - [ ] File layer: VFS with mod overlay support
  - [ ] Engine boots cleanly from extracted assets
- [ ] **Milestone 7**: Gameplay integration
  - [ ] Field system: player movement, warps, script VM, encounters
  - [ ] Battle system
  - [ ] Menus, UI, and save file management
  - [ ] Fully playable from new game to credits
- [ ] **Milestone 8**: Modding & enhancements
  - [ ] Scripting support and documented data formats
  - [ ] Debug and configuration overlay
  - [ ] High-resolution rendering / widescreen support
  - [ ] Stable plugin / mod API
- [ ] **Milestone 9**: Packaging & platforms
  - [ ] Windows, macOS, and Linux standalone builds
  - [ ] Mobile targets (Android / iOS)
- [ ] **Milestone 10**: Pokémon White support
  - [ ] Support for White version assets and script variations

## Legal

This repository contains no game assets, copyrighted Nintendo/Game Freak code, or ROMs. You must provide your own legally obtained ROM to extract required game assets.
