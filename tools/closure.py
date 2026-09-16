#!/usr/bin/env python3
"""
closure.py -- which matched translation units can be compiled for the host.

eidos (this repository) compiles the *same* C files that aletheia's
tools/csrc.py splices into the DS image.  A matched file can only be linked
on the host when every name it references is itself provided: by another
matched file, by our own SDK shim (sdk/), by the compiler runtime, or by a
deliberate stub (stubs/).  This script computes that closure mechanically so
CMakeLists.txt's source list can grow without anybody guessing.

Inputs, all read out of the aletheia checkout (--aletheia-root, default
../aletheia relative to this repository), none of them game bytes:

  layout/matched/**/*.json   the manifest shards: address, name, source,
                             isa, status, and for a unit entry the list of
                             functions the unit defines
  src/matched/*.c            the `extern` declarations and the `symbols:`
                             header block of each matched file
  layout/symbols_sdk.txt     SDK library symbols (address + name)
  layout/runtime_arm9.txt    mwccarm/MSL runtime helpers (address + name)

Every referenced name is put in exactly one class:

  matched   defined by another EXACT matched source           (link it)
  sdk       an SDK library symbol                             (sdk/)
  runtime   an MSL/runtime.a helper such as _s32_div_f          (sdk/)
  open      a game function at a known address that nothing
            in src/matched/ defines yet                       (stubs/)
  data      a global object (the `data` kind in `symbols:`, or
            a g_ name) that lives in the ARM9 image           (sdk/)

A source is *closed* when it references nothing in the `open` or `data`
classes.  A source is *linkable* when the port supplies the open and data
names some other way; the port records those explicitly rather than
letting the linker decide.

Usage:

    python tools/closure.py --list-closed
    python tools/closure.py --seed src/matched/sub_02018A7C.c
    python tools/closure.py --seed-func func_02045E3C --cmake
    python tools/closure.py --aletheia-root /path/to/aletheia --list-closed

`--seed` takes a source path or a function name (`--seed-func`) and walks
the call graph through matched files only.  `--cmake` prints the closure as
a CMake source list ready to paste into CMakeLists.txt.

This script reads only the aletheia checkout's layout/ and src/; it writes
nothing.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys

EIDOS_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.normpath(os.path.join(EIDOS_ROOT, "..", "aletheia"))

ADDR_NAME = re.compile(r"^(?:sub|func|unit)_0([0-9A-Fa-f]{7})$")
EXTERN_LINE = re.compile(r"^\s*extern\s+(.*?);", re.M | re.S)
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
SYMBOL_ROW = re.compile(r"^\s*\*\s+([A-Za-z_][A-Za-z0-9_]*)\s+(0x[0-9A-Fa-f]{8})\s+(thumb|arm|data)\s*$")
SYMBOLS_HDR = re.compile(r"^\s*\*\s*symbols:\s*$")

C_KEYWORDS = {
    "const", "volatile", "struct", "union", "enum", "unsigned", "signed",
    "char", "short", "int", "long", "float", "double", "void", "static",
    "inline", "register", "restrict", "extern", "typedef",
    "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "fx32", "fx16",
    "BOOL", "vu8", "vu16", "vu32", "vs8", "vs16", "vs32",
}


def read_symbol_table(path):
    """address -> name for the two-column layout/*.txt symbol files."""
    by_addr = {}
    by_name = {}
    full = os.path.join(ROOT, path)
    if not os.path.exists(full):
        return by_addr, by_name
    with open(full, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            try:
                addr = int(parts[0], 16)
            except ValueError:
                continue
            by_addr.setdefault(addr, parts[1])
            by_name.setdefault(parts[1], addr)
    return by_addr, by_name


class Index:
    def __init__(self):
        self.entries = []          # one dict per shard entry
        self.by_addr = {}          # address -> entry
        self.defines = {}          # function name -> source path
        self.sources = {}          # source path -> entry
        self.externs = {}          # source path -> [names]
        self.symbols = {}          # source path -> {name: (addr, kind)}
        self.sdk_by_addr, self.sdk_by_name = read_symbol_table("layout/symbols_sdk.txt")
        self.rt_by_addr, self.rt_by_name = read_symbol_table("layout/runtime_arm9.txt")
        self._load_shards()
        self._load_sources()

    def _load_shards(self):
        pattern = os.path.join(ROOT, "layout", "matched", "**", "*.json")
        for path in sorted(glob.glob(pattern, recursive=True)):
            with open(path, encoding="utf-8") as fh:
                shard = json.load(fh)
            for fn in shard.get("functions", []):
                if fn.get("status") not in ("EXACT", "EXACT*"):
                    continue
                entry = {
                    "name": fn["name"],
                    "address": int(fn["address"], 16),
                    "source": fn["source"].replace("\\", "/"),
                    "isa": fn.get("isa", "arm"),
                    "region": fn.get("region", "arm9"),
                    "status": fn["status"],
                    "shard": os.path.basename(path),
                    "functions": fn.get("functions") or [fn["name"]],
                }
                self.entries.append(entry)
                self.by_addr[entry["address"]] = entry
                self.sources[entry["source"]] = entry
                for name in entry["functions"]:
                    self.defines.setdefault(name, entry["source"])
                # `unit_0204ABCD` is a file name, not a symbol; the real lead
                # function keeps its own sub_/func_ spelling in `functions`.
                self.defines.setdefault(entry["name"], entry["source"])

    def _load_sources(self):
        for source in self.sources:
            full = os.path.join(ROOT, source)
            if not os.path.exists(full):
                self.externs[source] = []
                self.symbols[source] = {}
                continue
            with open(full, encoding="utf-8", errors="replace") as fh:
                text = fh.read()
            self.symbols[source] = self._parse_symbols(text)
            self.externs[source] = self._parse_externs(text)

    @staticmethod
    def _parse_symbols(text):
        out = {}
        in_block = False
        for line in text.splitlines():
            if SYMBOLS_HDR.match(line):
                in_block = True
                continue
            if in_block:
                m = SYMBOL_ROW.match(line)
                if m:
                    out[m.group(1)] = (int(m.group(2), 16), m.group(3))
                elif line.strip().startswith("*/") or not line.strip().startswith("*"):
                    in_block = False
        return out

    @staticmethod
    def _parse_externs(text):
        # Strip comments first so prose that mentions `extern` cannot match.
        stripped = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
        stripped = re.sub(r"//[^\n]*", " ", stripped)
        names = []
        for decl in EXTERN_LINE.findall(stripped):
            # The declared name is the last identifier before the first '('
            # (a function) or the last identifier overall (an object).
            head = decl.split("(")[0]
            idents = [i for i in IDENT.findall(head) if i not in C_KEYWORDS]
            if idents:
                names.append(idents[-1])
        return names

    def classify(self, source, name):
        """Return (klass, detail) for one name referenced by one source."""
        owner = self.defines.get(name)
        if owner and owner != source:
            return "matched", owner
        if owner == source:
            return "local", source

        addr = None
        sym = self.symbols.get(source, {}).get(name)
        if sym:
            addr, kind = sym
            if kind == "data":
                return "data", "0x%08X" % addr
        else:
            m = ADDR_NAME.match(name)
            if m:
                addr = int("0" + m.group(1), 16)

        if name in self.rt_by_name or (addr is not None and addr in self.rt_by_addr):
            return "runtime", name
        if name in self.sdk_by_name or (addr is not None and addr in self.sdk_by_addr):
            return "sdk", self.sdk_by_name.get(name) and name or self.sdk_by_addr.get(addr, name)
        if addr is not None:
            owner = self.by_addr.get(addr)
            if owner and owner["source"] != source:
                # Same address, different spelling of the name.  The DS build
                # resolves relocations by address and does not care; a host
                # linker resolves by name and does.  Every one of these needs
                # a forwarder in port/sdk/aliases.c.
                if name not in owner["functions"] and name != owner["name"]:
                    return "alias", "%s in %s" % (owner["functions"][0], owner["source"])
                return "matched", owner["source"]
            return "open", "0x%08X" % addr
        return "data", name

    def refs(self, source):
        out = []
        for name in self.externs.get(source, []):
            klass, detail = self.classify(source, name)
            if klass == "local":
                continue
            out.append((name, klass, detail))
        return out

    def closure(self, seeds):
        """Transitively pull in matched sources; report what stays open."""
        seen = []
        queue = list(seeds)
        open_refs = {}
        while queue:
            source = queue.pop(0)
            if source in seen:
                continue
            seen.append(source)
            for name, klass, detail in self.refs(source):
                if klass == "matched":
                    if detail not in seen and detail not in queue:
                        queue.append(detail)
                    continue
                if klass == "alias":
                    owner = detail.split(" in ", 1)[1]
                    if owner not in seen and owner not in queue:
                        queue.append(owner)
                open_refs.setdefault((name, klass, detail), []).append(source)
        return seen, open_refs

    def resolve_seed(self, seed):
        seed = seed.replace("\\", "/")
        if seed in self.sources:
            return seed
        if seed.endswith(".c"):
            for source in self.sources:
                if source.endswith("/" + os.path.basename(seed)):
                    return source
        owner = self.defines.get(seed)
        if owner:
            return owner
        raise SystemExit("closure.py: no matched source defines %r" % seed)


