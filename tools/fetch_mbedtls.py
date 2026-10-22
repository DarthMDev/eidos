#!/usr/bin/env python3
"""
tools/fetch_mbedtls.py -- the TLS library the relay connection uses
(Mbed TLS), from the official release, into a cache outside the
repository.

eidos talks to a multiplayer relay over TLS when the relay's address says
so (`tls://name`, docs/multiplayer.md, "TLS"; metaxy docs/SPEC.md section
2).  The library is optional: without it the port builds and runs, and a
TLS relay is refused with a message.  CMake finds it in the cache this
script fills (EIDOS_MBEDTLS_ROOT names another place).

One pinned release, verified against the SHA-256 below (the release's own
sha256sum file states the same number):

  mbedtls-3.6.7.tar.bz2   Mbed TLS 3.6, the long-term support branch

The unpacked tree is the release's, unchanged.

Where things go.  Nothing is written into the repository.  The cache is,
in order: --cache, the EIDOS_MBEDTLS_CACHE environment variable, then the
platform's per-user cache directory:

  Windows   %LOCALAPPDATA%\\eidos\\mbedtls
  macOS     ~/Library/Caches/eidos/mbedtls
  other     $XDG_CACHE_HOME/eidos/mbedtls, or ~/.cache/eidos/mbedtls

Inside it:

  downloads/mbedtls-3.6.7.tar.bz2   the verified archive
  mbedtls-3.6.7/                    the unpacked source

The last line the script prints is the source folder, for
EIDOS_MBEDTLS_ROOT.

usage: fetch_mbedtls.py [--cache DIR]
"""

import hashlib
import os
import sys
import tarfile
import urllib.request

VERSION = '3.6.7'
ARCHIVE = 'mbedtls-%s.tar.bz2' % VERSION
URL = ('https://github.com/Mbed-TLS/mbedtls/releases/download/'
       'mbedtls-%s/%s' % (VERSION, ARCHIVE))
SHA256 = 'a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6'
FOLDER = 'mbedtls-%s' % VERSION


def cache_dir(argv):
    if '--cache' in argv:
        return argv[argv.index('--cache') + 1]
    env = os.environ.get('EIDOS_MBEDTLS_CACHE')
    if env:
        return env
    if os.name == 'nt':
        base = os.environ.get('LOCALAPPDATA') or os.path.expanduser('~')
        return os.path.join(base, 'eidos', 'mbedtls')
    if sys.platform == 'darwin':
        return os.path.expanduser('~/Library/Caches/eidos/mbedtls')
    base = os.environ.get('XDG_CACHE_HOME') or os.path.expanduser('~/.cache')
    return os.path.join(base, 'eidos', 'mbedtls')


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def main(argv):
    cache = cache_dir(argv)
    downloads = os.path.join(cache, 'downloads')
    os.makedirs(downloads, exist_ok=True)
    archive = os.path.join(downloads, ARCHIVE)
    if not os.path.exists(archive) or sha256(archive) != SHA256:
        print('fetch_mbedtls: downloading', URL)
        tmp = archive + '.part'
        urllib.request.urlretrieve(URL, tmp)
        got = sha256(tmp)
        if got != SHA256:
            os.remove(tmp)
            sys.exit('fetch_mbedtls: %s has SHA-256 %s, expected %s' % (ARCHIVE, got, SHA256))
        os.replace(tmp, archive)
    root = os.path.join(cache, FOLDER)
    if not os.path.isdir(root):
        print('fetch_mbedtls: unpacking', ARCHIVE)
        with tarfile.open(archive) as t:
            for member in t.getmembers():
                if member.name.startswith('/') or '..' in member.name.split('/'):
                    sys.exit('fetch_mbedtls: unsafe path in the archive: %s' % member.name)
            t.extractall(cache)
    print(root.replace('\\', '/'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
