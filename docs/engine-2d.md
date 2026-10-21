# The 2D engine

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
display mode 2 shows bank A..D directly.

The 3D layer is composed with the 3D frame's alpha: alpha 0 is
transparent, so the 2D layers beneath show through, and any other alpha
blends with a second target directly beneath by the 3D layer's own rule,
`(3D * (A + 1) + 2D * (31 - A)) / 32`, EVA and EVB unused, whatever
BLDCNT's effect says; BG0HOFS scrolls it. Everything after the layer mix
runs in the hardware's 6 bits per channel: the compositor reports each
pixel's 6-bit value (`G2dFrameDesc.out6`) and MASTER_BRIGHT is applied
there (a 2D channel `c` enters as `2c + 1`, 0 as 0; that low bit is a
documented choice). BG mosaic (text, affine, extended and bitmap BGs
alike, resampled through the MOSAIC grid) and OBJ mosaic are drawn. The
display capture (DISPCAPCNT) runs with the frame: source A the graphics
screen (before the master brightness, composed even under a VRAM display)
or the 3D frame, source B a VRAM bank, the A+B blend, the four sizes and
the write and read offsets, into a bank in LCDC mode, and
`g2d_hw_render_dsmem` clears the enable bit when the frame completes, as
the hardware does. The game uses it: `0x80330010`/`0x80320010`
(`func_0204EAD4`, `func_0204EB68`: the main screen captured into bank D or
C and shown on the sub engine as a direct-colour bitmap BG, the two-screen
3D arrangement) and `0x81330010` (`func_021C2784`: the 3D frame alone);
ov203 sets every field from data. Still reported through
`G2D_HW_UNSUP_*` flags rather than drawn wrong, deliberately, because the
matched code sets none of them as a constant: BG mode 6's large bitmap,
mode 7, main memory display and capture from main memory (no DMA feeds the
display FIFO), and bitmap OBJs.

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
and I, the 3D layer with its alpha, its own blend and BG0HOFS, master
brightness on each engine in 6 bits, BG and OBJ mosaic, the display
capture with the game's own DISPCAPCNT words, the screen swap and display
modes. With game data, the title screen (`titledemo.narc`
0/1/2) and the species 25 pose are put into VRAM, palette RAM and OAM the
way the SDK would, and must come out **pixel-identical** to the asset
path drawing the same content from the files: the title scrolled on the
main engine, the pose on the sub engine, and both together under WIN0
and an OBJ-over-BG0 alpha blend. All three are identical.
