#!/usr/bin/env python3
"""
ovlink.py -- compile the game's ARM9 overlays into one host executable.

On the DS an overlay is a file copied to a fixed RAM window when the game
asks for it; several overlays share one window and the linker gave each its
own symbols, so `func_02155244` is one function in ov009 and another in
ov010 (aletheia's metadata repeats about a thousand names this way).  A host
executable has one global namespace and no windows.  This tool works out,
from aletheia's metadata and the build's own sources, what it takes to put
a set of overlays into one link anyway (docs/overlays.md is the design):

 1. A namespace per overlay.  Every external name an overlay's sources
    define, and every name they use whose address lies inside the overlay's
    own window, is renamed to `ovNNN_<name>` by a forced-include header
    (`#define func_02155244 ov010_func_02155244`).  The matched files are
    not edited (portability rule 1).  A spelling that differs from the
    defining file's (an SDK name for a func_ address) is renamed to the
    definition by address, inside the overlay as across to ARM9.

 2. Calls into a window from outside it.  A name whose address lies in a
    window the referring code does not own (ARM9 code calling 0x021DFC20,
    one overlay calling another) keeps its plain spelling, and the plain
    name is a generated trampoline: it asks the overlay registry which
    overlay is resident over the address and calls that overlay's function,
    or panics naming what is resident.  Code pointers stored in data take
    the same route.

 3. The overlay's data.  The image is copied to its window in the port's
    main RAM at load (sdk/overlay_image.c), as the ARM9 image is at reset
    (docs/armdata.md): every data name an overlay's sources use becomes a
    pointer variable private to that overlay (`#define d_021BD8E8
    (*bw_dsref_ov088_d_021BD8E8)`), bound when the overlay is loaded, and
    every pointer word the metadata lists in the image is relocated then.

 4. What the build lacks.  Every function known in a compiled overlay that
    no compiled source defines (unmatched, asm-bodied, excluded) is a stub
    that panics naming the overlay and the address, so a partly matched
    overlay loads and runs until it reaches one.  Optionally (--arm9-stubs)
    ARM9 names the overlays call and the link does not define get the same
    treatment, for a link without the boot.

 5. Implicit declarations and mwcc builtins.  A call with no declaration in
    scope gets C89's `int name();` (64-bit returns excepted), as
    tools/bootlink.py does for the boot; `__builtin__clear` and `__memcpy`
    map to port functions.

 6. Code addresses written as literals.  `(void *)0x021B6279` in an overlay
    source is a Thumb function's address passed as a callback; the dsaddr
    rewrite turns it into bw_ds_ptr, which would hand out the copied code
    bytes.  A source that casts a known function address gets a translator
    in place of bw_ds_ptr that answers the host function for those
    addresses and bw_ds_ptr for everything else.

Run once at configure time (cmake/overlays.cmake):

    python tools/ovlink.py --aletheia-root R --overlays "9;10;88" \\
        --link-sources lib_sources.txt --out-dir build/ovl/boot [--arm9-stubs]

Writes, under --out-dir (rewritten only when they change):

    ovl_units.txt        one line per translation unit: id|source|header
    include/ovNNN.h      the overlay's renames and data bindings
    include/tu/*.h       one forced-include header per translation unit
    ovNNN_link.c         the overlay's stubs, data image, code-literal
                         translator and static initialisers
    ovl_dispatch.c       the trampolines and the table of compiled overlays
    ovl_arm9_stubs.c     ARM9 names the link lacks (empty without --arm9-stubs)
    ovl_arm9_needs.txt   every ARM9 name the overlays use, and whether the
                         link defines it
    ovl_report.txt       counts and every decision worth a second look

Reads only metadata and C sources, never game bytes: layout/layout.json,
layout/matched/*/*.json, layout/symbols_sdk.txt, layout/data_objects.json,
asm/overlays/ovNNN.s (only the `-> symbol` annotations and the `.global`
labels), aletheia's src/matched/ and the port's own sources.

Standard library only.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import armdata          # noqa: E402  (name_address, SourceFile, exclusions)
import overlay_table    # noqa: E402  (KNOWN_SINIT)
import closure          # noqa: E402  (listing_veneers)

EIDOS_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_EXCLUSIONS = os.path.join(EIDOS_ROOT, "tools", "armdata_exclusions.txt")
DEFAULT_OV_EXCLUSIONS = os.path.join(EIDOS_ROOT, "tools", "ovlink_exclusions.txt")

# A data name below main RAM is bound to its address as a value: the SDK's
# linker constants (armdata.py's CONST_LIMIT), and the ITCM, whose code the
# overlays name only for its address (`&OS_IrqHandler` as a sentinel).  The
# same value tools/bootlink.py gives such a name in the boot.
CONST_LIMIT = 0x02000000
ADDR_FUNC = re.compile(r"^(?:[A-Za-z0-9]+_)?(?:func|sub|unit|veneer)_([0-9A-Fa-f]{8})$")

# The title path (docs/title-closure.md): boot, save check and title menu,
# opening cinematic.
TITLE_SET = [9, 10, 11, 13, 17, 228, 88, 179]

# Standard C library functions the overlays call (the ARM9's MSL copies on
# the DS); on the host the C library provides them, so they are never
# stubbed.
HOST_LIBC = {
    "memcpy", "memset", "memmove", "memcmp", "memchr", "strlen", "strcpy",
    "strncpy", "strcmp", "strncmp", "strchr", "strrchr", "strstr", "strcat",
    "strncat", "strtol", "strtoul", "atoi", "abs", "labs", "sprintf",
    "snprintf", "vsprintf", "vsnprintf", "sscanf", "qsort", "bsearch",
    "toupper", "tolower", "isdigit", "isalpha", "isspace", "isxdigit",
    "isupper", "islower", "isalnum", "isprint",
}

# mwcc builtins the matched files call without a declaration, and the port
# function each one becomes (sdk/overlay_image.c).
MWCC_BUILTINS = {
    "__builtin__clear": ("bw_mwcc_clear", "void bw_mwcc_clear(void *dst, unsigned int size);"),
    "__memcpy": ("bw_mwcc_memcpy",
                 "void *bw_mwcc_memcpy(void *dst, const void *src, unsigned int size);"),
}


# ---------------------------------------------------------------------------
# Reading one C source
# ---------------------------------------------------------------------------
#
# The same reading tools/closure.py's SourceScan does on the boot-link
# branch: what a file defines (and with which linkage), declares, uses and
# calls.  A declared name counts only when the file uses it outside its own
# declaration.

IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
CALL = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
MEMBER = re.compile(r"(?:\.|->)\s*[A-Za-z_]\w*")
ASM_BODY = re.compile(r"\b(?:asm|__asm)\b")
ATTRIBUTE = re.compile(r"\b__attribute__\s*\(")
TAG_BODY = re.compile(r"\b(struct|union|enum)\b(\s+[A-Za-z_]\w*)?\s*\{")
TAG_NAME = re.compile(r"\b(struct|union|enum)\s+(?!_tag\b)[A-Za-z_]\w*")
LOCAL_DECL = re.compile(r"\b([A-Za-z_]\w*)\s+\**\s*([A-Za-z_]\w*)\s*(?=[=;,\[)])")
FUNC_PTR_DECL = re.compile(r"\(\s*\*+\s*(?:const\s+|volatile\s+)*([A-Za-z_]\w*)\s*[\)\[]")
RET_PTR_DECL = re.compile(r"\(\s*\*+\s*([A-Za-z_]\w*)\s*\(")
# A declaration inside a function body: `void func_02156730(void);`.
BLOCK_PROTO = re.compile(
    r"(?:(?<=[;{}])|^)\s*(?:extern\s+)?((?:(?:const|volatile|unsigned|signed|struct|union|enum)\s+)*"
    r"[A-Za-z_]\w*)\s*(\**)\s*\(?\s*\**\s*([A-Za-z_]\w*)\s*\(([^;{}()]*(?:\([^;{}()]*\)[^;{}()]*)*)\)"
    r"\s*\)?\s*(?:\[[^\]]*\])?\s*;")
SYMBOLS_HDR = re.compile(r"^\s*\*\s*symbols:\s*$")
SYMBOL_ROW = re.compile(
    r"^\s*\*\s+([A-Za-z_][A-Za-z0-9_]*)\s+(0x[0-9A-Fa-f]{1,8})\s+(thumb|arm|data)\b(.*)$")
LITERAL_CAST = re.compile(
    r"\(\s*(?:const\s+|volatile\s+)*[A-Za-z_][\w\s]*?\*+\s*\)\s*\(?\s*(0x0?2[0-9A-Fa-f]{6,7})[uUlL]*")

NOT_TYPES = {"return", "case", "goto", "else", "do", "sizeof", "typedef"}

C_RESERVED = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while", "asm", "__asm", "__attribute__",
    "__inline", "_Bool", "defined", "nofralloc", "naked",
}


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


class Scan:
    """What one source defines, declares, uses and calls."""

    def __init__(self, path, text):
        self.path = path
        self.symbols = {}          # name -> (addr, kind)
        self.defined = set()       # functions and objects with a body or storage here
        self.statics = set()
        self.declared = set()      # extern or prototype, not static
        self.prototyped = set()    # declared as a function
        self.used = set()
        self.called = set()
        self.macros = set()
        self.typedefs = {}         # name -> the type text
        self.locals = set()
        self.heads = {}            # defined function -> its declarator text
        self.func_defs = set()     # names defined with a body
        self.block_protos = set()  # functions declared inside a body
        self.prototyped_static = set()
        self._parse_symbols(text)
        code = strip_source(text)
        self.has_asm = bool(ASM_BODY.search(code))
        self.literals = set(int(v, 16) for v in LITERAL_CAST.findall(code))
        self._scan(code)
        # stubs/stubs.h's BW_STUB(name, address, why) defines `name`.
        for m in re.finditer(r"\bBW_STUB\s*\(\s*([A-Za-z_]\w*)", code):
            self.defined.add(m.group(1))
            self.func_defs.add(m.group(1))
        # armdata's reading of the same file: which names are used as a
        # member, a tag, a local or in a static initialiser.
        sf = armdata.SourceFile(path, text, True)
        self.conflicts = {n for n in sf.identifiers
                          if n in sf.members or n in sf.init_names or n in sf.shadowed}

    def _parse_symbols(self, text):
        in_block = False
        for line in text.splitlines():
            if SYMBOLS_HDR.match(line):
                in_block = True
                continue
            if in_block:
                m = SYMBOL_ROW.match(line)
                if m:
                    self.symbols[m.group(1)] = (int(m.group(2), 16), m.group(3))
                elif line.strip().startswith("*/") or not line.strip().startswith("*"):
                    in_block = False

    def _use(self, text):
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
            words = IDENT.findall(head.split("(")[0])
            if words:
                name = words[-1]
        if name:
            self.defined.add(name)
            self.func_defs.add(name)
            self.heads[name] = " ".join(head.split())
            if re.search(r"\bstatic\b", head.split("(")[0]):
                self.statics.add(name)
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
        for bm in BLOCK_PROTO.finditer(body):
            typ, stars, bname = bm.group(1), bm.group(2), bm.group(3)
            last = typ.split()[-1]
            if last in NOT_TYPES:
                continue
            if stars or last in C_RESERVED or last in self.typedefs or last in INT_TYPES:
                self.block_protos.add(bname)
        self.locals.update(fp.group(1) for fp in FUNC_PTR_DECL.finditer(body))
        for typ, var in LOCAL_DECL.findall(body):
            if typ in self.typedefs or typ in C_RESERVED:
                self.locals.add(var)
        self._use(body)

    def _declaration(self, decl):
        decl = decl.strip()
        if not decl:
            return
        raw = decl
        pos = 0
        while True:
            m = TAG_BODY.search(decl, pos)
            if not m:
                break
            end = _matching(decl, m.end() - 1, "{", "}")
            repl = " %s _tag " % m.group(1)
            decl = decl[:m.start()] + repl + decl[end:]
            pos = m.start() + len(repl)
        decl = TAG_NAME.sub(lambda t: t.group(1) + " _tag", decl)
        head_words = IDENT.findall(_split_top(decl, "=")[0].split("(")[0])
        is_typedef = "typedef" in head_words
        is_extern = "extern" in head_words
        is_static = "static" in head_words
        for piece in _split_top(decl, ","):
            parts = _split_top(piece, "=")
            declarator = parts[0]
            if len(parts) > 1:
                self._use("=".join(parts[1:]))
            m = FUNC_PTR_DECL.search(declarator)
            if m and m.start() != declarator.find("("):
                m = None
            ret_ptr = RET_PTR_DECL.search(declarator)
            if ret_ptr and ret_ptr.start() != declarator.find("("):
                ret_ptr = None
            if ret_ptr:
                # `T (*name(params))[6]`: a function returning a pointer.
                name, is_func = ret_ptr.group(1), True
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
            for bound in re.findall(r"\[([^\]]*)\]", declarator):
                self._use(bound)
            if is_typedef:
                self.typedefs[name] = raw
            elif is_static:
                self.statics.add(name)
                if is_func:
                    self.prototyped_static.add(name)
                else:
                    self.defined.add(name)
            elif is_extern or is_func:
                self.declared.add(name)
                if is_func:
                    self.prototyped.add(name)
            else:
                self.defined.add(name)

    def exported(self):
        """Names this file gives external linkage."""
        return self.defined - self.statics

    def references(self, universe):
        """(names needed from elsewhere, implicit calls)."""
        local = self.defined | self.statics | self.macros | set(self.typedefs) | C_RESERVED
        refs = (self.declared & self.used) - local
        refs |= {n for n in self.symbols if n in self.used and n not in local}
        block = (self.block_protos & self.used) - local
        implicit = {name for name in self.called - self.declared - self.block_protos
                    - local - self.locals if universe(name)}
        return refs | block | implicit, implicit

    def static_undefined(self):
        """Static functions this file declares and uses but does not define:
        mwcc's link resolved them against another file's definition; a host
        compiler cannot (an internal symbol with no body)."""
        return {n for n in self.prototyped_static
                if n not in self.func_defs and n in self.used}

    def is_code(self, name):
        sym = self.symbols.get(name)
        if sym is not None:
            return sym[1] != "data"
        if name in self.prototyped or name in self.func_defs or name in self.block_protos:
            return True
        if name in self.declared:
            return False            # an object, perhaps a function pointer
        return name in self.called


# Port-side macros the build decides (--define), for the `#if
# EIDOS_HEAP_MATCHED` blocks in sdk/ that decide which names the link has;
# the same reading tools/bootlink.py does.
PORT_DEFINES = {}
PP_IF = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


def _pp_value(kind, expr):
    """True/False for a conditional PORT_DEFINES decides, None otherwise."""
    expr = expr.split("/*")[0].split("//")[0].strip()
    if kind in ("ifdef", "ifndef"):
        if expr not in PORT_DEFINES:
            return None
        return kind == "ifdef"
    neg = expr.startswith("!")
    name = expr[1:].strip() if neg else expr
    if name in PORT_DEFINES:
        value = PORT_DEFINES[name] not in ("0", "")
        return not value if neg else value
    return None


def preprocess(text):
    """Blank the branches of the conditionals PORT_DEFINES decides."""
    out = []
    stack = []                 # [this branch live, a branch taken, decided]
    for line in text.split("\n"):
        m = PP_IF.match(line)
        live = all(frame[0] for frame in stack)
        if m:
            kind, expr = m.group(1), m.group(2)
            if kind in ("if", "ifdef", "ifndef"):
                v = _pp_value(kind, expr)
                stack.append([v is not False, v is True, v is not None])
            elif kind == "elif" and stack:
                frame = stack[-1]
                v = _pp_value("if", expr)
                if frame[2] and v is not None:
                    frame[0] = (not frame[1]) and v
                    frame[1] = frame[1] or v
                else:
                    frame[0], frame[2] = True, False
            elif kind == "else" and stack:
                frame = stack[-1]
                frame[0] = (not frame[1]) if frame[2] else True
            elif kind == "endif" and stack:
                stack.pop()
            out.append("")
            continue
        out.append(line if live else "")
    return "\n".join(out)


def read_text(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def read_list(path):
    out = []
    if not path or not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if line:
                out.append(line.replace("\\", "/"))
    return out


def write_if_changed(path, text):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    if os.path.exists(path):
        with open(path, encoding="utf-8", newline="") as fh:
            if fh.read() == text:
                return
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def hx(v):
    return "0x%08X" % v


# ---------------------------------------------------------------------------
# Signatures for the trampolines
# ---------------------------------------------------------------------------
#
# A trampoline forwards its arguments, so it needs a prototype.  It is taken
# from the definition of the function it forwards to and normalised to
# types every host passes the same way: any pointer (data or function) is
# `void *`, every integer typedef the matched files use is its C type.  A
# signature with a struct passed by value, a float or `...` is not
# normalised; its trampoline panics instead of forwarding.

INT_TYPES = {
    "u8": "unsigned char", "s8": "signed char", "vu8": "unsigned char", "vs8": "signed char",
    "u16": "unsigned short", "s16": "short", "vu16": "unsigned short", "vs16": "short",
    "u32": "unsigned int", "s32": "int", "vu32": "unsigned int", "vs32": "int",
    "u64": "unsigned long long", "s64": "long long", "fx64": "long long",
    "fx32": "int", "fx16": "short", "BOOL": "int", "bool": "int", "_Bool": "int",
    "size_t": "unsigned int", "OSTick": "unsigned long long",
}
BASE_WORDS = {"unsigned", "signed", "char", "short", "int", "long", "void"}


def _norm_type(text, typedefs, depth=0):
    """C type text (no declarator name) -> normalised spelling, or None."""
    t = re.sub(r"\b(?:const|volatile|register|static|extern|inline|__inline)\b", " ", text)
    if "*" in t or "[" in t or "(" in t:
        return "void *"
    words = IDENT.findall(t)
    if not words:
        return None
    if words[0] == "enum":
        return "int"
    if words[0] in ("struct", "union"):
        return None
    if all(w in BASE_WORDS for w in words):
        s = set(words)
        if words == ["void"]:
            return "void"
        n_long = words.count("long")
        uns = "unsigned" in s
        if "char" in s:
            return "unsigned char" if uns else ("signed char" if "signed" in s else "char")
        if "short" in s:
            return "unsigned short" if uns else "short"
        if n_long >= 2:
            return "unsigned long long" if uns else "long long"
        if n_long == 1:
            return "unsigned long" if uns else "long"
        return "unsigned int" if uns else "int"
    if len(words) == 1:
        w = words[0]
        if w in INT_TYPES:
            return INT_TYPES[w]
        td = typedefs.get(w)
        if td and depth < 8:
            m = re.match(r"^\s*typedef\s+(.*?)\b" + re.escape(w) + r"\s*$", " ".join(td.split()))
            if m:
                return _norm_type(m.group(1), typedefs, depth + 1)
            if re.search(r"\(\s*\*\s*" + re.escape(w) + r"\s*\)", td):
                return "void *"
        return None
    return None


def _strip_declarator_name(param):
    """'const u8 *buf' -> 'const u8 *'; 'int' -> 'int'."""
    p = re.sub(r"\[[^\]]*\]", "[]", param.strip())
    if FUNC_PTR_DECL.search(p) or "(" in p:
        return "void *"
    words = IDENT.findall(p)
    if len(words) >= 2 and words[-1] not in BASE_WORDS and words[-1] not in INT_TYPES \
            and words[-1] not in ("const", "volatile"):
        k = p.rfind(words[-1])
        p = p[:k] + p[k + len(words[-1]):]
    return p


def signature(head, name, typedefs):
    """(return type, [param types]) of a definition head, normalised, or None."""
    i = head.find(name + "(")
    if i < 0:
        m = re.search(r"\b" + re.escape(name) + r"\s*\(", head)
        if not m:
            return None
        i = m.start()
    before = head[:i]
    rest = head[i + len(name):].strip()
    if not rest.startswith("("):
        return None
    params = rest[1:rest.rfind(")")]
    if not before.strip():
        ret = "int"                    # C89 implicit int
    else:
        ret = _norm_type(before, typedefs)
    if ret is None:
        return None
    out = []
    ps = [p for p in _split_top(params, ",") if p.strip()]
    if len(ps) == 1 and ps[0].strip() == "void":
        ps = []
    for p in ps:
        if p.strip() == "...":
            return None
        t = _norm_type(_strip_declarator_name(p), typedefs)
        if t is None or t == "void":
            return None
        out.append(t)
    return ret, tuple(out)


def proto(ret, name, params):
    plist = ", ".join("%s a%d" % (t, k) for k, t in enumerate(params)) or "void"
    sep = "" if ret.endswith("*") else " "
    return "%s%s%s(%s)" % (ret, sep, name, plist)


def proto_decl(ret, name, params):
    plist = ", ".join(params) or "void"
    sep = "" if ret.endswith("*") else " "
    return "%s%s%s(%s);" % (ret, sep, name, plist)


def implicit_return(head, name):
    """The return type of a non-prototype declaration (tools/bootlink.py's
    rule): `int`, as mwcc compiled the call, unless the definition returns a
    64-bit value."""
    if not head:
        return "int"
    before = head.split("(")[0]
    before = before[:before.rfind(name)] if name in before else before
    words = [w for w in IDENT.findall(before)
             if w not in ("static", "inline", "extern", "const", "volatile")]
    if "*" not in before and (any(w in ("u64", "s64", "fx64", "OSTick") for w in words)
                              or " ".join(words).count("long long")):
        return "long long"
    return "int"


# ---------------------------------------------------------------------------
# aletheia's metadata
# ---------------------------------------------------------------------------

class Overlay:
    def __init__(self, o):
        self.id = o["id"]
        self.lo = o["ram_start"]
        self.code_end = o["ram_start"] + o["code_size"]
        self.bss_start = o["bss_start"]
        self.hi = o["bss_end"]
        self.code_size = o["code_size"]
        self.bss_size = o["bss_size"]
        self.sinit_count = max(0, o.get("static_init_count", 1) - 1)
        self.tag = "ov%03d" % self.id

    def owns(self, addr):
        return self.lo <= addr < self.hi


class Aletheia:
    def __init__(self, root):
        self.root = root
        with open(os.path.join(root, "layout", "layout.json"), encoding="utf-8") as fh:
            lay = json.load(fh)
        self.overlays = {o["id"]: Overlay(o) for o in lay["overlays"]}
        self.sdk_by_name = {}       # name -> [(addr, region)]
        self.sdk_at = {}            # (region, addr) -> name
        self._read_sdk()
        self.ov_sources = {}        # id -> [source rel path]
        self.ov_names = {}          # id -> {addr: [names]} (every shard entry)
        self.src_names = {}         # source rel -> [(name, addr)]
        self.arm9_at = {}           # ARM9 addr -> [names] (shards)
        self.arm9_src = {}          # ARM9 source rel -> [(name, addr)]
        self._read_shards()

    def _read_sdk(self):
        """The SDK's symbols (symbols_sdk.txt) and the runtime helpers
        (runtime_arm9.txt), by name and by region and address."""
        for fname in ("symbols_sdk.txt", "runtime_arm9.txt"):
            path = os.path.join(self.root, "layout", fname)
            if not os.path.exists(path):
                continue
            with open(path, encoding="utf-8") as fh:
                for line in fh:
                    if line.startswith("#"):
                        continue
                    parts = line.split()
                    if len(parts) < 2 or not re.fullmatch(r"[0-9A-Fa-f]{8}", parts[0]):
                        continue
                    addr = int(parts[0], 16)
                    region = parts[3] if len(parts) >= 4 else "arm9"
                    if not region.startswith("ov"):
                        region = "arm9"
                    self.sdk_by_name.setdefault(parts[1], []).append((addr, region))
                    self.sdk_at.setdefault((region, addr), parts[1])

    def _read_shards(self):
        pattern = os.path.join(self.root, "layout", "matched", "*", "*.json")
        for path in sorted(glob.glob(pattern)):
            with open(path, encoding="utf-8") as fh:
                shard = json.load(fh)
            for e in shard.get("functions", []):
                if e.get("status") not in ("EXACT", "EXACT*"):
                    continue
                region = e.get("region", "arm9")
                src = e["source"].replace("\\", "/")
                names = e.get("functions") or [e["name"]]
                covers = e.get("covers")
                base = int(e["address"], 16)
                placed = []
                for k, n in enumerate(names):
                    m = ADDR_FUNC.match(n)
                    if covers and k < len(covers):
                        a = int(covers[k], 16)
                    elif m:
                        a = int(m.group(1), 16)
                    elif k == 0:
                        a = base
                    else:
                        hits = [ad for ad, rg in self.sdk_by_name.get(n, []) if rg == region]
                        a = hits[0] if hits else None
                    if a is not None:
                        placed.append((n, a))
                m = re.match(r"^ov(\d{3})$", region)
                if m:
                    oid = int(m.group(1))
                    self.ov_sources.setdefault(oid, [])
                    if src not in self.ov_sources[oid]:
                        self.ov_sources[oid].append(src)
                    for n, a in placed:
                        self.ov_names.setdefault(oid, {}).setdefault(a, []).append(n)
                    self.src_names.setdefault(src, []).extend(placed)
                else:
                    for n, a in placed:
                        self.arm9_at.setdefault(a, []).append(n)
                    self.arm9_src.setdefault(src, []).extend(placed)

    def windows(self, addr):
        return [o.id for o in self.overlays.values() if o.owns(addr)]

    def in_any_window(self, addr):
        return any(o.owns(addr) for o in self.overlays.values())

    def listing(self, oid):
        """(annotations {loc: (symbol, target)}, functions {addr: name}) from
        asm/overlays/ovNNN.s."""
        path = os.path.join(self.root, "asm", "overlays", "ov%03d.s" % oid)
        ann, funcs = {}, {}
        if not os.path.exists(path):
            return ann, funcs
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                s = line.strip()
                if s.startswith(".global"):
                    name = s.split()[-1]
                    m = ADDR_FUNC.match(name)
                    if m and not name.startswith("veneer_"):
                        funcs[int(m.group(1), 16)] = name
                    continue
                if "->" not in line or not armdata.WORD_LINE.match(line):
                    continue
                m = armdata.ANNOT.search(line)
                if not m:
                    continue
                sym = m.group(2)
                off = int(m.group(3), 16) if m.group(3) else 0
                base = armdata.name_address(sym)
                if base is None:
                    continue
                tgt = base + off
                if m.group(4):
                    tgt |= 1
                ann[int(m.group(1), 16)] = (sym, tgt)
        return ann, funcs

    def thumb_funcs(self, oid):
        """Addresses of the overlay's Thumb functions: a `.thumb_func`
        after a function's `.global` in asm/overlays/ovNNN.s."""
        path = os.path.join(self.root, "asm", "overlays", "ov%03d.s" % oid)
        out, last = set(), None
        if not os.path.exists(path):
            return out
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                s = line.strip()
                if s.startswith(".global"):
                    m = ADDR_FUNC.match(s.split()[-1])
                    last = int(m.group(1), 16) if m else None
                elif s.startswith(".thumb_func") and last is not None:
                    out.add(last)
        return out

    def data_object_relocs(self, oid):
        rng, objects, _ext = armdata.load_data_objects(self.root, "ov%03d" % oid)
        out = {}
        for obj in objects:
            for loc, tgt in armdata.object_pointer_words(obj):
                out[loc] = tgt
        return out


