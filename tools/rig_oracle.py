#!/usr/bin/env python3
"""
tools/rig_oracle.py -- generate the pixel oracle for engine/g2d.

The oracle is aletheia's own asset tool, `tools/assets/rig.py`: the same
compositor that produced every rig render in that project's asset lane,
written independently of the C engine and from the same attested
formats. This script imports it out of the aletheia checkout (nothing is
copied here, and aletheia is never modified), composes the rest pose of
each requested species, and writes two files per species into the output
directory:

    NNNN_pose.png    the PNG rig.py itself would write
    NNNN_pose.rgba   the same image as a raw dump the C test reads

The raw dump exists so the test needs no PNG decoder and no third-party
code on either side. Its framing is deliberately trivial:

    0x00  'RGBA'          4 bytes, ASCII
    0x04  u32 width       little-endian
    0x08  u32 height      little-endian
    0x0C  width*height*4  R, G, B, A bytes, row-major, top row first

The colour convention is rig.py's own `indices_to_rgb`, which the C side
reproduces in `g2d_pose_to_rgba`: palette index 0 is the transparent
index and is written as opaque (16, 16, 16), an index past the end of the
palette is written as magenta, and every pixel is opaque. Nothing in the
comparison then depends on how either side spells "transparent".
"""

import argparse
import os
import struct
import sys


def load_rig_module(aletheia_root):
    assets = os.path.join(aletheia_root, "tools", "assets")
    if not os.path.isdir(assets):
        raise SystemExit(f"aletheia asset tools not found at {assets}")
    sys.path.insert(0, assets)
    import narc  # noqa: E402
    import rig  # noqa: E402
    import battle_sprite  # noqa: E402  (rig.py's own PNG writer)
    return narc, rig, battle_sprite


def write_rgba(path, width, height, rgb):
    out = bytearray()
    out += b"RGBA"
    out += struct.pack("<II", width, height)
    for r, g, b in rgb:
        out += bytes((r, g, b, 255))
    with open(path, "wb") as fh:
        fh.write(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--aletheia-root", required=True)
    ap.add_argument("--data-root", required=True,
                    help="the extracted NitroFS tree (the one with a/0/0/4)")
    ap.add_argument("--archive", default="a/0/0/4")
    ap.add_argument("--block-size", type=int, default=20)
    ap.add_argument("--group", type=int, default=0)
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("species", type=int, nargs="+")
    args = ap.parse_args(argv)

    narc, rig, bs = load_rig_module(args.aletheia_root)
    archive = os.path.join(args.data_root, *args.archive.split("/"))
    n = narc.parse_file(archive)
    os.makedirs(args.out, exist_ok=True)

    for species in args.species:
        block_start = species * args.block_size
        (ncgr_data, ncer_data, nmcr_data, nanr_data,
         _nclr_data, nclr_all) = rig.load_rig_block(n, block_start, args.block_size)
        ncgr = rig.parse_ncgr(ncgr_data)
        ncer = rig.parse_ncer(ncer_data)
        nmcr = rig.parse_nmcr(nmcr_data)
        nanr = rig.parse_nanr(nanr_data) if nanr_data is not None else None
        palette = rig.build_bank_palette(nclr_all) or rig.DEFAULT_PALETTE
        nodes = nmcr.groups[args.group].nodes

        w, h, flat = rig.compose_pose(ncgr, ncer, nodes, nanr=nanr, t=0,
                                      n_pal_colors=len(palette))
        rgb = rig.indices_to_rgb(flat, palette)

        png_path = os.path.join(args.out, f"{species:04d}_pose.png")
        raw_path = os.path.join(args.out, f"{species:04d}_pose.rgba")
        # The PNG writer is rig.py's own (through battle_sprite), so the
        # image on disk is byte-identical to what the asset lane produces.
        bs.write_png_rgb8(png_path, w, h, rgb)
        write_rgba(raw_path, w, h, rgb)
        print(f"[*] species {species}: {w}x{h} -> {os.path.basename(raw_path)}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
