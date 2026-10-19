#!/usr/bin/env python3
"""
gamecut.py -- cut function definitions out of a copy of a src/game file.

A src/game translation unit holds a whole original source file, so the port
cannot leave it out to supply one of its functions itself, as it leaves out
a src/matched file (docs/game-tree.md).  Instead the copy it compiles has
those definitions cut out:

  - every function named with --drop (a shim in sdk/, a stubs/reimpl_ file,
    a function the eidos library compiles from elsewhere), and
  - with --asm, every function whose body is an mwcc `asm` block, which a
    host compiler cannot build; sdk/asm/ supplies those by name.

A cut definition becomes its own prototype (`asm` dropped, `static` kept for
a static one), so the rest of the unit still calls it with the right type.
The cut keeps the line count: the body's lines become empty lines, so a
`#line` directive and a compiler's diagnostics still name the original line.
Comments, strings and preprocessor lines are left as they are.

Usage:
    gamecut.py IN.c -o OUT.c [--drop NAME ...] [--drop-list FILE] [--asm]
    gamecut.py --list IN.c          print each top-level function and whether
                                     its body is asm

Standard library only.
"""

from __future__ import annotations

import argparse
import re
import sys

IDENT = re.compile(r"[A-Za-z_]\w*")


def _mask(text):
    """`text` with comments, strings and character constants blanked (same
    length, newlines kept), and preprocessor lines blanked."""
    out = list(text)
    i, n = 0, len(text)
    line_start = True
    while i < n:
        c = text[i]
        if line_start and c in " \t":
            i += 1
            continue
        if line_start and c == "#":
            j = i
            while j < n:
                if text[j] == "\n" and text[j - 1] != "\\":
                    break
                j += 1
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
            continue
        line_start = c == "\n"
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
            continue
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            for k in range(i, min(j + 1, n)):
                if out[k] != "\n":
                    out[k] = " "
            i = j + 1
            continue
        i += 1
    return "".join(out)


def functions(text):
    """[(name, head_start, body_open, body_end, is_asm)] for every top-level
    function definition; positions index `text`, body_end is one past '}'."""
    m = _mask(text)
    out = []
    depth = 0
    start = 0           # start of the current top-level declaration
    i, n = 0, len(m)
    paren = 0
    while i < n:
        c = m[i]
        if c == "(":
            paren += 1
        elif c == ")":
            paren -= 1
        elif c == "{" and paren == 0:
            if depth == 0:
                head = m[start:i]
                hs = head.strip()
                is_func = hs.endswith(")") and "=" not in hs.split("(")[0]
                # struct/union/enum bodies and initialisers are not functions
                if is_func and not re.search(r"\b(struct|union|enum)\s+\w*\s*$", hs.split("(")[0]):
                    j, d = i, 0
                    while j < n:
                        if m[j] == "{":
                            d += 1
                        elif m[j] == "}":
                            d -= 1
                            if d == 0:
                                break
                        j += 1
                    pre = hs.split("(")[0]
                    words = IDENT.findall(pre)
                    name = words[-1] if words else None
                    # an asm function, or a C function with an `asm { }`
                    # statement in it (OSi_DisplayExContext)
                    is_asm = bool(re.search(r"(^|\s)asm(\s|$)", pre)) or \
                        bool(re.match(r"\s*asm\b", m[i + 1:j])) or \
                        bool(re.search(r"\basm\s*(volatile\s*)?\{", m[i + 1:j]))
                    # the head starts after leading whitespace
                    hstart = start + (len(head) - len(head.lstrip()))
                    if name:
                        out.append((name, hstart, i, j + 1, is_asm))
                    i = j + 1
                    start = i
                    continue
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                start = i + 1
        elif c == ";" and depth == 0 and paren == 0:
            start = i + 1
        i += 1
    return out