# ---------------------------------------------------------------------------
# The link
# ---------------------------------------------------------------------------

def flat_name(rel):
    rel = rel[:-2] if rel.endswith(".c") else rel
    return re.sub(r"[^A-Za-z0-9_]", "_", rel)


class Unit:
    def __init__(self, oid, rel, path, scan):
        self.oid = oid
        self.rel = rel
        self.path = path
        self.scan = scan
        self.protos = {}           # name -> return type (implicit calls)
        self.builtins = set()
        self.private = {}          # name -> this file's own copy's name
        self.literal_table = False
        self.header = None


class OvState:
    def __init__(self, ov):
        self.ov = ov
        self.units = []
        self.excluded = []         # (rel, why)
        self.replaced = []         # (rel, port file): asm bodies in C
        self.standins = []         # port stand-ins (stubs/ov/) compiled in
        self.thumb = set()         # Thumb function addresses (the listing)
        self.defs = {}             # exported name -> unit
        self.at = {}               # addr -> defined function name (exported)
        self.known = set()         # function addresses known in the window
        self.renames = {}          # spelling -> ovNNN_ name
        self.stubs = {}            # addr -> canonical name (func_XXXXXXXX)
        self.data = {}             # name -> dict
        self.objects = {}          # name -> dict (host objects, conflicts)
        self.relocs = []           # {where, target, kind, ...}
        self.code_literals = {}    # value -> symbol expression
        self.sinits = []           # symbol names
        self.unresolved = {}


