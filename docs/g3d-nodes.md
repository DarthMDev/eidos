# The MDL0 node dictionary, decoded from the bytes

This note records what `engine/g3d` derives about the MDL0 **node (bone)
dictionary** of the NSBMD container, the byte evidence for each reading,
and the validation numbers. Everything here is derived from the ROM's own
bytes through this repository's code, plus the public NDS 3D hardware
documentation already cited by `engine/g3d/g3d.h`. **No other
decompilation, disassembly, emulator, NSBMD tool or format library was
consulted**, and aletheia's asset lane does not decode nodes at all, so
there is no independent node oracle anywhere in either project. Every
claim below is either byte-evidence (a record field read straight off the
bytes) or a structural self-consistency check; the exact set of nodes
used and the exact numbers are reproducible from the scans described.

Data used: every BMD0 sub-file of `a/0/0/8` (649 models), `a/0/4/9`,
`a/0/4/5`, `a/0/1/1`, `a/1/2/1`, `a/1/5/7`, `a/1/3/8`, `a/2/2/0`,
`a/0/4/7`, `a/1/2/7` and `a/1/4/8` (740 models, 1163 nodes). The two
models the geometry oracle uses (`pl_00_00` = `a/0/0/8#120`, `map24_23` =
`a/0/0/8#0`) were not enough on their own: both have exactly one node,
whose record is the 4-byte identity, so all of the rotation, scale,
hierarchy and binding evidence comes from the multi-node models,
especially `staffroll_b` (`a/2/2/0#6`, 29 named bones), `m_pc01_00_00`
(`a/0/0/8#489`), `m_closseum02_00_00` (`a/0/0/8#545`, 18 nodes),
`mus_stage01` (`a/0/4/7#30`, 31 nodes) and `cannon_01_00` (`a/1/2/1#0`).

## 1. The model header and where the sections are

The model data after the model dictionary opens with a 0x40-byte model
header. Four of its u32 fields are confirmed by where the sections land:

| offset | meaning | check |
| --- | --- | --- |
| `+0x00` | total model-data size | equals the tail length |
| `+0x04` | start of the render-command section | the node records end exactly here |
| `+0x08` | end of the render-command section | four bytes before the material ResDict |
| `+0x0C` | shape ResDict offset | exact |

The **node dictionary is the first Nitro ResDict in the tail**, at tail
offset `0x40` in every model scanned. Its 4-byte records are offsets from
the dictionary's own start, exactly as the shape dictionary's are. The
node records run back to back to `+0x04`; the **hierarchy and the shape
binding then live in the render-command section `[+0x04, +0x08)`**, which
is the region aletheia's `map_shapes.py` already reads for the material /
shape opcodes.

## 2. The node record: four components, not three

The record opens with a 4-byte header: a flags byte, a second flags byte,
and a halfword that is part of a full rotation (a sign-extended `s16`). The flags byte's low four bits
are **active-low presence bits**, and the components follow in this
order:

| bit | 0 = present | size | decoded |
| --- | --- | --- | --- |
| 0 | translation | 12 bytes (3 x `fx32`) | yes |
| 1 | rotation | 16 bytes if bit 3 clear, else 4 | yes |
| 2 | scale | 24 bytes | yes |
| 3 | selects the rotation form | | |

so a record is one of 4, 8, 16, 20, 28, 32, 40, 44 or 56 bytes. This was
**not** guessed: every model scanned decodes its record lengths so that
the last one ends exactly on `+0x04` (740/740 models), and the observed
`(flags, size)` pairs are

```
0x07 ->  4     0x06 -> 16     0x0d ->  8     0x0c -> 20     0x2c -> 20
0x4c -> 20     0x6c -> 20     0x8c -> 20     0x08 -> 44     0x18 -> 44
0x48 -> 44     0x88 -> 44     0x00 -> 56     0x04 -> 32     0x02 -> 40
0x03 -> 28
```

`bit 1` clear and `bit 3` clear means a 16-byte rotation; `bit 1` clear
and `bit 3` set means a 4-byte one. That split is why an earlier pass,
which assumed three components of 12/4/24 bytes, mis-sized the `0x00`
and `0x04` records by 12 bytes.

## 3. The rotation

There are two forms, and the 4-byte field is present in **both** (its
meaning does not change): it is two little-endian `s16`, the y and z of
the rotation matrix's **third row**, in the same 1.0 = 4096 fixed point
the fx32s use.