def cut(text, drop, cut_asm, drop_decls=False):
    """`text` with the chosen definitions replaced by prototypes.

    With `drop_decls` the functions in `drop` leave no declaration at all:
    the definition and every file-scope prototype of the name go, so the
    port's definition (a patch with another type, tools/ovlink.py) is the
    only one the unit sees, through its overlay header."""
    pieces = []
    last = 0
    cut_names = []
    for name, hs, bo, be, is_asm in functions(text):
        if name not in drop and not (cut_asm and is_asm):
            continue
        head = text[hs:bo].rstrip()
        proto = re.sub(r"(^|\s)asm(\s)", r"\1\2", head, count=1).strip()
        # The port defines the cut function in another unit, so it cannot
        # stay static here.
        proto = re.sub(r"^static\s+", "", re.sub(r"\s+", " ", proto)) + ";"
        if drop_decls and name in drop:
            proto = ""
        body_lines = text.count("\n", hs, be) - proto.count("\n")
        pieces.append(text[last:hs])
        pieces.append(proto + "\n" * max(body_lines, 0))
        last = be
        cut_names.append(name)
    pieces.append(text[last:])
    out = "".join(pieces)
    if cut_asm:
        # `asm` on a prototype (`asm void OS_EnableProtectionUnit(void);`):
        # the keyword goes, the declaration stays.
        out = re.sub(r"(?m)^([ \t]*)asm[ \t]+(?=[^;{}\n]*\)[ \t]*;)", r"\1", out)
    # A function declared without `static` and defined with it (biginteger.c's
    # CPSi_big_modinv: `extern void ...;` then `static void ... { }`), which
    # mwcc accepts and GCC refuses: the definition loses `static`.
    changed = True
    while changed:
        changed = False
        for name, hs, bo, _be, _a in functions(out):
            head = out[hs:bo]
            if re.match(r"\s*static\b", head) and re.search(
                    # a declaration: type words, then the name (not a macro
                    # body or a statement that calls it, dgt_md5.c's STEP)
                    r"(?m)^[ \t]*(extern[ \t]+)?(?!static)[A-Za-z_][\w \t\*]*\b%s[ \t]*\([^;{}=]*\)[ \t]*;"
                    % re.escape(name), out[:hs]):
                out = out[:hs] + re.sub(r"^(\s*)static\s+", r"\1", head, count=1) + out[bo:]
                changed = True
                break           # positions moved: scan again
    if drop_decls:
        for name in drop:
            # File-scope prototypes start in column 0; a statement calling
            # the function is indented and has no type in front.
            out = re.sub(r"(?m)^(?:extern[ 	]+)?(?:static[ 	]+)?[A-Za-z_][\w 	\*]*%s"
                         r"[ 	]*\([^;{}=]*\)[ 	]*;[ 	]*$" % re.escape(name), "", out)
    for name in set(cut_names):
        # A cut static function is defined in another unit now: its other
        # declarations here lose `static` too.
        out = re.sub(r"(?m)^([ \t]*)static[ \t]+(?=[^;{}\n]*\b%s[ \t]*\([^;{}]*\)[ \t]*;)"
                     % re.escape(name), r"\1", out)
    return out, cut_names


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("input")
    ap.add_argument("-o", "--output")
    ap.add_argument("--drop", action="append", default=[])
    ap.add_argument("--drop-list", action="append", default=[])
    ap.add_argument("--asm", action="store_true")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args(argv)
    with open(args.input, encoding="utf-8", errors="surrogateescape") as fh:
        text = fh.read()
    if args.list:
        for name, _hs, _bo, _be, is_asm in functions(text):
            print("%s%s" % (name, "  (asm)" if is_asm else ""))
        return 0
    drop = set(args.drop)
    for lst in args.drop_list:
        with open(lst, encoding="utf-8") as fh:
            drop.update(l.strip() for l in fh if l.strip() and not l.startswith("#"))
    out, names = cut(text, drop, args.asm)
    if args.output:
        with open(args.output, "w", encoding="utf-8", errors="surrogateescape", newline="") as fh:
            fh.write(out)
    else:
        sys.stdout.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())


KNR_DEF = re.compile(
    r"(?m)^([A-Za-z_][\w \t\*]*?\b[A-Za-z_]\w*)[ \t]*"
    r"\(([A-Za-z_]\w*(?:[ \t]*,[ \t]*[A-Za-z_]\w*)*)\)[ \t]*\n"
    r"((?:[ \t]*[A-Za-z_][^;{}()\n]*;[ \t]*\n)+)([ \t]*)\{")


def knr_to_ansi(text):
    """`text` with each old-style (K&R) definition head rewritten as a
    prototype-style one, for the scanners only (tools/ovlink.py Scan,
    tools/closure.py SourceScan, tools/armdata.py SourceFile): they find a
    function by the `)` in front of its `{`, and an identifier list with its
    declarations after it hides the definition and, after it, the rest of
    the file.  src/game keeps K&R where mwcc's argument promotions need it
    (ov010/symbol_map.c and a few more).  The line count is kept."""
    def one(m):
        names = [n.strip() for n in m.group(2).split(",")]
        decl = {}
        for line in m.group(3).split("\n"):
            line = line.strip().rstrip(";").strip()
            if not line:
                continue
            parts = line.split(",")
            first = parts[0].strip()
            mm = re.match(r"^(.*?)([\*\s]*)\b([A-Za-z_]\w*)\s*(\[[^\]]*\])?$", first)
            if not mm:
                continue
            base = mm.group(1).strip()
            decl[mm.group(3)] = "%s %s%s%s" % (base, mm.group(2).strip(), mm.group(3),
                                              mm.group(4) or "")
            for p in parts[1:]:
                pm = re.match(r"^\s*([\*\s]*)\b([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*$", p)
                if pm:
                    decl[pm.group(2)] = "%s %s%s%s" % (base, pm.group(1).strip(),
                                                      pm.group(2), pm.group(3) or "")
        params = ", ".join(decl.get(n, "int " + n) for n in names)
        lines = m.group(0).count("\n")
        return "%s(%s)%s%s{" % (m.group(1), params, "\n" * lines, m.group(4))
    return KNR_DEF.sub(one, text)