class Link:
    def __init__(self, args):
        self.args = args
        self.root = os.path.abspath(args.aletheia_root)
        self.al = Aletheia(self.root)
        self.notes = []
        self.ids = self._parse_ids(args.overlays)
        self.exclusions = armdata.load_exclusions(args.exclusions)
        self.ov_excl = {}
        for line in read_list(args.ov_exclusions):
            parts = line.split(None, 1)
            self.ov_excl[parts[0]] = parts[1] if len(parts) > 1 else "listed"
        self._read_link()
        self.state = {}
        self.tramps = {}           # plain name -> addr
        self.arm9_needs = {}       # name -> (addr or None, defined by link?)
        self.arm9_stub = {}        # name -> addr or None
        self._read_overlays()
        self._link_refs()
        for oid in self.ids:
            self._resolve(self.state[oid])
        for oid in self.ids:
            self._data(self.state[oid])
        self._finish_stubs()

    # -- inputs ------------------------------------------------------------

    def _parse_ids(self, text):
        text = (text or "").strip()
        if text.upper() == "ALL":
            ids = sorted(self.al.ov_sources)
        elif text.upper() in ("TITLE", ""):
            ids = list(TITLE_SET)
        else:
            ids = []
            for part in re.split(r"[;, ]+", text):
                if not part:
                    continue
                m = re.match(r"^(?:ov)?0*(\d+)$", part, re.I)
                if not m:
                    raise SystemExit("ovlink.py: bad overlay id %r" % part)
                ids.append(int(m.group(1)))
        for oid in ids:
            if oid not in self.al.overlays:
                raise SystemExit("ovlink.py: no overlay %d in layout.json" % oid)
        return sorted(set(ids))

    def rel_of(self, path):
        p = os.path.abspath(path).replace("\\", "/")
        r = self.root.replace("\\", "/")
        if p.lower().startswith(r.lower() + "/"):
            return p[len(r) + 1:]
        return None

    def _read_link(self):
        """The rest of the link: what it defines, and where."""
        self.link_defs = set()
        self.link_at = {}          # addr -> defined name
        self.link_scans = []
        self.link_heads = {}
        self.link_typedefs = {}
        paths = []
        for lst in self.args.link_sources:
            paths += read_list(lst)
        seen = set()
        for p in paths:
            if not p.endswith(".c") or p in seen:
                continue
            seen.add(p)
            if not os.path.exists(p):
                continue                 # generated later in the build
            sc = Scan(p, preprocess(read_text(p)))
            self.link_scans.append(sc)
            for n in sc.exported():
                self.link_defs.add(n)
                if n in sc.heads:
                    self.link_heads[n] = (sc.heads[n], sc.typedefs)
            rel = self.rel_of(p)
            if rel and rel in self.al.arm9_src:
                for n, a in self.al.arm9_src[rel]:
                    if n in sc.exported():
                        self.link_at.setdefault(a, n)
        for n in sorted(self.link_defs):
            a = armdata.name_address(n)
            if a is None:
                hits = [ad for ad, rg in self.al.sdk_by_name.get(n, [])
                        if not rg.startswith("ov")]
                a = hits[0] if hits else None
            if a is not None:
                self.link_at.setdefault(a, n)
        # A linker veneer is another address of its target (the listing's
        # veneer_ labels, closure.listing_veneers): an overlay calling the
        # veneer reaches what the link defines at the target.
        for v, t in closure.listing_veneers(self.root).items():
            if t in self.link_at:
                self.link_at.setdefault(v, self.link_at[t])

    def _read_overlays(self):
        for oid in self.ids:
            ov = self.al.overlays[oid]
            st = OvState(ov)
            self.state[oid] = st
            ann, lfuncs = self.al.listing(oid)
            st.listing = ann
            st.thumb = self.al.thumb_funcs(oid)
            st.known |= set(lfuncs)
            st.known |= set(self.al.ov_names.get(oid, {}))
            for (region, a), n in self.al.sdk_at.items():
                if region == ov.tag:
                    st.known.add(a)
            for rel in sorted(self.al.ov_sources.get(oid, [])):
                path = os.path.join(self.root, rel)
                if rel in self.ov_excl:
                    st.excluded.append((rel, self.ov_excl[rel]))
                    continue
                if not os.path.exists(path):
                    st.excluded.append((rel, "missing in the checkout"))
                    continue
                sc = Scan(path, read_text(path))
                if sc.has_asm:
                    # Portable C for an asm-bodied file, when the port has
                    # written it (sdk/asm/ov/<file>, as sdk/asm/ for ARM9),
                    # compiled under the overlay's names like any other.
                    repl = os.path.join(self.args.asm_dir, os.path.basename(rel)) \
                        if self.args.asm_dir else None
                    if repl and os.path.exists(repl):
                        sc = Scan(repl, read_text(repl))
                        st.units.append(Unit(oid, rel, repl, sc))
                        st.replaced.append((rel, repl))
                        continue
                    st.excluded.append((rel, "mwcc asm body"))
                    continue
                undef = sc.static_undefined()
                if undef:
                    st.excluded.append((rel, "uses static functions another file defines (%s), "
                                        "which only mwcc's link resolves"
                                        % ", ".join(sorted(undef))))
                    continue
                st.units.append(Unit(oid, rel, path, sc))
            self._port_standins(oid, st)
            self._owners(st)
            for k in range(ov.sinit_count):
                addrs = overlay_table.KNOWN_SINIT.get(oid, [])
                if k < len(addrs):
                    st.known.add(addrs[k])

    def _port_standins(self, oid, st):
        """Port C in stubs/ov/ for one overlay, two kinds:

        - a stand-in for a function aletheia has not matched,
          stubs/ov/ovNNN_<name>.c, written from the listing, compiled as one
          more unit of overlay NNN so the function is that code instead of
          a panicking stub.  One whose functions a matched file of the
          overlay now defines is left out and reported: it is obsolete;
        - a reimplementation of a matched file, stubs/ov/reimpl_ovNNN_<name>.c,
          whose header names the file it takes the place of
          (` * replaces: src/matched/ov/...`), as stubs/reimpl_ files do for
          the ARM9: the matched file is left out and this one compiled in
          its place (an argument the matched C forwards in a register)."""
        d = self.args.port_ov_dir
        if not d or not os.path.isdir(d):
            return
        for fname in sorted(os.listdir(d)):
            if not (fname.startswith("reimpl_" + st.ov.tag + "_") and fname.endswith(".c")):
                continue
            path = os.path.join(d, fname)
            text = read_text(path)
            m = re.search(r"^\s*\*\s*replaces:\s*(\S+)", text[:4096], re.M)
            if not m:
                raise SystemExit("ovlink.py: %s names no file it replaces" % path)
            rel = m.group(1)
            keep = [u for u in st.units if u.rel != rel]
            if len(keep) == len(st.units):
                st.excluded.append((path, "replaces %s, which this overlay does not "
                                    "compile (renamed or gone in aletheia?)" % rel))
                self.notes.append("port reimplementation %s replaces nothing" % path)
                continue
            st.units = keep
            st.units.append(Unit(oid, "port/ov/" + fname, path, Scan(path, text)))
            st.replaced.append((rel, path))
        defined = set()
        for u in st.units:
            defined |= set(u.scan.exported())
        for fname in sorted(os.listdir(d)):
            if not (fname.startswith(st.ov.tag + "_") and fname.endswith(".c")):
                continue
            path = os.path.join(d, fname)
            sc = Scan(path, read_text(path))
            clash = sorted(set(sc.exported()) & defined)
            if clash:
                st.excluded.append((path, "port stand-in made obsolete by aletheia's "
                                    "match of %s: delete it" % ", ".join(clash)))
                self.notes.append("obsolete port stand-in %s (%s matched)"
                                  % (path, ", ".join(clash)))
                continue
            rel = "port/ov/" + fname
            st.units.append(Unit(oid, rel, path, sc))
            st.standins.append(path)

    def _owners(self, st):
        """One definition per name.  Some matched files define, besides the
        function their manifest entry claims, neighbours another file claims
        too (a unit kept next to the single-function files split out of it);
        mwcc's link took the claimed one.  The owner of a name is the file
        whose entry claims it, else the first file defining it; every other
        file's copy gets a name private to that file, which its own calls
        keep using."""
        ov = st.ov
        owner = {}
        for u in st.units:
            exp = u.scan.exported()
            for n, _a in self.al.src_names.get(u.rel, []):
                if n in exp:
                    owner.setdefault(n, u)
        for u in st.units:
            for n in sorted(u.scan.exported()):
                owner.setdefault(n, u)
        for u in st.units:
            for n in u.scan.exported():
                if owner[n] is not u:
                    u.private[n] = "%s__%s" % (self.own(st, n), flat_name(os.path.basename(u.rel)))
        st.defs = owner
        for u in st.units:
            sc = u.scan
            for n, a in self.al.src_names.get(u.rel, []):
                if owner.get(n) is u and n in sc.func_defs:
                    st.at.setdefault(a, n)
        for n, u in sorted(owner.items()):
            if n not in u.scan.func_defs:
                continue
            a = armdata.name_address(n)
            if a is not None and ov.owns(a):
                st.at.setdefault(a, n)
            else:
                for ad, rg in self.al.sdk_by_name.get(n, []):
                    if rg == ov.tag:
                        st.at.setdefault(ad, n)

    def universe(self, name):
        if name in MWCC_BUILTINS:
            return True
        if armdata.name_address(name) is not None:
            return True
        if name in self.al.sdk_by_name or name in self.link_defs:
            return True
        return any(name in st.defs for st in self.state.values())

    def address(self, name, sc, st=None):
        """(address, how) of a name one source uses."""
        sym = sc.symbols.get(name)
        if sym is not None:
            return sym[0]
        if st is not None and name in st.defs:
            for a, n in st.at.items():
                if n == name:
                    return a
        a = armdata.name_address(name)
        if a is not None:
            return a
        hits = self.al.sdk_by_name.get(name, [])
        if st is not None:
            own = [ad for ad, rg in hits if rg == st.ov.tag]
            if own:
                return own[0]
        arm = [ad for ad, rg in hits if not rg.startswith("ov")]
        if arm:
            return arm[0]
        return None

    # -- names -------------------------------------------------------------

    def own(self, st, name):
        return "%s_%s" % (st.ov.tag, name)

    def stub_name(self, st, addr):
        st.stubs.setdefault(addr, "func_%08X" % addr)
        return self.own(st, st.stubs[addr])

    def own_code(self, st, addr):
        """The ovNNN_ symbol for code at `addr` (even) in st's window."""
        n = st.at.get(addr)
        if n is not None:
            return self.own(st, n)
        return self.stub_name(st, addr)

    def _resolve(self, st):
        ov = st.ov
        for u in st.units:
            sc = u.scan
            refs, implicit = sc.references(self.universe)
            # Everything this file exports is the overlay's.
            for n in sc.exported():
                st.renames[n] = self.own(st, n)
            for n in sorted(refs):
                if n in MWCC_BUILTINS:
                    if n in implicit:
                        u.builtins.add(n)
                    continue
                if n in st.defs:
                    st.renames[n] = self.own(st, n)
                    if n in implicit:
                        d = st.defs[n].scan
                        u.protos[n] = implicit_return(d.heads.get(n), n)
                    continue
                addr = self.address(n, sc, st)
                code = sc.is_code(n) or n in implicit
                if addr is not None and ov.owns(addr):
                    if code:
                        tgt = self.own_code(st, addr & ~1)
                        st.renames[n] = tgt
                        if n in implicit:
                            dn = st.at.get(addr & ~1)
                            head = st.defs[dn].scan.heads.get(dn) if dn else None
                            u.protos[n] = implicit_return(head, dn or n)
                    else:
                        self._data_name(st, n, addr, sc)
                    continue
                if n not in self.link_defs and addr is not None and addr >= CONST_LIMIT \
                        and self.al.in_any_window(addr):
                    if code:
                        self.tramps.setdefault(n, addr & ~1)
                        if n in implicit:
                            u.protos[n] = "int"
                    else:
                        self._data_name(st, n, addr, sc)
                    continue
                # ARM9, the runtime, a port object, or a linker constant.
                if n in self.link_defs:
                    self.arm9_needs.setdefault(n, (addr, True))
                    if n in implicit:
                        h = self.link_heads.get(n)
                        u.protos[n] = implicit_return(h[0] if h else None, n)
                    continue
                if code and n in HOST_LIBC:
                    # The host's C library defines it, with the standard
                    # meaning the matched file declares it for.
                    self.arm9_needs.setdefault(n, (addr, True))
                    if n in implicit:
                        u.protos[n] = "int"
                    continue
                if code:
                    tgt = self.link_at.get(addr & ~1) if addr is not None else None
                    if tgt is not None and tgt != n:
                        st.renames[n] = tgt
                        self.arm9_needs.setdefault(n, (addr, True))
                    else:
                        # Nothing defines it.  The overlays name it by the
                        # spelling the ARM9's own files use (the one a boot
                        # link stubs it under), and it is stubbed here only
                        # when no other source of the link reaches it.
                        canon = self.arm9_canonical(addr) if addr is not None else None
                        canon = canon or n
                        if canon != n:
                            st.renames[n] = canon
                        self.arm9_needs.setdefault(n, (addr, False))
                        if addr is None or (addr & ~1) not in self.link_open_at:
                            self.arm9_stub.setdefault(canon, addr)
                        tgt = canon
                    if n in implicit:
                        h = self.link_heads.get(tgt or n)
                        u.protos[n] = implicit_return(h[0] if h else None, n)
                    continue
                if addr is None:
                    st.unresolved[n] = os.path.basename(u.rel)
                    continue
                self._data_name(st, n, addr, sc)
            # Code addresses written as literals.
            for v in sorted(sc.literals):
                sym = self.code_symbol(st, v)
                if sym is not None:
                    st.code_literals[v] = sym
                    u.literal_table = True

    def arm9_canonical(self, addr):
        """The name the ARM9's own matched files define at `addr` (an
        asm-bodied file's, say), else the SDK's or the runtime's, else
        None."""
        names = self.al.arm9_at.get(addr & ~1)
        if names:
            return names[0]
        return self.al.sdk_at.get(("arm9", addr & ~1))

    def code_symbol(self, st, value):
        """The host symbol for a literal code address, or None if the value is
        not known to be code."""
        e = value & ~1
        thumb = value & 1
        if st.ov.owns(e):
            if e in st.at or (e in st.known) or (thumb and e < st.ov.code_end):
                return self.own_code(st, e)
            return None
        if self.al.in_any_window(e):
            if thumb or any(e in self.al.ov_names.get(w, {}) for w in self.al.windows(e)):
                name = "func_%08X" % e
                self.tramps.setdefault(name, e)
                return name
            return None
        n = self.link_at.get(e)
        if n is not None and (thumb or e in self.al.arm9_at):
            return n
        return None

    def _data_name(self, st, n, addr, sc):
        e = st.data.get(n)
        if e is None:
            e = {"name": n, "addr": addr, "conflict": False}
            st.data[n] = e

    def _link_refs(self):
        """Names the rest of the link uses inside an overlay window; and the
        names its matched sources use that it does not define, which the
        boot link stubs itself (tools/bootlink.py), so ovlink must not."""
        self.link_open = set()
        self.link_open_at = set()
        for sc in self.link_scans:
            refs, _imp = sc.references(self.universe)
            for n in refs:
                if n in self.link_defs:
                    continue
                addr = self.address(n, sc)
                if self.rel_of(sc.path):
                    self.link_open.add(n)
                    if addr is not None:
                        self.link_open_at.add(addr & ~1)
                if addr is None or addr < CONST_LIMIT or not self.al.in_any_window(addr):
                    continue
                if sc.is_code(n):
                    self.tramps.setdefault(n, addr & ~1)

    # -- data and relocations ------------------------------------------------

    def port_object_at(self, addr):
        best = None
        for n, (a, size, _o) in sorted(self.exclusions.items()):
            if a is None or n not in self.link_defs:
                continue
            if a == addr or (size and a <= addr < a + size):
                key = (a, 1 if size else 0)
                if best is None or key > (best[1], 1 if best[2] else 0):
                    best = (n, a, size)
        return best

    def _data(self, st):
        ov = st.ov
        conflicts = set()
        for u in st.units:
            conflicts |= u.scan.conflicts
        bounds = sorted({e["addr"] for e in st.data.values()} | {ov.lo, ov.bss_start, ov.hi})
        for n, e in sorted(st.data.items()):
            a = e["addr"]
            e["section"] = ("data" if ov.lo <= a < ov.bss_start else
                            "bss" if ov.bss_start <= a < ov.hi else
                            "const" if a < CONST_LIMIT else
                            "overlay" if self.al.in_any_window(a) else "other")
            port = self.port_object_at(a) if a >= CONST_LIMIT else None
            if n in conflicts and a >= CONST_LIMIT and port is None:
                nxt = [b for b in bounds if b > a]
                size = min((nxt[0] - a) if nxt else 0x100, 0x1000)
                e["bind"] = "OBJECT"
                e["size"] = size
                st.renames[n] = self.own(st, n)
                st.objects[n] = e
                continue
            if a < CONST_LIMIT:
                e["bind"] = "CONST"
            elif port is not None:
                e["bind"] = "HOST"
                e["host"] = port
            else:
                e["bind"] = "DS"
            e["size"] = 0
        # Pointer words: the listing's annotations and data_objects.json.
        words = {loc: tgt for loc, (_s, tgt) in st.listing.items()}
        syms = {loc: s for loc, (s, _t) in st.listing.items()}
        for loc, tgt in self.al.data_object_relocs(ov.id).items():
            words.setdefault(loc, tgt)
        for loc in sorted(words):
            if not (ov.lo <= loc < ov.code_end):
                continue
            tgt = words[loc]
            sym = syms.get(loc, "")
            e = tgt & ~1
            is_code = bool(tgt & 1) or ADDR_FUNC.match(sym) is not None \
                or e in st.known or e in self.al.arm9_at
            r = {"where": loc, "target": tgt}
            if is_code:
                r["kind"] = "CODE"
                if ov.owns(e):
                    r["fn"] = self.own_code(st, e)
                elif self.al.in_any_window(e):
                    name = "func_%08X" % e
                    self.tramps.setdefault(name, e)
                    r["fn"] = name
                else:
                    r["fn"] = self.link_at.get(e)
            else:
                port = self.port_object_at(tgt)
                if port is not None:
                    r["kind"] = "HOST"
                    r["anchor"] = port
                else:
                    r["kind"] = "DATA"
            st.relocs.append(r)
        # Static initialisers.
        for a in overlay_table.KNOWN_SINIT.get(ov.id, [])[:ov.sinit_count]:
            st.sinits.append(self.own_code(st, a & ~1))

    def _finish_stubs(self):
        """Every known function of a compiled overlay that nothing defines is a
        stub; and every trampoline's target in a compiled overlay exists."""
        for name, addr in self.tramps.items():
            for w in self.al.windows(addr):
                if w in self.state:
                    self.own_code(self.state[w], addr)
        for st in self.state.values():
            for a in sorted(st.known):
                if st.ov.owns(a) and a not in st.at:
                    self.stub_name(st, a & ~1)

    # -- trampolines ---------------------------------------------------------

    def target_signature(self, st, addr):
        n = st.at.get(addr)
        if n is None:
            return None, None
        sc = st.defs[n].scan
        head = sc.heads.get(n)
        if head is None:
            return n, None
        return n, signature(head, n, sc.typedefs)


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

