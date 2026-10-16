#!/usr/bin/env python3
"""
tools/pinned_checkout.py -- a clean copy of the pinned aletheia revision,
outside the repository.

eidos compiles aletheia's matched C, its layout/ manifests and its tools/
scripts out of the checkout at ALETHEIA_ROOT (CMakeLists.txt, "Where the
aletheia decompilation checkout lives").  That checkout is edited by its own
lanes, so a build against the live tree can break while a wave is in flight:
the port is verified against one revision, the pin, and a build that wants
exactly that revision needs a clean tree at it.  When a rename wave left about
1,286 uncommitted files in the checkout, the link only built against a clean
export of the pin.

This tool produces that tree.  It reads the revision from the pin in
cmake/aletheia_pin.cmake (EIDOS_ALETHEIA_PIN), exports it out of the source
checkout, and leaves a read-only copy under the per-user cache:

  Windows   %LOCALAPPDATA%\\eidos\\aletheia-<short-rev>
  macOS     ~/Library/Caches/eidos/aletheia-<short-rev>
  other     $XDG_CACHE_HOME/eidos/aletheia-<short-rev>, or
            ~/.cache/eidos/aletheia-<short-rev>

The source checkout is never written to.  The copy comes out with
`git archive <rev>`, which reads the objects and prints a tar stream, rather
than `git worktree`, which registers a worktree inside the repository it
borrows from and so touches the checkout this tool must leave alone.  The tar
goes to a scratch directory beside the destination and is unpacked there, and
the tree is renamed into place, so a copy that exists is complete.  The
directory name carries the
revision, so a second run reuses it and a moved pin makes a second directory.

The copy carries a stamp, .eidos-aletheia-rev, holding the full revision it
was exported from.  cmake/aletheia_pin.cmake reads that stamp, so a build
against the copy reports the pin instead of warning that a tree with no .git
cannot be read.

Pass the printed directory as ALETHEIA_ROOT.  The copy holds the tracked files
at the pin and nothing else, so pass BW_DATA_ROOT as well when the extracted
assets live in the working checkout: extract/data is not tracked and is not in
the copy.

Standard library only.  Writes nothing into the repository.

Usage:

    python tools/pinned_checkout.py                  # make or reuse the copy
    python tools/pinned_checkout.py --where          # print its directory
    python tools/pinned_checkout.py --check          # report, export nothing
    python tools/pinned_checkout.py --force          # rebuild it
    python tools/pinned_checkout.py --aletheia-root ../bw_decomp
    python tools/pinned_checkout.py --rev <hash>     # try a candidate revision
"""

import argparse
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIN_FILE = os.path.join(ROOT, "cmake", "aletheia_pin.cmake")
PIN_RE = re.compile(r'set\s*\(\s*EIDOS_ALETHEIA_PIN\s+"([0-9a-fA-F]{7,40})"')
STAMP = ".eidos-aletheia-rev"
SHORT = 10


# ---------------------------------------------------------------------------
# the pin, and where the copy goes
# ---------------------------------------------------------------------------

def read_pin(path=PIN_FILE):
    """The pinned revision, from the one file that holds it."""
    try:
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
    except OSError as exc:
        raise SystemExit("pinned_checkout: cannot read %s: %s" % (path, exc))
    match = PIN_RE.search(text)
    if not match:
        raise SystemExit("pinned_checkout: no EIDOS_ALETHEIA_PIN in %s" % path)
    return match.group(1)


def default_cache():
    env = os.environ.get("EIDOS_ALETHEIA_CACHE")
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
    return os.path.join(base, "eidos")


def copy_dir(cache, rev):
    """The copy's directory.  The short revision is in the name, so a copy
    that exists is a copy of that revision."""
    return os.path.join(cache, "aletheia-%s" % rev[:SHORT])


def default_checkout():
    """The sibling checkout, the same discovery CMakeLists.txt uses."""
    for name in ("aletheia", "bw_decomp"):
        cand = os.path.join(ROOT, "..", name)
        if os.path.isdir(os.path.join(cand, "src", "matched")):
            return os.path.abspath(cand)
    return os.path.abspath(os.path.join(ROOT, "..", "aletheia"))


# ---------------------------------------------------------------------------
# git
# ---------------------------------------------------------------------------

def git(checkout, *args):
    try:
        return subprocess.run(["git", "-C", checkout] + list(args),
                              capture_output=True, text=True, timeout=300)
    except OSError as exc:
        raise SystemExit("pinned_checkout: cannot run git: %s" % exc)


def is_checkout(checkout):
    out = git(checkout, "rev-parse", "--is-inside-work-tree")
    return out.returncode == 0 and out.stdout.strip() == "true"


def resolve(checkout, rev):
    """The full commit hash of rev, or None when the checkout lacks it."""
    out = git(checkout, "rev-parse", "--verify", "--quiet", rev + "^{commit}")
    if out.returncode != 0:
        return None
    return out.stdout.strip()