# ---------------------------------------------------------------------------
# The pointer-width audit
# ---------------------------------------------------------------------------
#
# How much of the matched corpus assumes `sizeof(void *) == 4`?  Three
# questions, asked of every matched source the manifest names, each one a
# separate reason the file would compute a different number on an LP64 host.
# docs/pointer-width.md records the answers and states the rule below as the
# definition of each count, because every one of these is a heuristic over C
# text rather than a type check: the point is a defensible order of magnitude
# and a file list to look at, not a proof.
#
#   A  an integer-typed name that holds an address.  Three signals:
#        A1  a name declared with an integer type (u8..u64, s8..s64, int,
#            long, ...) that is the operand of a cast to a pointer type.
#        A2  a cross-file prototype disagreement: some matched file spells a
#            function's return value or one of its parameters as an integer
#            while another spells the same position as a pointer.  The DS
#            build resolves this by address and does not care; on a host the
#            integer spelling truncates.  This is the strongest signal of the
#            three because the evidence is two independent decompilations of
#            the same signature.
#        A3  a function whose declared return type is an integer type and
#            whose body returns an address expression (`return &x` or
#            `return <pointer-typed name>`).
#
#   B  a hardcoded struct size or field offset.  Three signals:
#        B1  explicit padding: a byte array member (u8/s8/char) that is not
#            the last member of its struct, i.e. a member whose only job is
#            to place the members after it at a fixed byte offset.
#        B2  an index multiply by a literal >= 4 inside an expression that
#            also does member access or subscripting, i.e. a row stride
#            written out rather than taken from sizeof.
#        B3  a literal size argument to a call whose callee name contains
#            "alloc" or "Alloc".
#
#   C  a cast between an integer and a pointer, in either direction: a cast
#      to a pointer type applied to an integer-typed name or a numeric
#      literal, or a cast to an integer type applied to `&` or to a
#      pointer-typed name.
#
# B1 is by far the most common and is also the one that matters most: a
# padding member is a field offset frozen into the source, and it is correct
# on the DS and wrong on any target where a pointer in front of it is wider.

