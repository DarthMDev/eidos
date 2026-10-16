#!/usr/bin/env python3
"""
bootlink.py -- what it takes to link the game's own boot (docs/boot.md).

The boot (NitroMain and the closure of its matched callees, about 1400
translation units) is compiled from aletheia's matched C unchanged.  Linking
it on a host needs five things the DS linker gives for free, and this tool
works each of them out mechanically from aletheia's layout and sources and
from the port's own C:

 1. The source list.  tools/closure.py's fixpoint from NitroMain's callees,
    minus the matched files a port shim replaces (cmake/boot.cmake,
    EIDOS_BOOT_SHIM_REPLACED, checked here against the names the shims
    actually define), minus the files whose bodies are mwcc `asm` (a host
    compiler cannot build them; sdk/asm/<file>.c replaces one when it
    exists), minus the files the eidos library already compiles.

 2. Names by address.  The DS links by address, a host by name: a caller
    that says RTC_Init reaches a matched file that defines func_02088788,
    and a caller that says func_02088610 reaches the shim's
    PXI_SendWordByFifo.  Each translation unit gets a forced-include header
    (-include, /FI) that renames, for that file only, every such spelling
    to the one that is defined.  The matched file is not touched
    (portability rule 1).

 3. Implicit declarations.  mwcc accepts a call with no declaration in
    scope; GCC 14 does not.  The same header declares each such callee with
    the return type of its own definition and no parameter list (C99's
    non-prototype declaration: arguments get the default promotions, which
    is what the DS code was compiled against).

 4. Link-time constants.  `extern char SDK_IRQ_STACKSIZE[]` is 0x800
    because the DS linker puts the symbol *at* 0x800 (closure.py, class
    `const`).  The header renames each to `(*bw_lc_NAME)`, a pointer object
    whose value is the constant, so `(u32)SDK_IRQ_STACKSIZE` is 0x800 and
    `&SDK_AUTOLOAD_DTCM_START` is a pointer.  A value inside the DS address
    space is set at start-up to bw_ds_ptr(value) (bw_boot_link_init), so
    SDK_AUTOLOAD_DTCM_START names the bytes at DS 0x02FE0000 and the game's
    vblank flag, OS_WaitIrq and the shims all see the same word.  DTCM
    objects (0x02FE0000-0x02FE3FFF) no shim defines are pinned the same way.

 5. Data.  Every ARM9 data name a matched file uses is tools/armdata.py's
    (docs/armdata.md): bound at reset to its DS address in the player's own
    image.  This tool reads that generator's header (--armdata-header) and
    leaves every name it binds alone; a data name it does not bind and
    nothing defines fails the generation, by name.

 6. Functions nothing defines, generated and listed: a linker veneer (a
    `ldr ip, [pc]; bx ip` island with no source) becomes a forwarder to the
    function its target word names, read from the player's arm9_dec.bin
    when it is there; unmatched game code (`open`) and the asm-bodied SDK
    files of step 1 with no sdk/asm/ replacement yet become stubs that
    panic with their address and name when called.

Two modes:

    bootlink.py plan --aletheia-root R --seed S ... --exclude-file F
                     --shim-sources L --port-sources P --library-matched M
                     --asm-dir D [--define NAME=VALUE] --out-dir O
        configure time: writes O/boot_matched.txt (the sources to compile),
        O/boot_asm.txt (asm-bodied files and how each is handled),
        O/boot_asm_subst.txt (sdk/asm replacements to compile) and
        O/boot_plan.txt (a summary).  Fails when the exclusion list and the
        shims' definitions disagree.

    bootlink.py generate --aletheia-root R --plan-dir O --shim-sources L
                     --port-sources P --armdata-header H
                     [--define NAME=VALUE] --out-dir G
        build time: writes one header per translation unit
        (G/include/<name>.h), G/boot_link.c and
        G/boot_link_report.txt.

Standard library only; reads aletheia, writes only under the given dirs.
"""

from __future__ import annotations

import argparse
import os
import re
import sys

# Run from the build: no __pycache__ in the source tree.
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import closure  # noqa: E402
import ovlink    # noqa: E402  (the prototype normaliser, tools/ovlink.py)

DATA_ADDR_NAME = re.compile(r"^[A-Za-z]+_([0-9A-Fa-f]{8})$")
BW_REG = re.compile(r"\bBW_REG\(\s*(0x[0-9A-Fa-f]+)u?\s*,\s*([A-Za-z_]\w*)")
DTCM_LO, DTCM_HI = 0x02FE0000, 0x02FE4000
# A function whose whole body is one call (an alias forwarder):
# `T name(params) { [return] [(cast)] callee(...); }`.
FORWARDER = re.compile(
    r"\b([A-Za-z_]\w*)\s*\([^;{}()]*\)\s*\{\s*(?:return\s+)?(?:\([^()]*\)\s*)?"
    r"([A-Za-z_]\w*)\s*\([^;{}]*\)\s*;\s*\}")

VENEERS = closure.VENEERS
DS_LO, DS_HI = 0x02000000, 0x08000000


def read_list(path):
    out = []
    if not path:
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


def rel_to_root(path):
    """A source path as the manifest spells it (src/...), or None."""
    path = os.path.abspath(path).replace("\\", "/")
    root = os.path.abspath(closure.ROOT).replace("\\", "/")
    if path.lower().startswith(root.lower() + "/"):
        return path[len(root) + 1:]
    return None


# Port-side macros the generated build passes (bootlink.py --define), for
# the `#if EIDOS_HEAP_MATCHED` blocks in sdk/ that decide which names exist.
PORT_DEFINES = {}
PP_IF = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


def _pp_value(kind, expr):
    """True/False for the conditionals this build decides, None otherwise."""
    expr = expr.split("/*")[0].split("//")[0].strip()
    if kind in ("ifdef", "ifndef"):
        if expr not in PORT_DEFINES:
            return None
        return (kind == "ifdef")
    neg = expr.startswith("!")
    name = expr[1:].strip() if neg else expr
    if name in PORT_DEFINES:
        value = PORT_DEFINES[name] not in ("0", "")
        return not value if neg else value
    return None


def preprocess(text):
    """Blank the branches of the conditionals PORT_DEFINES decides."""
    out = []
    stack = []                 # (this branch live, some branch taken, decided)
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


