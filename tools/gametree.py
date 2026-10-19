#!/usr/bin/env python3
"""
gametree.py -- aletheia's src/game/ as the port's source of matched code.

aletheia keeps the matching work twice.  src/matched/ is the record: one
function (or one small unit) per file, each verified on its own.  src/game/
is the program as it was most likely written: one .c per original source
file (translation unit), with one declaration per function, real names, and
the fixes the restructure made (a call that relied on r0 passing through now
passes its argument).  The port compiles src/game/ (docs/game-tree.md).

This module turns src/game/ into the same entries the port's generators read
from aletheia's per-packet manifests (tools/closure.py, Index), so the closure
walk, the boot generator and the overlay linker work on translation units:

    {"name":      the file's stem ("particle", "backup_system_p02"),
     "address":   the file's first address (its header's `address:`),
     "source":    "src/game/<region>/<file>.c",
     "isa":       "arm" or "thumb" (the header's `isa:`),
     "region":    "arm9", "ovNNN", ... (the header's `region:`),
     "status":    the header's `status:`,
     "functions": every function the header lists, in its order,
     "addresses": each function's DS address, or None}

A function's address comes from aletheia's manifests by (region, name): the
manifests carry every member's address under its current name.  The ones they
lack (a linker veneer's spelling, the CodeWarrior runtime) come from the SDK
and runtime symbol tables, and failing that stay None: the walk then finds
the function by name alone.

Only the top level of each region folder is a translation unit.  A folder
src/game/<region>/<file>/ holds member files that could not join their part
yet (its MERGE.md says why); those are units of their own, read the same way.

Standard library only.
"""

from __future__ import annotations

import glob
import json
import os
import re

HEADER_FIELD = re.compile(r"^ \*\s*(file|unit|function|address|region|compiler|flags|lang|isa|status):\s*(.*)$")


def _header(text):
    """The header fields of a src/game file; `function` may continue on
    following comment lines that hold only names and commas."""
    out = {}
    lines = text.split("\n", 400)[:400]
    key = None
    for line in lines:
        if line.startswith("*/") or line.strip() == "*/":
            break
        m = HEADER_FIELD.match(line)
        if m:
            key = m.group(1)
            out[key] = m.group(2).strip()
            continue
        if key == "function":
            s = line.lstrip(" *").strip()
            if s and re.match(r"^[A-Za-z_]\w*(\s*,\s*[A-Za-z_]\w*)*,?$", s):
                out["function"] += (" " if out["function"].endswith(",") else ", ") + s
                continue
        key = None
    return out


def pair_members(names, addrs, rows=None):
    """Pair a unit's `functions` with its manifest `addresses`.

    aletheia lists the two in the same order, except where a unit's address
    list is sorted and its names are not (src/matched/arm9/converters.c: nine
    MSL converters, header order _s32_to_float.._f2d, addresses ascending).
    A name whose own address is in the list (its func_XXXXXXXX spelling, or
    its row in `rows`, the SDK and runtime tables) takes that address; the
    other names take the addresses left, in order."""
    want = set(addrs)
    own = []
    for n in names:
        m = re.match(r"^(?:\w+_)?func_([0-9A-Fa-f]{8})$", n or "")
        a = int(m.group(1), 16) if m else (rows or {}).get(n)
        own.append(a if a in want else None)
    taken = {a for a in own if a is not None}
    rest = [a for a in addrs if a not in taken]
    out = []
    for a in own:
        if a is None:
            a = rest.pop(0) if rest else None
        out.append(a)
    return out


def _manifest_addresses(root):
    """{(region, name): address} from aletheia's per-packet manifests."""
    out = {}
    rows = dict(_symbol_table(root, os.path.join("layout", "runtime_arm9.txt")))
    for k, v in _symbol_table(root, os.path.join("layout", "symbols_sdk.txt")).items():
        rows.setdefault(k, v)
    pattern = os.path.join(root, "layout", "matched", "**", "*.json")
    for path in sorted(glob.glob(pattern, recursive=True)):
        try:
            with open(path, encoding="utf-8") as fh:
                shard = json.load(fh)
        except (OSError, ValueError):
            continue
        for e in shard.get("functions", []):
            region = e.get("region", "arm9")
            names = e.get("functions") or [e.get("name")]
            addrs = e.get("addresses") or e.get("covers") or [e.get("address")]
            addrs = [int(a, 16) for a in addrs if a]
            for n, a in zip(names, pair_members(names, addrs, None if region.startswith("ov") else rows)):
                if n and a is not None:
                    out.setdefault((region, n), a)
    return out


