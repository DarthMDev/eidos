#!/usr/bin/env python3
"""
dsaddr.py -- rewrite a matched source's fixed DS addresses for the host.

A matched file reaches DS hardware and fixed RAM through casts of integer
literals to pointers (`*(vu16 *)0x04000208 = 1`).  On a host those are wild
pointers.  This tool copies a matched source into the build tree and turns
every cast of an integer literal whose value lies in a DS region
(0x02000000-0x07FFFFFF) into a call of sdk/dsmem.h's translator:

    (vu16 *)0x04000208            ->  ((vu16 *)bw_ds_ptr(0x04000208u))
    (volatile u32 *)(0x04000000)  ->  ((volatile u32 *)bw_ds_ptr(0x04000000u))
    (u8 *)(0x06000000 + off)      ->  ((u8 *)bw_ds_ptr(0x06000000 + off))

docs/boot.md, "Fixed addresses", is the design.  The rewrite is textual and
local: comments, strings, character constants and every literal that is not
the operand of a pointer cast are left byte for byte as they were, and the
line structure is kept, so a `#line` directive makes diagnostics and
__FILE__ name the original file and line.

A file with something to rewrite gets a three-line prelude: a comment, a
declaration of bw_ds_ptr and `#line 1 "<original path>"`.  The declaration
is spelled with `unsigned int` rather than by including sdk/dsmem.h,
because a matched translation unit carries its own u8/u16/u32 typedefs and
must not see a second set (sdk/types.h's header comment).  `unsigned int`
is the type uint32_t names on every target eidos builds for;
tests/test_dsaddr.py compiles the prelude next to sdk/dsmem.h to prove the
two declarations agree on the compiler in use.

A file with nothing to rewrite is copied byte for byte.

What is deliberately not rewritten, and reported by --survey instead:

  - DS-range literals outside a pointer cast (`#define REG_BASE 0x04000000`,
    arithmetic, comparisons, integer casts): the value is a number there,
    and whether it is used as an address is only known at run time;
  - casts to a function pointer type: bw_ds_ptr returns host data memory,
    and calling it would be no more correct than calling the DS address;
  - pointer casts in the initializer of a file-scope or `static` object,
    which C requires to be a constant expression; a call is not one.

Usage:
    dsaddr.py IN.c -o OUT.c [--name PATH]   rewrite one file
    dsaddr.py --survey DIR [--json FILE]     report casts and residue
    dsaddr.py --check IN.c ...               exit 1 if any file would change

Standard library only.
"""

from __future__ import annotations

import argparse
import bisect
import json
import os
import re
import sys
from collections import Counter, defaultdict

DS_LO = 0x02000000
DS_HI = 0x07FFFFFF

REGIONS = (
    (0x02000000, 0x02FFFFFF, "main"),
    (0x03000000, 0x03FFFFFF, "wram"),
    (0x04000000, 0x04FFFFFF, "io"),
    (0x05000000, 0x05FFFFFF, "palette"),
    (0x06000000, 0x06FFFFFF, "vram"),
    (0x07000000, 0x07FFFFFF, "oam"),
)

PRELUDE_DECL = "void *bw_ds_ptr(unsigned int);"

QUALIFIERS = {"const", "volatile", "restrict", "__restrict", "__volatile__"}
# Identifiers that cannot end a type name; a parenthesised group made of
# them is an expression, not a cast.
NON_TYPE_WORDS = {"sizeof", "return", "case", "if", "while", "for", "switch",
                  "do", "else", "goto", "_Alignof", "defined"}

_INT_RE = re.compile(r"(?<![\w.])(0[xX][0-9a-fA-F]+|[0-9]+)([uUlL]*)(?![\w.])")
_IDENT_RE = re.compile(r"[A-Za-z_]\w*")
_PTR_TYPEDEF_RE = re.compile(
    r"\btypedef\b[^;{}()]*\*\s*(?:(?:const|volatile|restrict)\s+)*([A-Za-z_]\w*)\s*;")


def region_of(value: int) -> str | None:
    for lo, hi, name in REGIONS:
        if lo <= value <= hi:
            return name
    return None


def int_value(digits: str) -> int:
    if digits[:2] in ("0x", "0X"):
        return int(digits, 16)
    if len(digits) > 1 and digits[0] == "0":
        try:
            return int(digits, 8)
        except ValueError:
            return -1
    return int(digits, 10)


# ---------------------------------------------------------------------------
# Masking: comments and literal contents become blanks of the same length
# ---------------------------------------------------------------------------

