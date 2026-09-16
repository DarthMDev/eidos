#!/usr/bin/env python3
"""
tools/fetch_sdl.py -- put SDL2 under third_party/ without an installer.

eidos needs SDL2 only for the platform layer (engine/platform/): the window,
the keyboard, the audio device and the frame pacer.  Everything else in this
repository, the engine library and every test included, builds and runs
without it, so SDL2 is an optional dependency and this script is optional
too.

What it does, with nothing but the standard library:

  1. downloads the official MinGW-w64 development package from the
     libsdl-org GitHub release named below,
  2. verifies its SHA-256 against the value recorded here,
  3. extracts it into third_party/.

third_party/ is gitignored: the archive and the tree it unpacks to are
never committed.  A fresh clone runs

    python tools/fetch_sdl.py

and then configures as usual; CMakeLists.txt looks in third_party/ first and
falls back to a system SDL2 (pkg-config, sdl2-config or an SDL2 CMake
package) when one is installed.

The pinned release is the latest SDL 2.x at the time it was pinned.  SDL 3 is
deliberately not used: SDL2 is the version every platform this port targets
ships today, and the platform layer uses nothing SDL3 adds.

Upgrading is two edits and a re-run: change SDL_VERSION, put the new
SHA-256 in SDL_SHA256 (print it with --print-sha after downloading), and run
the script again.  The hash is not fetched from the network on purpose: a
recorded hash is the only part of this that a compromised download cannot
satisfy.

Usage:

    python tools/fetch_sdl.py                 # download, verify, extract
    python tools/fetch_sdl.py --check         # verify an existing tree only
    python tools/fetch_sdl.py --force         # re-download and re-extract
    python tools/fetch_sdl.py --print-sha     # hash the archive and stop
"""

import argparse
import hashlib
import os
import shutil
import sys
import tarfile
import urllib.request

SDL_VERSION = "2.32.10"
SDL_ARCHIVE = "SDL2-devel-%s-mingw.tar.gz" % SDL_VERSION
SDL_URL = ("https://github.com/libsdl-org/SDL/releases/download/release-%s/%s"
           % (SDL_VERSION, SDL_ARCHIVE))
SDL_SHA256 = "83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2"

# The directory the tarball unpacks to, and the per-triplet sub-tree the
# CMake glue looks for inside it.
SDL_DIR = "SDL2-%s" % SDL_VERSION
SDL_TRIPLETS = ("x86_64-w64-mingw32", "i686-w64-mingw32")

CHUNK = 1 << 16


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def third_party(root):
    return os.path.join(root, "third_party")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fp:
        while True:
            block = fp.read(CHUNK)
            if not block:
                break
            h.update(block)
    return h.hexdigest()


def download(url, dest):
    print("downloading %s" % url)
    tmp = dest + ".part"
    with urllib.request.urlopen(url, timeout=300) as src:
        total = src.headers.get("Content-Length")
        total = int(total) if total else 0
        got = 0
        with open(tmp, "wb") as out:
            while True:
                block = src.read(CHUNK)
                if not block:
                    break
                out.write(block)
                got += len(block)
                if total:
                    sys.stdout.write("\r  %d / %d bytes" % (got, total))
                    sys.stdout.flush()
    if total:
        sys.stdout.write("\n")
    os.replace(tmp, dest)


def extract(archive, into, name):
    target = os.path.join(into, name)
    if os.path.isdir(target):
        shutil.rmtree(target)
    print("extracting %s" % name)
    with tarfile.open(archive, "r:gz") as tar:
        for member in tar.getmembers():
            # Refuse anything that would escape the destination directory.
            path = os.path.normpath(member.name)
            if path.startswith("..") or os.path.isabs(path):
                raise SystemExit("refusing path in archive: %s" % member.name)
        tar.extractall(into)
    if not os.path.isdir(target):
        raise SystemExit("archive did not contain %s" % name)
    return target


def describe(target):
    found = []
    for triplet in SDL_TRIPLETS:
        inc = os.path.join(target, triplet, "include", "SDL2", "SDL.h")
        lib = os.path.join(target, triplet, "lib", "libSDL2.dll.a")
        if os.path.isfile(inc) and os.path.isfile(lib):
            found.append(triplet)
    return found


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="verify what is already there, download nothing")
    ap.add_argument("--force", action="store_true",
                    help="re-download and re-extract even if present")
    ap.add_argument("--print-sha", action="store_true",
                    help="print the archive's SHA-256 and stop")
    args = ap.parse_args(argv)

    root = repo_root()
    tp = third_party(root)
    os.makedirs(tp, exist_ok=True)
    archive = os.path.join(tp, SDL_ARCHIVE)
    target = os.path.join(tp, SDL_DIR)

    if args.check:
        found = describe(target)
        if not found:
            print("SDL2 %s is not unpacked in %s" % (SDL_VERSION, tp))
            return 1
        print("SDL2 %s present: %s" % (SDL_VERSION, ", ".join(found)))
        return 0

    if args.force and os.path.isfile(archive):
        os.remove(archive)

    if not os.path.isfile(archive):
        download(SDL_URL, archive)

    digest = sha256_file(archive)
    if args.print_sha:
        print(digest)
        return 0
    if digest != SDL_SHA256:
        os.remove(archive)
        print("SHA-256 mismatch for %s" % SDL_ARCHIVE, file=sys.stderr)
        print("  expected %s" % SDL_SHA256, file=sys.stderr)
        print("  got      %s" % digest, file=sys.stderr)
        print("the download was deleted; re-run to try again", file=sys.stderr)
        return 1
    print("sha256 ok: %s" % digest)

    if args.force or not describe(target):
        extract(archive, tp, SDL_DIR)

    found = describe(target)
    if not found:
        print("extraction produced no usable triplet tree", file=sys.stderr)
        return 1
    print("SDL2 %s ready in %s (%s)" % (SDL_VERSION, target, ", ".join(found)))
    print("configure with -DEIDOS_SDL=ON (the default when this tree exists)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