def scan_file(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        return closure.SourceScan(preprocess(fh.read()))


def header_name(path):
    """include/<flattened path>.h for one translation unit."""
    rel = rel_to_root(path) or ("port/" + os.path.basename(path))
    return re.sub(r"[^A-Za-z0-9_]", "_", rel[:-2] if rel.endswith(".c") else rel) + ".h"


class Shims:
    """Every non-static name the eidos library (and boot port code) defines."""

    def __init__(self, idx, paths):
        self.defines = {}            # name -> defining file
        self.dups = []
        self.data_at = {}            # DS address -> data object name
        self.forwards = {}           # name -> the one function its body calls
        for path in paths:
            if not os.path.exists(path):
                continue             # generated at build time (overlay table)
            rel = rel_to_root(path)
            if rel and rel in idx.sources:
                sc = idx.scan(rel)
            else:
                sc = scan_file(path)
                with open(path, encoding="utf-8", errors="replace") as fh:
                    text = fh.read()
                for addr, name in BW_REG.findall(text):
                    self.data_at.setdefault(int(addr, 16), name)
                for name, callee in FORWARDER.findall(text):
                    self.forwards.setdefault(name, callee)
            for name in sorted(sc.defined - sc.statics):
                if name in self.defines and self.defines[name] != path:
                    self.dups.append((name, self.defines[name], path))
                    continue
                self.defines[name] = path


# ---------------------------------------------------------------------------
# plan
# ---------------------------------------------------------------------------

def defined_addr(idx, rel, name):
    """The DS address of a function `rel` (a matched source, or None for
    port code) defines as `name`.

    The manifest knows the lead function of every unit by address whatever
    it is called (OS_LockCard at 0x02084E38, NHTTP_CleanupAsync at
    0x0206074C, names the SDK table spells otherwise or not at all); a
    member's address comes from its func_/sub_ spelling or the SDK table.
    So any name whose address a compiled source defines resolves to it.
    """
    if rel:
        # The manifest's own address for the lead first: the SDK table's
        # spelling of a name can sit at another address (a signature
        # matcher's guess), and the manifest is what the DS build linked.
        entry = idx.sources.get(rel)
        if entry is not None and name in (entry["name"], entry["functions"][0]):
            return entry["address"]
        addr = idx.member_addr.get((rel, name))
        if addr is not None:
            return addr
    addr = idx._name_addr(name)
    if addr is None:
        addr = idx.rt_by_name.get(name)
    return addr


_OVERLAYS = None


def overlays_at(idx, addr):
    """The overlays whose code range holds `addr`, as "ov012" or "ov012 or
    ov013": the one aletheia's manifest has a function at exactly `addr`
    when there is one, else every overlay loaded over that range (overlays
    share address ranges, so the DS address alone does not say which)."""
    global _OVERLAYS
    if _OVERLAYS is None:
        _OVERLAYS = []
        path = os.path.join(closure.ROOT, "layout", "layout.json")
        if os.path.exists(path):
            import json
            with open(path, encoding="utf-8") as fh:
                for ov in json.load(fh).get("overlays", []):
                    _OVERLAYS.append((ov["id"], ov["ram_start"], ov["ram_end"]))
    exact = sorted({e["region"] for e in idx.entries
                    if e["address"] == addr and e["region"].startswith("ov")})
    if exact:
        return " or ".join(exact)
    hits = ["ov%03d" % i for i, lo, hi in _OVERLAYS if lo <= addr < hi]
    if len(hits) > 4:
        return "one of %d overlays, %s .. %s" % (len(hits), hits[0], hits[-1])
    return " or ".join(hits)


def port_references(idx, path):
    """The matched sources whose functions a port file calls, by name or by
    address spelling."""
    if not os.path.exists(path):
        return []
    sc = scan_file(path)
    out = set()
    for name in sc.references(idx.is_global_name)[0]:
        if name in sc.defined:
            continue
        owner = idx.defines.get(name)
        if owner is None:
            addr = idx._name_addr(name)
            entry = idx.entry_at(VENEERS.get(addr, addr)) if addr is not None else None
            owner = entry["source"] if entry else None
        if owner is not None:
            out.add(owner)
    return sorted(out)


def defined_names(idx, source):
    """A matched source's non-static names, the SDK names at their
    addresses, and the address spelling (func_XXXXXXXX) of each, so that a
    shim matches the file by address whatever either side calls it."""
    sc = idx.scan(source)
    out = set()
    for name in sc.defined - sc.statics:
        out.add(name)
        addr = defined_addr(idx, source, name)
        if addr is not None:
            out.add("func_%08X" % addr)
            if addr in idx.sdk_by_addr:
                out.add(idx.sdk_by_addr[addr])
    return out


THUNK_BODY = re.compile(
    r"^\s*(?:void|int|u32|s32|unsigned\s+int|BOOL)\s+([A-Za-z_]\w*)\s*\(\s*void\s*\)\s*\{"
    r"\s*(?:return\s+)?([A-Za-z_]\w*)\s*\(\s*\)\s*;\s*\}", re.M)


def thunk_of(idx, source):
    """(name, callee) when the matched source's one function is a bare tail
    call `void f(void) { g(); }`, else None."""
    sc = idx.scan(source)
    funcs = sc.defined - sc.statics
    if len(funcs) != 1:
        return None
    text = open(os.path.join(closure.ROOT, source), encoding="utf-8", errors="replace").read()
    code = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    m = THUNK_BODY.search(code)
    if not m or m.group(1) not in funcs:
        return None
    return m.group(1), m.group(2), text


def write_thunk(idx, source, out_dir):
    """A port unit for a tail-call thunk whose callee takes arguments: the
    thunk forwards them, as the DS does (r0-r3 untouched by `ldr ip, =g;
    bx ip`), with the callee's prototype normalised as tools/ovlink.py's
    trampolines have it.  Returns (path, None) or (None, why)."""
    import ovlink  # noqa: E402  (tools/ovlink.py, the prototype normaliser)

    t = thunk_of(idx, source)
    if t is None:
        return None, None
    name, callee, text = t
    addr, _kind = idx.address_of(source, callee)
    if addr is None:
        return None, "callee %s has no address" % callee
    entry = idx.entry_at(addr)
    if entry is None:
        return None, "nothing defines 0x%08X" % addr
    csrc = entry["source"]
    path = os.path.join(closure.ROOT, csrc)
    sc = ovlink.Scan(path, ovlink.read_text(path))
    cname = None
    for n in sorted(sc.heads):
        if defined_addr(idx, csrc, n) == addr:
            cname = n
            break
    if cname is None:
        return None, "no definition of 0x%08X in %s" % (addr, csrc)
    sig = ovlink.signature(sc.heads[cname], cname, sc.typedefs)
    if sig is None:
        return None, "the prototype of %s does not normalise" % cname
    ret, params = sig
    if not params and ret == "void":
        return None, None                  # nothing travels: the C is right
    isa = re.search(r"^\s*\*\s*isa:\s*(arm|thumb)\b", text[:4096], re.M)
    args_ = ", ".join("a%d" % k for k in range(len(params)))
    lines = [
        "/*",
        " * function: %s" % name,
        " * isa:      %s" % (isa.group(1) if isa else "arm"),
        " *",
        " * Generated by tools/bootlink.py in place of %s:" % source,
        " * `%s` is a tail call (the DS passes r0-r3 through" % ("%s() { %s(); }" % (name, callee)),
        " * untouched), so it forwards what %s takes" % cname,
        " * (%s), and returns what it returns.  docs/boot.md," % source_of_decl(csrc),
        " * \"Arguments in registers\".",
        " */",
        "",
        "%s" % ovlink.proto_decl(ret, callee, params),
        "%s;" % ovlink.proto(ret, name, params),
        "",
        ovlink.proto(ret, name, params),
        "{",
        "    %s%s(%s);" % ("" if ret == "void" else "return ", callee, args_),
        "}",
        "",
    ]
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, os.path.basename(source)).replace("\\", "/")
    write_if_changed(out, "\n".join(lines))
    return out, None


