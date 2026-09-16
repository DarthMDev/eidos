#!/usr/bin/env python3
"""
tools/bg_oracle.py -- generate the pixel oracle for engine/g2d's BG half.

The oracle is aletheia's own asset tool, `tools/assets/background.py`: the
NSCR decoder and tilemap renderer written independently of this engine,
in the decompilation project, against the archives' own bytes. This
script imports it out of the aletheia checkout (nothing is copied here,
and aletheia is never modified), renders each requested screen, and
writes three things into the output directory:

    NAME.png        the PNG background.py itself would write
    NAME.rgba       the same image as a raw dump the C test reads
    screens.txt     the manifest: for every screen, the NCGR and NCLR
                    sub-file background.py's coverage rule chose

The manifest matters as much as the pixels. The NCGR/NCLR pairing is a
property of how the archives are laid out, not of the DS hardware, and
background.py's rule for it (nearest sub-file by index distance whose
tile count covers the screen's maximum referenced tile index, and whose
colour count covers its maximum referenced palette row) is documented in
that module and in aletheia's docs/ASSETS.md. The C engine does not
re-derive it: the test reads the pair the oracle chose and renders the
same three sub-files, so what is compared is the rendering and nothing
else.

The raw dump's framing is the same one tools/rig_oracle.py uses, so
neither side needs a PNG decoder or any third-party code:

    0x00  'RGBA'          4 bytes, ASCII
    0x04  u32 width       little-endian
    0x08  u32 height      little-endian
    0x0C  width*height*4  R, G, B, A bytes, row-major, top row first

Every pixel is opaque: background.py's compose() paints palette index 0
as its palette colour rather than as transparent, because it renders one
layer on its own. The hardware treats index 0 as transparent and the
frame compositor does too; the single-layer path both sides compare here
does not.
"""

import argparse
import os
import struct
import sys


def load_background_module(aletheia_root):
    assets = os.path.join(aletheia_root, "tools", "assets")
    if not os.path.isdir(assets):
        raise SystemExit(f"aletheia asset tools not found at {assets}")
    sys.path.insert(0, assets)
    import narc  # noqa: E402
    import background  # noqa: E402
    import battle_sprite  # noqa: E402  (background.py's own PNG writer)
    return narc, background, battle_sprite


def write_rgba(path, width, height, rgb):
    out = bytearray()
    out += b"RGBA"
    out += struct.pack("<II", width, height)
    for r, g, b in rgb:
        out += bytes((r, g, b, 255))
    with open(path, "wb") as fh:
        fh.write(out)


def safe_name(archive, index):
    stem = archive.replace(".narc", "").replace("/", "_")
    return f"{stem}_{index}"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--aletheia-root", required=True)
    ap.add_argument("--data-root", required=True,
                    help="the extracted NitroFS tree (the one with a/0/0/1)")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("screens", nargs="+",
                    help="one or more ARCHIVE#INDEX, e.g. a/1/4/3#22")
    args = ap.parse_args(argv)

    narc, bg, bs = load_background_module(args.aletheia_root)
    os.makedirs(args.out, exist_ok=True)

    archives = {}
    manifest = []
    for spec in args.screens:
        archive, _, index_s = spec.partition("#")
        index = int(index_s)
        if archive not in archives:
            path = os.path.join(args.data_root, *archive.split("/"))
            n = narc.parse_file(path)
            archives[archive] = (n,
                                 bg.find_magic_indices(n, b"RGCN"),
                                 bg.find_magic_indices(n, b"RLCN"),
                                 {})
        n, ncgr_i, nclr_i, cache = archives[archive]

        data, _kind = bg.read_decompressed(n, index)
        nscr = bg.parse_nscr(data)
        ncgr, palette, gi, li = bg.load_pair_for(n, nscr, index, ncgr_i,
                                                 nclr_i, cache)
        w, h, rgb = bg.compose(nscr, ncgr, palette)

        name = safe_name(archive, index)
        bs.write_png_rgb8(os.path.join(args.out, name + ".png"), w, h, rgb)
        write_rgba(os.path.join(args.out, name + ".rgba"), w, h, rgb)
        bpp = 4 if ncgr.bytes_per_tile == 32 else 8
        manifest.append((name, archive, index, gi, li, w, h, bpp,
                         len(palette), len(set(rgb))))
        print(f"[*] {archive}#{index}: {w}x{h} {bpp}bpp, NCGR#{gi}, NCLR#{li}, "
              f"{len(set(rgb))} distinct colours -> {name}.rgba")

    with open(os.path.join(args.out, "screens.txt"), "w", encoding="ascii") as fh:
        fh.write("# name archive index ncgr nclr width height bpp "
                 "palette_colours distinct_colours\n")
        for row in manifest:
            fh.write(" ".join(str(v) for v in row) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