GEN = "Generated by tools/ovlink.py; do not edit."


def write_overlay_header(link, st, path):
    ov = st.ov
    lines = [
        "/*",
        " * %s" % GEN,
        " *",
        " * Forced include for every matched source of overlay %d (docs/overlays.md):" % ov.id,
        " * its own names become %s_<name>, and every data name it uses is a" % ov.tag,
        " * pointer variable of this overlay that sdk/overlay_image.c binds when the",
        " * overlay is loaded.",
        " */",
        "#ifndef BW_OVL_%s_H" % ov.tag.upper(),
        "#define BW_OVL_%s_H" % ov.tag.upper(),
        "",
        "/* A few SDK files mark a prototype __attribute__((pure)); cl.exe has",
        " * no __attribute__, and the mark changes nothing the host relies on. */",
        "#if defined(_MSC_VER) && !defined(__clang__) && !defined(__attribute__)",
        "#define __attribute__(x)",
        "#endif",
        "",
        "/* Names inside the overlay's window 0x%08X..0x%08X. */" % (ov.lo, ov.hi),
    ]
    for n in sorted(st.renames):
        if st.renames[n] != n:
            lines.append("#define %s %s" % (n, st.renames[n]))
    binds = [n for n in sorted(st.data) if st.data[n]["bind"] != "OBJECT"
             and n not in st.renames]
    if binds:
        lines += ["", "/* Data names: pointer variables bound at load. */"]
        for n in binds:
            e = st.data[n]
            lines.append("#define %s (*bw_dsref_%s_%s)   /* %s %s */"
                         % (n, ov.tag, n, hx(e["addr"]), e["section"]))
    lines += ["", "#endif", ""]
    write_if_changed(path, "\n".join(lines))


