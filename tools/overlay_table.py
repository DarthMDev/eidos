#!/usr/bin/env python3
"""overlay_table.py -- the overlay registry's table, generated from aletheia.

Reads, in the aletheia checkout:

  layout/layout.json        the 237 overlay table entries: RAM address, code
                            and .bss sizes, static-initialiser range, file id,
                            compression word (the same fields as y9.bin)
  layout/matched/ov*/*.json the manifest shards: which functions of which
                            overlay are matched, and in which source file
  layout/symbols_sdk.txt    SDK functions libmatch recovered inside overlays
  src/matched/**            only the `symbols:` header blocks of the matched
                            sources that belong to an overlay, for the
                            functions they call inside their own overlay

and, optionally, the list of matched sources this build compiles (one path
per line), which decides which functions are PRESENT.  Writes a C file
defining `bw_overlay_table_generated` (sdk/overlay.h).  Addresses, sizes and
names only: no game bytes are read and none are written.

    python tools/overlay_table.py --aletheia-root ../aletheia --out overlay_table.c \\
        [--sources build_sources.txt]
    python tools/overlay_table.py --aletheia-root ../aletheia --summary

Why generated at build time and not committed: the table's interesting
column (matched and present coverage) moves with every aletheia wave and
with every source added to the build, and the build already requires both
the aletheia checkout and Python.  A committed copy would be stale the day
after it was made, and a 1 MB diff per wave.
"""

import argparse
import glob
import json
import os
import re
import sys

ADDR_NAME = re.compile(r"^(?:[A-Za-z0-9]+_)?(?:func|sub|unit)_([0-9A-Fa-f]{8})$")
SYMBOL_ROW = re.compile(
    r"^\s*\*\s+([A-Za-z_][A-Za-z0-9_]*)\s+(0x[0-9A-Fa-f]{8})\s+(thumb|arm)\b"
    r"(?:\s+(ov\d{3}|arm9\S*))?")
EXTERN_FUNC = re.compile(r"\bextern\b[^;]*?\b((?:func|sub)_([0-9A-Fa-f]{8}))\s*\(")
ANY_ROW = re.compile(r"^\s*\*\s+\S+\s+0x[0-9A-Fa-f]{8}\b")
SYMBOLS_HDR = re.compile(r"^\s*\*\s*symbols:\s*$")
REGION = re.compile(r"^ov(\d{3})$")

OPEN, MATCHED, PRESENT = 0, 1, 2

# The static-initialiser lists.  layout.json gives each overlay its list's
# range and length; the entries themselves are words inside the overlay
# image, which this script does not read.  236 of the 237 lists hold only
# the NULL terminator.  The one that does not is ov230's, whose five entries
# aletheia records by address in docs/MATCHING.md, "The one exception,
# ov230".  A list of any other length is an error here, not a guess.
KNOWN_SINIT = {
    230: [0x02188408, 0x021889B0, 0x02188CB0, 0x02189448, 0x02189908],
}


def norm(path):
    return path.replace("\\", "/")


def rel_matched(path):
    """'.../src/matched/ov/x.c' -> 'src/matched/ov/x.c', else None."""
    p = norm(path)
    i = p.find("src/matched/")
    return p[i:] if i >= 0 else None


def read_symbols_sdk(root):
    """{region: {address: name}} for SDK functions inside overlays."""
    out = {}
    path = os.path.join(root, "layout", "symbols_sdk.txt")
    with open(path, encoding="utf-8") as fp:
        for line in fp:
            if line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 4 or not re.fullmatch(r"[0-9A-Fa-f]{8}", parts[0]):
                continue
            if REGION.match(parts[3]):
                out.setdefault(parts[3], {})[int(parts[0], 16)] = parts[1]
    return out


def header_symbols(text):
    """(name, address, region or None) rows of a source's `symbols:` block,
    code only."""
    out = []
    in_block = False
    for line in text.splitlines():
        if SYMBOLS_HDR.match(line):
            in_block = True
            continue
        if not in_block:
            continue
        if not ANY_ROW.match(line):
            in_block = False
            continue
        m = SYMBOL_ROW.match(line)
        if m:
            out.append((m.group(1), int(m.group(2), 16), m.group(4)))
    return out


def extern_calls(text):
    """(name, address) of every func_/sub_<address> a source declares."""
    return [(m.group(1), int(m.group(2), 16)) for m in EXTERN_FUNC.finditer(text)]


