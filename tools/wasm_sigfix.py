#!/usr/bin/env python3
"""wasm_sigfix.py -- make wasm-ld's signature-mismatch stubs call the definition.

    wasm_sigfix.py IN.wasm -o OUT.wasm

aletheia's matched files declare each extern with the prototype their call
site needed, so one function is often declared differently in different
files (a void function declared as returning int, a call with one argument
more than the definition takes).  On the DS that is harmless: arguments go
in registers and an unused return register is never read.  A wasm call is
type-checked, so wasm-ld routes every call whose type differs from the
definition's to a stub named "signature_mismatch:<name>" that traps.

This pass gives each such stub a body with the DS meaning: call <name> with
the caller's arguments, extra ones dropped and missing ones 0, an integer
of the other width wrapped or zero-extended, and the result discarded or 0
when the two sides disagree on it.  The module needs its name section
(wasm-ld keeps it unless told to strip).  Standard library only.
"""

import argparse
import sys

I32, I64, F32, F64 = 0x7F, 0x7E, 0x7D, 0x7C
CONST0 = {I32: b"\x41\x00", I64: b"\x42\x00",
          F32: b"\x43" + bytes(4), F64: b"\x44" + bytes(8)}
# value conversions (from, to) -> opcodes; floats go through the integer
# of their width (bit patterns, as in a DS register)
CONV = {(I64, I32): b"\xA7", (I32, I64): b"\xAD",
        (F32, I32): b"\xBC", (I32, F32): b"\xBE",
        (F64, I64): b"\xBD", (I64, F64): b"\xBF"}
INT_OF = {I32: I32, I64: I64, F32: I32, F64: I64}


def leb(b, i):
    v = shift = 0
    while True:
        c = b[i]
        i += 1
        v |= (c & 0x7F) << shift
        shift += 7
        if not c & 0x80:
            return v, i


def uleb(v):
    out = bytearray()
    while True:
        c = v & 0x7F
        v >>= 7
        if v:
            out.append(c | 0x80)
        else:
            out.append(c)
            return bytes(out)


def name(b, i):
    n, i = leb(b, i)
    return b[i:i + n].decode("utf-8"), i + n


def convert(src, dst):
    if src == dst:
        return b""
    if (src, dst) in CONV:
        return CONV[(src, dst)]
    # through the integers: src -> int(src) -> int(dst) -> dst
    return (convert(src, INT_OF[src]) + convert(INT_OF[src], INT_OF[dst])
            + convert(INT_OF[dst], dst))


def sections(b):
    out = []
    i = 8
    while i < len(b):
        sid = b[i]
        size, j = leb(b, i + 1)
        out.append((sid, j, j + size, i))
        i = j + size
    return out


def parse(b):
    types, func_types, names, code = [], [], {}, None
    n_imported = 0
    for sid, s, e, _ in sections(b):
        if sid == 1:
            n, k = leb(b, s)
            for _ in range(n):
                assert b[k] == 0x60, "only function types"
                np, k = leb(b, k + 1)
                params = list(b[k:k + np])
                k += np
                nr, k = leb(b, k)
                results = list(b[k:k + nr])
                k += nr
                types.append((params, results))
        elif sid == 2:
            n, k = leb(b, s)
            for _ in range(n):
                _, k = name(b, k)
                _, k = name(b, k)
                kind = b[k]
                k += 1
                if kind == 0:
                    t, k = leb(b, k)
                    func_types.append(t)
                    n_imported += 1
                elif kind == 1:
                    k += 1
                    flags, k = leb(b, k)
                    _, k = leb(b, k)
                    if flags & 1:
                        _, k = leb(b, k)
                elif kind == 2:
                    flags, k = leb(b, k)
                    _, k = leb(b, k)
                    if flags & 1:
                        _, k = leb(b, k)
                elif kind == 3:
                    k += 2
                else:
                    sys.exit("wasm_sigfix: unknown import kind %d" % kind)
        elif sid == 3:
            n, k = leb(b, s)
            for _ in range(n):
                t, k = leb(b, k)
                func_types.append(t)
        elif sid == 10:
            code = (s, e)
        elif sid == 0:
            sec_name, k = name(b, s)
            if sec_name != "name":
                continue
            while k < e:
                sub = b[k]
                size, k = leb(b, k + 1)
                if sub == 1:
                    n, m = leb(b, k)
                    for _ in range(n):
                        idx, m = leb(b, m)
                        nm, m = name(b, m)
                        names[idx] = nm
                k += size
    return types, func_types, n_imported, names, code


def stub_body(caller, callee, callee_idx):
    cp, cr = caller
    dp, dr = callee
    if len(cr) > 1 or len(dr) > 1:
        return None
    body = bytearray(b"\x00")               # no locals
    for k, t in enumerate(dp):
        if k < len(cp):
            body += b"\x20" + uleb(k) + convert(cp[k], t)
        else:
            body += CONST0[t]
    body += b"\x10" + uleb(callee_idx)
    if dr and not cr:
        body += b"\x1A"
    elif cr and not dr:
        body += CONST0[cr[0]]
    elif cr:
        body += convert(dr[0], cr[0])
    body += b"\x0B"
    return bytes(body)


def fix(b):
    types, func_types, n_imported, names, code = parse(b)
    if not names:
        sys.exit("wasm_sigfix: the module has no name section")
    if code is None:
        return b, 0, []
    # The name section also names file-local functions, so one name can
    # have several definitions.  A stub calls the definition of its own
    # type when there is one, and the only definition otherwise; with
    # several of other types it is left trapping (reported).
    by_name = {}
    for idx, nm in names.items():
        if idx >= n_imported:
            by_name.setdefault(nm, []).append(idx)
    prefix = "signature_mismatch:"
    s, e = code
    n, k = leb(b, s)
    bodies = []
    for _ in range(n):
        size, k2 = leb(b, k)
        bodies.append(b[k2:k2 + size])
        k = k2 + size
    fixed, failed = 0, []
    for idx, nm in names.items():
        if not nm.startswith(prefix) or idx < n_imported:
            continue
        cands = by_name.get(nm[len(prefix):], [])
        same = [c for c in cands if types[func_types[c]] == types[func_types[idx]]]
        target = same[0] if len(same) == 1 else cands[0] if len(cands) == 1 else None
        if target is None:
            failed.append(nm)
            continue
        body = stub_body(types[func_types[idx]], types[func_types[target]], target)
        if body is None:
            failed.append(nm)
            continue
        bodies[idx - n_imported] = body
        fixed += 1
    sec = bytearray(uleb(len(bodies)))
    for body in bodies:
        sec += uleb(len(body)) + body
    # code is a known section; rebuild it in place of the old one
    for sid, ss, se, start in sections(b):
        if sid == 10:
            out = b[:start] + bytes([10]) + uleb(len(sec)) + bytes(sec) + b[se:]
            return out, fixed, failed
    return b, 0, failed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("module")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    b = open(a.module, "rb").read()
    if b[:4] != b"\0asm":
        sys.exit("wasm_sigfix: %s is not a wasm module" % a.module)
    out, fixed, failed = fix(b)
    with open(a.out, "wb") as f:
        f.write(out)
    print("wasm_sigfix: %d signature-mismatch stubs now call their definition" % fixed)
    for nm in failed:
        print("wasm_sigfix: left trapping: %s" % nm, file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