**The full form (bit 3 clear).** The third row is `(X, Rlow, Rhigh)`
where `X` is the record header's second halfword as a sign-extended
`s16`, and the first two rows are the three `s16` that follow the 4-byte
field. (Reading `X` as unsigned puts a `0xF31D` third row x at +62237
instead of -3299 and fails the cross-product check on the nodes whose x
is negative; the signed read matches 21 of 21.) So the format stores 8 of the
9 matrix entries and the last is implied by orthonormality. The
cross-check is direct and decisive: 21 nodes across the corpus carry both
the 4-byte field and the 12-byte rows, and for **21 of 21** the 4-byte
halves equal the third row of the matrix the two stored rows imply, and
`X` equals its x. One example, `staffroll_b`'s `l_claw_thumb`:

```
04 f8 66 0c | 9a8e0000 04e5ffff 580a0000 | d0f6c6fb | b208 190d 09fd 2a05 0000 250f
 header X=0x0c66   translation (8.9,-1.69,0.65)   R         D (three s16 x2)
```

giving `row0 = (0x08b2, 0x0d19, -0x02f7)`, `row1 = (0x052a, 0, 0x0f25)`,
`row2 = (0x0c66, -0x0930, -0x043a)`, all unit and mutually orthogonal.

**The packed form (bit 3 set).** Only two of the three components are
stored, and across 217 such nodes `(Rlow, Rhigh)/4096` is always a
**unit** pair. What those two components *are* is not settled by the node
data. The engine reads them as the third row's y and z and completes the
matrix as the x-turn that takes z there (`row0 = (1,0,0)`,
`row1 = (0, z, -y)`, `row2 = (0, y, z)`), so `(0, 4096)` is the
identity. **This completion is an assumption, not a verified decode.**
Two searches were run and neither supports it:

- across all 740 models scanned, **0** joints are packed in one model and
  full in another (rotation form is fixed per joint), so there is no twin
  whose full matrix could fix the missing component;
- of the 21 full rotations only 2 have `row2.x == 0`, and neither of
  those has `row0 == (1,0,0)`, so the zero-roll family the completion
  assumes is not what the full form actually uses.

What would settle it: the paired BCA0 joint animation's frame 0 for a
packed joint (the companions exist, `staffroll_b`/`staffroll_w` in
`a/2/2/0`), or the ARM9 routine that expands a node record into a matrix.

**Validation.** The full form's rows are orthonormal and reconstruct to a
consistent matrix (**1163 / 1163 orthonormal over the whole corpus,
21 / 21 cross-product exact to 1 LSB**), but that says nothing about the
packed form: the completion above is orthonormal by construction, so
orthonormality cannot test it.

Readings that were tried and falsified as the reading of the 4-byte
field, with why:

- **three s10 in the GX `NORMAL` packing**: the unpacked vectors have
  magnitudes 60..620, not a fixed scale, and one exceeds the s10 maximum
  for a unit quaternion's xyz, so it is neither a unit vector nor a
  quaternion's xyz.
- **two `s16` angles**: it cannot express a general orientation and is
  not what the 21 paired records show; those pin it to the third row.
- **three s10 Euler angles**: any Euler decode is orthonormal by
  construction, so orthonormality cannot validate it, and it contradicts
  the paired records.
- **a `u16` pivot / index**: the paired records show the bytes are matrix
  entries.
- **a non-4096 fixed point**: the 21 paired records match the stored
  `s16` to within one unit at 4096 = 1.0, so the scale is 4096.

## 4. The scale