def build(root, sources):
    with open(os.path.join(root, "layout", "layout.json"), encoding="utf-8") as fp:
        layout = json.load(fp)
    overlays = layout["overlays"]
    for i, o in enumerate(overlays):
        if o["id"] != i:
            raise SystemExit("layout.json: overlay %d is listed at index %d" % (o["id"], i))

    sdk = read_symbols_sdk(root)
    present_sources = set()
    for s in sources:
        r = rel_matched(s)
        if r is not None:
            present_sources.add(r)

    # region -> {address: [name, status]}
    funcs = {}
    region_sources = {}

    def note(region, addr, name, status):
        slot = funcs.setdefault(region, {})
        cur = slot.get(addr)
        if cur is None:
            slot[addr] = [name, status]
        else:
            if status > cur[1]:
                cur[1] = status
            if cur[0] is None or ADDR_NAME.match(cur[0] or ""):
                if name is not None and not ADDR_NAME.match(name):
                    cur[0] = name

    shard_glob = os.path.join(root, "layout", "matched", "ov*", "*.json")
    for path in sorted(glob.glob(shard_glob)):
        with open(path, encoding="utf-8") as fp:
            shard = json.load(fp)
        for e in shard.get("functions", []):
            region = e.get("region", "")
            if not REGION.match(region):
                continue
            src = norm(e.get("source", ""))
            status = PRESENT if src in present_sources else MATCHED
            region_sources.setdefault(region, set()).add(src)
            names = e.get("functions") or [e["name"]]
            covers = e.get("covers")
            base = int(e["address"], 16)
            for k, n in enumerate(names):
                m = ADDR_NAME.match(n)
                if covers and k < len(covers):
                    a = int(covers[k], 16)
                elif m:
                    a = int(m.group(1), 16)
                elif k == 0:
                    a = base
                else:
                    a = next((ad for ad, sn in sdk.get(region, {}).items() if sn == n), None)
                    if a is None:
                        raise SystemExit("%s: cannot place function %s" % (path, n))
                note(region, a, n, status)

    # SDK functions libmatch found in an overlay: known, matched or not.
    for region, rows in sdk.items():
        for a, n in rows.items():
            note(region, a, n, OPEN)

    # Calls a matched source makes inside its own overlay's code range.
    for region, srcs in region_sources.items():
        o = overlays[int(REGION.match(region).group(1))]
        lo, hi = o["ram_start"], o["ram_start"] + o["code_size"]
        for src in sorted(srcs):
            path = os.path.join(root, src)
            try:
                with open(path, encoding="utf-8", errors="replace") as fp:
                    text = fp.read()
            except OSError:
                continue
            for n, a, r in header_symbols(text):
                if r is not None and r != region:
                    if REGION.match(r):
                        o2 = overlays[int(REGION.match(r).group(1))]
                        if o2["ram_start"] <= a < o2["ram_start"] + o2["code_size"]:
                            note(r, a, n, OPEN)
                    continue
                if lo <= a < hi:
                    note(region, a, n, OPEN)
            for n, a in extern_calls(text):
                if lo <= a < hi:
                    note(region, a, n, OPEN)

    # Static initialisers.
    sinits = []
    for o in overlays:
        count = o["static_init_count"] - 1
        if count <= 0:
            continue
        addrs = KNOWN_SINIT.get(o["id"])
        if addrs is None or len(addrs) != count:
            raise SystemExit("overlay %d has %d static initialisers and this script "
                             "knows %s of them; record them in KNOWN_SINIT"
                             % (o["id"], count, "none" if addrs is None else len(addrs)))
        region = "ov%03d" % o["id"]
        for a in addrs:
            note(region, a, None, OPEN)
            sinits.append((o["id"], a))

    return overlays, funcs, sinits


def c_name(name, addr):
    if name is None or name == "func_%08X" % addr:
        return "NULL"
    return '"%s"' % name