def source_of_decl(rel):
    return rel


def plan(args):
    idx = closure.Index()
    seeds = [idx.resolve_seed(s) for s in args.seed]
    # A seed file lists whole directories' worth (EIDOS_BOOT_ALL_GAME): a
    # file aletheia has added to src/matched before its manifest lists it
    # is left out until the manifest does, rather than failing the plan.
    unlisted = []
    for path in args.seed_file:
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                s = line.strip()
                if not s:
                    continue
                try:
                    seeds.append(idx.resolve_seed(s))
                except SystemExit:
                    unlisted.append(s)
    excluded = read_list(args.exclude_file)
    unknown = [s for s in excluded if s not in idx.sources]
    if unknown:
        raise SystemExit("bootlink.py: excluded sources not in the manifest: %s"
                         % ", ".join(unknown))
    library = set()
    for path in read_list(args.library_matched):
        rel = rel_to_root(path)
        if rel:
            library.add(rel)

    # The boot's own port code (NitroMain's reimplementation, sdk/boot/,
    # stubs/reimpl_ files, the sdk/asm/ replacements of asm files in the
    # closure) calls matched functions too: those seed the walk as well,
    # until nothing new is reached.
    port_paths = read_list(args.port_sources) if args.port_sources else []
    asm_dir_abs = os.path.abspath(args.asm_dir) if args.asm_dir else None
    library_paths = set(read_list(args.library_matched))
    while True:
        sources, _open = idx.closure(seeds, stop=set(excluded))
        extra = list(port_paths)
        if asm_dir_abs:
            for source in sources:
                repl = os.path.join(asm_dir_abs, os.path.basename(source))
                if idx.scan(source).has_asm and os.path.exists(repl):
                    extra.append(repl)
        more = []
        for path in extra:
            for rel in port_references(idx, path):
                if rel not in seeds and rel not in sources and rel not in library \
                        and os.path.join(closure.ROOT, rel).replace("\\", "/") not in library_paths:
                    more.append(rel)
        if not more:
            break
        seeds = seeds + sorted(set(more))

    # The library's port code (sdk/, stubs/, and the stubs/reimpl_ files its
    # matched lists carry) is what the boot's matched files must not clash
    # with.
    shim_paths = read_list(args.library_matched)
    for listing in args.shim_sources:
        shim_paths += read_list(listing)
    shims = Shims(idx, [p for p in shim_paths if rel_to_root(p) is None])

    # The exclusion list has to be exactly the closure's sources that define
    # a name a shim defines: one missing is a duplicate symbol at link time,
    # one extra is a matched file left out for no reason.
    asm_dir = os.path.abspath(args.asm_dir) if args.asm_dir else None
    shim_set = {os.path.normcase(os.path.abspath(p)) for p in shim_paths}

    def asm_replacement(source):
        """sdk/asm/<same name>.c for an asm-bodied source, or None."""
        if not asm_dir or not idx.scan(source).has_asm:
            return None
        repl = os.path.join(asm_dir, os.path.basename(source))
        return repl if os.path.exists(repl) else None

    # A shim that only forwards to the address's other spelling
    # (sdk/aliases.c: sub_02060C5C calls func_02060C5C) is a second name for
    # the matched function, not a replacement of it.
    shim_addrs = set()
    for n in shims.defines:
        addr = idx._name_addr(n)
        if addr is None:
            continue
        callee = shims.forwards.get(n)
        if callee is not None and idx._name_addr(callee) == addr:
            continue
        shim_addrs.add(addr)
    clashes = {}
    for source in sources:
        if source in library or asm_replacement(source):
            # An asm file is never compiled: its sdk/asm/ replacement (in
            # the library or compiled for the boot) defines its names.
            continue
        # By name, and by address: a shim defining sub_02086788 and a
        # matched file defining func_02086788 are one function twice, and a
        # caller would reach one or the other depending on its spelling.
        hit = sorted(n for n in defined_names(idx, source)
                     if n in shims.defines
                     or (idx._name_addr(n) is not None
                         and idx._name_addr(n) in shim_addrs))
        if hit:
            clashes[source] = hit
    in_closure = set(sources)
    missing = sorted(s for s in clashes if s not in excluded)
    extra = sorted(s for s in excluded if s in in_closure and s not in clashes)

    compile_list, asm_rows, subst, thunks = [], [], [], []
    for source in sorted(sources):
        if source in excluded or source in library:
            continue
        if idx.scan(source).has_asm:
            repl = asm_replacement(source)
            if repl and os.path.normcase(os.path.abspath(repl)) in shim_set:
                # BW_ASMSDK_SOURCES: already in the eidos library.
                asm_rows.append("%s in-library %s" % (source, repl.replace("\\", "/")))
            elif repl:
                asm_rows.append("%s replaced-by %s" % (source, repl.replace("\\", "/")))
                subst.append(repl.replace("\\", "/"))
            else:
                asm_rows.append("%s pending" % source)
            continue
        thunk, why = write_thunk(idx, source, os.path.join(args.out_dir, "thunk"))
        if thunk is not None:
            thunks.append("%s thunk %s" % (source, thunk))
            subst.append(thunk)
            continue
        if why is not None:
            thunks.append("%s kept: %s" % (source, why))
        compile_list.append(os.path.join(closure.ROOT, source).replace("\\", "/"))

    out = args.out_dir
    write_if_changed(os.path.join(out, "boot_thunks.txt"), "".join(r + "\n" for r in thunks))
    write_if_changed(os.path.join(out, "boot_matched.txt"), "".join(p + "\n" for p in compile_list))
    write_if_changed(os.path.join(out, "boot_asm.txt"), "".join(r + "\n" for r in asm_rows))
    write_if_changed(os.path.join(out, "boot_asm_subst.txt"), "".join(p + "\n" for p in subst))

    lines = [
        "closure from %d seeds: %d matched translation units" % (len(seeds), len(sources)),
        "  replaced by a port shim (cmake/boot.cmake): %d"
        % len([s for s in sources if s in excluded]),
        "  already in the eidos library: %d" % len([s for s in sources if s in library]),
        "  mwcc asm bodies: %d (%d with an sdk/asm replacement)" % (len(asm_rows), len(subst)),
        "  tail-call thunks forwarding their callee's arguments: %d"
        % len([t for t in thunks if " thunk " in t]),
        "  compiled for the boot: %d" % len(compile_list),
        "  seeds not in aletheia's manifest yet, left out: %d%s" % (
            len(unlisted), (" (" + ", ".join(sorted(os.path.basename(u) for u in unlisted)) + ")")
            if unlisted else ""),
    ]
    for source in missing:
        lines.append("NOT EXCLUDED, defines a shim's name: %s (%s)"
                     % (source, ", ".join(clashes[source])))
    for source in extra:
        lines.append("excluded without a name clash (kept out by decision): %s" % source)
    write_if_changed(os.path.join(out, "boot_plan.txt"), "\n".join(lines) + "\n")
    print("\n".join(lines))
    if missing:
        return 1
    return 0


