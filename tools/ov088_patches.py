#!/usr/bin/env python3
"""
tools/ov088_patches.py -- the title menu's Settings row for the src/game tree.

The title menu's port changes (the Settings row, docs/hooks.md
"title_settings") are written as stubs/ov/reimpl_ov088_func_*.c, which take
the place of src/matched files.  A build of src/game has no such files: each
reimpl "replaces nothing" and the Settings row is gone.  This tool writes the
same code as stubs/ov/patch_ov088_<name>.c, a patch of
src/game/ov088/startmenu.c (tools/ovlink.py, "patches:"), with the tree's
names: the functions from ALETHEIA_ROOT/layout/function_addrs.tsv, and the two
data tables src/game names.  Run it after a change to a reimpl.

usage: ov088_patches.py ALETHEIA_ROOT
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STUBS = os.path.join(HERE, 'stubs', 'ov')

# The data src/game names; the others keep their d_ spellings there.
DATA = {'d_021BD5EC': 'gStartMenuRowInfo', 'd_021BD7C4': 'gStartMenuWindowTemplates'}


def main(argv):
    if len(argv) != 2:
        sys.exit(__doc__)
    rows = {}
    tsv = os.path.join(argv[1], 'layout', 'function_addrs.tsv')
    for line in open(tsv, encoding='utf-8'):
        f = line.rstrip('\n').split('\t')
        if len(f) < 7 or f[0] == 'region':
            continue
        rows.setdefault((f[0], int(f[1], 16)), f[2])

    def game_name(tok):
        kind, hexa = tok.split('_')
        if tok in DATA:
            return DATA[tok]
        if kind not in ('func', 'sub'):
            return None
        a = int(hexa, 16)
        region = 'ov088' if 0x021B0000 <= a < 0x02200000 else 'arm9'
        return rows.get((region, a))

    for fname in sorted(os.listdir(STUBS)):
        if not fname.startswith('reimpl_ov088_'):
            continue
        text = open(os.path.join(STUBS, fname), encoding='utf-8').read()
        own_name = game_name(re.search(r'^ \* function: (func_[0-9A-F]{8})',
                                       text, re.M).group(1))
        body = text
        for tok in sorted(set(re.findall(r'\b(?:func|sub|d)_[0-9A-F]{8}\b', text)),
                          key=len, reverse=True):
            g = game_name(tok)
            if g:
                body = re.sub(r'\b%s\b' % tok, g, body)
        body = re.sub(r'^ \* replaces: \S+\n',
                      ' * patches: src/game/ov088/startmenu.c %s\n'
                      ' *\n'
                      ' * The src/game form of stubs/ov/%s (the same code, the\n'
                      ' * tree\'s names): the title menu\'s Settings row (docs/hooks.md,\n'
                      ' * "title_settings").  Written by tools/ov088_patches.py from\n'
                      ' * that file; a change goes into the reimpl, then this is\n'
                      ' * written again.\n' % (own_name, fname),
                      body, count=1, flags=re.M)
        out = os.path.join(STUBS, 'patch_ov088_%s.c' % own_name)
        open(out, 'w', encoding='utf-8', newline='\n').write(body)
        print(fname, '->', os.path.basename(out))


if __name__ == '__main__':
    main(sys.argv)