def mask(src: str) -> str:
    """Return `src` with every comment replaced by spaces and the contents of
    every string and character literal replaced by 'x', newlines kept, so
    offsets into the mask are offsets into the source and nothing inside a
    comment or a literal can look like code."""
    out = list(src)
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            j = i
            while j < n and src[j] != "\n":
                if src[j] == "\\" and src.startswith("\n", j + 1):
                    j += 2
                    continue
                if src[j] == "\\" and src.startswith("\r\n", j + 1):
                    j += 3
                    continue
                j += 1
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            for k in range(i + 1, min(j, n)):
                if out[k] != "\n":
                    out[k] = "x"
            i = j + 1
        else:
            i += 1
    return "".join(out)


def _spliced(m: str, nl: int) -> bool:
    """Whether the newline at `nl` ends a line with a backslash splice
    (LF or CRLF line endings)."""
    j = nl - 1
    if j >= 0 and m[j] == "\r":
        j -= 1
    return j >= 0 and m[j] == "\\"


def _skip_ws_back(m: str, i: int) -> int:
    """Index of the last non-blank character at or before i (blank includes a
    backslash-newline splice), or -1."""
    while i >= 0:
        c = m[i]
        if c in " \t\r\n\f\v":
            i -= 1
        elif c == "\\" and i + 1 < len(m) and m[i + 1] in "\r\n":
            i -= 1
        else:
            return i
    return -1


def _skip_ws_fwd(m: str, i: int) -> int:
    n = len(m)
    while i < n:
        c = m[i]
        if c in " \t\r\n\f\v":
            i += 1
        elif c == "\\" and i + 1 < n and m[i + 1] in "\r\n":
            i += 1
        else:
            return i
    return n


def _match_back(m: str, close: int) -> int:
    """Index of the '(' matching the ')' at `close`, or -1."""
    depth = 0
    i = close
    while i >= 0:
        c = m[i]
        if c == ")":
            depth += 1
        elif c == "(":
            depth -= 1
            if depth == 0:
                return i
        elif c in ";{}":
            return -1
        i -= 1
    return -1