# ---------------------------------------------------------------------------
# generate
# ---------------------------------------------------------------------------

def return_type(head, name):
    """The return type for a non-prototype declaration of `name`.

    mwcc compiled each such call as C89's implicit `int name()`, and the
    callers use the result that way (some store a void function's "result",
    some put a pointer in an int).  So the declaration is `int name()`, the
    one the DS code was compiled against, unless the definition returns a
    64-bit value, which does not travel in one int-sized register on any
    host.
    """
    before = head.split("(")[0]
    before = before[:before.rfind(name)] if name in before else before
    words = [w for w in re.findall(r"[A-Za-z_]\w*", before)
             if w not in ("static", "inline", "extern", "const", "volatile")]
    if "*" not in before and (any(w in ("u64", "s64", "fx64", "OSTick") for w in words)
                              or " ".join(words).count("long long")):
        return "long long"
    return "int"


# mwcc builtins a few matched files call with no declaration, and the port
# function each becomes (sdk/overlay_image.c; tools/ovlink.py does the same
# for the overlays).
MWCC_BUILTINS = {
    "__builtin__clear": ("bw_mwcc_clear", "void bw_mwcc_clear(void *dst, unsigned int size);"),
    "__memcpy": ("bw_mwcc_memcpy",
                 "void *bw_mwcc_memcpy(void *dst, const void *src, unsigned int size);"),
}


PARAM_LIST = re.compile(r"\(([^()]*)\)")
STRUCT_TAG = re.compile(r"\bstruct\s+([A-Za-z_]\w*)")


def param_struct_tags(path):
    """Struct tags that appear inside a parenthesised list before any
    declaration of the tag at file scope."""
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            code = closure.strip_source(fh.read())
    except OSError:
        return set()
    out = set()
    for m in PARAM_LIST.finditer(code):
        for tag in STRUCT_TAG.findall(m.group(1)):
            first = re.search(r"\bstruct\s+" + tag + r"\b", code)
            if first is not None and first.start() >= m.start():
                out.add(tag)
    return out


class Unit:
    """One translation unit of the boot and what its header must say."""

    def __init__(self, path, rel, scan):
        self.path = path
        self.rel = rel              # src/... for a matched file, else None
        self.scan = scan
        self.renames = {}           # spelling -> defined name
        self.consts = set()
        self.protos = {}            # name -> return type
        self.private = {}           # name -> this file's own copy's name


# The declared hookable set: mod/declared_set.txt (docs/mod-interface.md,
# section 1).  The arm9 rows get a slot here; the overlay rows are
# tools/ovlink.py's.
DECLARED_SET = os.path.join(closure.EIDOS_ROOT, "mod", "declared_set.txt")


def read_declared_set(region=None):
    """Rows of mod/declared_set.txt: (address, name, region), the rows of
    `region` only when it is given.  `#` starts a comment; a line is tab
    separated."""
    rows = []
    if not os.path.exists(DECLARED_SET):
        return rows
    with open(DECLARED_SET, encoding="utf-8") as fh:
        for line in fh:
            line = line.split("#", 1)[0]
            if not line.strip():
                continue
            parts = [p.strip() for p in line.rstrip("\n").split("\t")]
            if len(parts) < 3 or not parts[0].startswith("0x"):
                continue
            if region is not None and parts[2] != region:
                continue
            rows.append((int(parts[0], 16), parts[1], parts[2]))
    return rows


