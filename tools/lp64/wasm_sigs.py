#!/usr/bin/env python3
"""
tools/lp64/wasm_sigs.py -- how many game functions the matched corpus
declares with prototypes a wasm32 link would reject, across the whole corpus.

Why this exists: docs/lp64.md.  A wasm call is type-checked, so a function
declared with a different number of parameters, or as returning a value in
one file and void in another, needs an adapter (tools/lp64/sigfix.py) where
the DS and i686 need nothing.  A pointer against an integer is not such a
conflict (both are i32 on wasm32), which is why this number is far below
tools/closure.py's A2 count.  The link-time measurement covers one closure;
this is the static estimate for every matched source, read with the same
prototype parser tools/closure.py --pointer-audit uses.

    python tools/lp64/wasm_sigs.py --aletheia-root ../aletheia
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import closure  # noqa: E402  (tools/closure.py)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--aletheia-root", required=True)
    a = ap.parse_args()
    closure.ROOT = os.path.abspath(a.aletheia_root)
    idx = closure.Index()
    audits, _ = closure.pointer_audit(idx)

    sigs = {}
    for source, audit in audits.items():
        for name, seen in audit.protos.items():
            for ret, params in seen:
                sigs.setdefault(name, set()).add((ret == "void", len(params)))

    arity = ret = both = 0
    for name, forms in sigs.items():
        rets = {r for r, _ in forms}
        counts = {n for _, n in forms}
        if len(rets) > 1 and len(counts) > 1:
            both += 1
        elif len(rets) > 1:
            ret += 1
        elif len(counts) > 1:
            arity += 1
    total = arity + ret + both
    print("game functions declared in matched sources: %d" % len(sigs))
    print("declared with a wasm-incompatible disagreement: %d" % total)
    print("  parameter count only:      %d" % arity)
    print("  void vs value return only: %d" % ret)
    print("  both:                      %d" % both)
    return 0


if __name__ == "__main__":
    sys.exit(main())