INT_TYPE_NAMES = {
    "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64",
    "vu8", "vu16", "vu32", "vs8", "vs16", "vs32",
    "int", "long", "short", "char", "unsigned", "signed",
    "size_t", "BOOL", "fx32", "fx16", "uintptr_t", "intptr_t",
}
BYTE_TYPE_NAMES = {"u8", "s8", "char", "unsigned char", "signed char"}

DECL_QUALS = r"(?:const\s+|volatile\s+|static\s+|extern\s+|unsigned\s+|signed\s+|struct\s+|union\s+|enum\s+)*"
DECL_RE = re.compile(
    r"\b(" + DECL_QUALS + r"[A-Za-z_]\w*)\s+(\**)\s*([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*(?=[;,=)])")
FUNC_RE = re.compile(
    r"\b(" + DECL_QUALS + r"[A-Za-z_]\w*)\s+(\**)\s*([A-Za-z_]\w*)\s*\(([^;{)]*)\)\s*(?=[;{])")
STRUCT_RE = re.compile(r"\b(?:struct|union)\s+([A-Za-z_]\w*)?\s*\{([^{}]*)\}", re.S)
PTR_CAST_RE = re.compile(
    r"\(\s*(?:const\s+|volatile\s+)*(?:struct\s+|union\s+|enum\s+)?([A-Za-z_]\w*)\s*\*+\s*\)\s*"
    r"(0[xX][0-9a-fA-F]+|\d+|[A-Za-z_]\w*)")
