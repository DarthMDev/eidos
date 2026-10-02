#!/usr/bin/env python3
"""Find the calls in aletheia's matched C that pass an argument in a register
without naming it.

mwcc compiles `f(); g();` with nothing between the two `bl`s, so if g takes
an argument, on the DS it receives whatever f left in r0; and a function that
calls g() with fewer arguments than g takes hands on its own incoming r1..r3
untouched.  The matched C reproduces the bytes either way, because the value
is already in the register.  A host compiler passes what the C says, so the
callee reads an indeterminate value (docs/boot.md, "Arguments in registers").

For every matched function F that calls a function G it declares with fewer
parameters than G's definition takes, and where G's body uses a parameter F
does not pass, this tool reads F's code in aletheia's reassembly listing and
says where each missing register comes from at the call:

  passthrough    rN is F's own incoming argument N: nothing writes it first
  return-chain   r0 is the return value of the call just before
  other          set explicitly, clobbered by an earlier call, or past a
                 branch target (the listing has to be read by hand)

Usage:
  regargs.py --aletheia-root R [--boot-path FILE] [-o OUT.tsv]

--boot-path lists caller files (relative to R) the port already stands in
for with a reimplementation; those rows say yes in on_boot_path.  The output
is tab-separated with a header row.  Addresses and names only; nothing of
the game is read except the listing's mnemonics.
"""

import argparse
import glob
import os
import re
import sys


def strip_comments(t):
    t = re.sub(r"/\*.*?\*/", " ", t, flags=re.S)
    return re.sub(r"//[^\n]*", " ", t)


ADDR = re.compile(r"(?:^|_)(0[0-9A-Fa-f]{7})$")
DEF = re.compile(r"(?:^|(?<=[;}]))\s*(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)"
                 r"\s*\(([^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\)\s*\{", re.S | re.M)
DECL = re.compile(r"(?:^|(?<=[;}]))\s*(?:extern\s+)?[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)"
                  r"\s*\(([^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\)\s*;", re.S | re.M)
SYMROW = re.compile(r"^\s*\*\s+([A-Za-z_]\w*)\s+(0x[0-9A-Fa-f]{1,8})\s+(arm|thumb|data)\b", re.M)
KEYWORDS = {"if", "while", "for", "switch", "return", "sizeof"}


def nparams(params):
    params = params.strip()
    if params == "" or "..." in params:
        return None
    if params == "void":
        return 0
    depth, n = 0, 1
    for c in params:
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif c == "," and depth == 0:
            n += 1
    return n