def guard(checkout, dest):
    """Refuse to write into the source checkout, or to read the copy as one."""
    if not is_checkout(checkout):
        raise SystemExit(
            "pinned_checkout: %s is not a git checkout" % checkout)
    if dest == checkout or dest.startswith(checkout + os.sep):
        raise SystemExit(
            "pinned_checkout: refusing to write into the source checkout %s"
            % checkout)


# ---------------------------------------------------------------------------
# export
# ---------------------------------------------------------------------------

def check_paths(names):
    """Refuse any archive member that would land outside the destination."""
    for name in names:
        path = os.path.normpath(name)
        if path.startswith("..") or os.path.isabs(path) or ":" in path:
            raise SystemExit("pinned_checkout: refusing path in archive: %s"
                             % name)


def export(checkout, rev, dest):
    """Unpack `git archive rev` from checkout into dest, read-only.

    The tree goes into a scratch directory beside dest and is renamed into
    place, so dest appears complete or not at all.  The stamp names the
    revision, which is what the pin module reads on a copy that has no .git.
    """
    parent = os.path.dirname(dest)
    os.makedirs(parent, exist_ok=True)
    scratch = tempfile.mkdtemp(prefix=".export-", dir=parent)
    try:
        archive = os.path.join(scratch, "pin.tar")
        print("exporting %s from %s" % (rev[:SHORT], checkout), flush=True)
        with open(archive, "wb") as out:
            proc = subprocess.run(
                ["git", "-C", checkout, "archive", "--format=tar", rev],
                stdout=out, stderr=subprocess.PIPE)
        if proc.returncode != 0:
            err = proc.stderr.decode("utf-8", "replace").strip()
            raise SystemExit("pinned_checkout: git archive failed: %s" % err)

        tree = os.path.join(scratch, "tree")
        os.makedirs(tree)
        with tarfile.open(archive, "r:") as tar:
            members = tar.getmembers()
            check_paths(m.name for m in members)
            check_paths(os.path.join(os.path.dirname(m.name), m.linkname)
                        for m in members if m.issym() or m.islnk())
            if hasattr(tarfile, "data_filter"):
                # Python 3.11.4+: the standard library's own checks too.
                tar.extractall(tree, filter="data")
            else:
                tar.extractall(tree)
        os.remove(archive)

        with open(os.path.join(tree, STAMP), "w", encoding="utf-8") as fh:
            fh.write(rev + "\n")
        mark_read_only(tree)
        os.rename(tree, dest)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def mark_read_only(root):
    """The copy is a build input.  Files are read-only so it stays the pin."""
    for base, _dirs, files in os.walk(root):
        for name in files:
            try:
                os.chmod(os.path.join(base, name), stat.S_IREAD)
            except OSError:
                pass


def on_rm_error(func, path, _exc):
    try:
        os.chmod(path, stat.S_IWRITE | stat.S_IREAD)
        func(path)
    except OSError:
        pass


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Export the pinned aletheia revision to a clean copy "
                    "outside the repository (see the module docstring).")
    ap.add_argument("--aletheia-root", help="source aletheia checkout "
                    "(default: %s)" % default_checkout())
    ap.add_argument("--cache", help="cache directory (default: %s)"
                    % default_cache().replace("%", "%%"))
    ap.add_argument("--rev", help="revision to export (default: the pin in "
                    "cmake/aletheia_pin.cmake)")
    ap.add_argument("--force", action="store_true",
                    help="rebuild the copy even if it is present")
    ap.add_argument("--check", action="store_true",
                    help="report the copy, export nothing")
    ap.add_argument("--where", action="store_true",
                    help="print the copy's directory and stop")
    args = ap.parse_args(argv)

    pin = args.rev or read_pin()
    cache = os.path.abspath(args.cache) if args.cache else default_cache()
    dest = copy_dir(cache, pin)

    if args.where:
        print(dest.replace("\\", "/"))
        return 0

    if args.check:
        if os.path.isdir(dest):
            print("aletheia %s present at %s" % (pin[:SHORT], dest))
            return 0
        print("aletheia %s: no copy at %s" % (pin[:SHORT], dest))
        return 1
    if os.path.isdir(dest) and not args.force:
        print("aletheia %s ready in %s" % (pin[:SHORT], dest))
        print("pass -DALETHEIA_ROOT=%s" % dest.replace("\\", "/"))
        return 0

    checkout = (os.path.abspath(args.aletheia_root) if args.aletheia_root
                else default_checkout())
    guard(checkout, dest)

    full = resolve(checkout, pin)
    if full is None:
        raise SystemExit("pinned_checkout: %s does not contain %s"
                         % (checkout, pin))

    if os.path.isdir(dest):
        shutil.rmtree(dest, onerror=on_rm_error)
    export(checkout, full, dest)
    print("aletheia %s exported to %s" % (full[:SHORT], dest))
    print("pass -DALETHEIA_ROOT=%s" % dest.replace("\\", "/"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