INT_CAST_RE = re.compile(
    r"\(\s*(?:unsigned\s+|signed\s+)?(u8|u16|u32|u64|s8|s16|s32|s64|int|long|"
    r"size_t|uintptr_t|intptr_t|unsigned)\s*\)\s*(&|[A-Za-z_]\w*)")
LIT_RE = re.compile(r"\b(0[xX][0-9a-fA-F]+|\d+)\b")
MUL_LIT_RE = re.compile(r"(?:\*\s*(0[xX][0-9a-fA-F]+|\d+)\b)|(?:\b(0[xX][0-9a-fA-F]+|\d+)\s*\*(?!/))")
ALLOC_CALL_RE = re.compile(r"\b([A-Za-z_]\w*(?:alloc|Alloc)\w*)\s*\(([^;]*?)\)")
RETURN_RE = re.compile(r"\breturn\b([^;]*);")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def _base_type(decl):
    """The last word of a declaration's type, with qualifiers dropped."""
    words = [w for w in decl.split() if w not in
             ("const", "volatile", "static", "extern", "struct", "union", "enum")]
    if not words:
        return ""
    if len(words) >= 2 and words[-2] in ("unsigned", "signed"):
        return words[-1]
    return words[-1]


def _lit(value):
    try:
        return int(value, 0)
    except (TypeError, ValueError):
        return None


class PointerAudit:
    """One matched source, read three ways.  See the comment block above."""

    def __init__(self, source, text):
        self.source = source
        self.text = strip_comments(text)
        self.kinds = {}        # identifier -> "ptr" | "int" | "other"
        self.protos = {}       # function name -> (ret kind, [param kinds])
        self.hits = {"A": [], "B": [], "C": []}
        self._declarations()
        self._prototypes()
        self._padding()
        self._strides()
        self._casts()
        self._returns()

    # -- the per-file identifier type map ---------------------------------

    def _note(self, name, kind):
        have = self.kinds.get(name)
        if have is None:
            self.kinds[name] = kind
        elif have != kind:
            self.kinds[name] = "mixed"

    def _declarations(self):
        for decl, stars, name, _arr in DECL_RE.findall(self.text):
            base = _base_type(decl)
            if base in ("return", "sizeof", "if", "while", "for", "case"):
                continue
            if stars:
                self._note(name, "ptr")
            elif base in INT_TYPE_NAMES:
                self._note(name, "int")
            else:
                self._note(name, "other")

    @staticmethod
    def _kind_of(decl, stars):
        if stars:
            return "ptr"
        base = _base_type(decl)
        if base == "void":
            return "void"
        if base in INT_TYPE_NAMES:
            return "int"
        return "other"

    def _prototypes(self):
        for decl, stars, name, params in FUNC_RE.findall(self.text):
            if not ADDR_NAME.match(name):
                continue
            kinds = []
            for param in params.split(","):
                param = param.strip()
                if not param or param == "void":
                    continue
                m = re.match(r"^(" + DECL_QUALS + r"[A-Za-z_]\w*)\s*(\**)", param)
                kinds.append(self._kind_of(m.group(1), m.group(2)) if m else "other")
            self.protos.setdefault(name, []).append(
                (self._kind_of(decl, stars), tuple(kinds)))

    # -- B1: explicit padding ---------------------------------------------

    def _padding(self):
        for tag, body in STRUCT_RE.findall(self.text):
            members = [m.strip() for m in body.split(";") if m.strip()]
            for i, member in enumerate(members[:-1]):
                m = re.match(r"^(" + DECL_QUALS + r"[A-Za-z_]\w*)\s+(\**)\s*"
                             r"([A-Za-z_]\w*)\s*\[\s*([^\]]+?)\s*\]$", member)
                if not m or m.group(2):
                    continue
                if _base_type(m.group(1)) not in BYTE_TYPE_NAMES:
                    continue
                size = _lit(m.group(4))
                if size is None or size < 1:
                    continue
                self.hits["B"].append(
                    "B1 struct %s: %s %s[%s] fixes the offset of %s"
                    % (tag or "<anonymous>", _base_type(m.group(1)), m.group(3),
                       m.group(4), members[i + 1].split()[-1]))

    # -- B2/B3: strides and allocation sizes ------------------------------

    def _strides(self):
        for stmt in self.text.split(";"):
            if "->" not in stmt and "[" not in stmt:
                continue
            for a, b in MUL_LIT_RE.findall(stmt):
                value = _lit(a or b)
                # A row stride or a field offset is a multiple of the word;
                # requiring that drops the multiplicative constants of the
                # random-number generators, which are the loudest thing a
                # bare "multiply by a literal" rule otherwise finds.
                if value is not None and value >= 4 and value % 4 == 0:
                    self.hits["B"].append(
                        "B2 index multiply by %s: %s"
                        % (a or b, " ".join(stmt.split())[:90]))
                    break
        for callee, args in ALLOC_CALL_RE.findall(self.text):
            for value in LIT_RE.findall(args):
                if (_lit(value) or 0) >= 4:
                    self.hits["B"].append(
                        "B3 literal size %s passed to %s" % (value, callee))
                    break

    # -- A1/C: casts -------------------------------------------------------

    def _casts(self):
        for _type, operand in PTR_CAST_RE.findall(self.text):
            if operand and (operand[0].isdigit() or operand.startswith("0x")):
                # `(T *)0` is the null pointer constant, not an address held
                # in an integer; only a non-zero literal is a real one.
                if operand != "0":
                    self.hits["C"].append(
                        "C integer literal cast to a pointer: %s" % operand)
                continue
            if self.kinds.get(operand) == "int":
                self.hits["C"].append("C %s (integer) cast to a pointer" % operand)
                self.hits["A"].append("A1 %s is declared with an integer type "
                                      "and cast to a pointer" % operand)
        for _type, operand in INT_CAST_RE.findall(self.text):
            if operand == "&" or self.kinds.get(operand) == "ptr":
                self.hits["C"].append("C a pointer cast to an integer type (%s)"
                                      % operand)

    # -- A3: an address returned as an integer -----------------------------

    def _returns(self):
        for decl, stars, name, _params in FUNC_RE.findall(self.text):
            if stars or self._kind_of(decl, stars) != "int":
                continue
            body = self.text.split(name, 1)[-1]
            for expr in RETURN_RE.findall(body[:4000]):
                expr = expr.strip()
                if expr.startswith("&"):
                    self.hits["A"].append(
                        "A3 %s returns an address in %s" % (name, _base_type(decl)))
                    break
                head = IDENT.match(expr)
                if head and self.kinds.get(head.group(0)) == "ptr":
                    self.hits["A"].append(
                        "A3 %s returns the pointer %s in %s"
                        % (name, head.group(0), _base_type(decl)))
                    break


