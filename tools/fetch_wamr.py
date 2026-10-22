#!/usr/bin/env python3
"""
tools/fetch_wamr.py -- the WebAssembly runtime code mods run in (WAMR,
the WebAssembly Micro Runtime), from the official release, into a cache
outside the repository.

eidos runs code mods built for WebAssembly (docs/mod-code-wasm.md) in WAMR's
interpreter.  The runtime is optional: without it the port builds and runs,
and a mod whose code is a wasm module is refused with a message.  CMake
finds the runtime in the cache this script fills (EIDOS_WAMR_ROOT names
another place).

One pinned release, verified against the SHA-256 below:

  WAMR-2.2.0.tar.gz   the source of the release tag WAMR-2.2.0

Three small changes are made to the unpacked source.  Two let it build
with MinGW (GCC for Windows), which the release supports only with MSVC:
the Windows platform header does not define ssize_t again when the C
library already does, and win_clock.c does not declare
NtQueryTimerResolution again when MinGW's own headers do.  The third keeps
a module's memory as the module declared it: WAMR's loader shrinks the
memory of a module that has no memory.size or memory.grow opcode to its
data and fixes its maximum there, and the port grows every module's memory
by one page after instantiation for its payload copies (sdk/mod_wasm.c).
Each change is idempotent; the unpacked tree is otherwise the release's.

Where things go.  Nothing is written into the repository.  The cache is,
in order: --cache, the EIDOS_WAMR_CACHE environment variable, then the
platform's per-user cache directory:

  Windows   %LOCALAPPDATA%\\eidos\\wamr
  macOS     ~/Library/Caches/eidos/wamr
  other     $XDG_CACHE_HOME/eidos/wamr, or ~/.cache/eidos/wamr

Inside it:

  downloads/WAMR-2.2.0.tar.gz            the verified archive
  wasm-micro-runtime-WAMR-2.2.0/         the unpacked, patched source

The last line the script prints is the source folder, for EIDOS_WAMR_ROOT.

usage: fetch_wamr.py [--cache DIR]
"""

import hashlib
import os
import sys
import tarfile
import urllib.request

VERSION = '2.2.0'
ARCHIVE = 'WAMR-%s.tar.gz' % VERSION
URL = ('https://github.com/bytecodealliance/wasm-micro-runtime/archive/refs/'
       'tags/WAMR-%s.tar.gz' % VERSION)
SHA256 = '93b6ba03f681e061967106046b1908631ee705312b9a6410f3baee7af7c6aac9'
FOLDER = 'wasm-micro-runtime-WAMR-%s' % VERSION


def cache_dir(argv):
    if '--cache' in argv:
        return argv[argv.index('--cache') + 1]
    env = os.environ.get('EIDOS_WAMR_CACHE')
    if env:
        return env
    if os.name == 'nt':
        base = os.environ.get('LOCALAPPDATA') or os.path.expanduser('~')
        return os.path.join(base, 'eidos', 'wamr')
    if sys.platform == 'darwin':
        return os.path.expanduser('~/Library/Caches/eidos/wamr')
    base = os.environ.get('XDG_CACHE_HOME') or os.path.expanduser('~/.cache')
    return os.path.join(base, 'eidos', 'wamr')


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def replace_once(path, old, new, marker):
    """Apply one source change unless it is there already."""
    with open(path, encoding='utf-8', newline='') as f:
        text = f.read()
    if marker in text:
        return
    if old not in text:
        sys.exit('fetch_wamr: %s does not hold the expected text' % path)
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(text.replace(old, new, 1))


def patch(root):
    plat = os.path.join(root, 'core', 'shared', 'platform', 'windows')
    replace_once(os.path.join(plat, 'platform_internal.h'),
                 'typedef SSIZE_T ssize_t;',
                 '#ifndef __MINGW32__\ntypedef SSIZE_T ssize_t;\n#endif',
                 '#ifndef __MINGW32__\ntypedef SSIZE_T ssize_t;')
    replace_once(os.path.join(plat, 'win_clock.c'),
                 'extern NTSTATUS\nNtQueryTimerResolution(PULONG MinimumResolution, '
                 'PULONG MaximumResolution,\n                       PULONG '
                 'CurrentResolution);',
                 '#ifndef __MINGW32__\nextern NTSTATUS\nNtQueryTimerResolution('
                 'PULONG MinimumResolution, PULONG MaximumResolution,\n'
                 '                       PULONG CurrentResolution);\n#endif',
                 '#ifndef __MINGW32__\nextern NTSTATUS')
    replace_once(os.path.join(root, 'core', 'iwasm', 'interpreter',
                              'wasm_loader.c'),
                 '    if (!module->possible_memory_grow) {\n'
                 '        WASMMemoryImport *memory_import;',
                 '    /* eidos: the port grows every module\'s memory by one page\n'
                 '     * after instantiation (sdk/mod_wasm.c), so the memory keeps\n'
                 '     * the size, page size and maximum the module declared. */\n'
                 '    if (0 && !module->possible_memory_grow) {\n'
                 '        WASMMemoryImport *memory_import;',
                 '/* eidos: the port grows every module')


def main(argv):
    cache = cache_dir(argv)
    downloads = os.path.join(cache, 'downloads')
    os.makedirs(downloads, exist_ok=True)
    archive = os.path.join(downloads, ARCHIVE)
    if not os.path.exists(archive) or sha256(archive) != SHA256:
        print('fetch_wamr: downloading', URL)
        tmp = archive + '.part'
        urllib.request.urlretrieve(URL, tmp)
        got = sha256(tmp)
        if got != SHA256:
            os.remove(tmp)
            sys.exit('fetch_wamr: %s has SHA-256 %s, expected %s' % (ARCHIVE, got, SHA256))
        os.replace(tmp, archive)
    root = os.path.join(cache, FOLDER)
    if not os.path.isdir(root):
        print('fetch_wamr: unpacking', ARCHIVE)
        with tarfile.open(archive) as t:
            for member in t.getmembers():
                if member.name.startswith('/') or '..' in member.name.split('/'):
                    sys.exit('fetch_wamr: unsafe path in the archive: %s' % member.name)
            t.extractall(cache)
    patch(root)
    print(root.replace('\\', '/'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
