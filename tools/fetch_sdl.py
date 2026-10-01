#!/usr/bin/env python3
"""
tools/fetch_sdl.py -- SDL2 for every toolchain, from the official release,
into a cache outside the repository.

eidos needs SDL2 only for the platform layer (engine/platform/): the window,
the keyboard, the audio device and the frame pacer.  Everything else in this
repository, the engine library and every test but test_view, builds and runs
without it, so SDL2 is an optional dependency and this script is optional
too.  On Linux and macOS the system package is the normal way to get it
(README.md, "SDL2 on every platform"); this script is for the toolchains that
have no package manager behind them, and for any host as a last resort.

Three flavours of the same pinned release, all from the libsdl-org GitHub
release page, each verified against a SHA-256 recorded below:

  mingw   SDL2-devel-<ver>-mingw.tar.gz: headers, import libraries and
          SDL2.dll for x86_64-w64-mingw32 and i686-w64-mingw32.
  vc      SDL2-devel-<ver>-VC.zip: headers, SDL2.lib and SDL2.dll for x64
          and x86.  This is what cl.exe and clang-cl link against; the MinGW
          import library does not work with link.exe.
  source  SDL2-<ver>.tar.gz, configured, built and installed with CMake
          into a prefix named after the compiler that asked for it.  The
          fallback for a host with no system package and no prebuilt
          flavour (Linux without libsdl2-dev, an ILP32 build on a 64-bit
          distribution, a toolchain the other two do not cover).

Where things go.  Nothing is written into the repository.  The cache is,
in order: --cache, the EIDOS_SDL_CACHE environment variable, then the
platform's per-user cache directory:

  Windows   %LOCALAPPDATA%\\eidos\\sdl
  macOS     ~/Library/Caches/eidos/sdl
  other     $XDG_CACHE_HOME/eidos/sdl, or ~/.cache/eidos/sdl

so every checkout and every git worktree on the machine shares one download.
Inside it:

  downloads/<archive>            the verified archive
  SDL2-<ver>-mingw/              the unpacked mingw package
  SDL2-<ver>-vc/                 the unpacked VC package
  SDL2-<ver>-src/                the unpacked source
  SDL2-<ver>-<tag>/              a source build's install prefix

An unpacked tree is used only if it carries the stamp file this script writes
after a verified, complete extraction (.eidos-sha256, holding the archive's
hash), so an interrupted extraction or a tree from another version is never
mistaken for a good one.

Why the hashes are recorded here.  A hash fetched from the same server as
the archive proves nothing; a hash in the repository is the one part of
this a compromised download cannot satisfy.  The values below match the
asset digests GitHub publishes for release-2.32.10.  Upgrading is: change
SDL_VERSION, put the new hashes in PACKAGES (print one with --print-sha), and
re-run.

CMake drives this script itself (cmake/sdl2.cmake): --locate at every
configure, which never touches the network, and a fetch at configure time
only when -DEIDOS_SDL_FETCH=ON.  Standard library only.

Usage:

    python tools/fetch_sdl.py                     # the host's default flavour
    python tools/fetch_sdl.py --flavor vc         # one flavour
    python tools/fetch_sdl.py --flavor all        # mingw, vc and source
    python tools/fetch_sdl.py --check             # report the cache, fetch nothing
    python tools/fetch_sdl.py --where             # print the cache directory
    python tools/fetch_sdl.py --force             # re-download and re-extract
    python tools/fetch_sdl.py --print-sha FILE    # hash an archive and stop

    # build from source with a given compiler into SDL2-<ver>-<tag>/
    python tools/fetch_sdl.py --flavor source --build --tag linux-gcc-64 \\
        [--cmake-arg=-DCMAKE_C_COMPILER=gcc ...]

    # what CMake asks: the prefix to use, or exit status 1
    python tools/fetch_sdl.py --locate vc --arch x86
    python tools/fetch_sdl.py --locate source --tag linux-gcc-64
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

SDL_VERSION = "2.32.10"
SDL_RELEASE = ("https://github.com/libsdl-org/SDL/releases/download/release-%s/"
               % SDL_VERSION)

# flavour -> (archive name, SHA-256, directory the archive unpacks to)
PACKAGES = {
    "mingw": ("SDL2-devel-%s-mingw.tar.gz" % SDL_VERSION,
              "83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2",
              "SDL2-%s" % SDL_VERSION),
    "vc": ("SDL2-devel-%s-VC.zip" % SDL_VERSION,
           "af347939395a58b365846aaea27391e69f9ec9d4dd650d6ac40802159b418a6e",
           "SDL2-%s" % SDL_VERSION),
    "source": ("SDL2-%s.tar.gz" % SDL_VERSION,
               "5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165",
               "SDL2-%s" % SDL_VERSION),
}

MINGW_TRIPLETS = {"x64": "x86_64-w64-mingw32", "x86": "i686-w64-mingw32"}

STAMP = ".eidos-sha256"
CHUNK = 1 << 16


# ---------------------------------------------------------------------------
# where
# ---------------------------------------------------------------------------

def default_cache():
    env = os.environ.get("EIDOS_SDL_CACHE")
    if env:
        return os.path.abspath(env)
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA") or os.path.join(
            os.path.expanduser("~"), "AppData", "Local")
    elif sys.platform == "darwin":
        base = os.path.join(os.path.expanduser("~"), "Library", "Caches")
    else:
        base = os.environ.get("XDG_CACHE_HOME") or os.path.join(
            os.path.expanduser("~"), ".cache")
    return os.path.join(base, "eidos", "sdl")


def tree_dir(cache, flavor):
    return os.path.join(cache, "SDL2-%s-%s" % (
        SDL_VERSION, "src" if flavor == "source" else flavor))


def prefix_dir(cache, tag):
    return os.path.join(cache, "SDL2-%s-%s" % (SDL_VERSION, tag))


def host_flavor():
    return "mingw" if os.name == "nt" else "source"


# ---------------------------------------------------------------------------
# download, verify, extract
# ---------------------------------------------------------------------------

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
    print("downloading %s" % url, flush=True)
    tmp = dest + ".part"
    with urllib.request.urlopen(url, timeout=300) as src:
        with open(tmp, "wb") as out:
            shutil.copyfileobj(src, out, CHUNK)
    os.replace(tmp, dest)


def check_paths(names):
    """Refuse any archive member that would land outside the destination."""
    for name in names:
        path = os.path.normpath(name)
        if path.startswith("..") or os.path.isabs(path) or ":" in path:
            raise SystemExit("refusing path in archive: %s" % name)


def extract(archive, inner, target, digest):
    """Unpack `archive`, whose top directory is `inner`, to `target`.

    The archive goes into a scratch directory next to the target and is
    renamed into place only once complete, then stamped with its hash."""
    parent = os.path.dirname(target)
    scratch = tempfile.mkdtemp(prefix=".unpack-", dir=parent)
    try:
        print("extracting %s" % os.path.basename(archive), flush=True)
        if archive.endswith(".zip"):
            with zipfile.ZipFile(archive) as zf:
                check_paths(zf.namelist())
                zf.extractall(scratch)
        else:
            with tarfile.open(archive, "r:gz") as tar:
                members = tar.getmembers()
                check_paths(m.name for m in members)
                check_paths(os.path.join(os.path.dirname(m.name), m.linkname)
                            for m in members if m.issym() or m.islnk())
                if hasattr(tarfile, "data_filter"):
                    # Python 3.11.4+: the standard library's own checks too.
                    tar.extractall(scratch, filter="data")
                else:
                    tar.extractall(scratch)
        src = os.path.join(scratch, inner)
        if not os.path.isdir(src):
            raise SystemExit("archive did not contain %s" % inner)
        if os.path.isdir(target):
            shutil.rmtree(target)
        os.replace(src, target)
        with open(os.path.join(target, STAMP), "w") as fp:
            fp.write(digest + "\n")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def stamped(target, digest):
    try:
        with open(os.path.join(target, STAMP)) as fp:
            return fp.read().strip() == digest
    except OSError:
        return False


def fetch(cache, flavor, force):
    """Download, verify and unpack one flavour; return its tree or None."""
    archive_name, digest, inner = PACKAGES[flavor]
    target = tree_dir(cache, flavor)
    if not force and stamped(target, digest):
        return target

    downloads = os.path.join(cache, "downloads")
    os.makedirs(downloads, exist_ok=True)
    archive = os.path.join(downloads, archive_name)
    if force and os.path.isfile(archive):
        os.remove(archive)
    if not os.path.isfile(archive):
        try:
            download(SDL_RELEASE + archive_name, archive)
        except OSError as e:
            print("download failed: %s" % e, file=sys.stderr)
            return None

    got = sha256_file(archive)
    if got != digest:
        os.remove(archive)
        print("SHA-256 mismatch for %s" % archive_name, file=sys.stderr)
        print("  expected %s" % digest, file=sys.stderr)
        print("  got      %s" % got, file=sys.stderr)
        print("the download was deleted; re-run to try again", file=sys.stderr)
        return None
    print("sha256 ok: %s %s" % (archive_name, got))
    extract(archive, inner, target, digest)
    return target


# ---------------------------------------------------------------------------
# what a tree provides
# ---------------------------------------------------------------------------

def prebuilt_prefix(cache, flavor, arch):
    """The directory CMake uses for one prebuilt flavour and architecture,
    or None when it is not in the cache or is incomplete."""
    target = tree_dir(cache, flavor)
    if not stamped(target, PACKAGES[flavor][1]):
        return None
    if flavor == "mingw":
        pre = os.path.join(target, MINGW_TRIPLETS[arch])
        need = [("include", "SDL2", "SDL.h"), ("lib", "libSDL2.dll.a"),
                ("bin", "SDL2.dll")]
    else:
        pre = target
        need = [("include", "SDL.h"), ("lib", arch, "SDL2.lib"),
                ("lib", arch, "SDL2.dll")]
    if all(os.path.isfile(os.path.join(pre, *p)) for p in need):
        return pre
    return None


def source_prefix(cache, tag):
    pre = prefix_dir(cache, tag)
    if not stamped(pre, PACKAGES["source"][1]):
        return None
    for sub in ("lib", "lib64"):
        if os.path.isfile(os.path.join(pre, sub, "cmake", "SDL2",
                                       "SDL2Config.cmake")):
            return pre
    return None


def build_source(cache, tag, cmake_args, generator):
    """Configure, build and install the source flavour into SDL2-<ver>-<tag>."""
    if tag in ("mingw", "vc", "src"):
        print("--tag %s is reserved" % tag, file=sys.stderr)
        return None
    src = fetch(cache, "source", False)
    if not src:
        return None
    cmake = shutil.which("cmake")
    if not cmake:
        print("cmake is not on PATH", file=sys.stderr)
        return None
    pre = prefix_dir(cache, tag)
    bdir = os.path.join(cache, "build-%s-%s" % (SDL_VERSION, tag))
    shutil.rmtree(bdir, ignore_errors=True)
    shutil.rmtree(pre, ignore_errors=True)
    cfg = [cmake, "-S", src, "-B", bdir,
           "-DCMAKE_BUILD_TYPE=Release",
           "-DCMAKE_INSTALL_PREFIX=" + pre,
           # The shared library and its CMake package: no tests and no
           # static copy.  (eidos defines SDL_MAIN_HANDLED and keeps its own
           # main(), so whatever SDL2main SDL builds alongside is unused.)
           "-DSDL_SHARED=ON", "-DSDL_STATIC=OFF", "-DSDL_TEST=OFF"]
    if generator:
        cfg += ["-G", generator]
    cfg += list(cmake_args)
    # SDL's own configure and build are long and are not eidos's output (and
    # must not count as eidos warnings in tools/check.py): they go to a log
    # beside the prefix, whose tail is shown if a step fails.
    log = pre + ".log"
    print("building SDL2 %s from source as %s (log: %s)"
          % (SDL_VERSION, tag, log), flush=True)
    with open(log, "w", encoding="utf-8", errors="replace") as fh:
        for cmd in (cfg,
                    [cmake, "--build", bdir, "--config", "Release"],
                    [cmake, "--install", bdir, "--config", "Release"]):
            fh.write("$ %s\n" % " ".join(cmd))
            fh.flush()
            if subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT,
                              stdin=subprocess.DEVNULL).returncode != 0:
                fh.close()
                with open(log, encoding="utf-8", errors="replace") as rd:
                    sys.stderr.write("".join(rd.readlines()[-30:]))
                print("failed: %s" % " ".join(cmd), file=sys.stderr)
                return None
    with open(os.path.join(pre, STAMP), "w") as fp:
        fp.write(PACKAGES["source"][1] + "\n")
    shutil.rmtree(bdir, ignore_errors=True)
    return source_prefix(cache, tag)


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def report(cache):
    print("cache: %s" % cache)
    found = False
    for flavor in ("mingw", "vc"):
        arches = [a for a in ("x64", "x86")
                  if prebuilt_prefix(cache, flavor, a)]
        print("  %-6s %s" % (flavor, ", ".join(arches) if arches else "absent"))
        found = found or bool(arches)
    builds = []
    stem = "SDL2-%s-" % SDL_VERSION
    if os.path.isdir(cache):
        for name in sorted(os.listdir(cache)):
            tag = name[len(stem):]
            if (name.startswith(stem) and tag not in ("mingw", "vc", "src")
                    and source_prefix(cache, tag)):
                builds.append(tag)
    print("  %-6s %s" % ("source", ", ".join(builds) if builds
                         else "no builds"))
    return 0 if found or builds else 1


def main(argv):
    ap = argparse.ArgumentParser(
        description="Fetch the pinned SDL2 release into a cache outside "
                    "the repository (see the module docstring).")
    ap.add_argument("--flavor", choices=("mingw", "vc", "source", "all"),
                    help="which package (default: mingw on Windows, "
                         "source elsewhere)")
    ap.add_argument("--cache", help="cache directory (default: %s)"
                    % default_cache().replace("%", "%%"))
    ap.add_argument("--force", action="store_true",
                    help="re-download and re-extract even if present")
    ap.add_argument("--check", action="store_true",
                    help="report what the cache holds, download nothing")
    ap.add_argument("--where", action="store_true",
                    help="print the cache directory and stop")
    ap.add_argument("--print-sha", metavar="FILE",
                    help="print FILE's SHA-256 and stop")
    ap.add_argument("--locate", choices=("mingw", "vc", "source"),
                    help="print the prefix CMake should use, or exit 1; "
                         "never downloads")
    ap.add_argument("--arch", choices=("x64", "x86"), default="x64",
                    help="architecture for --locate mingw/vc")
    ap.add_argument("--build", action="store_true",
                    help="with --flavor source: build and install it")
    ap.add_argument("--tag", help="name of a source build's install prefix")
    ap.add_argument("--generator", help="CMake generator for --build")
    ap.add_argument("--cmake-arg", action="append", default=[],
                    help="extra argument for the source build's configure "
                         "(repeatable; write --cmake-arg=-D...)")
    args = ap.parse_args(argv)

    if args.print_sha:
        print(sha256_file(args.print_sha))
        return 0

    cache = os.path.abspath(args.cache) if args.cache else default_cache()
    if args.where:
        print(cache)
        return 0
    if args.check:
        return report(cache)

    if args.locate:
        if args.locate == "source":
            if not args.tag:
                ap.error("--locate source needs --tag")
            pre = source_prefix(cache, args.tag)
        else:
            pre = prebuilt_prefix(cache, args.locate, args.arch)
        if not pre:
            return 1
        print(pre.replace("\\", "/"))
        return 0

    os.makedirs(cache, exist_ok=True)
    flavors = (("mingw", "vc", "source") if args.flavor == "all"
               else (args.flavor or host_flavor(),))
    status = 0
    for flavor in flavors:
        if flavor == "source" and args.build:
            if not args.tag:
                ap.error("--build needs --tag")
            pre = build_source(cache, args.tag, args.cmake_arg,
                               args.generator)
            if not pre:
                status = 1
                continue
            print("SDL2 %s built: %s" % (SDL_VERSION, pre))
            continue
        target = fetch(cache, flavor, args.force)
        if not target:
            status = 1
            continue
        print("SDL2 %s %s ready in %s" % (SDL_VERSION, flavor, target))
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
