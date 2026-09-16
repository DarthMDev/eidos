#!/usr/bin/env python3
"""
tools/g3d_oracle.py -- generate the geometry oracle for engine/g3d.

The oracle is aletheia's own asset lane: `tools/assets/nsbmd.py` (the
NSBMD/NSBTX container, resource dictionary, MDL0 shape dictionary,
TEX0 texture records and the GX display-list decoder) and
`tools/assets/map_shapes.py` (the per-shape textured OBJ export built on
top of it).  Both were written in the decompilation project against the
archives' own bytes, independently of this engine.  This script imports
them out of the aletheia checkout -- nothing is copied here, and aletheia
is never modified -- and writes, into the build directory:

    NAME.obj              the per-shape OBJ map_shapes.py itself writes
    NAME.mtl              its material file
    NAME_textures/*.png   the textures it resolved
    models.txt            the manifest tests/test_g3d.c reads

The manifest is what makes the comparison well posed.  map_shapes.py
writes an OBJ whose `vt` lines are already divided by each shape's own
texture size and flipped in v (`1.0 - v/th`), which is the OBJ
convention, not the hardware's: the DS's TEXCOORD command carries texel
units in s16.4 fixed point.  The manifest records, per shape, the texture
name and the (width, height) the divide used, so the C test can undo it
exactly and compare in the units the engine actually computes in.

    # model <name> <archive> <index> <n_shapes>
    # shape <shape_index> <tex_name|-> <tex_w> <tex_h> <n_verts> <n_faces>

It also records, per model and over the whole a/0/1/4 texture archive,
which GX texture formats actually occur, which is the survey the README's
engine/g3d section quotes.

Nothing about the geometry is recomputed here.  The OBJ is the oracle; the
C engine decodes the same display lists through its own matrix stack and
vertex pipeline and has to land on the same numbers.
"""

import argparse
import os
import sys


def load_aletheia(aletheia_root):
    assets = os.path.join(aletheia_root, "tools", "assets")
    if not os.path.isdir(assets):
        raise SystemExit(f"aletheia asset tools not found at {assets}")
    sys.path.insert(0, assets)
    import narc  # noqa: E402
    import nsbmd  # noqa: E402
    import map_shapes  # noqa: E402
    return narc, nsbmd, map_shapes


# (model name as the model dict spells it, a/0/0/8 sub-file index, what it is)
DEFAULT_TARGETS = [
    ("pl_00_00", 120, "a player's house interior, 40 shapes"),
    ("map24_23", 0, "an outdoor terrain chunk"),
]


def survey_formats(nsbmd, narc, data_root, limit=None):
    """Count which GX texture formats occur across a/0/1/4, the shared
    terrain/building texture archive every map model draws from."""
    path = os.path.join(data_root, "a", "0", "1", "4")
    n = narc.parse_file(path)
    counts = {}
    sizes = {}
    total = 0
    end = n.file_count if limit is None else min(limit, n.file_count)
    for i in range(end):
        raw = n.read(i)
        if raw[:4] not in (b"BTX0", b"BMD0"):
            continue
        try:
            c = nsbmd.parse_container(raw)
            tex0 = nsbmd.find_tex0(c)
            if tex0 is None:
                continue
            for tr in tex0.texture_records():
                counts[tr.format] = counts.get(tr.format, 0) + 1
                sizes.setdefault(tr.format, set()).add((tr.width, tr.height))
                total += 1
        except Exception:  # noqa: BLE001  a sub-file this reader cannot parse
            continue
    return total, counts, sizes


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--aletheia-root", required=True)
    ap.add_argument("--data-root", required=True,
                    help="the extracted NitroFS tree (the one with a/0/0/8)")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--no-survey", action="store_true",
                    help="skip the archive-wide texture format survey")
    ap.add_argument("targets", nargs="*",
                    help="NAME:INDEX pairs; default is the two the test uses")
    args = ap.parse_args(argv)

    narc, nsbmd, map_shapes = load_aletheia(args.aletheia_root)
    os.makedirs(args.out, exist_ok=True)

    if args.targets:
        targets = []
        for spec in args.targets:
            name, _, idx = spec.partition(":")
            targets.append((name, int(idx), ""))
    else:
        targets = DEFAULT_TARGETS

    a008 = narc.parse_file(os.path.join(args.data_root, "a", "0", "0", "8"))
    print("[*] indexing a/0/1/4 (texture name scan)...")
    tex_archive = map_shapes.TextureArchive(
        os.path.join(args.data_root, "a", "0", "1", "4"))
    print(f"[*] {len(tex_archive.name_to_subfile)} texture names indexed")

    lines = []
    for expect, index, _what in targets:
        name, buf = map_shapes.load_model(a008, index)
        if name != expect:
            print(f"[!] a/0/0/8#{index} is '{name}', expected '{expect}'")
        result = map_shapes.export_textured(name, buf, tex_archive, args.out,
                                            name)
        lines.append(f"model {name} a/0/0/8 {index} {result['n_shapes']}")
        # face_report is (shape_idx, texname, n_verts, n_faces) in shape order
        for shape_idx, texname, nv, nf in result["face_report"]:
            wh = tex_archive.get(texname) if texname else None
            tw, th = (wh[0], wh[1]) if wh else (1, 1)
            lines.append(f"shape {shape_idx} {texname or '-'} {tw} {th} "
                         f"{nv} {nf}")
        nv = sum(v for _, _, v, _ in result["face_report"])
        nf = sum(f for _, _, _, f in result["face_report"])
        print(f"[*] {name} (a/0/0/8#{index}): {result['n_shapes']} shapes, "
              f"{nv} vertices, {nf} faces -> {os.path.basename(result['obj'])}")

    if not args.no_survey:
        total, counts, sizes = survey_formats(nsbmd, narc, args.data_root)
        lines.append(f"survey a/0/1/4 {total}")
        for fmt in sorted(counts):
            dims = sorted(sizes[fmt])
            lines.append(f"format {fmt} {nsbmd.FORMAT_NAMES[fmt]} "
                         f"{counts[fmt]} {len(dims)}")
            print(f"[*] format {fmt} ({nsbmd.FORMAT_NAMES[fmt]}): "
                  f"{counts[fmt]} textures, {len(dims)} distinct sizes")
        print(f"[*] {total} textures surveyed across a/0/1/4")

    with open(os.path.join(args.out, "models.txt"), "w", encoding="ascii") as fh:
        fh.write("# model NAME ARCHIVE INDEX N_SHAPES\n")
        fh.write("# shape INDEX TEXNAME TEXW TEXH N_VERTS N_FACES\n")
        fh.write("# format ID NAME COUNT DISTINCT_SIZES\n")
        for line in lines:
            fh.write(line + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
