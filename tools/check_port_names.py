#!/usr/bin/env python3
"""
tools/check_port_names.py -- every overlay function a port file calls by
name is in that file's symbols block.

The overlay link (tools/ovlink.py) gives an overlay function its plain name
(a trampoline to the resident overlay) only when some file it scans names
the function with an address.  A port file that declares an overlay
function as a weak extern and does not list it gets a NULL pointer, and its
feature silently does nothing.  This check finds those declarations.

usage: check_port_names.py ALETHEIA_ROOT FILE...
Exit 1 when a file declares an overlay function its symbols block lacks.
"""

import os
import re
import sys

SYMBOL_ROW = re.compile(r"^\s*\*\s+([A-Za-z_]\w*)\s+0x[0-9A-Fa-f]{1,8}\s+(thumb|arm)\b")
EXTERN_FN = re.compile(r"\bextern\b[^;(]*?\b([A-Za-z_]\w*)\s*\(", re.S)


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    overlay = set()
    tsv = os.path.join(argv[1], "layout", "function_addrs.tsv")
    for line in open(tsv, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) >= 3 and f[0].startswith("ov"):
            overlay.add(f[2])
    bad = 0
    for path in argv[2:]:
        text = open(path, encoding="utf-8", errors="replace").read()
        listed = set()
        for line in text.splitlines():
            m = SYMBOL_ROW.match(line)
            if m:
                listed.add(m.group(1))
        for name in sorted(set(EXTERN_FN.findall(text))):
            if name in overlay and name not in listed:
                print("%s: %s is an overlay function missing from the "
                      "symbols block" % (path, name))
                bad += 1
    print("check_port_names: %d file(s), %d missing" % (len(argv) - 2, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