def definition_signature(path, name):
    """(return type, [param types]) of `name` as the file at `path` defines
    it, normalised by tools/ovlink.py's signature (the call write_thunk
    makes), or None when it does not normalise."""
    if not path or not os.path.exists(path):
        return None
    sc = ovlink.Scan(path, ovlink.read_text(path))
    head = sc.heads.get(name)
    if head is None:
        return None
    return ovlink.signature(head, name, sc.typedefs)


def slot_function(region_expr, addr, slot):
    """The generated slot for one declared address: the indirection at the
    call site (docs/mod-interface.md, section 2).  Its default is the
    original, so with no mod installed it behaves as the direct call did."""
    ret, params = slot["sig"]
    name = slot["name"]
    args = ", ".join("a%d" % k for k in range(len(params)))
    cast = "%s (*)(%s)" % (ret, ", ".join(params) or "void")
    out = [ovlink.proto(ret, name, params), "{"]
    out.append("    void *h = bw_mod_slot_lookup(%s, 0x%08Xu);" % (region_expr, addr))
    out.append('    if (h != NULL && bw_hook_enabled("%s")) {' % slot["declared"])
    if ret == "void":
        out.append("        ((%s)h)(%s);" % (cast, args))
        out.append("        return;")
        out.append("    }")
        out.append("    %s(%s);" % (slot["original"], args))
    else:
        out.append("        return ((%s)h)(%s);" % (cast, args))
        out.append("    }")
        out.append("    return %s(%s);" % (slot["original"], args))
    out.append("}")
    out.append("")
    return "\n".join(out)


