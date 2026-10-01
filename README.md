# eidos

A native PC port of Pokémon Black (and eventually White), powered by the [aletheia](https://github.com/Project-Holon/aletheia) decompilation project.

## Goal

Run Pokémon Black (and eventually White) natively on modern platforms (PC, macOS, Linux) with high performance, arbitrary display resolutions, enhanced audio, modern controls, and an extensible architecture, without emulation overhead, compiled directly from matched C source.

## Relationship with aletheia

`eidos` is the native platform and runtime engine companion to [aletheia](https://github.com/Project-Holon/aletheia).

- **[aletheia](https://github.com/Project-Holon/aletheia)**: Focuses on clean-room reverse engineering, decompilation, and byte-matching accuracy against the original Nintendo DS ARM9 binary.
- **[eidos](https://github.com/Project-Holon/eidos)**: Consumes the matched C source code from aletheia, providing host platform abstraction (SDL2), 2D/3D hardware shims, native audio mixing, and file I/O.

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
| `engine/platform/` | the host: the SDL2 window, the keyboard, the audio device and the frame pacer |

Only `engine/platform/` includes SDL2, and it is the only optional piece of
the build.

**Assets** come from the player's own ROM. aletheia's `tools/ndsrom.py
extract` writes `extract/`, and eidos opens files under `extract/data` (the
NitroFS tree) and `extract/arm9_dec.bin` (for the constant tables that live
in the ARM9 image rather than in a file). No game data is in this
repository, and none is produced by a build.

## The 2D engine, and the pixel oracle it is tested against

`engine/` is the fourth thing in this repository, and the first that is
neither a shim over an SDK name nor game code: it is the host-side
reimplementation of hardware the DS had and a PC does not. `engine/g2d/`
is the 2D engine: the OBJ (sprite/OAM) half and the BG (tile layer) half,
a pure C99 library with no SDL dependency and no third-party code.

| file | what it does |
| --- | --- |
| `engine/g2d/g2d.h` | the whole API, and the provenance of every rule it implements |
| `engine/g2d/lzss.c` | BIOS LZ77 (0x10) / LZ11 (0x11) container decompression |
| `engine/g2d/resources.c` | NCLR palettes, NCGR character data, NCER cell banks, NANR cell animations, NMCR multi-cell node tables |
| `engine/g2d/affine.c` | the fixed-point sine table, the 8.8 PA/PB/PC/PD parameters, and the per-pixel inverse-mapped resample for OBJ and BG alike |
| `engine/g2d/compose.c` | the compositor: one OAM's pixels, and a whole multi-cell pose, into palette indices and then into 32-bit RGBA |
| `engine/g2d/rig.c` | loading one poseable multi-part sprite bundle out of a rig archive |
| `engine/g2d/bg.c` | NSCR screens, text-mode BG layers with scroll and priority, and the per-pixel frame compositor |
| `engine/g2d/bg_affine.c` | rotation/scaling BG layers: one-byte map entries, 8bpp tiles, the four square sizes, wraparound, the 20.8 reference point |
| `engine/g2d/window.c` | WIN0/WIN1, the OBJ window, WININ/WINOUT layer masks, and the colour special effects (alpha, brightness up and down) |
| `engine/g2d/hwcompose.c` | both screens from the DS 2D hardware state (registers, palette RAM, OAM, VRAM through the bank mapping) instead of from asset files |

What is covered: 4bpp and 8bpp character data, both `char_format`
conventions (8x8 tiled and linear bitmap), the `0xFFFF` "unspecified
dimensions" sentinel (which switches tile addressing from the sheet's own
width to the OAM's, the 1D OBJ mapping case), the full OAM attribute
decode (shape, size, H/V flip, affine double-size, priority, 16-colour
palette bank), the cell bank's simple and extended record shapes, the
NANR frame-entry indirection with all three frame-data shapes and the
duration-summing timeline, and the NMCR node walk.

Three behaviours are not the obvious reading of the bitfields, and each
one is attested rather than assumed:

- **Per-OAM flip placement.** A set flip bit replaces the entry's own
  position: `x = -(x + objWidth)`, `y = -(y + objHeight)`, with the
  dimensions from the standard OBJ tables. This is
  `src/matched/unit_02067B40.c`, the composer's flip lane, which names
  both tables by address. A composer that mirrors the pixels but leaves
  x and y alone puts every flipped part off by its own size.
- **Affine double-size placement.** `attr0` bits 8-9 == 3 shifts the
  entry by half its own width and height (`func_02066E30`, recorded in
  aletheia's `docs/ASSETS.md`).
- **Painter's order.** Nodes are emitted forward, and within a node's
  cell its OAMs are emitted forward, so the earliest-emitted OAM lands in
  the lowest hardware OAM slot, and a lower slot draws in *front*. A
  painter that blits in list order and lets later writes win therefore
  has to run the reverse of the emission order, with `attr2`'s priority
  outranking the slot index. Getting this backwards is what made
  sprites' eyes vanish under their heads in the asset lane.

**The oracle.** `tests/test_g2d.c` does not compare against hand-written
expected values. `tools/rig_oracle.py` imports aletheia's
`tools/assets/rig.py` (out of the sibling checkout; nothing is copied
here and aletheia is not modified), composes the same poses in Python and
dumps each one as a raw RGBA file next to the PNG the asset lane itself
produces, into the build directory. The C engine composes the same poses
and the test compares them pixel for pixel, printing the first differing
pixel and both colours on a mismatch. The raw dump exists so neither side
needs a PNG decoder or any third-party code; its framing is `'RGBA'`,
`u32 width`, `u32 height`, then `width * height * 4` bytes.

The two composers share no code and no language: rig.py was written in
the decompilation project against the archive's bytes and the game's own
composer, `engine/g2d` here against the same attested formats. Four
species are checked by ctest (25, 6, 94, 150 of `a/0/0/4`), covering
different cell counts, node counts and double-size mixes; all four are
pixel-identical. `test_g2d` also accepts an explicit species list after
the oracle directory, which is how a wider sweep is run by hand: species
1 to 120, 118 of 120 pixel-identical.

### The affine path, and the fixed-point question settled

The 2 of those 120 that did not render (species 50 and 88) are genuinely
non-identity SRT frames at t=0: species 50's sequence 4 carries `sy =
2048` (a half-height scale) and species 88's sequence 1 carries `rotZ =
3640`. Both render now. `engine/g2d/affine.c` is the OBJ rotation and
scaling engine:

- a **fixed-point sine table**, 4096 entries over one turn, fx32 valued
  (4096 == 1.0), indexed by a 16-bit binary angle shifted right by four,
  which is the index convention the NNS fixed-point helpers use and the
  one a NANR frame's `rotZ` (1/65536 of a turn) feeds directly. The table
  is built once by evaluating the sine series in 2.30 fixed point with
  64-bit intermediates: no float, no libm, no table literal. Every entry
  equals `round(sin(2*pi*i/4096) * 4096)`, checked for all 4096 indices.
- **PA/PB/PC/PD in 8.8**, the four parameters the DS OBJ engine holds per
  affine slot, computed from the frame's `(sx, sy, rotZ)`. They hold the
  *inverse* of the intended transform, because the hardware walks output
  pixels and asks each one which texel it shows: `PA = cos * 4096 / sx`,
  `PB = sin * 4096 / sx`, `PC = -sin * 4096 / sy`, `PD = cos * 4096 / sy`,
  each then in 8.8. Identity is `(256, 0, 0, 256)`; a 2x scale is `PA =
  PD = 128`.
- **the per-pixel fetch**: the output pixel's offset from the reference
  point through the matrix, shifted right eight with the fraction
  *discarded*, giving a whole texel. That truncation is the hardware's
  nearest neighbour.
- **the double-size reference point**: the drawn canvas is `W x H`
  normally and `2W x 2H` with `attr0` bits 8-9 == 3, centred on the same
  point either way. The matched code records the same offset from the
  other end of the pipe: `func_02066E30` shifts a double-size entry's
  translation by half its own width and height before shifting it left 12
  into the geometry engine's translate register, which is exactly the
  displacement of a `2W x 2H` canvas whose centre must stay put while its
  top-left moves.

The NNS cell animation carries one SRT per *node*, applied about the cell
origin (`src/matched/unit_020640B0.c` and
`docs/notes/g2d_srt_setters.c.txt`), not one matrix per OAM about each
OAM's own centre, so `g2d_compose_pose` composes the cell at identity and
resamples it as one picture. The arithmetic is the same either way;
`g2d_obj_affine_render` is the per-OBJ form of it, with the double-size
canvas, and is what the synthetic tests drive.

**The decision.** The engine follows the hardware. rig.py inverse-maps
the same geometry in double precision with `math.cos`/`math.sin` and
rounds the source coordinate to nearest; the engine uses a 12-bit sine
table, an 8.8 matrix and a floor-truncating shift. Where the two differ,
on a pixel near a texel boundary, **the engine is the reference**, because
the DS is. rig.py is not modified: it is the asset lane's tool, its
double-precision mapping is a good approximation for exporting art, and
the two answering the same question differently is now a documented
divergence rather than an open one.

**The divergence, measured.** Species 50 (a pure half-height scale)
renders **pixel-identical** to the oracle at 44x34: a power-of-two scale
with no rotation lands on the same texel in both arithmetics. Species 88
(`rotZ = 3640`, about 20 degrees) renders on the same 61x47 canvas as the
oracle and differs on **64 of 2867 pixels, 22 per mille**, all of them on
texel boundaries inside the rotated art. `tests/test_g2d.c` measures that
number every run and fails if it exceeds 150 per mille, so a real
regression in the affine path is caught while the fixed-point difference
itself is recorded rather than papered over.

**The synthetic tests.** `tests/test_affine.c` uses no game data and no
oracle: every expected pixel is computed on paper from the rule above. It
checks the sine table at the quarter and eighth turns, the four
parameters for identity, a 2x scale and a quarter turn, a quarter turn of
a known 4x4 block at single size (where the even-sized block loses a
column off the canvas, which is what double size exists to prevent), the
same quarter turn at double size (where all sixteen texels survive), a 2x
scale at double size (which is also the check that the shift floors
rather than truncating towards zero), and the cell-level resample at
identity and at a quarter turn.

### BG layers

`engine/g2d/bg.c` is the second piece: the NSCR (screen / tile map)
decoder, text-mode BG rasterisation over an NCGR/NCLR pair, and a frame
compositor.

What is covered: the SCRN chunk (screen size in pixels, the declared
entry-array size, and the u16 screen entries with a 10-bit tile index,
the two flip bits and the 4-bit palette row); 16-colour mode, where the
entry's palette row selects one bank, and 256-colour mode, where the row
field is ignored and the tile indexes the whole palette; character data
addressed flat by `tile_index * bytes_per_tile`, which is how a screen
entry addresses character memory and is independent of the CHAR
sub-header's `tile_num_x/y` (a BG tileset's are routinely the `0xFFFF`
sentinel); per-layer scroll (`BGxHOFS`/`BGxVOFS`) with hardware
wraparound; per-layer priority; and a compositor that stacks up to four
BG layers and an OBJ list into one 256x192 RGBA frame over the backdrop
(colour 0 of the BG palette).

Three rules are attested rather than assumed:

- **The screen-entry layout is not row-major for the wide and tall
  sizes.** `src/matched/func_02040EE0.c` is the game's own `(x, y)` to
  entry-index helper and enumerates every size the BG system knows: a
  16-wide map (the 128x128 affine size), the two 32-wide ones, 512x256
  where the second 32x32 map follows the first, and 512x512 where four
  are laid out in a 2x2 grid 0x400 entries apart. `g2d_bg_screen_index`
  is that function transcribed, u16 truncation included.
- **The BGxCNT bit assignment.** aletheia's `docs/notes/func_0203FCA0.c.txt`
  (bg_sys.c's per-layer initialiser) pins it from the literal pool and
  the store sequence: bits 0-1 priority, 2-4 character base, 7 the
  16/256 colour mode, 8-12 screen base, 13 extended palette for a text
  layer and wraparound for an affine one, 14-15 screen size. The layer
  struct here carries exactly those fields.
- **Index 0 is transparent in a frame, and opaque in a single-layer
  render.** On hardware a BG tile pixel of palette index 0 lets the
  layer behind (ultimately the backdrop) through, and
  `g2d_frame_compose` does that. The asset lane's renderer paints index
  0 as its palette colour, because it renders one layer on its own with
  nothing behind it, and `g2d_bg_render` matches that so the oracle
  comparison is exact. The two paths are one function apart on purpose.

**The oracle.** `tests/test_bg.c` compares against
`tools/assets/background.py`, the decompilation project's own NSCR
renderer, the same way the OBJ half compares against `rig.py`.
`tools/bg_oracle.py` imports it out of the sibling checkout, renders each
screen and writes the PNG, a raw RGBA dump and `screens.txt`, a manifest
recording which NCGR and NCLR sub-file background.py's coverage rule
paired with each NSCR. The C test reads that manifest and renders the
same three sub-files. The pairing heuristic is deliberately not under
test and not reimplemented here: which sub-files belong together is a
property of how the archives are laid out, not of the DS hardware, and
aletheia's `docs/ASSETS.md` documents both the rule and its known gap.

Eight screens are checked by ctest, picked for coverage rather than for
looks, and all eight are pixel-identical:

| screen | size | mode | what it is |
| --- | --- | --- | --- |
| `titledemo.narc#0` | 256x256 | 8bpp | the leftover Diamond/Pearl title logo, 255 distinct colours |
| `a/1/4/3#22` | 512x192 | 4bpp | the battle-background terrain layer |
| `a/1/3/7#26` | 256x256 | 4bpp | the "VS. RECORDER" UI banner |
| `a/0/4/6#11` | 256x512 | 4bpp | a menu window frame, the tall size |
| `a/0/3/4#17` | 512x256 | 8bpp | the wide size in 256-colour mode |
| `a/1/5/9#108` | 512x512 | 4bpp | the largest screen size |
| `a/1/2/0#4` | 256x192 | 4bpp | 55 distinct colours |
| `a/1/9/2#2` | 256x256 | 4bpp | 29 distinct colours, the Wi-Fi grid |

All four hardware screen sizes, both colour modes, and two authoring
sizes that are not hardware sizes at all. What the oracle cannot see,
because background.py renders one layer with no scroll, no priority and
no backdrop, the test checks separately: the screen-entry layout at the
boundaries that distinguish the four sizes, scroll wraparound (a full-map
scroll samples identically, an 8-pixel scroll shifts by exactly one
tile, a non-wrapping layer scrolled off its map samples nothing), the
compositor's priority and transparency order against an independent
`g2d_bg_sample` walk, and that an OBJ wins a priority tie against a BG.

### Affine BG layers

`func_0203FCA0` takes a layer type and gives layers 2, 3, 6 and 7 an
inner switch over three of them: type 0 is text, types 1 and 2 are the
two affine shapes (they build the same BGxCNT terms in a different order
and take bit 13 from a different template field). `engine/g2d/bg_affine.c`
is that second shape, for BG2 and BG3, sharing every piece of arithmetic
with the OBJ affine path: the same 8.8 PA/PB/PC/PD, the same
floor-truncating shift, the same discarded fraction. What differs is all
downstream of one thing, that an affine map entry is a single byte:

- **one byte per tile**, so 256 tiles addressable, no flip bits and no
  palette row. `src/matched/func_02042E34.c` is the game's own 8bpp
  screen fill and writes exactly one byte per entry, against
  `func_02042ED8.c`'s u16 per entry for a text screen.
- **8bpp character data**, 64 bytes per tile, indexing the whole
  256-colour palette, so a sample's palette index and its colour-table
  index are the same number.
- **a plain `tiles_wide * tiles_high` map**. The 512-pixel text screen's
  block layout has no affine counterpart, which is the other half of why
  `func_02040EE0`'s size code 0 (the 16x16-tile, 128x128-pixel map) is a
  simple `x + y * 16`.
- **four square sizes**: 128, 256, 512 and 1024 pixels.
- **BGxCNT bit 13 is wraparound** rather than the extended palette flag.
  Every affine map side is a power of two, which is why the hardware can
  wrap with a mask rather than a modulo, and why the engine does too.
- **a 20.8 reference point**, BGxX/BGxY, 28-bit signed and sign-extended
  from 28 bits here: a reference point that overflows wraps on hardware
  rather than running away, and an affine layer that has been scrolling
  for a long time is exactly where that would show.

For screen pixel `(x, y)` the sampled texel is `tx = (ref_x + PA * x +
PB * y) >> 8`, `ty = (ref_y + PC * x + PD * y) >> 8`, fraction discarded.

**The compositor was rebuilt around this.** It now resolves one pixel at
a time (`g2d_frame_stack`) rather than painting layer by layer. Painting
was enough while the only question was which colour ends on top; an
affine layer slots into the same priority order, and the colour special
effects below need to know *which* layer the topmost pixel belongs to and
which layer is directly beneath it, which is a per-pixel question. The
order it walks is the painting order read front to back, so the picture
is unchanged and the BG oracle test is untouched. `g2d_frame_compose`
still exists, as a wrapper over `g2d_frame_compose_desc`.

**The tests** are synthetic, in `tests/test_bg_affine.c`: a three-tile
8bpp set (a gradient tile, a constant-100 tile, a constant-200 tile) over
a 16x16 map, so every sample says both which tile it landed in and where
inside it. It checks the four sizes, identity sampling, the 20.8
reference point in whole pixels and in a fraction that must floor rather
than truncate towards zero, a 2x scale, a quarter turn (where screen `y`
walks the texture's `x` axis and any positive screen `x` lands at a
negative texture `y`), wraparound on and off at all four edges, and that
an affine layer takes part in the frame stack at its own layer number.

**The one discrepancy, stated rather than hidden.** For a 512-pixel-wide
or 512-pixel-tall map the VRAM screen order and a row-major reading of
the entry array are different orders, and which one an NSCR file on disk
is in is not settled by the evidence this repository has.
`func_02040EE0` attests the VRAM order, and `background.py` reads every
NSCR row-major at its own declared width. Two facts point at row-major
being the authored order: several NSCR resources declare sizes that are
not hardware screen sizes at all (`a/1/4/3#22` is 512x192, `a/0/3/3#29`
is 448x192), where the VRAM order is not even defined, and the game has
a helper whose whole job is to place an entry at `(x, y)` in VRAM, which
is what an upload path that scatters a row-major authoring buffer into
the hardware's map blocks needs. Neither is proof, and no rendered
512-wide screen was found whose content decides it by eye.

**Both readings are implemented, behind a flag on the layer, and the
default is the attested one.** `G2dBgLayer.screen_order` is
`G2D_BG_ORDER_VRAM` (the value zero, so a zeroed layer gets it), which is
`func_02040EE0` transcribed, or `G2D_BG_ORDER_ROW_MAJOR`, which reads the
same entry array row-major at the map's own tile width the way
`background.py` does. The flag is ignored for `G2D_BG_SIZE_FLAT`, which
is row-major by construction and is the only reading available for a
screen whose declared size is not a hardware size at all; that is what
the oracle comparison uses, because it is what the oracle does. The two
readings coincide for every size except 512x256 and 512x512, and
`tests/test_bg_affine.c` pins the one place they differ: tile (32, 0) of
a 512x256 map is entry 32 row-major and entry 1024 in VRAM order, and the
test reads a different tile through each. **The question itself is still
open.** Settling it needs the matched upload path (the caller of
`func_02040EE0` that copies a screen resource into VRAM), which is not
decompiled yet; until then the engine defaults to what the game's own
helper says and the other reading is one field away.

### Windows and the colour special effects

`engine/g2d/window.c` is the last piece of the 2D engine's per-pixel
stage: two mechanisms that meet in the compositor.

**The windows** decide, per pixel, which layers may contribute and
whether the colour effect runs there. WIN0 and WIN1 are rectangles
(WIN0H/WIN0V and WIN1H/WIN1V), each with its own half of WININ; the OBJ
window is not a rectangle but the union of the pixels of the OBJs whose
OAM mode selects it, which is the OBJ engine's business rather than the
compositor's and reaches the window system as a mask the caller supplies;
WINOUT's two halves cover the OBJ window and everything outside every
window. Priority runs WIN0, WIN1, the OBJ window, outside, and the first
region that contains the pixel decides it. With no window enabled at all
the mask is every layer plus the effect, which is what the hardware does
when DISPCNT's window bits are clear. A right or bottom edge that is past
the screen, or before its own left or top edge, reads as the screen edge:
X2 is eight bits on hardware and cannot express 256 at all, which is why
a full-width window is written with X2 = 0 as often as with X2 = 255.

**The colour effects** are BLDCNT's three: alpha blending between a first
and a second target with BLDALPHA's EVA and EVB, brightness increase with
BLDY's EVY, and brightness decrease with the same. The coefficients are
sixteenths and saturate at 16. The rule the compositor enforces is the DS
one, and it is the reason the compositor had to be rebuilt around
per-pixel resolution: **the effect runs only where the topmost pixel
belongs to the first target set, and alpha blending additionally only
where the pixel directly beneath that one belongs to the second target
set.** A second-target pixel two layers down is not blended with, and
neither is one the window system hid at that pixel, and neither is
anything when the window in force there has its effect bit clear. The
backdrop is a target like any other and is what "directly beneath" means
once the last layer has been passed.

**The arithmetic is in five bits per channel**, not in the eight the
palette is stored as. A colour is folded back to 5 bits, blended, clamped
to 31 and expanded again; the fold `(c8 * 31 + 127) / 255` recovers the
original 5-bit value for all 32 of them, which the test checks for every
one. Blending in the 8-bit expansion instead would round differently from
the hardware on essentially every blended pixel.

**The tests** are synthetic, in `tests/test_window.c`: a frame of two
flat-coloured 8bpp text BG layers over a backdrop, every expected pixel
worked out in 5-bit channels on paper. It checks the window priority
order (WIN0 over WIN1 over the OBJ window over the outside), the
half-open edges, the past-the-screen edge rule, layers hidden by a window
revealing the layer behind and then the backdrop, alpha blending at three
coefficient pairs, saturation at 31, a coefficient above 16 reading as
16, brightness up and down at three strengths, and the four ways the DS
rule refuses to blend: a second target that is not in the frame, a second
target that is two layers down, a window whose effect bit is clear, and a
first target that is not the topmost pixel. It also checks the
positive case that makes the "directly beneath" rule concrete: hiding BG1
with a window makes the backdrop the layer directly beneath BG0, and the
same blend that refused to run then runs.

### Both screens from the hardware state

Everything above draws what a parsed asset file says. Once the game's own
code runs, the picture is no longer described by files but by what that
code wrote into the 2D hardware: DISPCNT and the BG, window and blend
registers, palette RAM, OAM, and VRAM through the bank mapping
(`sdk/dsmem.h`). `engine/g2d/hwcompose.c` reads that state and builds the
same layer descriptions the asset path builds, then runs the same
`g2d_frame_compose_desc`, so every rule the two share is one piece of
code:

- a text BG is a `G2dBgLayer` over a map read out of VRAM at the screen
  base and a tile view into VRAM at the character base (DISPCNT's 64 KB
  bases on the main engine plus BGxCNT's), in the VRAM entry order
  `func_02040EE0` attests;
- an affine BG is a `G2dBgAffineLayer`, BGxPA..PD and BGxX/BGxY straight
  from the registers;
- the 3D layer is the `G2dLayer3d` at BG0 that g2d already composes;
- WIN0/WIN1/WININ/WINOUT and BLDCNT/BLDALPHA/BLDY become `G2dWindows` and
  `G2dBlend`, and MASTER_BRIGHT is applied last with BLDY's arithmetic.

Two things needed the compositor to grow. Layer shapes no asset type
expresses (an extended rotation/scaling BG with 16-bit entries, 256-colour
and direct-colour bitmap BGs, a 256-colour text BG through an extended
palette) are resolved from VRAM into a `G2dLayerRaster`, an RGBA layer
with transparency that the stack walks like any BG. And a
semi-transparent OBJ (OAM mode 1) is now a flag on `G2dObjEntry`: it is a
first target whatever BLDCNT says and alpha blends whenever the pixel
directly beneath it is a second target.

OBJs are not passed one entry per OAM slot, because that is not what the
hardware does: it renders every OBJ into one OBJ pixel per screen position
first (lower priority value wins, then lower OAM index) and only then
sets that one pixel against the BGs, so two OBJs never blend with each
other. hwcompose builds that line buffer (1D and 2D tile mapping, the 1D
boundary, flips, affine and double-size through `g2d_obj_affine_render`,
4bpp, 8bpp and extended palettes, the OBJ window from mode 2) and hands it
over as non-overlapping full-screen poses. POWCNT1 bit 15 decides which
engine is on which screen. Display mode 0 and forced blank are white,
display mode 2 shows bank A..D directly. BG mode 6's large bitmap, mode
7, main memory display, bitmap OBJs and mosaic are reported through
`G2D_HW_UNSUP_*` flags rather than drawn wrong; the display capture is
out of scope.

**The tests**, `tests/test_hwcompose.c`, reset the port's own address
space (`sdk/dsmem.c`) and write registers, palette, OAM and VRAM byte by
byte, so the bank mapping the renderer reads is the one a matched store
goes through (the extended palettes and the LCDC banks included, through
`bw_vram_slot` and the LCDC window). The synthetic half checks hand-computed pixels for BG
priority and scroll wrap, OBJ against BG and OBJ against OBJ, hidden
OBJs, negative X and wrapped Y, flips, 1D and 2D mapping and the 1D
boundary, affine OBJs at identity and 2x in double size, WIN0 over WIN1,
the OBJ window, alpha and brightness and the effect bit, the
semi-transparent OBJ, affine BGs with and without wrap, the extended
affine BG, both bitmap BGs, BG and OBJ extended palettes through banks E
and I, the 3D layer, master brightness on each engine, the screen swap
and display modes. With game data, the title screen (`titledemo.narc`
0/1/2) and the species 25 pose are put into VRAM, palette RAM and OAM the
way the SDK would, and must come out **pixel-identical** to the asset
path drawing the same content from the files: the title scrolled on the
main engine, the pose on the sub engine, and both together under WIN0
and an OBJ-over-BG0 alpha blend. All three are identical.

## The 3D engine, and the geometry oracle it is tested against

`engine/g3d/` is the host-side stand-in for the DS's geometry engine and
its rasteriser. Pure C99, no SDL, no third-party code, and not even
`math.h`: every number below is integer arithmetic on the hardware's own
20.12 fixed point, which is portability rule 3 taken literally.

| file | what it does |
| --- | --- |
| `engine/g3d/g3d.h` | the whole API, and the provenance of every rule it implements |
| `engine/g3d/mtx.c` | fx32 matrices, the four matrix modes, the position/vector stack, and frustum, perspective and look-at |
| `engine/g3d/nsbmd.c` | NSBMD/NSBTX containers, Nitro resource dictionaries, TEX0 records, the MDL0 shape dictionary and node dictionary, the render-command bytecode, the a/0/1/4 texture index |
| `engine/g3d/texture.c` | all seven GX texture pixel formats |
| `engine/g3d/gx.c` | the display-list interpreter: the packed command word, the whole opcode table, every vertex encoding, strip assembly |
| `engine/g3d/raster.c` | the 256x192 software rasteriser, and the meeting point with the 2D compositor |
| `engine/g3d/hw3d.c` | the 3D hardware interface: the geometry FIFO and command ports, the 3D registers, the texture slots, `SWAP_BUFFERS` |

**Where the rules come from.** In order of authority, and each file
header repeats it: aletheia's asset lane (`tools/assets/nsbmd.py` cracked
the container, the resource dictionary, the TEX0 records and the MDL0
shape dictionary; `tools/assets/map_shapes.py` cracked the render-command
bytecode and the material-to-texture link, and `docs/ASSETS.md` records
both with their gaps); aletheia's matched sources
(`src/matched/unit_02048AD0.c` and `unit_02048BC4.c` are g3d_light.c's
accessor run and light-set builder, and are what says the game's 3D
vectors are three `fx32` in the 1.19.12 format `sdk/types.h` defines --
no matched source in aletheia today carries the geometry engine's own
arithmetic); and public NDS hardware documentation for the command set,
the matrix modes and stack depths, the vertex encodings,
`TEXIMAGE_PARAM`'s bits, the texture formats and `POLYGON_ATTR`'s fields.

### What is attested

**The container and the display lists**, because they are compared
against an oracle. `tools/g3d_oracle.py` imports aletheia's `nsbmd.py`
and `map_shapes.py` out of the sibling checkout (nothing is copied here,
and aletheia is not modified), writes the per-shape textured OBJ that
lane exports into the build directory, and beside it `models.txt`: the
manifest naming each shape's texture and the `(width, height)` the OBJ's
`vt` lines were divided by. `tests/test_g3d.c` decodes the same
sub-files with the engine's own reader, runs every shape's display list
through `engine/g3d/gx.c` with an identity matrix stack, and compares.

| model | what it is | shapes | vertices | faces | textures agreed |
| --- | --- | --- | --- | --- | --- |
| `pl_00_00` (`a/0/0/8#120`) | an interior, the model the shape-dictionary crack was confirmed against | 40 / 40 | **5678 / 5678** | **1782 / 1782** | 40 / 40 |
| `map24_23` (`a/0/0/8#0`) | an outdoor terrain chunk | 26 / 26 | **2120 / 2120** | **572 / 572** | 26 / 26 |

**The tolerance, and why it is what it is.** The OBJ writes each
coordinate with `%.6f`, so the oracle's number is the exact value rounded
to six decimals and carries at most 5e-7 of representation error. The
engine's number is exact: a vertex command's payload is a whole number of
1/4096 (VTX_16, VTX_XY/XZ/YZ), 1/64 (VTX_10) or 1/512 (VTX_DIFF) and
stays in fx32 the whole way. The test therefore requires
`|obj - engine/4096| <= 1e-6` and reports the worst deviation every run.
**Measured: 5.000e-07 on both models**, for positions and for texture
coordinates alike, which is exactly the rounding bound and not one ulp
more. Texture coordinates are compared after undoing the OBJ's own
normalisation (the manifest's per-shape texture size, and the flipped v
axis), so what is compared is the sixteenth-of-a-texel number `TEXCOORD`
really carried.

The face comparison is not a count: every face's vertex indices are
compared slot by slot, relative to its own shape, so the four primitive
kinds and the strip winding (an odd triangle of a strip has its last two
vertices swapped; a quad strip's four consecutive vertices go out as
a, b, d, c) are under test rather than assumed.

**Which texture formats occur.** The oracle also surveys `a/0/1/4`, the
shared terrain and building texture archive every map model draws from:
**21780 textures**, in five of the seven formats.

| format | name | textures in `a/0/1/4` |
| --- | --- | --- |
| 3 | 16-colour, 4bpp | 15395 |
| 2 | 4-colour, 2bpp | 3467 |
| 1 | A3I5 (5-bit index, 3-bit alpha) | 1702 |
| 6 | A5I3 (3-bit index, 5-bit alpha) | 1083 |
| 4 | 256-colour, 8bpp | 133 |
| 5 | 4x4 texel compressed | **none** |
| 7 | direct colour, BGR555 | **none** |

All seven decode anyway, because a decoder that only handles what one
archive happens to use is a trap for the next archive. Format 5 is the
one piece here with no real instance behind it: it is implemented from
the public block encoding (one u32 of sixteen two-bit indices plus one
u16 of palette address and interpolation mode per 4x4 block, both inside
the one region the TEX0 header bounds) and `engine/g3d/texture.c` says so
where it lives. aletheia's own asset lane reached the same conclusion,
that format 5 is absent ROM-wide.

**Palette pairing is the asset lane's, gaps included.** A texture record
does not name its palette; the material record that would is a structure
nothing in this project has cracked. So `g3d_tex0_decode` does what
aletheia does: the `<name>_pl` naming convention where the palette
dictionary has that exact name (a real link, confirmed 1:1 across the
shared sets), otherwise the palette whose decoded indices all land in
range, tie-broken by how closely the format's index range matches the
palette's colour count. Validated per candidate, not guessed, and still
a stand-in for a link that has not been found.

### The rasteriser, and what it does not claim

**There is no DS frame in this repository to compare a picture against,
and nothing in `engine/g3d/raster.c` claims pixel-exactness with the
hardware.** That is the sharpest difference between this engine and the
other two: `engine/g2d` is pixel-identical to a Python renderer and
`engine/audio` is bit-identical to a Python synth, and neither statement
has an analogue here yet. Every rule in the rasteriser is marked in its
own comment as one of three things, and the file header lists them:

- **documented hardware behaviour**: the near-plane clip, the
  perspective divide, the 256x192 viewport, `POLYGON_ATTR`'s two culling
  bits and its 5-bit alpha (0 being the wireframe mode, which this engine
  skips rather than quietly filling), `TEXIMAGE_PARAM`'s repeat, flip and
  clamp wrapping by mask (every texture dimension is a power of two,
  which is what makes a mask rather than a modulo correct), and the
  modulate and decal blend formulas, which are defined on 6-bit colour
  channels and 5-bit alpha rather than on the 8 bits a host buffer holds.
  Blending in the 8-bit expansion instead would round differently on
  essentially every pixel, which is the same reason
  `engine/g2d/window.c` blends in 5 bits.
- **an approximation, named as one**: texture coordinates are
  interpolated **affinely along a span**, not divided per pixel, which is
  invisible on the near-axis-aligned geometry a map chunk is made of and
  would first show on a steeply oblique floor; the fill rule is a
  **top-left rule at pixel centres** in 20.4 fixed point, chosen to be
  watertight (no pixel drawn twice, no gap between shared edges) rather
  than to be the hardware's, whose own edge inclusion is not documented
  precisely enough anywhere public to transcribe; depth is **z/w mapped
  linearly onto a 23-bit range**, and the exact quantisation is this
  engine's; the toon and highlight polygon mode **falls back to
  modulate** when no toon table is loaded (the asset path loads none,
  `engine/g3d/hw3d.c` loads the game's `TOON_TABLE`), because inventing a
  ramp would be a guess; and shadow polygons are **not drawn at all**
  rather than drawn as black slabs.
- **a port decision**: a polygon whose `POLYGON_ATTR` word is zero is
  drawn double-sided and opaque rather than culled. That is not a
  hardware rule. A shape's display list is entered at a heuristically
  located offset, which is usually *past* the model's own `POLYGON_ATTR`
  command, so the attribute word really was never seen rather than really
  being zero, and culling everything in that case would render an empty
  screen and call it correct.

**What the raster test checks instead.** Two things, and it says which is
which. It renders `pl_00_00` from a fixed camera, writes the frame as a
BMP into the build directory, and asserts *content* rather than pixels:
**21239 of 49152 pixels covered (432 per mille), 133 distinct colours,
20229 of them textured** (a pixel whose three channels are not all
equal), and that the depth buffer was written on exactly the pixels the
colour buffer was. Beside that are synthetic cases whose every expected
pixel is computed on paper from the rules above: one flat triangle
(coverage at six hand-checked pixels including both sides of the
hypotenuse at one pixel's resolution, both culling bits, the wireframe
alpha, and that an untextured polygon's colour comes through byte for
byte), one textured quad (a 2x2 texture over 64x48 pixels, four
hand-computed texel lookups, the exact modulate result at two vertex
brightnesses, and the repeat wrap), the matrix stack (identity, that
`MTX_MULT` puts the parameter on the *left* so a later command is applied
to the vertex earlier, push/pop, the underflow flag, and that `FX_MUL`
truncates towards minus infinity), and the texture byte sizes and two
format decodes. The node half is described under "The node dictionary"
below. **133 checks, 0 failures.**

### The node dictionary, and the transforms it carries

The engine reads the MDL0 **node (bone) dictionary**, the first Nitro
ResDict in a model's data tail, and decodes the whole transform: its
4-byte records are offsets from the dictionary's own start to a
variable-sized node record, and the records run back to back to the byte
the model header names as the start of the render-command section. The
record's flags byte is an **active-low presence mask** for a
**translation** (12 bytes of `fx32`), a **rotation** (16 or 4 bytes) and
a **scale** (24 bytes), with the flags' bit 3 selecting the rotation
form. So a record is one of 4, 8, 16, 20, 28, 32, 40, 44 or 56 bytes,
and the decoded lengths land on the section end in **all 740 models
scanned**. `docs/g3d-nodes.md` carries the full byte evidence.

- **Rotation.** The 4-byte field is two `s16`, the y and z of the
  rotation matrix's third row (1.0 = 4096). The full form stores the
  other two rows as six more `s16` and the third row's x as the record
  header's second halfword (sign-extended `s16`); on the 21 nodes that carry both, the 4-byte halves
  match the third row of the stored rows **21 of 21**. The packed form
  stores only two components, always a unit pair; the engine completes
  it as the x-turn, but that completion is an **assumption, not a
  verified decode** (no packed/full twin exists in the corpus to fix the
  missing component: 0 such twins across 740 models). See
  `docs/g3d-nodes.md`, "The packed form".
- **Scale.** The 24 bytes are three `fx32` scale factors followed by their
  reciprocals, `round(2^24 / scale)` in **534 of 537** samples.
- **Hierarchy.** The render-command section carries a
  `[0x26|0x66|0x46] <node> <parent> ...` record per node; `parent ==
  node` is the root. Every parent is in range and the graph acyclic in
  **670 / 670** models, and `staffroll_b`'s tree is exactly the biped its
  bone names describe (`l_foot -> l_shin -> l_thigh -> pelvis`, and so
  on).
- **Binding.** Opcodes `0x04`/`0x24`/`0x44` bind a material and `0x05`
  draws a shape under the last node record, so a shape drawn under
  several nodes is instanced. Every drawn shape binds to a node that
  exists, **0 bad bindings**, and 636 of 670 models draw all their
  shapes; a shape the render commands do not name is simply not drawn.

The two models the geometry oracle uses have exactly **one node each**,
whose record is the 4-byte identity (`flags 0x07`), so the transform
applied is the identity and the oracle comparison and the house render
are what they were. `g3d_render_model` now follows the draw list,
applying each shape's node world matrix and its ancestors' before the
camera; `g3d_model_set_node_local` / `g3d_model_set_node_parent` /
`g3d_model_update_nodes` drive a pose from outside. `tests/test_g3d.c`
checks the synthetic pipeline (translation, scale, quarter turn, a
two-node draw list and parent+child composition, every pixel and matrix
entry on paper), the byte decode against `m_pc01_00_00`, `m_ph01_00_00`,
`staffroll_b` and `batt_stage01`, and a real render of `pl_00_00` under a
2-unit node pose override (21239 pixels covered, 10637 moved). Measured
on this tree: `ctest` **14/14** and `test_g3d` **133 checks, 0
failures**, in both the LP64 and the ILP32 configuration.

### Meeting the 2D compositor

The render target is an RGBA buffer in exactly the byte order and at
exactly the geometry `g2d_frame_compose` writes, so it can stand as BG0's
pixels the way the 3D layer does on hardware. `g3d_compose_over()` puts
it *behind* an ordinary `G2dFrameDesc`: every pixel where the 2D frame
would show its backdrop shows the 3D frame instead, which is the
arrangement a DS game uses when BG0 carries the 3D output at the lowest
priority, and is the overworld's.

**The general hook exists, as `G2dLayer3d`.** The 3D output can instead be
a layer inside `g2d_frame_stack`, at its own priority and hardware layer
number, so it takes its place in the same per-pixel order as the BGs and
OBJs: a 2D BG can sit in front of or behind it, a window can hide it, and
the colour effects blend with a 3D pixel (the topmost-pixel and
directly-beneath rules apply to it like any layer). `g2d` stays independent
of `g3d`: the descriptor is a plain RGBA buffer, and `g3d_compose_over` is
kept for the common case it already covered. `tests/test_compose3d.c`
checks the stack, priority, window and blend cases synthetically (18
checks).

### The hardware interface (`engine/g3d/hw3d.c`)

Once the game's own code runs, the 3D picture is what it writes into the
3D hardware, and `engine/g3d/hw3d.c` is that hardware, the way
`engine/g2d/hwcompose.c` is the 2D engine's. It takes packed command words
at the geometry FIFO (`0x04000400`, and its mirrors to `0x0400043F`) and
single parameters at the direct command ports (`0x04000440`-`0x040005FF`),
frames each command and runs it through **the same `g3d_gx_run`** the
NSBMD path uses, keeps `GXSTAT` (matrix stack levels, the projection stack
bit, the stack error, FIFO empty, the box test), `RAM_COUNT`, the position
and vector test results and the clip and vector matrices current in the
register file, reads `DISP3DCNT`, the clear colour and depth, the alpha
test reference and the toon table at `SWAP_BUFFERS`, decodes textures out
of whatever VRAM banks `VRAMCNT` gives the texture image and palette slots,
and renders the frame through the existing rasteriser into the RGBA buffer
`g2d_hw_render_dsmem` takes as the 3D layer (`g3d_hw_frame()`).

**How a store reaches the FIFO.** The register file is plain memory, so ten
stores to `0x04000400` leave one value. The rewrite does give one
`bw_ds_ptr` call per access, so `sdk/dsmem_ext.h` grew *access hooks*: a
function called on every translation of an address in a range (and at every
`bw_io_sync`). The 3D window's hook resolves the previous access (the word
stored there is a command word or parameter) before handing out the next
pointer. The hand-written ARM copy loops that burst into the FIFO
(`GX_SendFifo48B/64B`, `GXi_NopClearFifo128_`, `MIi_CpuSend32`,
`MI_Copy36B/64B`) and the DMA senders (`MI_SendGXCommandAsync`,
`...AsyncFast`, their two TWL NDMA forms) are shims onto it in
`sdk/g3_fifo.c` and `sdk/mi_gx.c`. The file header of `hw3d.c` lists
exactly which of the corpus's write paths are captured and which are not
(a pointer computed once and stored through repeatedly; port code writing
through `bw_io_write32`; `sdk/mi.c`'s copies aimed at the FIFO; 8/16-bit
port stores), and the timing model (an infinitely fast engine: the FIFO
always reads empty, the GXFIFO interrupt is never raised, the swap renders
immediately).

**What is checked.** `tests/test_g3dhw.c` builds one display list (every
vertex encoding, all four primitive kinds, push/pop, a translucent strip,
a PAL16 and an A3I5 texture out of VRAM with a palette base and a flip),
renders it with the interpreter and the rasteriser directly, and requires
the hardware path's frame **pixel-identical**: packed into the FIFO one
translated store per word, through the direct ports, through
`MIi_CpuSend32` and through `MI_SendGXCommandAsync`, and with texturing
off. Beside that, the SDK's own traffic shapes (an opcode store then
`GX_SendFifo64B` or `MI_Copy36B` to a fresh `&FIFO`, an integer FIFO
address, `GXi_NopClearFifo128_` completing a half-sent command, the clip
and vector matrices read back through `MI_Copy64B`/`MI_Copy36B` from raw
result addresses), every `GXSTAT` field the SDK polls including the
`G3X_InitMtxStack` sequence and the `GXSTAT |= 0x8000` acknowledgement,
`POS_TEST`, `VEC_TEST`, `BOX_TEST`, the viewport, toon and highlight, the
alpha test, blending off, an unterminated vertex list, and the frame
composed by `hwcompose` as BG0. **79 checks, 0 failures**, both pointer
widths. Two interpreter fixes came out of it: opcode `0x2B` (`PLTT_BASE`)
was missing from the parameter-count table, so a list using it stopped
there, and `MTX_SCALE` in position+vector mode now scales the position
matrix only, as the hardware does; `test_g3d`'s oracle numbers are
unchanged by both.

Not honoured, and reported per frame by `g3d_hw_unsupported()`: fog, edge
marking, anti-aliasing, the rear-plane bitmap, the w-buffer, shadow
polygons, and lighting (as in the interpreter, a vertex keeps its last
`COLOR`). The hardware's polygon and vertex RAM limits are not imposed.

### What is not decoded, and is the next step

- **The node and bone table, almost closed.** The node dictionary's
  record layout, its **translation**, its **full rotation**, its **scale**,
  the **parent/child tree** and the **shape -> node binding** are decoded
  and cross-checked (see "The node dictionary" above and
  `docs/g3d-nodes.md`): 21/21 full rotations cross-product exact, 534/537
  exact reciprocal scales, 670/670 in-range acyclic parents, 0 bad shape
  bindings. `g3d_render_model` applies each shape's node and ancestor
  transforms. **The packed rotation is provisional**: only two of its
  components are stored (a verified unit pair) and the third-row
  completion is assumed, with no packed/full twin in the corpus to fix it
  (0 across 740 models). Also uninterpreted: the second flags byte, the
  flags' bits 4-7, and the header halfword's meaning for a packed
  rotation. The packed completion is the one transform piece not on byte
  evidence; the paired BCA0 animation or the ARM9 matrix builder would
  settle it.
- **Lighting.** The four light slots, the diffuse/ambient and
  specular/emission material registers and the shininess table are
  consumed with the right parameter counts so the stream stays aligned,
  and are not interpreted. Nothing has shown that a map model enables
  them, and inventing lighting would be a guess.
- **The texture matrix.** `TEXIMAGE_PARAM` bits 30-31 select a
  coordinate transform source; no map model in the corpus was found to
  need one and `map_shapes.py` records the same finding, so a texture
  coordinate arrives at the rasteriser exactly as its command carried it.
  If a model ever does need one it shows up as a texcoord mismatch
  against the oracle rather than as a silently wrong picture.
- **A pixel oracle.** The rasteriser's honest exit condition is a real DS
  frame to compare against. Until there is one, the synthetic cases are
  the only thing under this engine that is checked rather than believed.
- **The per-shape display list preamble.** Each shape's declared range
  opens with a short (0 to 20 byte) non-command preamble neither project
  has identified, so the stream's start is located by the same
  self-consistency scan aletheia uses. Both sides make the same
  assumption, so the oracle cannot see it; cracking it would remove a
  heuristic from the middle of the loader.

## The audio engine, and the synth oracle it is tested against

`engine/audio/` is the second host-side reimplementation of hardware the DS
had and a PC does not: the ARM7 sound core. It is pure C99 with no SDL, no
third-party code and nothing outside the standard library (`math.h` for
`pow`, `fmod`, `ceil`, `round` and `fabs`, and nothing else). The platform
layer's audio callback calls into it; `tests/test_audio.c` drives it
headless.

| file | what it does |
| --- | --- |
| `engine/audio/audio.h` | the whole API, and the provenance of every rule it implements |
| `engine/audio/sdat.c` | the SDAT container: SYMB, INFO, FAT and FILE, and the SEQ / BANK / WAVARC / PLAYER records |
| `engine/audio/sbnk.c` | SBNK instrument tables: types 0, 1, 2, 3, 16 and 17, and region selection |
| `engine/audio/swav.c` | SWAR archives and SWAV waves: PCM8, PCM16 and IMA-ADPCM, and the loop point |
| `engine/audio/sseq.c` | the SSEQ opcode decoder and the per-track bytecode interpreter |
| `engine/audio/mixer.c` | one voice: the resample, the ADSR, the pan, and the two PSG generators |
| `engine/audio/player.c` | SEQ to BANK to WAVARC resolution, the tempo map, the render and the streaming player |

**The formats.** The SEQ INFO record's bytes +4:+6 are one little-endian
u16 holding a **direct** BANK-table index, not two u8 fields and not a
PLAYER indirection; aletheia's asset lane settled that against the data
(940 of 940 populated SEQ records resolve to a real BANK record that way,
and the resolved BANK's name matches the SEQ's own in every case sampled)
and `bw_sdat_seq_record` is that reading. A BANK record carries a file id
and four wave-archive slots; a region's `wave_arc` is a *local* 0-3 index
into those, which gives the global WAVARC index, whose INFO record is a
bare u32 file id for the SWAR archive, in which `wave_id` selects the
SWAV. The SBNK region record is **10 bytes**, `wave_id` first, and a type
16 or 17 region entry is a u16 type prefix in front of it, so 12 bytes per
region: reading it 12 bytes wide is what used to make every region past
the first of a drum kit decode as garbage. Types 2 and 3 are PSG and carry
no wave at all; type 2's leading u16 is a duty selector.

**The sequencer** is a real interpreter, not a linear disassembly. Each
track runs its own bytecode with its own tick, program, volume, expression,
pan and transpose; a note's duration gates the next event as well as
sounding the note, because this format has no note-off; a forward `Jump` is
taken unconditionally, a backward one (this format's loop idiom) once, so a
looping track renders as its body twice and then ends; `Call` and `Return`
use a real return stack. The three opcodes `0xA0` (Random), `0xA1`
(GetVariable) and `0xA2` (If) are genuinely undocumented in this project:
their argument shapes were never re-derived, and they are skipped by the
commonly cited 5, 2 and 1 byte widths. That is a labelled approximation
rather than a decode, it is the same one the oracle makes, and any *other*
unrecognised opcode ends that track's render where it stands.

**The mixer** is 16 channels at 32768 Hz, which is the DS mixer's own
output rate and the rate the host device is opened at, so nothing resamples
twice. A voice resamples its source by the semitone ratio
`2^((played - base) / 12) * native_rate / output_rate` with linear
interpolation, wraps into `[loop_start, total_len)` past the loop point,
plays a one-shot out to its own natural length whatever the note's gate
says (which is what makes a cry, whose gate is zero ticks, sound at all),
shapes it with a linear ADSR, pans it linearly about 64 and clips to 16
bits. PSG square is a bipolar rectangle with the hardware's eight duty
settings; PSG noise is the 15-bit Galois LFSR clocked at the *channel timer*
rate the SDK's SND timer formula derives from the note's frequency
(`timer = -(clock / frequency)`, clock = 33513982/2 Hz) and sample-and-held
to the output rate, because an LFSR bit is a held digital level between
clocks rather than something to interpolate through.

**A voice is chunked, and that is load bearing.** All the state a note
carries (the source position, the square's phase, the noise LFSR and its
cursor, the sample counter) lives in the voice, so rendering a note in
pieces is bit-identical to rendering it in one call. That is what lets the
offline render and the real-time callback be the *same* mixer rather than
two implementations that drift; `tests/test_audio.c` checks it directly, on
a PCM voice in 37-sample chunks and a PSG voice in 101-sample chunks.

**The oracle, and why the comparison is exact.** `tests/test_audio.c` does
not compare against hand-written expected values. `tools/audio_oracle.py`
imports aletheia's `tools/assets/sseqsynth.py` (out of the sibling
checkout; nothing is copied here and aletheia is not modified), renders the
same sequences in Python and writes the `.wav` that tool itself produces
plus a note dump, into the build directory. The C engine renders the same
sequences and the test checks two things:

- **the note event list**, diffed line for line. A difference there is a
  *sequencer* difference: a mis-decoded opcode, a jump taken a different
  number of times, a tempo segment off by a tick, a region resolved to the
  wrong instrument. Every float in the dump is written as its exact
  IEEE-754 bit pattern, so the diff means "the same double" rather than
  "the same 17 digits", which two printf implementations need not agree on.
- **the sample buffers**, sample for sample, with the peak, the RMS and the
  largest difference reported per sequence.

The tolerance is **zero**: the two renders are required to be identical,
not merely close. That is reachable, and it is reachable on purpose.
sseqsynth.py is a Python program and Python floats are IEEE-754 binary64,
so `engine/audio` computes the same quantities in the same order in
`double` and gets the same bits. Three things had to be got right for that
and each is written down where it lives:

- **Python's `round()` is round-half-to-even**, not C `round()`'s
  half-away-from-zero, and the oracle uses it for the note start sample,
  the note length, the timer period and the final quantisation. Getting it
  wrong shifts whole notes by a sample. `bw_audio_py_round` is CPython's
  own `float.__round__`.
- **the order of the mix accumulation is observable**, because floating
  point addition is not associative. The offline render sums the notes in
  the oracle's order, which is a Python dict's insertion order over the
  tracks (the `OpenTrack` order with track 0 appended last) and then note
  order within a track, each note's whole span at once.
- **x87 is not binary64.** On 32-bit x86 the default floating point unit
  evaluates `double` expressions in 80-bit registers and rounds to 64 bits
  only on a spill, which made every ILP32 render differ from the oracle's
  in the last ulp of a gain or a note-off time. `CMakeLists.txt` compiles
  the six audio sources with `-msse2 -mfpmath=sse` on such a target. That
  is the whole of the difference; the other 32-bit results were already
  integer arithmetic and never cared.

Nothing about the engine *requires* double. `BwAudioVoice` is the whole of
the per-note arithmetic and is the piece a later fixed-point pass would
replace, with this test standing as the reference it then has to stay near
rather than equal.

**Measured**, three sequences over the same 10-second window, checked by
ctest:

| SEQ | name | what it covers | notes | render | difference |
| --- | --- | --- | --- | --- | --- |
| 1062 | `SEQ_BGM_GYM` | PCM waves out of a real SWAR | 296 events, 22 rendered, 9 tracks, 116 BPM | peak 14960, RMS 3090.378 | **0 of 655360 samples** |
| 1146 | `SEQ_BGM_BATTLEPINCH` | PSG square, with PCM alongside | 173 events, 35 rendered, 6 tracks, 180 BPM | peak 6052, RMS 2221.014 | **0 of 655360 samples** |
| 1379 | `SEQ_SE_KOUKA_H` | PSG noise | 17 events, 12 rendered, 2 tracks, 89 BPM | peak 5751, RMS 1896.658 | **0 of 275032 samples** |

All three note event lists are identical to the oracle's, and the peak and
RMS agree exactly because the buffers do. The busiest of the three needs
**5 voices at once**, so the DS's 16 channels are not the binding
constraint on any of them; the test reports that number and the streaming
player counts the note-ons that found no free channel, which is zero here.

**What is reproduced rather than improved, deliberately.** sseqsynth.py
documents its own approximations, and the engine makes the same ones,
because the point of an oracle comparison is that two independent
implementations of the *same* model agree. The ADSR is linear with the
byte used directly as a fraction of each stage's maximum time (the asset
lane picked that direction empirically, not from a spec); volume,
expression and velocity multiply linearly rather than in dB; pan is a
linear crossfade rather than equal power; portamento, modulation, sweep and
the pitch wheel are no-ops. One of them looks like a bug and is not:
**the noise LFSR's all-ones reset value is a fixed point of its own
update** (right-shifting all ones always shifts out a 1, so the tap is
always XORed back in), so every noise note is a constant level shaped by
the envelope rather than noise. aletheia's tool found that, proved it (its
clock-rate fix changed the rendered file not at all, byte for byte) and
deliberately left it. The engine reproduces it exactly, so that whoever
fixes the seed fixes it in the tool first and this test then *measures* the
change instead of hiding it.

## The frame skeleton

`engine/frame/` is the per-frame shape the game will run on, and it contains
no SDL: it is the DS's frame *convention*, not the host's window, so a
headless test drives it exactly as the platform layer does.

One iteration is **step, then vblank, then render**, and that order is
attested by the hardware rather than chosen. The game's main loop runs
during the visible period and builds the next picture into shadow copies of
OAM and of the BG registers; the vblank interrupt is where those shadows may
be committed, because the display is scanning the real ones out at any other
time. A port that renders before it commits shows the previous frame's OAM,
which is a one-frame lag on everything that moves. The frame and tick
counters advance at the vblank too, because on hardware that interrupt is
what the OS counts: a step function therefore sees the number of the frame it
is building, and a render function the one it is about to show.

The game's own scheduler sits on top of this rather than inside it, and it is
in the build now: `src/matched/sub_020310C4.c` (tcbl.c's runner) **is** the
step function `demo/eidos_view` installs. See "The game's scheduler" below.

**The OS tick is not 64 Hz.** `src/matched/func_02021444.c` (palanm.c) stamps
its module state block with `OS_GetTick()`'s full `u64`, stored as one
4-aligned `long long`, which fixes both the width and the unit. The SDK tick
is the system clock divided by 64, **523655.97 Hz**; the 64 is the timer
prescaler's divisor, not a frequency. One DS frame is 560190 / 64 = 8752
ticks and 62/64 of another, so `engine/frame/frame.c` advances by 8752 and
carries the remainder, adding a whole tick every 32 frames. Dropping it would
run the game's clock half a second slow per hour. `sdk/os_tick.c` is
`OS_GetTick` and `OS_GetTickLo` over that counter, so a tick difference the
game measures is a whole number of frames' worth whatever the host was doing,
and a headless run that steps frames without sleeping advances the game's
clock exactly as a paced one does.

`tests/test_frame.c` is synthetic: the callback order, that a value the step
function writes reaches the renderer only through the vblank commit, that 32
frames are exactly 280095 ticks, and the tick/millisecond conversions.

**Under the game's own boot** (`demo/eidos_boot.c`, see "Running the game's
boot") the same loop is the driver. The step is one iteration of NitroMain's
loop, ending where the original waits for vblank, and the vblank hook does
the hardware's half of the interrupt only: it sets DISPSTAT's vblank flag
and raises IF bit 0 through the OS layer (`bw_os_raise_irq`). The OS layer
owns IF; it takes the request at its next safe point, runs the handler the
game installed in `OS_IRQTable` and wakes the threads waiting in
`OS_WaitIrq`, as `OS_IrqHandler` does on the DS (docs/boot.md, "Threads,
interrupts and the frame").

## The game's scheduler, and the first matched code inside the frame

`aletheia`'s tcbl.c is the game's task-callback list: a manager owning a
fixed pool of nodes, a caller registering a callback with a priority and
getting a node back, and one pass of a runner walking the live nodes in
priority order and calling each callback with the node and its work area.
That pass is what a DS game does once per frame, and it is now what
`engine/frame/` calls as the step.

**What runs through matched code now.** Everything in this list is
`src/matched/`, compiled unchanged for the host and actually executed by
`ctest`:

- **The heap.** `sub_02030734`, the allocation entry point with 1163 callers
  in the image, over `sub_02030098`, the only function in the game that takes
  memory from a heap: the 0x7fff heap-id mask and its bit-15 direction flag,
  the 0x1c-byte block header with the `0x194E` signature, the per-heap
  live-allocation counter, and the three result codes. Around it
  `sub_02030880` stamps the allocation-site provenance pair into the header,
  `sub_020308B8` reads it back and validates it, `sub_02030A00` filters on
  the watched-heap id and `sub_02030984` audits, `sub_02030428` /
  `sub_0203046C` / `sub_020304B8` / `sub_020304C0` are the lookups, and
  `unit_0202FBAC`'s four accessors resolve a heap id to its record.
- **The release side of the heap**, new since wave 26. `sub_020307B0`, 904
  branch references image-wide, is the counterpart of the allocation entry
  point: `sub_02030A2C` runs the free-side watch filter, `sub_020309B8` the
  free-side audit and `sub_02030344` the checked heap-id lookup under it,
  and then the raw free walks back over the 0x1c-byte header. The port used
  to reimplement the whole of that as one function.
- **The expanded heap's front door.** `func_02060CA0`: round the request up
  to a word, and take the block off the head of the arena for a non-negative
  alignment or off its tail for a negative one, which is how the raw
  allocator spells bit 15 of the public heap id. Its two callees are still
  the port's; see "The heap environment" below.
- **The task list's construction.** `sub_0203101C` allocates the 0x38-byte
  manager and the node pool, stores the count, the work size and the home
  heap, and self-links the sentinel; `sub_02030FC8` walks every row of the
  pool into the free list.
- **The runner.** `sub_020310C4`, one pass: clear the flag, call the node's
  callback with the node and its work area, and either advance or unlink,
  with the walk cursor published in the manager so a callback can destroy a
  node from inside another one.
- **The rest of tcbl.c, under ILP32.** Wave 26 matched the five functions the
  port used to supply -- `sub_020311B8` (register a callback in ascending key
  order), `sub_02031194` (destroy one node, deferring to the runner when it
  is the node being run), `sub_0203115C` (destroy every node), `sub_02030F50`
  (reset a node) and `sub_02030F70` (unlink one and release its work block).
  In the reference configuration **the whole of tcbl.c is aletheia's own C
  and nothing of the task-callback list is a reimplementation.** On an LP64
  host those five stay out, for a reason that is about the family's own
  declarations rather than about what is decompiled; see "Why nine matched
  files are out of the LP64 build" below.

`tests/test_tcbl.c` drives all of it with no SDL, no game data and no
oracle: **49 checks**, including that the constructor's two allocations show
up in the matched heap's own live-block count, that the pool is exactly as
deep as it was asked for (which is only true if the matched free-list build
reached every row), that the provenance the matched allocator stamped reads
back as `tcbl.c` through the matched validator, that a pass over an empty
list is a no-op, that three callbacks fire in priority order with their own
work areas, that a node removed from outside a pass drops out of the order
and can be re-registered at a new priority, that a node which destroys
itself from inside its own callback still lets the rest of the pass finish
and is gone from the next one, and that the heap balances back to zero.

**The ordering assertion no longer rests on a reimplementation.** "Three
callbacks fire in priority order" used to be half a statement about matched
code, the runner's forward walk, and half about the port's transcription of
0x020311B8's listing. Under ILP32 both halves are matched now: the
ascending-key insert is `src/matched/sub_020311B8.c` and the forward walk is
`src/matched/sub_020310C4.c`. Nothing in the test had to change for that,
which was the point of writing it against the behaviour rather than against
the stand-in.

`demo/eidos_view` then puts one task on such a list and makes the frame
loop's step `sub_020310C4`. The task advances the Pikachu pose along its
NANR timeline and writes the scroll, the sprite position and the animation
time into the pending register block; the vblank commits; the renderer
recomposes the pose at the committed time. `tests/test_view.c` takes a
screenshot after frame 1 and another after frame 12 and requires the bottom
screen to have moved by at least 200 pixels while the top screen, which has
no task, is **pixel-identical**. Measured: top 0, bottom 1223.

That is the demo's list. Under the game's own boot the step is NitroMain's
loop instead (previous section), and the lists the runner walks are the ones
the game's code builds.

### Why nine matched files are out of the LP64 build

This is the first place the port has had to leave an EXACT matched source
out, and the reason is a property of the game's data rather than of any
file: **the task-callback node and its manager are the first game-owned
structs in the port that store pointers.**

It is also the question that settled the port's pointer-width policy, and
the answer is that these four are out of the *LP64* build and in the ILP32
one: `docs/pointer-width.md` has the measurement and the decision, and
"Pointer width, and the reference configuration" below has the short form.
Configured with `cmake/ilp32-toolchain.cmake` all four are compiled as
written, straight out of aletheia, and every test still passes.

Portability rule 5 below says no pointer may be stored in a struct the
game's layout owns, and until now the port could honour it, because the one
such struct (`ArcFile`) is one the port itself fills in and it stores an
index. A tcbl node cannot: its `prev`, `next`, `owner` and `func` are
pointers the *game* writes. On the DS a pointer is four bytes and the node
header is 0x18 with the manager at 0x38; on an LP64 host the same
declarations produce a 40-byte node header and an 88-byte manager, and every
field after the first pointer moves.

That is harmless for a matched file that declares the whole struct, because
its own compiler lays it out consistently and every other file in the family
agrees. It is fatal for a matched file that reaches one field through
explicit padding, and four of them do:

| file | what it does | why it cannot run here |
| --- | --- | --- |
| `sub_02030EF0.c` | the pool row address | returns an address in a `u32`, and reads the table pointer at DS offset 0x14 (24 here) |
| `sub_02030F34.c` | free-list push | reaches the free-list head through `char pad[0x34]`; it is at 80 here |
| `func_02030F04.c` | free-list pop | the same `char pad[0x34]` |
| `sub_02030FB4.c` | the work-area accessor | reads the owned-work pointer at +0x10 (28 here) and returns node + 0x18 (40 here) |

Each one is byte-correct for the DS and nothing about it is wrong. They stay
in aletheia untouched, and on an LP64 host a `stubs/reimpl_` file stands in,
naming the matched source it replaces and transcribing its body against the
struct rather than against the numbers. `sdk/tcbl.h` carries the host layout
with the DS offsets beside it.

**Wave 26 added five more to that list, and for a different reason.** The two
findings the port had recorded as open questions -- the node's `+0x00` and
its `+0x10` are declared `u32` in the tcbl files that were in the build and
both of them hold pointers -- were answered by the five new EXACT sources,
and the answer is that they are pointers: `src/matched/sub_02030F50.c`,
`sub_02030F70.c` and `sub_020311B8.c` declare the node as six members with
`struct Tcbl *owner` at `+0x00` and `void *work` at `+0x10`.

That is a better declaration of the same object, and **on the DS and on any
ILP32 target it *is* the same object**: 0x18 for the node and 0x38 for the
manager either way. On an LP64 host it is not. The pointer form lays the node
out at 48 bytes and the manager at 96; the `u32` form, which
`src/matched/sub_0203101C.c` and `src/matched/sub_020310C4.c` still use,
gives 40 and 88. The family has stopped agreeing with itself, and something
has to pick one reading:

- the constructor allocates `sizeof(struct Tcbl)` as *its own* compiler sees
  it, so whatever it says the manager is, the manager is;
- the runner walks the node with *its own* offsets on every frame.

Both are in the build unconditionally, so the `u32` form is what an LP64
build has to hold, and the five pointer-form sources join the four
offset-reaching accessors outside it. **This is not a defect in either set of
declarations**: each is byte-correct for the DS, which is the only target
aletheia has to satisfy. It is the pointer-width policy earning its keep a
second time, and the sharpest argument yet for ILP32 being the reference
configuration -- there the port does not choose between two matched readings
of one struct, it configures a target on which there is only one.

Which of the two lists goes into the library is `EIDOS_TCBL_MATCHED`, whose
default CMake decides from `CMAKE_SIZEOF_VOID_P`: on a 32-bit target the nine
matched sources are compiled as written and the stand-ins are left out, and
the node and the manager come out at exactly `0x18` and `0x38`, the DS's own
sizes.

One consequence is visible in behaviour rather than in layout. The node's
`+0x10` is the work block a node owns when its request does not fit inside
the node, and on an LP64 host there is no padding to borrow the way there is
at `+0x00`. So the port refuses an oversize work request there rather than
storing a truncated pointer, and every node on that host carries its work
area inside itself; under ILP32 the matched registrar allocates the block
from the list's home heap and the matched unlink releases it, exactly as the
DS does. `sdk/tcbl.c` keeps the refusal in both configurations so that the
port has one behaviour to test rather than two.

And one arithmetic adjustment, which is the port's own and is confined to one
line: `sub_0203101C` sizes the pool as `(((workSize + 3) & ~3) + 0x18) *
count` with the DS node header written out as a literal. Rather than keep the
constructor out of the build over one constant, `bw_tcbl_create` adds the
difference between the two headers to the caller's work size before handing
it over, so the rows the matched arithmetic produces come out exactly `40 +
work` bytes apart and the last one ends exactly at the end of the table.

The table in this section lists the four accessors. The five wave-26 files
are `sub_02030F50.c`, `sub_02030F70.c`, `sub_020311B8.c`, `sub_02031194.c`
and `sub_0203115C.c`, and none of them reaches a field through a byte offset
at all: every one declares the whole struct. They are out of the LP64 build
purely because the struct they declare is not the struct the constructor
allocates there.

### The heap environment, and what is under the allocator

`g_EnvModule` at 0x02146954 is the heap manager, and the matched code reads
it constantly: the id-table length at +8 bounds the id, the byte table at +4
maps a public heap id to an entry index, the entry array at +0 holds each
heap's handle and its live-allocation count, and the halfword at +0xe is the
result code every one of them writes.

**In the reference configuration the whole heap is the game's own, down to
the arena.** aletheia has since matched the heap environment constructor
(`sub_0202FC48`) and everything under it, so with `EIDOS_HEAP_MATCHED` on
(the default wherever pointers are four bytes) the port builds the manager
the way the game's boot does and allocation runs matched to the bottom:

- **the constructor**, `src/matched/sub_0202FC48.c`: the entry table (the
  caller's heaps plus 0x18 spares, 16 bytes each), the id table (clamped up
  to the entry count, every byte 0xff) and one region per heap, all taken off
  arena 0, each region turned into a NitroSystem expanded heap by
  `func_02060C5C` / `func_02060814` / `func_020606D0`, which also puts it on
  the root heap list;
- **allocation**, `func_02060CA0` into `func_02060A08` (first fit from the
  head of the free list) or `func_02060ABC` (from the tail, for a heap id
  with bit 15 set), and the carve that splits the chosen block, leaving any
  remainder of 0x14 bytes or more as a new free block;
- **release**, `func_02030164` (the raw free: signature 0x194E to 0x194D,
  the live counter down) into the expanded heap's free, which returns the
  region with `func_02060B6C`, merging it with a free neighbour on either
  side;
- **child heaps**, `sub_0202FDC4` and `func_0202FED8`: a region allocated
  from a parent heap and bound to the first unused spare entry, which is
  what the game's boot does with `func_0203064C(1, 7, 0x25000)`
  (`src/matched/sub_0200545C.c`). `bw_heap_create_child` is that call;
- **the queries** the audits use: free bytes (`func_02060E6C`), the largest
  allocatable block (`func_02060E94`), a block's size (`func_02060F90`).

What the port supplies is the arena and the heap sizes. `sdk/os_arena.c` is
`OS_AllocFromArenaLo` (`sub_02086788`) over one region the port allocates
itself, sized by `bw_heap_init_sizes` to exactly what the constructor will
take. The sizes are the port's because the game's own table (`d_0209D8A0`
on TWL, `d_0209D8B0` on NTR, two heaps and 0xAD ids) is ARM9 rodata in the
player's image; the boot path is where that table gets read.

**Two findings about the matched sources, both handled in the port layer.**

- *Two names, one object.* `func_020606A8.c` names the root heap list
  `g_02146B64` where `func_020606D0.c` says `sRootList`, and
  `func_0202FED8.c` names the manager `g_02146954` where everything else says
  `g_EnvModule`. The DS links by address and gets one object; a host links
  by name and would get two. CMake compiles each of those two files with its
  spelling `#define`d to the other's, so the files are untouched and the
  object is shared.
- *A frame-layout dependence.* `func_02060888.c` (the carve) and
  `func_02060E30.c` (the free) declare `int end; int start;`, let
  `func_02060764(&start, blk)` write a two-word region, and read the second
  word as `end` (and pass `&end - 1` as the region). That is right exactly
  when the compiler puts `end` in the word above `start`, which mwccarm does,
  and is out of bounds in C. Under GCC the carve read a word it never wrote
  and gave the first split block a garbage size; `test_heap` caught it on
  the first allocation. `stubs/reimpl_02060888.c` and
  `stubs/reimpl_02060E30.c` are the two bodies with the pair declared as an
  array and nothing else changed. They are the only two files in the corpus
  with the `&local - 1` shape. Declaring the pair as an array in aletheia
  would make both portable, if match.py agrees that the bytes survive; that
  is aletheia's call.

**On an LP64 host** none of this can run (an expanded-heap block header is
0x10 bytes with two pointers in it, and every file in the chain does its
address arithmetic in `int`), so `EIDOS_HEAP_MATCHED` is off there:
`sdk/game_globals.c` fills in the fields the upper layers read with a token
per heap, and `stubs/reimpl_02060A08.c`, `stubs/reimpl_02060ABC.c` and
`stubs/reimpl_02030164.c` take blocks from the host allocator. The manager's
contract (ids, result codes, the live counter, alignment) is the same, and
`test_heap` checks it in both configurations; the addresses are not, and the
arena checks run only under ILP32.

**`tests/test_heap.c`** needs no game data. Every expected number is worked
out on paper from the rules above and each check names its source: the
arena used to the byte (27 entries, a 28-byte id table and three regions),
the heap heads at the region starts with their 'EXPH' signature and a single
free block of region - 0x38 - 0x10 bytes, a head allocation placed right
after the first header and a tail one ending at the heap's end with its tail
bit set, both merges on free, first fit into a hole, the 0x14-byte
remainder rule in both directions, the exact fit that empties the free list
and the word more that fails with result 3, a region under 0x4c bytes that
the constructor turns into "no heap", child heaps from the head and the tail
of a parent (on the parent's child list, not the root list) and all four of
their failure codes including the 0x18-spare limit, and fifty interleaved
blocks over three heaps that never overlap and all come back. **206 checks
under ILP32, 89 under LP64, 0 failures**, and clean under AddressSanitizer
and UBSan in both.

The out-of-memory report (`sub_02030B50`) stays a stub: it is EXACT in
aletheia, but its closure reaches the game's debug print (`func_0209128C`,
open), and it runs one call before `OS_Terminate`.

## The platform layer

`engine/platform/` is the only part of eidos that includes SDL2 and the only
optional part of the build (`-DEIDOS_SDL=OFF`, or simply no SDL2 present:
the engine library and every other test still build and pass).

**The window** is an integer multiple of the DS layout: two 256x192 screens
stacked vertically, which is the hardware's own arrangement, or side by side
with `--side-by-side`, with a configurable gap. Integer scales only. The 2D
engine places every pixel exactly, against a Python oracle, and a fractional
scale would resample that; the render hint is nearest-neighbour for the same
reason.

**The keyboard** is a table of `SDL_Scancode` values, which are physical key
positions rather than characters, so the defaults land on the same keys on a
QWERTZ or AZERTY keyboard. A caller may pass its own table. The defaults:

| DS | key | DS | key |
| --- | --- | --- | --- |
| d-pad | the arrow keys | START | Return |
| A | X | B | Z |
| X | S | Y | A |
| L | Q | R | E |
| SELECT | Right Shift, Backspace | | |

Escape quits and F12 writes a screenshot; those two are the platform layer's
own and are not in the table. The button word keeps the DS keypad's bit order
(`KEYINPUT`, the SDK's `PAD_*`) but is active high, because every caller asks
"is A down" and the inversion belongs at the one place that reads the
register. No key may be bound to two buttons: the word is rebuilt from the
whole table on every pump, which is also why a key held across a focus change
cannot leave a bit stuck down.

**The pacer runs at the DS's rate, not at 60 Hz.** A frame is 355 dots x 263
lines x 6 clocks at 33513982 Hz = 560190 clocks, i.e. 59.8261 Hz and a period
of 16715113 ns plus 3790034/33513982 of a nanosecond. The pacer keeps that
exact rational period and accumulates the remainder, so the n-th frame is due
at `round(n * 560190 * 10^9 / 33513982)` ns rather than n times a rounded
constant: 16 ms would run 4.3 percent fast, and even 16666667 ns gains a
frame every six minutes. A caller that falls more than four frames behind (a
breakpoint, a suspended laptop) is resynchronised rather than made to run the
backlog, and the count of those resynchronisations is readable. The renderer
is created **without** `SDL_RENDERER_PRESENTVSYNC`: a 60.000 Hz or 144 Hz
panel would otherwise pull the game off the DS's rate.

**The audio device** is opened at 32768 Hz, stereo, signed 16-bit, which is
the DS mixer's own output rate, so the sequencer does not resample twice,
and the buffer is 1024 frames, 31 ms, under two DS frames. A machine with
no sound device prints a line and runs silent rather than refusing to
start. The callback itself is a plain function pointer over that buffer,
which is exactly what `bw_audio_player_mix` fills, so the whole of the
wiring between the platform layer and `engine/audio` is one thunk and one
`eidos_platform_set_audio` call. It runs on SDL's audio thread and never
allocates: the player prepared every note and decoded every wave it needs
when it was created, and mixing touches nothing but its own sixteen voices
and a block buffer on the stack.

**The frame upload** is two 256x192 RGBA buffers into two streaming textures
created as `SDL_PIXELFORMAT_ABGR8888`, which is the packed pixel `0xAABBGGRR`
and therefore the byte order R, G, B, A that `g2d_frame_compose` writes.

**The screenshot** (`SDL_SaveBMP`) is composed from the port's own
last-uploaded buffers rather than read back out of the renderer: a read-back
is driver-dependent and under the dummy video driver there is nothing to read
back at all, while the shadow copies are exactly what the engine produced. It
is written at 1x in the configured layout, as RGB24, so SDL writes a plain
24-bit BMP that a test can parse with no image library.

## eidos_view, the first visible output

```
build/eidos_view --data-root <extract/data> [--scale N] [--side-by-side]
                 [--gap N] [--species N] [--map N] [--frames N]
                 [--shot PATH] [--shot-first PATH] [--anim-speed N]
                 [--no-audio] [--music [SEQ]]
```

`--map N` replaces the top screen with the 3D layer: one `a/0/0/8` map,
building or dungeon model, decoded once into a geometry buffer in model
space and transformed and rasterised every frame by `engine/g3d`, with
its textures out of `a/0/1/4`. `--map 120` is `pl_00_00`, the interior
`tests/test_g3d.c` compares vertex for vertex against aletheia's OBJ, and
`--map 0` is `map24_23`, an outdoor terrain chunk. The camera is a
**second task on the same task-callback list**, at a higher priority than
the animation task so the matched runner reaches it first; it writes the
eye's yaw, pitch and distance into the pending register block and the
vblank commits them, exactly as the animation task writes the sprite's
position. The arrows orbit, X and Y (or L and R) move in and out, START
recentres. The angles are 16-bit binary angles into `engine/g2d`'s own
sine table, so the demo still needs no libm.

`--music` plays a sequence out of the player's own `wb_sound_data.sdat`
through `engine/audio`, looping; with no argument it plays SEQ 1062,
`SEQ_BGM_GYM`, which is one of the three the audio test checks sample for
sample against the Python synth. It is behind a flag because it is the
demo's only reason to open a 50 MB archive, and the title music replaces it
once the boot path names its SEQ id. On exit the demo reports how many
frames it mixed and how many note-ons found no free channel.

The top screen is `titledemo.narc` member 0, the 256x256 8bpp title screen
the asset lane identified, over the NCGR/NCLR pair `background.py`'s coverage
rule paired with it (members 1 and 2, the same triple `tests/test_bg.c`
checks pixel for pixel against the Python renderer). The bottom screen is one
composed rig pose out of `a/0/0/4`, species 25 by default. Both come from the
player's own `extract/`; nothing is bundled.

**The frame loop's step is game code.** The demo builds a task-callback list
through the matched constructor and registers one task on it; the step
function is `src/matched/sub_020310C4.c`, one pass over that list, and the
loop does not know what is on it. The task advances the pose along its NANR
timeline and writes the next frame's scroll, sprite position and animation
time into a pending register block; `view_vblank` commits it; `view_render`
reads only the committed copy and recomposes the pose at the committed time,
which is what OAM and the BG registers do on hardware. The sprite's bob comes
from the engine's own fixed-point sine table, so the demo needs no libm.

`tests/test_view.c` runs it headless. ctest sets `SDL_VIDEODRIVER=dummy` and
`SDL_AUDIODRIVER=dummy` on the test, so the window, the textures, the pacer
and `SDL_SaveBMP` all run on a machine with no display and no sound card;
the test then parses the BMP: 256 x 384 at 24 bits, the two screens stacked
at 1x, and neither half blank. The thresholds for "not blank" are
deliberately low (eight distinct colours and five hundred pixels off the
backdrop per half), because the bottom screen is one 48x52 sprite on a
backdrop. It then compares the screenshot taken after frame 1 with the one
after frame 12: the bottom screen must have moved by at least 200 pixels and
the top screen, which has no task, must be pixel-identical. Measured: top 0,
bottom 1223. What the picture *is* stays `test_bg`'s and `test_g2d`'s
business, against the Python renderers.

## Running the game's boot

`docs/boot.md` is the design for running the game's real start-up rather
than a demo's: what runs, the three layers every SDK name falls into, how
fixed DS addresses are rewritten at build time, the address space, and the
thread and interrupt model. `docs/title-closure.md` lists what still blocks
the title screen. The pieces in the tree:

- `sdk/dsmem.c`, the DS address space every matched fixed-address access
  goes through (`tools/dsaddr.py` rewrites each matched source's literal
  casts into `bw_ds_ptr` calls in a build-tree copy, `cmake/dsaddr.cmake`);
- the OS layer (`sdk/os_*.c`, `sdk/plat_thread.c`): threads on host threads
  under one run token, the interrupt controller, alarms and the tick;
- `sdk/pxi.c` and `sdk/arm7/`, the FIFO between the processors and the ARM7
  stand-in that answers it;
- `sdk/fs.c` and `sdk/overlay.c`, the ROM file system and overlays onto the
  extract;
- `bw_port_reset` (`sdk/port.h`), the one power-on: `bw_ds_reset`, then
  `bw_os_reset`, then `bw_pxi_reset`, then the joins (PXI interrupt requests
  go to the OS layer's latch, and every `OS_RestoreInterrupts` gives the
  ARM7 its turn). The boot and every test that runs game code on the whole
  machine call it rather than the three resets;
- `stubs/reimpl_02005124.c`, NitroMain, and `demo/eidos_boot.c`, its
  headless driver.

```sh
cmake -S . -B build -G Ninja -DALETHEIA_ROOT=../bw_decomp -DEIDOS_BOOT=ON
cmake --build build --target eidos_boot
./build/eidos_boot --data-root ../bw_decomp/extract/data --frames 600
```

With `EIDOS_BOOT` off (the default) `eidos_boot` builds without the game's
code, says so and exits 0. With it on, CMake computes NitroMain's matched
closure with `tools/closure.py` and links it; that link is the work in
progress, and `docs/boot.md` tracks what it still needs.

## Growing the source list

`CMakeLists.txt` carries a curated list of matched TUs. It is grown
mechanically, not by guessing:

```
python tools/closure.py --seed-func func_02018A7C
python tools/closure.py --seed-func func_02018A7C --cmake
python tools/closure.py --seed-func sub_0203101C
python tools/closure.py --list-closed
```

The `sub_0203101C` seed is the one the heap layer and the task-callback list
came in on, and `sub_02030734` (the allocation entry point) and
`sub_020311B8` (the registrar) are the other two the current source list is
grown from. Re-run after wave 26 they report:

- **`sub_0203101C`**: 20 matched translation units, five `sdk` names
  (`MIi_CpuClear32`, `OS_DisableInterrupts`, `OS_RestoreInterrupts`,
  `OS_Terminate`, `sub_02030524`), one `alias` (`sub_02060CA0`, defined as
  `func_02060CA0`), four `open` ones (`func_02060888`, `sub_02030B50`,
  `sub_02060E6C`, `sub_02060E94`) and three `data` ones (`g_EnvModule`,
  `g_020A7820`, `sub_0203101C_File`).
- **`sub_02030734`**: the same minus the three tcbl files.
- **`sub_020311B8`**: adds `func_02030F04`, a second `alias`
  (`sub_02030F04`) and the `data` name `sub_020311B8_File`, which is the
  same ARM9 word `0x020A7834` as `sub_0203101C_File` under the registrar's
  own spelling.
- **`sub_0203115C`** and **`sub_020307B0`**: the release closure, which is
  where `sub_02030344`, `sub_020309B8`, `sub_02030A2C` and the two open names
  `sub_02030164` and `sub_02060F90` come from.

`sub_02030F50` and `sub_02060CA0` have left the `open` lists entirely: both
are matched now.

**Two places the report and the build disagree, both on purpose.** Nine
matched units (`sub_02030EF0`, `sub_02030F34`, `func_02030F04`,
`sub_02030FB4`, `sub_02030F50`, `sub_02030F70`, `sub_020311B8`,
`sub_02031194`, `sub_0203115C`) are in the build only where pointers are four
bytes, see "Why nine matched files are out of the LP64 build". And
`func_02060A08` / `func_02060ABC` are matched, so the closure walks through
them and reports `func_02060888` as open; the port stops one level higher,
at `func_02060CA0`, and stands in for those two itself, because they walk an
arena the port has not got. The ILP32 configuration answers the rest of the
report exactly.

`tools/closure.py` also answers the pointer-width question over the whole
corpus rather than one closure:

```
python tools/closure.py --pointer-audit
```

counts the matched sources that hold an address in an integer, hardcode a
struct size or field offset, or cast between an integer and a pointer, and
prints the file list behind each count. `docs/pointer-width.md` is that
output plus the decision it supports.

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
ROM. The standard library is the only hard dependency; SDL2 is optional and
is needed only for `engine/platform/` and the `eidos_view` demo.

**SDL2 without an installer.** `tools/fetch_sdl.py` downloads the official
MinGW-w64 development package of SDL **2.32.10** from the libsdl-org GitHub
release, verifies it against the SHA-256 recorded in the script, and unpacks
it into `third_party/` (gitignored, so neither the archive nor the tree it
unpacks to is ever committed). Standard library only; a fresh clone needs
nothing but Python to get SDL2:

```
python tools/fetch_sdl.py            # download, verify, extract
python tools/fetch_sdl.py --check    # report what is already there
```

CMake looks in `third_party/` first, then for a system SDL2 CMake package,
then for pkg-config's `sdl2.pc`, and prints which it found. With none of the
three it says so and skips the platform layer and the demo: **every other
target still builds, and every test but `test_view` still runs.**
`-DEIDOS_SDL=OFF` forces that path, which is how the no-SDL build is checked.
The version and the hash are pinned in the script rather than resolved at run
time, because a recorded hash is the only part of a download a compromised
mirror cannot satisfy; upgrading is two edits and a re-run.

```
# in the aletheia checkout:
python tools/ndsrom.py extract "<your-rom>.nds" -o extract

# in this repository:
python tools/fetch_sdl.py
cmake -S . -B build -DALETHEIA_ROOT=../aletheia
cmake --build build
ctest --test-dir build --output-on-failure
```

**Without game data.** The fixtures and the four oracles are generated from
the extracted ROM at build time, so a tree with no `extract/` used to fail
its build at the first of them. Configure now checks whether the data root
holds a NitroFS tree and sets `EIDOS_DATA_TESTS` from the answer. With no
data the whole library, the demo and every test executable that does not
compile a generated header in still build, the seven synthetic tests run,
and the seven data-driven ones (`test_text`, `test_stats`, `test_g2d`,
`test_bg`, `test_g3d`, `test_audio`, `test_view`) are registered as
disabled, so ctest lists them as `Not Run (Disabled)` rather than dropping
them from the count. `-DEIDOS_DATA_TESTS=ON` forces the data path.

Configure also compiles and links a one-line SDL2 program before it trusts
an SDL2 it found. On a 64-bit Linux host configured with the ILP32
toolchain file, pkg-config reports the 64-bit SDL2, which does not build
under `-m32`; the probe turns that into "SDL2 not found for this target"
and the platform layer is skipped, instead of a compile error half way
through the build. libm is asked for the same way, by linking a call to
`pow` with `-lm` for the target being built, because `find_library` answers
for the host's default ABI and under `-m32` found no libm at all.

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
for it. It was the port's only LP32 build until
`cmake/ilp32-toolchain.cmake` arrived; the i686 configuration does the same
thing for pointer width and can also run what it builds, so `--check` is now
a second compiler's opinion rather than the only 32-bit evidence there is.
Current state: 69 objects compiled clean, `test_text`, `test_stats` and
`test_tcbl` linked.

### Checking every configuration at once

```
python tools/check.py                 # every configuration this machine can build
python tools/check.py --only gcc-ilp32 asan-ilp32
python tools/check.py --data-root ../aletheia/extract/data
```

configures, builds and tests each configuration in its own directory and
fails on a build error, a failed test **or a single compiler warning**:
GCC and Clang at both pointer widths, AddressSanitizer + UBSan at both, and
the no-SDL build. A configuration whose compiler or 32-bit runtime is not
installed is reported as skipped with the reason. On this tree, with no
game data (Linux, GCC 13.3, Clang 18.1): all seven ok, 0 warnings, 9 of 9
enabled tests passing in each, the data-driven ones disabled.

**Windows** is in the matrix too, cross-compiled with MinGW-w64
(`mingw-x64`, `mingw-x86`); with Wine installed the x64 tests run as real
`.exe` files through `CMAKE_CROSSCOMPILING_EMULATOR`, and on this machine
all ten pass there, including `test_extract`'s oracle comparison. That run
found that `eidos_extract` took its paths from `argv`, which Windows gives
in the ANSI code page rather than UTF-8, so a ROM under a folder named with
an accent would not open; it now reads the wide command line. It also found
that `cmake/ilp32-toolchain.cmake` preferred `i686-w64-mingw32-gcc` on
every host, so a Linux machine with MinGW installed got a Windows cross
compiler for its "32-bit" build; the search is now split by host OS.

Two portability fixes came out of the first Clang run: `sdk/os_alloc.c`
computed alignment by subtracting a null pointer, which is undefined and
now goes through `uintptr_t`; and `cmake/ilp32-toolchain.cmake` used to add
`-m32` only when it picked the compiler itself, so naming one
(`-DCMAKE_C_COMPILER=clang`) silently produced a 64-bit build. A named
compiler now gets `-m32` too, and a configure that asked for ILP32 and got
8-byte pointers is a fatal error.

### Building with MSVC

MSVC's settings live in `cmake/msvc.cmake`, which `CMakeLists.txt` includes
whenever CMake reports `MSVC` (both `cl.exe` and `clang-cl`): `/W4 /utf-8`,
the CRT's Annex K deprecations turned off at their source
(`_CRT_SECURE_NO_WARNINGS`, `_CRT_NONSTDC_NO_WARNINGS`), the narrowing
warnings C4244/C4242/C4267 off because GCC and Clang only report those under
`-Wconversion`, which the port does not enable on any compiler, and the
matched sources' three suppressions spelled `/wd4100 /wd4101 /wd4189`
(`-Wno-...` under clang-cl), plus two that only `cl.exe` raises on them:
C4200 (`u8 data[0]`, mwccarm's flexible array member) and C4245 (`u32 x =
-1`, which GCC and Clang report only under `-Wsign-conversion`); the few
matched files with mwccarm's `#pragma unused` also get C4068 off, as they
get `-Wno-unknown-pragmas` under GCC. C4324 (a struct padded to a
`jmp_buf`'s 16-byte alignment) is off everywhere: it reports the padding
the alignment requires, and GCC and Clang have no such warning. Plain `char` is signed under MSVC already, so
nothing is passed for it; `/J`, which an earlier revision passed, makes it
*unsigned*, and `sdk/types.h` now fails the build if plain `char` is not
signed on any compiler.

The pointer width comes from the developer environment, not from a
toolchain file: `vcvarsall.bat x64` builds LP64 (LLP64, strictly), and
`vcvarsall.bat x64_x86` builds the 32-bit reference configuration, whose
executables run on the same machine. Do not pass
`cmake/ilp32-toolchain.cmake` with `cl.exe`.

```
"C:\Program Files\Microsoft Visual Studio\<version>\Community\VC\Auxiliary\Build\vcvarsall.bat" x64_x86
cmake -S . -B build-msvc32 -G Ninja -DALETHEIA_ROOT=..\aletheia -DEIDOS_SDL=OFF
cmake --build build-msvc32
ctest --test-dir build-msvc32 --output-on-failure
```

`tools/check.py` has the same three as `msvc-x64`, `msvc-x86` and
`clangcl-x64`: it finds Visual Studio with `vswhere`, loads `vcvarsall.bat`
into the child processes only, and skips each with the reason when it
cannot (not Windows, no Visual Studio, no C++ build tools, no clang-cl).
The build needs the Visual Studio components
`Microsoft.VisualStudio.Component.VC.Tools.x86.x64` (the compiler, its
headers and CRT, and `vcvarsall.bat`) and a Windows SDK (the UCRT headers
and `kernel32.lib`); clang-cl is
`Microsoft.VisualStudio.Component.VC.Llvm.Clang`.

**SDL2 under MSVC.** `tools/fetch_sdl.py` fetches the MinGW package, whose
import library does not link with MSVC, so the MSVC configurations build with
`-DEIDOS_SDL=OFF`. For the platform layer and the demo, either install SDL2
with vcpkg (`vcpkg install sdl2:x64-windows` or `sdl2:x86-windows`, then
configure with
`-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`), or unpack
the official `SDL2-devel-<version>-VC.zip` from the same libsdl-org release
anywhere outside the repository and configure with
`-DSDL2_DIR=<unpacked>/cmake`. Either one is found by the existing
`find_package(SDL2)` step; neither puts a binary in the repository. Copy the
matching `SDL2.dll` next to `eidos_view.exe` before running it.

**No dialogs.** The Microsoft runtime reports `abort()`, a failed
`assert()`, an invalid CRT argument and every debug-CRT error with a modal
window that waits for a click, which stalls an unattended test run (several
tests abort on purpose through `tests/expect_panic.cmake`).
`sdk/plat_nodialog.c` is compiled into every executable of an MSVC or
clang-cl build (option `EIDOS_NO_CRT_DIALOGS`, on by default) and, from a
CRT initialiser before `main`, sends the debug CRT's reports to stderr,
turns off `abort()`'s message box and fault report, installs an
invalid-parameter handler that prints and exits, and sets
`SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)` so a crash
is an exit code, not a Windows Error Reporting window. `tools/check.py`
sets the same error mode on itself on Windows, which every child inherits.

**Debug runtime.** The check configurations build `CMAKE_BUILD_TYPE=Debug`
(MSVC's default, stated explicitly in `check.py`), so they link the debug
CRT: it checks the heap's guard bytes on every `free` and fills new
allocations with `0xCD`, the closest thing these configurations have to the
sanitizer runs GCC and Clang get. With the dialogs off it costs nothing in
unattended runs.

**Status.** Built and tested on Visual Studio 2026 (18, MSVC 14.50, Windows
SDK 10.0.26100, clang-cl from the same installation): `msvc-x64`,
`msvc-x86` and `clangcl-x64` are all ok in `tools/check.py`, 0 warnings at
`/W4`, with the real game data: 34 of 34 tests on x64 under both compilers
and 35 of 35 on the x86 ILP32 reference (`test_snd_matched` is ILP32-only),
`test_extract_rom` skipped for want of a ROM in each. The first run's
warnings were all in port code or explained above: block-scope `extern`
function declarations (C4210, moved to file scope in `sdk/narc.c`,
`sdk/tcbl.c` and `tests/test_tcbl.c`), constant truncation in a `u16`
mask (C4310, `sdk/arm7/pm.c`), an uninitialised `static const` table
(C4132, `engine/g2d/hwcompose.c`) and a local the compiler could not prove
set (C4701, `engine/g3d/nsbmd.c`).

## Pointer width, and the reference configuration

**ILP32 is the reference configuration for eidos until the field code is in.
LP64 keeps building and keeps passing every test.** `docs/pointer-width.md`
is the measurement this is decided on and states the trade-offs; the short
form is that on a 32-bit target every struct the game's layout owns has the
DS's own bytes, and that 642 of the 1110 matched sources aletheia's manifest
names (57.8%) carry at least one assumption that a pointer is four bytes.

```
cmake -S . -B build32 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/ilp32-toolchain.cmake \
    -DALETHEIA_ROOT=../aletheia
cmake --build build32
ctest --test-dir build32 --output-on-failure
```

`cmake/ilp32-toolchain.cmake` looks for an i686 compiler driver, first in
aletheia's `tools/gcc-host/` (the MinGW-w64 archive ships
`i686-w64-mingw32-gcc` next to the x86_64 one, so `-DALETHEIA_ROOT` is the
whole of what it needs) and then on PATH, and falls back to `-m32` on the
default compiler. It does not set `CMAKE_SYSTEM_NAME`: this is a same-OS,
different-ABI build and ctest runs the executables exactly as it does the
64-bit ones. SDL2 is not a problem either, because the devel package
`tools/fetch_sdl.py` unpacks carries an `i686-w64-mingw32` tree beside the
64-bit one and `CMakeLists.txt` picks the triplet by pointer width;
`-DEIDOS_SDL=OFF` is there for a host with no 32-bit SDL2.

`CMakeLists.txt` then reads `CMAKE_SIZEOF_VOID_P` and decides one thing:

| `EIDOS_TCBL_MATCHED` | default on | the nine tcbl sources come from |
| --- | --- | --- |
| ON | a 32-bit target | `${ALETHEIA_ROOT}/src/matched/`, compiled as written |
| OFF | a 64-bit target | `stubs/reimpl_` |

Turning it on for a target whose pointers are 8 bytes is a configure-time
fatal error, not a link that silently truncates addresses.

Measured on this machine, same tree, same tests:

| configuration | node / manager | the nine tcbl sources | ctest | test_tcbl |
| --- | --- | --- | --- | --- |
| LP64 (default) | 40 / 88 bytes | `stubs/reimpl_` | 11/11 | 49 checks, 0 failures |
| ILP32 | 24 / 56 bytes | matched, compiled as written | 11/11 | 49 checks, 0 failures |

24 and 56 are `0x18` and `0x38`. `tests/test_tcbl.c` asserts both layouts
rather than one, and on ILP32 it additionally asserts that
`EIDOS_TCBL_HEADER_BIAS` is zero, which is the port's one piece of arithmetic
that exists only to pay for pointer width.

**The exit condition is a number, and it is tracked.** `python
tools/closure.py --pointer-audit` counts the matched sources that hold an
address in an integer, hardcode a struct size or field offset, or cast
between an integer and a pointer. The largest category, 355 files, is
explicit padding (`u8 pad[0x14]; u32 active;`), which is not a rule violation
to be corrected but a field offset frozen into a source that does not yet
know what those bytes are. It falls as waves decompile the structs around it,
and when it is small enough to re-verify in a wave or two, LP64 becomes the
reference and ILP32 becomes the fidelity cross-check. That has to happen
before Milestone 9 in any case: macOS and iOS have no 32-bit.

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

**5. No pointer may be stored in a struct the game's layout owns, in code
the port itself writes.** This is a rule about the shim, not about matched
code: a game-owned struct the game fills in with pointers (the task-callback
node) is the game's business, and the answer to it is the pointer width the
port is configured with, not a rule the port could impose. See "Pointer
width, and the reference configuration" above and `docs/pointer-width.md`.
Where the port owns the bytes it stores an index. The matched `struct
ArcFile` is `u8 file[0x48]; u32 imageBase; u16 fatOffset;
u16 count;`, four-byte aligned. The FS shim owns those 0x48 bytes and stores
an *index* into a table of open `FILE *`, not the pointer, so the struct
neither grows nor needs eight-byte alignment on a 64-bit host. `sdk/fs.c`
asserts the size at compile time.

**6. `char` is signed.** `-char signed` is part of the pinned DS compiler
configuration; the CMake build passes `-fsigned-char` to GCC and Clang so
the host agrees. MSVC's plain `char` is signed by default and gets no flag
(its `/J` would make it unsigned; see "Building with MSVC"). `sdk/types.h`
asserts the signedness at compile time, so a configuration that gets it
wrong fails to build.

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

Two more, both about the task-callback list and both now load bearing.
`sdk/tcbl.h` has the detail; in short, the node's `+0x00` and `+0x10` are
declared `u32` in every matched tcbl file and both hold pointers on the DS.
The port borrows the four bytes of LP64 padding behind `+0x00` to widen the
first into a real pointer (safe: no matched file reads either half), and
refuses the second outright, so no node on this host owns a separately
allocated work area.

A third, not yet load bearing: some matched sources declare
`OS_AllocFromHeap` / `OS_FreeToHeap` with all-`u32` parameters, because on the
DS a pointer is a u32 and the matcher only needs the relocation. On an LP64
host such a declaration cannot carry a pointer. Neither function is in the
current closure; when a TU that declares them that way enters it, the fix is a
wrapper include in the port layer, never an edit to the matched file.

One about the FS shim, not yet load bearing: 0x02078C18 (`FS_OpenFile`)
takes a path. `src/matched/arm9/func_02020DEC.c` and
`ov107_func_021F0850.c` pass a `char *`, `unit_02048DDC.c` passes a `u32` ARM9
address under the name `CRYPTOi__BN_lshift1`, and `func_020461F4.c` passes a
`u32` under `func_02078C18`. `sdk/fs.c` defines the pointer form for the SDK
name and the address spelling, and the `u32` form for `CRYPTOi__BN_lshift1`,
so every caller in the build today gets the type it declared. When
`func_020461F4` enters an LP64 build it needs a wrapper include that routes its
call to the address form.

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
3 species). With the 2D engine added (2026-09-16): 38 objects, three tests
pass (test_g2d 4/4 species, pixel-identical to the oracle). With the BG
half added (2026-09-16): 39 objects, four tests pass (test_bg 8/8 screens
pixel-identical, plus the scroll, priority and screen-layout checks).
With the affine paths, the windows and the colour effects added
(2026-09-16): 34 library objects, seven tests pass. `test_g2d` now checks
six species, the four identity ones pixel-identical and the two affine
ones measured against the oracle; `test_affine` (46 checks), `test_bg_affine`
(48 checks) and `test_window` (66 checks) are synthetic and need neither
game data nor an oracle.

With the platform layer, the frame skeleton and the demo added
(2026-09-16): 36 library objects plus the platform library, **nine tests
pass** in 1.1 seconds, including `test_view`, which runs `eidos_view` under
the dummy video and audio drivers and checks the BMP it wrote. The same tree
configured with `-DEIDOS_SDL=OFF` builds warning-free and passes the other
eight. A windowed run of `eidos_view --frames 60` takes one second of wall
clock and reports 60 frames, 1002 ms of game time and no pacer
resynchronisation, which is 60 / 59.8261 s to the millisecond.

With the heap layer and the game's task-callback list added (2026-09-16):
**ten tests pass** in 1.4 seconds. `test_tcbl` runs 49 checks over the
matched constructor, the matched free-list build and the matched runner on
the matched heap, and needs neither SDL nor game data; `test_view` now also
checks that two frames of the demo differ on the bottom screen, which is the
animation a task on that list drove, and not at all on the top screen.

With the 3D engine added (2026-09-17): five more library objects, all of
them `engine/g3d/`, and **twelve tests pass** in 1.7 seconds. `test_g3d` ran 68 checks at that point: the geometry half
against the OBJ aletheia's `map_shapes.py` exports (7798 vertices and
2354 faces over two map models, worst deviation 5.000e-07), and the
raster half against hand-computed pixels plus a content check on a
rendered frame. `test_view` also runs `eidos_view --map 120` headless and
checks the 3D layer reached the top screen. The same tree configured
with `cmake/ilp32-toolchain.cmake` passes the same twelve, and
`-DEIDOS_SDL=OFF` builds warning-free and passes the other eleven.

With the node dictionary fully decoded (2026-09-17): `nsbmd.c` reads the
MDL0 node dictionary, its translation, its packed and full rotations and
its scale, and the render-command section's node hierarchy and shape ->
node binding. The record layout is checked across 740 models, rotations
are orthonormal in 1163/1163 nodes, reciprocal scales are exact in
534/537, parents are in range and acyclic in 670/670, and every drawn
shape binds to a node that exists. `g3d_render_model` follows the draw
list applying each shape's node and ancestor transforms. `test_g3d` is
now **133 checks, 0 failures**: the same geometry and raster numbers as
before (the node is the identity for both oracle models) plus the byte
decode of `m_pc01_00_00`, `m_ph01_00_00`, `staffroll_b` and
`batt_stage01`, the synthetic translation/scale/rotation/binding/
composition cases, and a real render of `pl_00_00` under a node pose
override. All fourteen tests still pass in both configurations.
`docs/g3d-nodes.md` records the byte evidence and the two remaining
uninterpreted fields (the second flags byte and the packed rotation's
header `u16`), neither needed for the transform.

With the ILP32 configuration added (2026-09-16): the same tree configured
with `cmake/ilp32-toolchain.cmake` against the `i686-w64-mingw32-gcc` in the
same MinGW-w64 archive builds 96 targets warning-free, finds the i686 SDL2
tree beside the x86_64 one, and passes the same **ten tests** in 1.2 seconds,
with the nine pointer-width-dependent tcbl sources compiled as written and
the node and the manager at the DS's own `0x18` and `0x38`. `tools/hostbuild.py --check`
makes the same choice for the ARM cross build and still compiles 69 objects
clean.

## The file layer

The port's data pipeline starts with `sdk/vfs.c`, the one place a
NitroFS-relative path becomes a host path. It owns the **data root**
`tools/ndsrom.py`'s `extract` wrote (the player's own ROM dump) and a list
of **mod roots**, each laid at the same relative layout, in a load order in
which a later mod wins. `BW_DATA_ROOT` and `BW_MOD_ROOT` set them from the
environment (`BW_MOD_ROOT` may list several roots separated by `;`, on every
host, since a Windows drive letter uses `:`), and an explicit
`bw_set_data_root` / `bw_set_mod_root` call wins over both. `sdk/vfs.h` has
the rest of the API: `bw_add_mod_root`, `bw_clear_mods`, the list in load
order, and the merge calls below.

A mod overrides game files at two granularities:

- **Whole files.** `<mod>/a/0/1/6` as a regular file replaces the NitroFS
  file `a/0/1/6`. A path resolves to the latest layer holding a regular file
  there, the data root last (`bw_data_path`, `bw_data_exists`).
- **Single archive members.** `<mod>/a/0/1/6/` as a *directory* holds loose
  members: a file named `<index>` or `<index>.<ext>` (`12`, `0012.bin`,
  `12.nclr`; the extension is free, leading zeros are allowed) replaces
  member 12 of the archive, or **adds** it when the archive is shorter, so a
  mod grows an archive (a new Pokemon, move, map) without repacking it.
  Members past the original end that no mod fills are empty. Other names in
  the directory (`README.txt`, `12.bin.bak`) are ignored; two files naming
  the same index in one mod are an error, reported on stderr, and the
  archive then fails to open rather than pick one. The member file holds the
  member's own bytes in the game's format (an NCGR, a compressed blob, a
  data record); converting from a readable format is a tool's job, not the
  file layer's.

Load order. Mods load in the order given, re-sorted stably by the `order`
key of each mod's optional manifest, `<mod>/eidos_mod.txt`:

    name  = Fairy type    # shown by tools; default: the directory name
    order = 10            # signed integer, default 0; higher loads later

For one archive the rule is a walk through the layers in load order: the
latest layer holding a regular file supplies the base archive (a whole-file
replacement discards every earlier mod's members of that archive), and
every later mod's member directory is applied on top of it, a later mod
winning for the same index. A member directory with no file under it in any
layer is just a directory, not a new archive (`a/0/1` in a mod tree holds
the replacement files `a/0/1/0..9`, whose names look like indices); a
brand-new archive ships as a whole NARC. The format's own limit stays:
BTAF's count is a u16, so 65535 members.

How every reader sees it. The game's readers are matched code
(`func_02048E34`, `func_02049578` and the rest): they read the NARC's header,
BTAF and GMIF through `FS_SeekFile`/`FS_ReadFile` and trust the
`imageBase`/`fatOffset`/`count` fields filled from the same bytes. So
`sdk/vfs_narc.c` synthesises the merged archive as a complete NARC in
memory, laid out exactly as the cartridge's are (header, BTAF, BTNF, GMIF,
4-byte member alignment padded with 0xFF; repacking any of the 234 real
archives from its own tables reproduces it byte for byte), and the FS shim
serves it as a memory file under every open: `FS_OpenFile` by path,
`bw_fs_open_id` / `FS_OpenFileFast` by file id, `FS_OpenFileDirect` by ROM
range (a range covering the whole file opens the whole merged file), the
archive id table, and `bw_arc_open`, which now opens through
`bw_fs_open_rel`. Merged images are cached per path with a reference count
held by the open files (the `FSFile` record stores the cache's index, never
a pointer, so the 0x48-byte rule holds), dropped on any change to the roots
or the mod list, and kept alive for a file still open across one.
`bw_narc_pack`, `bw_vfs_build_merged` and `bw_vfs_member_overrides` are
callable directly, for tests and mod tools. Two limits, stated: the ROM FAT
the boot copies (`FS_LoadArchiveTables`) still holds the cartridge sizes,
and a strict sub-range of an archive opened by ROM offset keeps the
cartridge layout's offsets (no matched caller does either with an archive).
Paths are narrow strings throughout, as fopen takes them;
`sdk/plat_dir.c` is the one platform file (Win32 and POSIX) that tells a
file from a directory and lists one.

`tests/test_vfs.c` checks whole-file resolution (13 checks).
`tests/test_vfs_members.c` builds a base tree and seven mod trees and reads
every case three independent ways: `sdk/narc.c`'s reader plus the matched
`func_02049578` on its handle, the matched readers by archive id
(`func_02048DDC`, `func_02048E34`, `func_02048F4C`) over a synthetic name
table, and `FS_OpenFile` of the archive, whose whole image must equal a NARC
packed from the expected member list. Cases: no mods, an empty mod, a
replaced member, members added past the end with a hole, two mods on one
member in both load orders, a manifest's `order` and `name`, a whole file
superseding earlier member directories and a member directory on top of a
whole file, a duplicate index, members over a non-NARC, and an open file
surviving a mod list change. With game data it also repacks all 234 real
archives byte for byte, then replaces member 5 of the real `a/0/1/6` and adds
one past its end, and reads both through the real ARM9 archive table and
`func_02048E34` / `func_02048F4C`, by file id and by ROM range (**369 checks
in both pointer widths**).

The other two items of the milestone are the
first-run extractor (below) and booting the game from the extracted tree:
the game's own `FS_Init` and overlay loads now reach the extract through
`sdk/fs.c` and `sdk/overlay.c` (the sections below), and the boot that
calls them is the work `docs/boot.md` plans ("Running the game's boot").

### Archives by id, the way the game opens them

Every asset the game loads goes through one family of matched readers that
take an **archive id** rather than a path: `func_02048DDC` opens archive N
through the name table the boot installs (`func_02048CEC(0x020A6C58, 0xEB)`
in `src/matched/sub_0200545C.c`: 235 ARM9 addresses of the strings
`a/0/0/0` to `a/2/3/4`), `func_02048E34` resolves one member's extent out of
the NARC's own tables, and `func_02048D00` / `func_02048F4C` read a member
into a buffer. `src/matched/unit_02048DDC.c` is now in the build (it was the
last stub under the two readers already there), and the port supplies the
bottom of it:

- `bw_arc_install_table` is the boot's call: it points `g_02146A68` at the
  table inside the ARM9 image. The object is declared the way the reader
  declares it (`u32 * volatile`), so it works at either pointer width, and
  the entries stay DS addresses.
- the open-by-path SDK call at 0x02078C18 resolves such an address through
  the ARM9 window to its string and the string through the file layer, **mods
  first**, so a mod's replacement archive, or the merged archive its member
  overrides make, is what the game's own readers see. aletheia's `layout/symbols_sdk.txt` now names 0x02078C18
  `FS_OpenFile`, but the matched file still calls it `CRYPTOi__BN_lshift1`,
  the name an earlier signature scan gave it, and passes the path as a DS
  address. The port defines that spelling as the address form of
  `FS_OpenFile` and records the history in `sdk/fs.c`.
- `bw_arm9_set_image` installs an ARM9 image from memory, for a caller that
  already holds it (the first-run extractor, a test).

`tests/test_arcid.c` needs no game data: it writes three NARCs and a mod
copy of one, and a synthetic ARM9 image with its own three-entry table, then
reads every member by id through the matched readers and checks every byte
against what it wrote and against `sdk/narc.c`'s reader, which shares no
code with the matched one. It also covers D00's byte-offset mode, the
out-of-range member reaching the game's assert sink, the mod overlay winning
and falling through, and the open shim's refusals. **82 checks in both
pointer widths.**

### The ROM file system the boot sets up

The boot calls `FS_Init(1)` and then sizes and fills the FAT/FNT cache through
the matched `FS_TryLoadTable` (aletheia files it as `dwc_startutilityex.c`),
which bottoms out in `FS_LoadArchiveTables` (`func_0207A5C8`). aletheia
matches all of libfs, but every path through it ends in a cartridge read, so
`sdk/fs.c` is a shim over the extracted tree with the SDK's surface:
`FS_Init`, `FS_SetDefaultDMA`, `FS_InitFile`, `FS_OpenFile`/`FS_OpenFileEx`
by path, `FS_ConvertPathToFileID` + `FS_OpenFileFast` by file id,
`FS_OpenFileDirect` by ROM range, `FS_CreateFileFromMemory`, seek, read,
length, position and close, under the SDK names and the `func_<address>`
spellings the matched callers use.

- The extract's `fat.bin` and `fnt.bin` (beside the data root) are the tables.
  Paths resolve through the FNT the way the SDK resolves them (`rom:` prefix,
  leading slash, ASCII case, `.` and `..`), and a file id resolves back to its
  NitroFS path and then through the file layer, mod overlay first.
- `FS_LoadArchiveTables` answers the SDK's own size,
  `(fat + fnt + 32 + 31) & ~31`, and copies both tables into the block at its
  first 32-byte boundary, so the boot's arena block has its DS size and DS
  contents and every later arena address keeps its DS distance.
- `FS_OpenFileDirect` turns a ROM range into the FAT entry that holds it and a
  window into that file, which is what lets a reader built on ROM offsets run
  over a directory of extracted files.
- The 0x48-byte `FSFile` rule is unchanged: the record in it holds an index
  into a table of open host files or buffers, never a pointer.

`tests/test_fs.c` checks all of it against a hand-built four-file tree and
hand-built tables (no game data), and, given a data root, round-trips every
named file id of the real FNT, opens real archives by path and by ROM range
and compares their bytes, and checks the table cache against the two files.
**111 checks in both pointer widths.**

### Overlays

In the port an overlay is compiled in, not loaded. `sdk/overlay.c`
implements `FS_LoadOverlayInfo`, `FS_LoadOverlayImage`, `FS_StartOverlay`,
`FS_EndOverlay`, `FS_UnloadOverlayImage`, `FS_LoadOverlay` and
`FS_UnloadOverlay` (and `func_020792B8`, the unload's spelling in the game's
overlay manager, `sub_02034A3C`) over a registry with one entry per overlay:
the overlay table's own eight words, every function aletheia's metadata knows
in it with its status (open, matched, present in this build), and its static
initialisers.

- `FS_LoadOverlayInfo` returns the same eight words as the ROM's `y9.bin`,
  plus the file's ROM extent from the FAT.
- Loading an overlay that is not all present **panics at the load**, naming
  the overlay, its RAM range, the lowest-addressed missing function and
  whether aletheia has matched it, and the overlay's coverage:
  `overlay 9 (0x02155160..0x021A8260) is not in this build: first missing
  function func_02155168 at 0x02155168 (matched in aletheia, not compiled
  into this build); 0 of 1621 known functions present, 1604 matched`.
- Loading a present overlay calls the image hook
  (`bw_overlay_set_image_hook`), which must put the overlay's data and .bss
  back to load-time state, then runs its static initialisers, then marks it
  resident and evicts any resident overlay sharing its RAM. With no hook the
  load panics rather than running code over stale data.

The table is generated at build time by `tools/overlay_table.py` from
aletheia's `layout/layout.json`, its manifest shards, `layout/symbols_sdk.txt`
and the `symbols:` blocks and externs of the matched overlay sources, plus the
build's own source list (which decides "present"). Addresses, sizes and names
only. It is generated rather than committed because its coverage columns move
with every aletheia wave and every source the build adds; `python
tools/overlay_table.py --summary` prints it (237 overlays, about 23,000 known
functions, 18,500 of them matched, none present yet).

Two limits, stated so nobody relies on more. The function list is a **lower
bound**: aletheia commits no per-overlay list of function starts, only the
matched ones, so a function nothing matched names is invisible here, and an
overlay could be reported present while one is missing. And 1046 function
names repeat across overlays that share RAM (`func_02155244` is in ov9 and
ov10), so compiling two such overlays into one executable needs per-overlay
symbol names: `tools/ovlink.py` gives each compiled overlay its own
(`ov010_func_02155244`), as the next section describes. Only ov230 has static
initialisers (five; aletheia's `docs/MATCHING.md`), and the generator refuses
any other non-empty list rather than guess.

`tests/test_overlay.c` checks the registry against a synthetic four-overlay
table, checks the generated table's shape, and, given a data root, compares
all 237 `FS_LoadOverlayInfo` headers word for word with `y9.bin`, the file
extents with the FAT, and ov230's initialisers with its decompressed image.
`test_overlay_refuses` checks the panic message. **70 checks in both pointer
widths.**

#### Overlays compiled in

`docs/overlays.md` is the design. `tools/ovlink.py` compiles a set of
overlays into one executable (`eidos_overlays()` in `cmake/overlays.cmake`;
`EIDOS_OVERLAYS` puts one into `eidos_boot`, the title path by default when
`EIDOS_BOOT` is on):

- each overlay's names are renamed `ovNNN_<name>` by a forced-include header,
  so overlays sharing a window link side by side; a call into a window from
  outside it goes through a generated trampoline that asks which overlay is
  resident and calls its function, or panics naming what is there;
- loading one copies its file from the player's `extract/overlays_dec/` into
  its window in the port's main RAM, zeroes the `.bss`, binds the overlay's
  own data names and relocates the pointer words the metadata lists, the
  ARM9 image's model (`docs/armdata.md`) applied at load time
  (`sdk/overlay_image.c`);
- a function the build lacks (unmatched, an mwcc `asm` body) is a stub that
  panics naming the overlay and the address, so the loader's presence check
  is per function: a compiled overlay with stubs loads and runs until it
  reaches one.

The title set (ov009, ov010, ov011, ov013, ov017, ov228, ov088, ov179) is
3163 matched sources, 30 `asm` files left to stubs, 340 trampolines.
`tests/test_ovlink.c` checks two synthetic overlays sharing one window, both
loaded alternately and reached from ARM9 code and from a third overlay;
`tests/test_ovlink_real.c` loads ov088 and ov179 from the extract and runs a
matched function of each.

## The first-run extractor (install/)

The end state is an installer: the player gives it their own ROM (and,
optionally later, a DSi BIOS) and gets a playable game on Windows, macOS,
Linux, Android or iOS. `install/` is the part of that which is the same on
every platform, plain C99 with no SDL and nothing of the game, plus one
small file of OS calls (`install/fsutil.c`, Win32 and POSIX):

- `bw_rom_identify`: the header (title, game code, unit code, checksum,
  capacity) and a streamed SHA-1 of the whole image (`install/sha1.c`, FIPS
  vectors in the test), plus the hash of a trimmed image padded back to
  its capacity;
- `bw_rom_verdict`: whether eidos takes the image and, if not, why, in
  words a player can act on: not a DS image, another game, White or another
  region, a damaged header or a dump cut short, a modified dump. A ZIP or
  7-Zip archive is named as one. A dump trimmed of padding only is accepted;
- `bw_rom_extract`: the tree aletheia's `tools/ndsrom.py extract` writes
  (`data/` NitroFS files, ARM9/ARM7, FNT/FAT, both overlay tables and their
  files, orphans, the banner, the DSi regions) plus what `tools/delink.py`
  derives: `arm9_dec.bin` and `overlays_dec/`, BLZ-decompressed by
  `install/blz.c` exactly as `tools/blz.py` does. `eidos_manifest.json`
  records which image and which installer version. The output is atomic
  (staged in `<out>.partial`, renamed into place when complete); a finished
  extraction of the same image is reused, an interrupted one restarts, and
  a folder holding anything eidos did not write is refused, never deleted;
- the ROM is untrusted input: every offset is checked, the whole plan is
  validated (bounds, overlaps, a cyclic FNT) before a single file is
  written, names are sanitised exactly as ndsrom.py sanitises them, and
  paths are UTF-8 on every platform (the wide API on Windows, with the
  long-path prefix past 200 characters).

`eidos_extract [--any] [--replace] <rom.nds> [out_dir]` is the command-line
front end. **On the real ROM it writes 726 files, all 726 identical (size
and SHA-1) to aletheia's own extract/**, with nothing of that tree missing
but four files written on purpose by aletheia's tools only (rebuild
bookkeeping, the DSi-key-only `arm9i_dec.bin`, a disassembler view). It
takes about 6 s with an optimised build on this machine, half of it the
hash. `docs/installer.md` has the details, the intended differences, and the
design of the per-platform first-run wizard (where the data goes, how the
ROM is picked, what the engine does without data).

**`tests/test_extract.c`** needs no game data. `tools/synth_rom.py` builds
a synthetic DSi-enhanced cartridge from a fixed seed (nested directories,
names that need sanitising, overlays in both tables, a BLZ-compressed ARM9
overlay, an orphan, an empty FAT slot, a zero-length file, an animated
banner, both digest tables, a residue blob, a correct header checksum, an
ARM9 packed by aletheia's own `blz.py`) and runs aletheia's
`ndsrom.py extract` and `blz.py` on it. The C extractor's output, written
into a folder named outside ASCII, is compared with that **file for file by
SHA-1: 28 of 28 identical**. Around it: the SHA-1 and BLZ vectors, 17 name
cases, every verdict, the padded hash of a trimmed image, the existing-folder
rules (foreign files, empty, another image, an older installer, a killed
run's staging), 20 broken images each refused with the right message and
nothing left behind, and 300 randomly damaged images (43 extracted, 257
refused, none crashing, run under ASan by `tools/check.py`). 148 checks.

**`test_extract_rom`** (`tests/extract_rom.cmake`) runs `eidos_extract` on
the ROM the `BW_ROM` environment variable names, into a folder named
`extract é€ ü`, and compares every file with the extract/ tree that holds
`BW_DATA_ROOT`; the expected hashes are computed from that tree each run,
never stored in the repository. It also checks that a second run is a no-op
and no staging is left. Without `BW_ROM` it reports itself skipped.

## Planned Milestones

- [ ] **Milestone 1**: Portability pass
  - [x] Fix pointer width assumptions (32-bit to 64-bit host): the first
        game-owned structs that store pointers are in the build (the
        task-callback node and its manager), the host layout is written
        down beside the DS one in `sdk/tcbl.h`, the nine matched sources
        that depend on a four-byte pointer are identified and stood in for,
        and the two `u32`-declared pointer fields are answered by wave 26:
        both really are pointers, and two matched declarations of the same
        struct now disagree on an LP64 host. See "Why nine matched files
        are out of the LP64 build"
  - [x] Settle the pointer-width policy: an ILP32 build configuration
        (`cmake/ilp32-toolchain.cmake`) where the nine matched sources
        are compiled as written and the game's structs have the DS's own
        bytes, the LP64 build kept working through one CMake switch keyed
        on `sizeof(void *)`, and the cost of each route measured over the
        whole matched corpus by `tools/closure.py --pointer-audit`. ILP32
        is the reference configuration; `docs/pointer-width.md` records the
        counts, the decision and what would reverse it. Wave 26 is the
        first case the policy *decided* rather than merely survived: two
        EXACT readings of one struct, one target on which they agree
  - [x] Endianness and struct packing compatibility: every read out of
        `extract/` goes through `bw_rd16`/`bw_rd32`, `char` is signed on
        every compiler the build supports, and no matched struct in the
        source list is reached through a byte offset that the host lays
        out differently
  - [ ] Under-declared globals and compiler-specific extensions (two known
        and recorded under "Known type mismatches", two more found in the
        tcbl family; the sweep itself is not done)
  - [x] Warning-free builds on GCC, Clang, and MSVC: warning-free and
        green on the pinned MinGW-w64 GCC at both pointer widths, and on
        MSVC x64, MSVC x86 (the ILP32 reference) and clang-cl x64 at
        `/W4`, all with the real game data, through `tools/check.py` (see
        "Building with MSVC"). GCC and Clang on Linux, and the ASan/UBSan
        configurations, are in `tools/check.py` too but have not been run
        on a real Linux machine yet
- [ ] **Milestone 2**: Platform layer (SDL2)
  - [x] Window creation and input handling: an integer scale of the DS
        layout, stacked or side by side, a configurable SDL scancode table
        for the keypad and the frame upload of two RGBA buffers
        (`engine/platform/`)
  - [x] Audio device initialization and timing: 32768 Hz stereo s16, the DS
        mixer's own rate, with a silent callback until the sequencer exists;
        and the frame pacer on the exact 59.8261 Hz period with the
        remainder accumulated
  - [x] The game's own scheduler runs the frame: the whole of aletheia's
        tcbl.c is in the build (construct, build the free list, register,
        run one pass, destroy one node, destroy all, and the four
        accessors), matched under ILP32, and `demo/eidos_view`'s step
        function is `src/matched/sub_020310C4.c`
  - [x] The heap the scheduler allocates from is matched end to end: the
        allocation entry point, the raw allocator, the provenance stamp and
        both audits, the release entry point and the expanded heap's front
        door. What is left port-side is the arena, which needs the heap
        environment constructor at `0x0202FC48`
  - [x] DS frame emulation loop: task list scheduler running at 60 Hz,
        vblank callbacks. `engine/frame/` is the vblank-driven loop, the
        commit-at-vblank convention and the DS OS tick (`sdk/os_tick.c`
        answers `OS_GetTick`), and the scheduler inside it is the game's
        own: `src/matched/sub_0203101C.c` builds the list on the matched
        heap layer and `src/matched/sub_020310C4.c` is the step function
        `demo/eidos_view` installs
  - [ ] The game's own boot runs the frame: NitroMain is supplied
        (`stubs/reimpl_02005124.c`), and the machine it runs on is in the
        tree and tested (the address space, the OS layer, the ARM7 stand-in,
        the file system and overlays, one `bw_port_reset`); linking its
        matched closure under `EIDOS_BOOT=ON` is in progress. See "Running
        the game's boot"
- [ ] **Milestone 3**: 2D engine shim
  - [x] BG layers from tile and palette memory: NSCR screens, all four
        text screen sizes, 16- and 256-colour modes, scroll, priority and
        the BG/OBJ frame compositor (`engine/g2d/bg.c`, oracle-tested),
        plus the rotation/scaling layers for BG2 and BG3: one-byte map
        entries, 8bpp tiles, the four square sizes from 128 to 1024,
        wraparound and the 20.8 reference point
        (`engine/g2d/bg_affine.c`)
  - [x] OAM sprites: priorities, flips, cell banks and multi-cell poses
        (`engine/g2d/`, oracle-tested), with the affine path in the
        hardware's fixed point: an fx32 sine table, the 8.8 PA/PB/PC/PD
        parameters, the double-size reference point and the per-pixel
        inverse-mapped texel fetch (`engine/g2d/affine.c`)
  - [x] Windowing and alpha blending: WIN0/WIN1, the OBJ window,
        WININ/WINOUT layer masks, and BLDCNT's alpha blend and the two
        BLDY brightness effects, under the DS rule that an effect runs
        only on the topmost pixel and blends only with the one directly
        beneath it (`engine/g2d/window.c`)
  - [x] Dual-screen rendering layout: two 256x192 screens, stacked or side
        by side, at an integer scale, each composed independently by
        `g2d_frame_compose_desc` and uploaded as its own texture
        (`engine/platform/`, `demo/eidos_view.c`)
  - [ ] Opening cinematic and title screen render correctly (the title
        screen's BG renders, checked pixel for pixel against the Python
        renderer and shown by `eidos_view`; the cinematic is untouched)
- [ ] **Milestone 4**: 3D engine shim
  - [x] DS fixed-point geometry engine emulation: the whole pipeline is
        20.12 integer arithmetic with 64-bit intermediates and no float
        and no `math.h` anywhere in `engine/g3d/`, including the
        perspective divide, the viewport transform and the camera
        helpers, which take the sine and cosine from `engine/g2d`'s
        existing table rather than adding a second one
  - [x] Matrix stack and vertex pipeline: the four matrix modes, the
        one-deep projection and texture saves, the 31-entry
        position/vector stack with its sticky over and underflow flag,
        load/mult in the 4x4, 4x3 and 3x3 forms, translate and scale,
        and a display-list interpreter covering the whole opcode table
        with all six vertex encodings, the current-vertex register,
        texcoord, colour, normal, polygon attributes and strip assembly
        (`engine/g3d/mtx.c`, `engine/g3d/gx.c`). Checked against
        aletheia's own OBJ export: 5678 of 5678 vertices and 1782 of
        1782 faces on one model, 2120 / 2120 and 572 / 572 on another,
        worst deviation 5.000e-07 against a 1e-6 tolerance
  - [x] Texture formats (A3I5, A5I3, 4x4 texel compressed, etc.): all
        seven decode (`engine/g3d/texture.c`), and the five that occur
        in `a/0/1/4`'s 21780 textures are surveyed and listed above.
        Format 5 is implemented from the public block encoding and has
        no real instance in this ROM; format 7 likewise does not occur
  - [ ] DS rasterizer quirks (perspective handling, edge rules): there
        is a rasteriser, it is documented rule by rule as hardware
        behaviour, approximation or port decision, and it has synthetic
        tests. It is **not** checked against a DS frame, and the two
        items in this line's name are precisely the two that are
        approximations: texture coordinates are interpolated affinely
        per span, and the fill rule is a conventional top-left one
        rather than the hardware's own edge inclusion. This box stays
        open until there is a frame to compare against
  - [ ] Overworld and map chunks render correctly: a map chunk renders,
        textured, from the player's own archives, and `eidos_view
        --map N` orbits it. The node dictionary's full SRT, its
        parent/child tree and the per-shape node binding are decoded and
        applied, so a multi-node model's parts sit at the positions and
        orientations its joint matrices put them at
        (`docs/g3d-nodes.md`); what is still missing is lighting, which
        is parsed and not interpreted, and a pixel oracle
- [ ] **Milestone 5**: Audio
  - [x] SDAT sequencer and mixer (replacing ARM7 audio core): the SDAT
        container, SBNK instrument tables, SWAR/SWAV waves, the SSEQ
        bytecode interpreter and a 16-channel mixer at 32768 Hz, in pure C
        with no SDL (`engine/audio/`), checked sample for sample against
        aletheia's `sseqsynth.py` and wired into the platform layer's audio
        callback (`eidos_view --music`)
  - [x] PCM and ADPCM channels: PCM8, PCM16 and IMA-ADPCM decode with loop
        points, semitone-ratio resampling and the linear ADSR, bit-identical
        to the oracle on `SEQ_BGM_GYM`
  - [x] PSG square wave and noise channels: the eight duty settings, and the
        15-bit Galois LFSR clocked at the SND timer rate, bit-identical to
        the oracle on `SEQ_BGM_BATTLEPINCH` and `SEQ_SE_KOUKA_H`
  - [ ] Music, sound effects, and Pokémon cries play accurately (the
        sequencer renders all three and the cry path is the same one, but
        the model it reproduces is the asset lane's: the ADSR curve, the
        volume law and the pan law are documented approximations of the
        hardware's, and Tie, portamento, modulation and the pitch wheel are
        no-ops. Making those accurate needs the DS sound driver itself,
        which is not decompiled yet, and it is what this test exists to
        measure the change of)
  - [ ] The game's own sound calls reach it: nothing in `src/matched/` plays
        a sequence yet, so the SEQ id is still chosen by the demo
- [ ] **Milestone 6**: Data pipeline & asset extraction
  - [ ] First-run asset extractor from legally dumped ROM (the core is
        in and verified on the real ROM: `install/` identifies the dump,
        refuses a wrong game or a bad dump with a message, and extracts it
        atomically into ndsrom.py's layout plus `arm9_dec.bin` and
        `overlays_dec/`, 726 of 726 files identical to aletheia's own
        extract, checked by `test_extract_rom` when `BW_ROM` is set; the
        per-platform wizard is designed in `docs/installer.md` but not
        built, and the readable-format conversion is not started)
  - [x] File layer: VFS with mod overlay support (`sdk/vfs.c`: the data
        root and several mods in a manifest-ordered load order, whole-file
        overrides; `sdk/vfs_narc.c`: member-level NARC overrides that
        replace or add members, served to every reader, matched ones
        included, as one merged archive; `test_vfs`, `test_vfs_members`,
        see "The file layer")
  - [ ] Engine boots cleanly from extracted assets (the ARM9 boot's
        closure is matched in aletheia and NitroMain is reimplemented here;
        the port's side is in progress, see "Running the game's boot", and
        what blocks the title screen past the first overlay load is listed
        in `docs/title-closure.md`)
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

eidos is original code under the MIT License (`LICENSE`). It contains no game
data and no reverse-engineered game source: it compiles that source from the
user's own aletheia checkout and reads assets the user extracted from their own
ROM. See `LEGAL.md`. Pokémon and Nintendo DS are trademarks of Nintendo; the
game is the property of Nintendo, Creatures Inc. and GAME FREAK inc.