def write_unit_header(link, u, ov_header, path):
    st = link.state[u.oid]
    lines = ["/* %s  Overlay %d, %s. */" % (GEN, u.oid, u.rel),
             '#include "%s"' % ov_header.replace("\\", "/")]
    for n in sorted(u.private):
        lines.append("#undef %s" % n)
        lines.append("#define %s %s   /* another file of the overlay owns %s */"
                     % (n, u.private[n], n))
    for n in sorted(u.builtins):
        port, decl = MWCC_BUILTINS[n]
        lines.append(decl)
        lines.append("#define %s %s" % (n, port))
    if u.literal_table:
        fn = "bw_ovl_ds_ptr_%s" % st.ov.tag
        lines.append("/* Code addresses this file writes as literals resolve to host")
        lines.append(" * functions (tools/ovlink.py, step 6). */")
        lines.append("void *%s(unsigned int addr);" % fn)
        lines.append("#define bw_ds_ptr %s" % fn)
    for n in sorted(u.protos):
        rt = u.protos[n]
        lines.append("%s %s();" % (rt, n))
    write_if_changed(path, "\n".join(lines) + "\n")


def c_str(s):
    return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"')


def write_overlay_c(link, st, path):
    ov = st.ov
    out = []
    w = out.append
    w("/*")
    w(" * %s" % GEN)
    w(" *")
    w(" * Overlay %d (0x%08X..0x%08X): the stubs for the functions this build" % (ov.id, ov.lo, ov.hi))
    w(" * lacks, the data image sdk/overlay_image.c copies, binds and relocates at")
    w(" * load, and the static initialisers.  Addresses, sizes and names only.")
    w(" */")
    w("")
    w("#include <stdint.h>")
    w("")
    w('#include "armdata.h"')
    w('#include "dsmem.h"')
    w('#include "overlay_image.h"')
    w("")
    # Stubs.
    if st.stubs:
        w("/* Functions of the overlay no compiled source defines: reaching one")
        w(" * panics with the overlay and the address. */")
        for a in sorted(st.stubs):
            n = link.own(st, st.stubs[a])
            w("void %s(void);" % n)
            w("void %s(void) { bw_ovl_stub(%du, 0x%08Xu); }" % (n, ov.id, a))
        w("")
    # Functions the relocations, the literals and the initialisers name.
    funcs = {}
    for r in st.relocs:
        if r["kind"] == "CODE":
            funcs[r["target"]] = r.get("fn")
    stub_names = {link.own(st, n) for n in st.stubs.values()}
    externs = sorted({n for n in list(funcs.values()) + list(st.code_literals.values()) + st.sinits
                      if n and n not in stub_names})
    if externs:
        w("/* Functions defined elsewhere in the build that the data, the literals")
        w(" * or the initialisers point at.  Only the address is taken. */")
        for n in externs:
            w("extern void %s(void);" % n)
        w("")
    traps = sorted({t & ~1 for t, n in funcs.items() if not n})
    for t in traps:
        w("static void bw_trap_%08X(void) { bw_data_trap(0x%08Xu); }" % (t, t))
    if traps:
        w("")
    # Anchors.
    anchors = {}
    for e in st.data.values():
        if e["bind"] == "HOST":
            anchors[e["host"][0]] = (e["host"][1], e["host"][2])
    for r in st.relocs:
        if r["kind"] == "HOST":
            anchors[r["anchor"][0]] = (r["anchor"][1], r["anchor"][2])
    for n in sorted(anchors):
        w("extern char %s[];" % n)
    if anchors:
        w("")
    refs = [n for n in sorted(st.data) if st.data[n]["bind"] != "OBJECT"]
    for n in refs:
        w("void *bw_dsref_%s_%s;" % (ov.tag, n))
    if refs:
        w("")
    objs = sorted(st.objects)
    for n in objs:
        e = st.objects[n]
        w("BwDataCell %s[BW_DATA_CELLS(0x%Xu)];   /* %s: used as a member, tag, local "
          "or in an initialiser */" % (link.own(st, n), e["size"], hx(e["addr"])))
    if objs:
        w("")
    w("static const BwDataName k_names[] = {")
    for n in sorted(st.data, key=lambda k: (st.data[k]["addr"], k)):
        e = st.data[n]
        ref, anchor, aaddr = "&bw_dsref_%s_%s" % (ov.tag, n), "NULL", 0
        if e["bind"] == "HOST":
            anchor, aaddr = e["host"][0], e["host"][1]
        elif e["bind"] == "OBJECT":
            ref, anchor, aaddr = "NULL", link.own(st, n), e["addr"]
        w("    { %s, 0x%08Xu, %s, BW_DATA_BIND_%s, %s, 0, 0x%Xu, %s, 0x%08Xu }," % (
            c_str(n), e["addr"], ref, e["bind"], armdata.SECTION_ENUM[e["section"]],
            e["size"], anchor, aaddr))
    if not st.data:
        w("    { NULL, 0, NULL, 0, 0, 0, 0, NULL, 0 }")
    w("};")
    w("")
    w("static const BwDataRange k_ranges[] = {")
    w("    { 0x%08Xu, 0x%Xu, 0u, 1 },   /* code and data, from the overlay's file */"
      % (ov.lo, ov.code_size))
    nranges = 1
    if ov.bss_size:
        w("    { 0x%08Xu, 0x%Xu, 0u, 0 },   /* .bss */" % (ov.bss_start, ov.bss_size))
        nranges += 1
    w("};")
    w("")
    fn_index = {}
    w("static const BwDataFunc k_funcs[] = {")
    for i, t in enumerate(sorted(funcs)):
        fn_index[t] = i
        n = funcs[t]
        if n:
            w("    { 0x%08Xu, %s, %s }," % (t, n, c_str(n)))
        else:
            w("    { 0x%08Xu, bw_trap_%08X, NULL }," % (t, t & ~1))
    if not funcs:
        w("    { 0, NULL, NULL }")
    w("};")
    w("")
    anchor_index = {}
    w("static const BwDataAnchor k_anchors[] = {")
    for i, n in enumerate(sorted(anchors)):
        anchor_index[n] = i
        a, size = anchors[n]
        w("    { 0x%08Xu, 0x%Xu, %s, %s }," % (a, size or 0, n, c_str(n)))
    if not anchors:
        w("    { 0, 0, NULL, NULL }")
    w("};")
    w("")
    w("static const BwDataReloc k_relocs[] = {")
    for r in st.relocs:
        idx = 0
        if r["kind"] == "CODE":
            idx = fn_index[r["target"]]
        elif r["kind"] == "HOST":
            idx = anchor_index[r["anchor"][0]]
        w("    { 0x%08Xu, 0x%08Xu, BW_DATA_RELOC_%s, %u }," % (r["where"], r["target"], r["kind"], idx))
    if not st.relocs:
        w("    { 0, 0, 0, 0 }")
    w("};")
    w("")
    w("/* Host objects for names that cannot be macros: filled at load and")
    w(" * registered at their address, withdrawn when another overlay takes the")
    w(" * window (sdk/overlay_image.c); docs/overlays.md, \"Data\". */")
    w("static const BwDataObject k_objects[] = {")
    for n in objs:
        e = st.objects[n]
        w("    { 0x%08Xu, 0x%Xu, %s, %s }," % (e["addr"], e["size"], link.own(st, n), c_str(n)))
    if not objs:
        w("    { 0, 0, NULL, NULL }")
    w("};")
    w("")
    w("static const BwDataImage k_image = {")
    w("    %s, %d," % (c_str(ov.tag), ov.id))
    w("    k_ranges, %du," % nranges)
    w("    k_names, %du," % len(st.data))
    w("    k_relocs, %du," % len(st.relocs))
    w("    k_funcs, %du," % len(funcs))
    w("    k_anchors, %du," % len(anchors))
    w("    k_objects, %du," % len(objs))
    w("    NULL, 0u")
    w("};")
    w("")
    # Static initialisers.
    w("static const BwDataFn k_sinits[] = {")
    for n in st.sinits:
        w("    %s," % n)
    if not st.sinits:
        w("    NULL")
    w("};")
    w("")
    # The code-literal translator.
    if st.code_literals:
        fn = "bw_ovl_ds_ptr_%s" % ov.tag
        w("/* bw_ds_ptr for the sources of this overlay that write code addresses")
        w(" * as literals (tools/ovlink.py, step 6). */")
        w("void *%s(unsigned int addr);" % fn)
        w("void *%s(unsigned int addr)" % fn)
        w("{")
        w("    switch (addr) {")
        for v in sorted(st.code_literals):
            w("    case 0x%08Xu: return (void *)(uintptr_t)%s;" % (v, st.code_literals[v]))
        w("    default: return bw_ds_ptr(addr);")
        w("    }")
        w("}")
        w("")
    # Every function of the overlay by the value a code pointer to it holds,
    # for the ARM9 data's pointers into the window (sdk/overlay_image.c,
    # bw_ovl_code_entry).
    entries = {}
    for a, n in st.at.items():
        entries[a | (1 if a in st.thumb else 0)] = link.own(st, n)
    for a, n in st.stubs.items():
        entries.setdefault(a | (1 if a in st.thumb else 0), link.own(st, n))
    declared = set(stub_names) | set(externs)
    fresh = sorted(set(entries.values()) - declared)
    if fresh:
        w("/* The overlay's functions, for bw_ovl_code_entry; only the address is")
        w(" * taken here. */")
        for n in fresh:
            w("extern void %s(void);" % n)
        w("")
    w("static const BwDataFunc k_entries[] = {")
    for v in sorted(entries):
        w("    { 0x%08Xu, %s, %s }," % (v, entries[v], c_str(entries[v])))
    if not entries:
        w("    { 0, NULL, NULL }")
    w("};")
    w("")
    w("const BwOvlImage bw_ovl_%s = {" % ov.tag)
    w("    %du, &k_image, k_sinits, %du, %du, %du," % (
        ov.id, len(st.sinits), len(st.at), len(st.stubs)))
    w("    k_entries, %du" % len(entries))
    w("};")
    w("")
    write_if_changed(path, "\n".join(out))