def generate(args):
    idx = closure.Index()
    pdir = args.plan_dir
    matched = read_list(os.path.join(pdir, "boot_matched.txt"))
    port = read_list(args.port_sources) + read_list(os.path.join(pdir, "boot_asm_subst.txt"))
    asm_rows = [r.split() for r in read_list(os.path.join(pdir, "boot_asm.txt"))]
    data_bound, data_objects, data_lines = read_armdata_header(args.armdata_header)

    units = []
    for path in matched:
        rel = rel_to_root(path)
        units.append(Unit(path, rel, idx.scan(rel)))
    for path in port:
        units.append(Unit(path, None, scan_file(path)))

    # What is defined, and where.
    shims = Shims(idx, read_list(args.shim_sources)
                  + read_list(os.path.join(pdir, "library_matched.txt")))
    defined = dict(shims.defines)
    # The data generator's host objects are definitions like any other.
    for name in data_objects:
        defined.setdefault(name, args.armdata_header)
    # Two library files defining one name is the library's own business
    # (often an #if this scan does not evaluate); only a boot file against
    # anything is a duplicate this link would hit.
    #
    # Two boot files defining one name: a matched file that carries, besides
    # the function its manifest entry claims, a neighbour another file
    # claims too (a unit kept next to the single-function files split out of
    # it; the overlays' ARM9 callees bring such pairs in).  mwcc's link took
    # the claimed one.  The claiming file owns the name; the other's copy is
    # renamed to a name of its own in that file's header, and its own calls
    # keep using it (tools/ovlink.py does the same inside an overlay).
    unit_at = {u.path: u for u in units}

    def claims(u, name):
        entry = idx.sources.get(u.rel) if u.rel else None
        return bool(entry) and name in (entry.get("functions") or [entry.get("name")])

    dups = []
    for unit in units:
        for name in sorted(unit.scan.defined - unit.scan.statics):
            if name in defined and defined[name] != unit.path:
                other = unit_at.get(defined[name])
                if other is not None and other.rel and unit.rel:
                    owner, copy = ((unit, other) if claims(unit, name) and not claims(other, name)
                                   else (other, unit))
                    copy.private[name] = "%s__%s" % (
                        name, re.sub(r"[^A-Za-z0-9_]", "_", os.path.basename(copy.rel)[:-2]))
                    defined[name] = owner.path
                    continue
                dups.append((name, defined[name], unit.path))
                continue
            defined[name] = unit.path

    # Defined functions by address (func_/sub_ spellings and SDK names), and
    # defined data by address (g_XXXXXXXX spellings and BW_REG rows).
    func_at, data_at = {}, dict(shims.data_at)
    for name in sorted(defined):
        addr = defined_addr(idx, rel_to_root(defined[name]), name)
        if addr is not None:
            func_at.setdefault(addr, name)
        m = DATA_ADDR_NAME.match(name)
        if m and not closure.ADDR_NAME.match(name):
            data_at.setdefault(int(m.group(1), 16), name)

    # The declared ARM9 rows and the slot each one gets (docs/mod-interface.md,
    # section 2).  The definition's prototype comes from the defining unit and
    # is normalised by tools/ovlink.py's signature.  A row whose definition the
    # link does not carry is reported and skipped, like an unbound data name.
    arm9_slots = {}            # address -> slot dict
    slot_skipped = []          # (declared name, address, why)
    for addr, dname, _region in read_declared_set("arm9"):
        oname = func_at.get(addr)
        if oname is None and dname in defined:
            # A unit's member whose spelling is not func_/sub_ and not in the
            # SDK table (BEACON_SetFlag in unit_0200C0E8.c): the declared row
            # carries the address, and the unit defines the name.
            oname = dname
        if oname is None:
            slot_skipped.append((dname, addr, "nothing in the link defines it"))
            continue
        sig = definition_signature(defined.get(oname), oname)
        if sig is None:
            slot_skipped.append((dname, addr,
                                 "%s: its prototype does not normalise" % oname))
            continue
        arm9_slots[addr] = {
            "name": "bw_slot_arm9_%08X" % addr,
            "declared": dname,
            "original": oname,
            "sig": sig,
        }

    # Every data spelling the boot uses, by address, so one object stands
    # for one DS address.
    data_names = {}            # address -> set of spellings
    data_noaddr = set()
    missing_funcs = {}         # name -> (address or None, why)
    consts = {}                # name -> value

    # The functions the asm-bodied files define, by name and by address:
    # a stub takes the defining file's own spelling, so the sdk/asm/
    # replacement (same file name, same names) drops in without a rename.
    asm_pending, asm_at = {}, {}
    for row in asm_rows:
        if len(row) >= 2 and row[1] == "pending":
            sc = idx.scan(row[0])
            for name in sorted(sc.defined - sc.statics):
                asm_pending[name] = row[0]
                addr = defined_addr(idx, row[0], name)
                if addr is not None:
                    asm_at.setdefault(addr, name)
    missing_refs = []          # (unit, name, address)

    def refs_of(unit):
        if unit.rel:
            names = unit.scan.references(idx.is_global_name)[0]
            out = []
            for name in names:
                addr, kind = idx.address_of(unit.rel, name)
                klass, _detail = idx.classify(unit.rel, name)
                if klass == "local":
                    # The file calls itself by another name (an SDK
                    # function's recursion through its library name).
                    own = None
                    if addr is not None and name not in unit.scan.defined:
                        own = idx.owner_name(idx.sources[unit.rel], addr)
                    if own and own != name:
                        unit.renames[name] = own
                    continue
                out.append((name, addr, kind, klass))
            return out
        out = []
        for name in unit.scan.references(idx.is_global_name)[0]:
            addr = idx._name_addr(name)
            out.append((name, addr, None, "data" if addr is None and
                        name not in unit.scan.prototyped else "func"))
        return out

    # A name's DS address as any boot file's `symbols:` block records it,
    # for the files whose own header does not list it.
    known_addr = {}
    for unit in units:
        for name, (addr, _kind) in unit.scan.symbols.items():
            known_addr.setdefault(name, addr)

    pending_data = []          # (unit, name, address)
    for unit in units:
        for name, addr, kind, klass in refs_of(unit):
            if name in data_bound:
                # Bound to its DS address by the data header (a matched
                # file's own declaration, read through a pointer variable).
                continue
            if addr is None and name in known_addr:
                addr = known_addr[name]
            if addr is not None and addr in arm9_slots \
                    and name in unit.scan.called \
                    and defined.get(arm9_slots[addr]["original"]) != unit.path:
                # A declared ARM9 address: the caller goes through the slot.
                # The defining unit keeps its own name and gets no rename.
                unit.renames[name] = arm9_slots[addr]["name"]
                continue
            if closure.is_link_constant(name, addr, kind):
                if addr is None:
                    raise SystemExit("bootlink.py: link constant %s has no value in %s"
                                     % (name, unit.path))
                consts[name] = addr
                unit.consts.add(name)
                continue
            if name in defined:
                # A spelling another file defines at another address: the
                # SDK's name reused for a different function (sub_0204968C.c
                # calls 0x020307B0 "ENV_SetResourceSet"; the SDK's is at
                # 0x0206434C).  The file's own symbols row says which one it
                # calls, and mwcc's link went by that address.
                if kind in ("arm", "thumb") and addr is not None and addr in func_at \
                        and func_at[addr] != name:
                    daddr = defined_addr(idx, rel_to_root(defined[name]), name)
                    if daddr is not None and daddr != addr:
                        unit.renames[name] = func_at[addr]
                continue
            is_data = kind == "data" or klass == "data"
            if not is_data and addr is not None and addr in func_at:
                unit.renames[name] = func_at[addr]
                continue
            if not is_data and addr in VENEERS and VENEERS[addr] in func_at:
                # A linker veneer is another address of its target.
                unit.renames[name] = func_at[VENEERS[addr]]
                continue
            if not is_data and klass != "data":
                if addr is not None and DTCM_LO <= addr < DTCM_HI:
                    is_data = True
                else:
                    missing_refs.append((unit, name, addr))
                    continue
            if addr is None:
                m = DATA_ADDR_NAME.match(name)
                addr = int(m.group(1), 16) if m else None
            pending_data.append((unit, name, addr))
            if addr is None:
                data_noaddr.add(name)
            else:
                data_names.setdefault(addr, set()).add(name)

    # One stub per missing function address, under the asm file's spelling
    # when there is one, else the first spelling in sort order.
    spellings = {}
    for _unit, name, addr in missing_refs:
        spellings.setdefault(addr if addr is not None else name, set()).add(name)
    stub_name = {}
    for key, names in spellings.items():
        if not isinstance(key, str) and key in asm_at:
            stub_name[key] = asm_at[key]
        else:
            stub_name[key] = sorted(names)[0]
        name = stub_name[key]
        addr = None if isinstance(key, str) else key
        if name in defined:
            # The chosen stub name is already a definition in this link (a
            # compiled unit or a shim): the asm file's spelling is that
            # definition under another name, so rename the reference onto it
            # and emit no stub.  MYSTATUS_NameToStrbuf (0x02008500) is the
            # case: the asm file names it and unit_020084BC.c defines it as C.
            continue
        if name in asm_pending:
            why = "asm body, %s" % asm_pending[name]
        elif addr is not None and overlays_at(idx, addr):
            why = "open, overlay code (%s) called from the static module"                 % overlays_at(idx, addr)
        else:
            why = "open"
        missing_funcs[name] = (addr, why)
    for unit, name, addr in missing_refs:
        target = stub_name[addr if addr is not None else name]
        if target != name:
            unit.renames[name] = target

    views = {}                 # second spelling in one file -> canonical
    # One spelling per DS address: the one a shim or data source defines,
    # else the g_XXXXXXXX spelling, else the first in sort order.  DTCM
    # objects nobody defines are pinned to the DS bytes, like a constant.
    canonical = {}
    pinned = {}
    for addr, names in sorted(data_names.items()):
        if addr in data_at:
            canonical[addr] = data_at[addr]
            continue
        spelled = sorted(n for n in names if n == "g_%08X" % addr)
        canonical[addr] = spelled[0] if spelled else sorted(names)[0]
        if DTCM_LO <= addr < DTCM_HI:
            pinned[canonical[addr]] = addr
    for unit, name, addr in pending_data:
        if addr is None:
            continue
        target = canonical[addr]
        if target != name and (target in unit.scan.declared or target in unit.scan.defined):
            # The file names this address twice, with two types (a struct and
            # a word view of it): a rename would make one declaration
            # conflict with the other, so the second spelling reads through
            # a pointer to the first.
            unit.consts.add(name)
            views[name] = target
            continue
        if target in pinned:
            unit.consts.add(target)
            if target != name:
                unit.renames[name] = target
        elif target != name:
            unit.renames[name] = target

    # Implicit calls: a non-prototype declaration of what the call reaches.
    heads = {}
    for unit in units:
        heads.update({n: h for n, h in unit.scan.heads.items() if n not in unit.scan.statics})
    for unit in units:
        if not unit.rel:
            continue
        # A name the file's own `#include` header declares must not get a
        # generated non-prototype declaration: the header's declaration,
        # parsed after the forced include, would conflict with it.  The
        # generated header cannot see the file's includes; this scan can.
        inc_decls = closure.included_declarations(unit.path)
        for name in idx.implicit_calls(unit.rel):
            if name in inc_decls:
                continue
            target = unit.renames.get(name, name)
            if target in inc_decls:
                continue
            if target in unit.scan.declared or target in unit.scan.defined:
                continue
            head = heads.get(target)
            if head is None and target in defined and os.path.exists(defined[target]):
                head = scan_file(defined[target]).heads.get(target)
            unit.protos[target] = return_type(head, target) if head else "int"

    # Write the headers.
    out = args.out_dir
    incdir = os.path.join(out, "include")
    for unit in units:
        lines = ["/* generated by tools/bootlink.py for %s; do not edit */"
                 % (unit.rel or os.path.basename(unit.path))]
        # The data header's lines for the names this unit names and does
        # not define as a macro itself (a file that says `#define tpState
        # g_02152FA4` binds through the other name).
        idents, macros = unit_identifiers(unit.path)
        for name in sorted(data_lines):
            if name in idents and name not in macros:
                lines.append(data_lines[name])
        for name in sorted(unit.renames):
            lines.append("#define %s %s" % (name, unit.renames[name]))
        for name in sorted(unit.private):
            lines.append("#define %s %s   /* another boot file owns %s */"
                         % (name, unit.private[name], name))
        # A struct tag a prototype names only inside its parameter list is
        # scoped to that list (GCC warns, with no option to say otherwise);
        # mwcc took it as the file's tag.  A forward declaration first makes
        # it one.
        for tag in sorted(param_struct_tags(unit.path)):
            lines.append("struct %s;" % tag)
        for name in sorted(MWCC_BUILTINS):
            if name in unit.scan.called and name not in unit.scan.declared \
                    and name not in unit.scan.defined:
                port, decl = MWCC_BUILTINS[name]
                lines.append(decl)
                lines.append("#define %s %s" % (name, port))
        for name in sorted(unit.consts):
            lines.append("#define %s (*bw_lc_%s)" % (name, name))
        for name in sorted(unit.protos):
            rtype = unit.protos[name]
            sep = "" if rtype.endswith("*") else " "
            lines.append("%s%s%s();" % (rtype, sep, name))
        write_if_changed(os.path.join(incdir, header_name(unit.path)), "\n".join(lines) + "\n")

    write_link_c(os.path.join(out, "boot_link.c"), consts, pinned, views, missing_funcs,
                 arm9_slots)
    # Data the data header does not bind and nothing defines has no
    # storage: the link would fail on it, so the generator does, by name.
    unbound = sorted((canonical[a], a) for a in data_names
                     if canonical[a] not in defined and canonical[a] not in pinned)
    unbound += sorted((n, None) for n in data_noaddr if n not in defined)

    report = ["translation units: %d matched, %d port" % (len(matched), len(port)),
              "renamed spellings: %d" % sum(len(u.renames) for u in units),
              "implicit declarations supplied: %d in %d files"
              % (sum(len(u.protos) for u in units), sum(1 for u in units if u.protos)),
              "link constants: %d (%d pinned DTCM objects)" % (len(consts), len(pinned)),
              "data names bound by the data header: %d" % len(data_bound),
              "data objects with no storage: %d" % len(unbound),
              "functions stubbed: %d open, %d asm-pending"
              % (sum(1 for v in missing_funcs.values() if v[1].startswith("open")),
                 sum(1 for v in missing_funcs.values() if not v[1].startswith("open"))),
              "mod slots: %d arm9 emitted, %d declared rows skipped"
              % (len(arm9_slots), len(slot_skipped))]
    for name, a, b in dups:
        report.append("DUPLICATE %s: %s and %s" % (name, a, b))
    report.append("")
    report.append("mod slots (a generated indirection per declared ARM9 address):")
    for addr in sorted(arm9_slots):
        report.append("  %-36s 0x%08X -> %s" % (
            arm9_slots[addr]["declared"], addr, arm9_slots[addr]["original"]))
    for dname, addr, why in slot_skipped:
        report.append("  SKIPPED %-28s 0x%08X %s" % (dname, addr, why))
    report.append("")
    report.append("implicit declarations:")
    for unit in units:
        for name in sorted(unit.protos):
            report.append("  %s: %s %s()" % (unit.rel or unit.path, unit.protos[name], name))
    report.append("")
    report.append("functions stubbed:")
    for name in sorted(missing_funcs):
        addr, why = missing_funcs[name]
        report.append("  %-36s %-10s %s" % (name, "0x%08X" % addr if addr is not None else "-", why))
    report.append("")
    report.append("data objects with no storage (the data header binds no such name):")
    for name, addr in unbound:
        report.append("  %-36s %s" % (name, "0x%08X" % addr if addr is not None else "-"))
    write_if_changed(os.path.join(out, "boot_link_report.txt"), "\n".join(report) + "\n")
    print("\n".join(report[:8]))
    for name, addr in unbound:
        print("bootlink.py: no storage for data %s (%s)"
              % (name, "0x%08X" % addr if addr is not None else "no address"), file=sys.stderr)
    for dname, addr, why in slot_skipped:
        print("bootlink.py: no slot for %s (0x%08X): %s" % (dname, addr, why),
              file=sys.stderr)
    return 1 if dups or unbound else 0