def pointer_audit(idx):
    """Audit every matched source; return (per-file hits, prototype conflicts)."""
    audits = {}
    for source in sorted(idx.sources):
        full = os.path.join(ROOT, source)
        if not os.path.exists(full):
            continue
        with open(full, encoding="utf-8", errors="replace") as fh:
            audits[source] = PointerAudit(source, fh.read())

    # A2 is a property of the corpus rather than of one file: the same
    # function spelled with a pointer in one matched source and an integer in
    # another.  Both spellings are EXACT on the DS, which is the finding.
    signatures = {}
    for source, audit in audits.items():
        for name, seen in audit.protos.items():
            for ret, params in seen:
                signatures.setdefault(name, []).append((source, ret, params))

    conflicts = []
    for name, rows in sorted(signatures.items()):
        rets = {r for _s, r, _p in rows if r in ("ptr", "int")}
        clash = "ret" if len(rets) > 1 else None
        if not clash:
            width = max((len(p) for _s, _r, p in rows), default=0)
            for i in range(width):
                kinds = {p[i] for _s, _r, p in rows
                         if len(p) > i and p[i] in ("ptr", "int")}
                if len(kinds) > 1:
                    clash = "param %d" % i
                    break
        if not clash:
            continue
        conflicts.append((name, clash, rows))
        for source, _r, _p in rows:
            audits[source].hits["A"].append(
                "A2 %s: its %s is a pointer in one matched file and an "
                "integer in another" % (name, clash))
    return audits, conflicts