def write_dispatch(link, path, set_name):
    out = []
    w = out.append
    w("/*")
    w(" * %s" % GEN)
    w(" *")
    w(" * The overlays compiled into this executable, and the trampolines: every")
    w(" * plain name a source outside an overlay's window uses for an address")
    w(" * inside it.  A trampoline calls the function of the overlay resident")
    w(" * over its address, or panics naming what is resident")
    w(" * (docs/overlays.md, \"Calls into a window\").  Pointer arguments travel as")
    w(" * void *, which every target eidos builds for passes like any other")
    w(" * pointer.")
    w(" */")
    w("")
    w('#include "overlay_image.h"')
    w("")
    declared = {}
    body = []
    for name in sorted(link.tramps):
        addr = link.tramps[name]
        cands = link.al.windows(addr)
        sig = None
        targets = []
        for c in cands:
            st = link.state.get(c)
            if st is None:
                targets.append((c, None, None))
                continue
            dn, s = link.target_signature(st, addr)
            targets.append((c, link.own_code(st, addr) if dn else None, s))
            if s is not None and sig is None:
                sig = s
        if sig is None:
            sig = ("void", ())
            forward = False
        else:
            forward = True
        ret, params = sig
        body.append("/* %s at 0x%08X: %s */" % (
            name, addr, ", ".join("ov%03d" % c for c in cands) or "no overlay"))
        body.append("%s;" % proto(ret, name, params))
        body.append(proto(ret, name, params))
        body.append("{")
        body.append("    int ov = bw_ovl_resident_at(0x%08Xu);" % addr)
        args = ", ".join("a%d" % k for k in range(len(params)))
        for c, sym, s in targets:
            if c not in link.state:
                continue
            st = link.state[c]
            if sym is None:
                body.append("    if (ov == %d) {" % c)
                body.append("        bw_ovl_stub(%du, 0x%08Xu);" % (c, addr))
                body.append("    }")
                continue
            if not forward or s != sig:
                body.append("    if (ov == %d) {" % c)
                body.append("        bw_ovl_dispatch_fail(0x%08Xu, %s, ov, BW_OVL_WHY_SIGNATURE);"
                            % (addr, c_str(name)))
                body.append("    }")
                continue
            if sym not in declared:
                declared[sym] = proto_decl(s[0], sym, s[1])
            body.append("    if (ov == %d) {" % c)
            if ret == "void":
                body.append("        %s(%s);" % (sym, args))
                body.append("        return;")
            else:
                body.append("        return %s(%s);" % (sym, args))
            body.append("    }")
        body.append("    bw_ovl_dispatch_fail(0x%08Xu, %s, ov, ov < 0 ? BW_OVL_WHY_NONE "
                    ": BW_OVL_WHY_NOT_COMPILED);" % (addr, c_str(name)))
        for k in range(len(params)):
            body.append("    (void)a%d;" % k)
        if ret != "void":
            body.append("    return 0;")
        body.append("}")
        body.append("")
    if declared:
        w("/* The overlay functions the trampolines forward to, as normalised")
        w(" * prototypes of their definitions. */")
        for sym in sorted(declared):
            w(declared[sym])
        w("")
    out += body
    for oid in link.ids:
        w("extern const BwOvlImage bw_ovl_ov%03d;" % oid)
    w("")
    w("static const BwOvlImage *const k_images[] = {")
    for oid in link.ids:
        w("    &bw_ovl_ov%03d," % oid)
    if not link.ids:
        w("    NULL")
    w("};")
    w("")
    w("/* The overlays of set %s: install with bw_ovl_install. */" % set_name)
    w("const BwOvlImageSet bw_ovl_set_%s = { k_images, %du };" % (set_name, len(link.ids)))
    w("")
    write_if_changed(path, "\n".join(out))