def param_names(params):
    out = []
    for p in params.split(","):
        m = re.search(r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*$", p.strip())
        out.append(m.group(1) if m else "")
    return out


def body_at(t, start):
    depth, k = 1, start
    while k < len(t) and depth:
        if t[k] == "{":
            depth += 1
        elif t[k] == "}":
            depth -= 1
        k += 1
    return t[start:k]


class Listing:
    """One reassembly listing: function labels and instruction lines."""

    INSN = re.compile(r"^\t(\S+)\s*([^/]*?)\s*/\* ([0-9A-F]{8})")

    def __init__(self, path):
        self.lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
        self.label = {}
        for i, l in enumerate(self.lines):
            m = re.match(r"^(?:func|sub)_([0-9A-F]{8}):", l)
            if m:
                self.label[int(m.group(1), 16)] = i

    def calls(self, faddr, target):
        i = self.label.get(faddr)
        if i is None:
            return None, []
        want = "%08X" % target
        out, j = [], i + 1
        while j < len(self.lines):
            l = self.lines[j]
            if re.match(r"^\t\.global\t(?:func|sub)_", l):
                break
            m = self.INSN.match(l)
            if m and want in l and (self.is_call(m.group(1)) or self.FAR.search(l)):
                out.append(j)          # .inst.w ... bl(x) 0x... (another region)
            j += 1
        if not out:
            out = self.tail_calls(i, j, want)
        return i, out

    def tail_calls(self, start, end, want):
        """`ldr rN, pool_X; ... bx rN` where pool_X holds the target: a tail
        call that leaves r0-r2 as the function received them."""
        pools = set()
        for k in range(start, min(end + 64, len(self.lines))):
            l = self.lines[k]
            if ".word" in l and "->" in l and want in l.split("->", 1)[1]:
                for b in range(k, max(k - 8, start), -1):
                    pm = re.match(r"^(pool_[0-9A-F]{8}):", self.lines[b])
                    if pm:
                        pools.add(pm.group(1))
                        break
        out = []
        for pool in pools:
            for k in range(start, end):
                lm = re.match(r"^\tldr\s+(r\d+), %s\b" % pool, self.lines[k])
                if not lm:
                    continue
                for x in range(k + 1, end):
                    if re.match(r"^\tbx\s+%s\b" % lm.group(1), self.lines[x]):
                        out.append(x)
                        break
        return out

    CONDS = "eq|ne|cs|cc|hs|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al"
    FAR = re.compile(r"\bblx? 0x[0-9A-Fa-f]{8}")

    @classmethod
    def is_call(cls, op):
        """bl/blx with an optional condition; `blt`, `ble`, `bls` are
        branches (b + lt, le, ls), not calls."""
        return re.match(r"^blx?(?:%s)?$" % cls.CONDS, op.lower()) is not None

    @classmethod
    def writes(cls, op, args, reg, line=""):
        op = op.lower()
        if cls.is_call(op) or (op.startswith(".inst") and cls.FAR.search(line)):
            return "call"
        if op.startswith(("push", "str", "cmp", "cmn", "tst", "teq", "stm", "b", ".")):
            return None
        if op.startswith(("pop", "ldm")):
            return "set" if re.search(r"\b%s\b" % reg, args.split("{", 1)[-1]) else None
        first = args.split(",")[0].strip().lower()
        return "set" if first == reg else None

    def source_of(self, faddr, target, k):
        start, calls = self.calls(faddr, target)
        if start is None:
            return "no label"
        if not calls:
            return "no call"
        reg = "r%d" % k
        kinds = []
        for c in calls:
            kind = None
            x = c - 1
            while x > start:
                l = self.lines[x]
                if re.match(r"^(?:loc|func|sub)_[0-9A-F]{8}:", l):
                    kind = "branch target"
                    break
                m = self.INSN.match(l)
                if m:
                    w = self.writes(m.group(1), m.group(2), reg, l)
                    if w == "call":
                        kind = "return" if k == 0 else "clobbered"
                        break
                    if w == "set":
                        kind = "set"
                        break
                x -= 1
            kinds.append(kind or "entry")
        return ",".join(sorted(set(kinds)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--aletheia-root", required=True)
    ap.add_argument("--boot-path", help="file of caller paths the port stands in for")
    ap.add_argument("-o", "--out")
    args = ap.parse_args(argv)
    root = os.path.abspath(args.aletheia_root)

    sdk = {}
    for line in open(os.path.join(root, "layout", "symbols_sdk.txt"), encoding="utf-8"):
        p = line.split()
        if len(p) >= 2 and not line.startswith("#"):
            try:
                sdk.setdefault(p[1], int(p[0], 16))
            except ValueError:
                pass

    files = sorted(glob.glob(os.path.join(root, "src", "matched", "*", "*.c")))
    texts, rows_of, region_of = {}, {}, {}
    for f in files:
        raw = open(f, encoding="utf-8", errors="replace").read()
        texts[f] = strip_comments(raw)
        rows_of[f] = {m.group(1): int(m.group(2), 16) for m in SYMROW.finditer(raw[:8192])}
        m = re.search(r"^\s*\*\s*region:\s*(\S+)", raw[:4096], re.M)
        region_of[f] = m.group(1) if m else "arm9"

    def addr_of(name, f):
        if name in rows_of[f]:
            return rows_of[f][name]
        m = ADDR.search(name)
        if m:
            return int(m.group(1), 16)
        return sdk.get(name)

    # Definitions: address -> (params, names used, file, name)
    defs = {}
    for f, t in texts.items():
        for m in DEF.finditer(t):
            name = m.group(1)
            if name in KEYWORDS or m.group(0).lstrip(";} \n\t").startswith("static"):
                continue
            a = addr_of(name, f)
            n = nparams(m.group(2))
            if a is None or n is None:
                continue
            body = body_at(t, m.end())
            used = [bool(pn) and re.search(r"\b%s\b" % re.escape(pn), body) is not None
                    for pn in param_names(m.group(2))]
            defs.setdefault((region_of[f], a), (n, used, f, name))

    listings = {}

    def listing(region):
        if region not in listings:
            path = (os.path.join(root, "asm", "arm9", "main.s") if region.startswith("arm9")
                    else os.path.join(root, "asm", "overlays", region + ".s"))
            listings[region] = Listing(path) if os.path.exists(path) else None
        return listings[region]

    boot = set()
    if args.boot_path:
        boot = {l.strip().replace("\\", "/") for l in open(args.boot_path) if l.strip()}

    out = ["caller_file\tcaller_function\tcallee\timplicit\tkind\ton_boot_path\tnote"]
    for f, t in texts.items():
        region = region_of[f]
        decls = {}
        for m in DECL.finditer(t):
            n = nparams(m.group(2))
            if n is not None:
                decls.setdefault(m.group(1), n)
        for m in DEF.finditer(t):
            fname = m.group(1)
            if fname in KEYWORDS:
                continue
            faddr = addr_of(fname, f)
            body = body_at(t, m.end())
            for callee, nd in decls.items():
                if not re.search(r"\b%s\s*\(" % re.escape(callee), body):
                    continue
                a = addr_of(callee, f)
                if a is None:
                    continue
                d = defs.get((region, a)) or defs.get(("arm9", a))
                if d is None or nd >= d[0]:
                    continue
                nf, used, dfile, dname = d
                lst = listing(region) if faddr is not None else None
                for k in range(nd, min(nf, 4)):
                    if not used[k]:
                        continue
                    src = lst.source_of(faddr, a, k) if lst else "no listing"
                    if src == "entry":
                        kind, implicit = "passthrough", "r%d" % k
                    elif src == "return":
                        kind, implicit = "return-chain", "ret->r0"
                    else:
                        kind, implicit = "other", "r%d" % k
                    rel = os.path.relpath(f, root).replace("\\", "/")
                    note = "%s defined with %d parameters in %s; caller passes %d; listing: %s" % (
                        dname, nf, os.path.relpath(dfile, root).replace("\\", "/"), nd, src)
                    out.append("\t".join([rel, fname, callee, implicit, kind,
                                          "yes" if rel in boot else "no", note]))
    text = "\n".join(out) + "\n"
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
    else:
        sys.stdout.write(text)
    print("regargs: %d sites" % (len(out) - 1), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