ARMDATA_REF = re.compile(r"^#define\s+([A-Za-z_]\w*)\s+\(\*bw_dsref_\1\)", re.M)
ARMDATA_OBJ = re.compile(r"^/\*\s+([A-Za-z_]\w*)\s+is a host object", re.M)


def read_armdata_header(path):
    """The names tools/armdata.py's header binds, its host objects, and the
    header's #define line for each bound name."""
    if not path:
        return set(), set(), {}
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    objects = set(ARMDATA_OBJ.findall(text))
    lines = {}
    for line in text.splitlines():
        m = ARMDATA_REF.match(line)
        if m:
            lines[m.group(1)] = line
    return set(lines) | objects, objects, lines


IDENT = re.compile(r"[A-Za-z_]\w*")
DEFINE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)", re.M)


def unit_identifiers(path):
    """Every identifier in a source, and the names it #defines."""
    with open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    return set(IDENT.findall(text)), set(DEFINE.findall(text))


LINK_C_HEAD = """\
/*
 * boot_link.c -- generated by tools/bootlink.py; do not edit.
 *
 * Link-time constants and the functions nothing defines yet, for the
 * game's own boot (docs/boot.md, "Linking the boot").
 *
 * bw_lc_NAME holds the value the DS linker gives NAME's *address*; each
 * translation unit that names NAME reads it as (*bw_lc_NAME) through its
 * generated header.  The matched files declare NAME with several types
 * (char[], u32[], a function); the object is a pointer in every one of
 * them, so its size and representation agree on every target.
 *
 * A value inside the DS address space is a host pointer, set by
 * bw_boot_link_init after bw_ds_reset: SDK_AUTOLOAD_DTCM_START is
 * bw_ds_ptr(0x02FE0000), the same bytes sdk/os_interrupt.c reaches as
 * BW_DS_INTR_CHECK_BUF - 0x3FF8.
 *
 * Every function below the constants is a STUB: it panics with its DS
 * address when called.  "open" is game code aletheia has not matched;
 * "asm body" is an SDK file written as mwcc assembly, which sdk/asm/
 * replaces (bootlink.py plan picks a replacement up by file name).
 */

#include <stdint.h>

#include "dsmem.h"
#include "port.h"
#include "mod_slot.h"
#include "mod_hooks.h"

void bw_boot_link_init(void);
"""