def emit(overlays, funcs, sinits, out):
    lines = []
    w = lines.append
    w("/*")
    w(" * overlay_table.c -- GENERATED by tools/overlay_table.py; do not edit.")
    w(" *")
    w(" * The overlay registry's table (sdk/overlay.h), from aletheia's")
    w(" * layout/layout.json and manifest shards and this build's source list.")
    w(" * Addresses, sizes and names only; no game data.")
    w(" */")
    w("")
    w('#include "overlay.h"')
    w("")

    present_sinit = []
    for ov, a in sinits:
        name, status = funcs["ov%03d" % ov][a]
        if status == PRESENT:
            present_sinit.append((ov, a, name or "func_%08X" % a))
    for ov, a, n in present_sinit:
        w("extern void %s(void);" % n)
    if present_sinit:
        w("")

    flat = []
    desc = []
    for o in overlays:
        region = "ov%03d" % o["id"]
        rows = sorted(funcs.get(region, {}).items())
        first = len(flat)
        matched = sum(1 for _, (n, s) in rows if s >= MATCHED)
        present = sum(1 for _, (n, s) in rows if s == PRESENT)
        for a, (n, s) in rows:
            flat.append((a, s, n))
        desc.append((o, first, len(rows), matched, present))

    status_name = {OPEN: "BW_OVF_OPEN", MATCHED: "BW_OVF_MATCHED", PRESENT: "BW_OVF_PRESENT"}
    w("static const BwOverlayFunc g_funcs[] = {")
    for a, s, n in flat:
        w("    { 0x%08Xu, %s, %s }," % (a, status_name[s], c_name(n, a)))
    if not flat:
        w("    { 0u, BW_OVF_OPEN, NULL }")
    w("};")
    w("")

    w("static const BwOverlaySinit g_sinits[] = {")
    sinit_first = {}
    for k, (ov, a) in enumerate(sinits):
        sinit_first.setdefault(ov, k)
        fn = next((n for o2, a2, n in present_sinit if o2 == ov and a2 == a), None)
        w("    { %du, 0x%08Xu, %s }," % (ov, a, fn if fn else "NULL"))
    if not sinits:
        w("    { 0u, 0u, NULL }")
    w("};")
    w("")

    w("static const BwOverlayDesc g_overlays[%d] = {" % len(overlays))
    w("    /* id  ram         code        bss         sinit       sinit_end   file"
      "  compressed  funcs (first, count, matched, present)  sinits */")
    for o, first, count, matched, present in desc:
        word = (o["compressed_size"] & 0xFFFFFF) | (o["flags"] << 24)
        nsin = max(0, o["static_init_count"] - 1)
        w("    { %3du, 0x%08Xu, 0x%06Xu, 0x%06Xu, 0x%08Xu, 0x%08Xu, %3du, 0x%08Xu, "
          "%5du, %4du, %4du, %4du, %du, %du },"
          % (o["id"], o["ram_start"], o["code_size"], o["bss_size"],
             o["static_init_start"], o["static_init_end"], o["file_id"], word,
             first, count, matched, present, sinit_first.get(o["id"], 0), nsin))
    w("};")
    w("")
    w("const BwOverlayTable bw_overlay_table_generated = {")
    w("    g_overlays, %du," % len(overlays))
    w("    g_funcs, %du," % len(flat))
    w("    g_sinits, %du" % len(sinits))
    w("};")
    text = "\n".join(lines) + "\n"

    try:
        with open(out, encoding="utf-8") as fp:
            if fp.read() == text:
                return False
    except OSError:
        pass
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as fp:
        fp.write(text)
    return True


def summary(overlays, funcs, sinits):
    tk = tm = tp = 0
    print("  id   ram         code      bss       static_init              file  "
          "known  matched  present")
    for o in overlays:
        rows = funcs.get("ov%03d" % o["id"], {})
        k = len(rows)
        m = sum(1 for n, s in rows.values() if s >= MATCHED)
        p = sum(1 for n, s in rows.values() if s == PRESENT)
        tk, tm, tp = tk + k, tm + m, tp + p
        si = "0x%08X..0x%08X (%d)" % (o["static_init_start"], o["static_init_end"],
                                       o["static_init_count"] - 1)
        print("  %3d  0x%08X  0x%06X  0x%06X  %-23s  %4d  %5d  %7d  %7d"
              % (o["id"], o["ram_start"], o["code_size"], o["bss_size"], si,
                 o["file_id"], k, m, p))
    print("  %d overlays; %d known functions, %d matched in aletheia, %d present "
          "in this build; %d static initialisers"
          % (len(overlays), tk, tm, tp, len(sinits)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--aletheia-root", default="../aletheia")
    ap.add_argument("--out", help="C file to write (rewritten only if it changes)")
    ap.add_argument("--sources", help="file listing the build's sources, one per line")
    ap.add_argument("--summary", action="store_true", help="print the table")
    a = ap.parse_args(argv)

    sources = []
    if a.sources:
        with open(a.sources, encoding="utf-8") as fp:
            sources = [ln.strip() for ln in fp if ln.strip()]
    overlays, funcs, sinits = build(a.aletheia_root, sources)
    if a.out:
        emit(overlays, funcs, sinits, a.out)
    if a.summary or not a.out:
        summary(overlays, funcs, sinits)
    return 0


if __name__ == "__main__":
    sys.exit(main())
