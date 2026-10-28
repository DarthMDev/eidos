#!/usr/bin/env python3
"""wasm_imports.py -- the names a WebAssembly module imports from one module.

    wasm_imports.py MODULE.wasm IMPORT_MODULE [PREFIX] -o OUT.txt

Writes one name a line: the function imports of IMPORT_MODULE whose names
start with PREFIX (the host program's OpenGL list: `env gl`).  Reads the
binary's import section itself; the standard library only.
"""

import argparse
import sys


def leb(b, i):
    v = shift = 0
    while True:
        c = b[i]
        i += 1
        v |= (c & 0x7F) << shift
        shift += 7
        if not c & 0x80:
            return v, i


def name(b, i):
    n, i = leb(b, i)
    return b[i:i + n].decode('utf-8'), i + n


def imports(path):
    b = open(path, 'rb').read()
    if b[:4] != b'\0asm':
        sys.exit('wasm_imports: %s is not a wasm module' % path)
    i = 8
    while i < len(b):
        sid = b[i]
        size, j = leb(b, i + 1)
        if sid == 2:
            count, k = leb(b, j)
            for _ in range(count):
                mod, k = name(b, k)
                field, k = name(b, k)
                kind = b[k]
                k += 1
                if kind == 0:               # function: a type index
                    _, k = leb(b, k)
                    yield mod, field
                elif kind == 1:             # table: reftype + limits
                    k += 1
                    flags, k = leb(b, k)
                    _, k = leb(b, k)
                    if flags & 1:
                        _, k = leb(b, k)
                elif kind == 2:             # memory: limits
                    flags, k = leb(b, k)
                    _, k = leb(b, k)
                    if flags & 1:
                        _, k = leb(b, k)
                elif kind == 3:             # global: valtype + mutability
                    k += 2
                else:
                    sys.exit('wasm_imports: unknown import kind %d' % kind)
            return
        i = j + size


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('module')
    ap.add_argument('import_module')
    ap.add_argument('prefix', nargs='?', default='')
    ap.add_argument('-o', '--out', required=True)
    a = ap.parse_args()
    names = sorted(f for m, f in imports(a.module) if m == a.import_module and f.startswith(a.prefix))
    with open(a.out, 'w', newline='\n') as f:
        f.write(''.join(n + '\n' for n in names))
    return 0


if __name__ == '__main__':
    sys.exit(main())