def _symbol_table(root, rel):
    """{name: address} from a `name address ...` table (symbols_sdk.txt,
    runtime_arm9.txt); the first address a name has wins."""
    out = {}
    path = os.path.join(root, rel)
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            addr = name = None
            for p in parts:
                if addr is None and re.match(r"^(0x)?[0-9A-Fa-f]{8}$", p):
                    addr = int(p, 16)
                elif name is None and re.match(r"^[A-Za-z_]\w*$", p) and not re.match(r"^(arm|thumb|data|func)$", p):
                    name = p
            if addr is not None and name is not None:
                out.setdefault(name, addr)
    return out


def source_files(root):
    """Every translation unit of src/game/, as paths relative to `root`."""
    base = os.path.join(root, "src", "game")
    out = []
    for region in sorted(os.listdir(base)) if os.path.isdir(base) else []:
        rdir = os.path.join(base, region)
        if not os.path.isdir(rdir):
            continue
        for name in sorted(os.listdir(rdir)):
            full = os.path.join(rdir, name)
            if name.endswith(".c") and not name.startswith("_") and os.path.isfile(full):
                out.append(os.path.relpath(full, root).replace("\\", "/"))
            elif os.path.isdir(full) and name not in ("unplaced",):
                for sub in sorted(os.listdir(full)):
                    if sub.endswith(".c") and not sub.startswith("_"):
                        out.append(os.path.relpath(os.path.join(full, sub), root).replace("\\", "/"))
    return out


def entries(root):
    """The manifest-shaped entries of src/game/ (see the module comment)."""
    by_name = _manifest_addresses(root)
    sdk = _symbol_table(root, os.path.join("layout", "symbols_sdk.txt"))
    rt = _symbol_table(root, os.path.join("layout", "runtime_arm9.txt"))
    out = []
    for rel in source_files(root):
        with open(os.path.join(root, rel), encoding="utf-8", errors="replace") as fh:
            h = _header(fh.read(60000))
        try:
            addr = int(h.get("address", "0"), 16)
        except ValueError:
            addr = 0
        region = h.get("region") or rel.split("/")[2]
        funcs = [f.strip() for f in h.get("function", "").split(",") if f.strip()]
        addrs = []
        for f in funcs:
            a = by_name.get((region, f))
            if a is None and not region.startswith("ov"):
                a = sdk.get(f, rt.get(f))
            addrs.append(a)
        isa = (h.get("isa") or "arm").split()[0] if h.get("isa") else "arm"
        out.append({
            "name": os.path.splitext(os.path.basename(rel))[0],
            "address": addr,
            "source": rel,
            "isa": isa,
            "region": region,
            "status": (h.get("status") or "EXACT").split()[0],
            "functions": funcs,
            "addresses": addrs,
            "compiler": h.get("compiler", ""),
            "flags": h.get("flags", ""),
            "lang": h.get("lang", ""),
        })
    return out


def main():
    import argparse
    ap = argparse.ArgumentParser(description="list src/game's translation units")
    ap.add_argument("root")
    ap.add_argument("--stats", action="store_true")
    args = ap.parse_args()
    es = entries(args.root)
    if args.stats:
        nf = sum(len(e["functions"]) for e in es)
        na = sum(1 for e in es for a in e["addresses"] if a is not None)
        print("units %d, functions %d, with an address %d" % (len(es), nf, na))
        noaddr = [e["source"] for e in es if not e["address"]]
        print("units with no header address: %d %s" % (len(noaddr), noaddr[:5]))
        return
    for e in es:
        print("%s\t%s\t0x%08X\t%d functions" % (e["source"], e["region"], e["address"], len(e["functions"])))


if __name__ == "__main__":
    main()
