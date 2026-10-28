# eidos: goal, relationship with aletheia, architecture

## Goal

Run Pokémon Black (and eventually White) natively on modern platforms (PC, macOS, Linux) with high performance, arbitrary display resolutions, enhanced audio, modern controls, and an extensible architecture, without emulation overhead, compiled directly from matched C source.

## Relationship with aletheia

`eidos` is the native platform and runtime engine companion to aletheia.

- **aletheia**: Focuses on clean-room reverse engineering, decompilation, and byte-matching accuracy against the original Nintendo DS ARM9 binary.
- **eidos**: Consumes the matched C source code from aletheia, providing host platform abstraction (SDL3), 2D/3D hardware shims, native audio mixing, and file I/O.

`eidos` does not vendor any of aletheia's source: `ALETHEIA_ROOT` (see
"Building" below) points at a sibling checkout of the decompilation project
(it has no public repository URL to link here), and every matched source,
manifest and asset tool this repository uses is read from there at configure
or build time. The declared end state is an open-source native port of
Pokemon Black: one executable, cross-platform, moddable, with every asset
extracted from the player's own ROM the first time it runs. Today the
game's own code boots natively from power-on to the title menu: the logos,
the copyright screen, the opening cinematic, the title screen and its menu,
drawn and played from the hardware state the game itself writes, with no
emulator (`eidos_boot --window`, see "Running the game's boot"). Going past
the menu is port work: the next overlays (the new-game intro and name entry)
are already matched, and are not compiled into the boot yet.

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
| `sdk/port.h`, `sdk/port_reset.c` | the port's own API: data root, archives, panic, and `bw_port_reset`, the one power-on (address space, OS layer, PXI, and the joins between them) |
| `sdk/dsmem.h`, `sdk/dsmem.c` | the DS address space: the registry of game objects, main RAM, WRAM, the I/O register file with its write hooks, palette, VRAM through VRAMCNT (CPU window and the engine-only slots), OAM |
| `sdk/host_vm.c` | reserve and commit for main RAM, the one OS-specific file under `sdk/` besides `plat_thread.c` |
| `sdk/fs.c` | FS: the ROM file system onto the extracted tree (`FS_Init`, the open/seek/read family, the archive tables) |
| `sdk/overlay.c` | FS overlays: compiled in, checked and made resident on load, against the generated overlay table |
| `sdk/vfs.c` | the data root, the mod overlay and path resolution |
| `sdk/os_alloc.c` | OS heap: `OS_AllocFromHeap`/`OS_FreeToHeap`, and the expanded heap's blocks, the one place the port takes memory from the host |
| `sdk/os_arena.c` | OS arena: `OS_AllocFromArenaLo`, the memory the heaps are built in |
| `sdk/os_irq.c` | the CPU's interrupt state: the CPSR I and F bits behind `OS_DisableInterrupts`/`OS_RestoreInterrupts` (saved per thread), the processor mode, the hooks that give the ARM7 its turn and take pending interrupts on enable, `OS_Terminate` as exit |
| `sdk/os_interrupt.c` | the interrupt controller: IE/IF/IME (the OS layer owns IF), the IRQ table and callback slots, taking an interrupt, `OS_Halt`, `OS_WaitIrq` |
| `sdk/os_thread.c`, `sdk/plat_thread.c` | OS threads on host threads under one run token; the host mutex, condition variable and thread start for Win32 and POSIX |
| `sdk/os_alarm.c`, `sdk/os_tick.c` | alarms on the OS tick, and `OS_GetTick` onto the frame loop's clock |
| `sdk/os_system.c`, `sdk/os_argument.c` | boot and console type, the protection unit, the ARM7 lock handshake, the deliver argument |
| `sdk/pxi.c`, `sdk/arm7/` | the IPC FIFO pair and IPCSYNC, and the ARM7 stand-in answering each PXI tag (OS, PM, RTC, CTRDG, CARD, FS, SND) |
| `sdk/gx.c` | GX: `GX_GetBankForBGExtPltt` and the address the heap layer's failure arm calls |
| `sdk/mi.c` | MI: the `MI_CpuCopy*` / `MI_CpuFill*` family onto memcpy/memset |
| `sdk/tcbl.c`, `sdk/tcbl.h` | the host layout of the game's task-callback list, and the port's thin helper over it |
| `sdk/narc.c` | the data root, the NARC container reader, and the ARM9 rodata window |
| `sdk/game_globals.c` | the ARM9 *objects* the matched code names (`g_Sink`, `g_0214652C`, `g_02146270`, `func_02019B04_Table`, the assert strings) |
| `sdk/aliases.c` | forwarders for addresses two matched files spell two different ways |
| `sdk/panic.c` | the one place the port gives up |

The SDK headers under aletheia's `sdk/` at its repository root are reference
for declarations only. Nothing from them is copied here, and eidos must
stay buildable by somebody who has their own ROM and nothing else.

**3. Stubs**, under `stubs/`, one symbol per file, in two shapes:

- `stub_ADDRESS.c` aborts naming the DS address. This is the honest shape for
  a game function that is not in the build yet and whose behaviour is not
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

Alongside those three, **the engine**, under `engine/`, is the host-side
stand-in for the DS hardware itself rather than for its SDK: see "The 2D
engine, and the pixel oracle it is tested against" below. It has five
parts:

| directory | what it is |
| --- | --- |
| `engine/g2d/` | the 2D engine: OBJ, BG, affine, windows and the colour effects, no SDL |
| `engine/g3d/` | the 3D engine: the fixed-point geometry pipeline, NSBMD/NSBTX loading, the display-list interpreter and a software rasteriser, no SDL |
| `engine/audio/` | the SDAT sequencer and the 16-channel mixer, the ARM7 sound core's stand-in, no SDL |
| `engine/frame/` | the per-frame skeleton: the vblank convention and the DS OS tick, no SDL |
| `engine/platform/` | the host: the SDL3 window, the keyboard, the audio device and the frame pacer |

Only `engine/platform/` includes SDL3, and it is the only optional piece of
the build.

**Assets** come from the player's own ROM. aletheia's `tools/ndsrom.py
extract` writes `extract/`, and eidos opens files under `extract/data` (the
NitroFS tree) and `extract/arm9_dec.bin` (for the constant tables that live
in the ARM9 image rather than in a file). No game data is in this
repository, and none is produced by a build.
