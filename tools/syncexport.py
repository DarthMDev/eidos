#!/usr/bin/env python3
"""syncexport.py -- keep an export of an aletheia commit up to date in place.

The build reads aletheia from a plain export (`git archive`), off the
synced checkout.  Exporting every new commit to a fresh directory changes
every file's path and time, so tools/alcache.py's caches start empty and the
next configure reads the whole tree again.  This tool moves an existing
export from the commit it holds to another one, touching only the files the
two commits differ in; everything else keeps its time and stays cached.

    syncexport.py REPO COMMIT DEST

REPO is a git checkout that has COMMIT (fetch first).  DEST holds the
export; `DEST/.eidos-aletheia-rev` records its commit (the stamp
tools/pinned_checkout.py writes, which cmake/aletheia_pin.cmake reads).
Without that file DEST must be empty or missing, and the whole commit is
exported.
"""

from __future__ import annotations

import io
import os
import subprocess
import sys
import tarfile

MARK = ".eidos-aletheia-rev"   # the stamp tools/pinned_checkout.py writes too


def git(repo, *args, binary=False):
    out = subprocess.run(["git", "-C", repo] + list(args), check=True,
                         stdout=subprocess.PIPE).stdout
    return out if binary else out.decode("utf-8")


def extract(repo, commit, dest, paths=None):
    """Write `paths` (all when None) of `commit` under dest."""
    args = ["archive", "--format=tar", commit]
    if paths:
        args += ["--"] + paths
    data = git(repo, *args, binary=True)
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        for m in tar.getmembers():
            if not m.isfile():
                continue
            out = os.path.join(dest, m.name)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with tar.extractfile(m) as src, open(out, "wb") as fh:
                fh.write(src.read())


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    repo, commit, dest = sys.argv[1:4]
    commit = git(repo, "rev-parse", commit + "^{commit}").strip()
    mark = os.path.join(dest, MARK)
    old = None
    if os.path.exists(mark):
        with open(mark, encoding="utf-8") as fh:
            old = fh.read().strip()
    elif os.path.isdir(dest) and os.listdir(dest):
        raise SystemExit("syncexport.py: %s is not an export this tool made "
                         "(no %s) and is not empty" % (dest, MARK))
    if old == commit:
        print("syncexport.py: %s already holds %s" % (dest, commit[:10]))
        return
    os.makedirs(dest, exist_ok=True)
    if old is None:
        extract(repo, commit, dest)
        print("syncexport.py: exported %s to %s" % (commit[:10], dest))
    else:
        changed, removed = [], []
        diff = git(repo, "diff", "--name-status", "--no-renames", "-z", old, commit)
        parts = diff.split("\0")
        for i in range(0, len(parts) - 1, 2):
            status, path = parts[i], parts[i + 1]
            (removed if status.startswith("D") else changed).append(path)
        for p in removed:
            full = os.path.join(dest, p)
            if os.path.isfile(full):
                os.remove(full)
        for i in range(0, len(changed), 500):
            extract(repo, commit, dest, changed[i:i + 500])
        print("syncexport.py: %s %s -> %s: %d written, %d removed"
              % (dest, old[:10], commit[:10], len(changed), len(removed)))
    with open(mark, "w", encoding="utf-8") as fh:
        fh.write(commit + "\n")


if __name__ == "__main__":
    main()