def print_pointer_audit(idx):
    audits, conflicts = pointer_audit(idx)
    buckets = {k: [s for s, a in sorted(audits.items()) if a.hits[k]]
               for k in ("A", "B", "C")}
    any_hit = sorted(set(buckets["A"]) | set(buckets["B"]) | set(buckets["C"]))

    print("matched sources audited: %d" % len(audits))
    print("A  integer-typed address (u32/int holding a pointer): %d"
          % len(buckets["A"]))
    print("B  hardcoded struct size or field offset:             %d"
          % len(buckets["B"]))
    print("C  cast between an integer and a pointer:             %d"
          % len(buckets["C"]))
    print("any of the three:                                     %d  (%.1f%%)"
          % (len(any_hit), 100.0 * len(any_hit) / max(len(audits), 1)))
    print()
    print("per signal, the number of files at least one hit came from:")
    for signal in ("A1", "A2", "A3", "B1", "B2", "B3"):
        n = sum(1 for a in audits.values()
                if any(h.startswith(signal) for h in a.hits[signal[0]]))
        print("  %s  %d" % (signal, n))
    print()
    print("cross-file prototype conflicts (signal A2): %d" % len(conflicts))
    for name, where, rows in conflicts:
        print("  %-16s %-10s %s" % (name, where,
              ", ".join("%s:%s(%s)" % (os.path.basename(s), r, ",".join(p))
                        for s, r, p in rows)))
    print()
    for key, title in (("A", "A  integer-typed address"),
                       ("B", "B  hardcoded size or offset"),
                       ("C", "C  integer/pointer cast")):
        print("%s -- %d files" % (title, len(buckets[key])))
        for source in buckets[key]:
            print("  %-40s %s" % (source, audits[source].hits[key][0]))
        print()
    return 0


def main(argv=None):
    # Declared before ROOT is read as the --aletheia-root default: a global
    # statement has to precede every use of the name in its function, and
    # Python 3.11 rejects the other order outright rather than warning.
    global ROOT

    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--aletheia-root", default=ROOT,
                    help="path to the aletheia checkout (default: ../aletheia)")
    ap.add_argument("--seed", action="append", default=[],
                    help="matched source path (repeatable)")
    ap.add_argument("--seed-func", action="append", default=[],
                    help="function name whose defining source seeds the walk")
    ap.add_argument("--list-closed", action="store_true",
                    help="every matched source whose externs are all matched/sdk/runtime")
    ap.add_argument("--cmake", action="store_true",
                    help="print the closure as a CMake source list")
    ap.add_argument("--pointer-audit", action="store_true",
                    help="count the matched sources that assume 32-bit "
                         "pointers (docs/pointer-width.md)")
    args = ap.parse_args(argv)

    ROOT = os.path.abspath(args.aletheia_root)

    idx = Index()

    if args.pointer_audit:
        return print_pointer_audit(idx)

    if args.list_closed:
        closed, openish = [], 0
        for source in sorted(idx.sources):
            refs = idx.refs(source)
            bad = [r for r in refs if r[1] in ("open", "data", "alias")]
            if bad:
                openish += 1
            else:
                closed.append(source)
        print("matched sources: %d" % len(idx.sources))
        print("closed (matched + sdk + runtime only): %d" % len(closed))
        print("open (needs a stub or a data provider): %d" % openish)
        for source in closed:
            print("  " + source)
        return 0

    seeds = [idx.resolve_seed(s) for s in args.seed]
    seeds += [idx.resolve_seed(s) for s in args.seed_func]
    if not seeds:
        ap.error("give --seed or --seed-func, or --list-closed")

    sources, open_refs = idx.closure(seeds)

    if args.cmake:
        for source in sorted(sources):
            print("    ${BW_ROOT}/%s" % source)
        return 0

    print("closure of %s: %d matched translation units" % (", ".join(seeds), len(sources)))
    for source in sorted(sources):
        entry = idx.sources[source]
        print("  %-38s %s %-6s %s" % (source, entry["status"], entry["isa"],
                                      ",".join(entry["functions"])))
    print()
    buckets = {}
    for (name, klass, detail), users in sorted(open_refs.items()):
        buckets.setdefault(klass, []).append((name, detail, users))
    for klass in ("sdk", "runtime", "alias", "open", "data"):
        rows = buckets.get(klass, [])
        if not rows:
            continue
        print("%s (%d):" % (klass, len(rows)))
        for name, detail, users in rows:
            print("  %-28s %-12s <- %s" % (name, detail,
                                           ", ".join(os.path.basename(u) for u in users)))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