def write_arm9_stubs(link, path, enabled):
    out = ["/*", " * %s" % GEN, " *",
           " * ARM9 functions the overlays call that nothing else in this link",
           " * defines: each panics with its address and name.  Only written for a",
           " * link without the boot (ovlink.py --arm9-stubs).", " */", "",
           '#include "overlay_image.h"', ""]
    if enabled:
        for n in sorted(link.arm9_stub):
            if n in link.link_open or n in link.tramps:
                continue          # the boot link's own stub, or a trampoline
            a = link.arm9_stub[n]
            out.append("void %s(void);" % n)
            out.append("void %s(void) { bw_ovl_arm9_stub(0x%08Xu, %s); }"
                       % (n, a or 0, c_str(n)))
    else:
        out.append("typedef int bw_ovl_no_arm9_stubs;")
    out.append("")
    write_if_changed(path, "\n".join(out))


def write_report(link, path):
    r = []
    p = r.append
    tot_units = sum(len(st.units) for st in link.state.values())
    p("ovlink: %d overlays, %d translation units, %d trampolines, %d ARM9 names "
      "(%d not in the link)" % (
          len(link.ids), tot_units, len(link.tramps), len(link.arm9_needs),
          sum(1 for v in link.arm9_needs.values() if not v[1])))
    for oid in link.ids:
        st = link.state[oid]
        kinds = {}
        for e in st.data.values():
            kinds[e["bind"]] = kinds.get(e["bind"], 0) + 1
        rk = {}
        for x in st.relocs:
            rk[x["kind"]] = rk.get(x["kind"], 0) + 1
        p("%s  0x%08X..0x%08X  units %d  excluded %d  defined %d  stubs %d  data %s  "
          "relocs %s  code literals %d  implicit decls %d  private copies %d" % (
              st.ov.tag, st.ov.lo, st.ov.hi, len(st.units), len(st.excluded), len(st.at),
              len(st.stubs), " ".join("%s=%d" % kv for kv in sorted(kinds.items())) or "0",
              " ".join("%s=%d" % kv for kv in sorted(rk.items())) or "0",
              len(st.code_literals), sum(len(u.protos) for u in st.units),
              sum(len(u.private) for u in st.units)))
    for oid in link.ids:
        st = link.state[oid]
        for rel, why in st.excluded:
            p("  %s excluded %s: %s" % (st.ov.tag, rel, why))
        for rel, repl in st.replaced:
            p("  %s asm body %s: compiled from %s" % (st.ov.tag, rel, repl))
        for standin in st.standins:
            p("  %s port stand-in %s: an unmatched function, written from the listing"
              % (st.ov.tag, standin))
        for n, src in sorted(st.unresolved.items()):
            p("  %s unresolved %s (%s): no address in any symbols block, name or SDK table"
              % (st.ov.tag, n, src))
        for n in sorted(st.objects):
            p("  %s host object %s at %s: a matched file uses the name as a member, tag, "
              "local or in an initialiser" % (st.ov.tag, n, hx(st.objects[n]["addr"])))
        for u in st.units:
            for n in sorted(u.private):
                p("  %s private copy %s in %s (%s owns it)" % (
                    st.ov.tag, n, u.rel, st.defs[n].rel))
        for v, s in sorted(st.code_literals.items()):
            p("  %s code literal 0x%08X -> %s" % (st.ov.tag, v, s))
    for n in sorted(link.tramps):
        p("  trampoline %s 0x%08X (%s)" % (n, link.tramps[n], ", ".join(
            "ov%03d" % c for c in link.al.windows(link.tramps[n]))))
    for line in link.notes:
        p("  note: " + line)
    write_if_changed(path, "\n".join(r) + "\n")
    return r


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--aletheia-root", required=True)
    ap.add_argument("--overlays", default="TITLE",
                    help="overlay ids separated by ';' or ',', ALL, or TITLE")
    ap.add_argument("--link-sources", action="append", default=[],
                    help="file listing the other C sources of the link (repeatable)")
    ap.add_argument("--exclusions", default=DEFAULT_EXCLUSIONS)
    ap.add_argument("--ov-exclusions", default=DEFAULT_OV_EXCLUSIONS)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--set-name", default="build")
    ap.add_argument("--arm9-stubs", action="store_true")
    ap.add_argument("--asm-dir", default=os.path.join(EIDOS_ROOT, "sdk", "asm", "ov"),
                    help="portable C for asm-bodied overlay files, by file name")
    ap.add_argument("--port-ov-dir", default=os.path.join(EIDOS_ROOT, "stubs", "ov"),
                    help="port stand-ins for unmatched overlay functions, "
                         "ovNNN_<name>.c")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--define", action="append", default=[],
                    help="NAME=VALUE a port source is compiled with (repeatable)")
    args = ap.parse_args(argv)
    for d in args.define:
        k, _, v = d.partition("=")
        PORT_DEFINES[k] = v
    if not re.match(r"^[A-Za-z_]\w*$", args.set_name):
        raise SystemExit("ovlink.py: --set-name must be a C identifier")

    link = Link(args)
    out = os.path.abspath(args.out_dir)
    inc = os.path.join(out, "include")
    units = []
    for oid in link.ids:
        st = link.state[oid]
        oh = os.path.join(inc, "%s.h" % st.ov.tag)
        write_overlay_header(link, st, oh)
        for u in st.units:
            u.header = os.path.join(inc, "tu", flat_name(u.rel) + ".h")
            write_unit_header(link, u, oh, u.header)
            units.append("%d|%s|%s" % (oid, u.path.replace("\\", "/"),
                                        u.header.replace("\\", "/")))
        write_overlay_c(link, st, os.path.join(out, "%s_link.c" % st.ov.tag))
    write_if_changed(os.path.join(out, "ovl_c.txt"), "".join(
        os.path.join(out, "ov%03d_link.c" % oid).replace("\\", "/") + "\n"
        for oid in link.ids))
    write_dispatch(link, os.path.join(out, "ovl_dispatch.c"), args.set_name)
    write_arm9_stubs(link, os.path.join(out, "ovl_arm9_stubs.c"), args.arm9_stubs)
    write_if_changed(os.path.join(out, "ovl_units.txt"), "".join(x + "\n" for x in units))
    # What a re-run depends on beyond the tool: the overlays' shard
    # directories (a new matched file adds a shard there) and listings.
    deps = []
    for oid in link.ids:
        for rel in ("layout/matched/ov%03d" % oid, "asm/overlays/ov%03d.s" % oid):
            path = os.path.join(link.root, rel)
            if os.path.exists(path):
                deps.append(path.replace("\\", "/"))
    write_if_changed(os.path.join(out, "ovl_depends.txt"), "".join(x + "\n" for x in deps))
    needs = ["%s %s %s" % (n, hx(a) if a is not None else "-",
                           "defined" if d else "missing")
             for n, (a, d) in sorted(link.arm9_needs.items())]
    write_if_changed(os.path.join(out, "ovl_arm9_needs.txt"), "".join(x + "\n" for x in needs))
    # The matched ARM9 sources that define what the overlays call and the
    # link lacks: seeds for the boot's closure (cmake/boot.cmake), as
    # paths under src/matched/.
    by_name, by_addr = {}, {}
    for src, rows in link.al.arm9_src.items():
        for n, a in rows:
            by_name.setdefault(n, src)
            by_addr.setdefault(a, src)
    seeds = set()
    for n, (a, d) in link.arm9_needs.items():
        if d:
            continue
        src = by_name.get(n) or (by_addr.get(a & ~1) if a is not None else None)
        if src and src.startswith("src/matched/"):
            seeds.add(src[len("src/matched/"):])
    write_if_changed(os.path.join(out, "ovl_arm9_seeds.txt"),
                     "".join(x + "\n" for x in sorted(seeds)))
    rep = write_report(link, os.path.join(out, "ovl_report.txt"))
    if args.summary:
        print("\n".join(rep[:1 + len(link.ids)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
