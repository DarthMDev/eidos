# The 3D engine

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
| `engine/g3d/raster.c` | the 256x192 software rasteriser, the rear passes (edge marking, fog, anti-aliasing), and the meeting point with the 2D compositor |
| `engine/g3d/light.c` | the DS lighting equation (four lights, diffuse, ambient, specular with the shininess table, emission), driven by `hw3d.c` |
| `engine/g3d/hw3d.c` | the 3D hardware interface: the geometry FIFO and command ports, the 3D registers, the texture slots, `SWAP_BUFFERS` |

**Where the rules come from.** In order of authority, and each file
header repeats it: aletheia's asset lane (`tools/assets/nsbmd.py` cracked
the container, the resource dictionary, the TEX0 records and the MDL0
shape dictionary; `tools/assets/map_shapes.py` cracked the render-command
bytecode and the material-to-texture link, and aletheia's `docs/ASSETS.md`
records both with their gaps); aletheia's matched sources
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
alpha test, blending off, an unterminated vertex list, the frame
composed by `hwcompose` as BG0, the lighting equation (diffuse, ambient,
emission, specular with and without the shininess table, the normal
through the vector matrix, `DIF_AMB` bit 15), edge marking, fog (the
table interpolation, the clear plane's fog bit, alpha-only), the
w-buffer against the z-buffer, the rear-plane bitmap with its offset and
depth, and anti-aliasing over a transparent and an opaque clear plane.
**111 checks, 0 failures**, both pointer widths. Two interpreter fixes came out of it: opcode `0x2B` (`PLTT_BASE`)
was missing from the parameter-count table, so a list using it stopped
there, and `MTX_SCALE` in position+vector mode now scales the position
matrix only, as the hardware does; `test_g3d`'s oracle numbers are
unchanged by both.

The frame also honours what the game turns on (each found in the
matched corpus): lighting (`engine/g3d/light.c`, the public lighting
equation with up to four lights, the light vectors and normals through
the vector matrix, the half vector and the shininess table), the
w-buffer (`SWAP_BUFFERS` bit 1: `func_02046440`, ov021, ov057), edge
marking with `EDGE_COLOR` (ov021, ov091, ov217), anti-aliasing (ov021,
ov091, ov217, ov223, and G3X_Init's default), fog with its table, colour,
offset, shift and alpha-only mode (enabled in ov119), the rear-plane
bitmap (the SDK's `GX_SetBankForClearImage` path), and the polygon IDs
and fog flags those passes need, including the clear plane's. The frame's
alpha is the hardware's (a translucent pixel over the clear plane's alpha
0 keeps its own alpha), which is what lets hwcompose see through and
blend the 3D layer. The fixed point of the lighting and the coverage
rule of the anti-aliasing are marked as approximations in their files.
Still not drawn, and reported by `g3d_hw_unsupported()`: shadow polygons
(no `POLYGON_ATTR` constant in the corpus selects mode 3). The hardware's
polygon and vertex RAM limits are not imposed.

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
- **Lighting on the asset path.** The display-list interpreter still
  consumes the lighting commands without interpreting them, so an asset
  render of a map model does not depend on lights nobody set. The
  hardware path interprets them (`engine/g3d/light.c` through
  `engine/g3d/hw3d.c`), so what the game's own code lights is lit.
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