def write_link_c(path, consts, pinned, views, missing_funcs, slots=None):
    slots = slots or {}
    lines = [LINK_C_HEAD]
    lines.append("/* ---- link-time constants ---- */\n")
    runtime = []               # (name, initial value), set at start-up
    for name in sorted(set(consts) | set(pinned)):
        value = consts.get(name, pinned.get(name))
        if DS_LO <= value < DS_HI:
            lines.append("void *bw_lc_%s;   /* DS 0x%08X */" % (name, value))
            runtime.append((name, "bw_ds_ptr(0x%08Xu)" % value))
        else:
            lines.append("void *bw_lc_%s = (void *)(uintptr_t)0x%08Xu;" % (name, value))
    lines.append("")
    lines.append("/* ---- second views: one file, two spellings of one object ---- */\n")
    for name in sorted(views):
        target = views[name]
        if target in pinned:
            lines.append("void *bw_lc_%s;   /* = bw_lc_%s */" % (name, target))
            runtime.append((name, "bw_lc_%s" % target))
        else:
            lines.append("extern char %s[];" % target)
            lines.append("void *bw_lc_%s = (void *)%s;" % (name, target))
    lines.append("")
    if slots:
        lines.append("/* ---- mod slots: the originals the slots call ---- */\n")
        for addr in sorted(slots):
            ret, params = slots[addr]["sig"]
            lines.append(ovlink.proto_decl(ret, slots[addr]["original"], params))
        lines.append("")
    lines.append("void bw_boot_link_init(void)\n{")
    # Addresses first: a second view of a pinned object copies its pointer.
    for name, value in sorted(runtime, key=lambda r: r[1].startswith("bw_lc_")):
        lines.append("    bw_lc_%s = %s;" % (name, value))
    # Publish each declared original so a wrap override can call it.
    for addr in sorted(slots):
        lines.append("    bw_mod_slot_publish(BW_MOD_REGION_ARM9, 0x%08Xu, (void *)%s);"
                     % (addr, slots[addr]["original"]))
    lines.append("}\n")
    if slots:
        lines.append("/* ---- mod slots: one indirection per declared ARM9 address ---- */\n")
        for addr in sorted(slots):
            lines.append(slot_function("BW_MOD_REGION_ARM9", addr, slots[addr]))
    lines.append("/* ---- STUBS: functions nothing defines yet ---- */\n")
    for name in sorted(missing_funcs):
        addr, why = missing_funcs[name]
        lines.append("void %s(void);" % name)
        lines.append("void %s(void)\n{\n    bw_panic(\"boot stub reached (%s)\", 0x%08Xu, \"%s\");\n}\n"
                     % (name, why.replace("\\", "/"), addr or 0, name))
    write_if_changed(path, "\n".join(lines))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    sub = ap.add_subparsers(dest="mode", required=True)
    p = sub.add_parser("plan")
    p.add_argument("--aletheia-root", required=True)
    p.add_argument("--seed", action="append", default=[])
    p.add_argument("--seed-file", action="append", default=[],
                   help="file listing seeds, one per line (repeatable)")
    p.add_argument("--exclude-file", required=True)
    p.add_argument("--shim-sources", action="append", required=True,
                   help="file listing port sources (repeatable)")
    p.add_argument("--library-matched")
    p.add_argument("--asm-dir")
    p.add_argument("--port-sources",
                   help="file listing the boot's port sources, whose calls seed the walk")
    p.add_argument("--define", action="append", default=[],
                   help="NAME=VALUE a port #if depends on (repeatable)")
    p.add_argument("--out-dir", required=True)
    g = sub.add_parser("generate")
    g.add_argument("--aletheia-root", required=True)
    g.add_argument("--plan-dir", required=True)
    g.add_argument("--shim-sources", required=True)
    g.add_argument("--port-sources", required=True)
    g.add_argument("--armdata-header",
                   help="tools/armdata.py's forced-include header: the data names it binds")
    g.add_argument("--define", action="append", default=[],
                   help="NAME=VALUE a port #if depends on (repeatable)")
    g.add_argument("--out-dir", required=True)
    args = ap.parse_args(argv)
    closure.ROOT = os.path.abspath(args.aletheia_root)
    for item in getattr(args, "define", []):
        name, _, value = item.partition("=")
        PORT_DEFINES[name] = value
    return plan(args) if args.mode == "plan" else generate(args)


if __name__ == "__main__":
    sys.exit(main())