def _match_fwd(m: str, open_: int) -> int:
    depth = 0
    i = open_
    n = len(m)
    while i < n:
        c = m[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i
        elif c in ";{}":
            return -1
        i += 1
    return -1


def classify_type(text: str, ptr_typedefs: set[str]) -> str:
    """What the contents of a parenthesised group before a literal are:
    'ptr' (an object pointer type name), 'funcptr', 'int' (a type name with
    no pointer), or 'expr' (not a type name at all)."""
    t = re.sub(r"\\\r?\n", " ", text).strip()
    if not t:
        return "expr"
    if "(" in t:
        # An abstract declarator in parentheses: `R (*)(A)` is a function
        # pointer, `R (**)(A)` a pointer to one (a data slot) and
        # `T (*)[N]` a pointer to an array; the last two are object
        # pointers and are rewritten like any other.
        d = re.search(r"\(\s*(\*[\s\w*]*)\)\s*(.)?", t)
        if not d:
            return "expr"
        if d.group(1).count("*") >= 2 or d.group(2) == "[":
            return "ptr"
        return "funcptr"
    if re.search(r"[^\w\s*]", t):
        return "expr"
    words = _IDENT_RE.findall(t)
    if not words or words[0] in NON_TYPE_WORDS or words[-1] in NON_TYPE_WORDS:
        return "expr"
    if "*" in t:
        # identifiers, then the pointer part: stars and qualifiers only
        head, _, tail = t.partition("*")
        if not _IDENT_RE.search(head):
            return "expr"
        if any(w not in QUALIFIERS for w in _IDENT_RE.findall(tail)):
            return "expr"
        return "ptr"
    base = [w for w in words if w not in QUALIFIERS]
    if base and base[-1] in ptr_typedefs:
        return "ptr"
    # Two identifiers in a row is only a type name (`unsigned int`); a
    # lone identifier is a type name or a macro parameter / variable.
    return "int"


_DEFINE_RE = re.compile(r"[ \t]*#[ \t]*define\b")
# Keywords after which a parenthesised group starts an expression (so may be
# a cast), where any other identifier would make it a call's argument list.
EXPR_KEYWORDS = {"return", "case", "else", "do", "sizeof", "goto"}


def _is_call_paren(m: str, open_: int, ptr_typedefs: set[str]) -> bool:
    """Whether the '(' at `open_` opens a call's argument list (or an
    `if`/`while` condition), so the group cannot be a cast."""
    before = _skip_ws_back(m, open_ - 1)
    if before < 0:
        return False
    c = m[before]
    if c.isalnum() or c == "_":
        s = before
        while s > 0 and (m[s - 1].isalnum() or m[s - 1] == "_"):
            s -= 1
        return m[s:before + 1] not in EXPR_KEYWORDS
    if c == "]":
        return True
    if c == ")":
        # `(u32)(u8 *)0x...` is a cast of a cast; `f(a)(...)` and
        # `if (x)(...)` are not
        o = _match_back(m, before)
        if o < 0 or _is_call_paren(m, o, ptr_typedefs):
            return True
        return classify_type(m[o + 1:before], ptr_typedefs) == "expr"
    return False


# ---------------------------------------------------------------------------
# Scope: is an offset at file scope (where an initializer must be constant)
# ---------------------------------------------------------------------------

def _scope_map(m: str):
    """For each '{' decide whether it opens a function body (or a block in
    one) or a brace initializer / struct body at file scope, and return a
    function telling whether an offset lies outside every function body.
    Preprocessor lines are skipped: a macro body is not at any scope."""
    events = []          # (offset, +1/-1 for function depth)
    stack = []           # True when the brace counts as function depth
    func_depth = 0
    line_start = True
    in_pp = False
    i, n = 0, len(m)
    while i < n:
        c = m[i]
        if c == "\n":
            if not _spliced(m, i):
                in_pp = False
            line_start = True
            i += 1
            continue
        if line_start and c == "#":
            in_pp = True
        if c not in " \t\r\f\v":
            line_start = False
        if in_pp:
            i += 1
            continue
        if c == "{":
            if func_depth > 0:
                is_func = True
            else:
                p = _skip_ws_back(m, i - 1)
                # `) {` is a function body; `= {`, `, {`, `{ {`, a struct
                # or enum tag is an initializer or a type body.
                is_func = p >= 0 and m[p] == ")"
            stack.append(is_func)
            if is_func:
                func_depth += 1
                events.append((i, func_depth))
        elif c == "}":
            if stack and stack.pop():
                func_depth -= 1
                events.append((i, func_depth))
        i += 1

    def at_file_scope(off: int) -> bool:
        depth = 0
        for pos, d in events:
            if pos > off:
                break
            depth = d
        return depth == 0
    return at_file_scope


def _in_static_init(m: str, off: int) -> bool:
    """Whether `off`, inside a function, lies in the initializer of a
    `static` local, which C also requires to be a constant expression."""
    braces = 0
    i = off - 1
    while i >= 0:
        c = m[i]
        if c == "}":
            braces += 1
        elif c == "{":
            if braces:
                braces -= 1
            else:
                p = _skip_ws_back(m, i - 1)
                if p < 0 or m[p] not in "=,{":
                    break           # a block: the statement starts here
        elif c == ";" and not braces:
            break
        i -= 1
    seg = m[i + 1:off]
    return bool(re.search(r"\bstatic\b", seg)) and "=" in seg


def _pp_line_bounds(m: str, off: int):
    """(start, is_preprocessor) of the logical line holding `off`."""
    s = off
    while True:
        s = m.rfind("\n", 0, s)
        if s <= 0 or not _spliced(m, s):
            break
    s = 0 if s < 0 else s + 1
    k = _skip_ws_fwd(m, s)
    return s, k < len(m) and m[k] == "#"


# ---------------------------------------------------------------------------
# The rewrite
# ---------------------------------------------------------------------------

class Site:
    __slots__ = ("kind", "line", "value", "region", "start", "end", "text",
                 "type", "form")

    def __init__(self, kind, line, value, start, end, text, type_=None, form=None):
        self.kind = kind          # 'rewrite' or a residue kind
        self.line = line
        self.value = value
        self.region = region_of(value)
        self.start = start
        self.end = end
        self.text = text
        self.type = type_
        self.form = form


def analyse(src: str):
    """Find every DS-range integer literal in code and decide what to do with
    it.  Returns (rewrites, residue): rewrites are Sites with start/end/text
    to splice in, non-overlapping and sorted; residue are Sites left alone."""
    m = mask(src)
    ptr_typedefs = set(_PTR_TYPEDEF_RE.findall(m))
    at_file_scope = None
    rewrites, residue = [], []
    taken_until = -1

    starts = [0] + [nl.end() for nl in re.finditer("\n", src)]

    def line_of(off):
        return bisect.bisect_right(starts, off)

    for lit in _INT_RE.finditer(m):
        value = int_value(lit.group(1))
        if not (DS_LO <= value <= DS_HI):
            continue
        ls, lend = lit.start(), lit.end()
        if ls < taken_until:
            continue        # inside an operand already rewritten
        line = line_of(ls)
        pp_start, in_pp = _pp_line_bounds(m, ls)
        pp_kind = None
        if in_pp:
            pp_kind = ("define" if _DEFINE_RE.match(m, pp_start)
                       else "directive")

        # Peel parentheses wrapped tightly round the literal.
        a, b = ls, lend
        while True:
            p = _skip_ws_back(m, a - 1)
            q = _skip_ws_fwd(m, b)
            if p >= 0 and m[p] == "(" and q < len(m) and m[q] == ")":
                pp = _skip_ws_back(m, p - 1)
                # `f(0x..)`/`sizeof(0x..)`: a call's parentheses, not a wrapper
                if pp >= 0 and (m[pp].isalnum() or m[pp] == "_"):
                    break
                a, b = p, q + 1
                continue
            break
        form = "literal" if (a, b) == (ls, lend) else "paren"
        p = _skip_ws_back(m, a - 1)

        # `(T *)(0x06000000 + off)`: the literal opens a parenthesised
        # operand that is more than the literal.
        if p >= 0 and m[p] == "(":
            pp = _skip_ws_back(m, p - 1)
            if pp >= 0 and m[pp] == ")":
                close = _match_fwd(m, p)
                if close > 0:
                    a, b = p, close + 1
                    form = "expr"
                    p = pp
        cast_kind = None
        if p >= 0 and m[p] == ")":
            open_ = _match_back(m, p)
            if open_ >= 0:
                if _is_call_paren(m, open_, ptr_typedefs):
                    cast_kind = "expr"
                else:
                    cast_kind = classify_type(m[open_ + 1:p], ptr_typedefs)
        if cast_kind == "ptr":
            if at_file_scope is None:
                at_file_scope = _scope_map(m)
            if not in_pp and (at_file_scope(open_) or _in_static_init(m, open_)):
                residue.append(Site("static-init", line, value, open_, b,
                                    None, src[open_ + 1:p].strip(), form))
                taken_until = b
                continue
            type_text = src[open_ + 1:p].strip()
            if form == "expr":
                inner = src[a + 1:b - 1].strip()
            else:
                inner = "0x%08Xu" % value
            new = "((%s)bw_ds_ptr(%s))" % (type_text, inner)
            if in_pp:
                form += "-in-" + pp_kind    # inside a macro body
            rewrites.append(Site("rewrite", line, value, open_, b, new,
                                 type_text, form))
            taken_until = b
            continue
        if cast_kind == "funcptr":
            kind = "funcptr-cast"
        elif cast_kind == "int":
            kind = "int-cast"
        elif pp_kind == "define":
            kind = "define"
        elif pp_kind == "directive":
            kind = "directive"
        else:
            kind = "bare"
        residue.append(Site(kind, line, value, ls, lend, None,
                            src[open_ + 1:p].strip() if cast_kind else None))
    return rewrites, residue


def rewrite(src: str, name: str):
    """The rewritten text of `src` (identical when there is nothing to
    rewrite) and the analysis behind it."""
    rewrites, residue = analyse(src)
    if not rewrites:
        return src, rewrites, residue
    parts, pos = [], 0
    for s in rewrites:
        parts.append(src[pos:s.start])
        parts.append(s.text)
        # A cast split over lines keeps its line count, so everything after
        # it keeps its line number (inside a macro body, as a splice).
        lost = src.count("\n", s.start, s.end) - s.text.count("\n")
        if lost > 0:
            parts.append(("\\\n" if "-in-" in s.form else "\n") * lost)
        pos = s.end
    parts.append(src[pos:])
    body = "".join(parts)
    shown = name.replace("\\", "/").replace('"', '\\"')
    prelude = ("/* tools/dsaddr.py: fixed DS addresses go through bw_ds_ptr */\n"
               "%s\n#line 1 \"%s\"\n" % (PRELUDE_DECL, shown))
    return prelude + body, rewrites, residue


# ---------------------------------------------------------------------------
# Command line
# ---------------------------------------------------------------------------

def _read(path: str) -> str:
    with open(path, "rb") as f:
        return f.read().decode("latin-1")


def _write(path: str, text: str) -> None:
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    data = text.encode("latin-1")
    try:
        with open(path, "rb") as f:
            if f.read() == data:
                # Unchanged: keep the timestamp moving so a build tool that
                # compares it with the input's does not rerun us forever.
                os.utime(path, None)
                return
    except OSError:
        pass
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def _normalise_context(src: str, site: Site) -> str:
    s = src.rfind("\n", 0, site.start) + 1
    e = src.find("\n", site.end)
    e = len(src) if e < 0 else e
    line = src[s:e].strip()
    line = re.sub(r"//.*$", "", line)
    line = re.sub(r"/\*.*?\*/", "", line).strip()
    line = re.sub(r"\b0[xX][0-9a-fA-F]+[uUlL]*\b", "N", line)
    line = re.sub(r"\b\d+[uUlL]*\b", "n", line)
    line = re.sub(r"\b(?:func|sub|unit|g|data|ov\d+_func|D)_[0-9A-Fa-f_]+\w*", "SYM", line)
    line = re.sub(r"\s+", " ", line)
    return line[:110]


def survey(root: str, json_out: str | None, show: int) -> int:
    files = []
    for d, _, names in os.walk(root):
        for nm in names:
            if nm.endswith(".c"):
                files.append(os.path.join(d, nm))
    files.sort()
    casts = Counter()
    cast_forms = Counter()
    cast_types = Counter()
    resid = Counter()
    resid_groups = defaultdict(list)
    changed = 0
    for path in files:
        src = _read(path)
        rel = os.path.relpath(path, root).replace("\\", "/")
        rewrites, residue = analyse(src)
        if rewrites:
            changed += 1
        for s in rewrites:
            casts[s.region] += 1
            cast_forms[s.form] += 1
            cast_types[re.sub(r"\s+", " ", s.type)] += 1
        for s in residue:
            resid[(s.kind, s.region)] += 1
            if s.kind == "int-cast" or s.kind == "funcptr-cast":
                key = (s.kind, s.region, "(%s)N" % re.sub(r"\s+", " ", s.type or ""))
            else:
                key = (s.kind, s.region, _normalise_context(src, s))
            resid_groups[key].append("%s:%d" % (rel, s.line))
    total = sum(casts.values())
    print("files scanned: %d, files rewritten: %d" % (len(files), changed))
    print("pointer casts rewritten: %d" % total)
    for r, c in sorted(casts.items(), key=lambda x: -x[1]):
        print("  %-8s %6d" % (r, c))
    print("  by form: " + ", ".join("%s %d" % kv for kv in cast_forms.most_common()))
    print("  types (top 25): " + ", ".join("%s %d" % kv for kv in cast_types.most_common(25)))
    print("residue (DS-range literals left alone), by kind and region:")
    for (k, r), c in sorted(resid.items(), key=lambda x: (x[0][0], -x[1])):
        print("  %-16s %-8s %6d" % (k, r, c))
    if show:
        print("residue groups (I/O and non-main regions, and every cast form):")
        for key, sites in sorted(resid_groups.items(), key=lambda kv: (kv[0][0], kv[0][1], -len(kv[1]))):
            kind, region, pat = key
            if region == "main" and kind in ("bare", "define", "directive"):
                continue
            ex = ", ".join(sites[:show]) + (" ..." if len(sites) > show else "")
            print("  [%s %s x%d] %s\n      %s" % (kind, region, len(sites), pat, ex))
    if json_out:
        data = {
            "files": len(files), "files_rewritten": changed,
            "casts": dict(casts), "forms": dict(cast_forms),
            "residue": [{"kind": k, "region": r, "pattern": p, "sites": s}
                        for (k, r, p), s in sorted(resid_groups.items())],
        }
        with open(json_out, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=1)
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("inputs", nargs="*")
    ap.add_argument("-o", "--output")
    ap.add_argument("--name", help="path the #line directive names (default: the input)")
    ap.add_argument("--survey", metavar="DIR")
    ap.add_argument("--json", metavar="FILE")
    ap.add_argument("--show", type=int, default=4, metavar="N",
                    help="--survey: example sites per residue group (0: none)")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if any input would be rewritten")
    a = ap.parse_args(argv)
    if a.survey:
        return survey(a.survey, a.json, a.show)
    if a.check:
        bad = 0
        for p in a.inputs:
            _, rw, _ = rewrite(_read(p), p)
            for s in rw:
                print("%s:%d: 0x%08X (%s)" % (p, s.line, s.value, s.type))
            bad += bool(rw)
        return 1 if bad else 0
    if len(a.inputs) != 1 or not a.output:
        ap.error("give one input and -o OUTPUT, or --survey DIR, or --check")
    src_path = a.inputs[0]
    name = a.name or os.path.abspath(src_path)
    text, _, _ = rewrite(_read(src_path), name)
    _write(a.output, text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
