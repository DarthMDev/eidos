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
  src/matched/*.c            the `symbols:` header block of each matched
                             file and every name it needs from elsewhere:
                             extern declarations, file-scope prototypes,
                             and calls with no declaration in scope
  layout/symbols_sdk.txt     SDK library symbols (address + name)
  layout/runtime_arm9.txt    mwccarm/MSL runtime helpers (address + name)

Every referenced name is put in exactly one class:

  matched   defined by another EXACT matched source           (link it)
  alias     a matched function reached by address under
            another spelling                                  (rename)
  const     a linker symbol whose address is its value
            (SDK_IRQ_STACKSIZE, ov010_id)                     (bootlink.py)
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
import bisect
import glob
import json
import os
import re
import sys

EIDOS_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.normpath(os.path.join(EIDOS_ROOT, "..", "aletheia"))
# Which of aletheia's trees the port compiles: "matched" (src/matched, one
# function or unit per file, found through the manifests) or "game"
# (src/game, one original source file per file; tools/gametree.py).  The
# build sets it (CMake's EIDOS_SOURCE_TREE) in the environment of every
# generator (docs/game-tree.md).
SOURCE_TREE = os.environ.get("EIDOS_SOURCE_TREE", "matched")

ADDR_NAME = re.compile(r"^(?:sub|func|unit)_0([0-9A-Fa-f]{7})$")
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
SYMBOL_ROW = re.compile(r"^\s*\*\s+([A-Za-z_][A-Za-z0-9_]*)\s+(0x[0-9A-Fa-f]{1,8})\s+(thumb|arm|data)\b(?:\s+\S+)*\s*$")
SYMBOLS_HDR = re.compile(r"^\s*\*\s*symbols:\s*$")
CALL = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
MEMBER = re.compile(r"(?:\.|->)\s*[A-Za-z_]\w*")
ASM_BODY = re.compile(r"\b(?:asm|__asm)\b")
ATTRIBUTE = re.compile(r"\b__attribute__\s*\(")
TAG_BODY = re.compile(r"\b(struct|union|enum)\b(\s+[A-Za-z_]\w*)?\s*\{")
TAG_NAME = re.compile(r"\b(struct|union|enum)\s+(?!_tag\b)[A-Za-z_]\w*")
LOCAL_DECL = re.compile(r"\b([A-Za-z_]\w*)\s+\**\s*([A-Za-z_]\w*)\s*(?=[=;,\[)])")
FUNC_PTR_DECL = re.compile(r"\(\s*\*+\s*(?:const\s+|volatile\s+)*([A-Za-z_]\w*)\s*[\)\[]")
# `T (*name(params))[6]` / `T (*name(params))(void)`: a function returning a
# pointer, whose declarator the object form above does not match (the name is
# followed by `(`, a parameter list, not `)` or `[`).
RET_PTR_DECL = re.compile(r"\(\s*\*+\s*(?:const\s+|volatile\s+)*([A-Za-z_]\w*)\s*\(")
# A quoted `#include`, the form a matched file uses for aletheia's headers.
INCLUDE_QUOTED = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)

# C's reserved words plus the spellings matched files use for mwcc
# extensions; none of them can name a symbol.
C_RESERVED = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while", "asm", "__asm", "__attribute__",
    "__inline", "_Bool", "defined", "nofralloc", "naked",
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


# ---------------------------------------------------------------------------
# Reading one matched source
# ---------------------------------------------------------------------------
#
# A matched file names the symbols it needs in three ways, and the closure
# has to see all three, because the host linker does:
#
#   - an `extern` declaration (object or function), possibly with an
#     __attribute__, a struct body or several declarators;
#   - a file-scope prototype without `extern` (`void func_02085B20(void);`);
#   - a call to a function with no declaration at all.  mwcc accepts the
#     implicit declaration and so does the DS build; a host compiler needs a
#     prototype (tools/bootlink.py writes one), but the linker needs the
#     definition either way.
#
# A declared name counts only when the file uses it outside its own
# declaration: in a function body, an initializer or a macro body.  A name
# that is declared and never used costs the linker nothing.

def strip_source(text):
    """Comments, string and character literals blanked; line splices joined."""
    text = text.replace("\\\r\n", " ").replace("\\\n", " ")
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(" " + "\n" * text.count("\n", i, j))
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _matching(text, i, open_ch, close_ch):
    """Index just past the bracket that closes text[i] (== open_ch)."""
    depth = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return n


def _drop_attributes(text):
    while True:
        m = ATTRIBUTE.search(text)
        if not m:
            return text
        end = _matching(text, m.end() - 1, "(", ")")
        text = text[:m.start()] + " " + text[end:]


def _split_top(text, sep):
    """Split on `sep` where no (), [] or {} is open."""
    parts, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == sep and depth == 0:
            parts.append(text[start:i])
            start = i + 1
    parts.append(text[start:])
    return parts


def is_link_constant(name, addr, kind):
    """A linker-defined symbol whose *address* is the value the code uses.

    `extern u8 SDK_IRQ_STACKSIZE[]; ... (u32)SDK_IRQ_STACKSIZE` is 0x800 on
    the DS because the linker command file sets the symbol's address to
    0x800; overlay ids (`ov010_id`) work the same way.  A host linker cannot
    place a symbol at an absolute address portably, so these are their own
    class: every SDK_ linker symbol, and every data symbol whose address is
    below main RAM.
    """
    if name.startswith("SDK_"):
        return True
    return kind == "data" and addr is not None and addr < 0x02000000


def _include_candidates(cur, inc, include_dirs):
    """Where a quoted `#include` is looked for: the including file's own
    directory first (the compiler's first search for `"..."`), then the
    build's include directories."""
    if os.path.isabs(inc):
        return [inc]
    out = [os.path.normpath(os.path.join(os.path.dirname(cur), inc))]
    out += [os.path.normpath(os.path.join(d, inc)) for d in include_dirs]
    return out


def included_declarations(path, include_dirs=()):
    """Names the quoted headers a source #includes declare, transitively.

    The generated forced-include header (tools/bootlink.py, tools/ovlink.py)
    is compiled before the file's own `#include`s, so it cannot see them.  A
    name the file both calls and gets from a header must therefore not get a
    non-prototype declaration of its own: the header's declaration would
    conflict with it.  Resolve the quoted includes here and return the union
    of the included headers' declared names, so the generator can leave them
    alone.
    """
    start = os.path.abspath(path)
    seen = set()
    out = set()
    stack = [start]
    while stack:
        cur = stack.pop()
        key = os.path.normcase(os.path.abspath(cur))
        if key in seen:
            continue
        seen.add(key)
        try:
            with open(cur, encoding="utf-8", errors="replace") as fh:
                text = fh.read()
        except OSError:
            continue
        if os.path.abspath(cur) != start:
            out |= SourceScan(text).declared
        for inc in INCLUDE_QUOTED.findall(text):
            for cand in _include_candidates(cur, inc, include_dirs):
                if os.path.exists(cand):
                    stack.append(cand)
                    break
    return out


class SourceScan:
    """What one matched source defines, declares, uses and calls."""

    def __init__(self, text):
        self.symbols = self._parse_symbols(text)
        self.defined = set()       # functions and objects with a body here
        self.statics = set()       # file-local names (static)
        self.declared = set()      # extern or prototype, not static
        self.prototyped = set()    # declared as a function (any linkage)
        self.used = set()          # identifiers outside declarations
        self.called = set()        # identifiers followed by '('
        self.macros = set()
        self.typedefs = set()
        self.locals = set()        # parameters and local function pointers
        self.heads = {}            # defined function -> its declarator text
        code = strip_source(text)
        self.has_asm = bool(ASM_BODY.search(code))
        self._scan(code)

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

    def _use(self, text):
        # `s.callback(...)` and `p->next` name members, not symbols.
        text = MEMBER.sub(" ", text)
        self.used.update(IDENT.findall(text))
        self.called.update(CALL.findall(text))

    def _scan(self, code):
        body_lines = []
        for line in code.split("\n"):
            s = line.lstrip()
            if s.startswith("#"):
                m = re.match(r"#\s*define\s+([A-Za-z_]\w*)(\([^)]*\))?(.*)", s)
                if m:
                    self.macros.add(m.group(1))
                    self._use(m.group(3))
                body_lines.append("")
            else:
                body_lines.append(line)
        code = _drop_attributes("\n".join(body_lines))

        i, n, start = 0, len(code), 0
        while i < n:
            c = code[i]
            if c == "(":
                i = _matching(code, i, "(", ")")
                continue
            if c == "[":
                i = _matching(code, i, "[", "]")
                continue
            if c == "{":
                head = code[start:i]
                stripped = head.rstrip()
                tag = TAG_BODY.search(head + "{")
                is_tag = tag is not None and tag.end() == len(head) + 1
                if stripped.endswith(")") and not is_tag \
                        and "=" not in _split_top(head, ",")[-1]:
                    end = _matching(code, i, "{", "}")
                    self._function(head, code[i:end])
                    i = start = end
                    continue
                i = _matching(code, i, "{", "}")
                continue
            if c == ";":
                self._declaration(code[start:i])
                start = i + 1
            i += 1

    def _function(self, head, body):
        head = head.strip()
        name = None
        m = FUNC_PTR_DECL.search(head)
        if m and head.find("(") == m.start():
            name = m.group(1)
        else:
            rp = RET_PTR_DECL.search(head)
            if rp and head.find("(") == rp.start():
                name = rp.group(1)
            else:
                words = IDENT.findall(head.split("(")[0])
                if words:
                    name = words[-1]
        if name:
            self.defined.add(name)
            self.heads[name] = head
            if re.search(r"\bstatic\b", head.split("(")[0]):
                self.statics.add(name)
        # Parameters and local function pointers can be called; they are
        # never symbols.
        if "(" in head:
            params = head[head.find("(") + 1:head.rfind(")")]
            for param in _split_top(params, ","):
                fp = FUNC_PTR_DECL.search(param)
                idents = [w for w in IDENT.findall(re.sub(r"\[[^\]]*\]", " ", param))
                          if w not in C_RESERVED]
                if fp:
                    self.locals.add(fp.group(1))
                elif idents:
                    self.locals.add(idents[-1])
        self.locals.update(fp.group(1) for fp in FUNC_PTR_DECL.finditer(body))
        for typ, var in LOCAL_DECL.findall(body):
            if typ in self.typedefs or typ in C_RESERVED:
                self.locals.add(var)
        self._use(body)

    def _declaration(self, decl):
        decl = decl.strip()
        if not decl:
            return
        # Struct, union and enum bodies declare members and enumerators,
        # never symbols; an initializer's braces are kept (they hold refs).
        pos = 0
        while True:
            m = TAG_BODY.search(decl, pos)
            if not m:
                break
            end = _matching(decl, m.end() - 1, "{", "}")
            repl = " %s _tag " % m.group(1)
            decl = decl[:m.start()] + repl + decl[end:]
            pos = m.start() + len(repl)
        # A tag name is a type, never a declarator (`struct Obj;`,
        # `struct Obj *p;`).
        decl = TAG_NAME.sub(lambda t: t.group(1) + " _tag", decl)
        head_words =IDENT.findall(_split_top(decl, "=")[0].split("(")[0])
        is_typedef = "typedef" in head_words
        is_extern = "extern" in head_words
        is_static = "static" in head_words
        for piece in _split_top(decl, ","):
            parts = _split_top(piece, "=")
            declarator = parts[0]
            if len(parts) > 1:
                self._use("=".join(parts[1:]))
            # `T (*name)(...)` is an object; a parameter list's own `(*cb)`
            # is not the declarator, so only a match at the first '(' counts.
            m = FUNC_PTR_DECL.search(declarator)
            if m and m.start() != declarator.find("("):
                m = None
            rp = RET_PTR_DECL.search(declarator)
            if rp and rp.start() != declarator.find("("):
                rp = None
            if rp:
                # `T (*name(params))[6]`: a function returning a pointer.
                name, is_func = rp.group(1), True
            elif m:
                name, is_func = m.group(1), False
            elif "(" in declarator:
                idents = IDENT.findall(declarator.split("(")[0])
                name, is_func = (idents[-1] if idents else None), True
            else:
                idents = [w for w in IDENT.findall(re.sub(r"\[[^\]]*\]", " ", declarator))
                          if w not in C_RESERVED]
                name, is_func = (idents[-1] if idents else None), False
            if not name or name in C_RESERVED or name == "_tag":
                continue
            # Array bounds may name enumerators or macros; they are uses.
            for bound in re.findall(r"\[([^\]]*)\]", declarator):
                self._use(bound)
            if is_typedef:
                self.typedefs.add(name)
            elif is_static:
                self.statics.add(name)
                if not is_func:
                    self.defined.add(name)
            elif is_extern or is_func:
                self.declared.add(name)
                if is_func:
                    self.prototyped.add(name)
            else:
                self.defined.add(name)

    def references(self, universe):
        """(every name this file needs from elsewhere, the implicit calls)."""
        local = self.defined | self.statics | self.macros | self.typedefs | C_RESERVED
        refs = (self.declared & self.used) - local
        # A name the file's own `symbols:` block carries is a real reference
        # even when the manifest's canonical spelling for that address differs
        # (a caller keeps a descriptive name while the definition keeps the
        # address name).  The DS resolves it by address; classify() turns it
        # into an alias the port renames, and bootlink.py declares the callee.
        implicit = {name for name in self.called - self.declared - local - self.locals
                    if universe(name) or name in self.symbols}
        return sorted(refs | implicit), sorted(implicit)


# Linker veneers in the ARM9 static module: 12-byte islands the DS linker
# placed for a far call, `ldr ip, [pc]; bx ip` (0xE59FC000 0xE12FFF1C) and the
# target word.  They have no source; a caller that names the veneer reaches
# the target, so the walk follows the veneer to the target's source (class
# `alias`), and tools/bootlink.py renames the veneer to whatever defines the
# target.  Each target is the third word at the veneer's address in the
# game's arm9_dec.bin (static module at 0x02004000), read once and recorded
# here: the build reads no game data.
VENEERS = {
    0x020377AC: 0x020380E4,
    0x02072788: 0x0208970C,   # veneer_02072788 -> SND_SetMasterVolume
    0x02072920: 0x02073F3C,
}


def listing_veneers(root):
    """{veneer address: target} for every `veneer_XXXXXXXX:` label of the
    ARM9 reassembly listing whose pool word is annotated `-> func_`/`sub_`.
    Only the annotation is read (the target address comes from the name),
    as tools/armdata.py reads the listing; the hand-kept VENEERS above stay
    for a listing without annotations."""
    path = os.path.join(root, "asm", "arm9", "main.s")
    out = {}
    if not os.path.exists(path):
        return out
    pending = None
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = re.match(r"^veneer_([0-9A-Fa-f]{8}):", line)
            if m:
                pending = (int(m.group(1), 16), 0)
                continue
            if pending is None:
                continue
            a = re.search(r"->\s+(?:func|sub)_([0-9A-Fa-f]{8})", line)
            if a:
                out[pending[0]] = int(a.group(1), 16)
                pending = None
                continue
            pending = (pending[0], pending[1] + 1)
            if pending[1] > 8:
                pending = None
    return out


class Index:
    def __init__(self):
        for v, t in listing_veneers(ROOT).items():
            VENEERS.setdefault(v, t)
        self.entries = []          # one dict per shard entry
        self.by_addr = {}          # ARM9/TWL/ITCM address -> entry
        self.member_addr = {}      # (source, member name) -> address
        self.member_by_addr = {}   # (source, address) -> member name
        self.defines = {}          # function name -> source path
        self.sources = {}          # source path -> entry
        self._scans = {}           # source path -> SourceScan (lazy)
        self._static_sorted = None # [(address, source)] of the static image
        self._addr_miss = set()
        self.sdk_by_addr, self.sdk_by_name = read_symbol_table("layout/symbols_sdk.txt")
        self.rt_by_addr, self.rt_by_name = read_symbol_table("layout/runtime_arm9.txt")
        self._load_shards()

    def _load_game(self):
        """The entries of src/game (tools/gametree.py): one per translation
        unit, every function at the address the manifests give its name."""
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import gametree
        for e in gametree.entries(ROOT):
            entry = {
                "name": e["name"],
                "address": e["address"],
                "source": e["source"],
                "isa": e["isa"],
                "region": e["region"],
                "status": e["status"],
                "shard": "src/game",
                "functions": e["functions"] or [e["name"]],
            }
            self.entries.append(entry)
            self.sources[entry["source"]] = entry
            for name in entry["functions"]:
                self.defines.setdefault(name, entry["source"])
            if entry["region"].startswith("ov"):
                continue
            self.by_addr.setdefault(entry["address"], entry)
            for name, addr in zip(e["functions"], e["addresses"]):
                if addr is None:
                    addr = self._name_addr(name)
                if addr is not None:
                    self.member_addr[(entry["source"], name)] = addr
                    self.member_by_addr.setdefault((entry["source"], addr), name)
                    self.by_addr.setdefault(addr, entry)

    def _load_shards(self):
        if SOURCE_TREE == "game":
            self._load_game()
            return
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
                self.sources[entry["source"]] = entry
                for name in entry["functions"]:
                    self.defines.setdefault(name, entry["source"])
                # `unit_0204ABCD` is a file name, not a symbol; the real lead
                # function keeps its own sub_/func_ spelling in `functions`.
                self.defines.setdefault(entry["name"], entry["source"])
                # Overlays share address ranges with each other; only the
                # static image (ARM9, its TWL extension, ITCM) is resolved by
                # address.
                if entry["region"].startswith("ov"):
                    continue
                self.by_addr.setdefault(entry["address"], entry)
                # A unit's members live at their own addresses: the member
                # name spells it (func_/sub_), or the SDK table names it.
                for name in entry["functions"]:
                    addr = self._name_addr(name)
                    if addr is not None:
                        self.member_addr[(entry["source"], name)] = addr
                        self.member_by_addr.setdefault((entry["source"], addr), name)
                        self.by_addr.setdefault(addr, entry)

    def entry_at(self, addr):
        """The static-image entry whose source defines a function at `addr`.

        A unit's manifest entry may list only its lead function; the other
        functions it defines are found by reading the units that start at or
        before `addr` (units are contiguous, so a few are enough).
        """
        entry = self.by_addr.get(addr)
        if entry is not None or addr in self._addr_miss:
            return entry
        if self._static_sorted is None:
            self._static_sorted = sorted(
                (e["address"], e["source"]) for e in self.entries
                if not e["region"].startswith("ov"))
        i = bisect.bisect_right(self._static_sorted, (addr, chr(0x10FFFF))) - 1
        for k in range(i, max(i - 8, -1), -1):
            source = self._static_sorted[k][1]
            sc = self.scan(source)
            for name in sorted(sc.defined - sc.statics):
                if self._name_addr(name) == addr:
                    self.member_addr[(source, name)] = addr
                    self.member_by_addr.setdefault((source, addr), name)
                    entry = self.sources[source]
                    self.by_addr[addr] = entry
                    return entry
        self._addr_miss.add(addr)
        return None

    def _name_addr(self, name):
        m = ADDR_NAME.match(name)
        if m:
            return int("0" + m.group(1), 16)
        if name in self.sdk_by_name:
            return self.sdk_by_name[name]
        return None

    def scan(self, source):
        sc = self._scans.get(source)
        if sc is None:
            full = os.path.join(ROOT, source)
            text = ""
            if os.path.exists(full):
                with open(full, encoding="utf-8", errors="replace") as fh:
                    text = fh.read()
            sc = self._scans[source] = SourceScan(text)
        return sc

    def is_global_name(self, name):
        """A name a call with no declaration in scope may resolve to."""
        return (name in self.defines or name in self.sdk_by_name
                or name in self.rt_by_name or ADDR_NAME.match(name) is not None)

    def owner_name(self, entry, addr):
        """The symbol the owning source defines at `addr`."""
        name = self.member_by_addr.get((entry["source"], addr))
        if name is not None:
            return name
        if entry["address"] == addr:
            return entry["functions"][0]
        return None

    def address_of(self, source, name):
        """(DS address, kind) of `name` as `source` sees it; kind may be None."""
        sym = self.scan(source).symbols.get(name)
        if sym:
            return sym
        addr = self._name_addr(name)
        if addr is None:
            addr = self.rt_by_name.get(name)
        return addr, None

    def classify(self, source, name):
        """Return (klass, detail) for one name referenced by one source.

        klass is one of matched, alias, runtime, sdk, open, data, local.  An
        `alias` is a name the DS build resolves by address to a matched
        source that defines the address under another spelling; the detail
        is "<defined name> in <source>".
        """
        owner = self.defines.get(name)
        if owner and owner != source:
            return "matched", owner
        if owner == source:
            return "local", source

        addr, kind = self.address_of(source, name)
        if is_link_constant(name, addr, kind):
            return "const", ("0x%08X" % addr) if addr is not None else name
        if kind == "data":
            return "data", "0x%08X" % addr
        if addr is not None:
            entry = self.entry_at(VENEERS.get(addr, addr))
            if entry:
                if entry["source"] == source:
                    return "local", source
                defined = self.owner_name(entry, addr)
                if defined and defined != name:
                    return "alias", "%s in %s" % (defined, entry["source"])
                return "matched", entry["source"]
        if name in self.rt_by_name or (addr is not None and addr in self.rt_by_addr):
            return "runtime", name
        if name in self.sdk_by_name or (addr is not None and addr in self.sdk_by_addr):
            return "sdk", self.sdk_by_name.get(name) and name or self.sdk_by_addr.get(addr, name)
        if addr is not None:
            return "open", "0x%08X" % addr
        if name in self.scan(source).prototyped:
            # A function no table knows the address of.
            return "open", name
        return "data", name

    def refs(self, source):
        out = []
        names, _implicit = self.scan(source).references(self.is_global_name)
        for name in names:
            klass, detail = self.classify(source, name)
            if klass == "local":
                continue
            out.append((name, klass, detail))
        return out

    def implicit_calls(self, source):
        """Callees `source` calls with no declaration in scope."""
        return self.scan(source).references(self.is_global_name)[1]

    def closure(self, seeds, stop=()):
        """Transitively pull in matched sources; report what stays open.

        Sources in `stop` are part of the result but not followed: the port
        replaces them, so what they call is not needed on their account.
        """
        seen = []
        seen_set = set()
        queue = list(seeds)
        queued = set(queue)
        open_refs = {}
        stop = set(stop)
        while queue:
            source = queue.pop(0)
            if source in seen_set:
                continue
            seen.append(source)
            seen_set.add(source)
            if source in stop:
                continue
            for name, klass, detail in self.refs(source):
                if klass in ("matched", "alias"):
                    owner = detail if klass == "matched" else detail.split(" in ", 1)[1]
                    if owner not in seen_set and owner not in queued:
                        queue.append(owner)
                        queued.add(owner)
                    if klass == "matched":
                        continue
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
        if SOURCE_TREE == "game" and seed.endswith(".c"):
            # A src/matched path (the boot's seed lists name those): the
            # src/game unit that defines the functions the manifests list
            # for that file.
            for name in self._matched_functions().get(seed.split("src/matched/")[-1], []):
                owner = self.defines.get(name)
                if owner:
                    return owner
        raise SystemExit("closure.py: no matched source defines %r" % seed)

    def _matched_functions(self):
        """{path under src/matched/: [functions]} from the manifests."""
        if getattr(self, "_mf", None) is None:
            self._mf = {}
            pattern = os.path.join(ROOT, "layout", "matched", "**", "*.json")
            for path in sorted(glob.glob(pattern, recursive=True)):
                try:
                    with open(path, encoding="utf-8") as fh:
                        shard = json.load(fh)
                except (OSError, ValueError):
                    continue
                for fn in shard.get("functions", []):
                    src = (fn.get("source") or "").replace("\\", "/")
                    src = src.split("src/matched/")[-1]
                    self._mf.setdefault(src, []).extend(fn.get("functions") or [fn["name"]])
        return self._mf


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
            bad = [r for r in refs if r[1] in ("open", "data", "alias", "const")]
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
    for klass in ("sdk", "runtime", "alias", "const", "open", "data"):
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