The 24-byte component is three `fx32` scale factors followed by their
reciprocals. `S[3+i]` equals `round(2^24 / S[i])` in **534 of 537**
samples; the three exceptions are one node of `cannon_01_00` whose scale
is 41 (1/100) and whose inverse is stored as 409191 rather than the
rounded 409200, a difference of 9 units in 16.7 million, i.e. the
reciprocal was computed by a slightly coarser fixed-point division. One
sample (`m_gym0202_00_00`'s `pasted__trade6`) is an all-zero scale,
matching its all-zero rows. Identity is `(4096, 4096, 4096)`.

Examples: `batt_stage01`'s node has scale `(1.0, 0.586, 1.0)` and inverse
`(1.0, 1.705, 1.0)`; `C3_00_01`'s node 1 has `(1.318, 1, 1)` and
`(0.759, 1, 1)`.

The `local` matrix is `scale * rotation * translation` in the row-vector
convention (`v' = v * local`), which is what `g3d_render_model` applies.

## 5. The parent/child hierarchy

The hierarchy is in the render-command section, not in the node record.
It is a sequence of variable-length records that begin with a **node
record introducer** `0x26`, `0x66` or `0x46`:

```
26 <node> <parent> <flags> <node>
66 <node> <parent> <flags> <node> ... 
46 <node> <parent> <flags> <node> ...
```

The node index appears first and again in the record; the byte after it
is the **parent index**, and `parent == node` marks the root. The
different introducer bytes are three encodings of the same record (the
`0x66` / `0x46` forms carry one more field), not three meanings.

**Validation** over the 670 models whose render-command section has such
records: every parent is in range (**670 / 670**) and the graph is
acyclic when the self-parent root is removed (**670 / 670**). The tree
itself is the sharpest evidence: `staffroll_b`, a biped, parents
`l_foot -> l_shin -> l_thigh -> pelvis`, `l_hand -> l_forearm -> l_bicep
-> l_shoulder -> chest`, `head -> neck -> chest`, and so on, which is the
skeleton its node names describe. A count of the introducer records is
within one of the node count in every multi-node model, because a flat
map model has a root plus children and the root's own record doubles as
the terminator.

## 6. The shape -> node binding

The same render-command stream carries the draw list. The opcodes are
`(operand, opcode)` pairs: `0x04` (and its `0x24` / `0x44` variants) bind
a material and `0x05` draws a shape, under whatever node record was last
seen; `0x2b` ends a node's block. So a shape drawn between two node
records is bound to the first, and a shape drawn under several nodes is
**instanced** once per draw.

**Validation** over the same 670 models: every shape that is drawn binds
to a node that exists and is in the shape dictionary's range (0 bad
bindings), and in **636 of 670** models every declared shape is drawn at
least once. The remaining 34 draw a subset (for example `m_pc01_00_00`
draws 19 of its 23 shapes); a shape that is not in the render-command
list is simply not drawn by the game, which is the format's authority on
what is visible. As an example, `mus_stage01` binds its 28 shapes to 16
of its 31 nodes with 56 draws, e.g. `polySurface71` under node 4 and
`pCylinder48/49` under nodes 1/2.

## 7. What the engine does with it

`engine/g3d/nsbmd.c` parses the node dictionary into `G3dModelNode`
(name, flags, the presence bits, the form bit, `pivot`, translation,
scale, unit rotation matrix, `local`, `world`), parses the
render-command section for the parents and the draw list, and composes
`world = local * parent->world`. `g3d_render_model` follows the draw
list, applying each shape's node world matrix (and therefore its
ancestors') before the camera.

`tests/test_g3d.c` checks, and ctest runs:

- the synthetic pipeline (`synthetic_nodes`): a hand-built one-triangle
  model driven through translation, a 2x x scale, a quarter turn about
  z, a two-node draw list, and a parent+child composition, with every
  expected pixel and matrix entry computed on paper;
- the byte decode (`real_node_checks`): `m_pc01_00_00`'s four nodes,
  names, translations and flat tree; `m_ph01_00_00`'s packed
  `pCube4`/`polySurface157` rotations; `staffroll_b`'s full rotation
  entries and its five-deep bone tree; `batt_stage01`'s scale;
- the real render (`node_render_check`): `pl_00_00` rendered at the house
  camera (21239 of 49152 pixels covered) and the same model under a
  2-unit node pose override, which moves 10637 pixels.

Measured on this tree: `ctest` **13 / 13** and `test_g3d` **133 checks,
0 failures**, in both the LP64 and the ILP32 configuration.

## 8. What is not decoded

- **The packed rotation's missing component and its roll completion.** The
  two stored components are a verified unit pair, but their meaning and
  the reconstruction of the third row are assumed (see "The packed form"
  above); a packed joint's orientation is therefore provisional. This is
  the one part of the transform that is not on byte evidence.

Two things in the render-command stream are recorded but not interpreted,
and neither is needed for the transform or the visibility:

- the node record's second flags byte and the flags byte's bits 4-7;
- the header halfword's meaning for the **packed** rotation, where it
  is not the third row's x (it is the third row's x for the full form).
  It is exposed as `G3dModelNode.pivot` and not used.

`0x2b`'s operand and the exact role of the `0x24` / `0x44` bind-material
variants are likewise not interpreted; the material bindings are not
needed for geometry, and the shape opcode is unambiguous.
