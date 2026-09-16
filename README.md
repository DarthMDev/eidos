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
| `sdk/port.h` | the port's own API: data root, archives, panic |
| `sdk/fs.c` | FS: `FS_SeekFile`, `FS_ReadFile`, `FS_CloseFile` onto stdio |
| `sdk/os_alloc.c` | OS heap: `OS_AllocFromHeap`/`OS_FreeToHeap`, and the expanded heap's blocks, the one place the port takes memory from the host |
| `sdk/os_irq.c` | OS: `OS_DisableInterrupts`/`OS_RestoreInterrupts` as a saved flag, `OS_Terminate` as exit |
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
engine, and the pixel oracle it is tested against" below. It has four
parts:

| directory | what it is |
| --- | --- |
| `engine/g2d/` | the 2D engine: OBJ, BG, affine, windows and the colour effects, no SDL |
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
it constantly: the heap count at +8 bounds the id, the byte table at +4 maps
a public heap id to a record index, the record array at +0 holds the arena
handle and the live-allocation count, and the halfword at +0xe is the result
code every one of them writes. The DS constructor that fills it in
(0x0202FC48) is not decompiled, so there is no matched constructor to call
and `bw_heap_init` initialises the fields the matched code reads.

**Wave 26 matched the whole of the release side and the expanded heap's
front door, and the host allocator path shrank but did not go away.** What is
in the build now is `src/matched/sub_020307B0.c` (the release entry point),
`sub_02030A2C.c` and `sub_020309B8.c` (the free-side watch filter and audit,
the mirrors of `sub_02030A00` and `sub_02030984` on the allocation side),
`sub_02030344.c` (the checked heap-id lookup under the audit) and
`src/matched/func_02060CA0.c` (the expanded heap's allocation front: round
the request up to a word, take it off the head for a non-negative alignment
and off the tail for a negative one, align to the magnitude).

**What stops the port dropping the host allocator is the arena, not the
code.** `func_02060CA0`'s two callees are EXACT in aletheia too, and both walk
a real region: `func_02060A08` walks the free list from the head and
`func_02060ABC` from the tail, each keeping the smallest block that can hold
the request once its data start is rounded, and each handing the winner to
`func_02060888`, which carves the region out and moves it to the used list.
Three things have to exist before any of that can run here, and all three
lead back to one address:

- **an arena.** `0x0202FC48`, the heap environment constructor, is the
  function that builds one and fills `g_EnvModule`'s records in with it. It
  is still open, so `sdk/game_globals.c` publishes a distinct non-null token
  per heap rather than a handle, and there is no free list to walk.
- **`func_02060888`**, the carve, which is what actually produces the pointer
  and writes the block header the walk reads. Open.
- **`sub_02030164`** on the release side, the raw free, and `sub_02060F90`,
  the block-size query the audit uses. Both open.

So `stubs/reimpl_02060A08.c` and `stubs/reimpl_02060ABC.c` stand in for the
two walks and `stubs/reimpl_02030164.c` for the raw free, all three over
`sdk/os_alloc.c`'s `bw_block_alloc` / `bw_block_free`. The head/tail
distinction therefore has no effect on the address handed out, and that is
the one observable difference from the DS: the direction flag exists so that
long-lived allocations can be kept from fragmenting the low end of a fixed
arena, and a host heap has neither a fixed arena nor that failure mode.
`src/matched/sub_02030098.c`, the only caller, tests the result against null
and nothing else.

Net: three port files where there used to be two, one level further down,
with the request rounding, the direction convention, the watch filtering, the
audit and the whole of the release entry point now running matched. The line
where the port begins is `func_02060888` and `0x0202FC48`, and it moves in
one step when the heap constructor is decompiled.

One host-only detail lives in `sdk/os_alloc.c` and nowhere else: **what is
aligned is the payload, not the block.** The caller adds a 0x1c-byte header
and hands out `block + 0x1c`, and 0x1c is not a multiple of eight, so an
eight-aligned block would hand out a payload that is only four-aligned and
every pointer in a task-callback node would be misaligned.

The rest of the failure path is aborting stubs, and honestly so:
`sub_02030B50` (the heap report, `MATCH` in aletheia's `src/pending/`),
`sub_02060E6C` and `sub_02060E94` (the arena walk and dump) are reachable
only after an allocation has already failed, one call before `OS_Terminate`,
and `sub_02060F90` only from the free-side audit, which the retail
watched-heap id of -1 never enables.

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
                 [--gap N] [--species N] [--frames N] [--shot PATH]
                 [--shot-first PATH] [--anim-speed N] [--no-audio]
                 [--music [SEQ]]
```

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

With the ILP32 configuration added (2026-09-16): the same tree configured
with `cmake/ilp32-toolchain.cmake` against the `i686-w64-mingw32-gcc` in the
same MinGW-w64 archive builds 96 targets warning-free, finds the i686 SDL2
tree beside the x86_64 one, and passes the same **ten tests** in 1.2 seconds,
with the nine pointer-width-dependent tcbl sources compiled as written and
the node and the manager at the DS's own `0x18` and `0x38`. `tools/hostbuild.py --check`
makes the same choice for the ARM cross build and still compiles 69 objects
clean.

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
  - [ ] Warning-free builds on GCC, Clang, and MSVC (warning-free on the
        pinned MinGW-w64 GCC today, with and without SDL2)
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
        `demo/eidos_view` installs. What is still missing is the boot path
        that makes the *game's* lists rather than the demo's one
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
  - [ ] DS fixed-point geometry engine emulation
  - [ ] Matrix stack and vertex pipeline
  - [ ] Texture formats (A3I5, A5I3, 4x4 texel compressed, etc.)
  - [ ] DS rasterizer quirks (perspective handling, edge rules)
  - [ ] Overworld and map chunks render correctly
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

eidos is original code under the MIT License (`LICENSE`). It contains no game
data and no reverse-engineered game source: it compiles that source from the
user's own aletheia checkout and reads assets the user extracted from their own
ROM. See `LEGAL.md`. Pokémon and Nintendo DS are trademarks of Nintendo; the
game is the property of Nintendo, Creatures Inc. and GAME FREAK inc.
