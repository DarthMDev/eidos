#!/usr/bin/env python3
"""
synth_rom.py -- a synthetic DS cartridge image, and the reference extraction
of it, for testing the first-run extractor without anybody's ROM.

    python tools/synth_rom.py --aletheia-root ../aletheia --out build/synth

writes into --out:

    synth.nds          the image
    oracle/            what aletheia's tools/ndsrom.py extract writes for it,
                       plus arm9_dec.bin and overlays_dec/ from tools/blz.py
                       (what tools/delink.py writes)
    oracle_files.txt   one line per oracle file the C extractor must also
                       write: "<sha1> <size> <relative path>"; ndsrom.py's
                       rebuild bookkeeping (manifest.json, residue/) is left
                       out, because the C extractor deliberately does not
                       write it
    expect.txt         the counts the C extractor should report

Nothing here is game data.  Every byte is generated from a fixed seed, and
the layout is chosen to cover what the extractor has to get right rather
than to look like a real cartridge: nested directories, names that need
sanitising (a reserved Windows name, characters Windows refuses, a trailing
dot and space, a byte outside ASCII), overlay files in both the ARM9 and the
ARM7 tables, a BLZ-compressed ARM9 overlay, a FAT entry nothing names, an
empty FAT slot, a zero-length file, a DSi (TWL) header with arm9i/arm7i and
both digest tables, an animated-icon banner, a non-padding blob between
regions (ndsrom.py's residue), a correct header checksum and capacity byte,
and an ARM9 binary packed by aletheia's own BLZ compressor with a
0x4000-byte verbatim prefix, as the SDK packs a real one.

aletheia is read, never written: tools/blz.py compresses the ARM9 image and
tools/ndsrom.py is run as a program to produce the oracle.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import random
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ALETHEIA = os.path.normpath(os.path.join(ROOT, "..", "aletheia"))

ROM_SIZE = 0x100000
SEED = 0x45494453          # 'EIDS'


def header_crc(h):
    """CRC-16 of header bytes 0x000..0x15D (polynomial 0xA001 reflected,
    initial 0xFFFF): the checksum at 0x15E that the console verifies."""
    crc = 0xFFFF
    for b in h[:0x15E]:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build_rom(blz):
    rnd = random.Random(SEED)
    rom = bytearray(b"\xff" * ROM_SIZE)
    cursor = [0x4000]      # the header area is 0x4000

    def place(data, align=0x200):
        off = (cursor[0] + align - 1) & ~(align - 1)
        rom[off:off + len(data)] = data
        cursor[0] = off + len(data)
        return off

    def noise(n):
        return bytes(rnd.getrandbits(8) for _ in range(n))

    # --- ARM9: 0x4000 of "secure area", then compressible code-like data --
    arm9_img = bytearray(noise(0x4000))
    for i in range(0x3000):
        arm9_img += struct.pack("<I", (0xE1A00000 | (i % 17) << 12) if i % 5 else i * 0x1234567 & 0xFFFFFFFF)
    arm9_img += noise(0x800)
    arm9_img += b"eidos synthetic arm9 image\x00" * 64
    arm9_img = bytes(arm9_img)
    arm9 = blz.compress(arm9_img, blz.ARM9_SECURE_AREA)
    assert blz.decompress(arm9, 0) == arm9_img
    arm9_off = place(arm9)

    arm7 = noise(0x3000)
    arm7_off = place(arm7)

    # --- files ---------------------------------------------------------
    # file ids 0..2 are overlays (two ARM9, one ARM7), 3.. NitroFS files,
    # then one orphan and one empty slot.
    # Overlay 17 is BLZ-compressed, as most of the real cartridge's are: its
    # table entry carries the decompressed size and the compressed flag, and
    # the oracle's overlays_dec/ holds what tools/delink.py would decompress
    # it to.
    ov17_img = bytes(noise(0x80)) + b"".join(
        struct.pack("<I", 0xE59F0000 | (i % 13) << 2) for i in range(0x180))
    ov17 = blz.compress(ov17_img, 0)
    assert len(ov17) < len(ov17_img)
    overlay_blobs = [noise(0x600), ov17, noise(0x220)]
    fs_tree = [
        # (path components, size)
        (["a", "0", "0", "0"], 0x1200),
        (["a", "0", "0", "1"], 0x40),
        (["a", "0", "0", "2"], 0),                      # zero-length
        (["a", "0", "1", "0"], 0x333),                  # not a multiple of 4
        (["a", "1", "deep", "er", "leaf.bin"], 0x80),
        (["CON.txt"], 0x10),                            # reserved on Windows
        (["we<ird>:na|me?.bin"], 0x20),                 # refused characters
        (["trail. "], 0x08),                            # trailing dot, space
        ([b"caf\xe9".decode("latin-1")], 0x0C),         # a non-ASCII byte
        (["title.narc"], 0x900),
    ]

    # Directory tree from the paths: dir index 0 is the root.
    dirs = {(): []}                 # dir tuple -> ordered entries
    order = [()]
    for comps, _size in fs_tree:
        for depth in range(1, len(comps)):
            d = tuple(comps[:depth])
            if d not in dirs:
                dirs[d] = []
                order.append(d)
                parent = tuple(comps[:depth - 1])
                dirs[parent].append(("dir", comps[depth - 1], d))
        dirs[tuple(comps[:-1])].append(("file", comps[-1], None))
    # Files must be numbered contiguously per directory; assign in the order
    # the FNT will list them.
    first_fid = {}
    fid_of = {}
    next_fid = len(overlay_blobs)
    for d in order:
        first_fid[d] = next_fid
        for kind, name, _ in dirs[d]:
            if kind == "file":
                fid_of[d + (name,)] = next_fid
                next_fid += 1
    sizes = {tuple(c): s for c, s in fs_tree}

    file_data = {}
    for comps, size in fs_tree:
        file_data[fid_of[tuple(comps)]] = noise(size)
    orphan_fid = next_fid
    empty_fid = next_fid + 1
    fat_count = next_fid + 2
    fat = [(0, 0)] * fat_count
    for i, blob in enumerate(overlay_blobs):
        off = place(blob)
        fat[i] = (off, off + len(blob))
    for fid in sorted(file_data):
        data = file_data[fid]
        off = place(data) if data else cursor[0]
        fat[fid] = (off, off + len(data))
    orphan = noise(0x150)
    off = place(orphan)
    fat[orphan_fid] = (off, off + len(orphan))
    fat[empty_fid] = (0, 0)

    # --- FNT -----------------------------------------------------------
    dir_index = {d: i for i, d in enumerate(order)}
    subtables = []
    for d in order:
        st = bytearray()
        for kind, name, sub in dirs[d]:
            raw = name.encode("latin-1")
            if kind == "file":
                st.append(len(raw))
                st += raw
            else:
                st.append(0x80 | len(raw))
                st += raw
                st += struct.pack("<H", 0xF000 | dir_index[sub])
        st.append(0)
        subtables.append(bytes(st))
    main = bytearray()
    sub_off = 8 * len(order)
    for i, d in enumerate(order):
        parent = len(order) if i == 0 else 0xF000 | dir_index[d[:-1]]
        main += struct.pack("<IHH", sub_off, first_fid[d], parent)
        sub_off += len(subtables[i])
    fnt = bytes(main) + b"".join(subtables)
    fnt_off = place(fnt)
    fat_bytes = b"".join(struct.pack("<II", s, e) for s, e in fat)
    fat_off = place(fat_bytes)

    # --- overlay tables (32-byte entries) ------------------------------
    def ov_entry(ov_id, fid, load, size=None, last=0):
        if size is None:
            size = fat[fid][1] - fat[fid][0]
        return struct.pack("<8I", ov_id, load, size, 0x40, 0, 0, fid, last)
    y9 = (ov_entry(0, 0, 0x021D0000)
          + ov_entry(17, 1, 0x021E0000, len(ov17_img), (1 << 24) | len(ov17)))
    y7 = ov_entry(3, 2, 0x037F8000)
    y9_off = place(y9)
    y7_off = place(y7)

    # --- banner (DSi animated icon, version 0x0103, 0x23C0 bytes) --------
    banner = bytearray(noise(0x23C0))
    banner[0:2] = struct.pack("<H", 0x0103)
    banner_off = place(bytes(banner))

    # --- a residue blob, then the DSi regions --------------------------
    place(b"residue: not a modelled region" + noise(40), align=0x40)
    arm9i = noise(0x1400)
    arm7i = noise(0x800)
    arm9i_off = place(arm9i, align=0x1000)
    arm7i_off = place(arm7i)
    dsect = noise(0x300)
    dblock = noise(0x100)
    dsect_off = place(dsect)
    dblock_off = place(dblock)
    assert cursor[0] < ROM_SIZE

    # --- header --------------------------------------------------------
    h = bytearray(noise(0x4000))            # header_ext is whatever
    h[0:0x200] = b"\x00" * 0x200
    h[0x000:0x00C] = b"POKEMON B\x00\x00\x00"
    h[0x00C:0x010] = b"IRBO"
    h[0x010:0x012] = b"01"
    h[0x012] = 0x02                          # NTR + TWL
    h[0x014] = 3                             # capacity 128 KiB << 3 = ROM_SIZE
    struct.pack_into("<4I", h, 0x20, arm9_off, 0x02000800, 0x02000000, len(arm9))
    struct.pack_into("<4I", h, 0x30, arm7_off, 0x02380000, 0x02380000, len(arm7))
    struct.pack_into("<4I", h, 0x40, fnt_off, len(fnt), fat_off, len(fat_bytes))
    struct.pack_into("<4I", h, 0x50, y9_off, len(y9), y7_off, len(y7))
    struct.pack_into("<I", h, 0x68, banner_off)
    struct.pack_into("<I", h, 0x80, ROM_SIZE)
    struct.pack_into("<I", h, 0x84, 0x4000)
    struct.pack_into("<I", h, 0x1C0, arm9i_off)
    struct.pack_into("<I", h, 0x1C8, 0x02400000)
    struct.pack_into("<I", h, 0x1CC, len(arm9i))
    struct.pack_into("<I", h, 0x1D0, arm7i_off)
    struct.pack_into("<I", h, 0x1D8, 0x02E80000)
    struct.pack_into("<I", h, 0x1DC, len(arm7i))
    struct.pack_into("<4I", h, 0x1F0, dsect_off, len(dsect), dblock_off, len(dblock))
    struct.pack_into("<I", h, 0x208, 0x23C0)
    struct.pack_into("<I", h, 0x210, ROM_SIZE)
    struct.pack_into("<H", h, 0x15E, header_crc(h))
    rom[0:0x4000] = h

    counts = {
        "fs_files": len(fs_tree),
        "overlays": len(overlay_blobs),
        "orphans": 1,
        "arm9_dec_size": len(arm9_img),
        "overlays_dec": 1,
    }
    return bytes(rom), arm9_img, {17: ov17_img}, counts


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--aletheia-root", default=DEFAULT_ALETHEIA)
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)

    tools = os.path.join(os.path.abspath(args.aletheia_root), "tools")
    sys.path.insert(0, tools)
    import blz  # noqa: E402  (aletheia's tools/blz.py)

    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    rom, arm9_img, ov_imgs, counts = build_rom(blz)
    rom_path = os.path.join(out, "synth.nds")
    with open(rom_path, "wb") as fh:
        fh.write(rom)

    oracle = os.path.join(out, "oracle")
    shutil.rmtree(oracle, ignore_errors=True)
    subprocess.run([sys.executable, os.path.join(tools, "ndsrom.py"), "extract",
                    rom_path, "-o", oracle], check=True, stdout=subprocess.DEVNULL)
    with open(os.path.join(oracle, "arm9.bin"), "rb") as fh:
        dec = blz.decompress(fh.read(), 0)
    assert dec == arm9_img
    with open(os.path.join(oracle, "arm9_dec.bin"), "wb") as fh:
        fh.write(dec)

    # overlays_dec/: tools/delink.py's step 3, the same call it makes.
    with open(os.path.join(oracle, "y9.bin"), "rb") as fh:
        y9 = fh.read()
    for base in range(0, len(y9), 32):
        ov_id, _ram, size = struct.unpack_from("<3I", y9, base)
        last = struct.unpack_from("<I", y9, base + 28)[0]
        if not (last >> 24) & 1:
            continue
        with open(os.path.join(oracle, "overlays", "y9_%04d.bin" % ov_id), "rb") as fh:
            raw = fh.read()
        img = blz.decompress(raw[:last & 0xFFFFFF], 0, expect_size=size)
        assert img == ov_imgs[ov_id]
        os.makedirs(os.path.join(oracle, "overlays_dec"), exist_ok=True)
        with open(os.path.join(oracle, "overlays_dec", "y9_%04d.bin" % ov_id), "wb") as fh:
            fh.write(img)

    lines = []
    for dirpath, _dirs, files in os.walk(oracle):
        for f in files:
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, oracle).replace(os.sep, "/")
            if rel == "manifest.json" or rel.startswith("residue/"):
                continue
            with open(full, "rb") as fh:
                data = fh.read()
            lines.append("%s %d %s" % (hashlib.sha1(data).hexdigest(), len(data), rel))
    lines.sort(key=lambda l: l.split(" ", 2)[2])
    with open(os.path.join(out, "oracle_files.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    with open(os.path.join(out, "expect.txt"), "w", encoding="utf-8") as fh:
        fh.write("sha1 %s\n" % hashlib.sha1(rom).hexdigest())
        fh.write("files %d\n" % len(lines))
        for k in ("fs_files", "overlays", "overlays_dec", "orphans", "arm9_dec_size"):
            fh.write("%s %d\n" % (k, counts[k]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
