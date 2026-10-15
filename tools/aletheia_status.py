#!/usr/bin/env python3
"""
aletheia_status.py -- where the decompilation stands, in one block.

eidos compiles aletheia's matched C, so aletheia's progress is eidos's input.
This prints the few things that matter to the port: how far aletheia has got,
whether it is mid-wave (a live working tree means the port is building against
a moving target), and what the recent waves changed.

Reads only git and aletheia's own progress report. Writes nothing.

Usage:
  python tools/aletheia_status.py
  python tools/aletheia_status.py --aletheia-root <path> --commits 6
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def git(root, *args):
    try:
        out = subprocess.run(["git", "-C", root] + list(args),
                             capture_output=True, text=True, timeout=120)
        return out.stdout.strip()
    except Exception as exc:                                  # pragma: no cover
        return "git failed: %s" % exc


def report_summary(path):
    """The matched totals from aletheia's decomp.dev report, when it is there."""
    if not os.path.exists(path):
        return None
    try:
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
    except Exception:
        return None
    measures = data.get("measures") or {}
    keys = [k for k in measures if isinstance(measures[k], (int, float))]
    out = {}
    for want in ("matched_code", "total_code", "matched_functions",
                 "total_functions", "complete_code", "complete_functions"):
        if want in measures:
            out[want] = measures[want]
    if not out and keys:
        out = {k: measures[k] for k in keys[:6]}
    return out


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--aletheia-root", default=os.path.join(ROOT, "..", "bw_decomp"))
    ap.add_argument("--commits", type=int, default=5)
    args = ap.parse_args(argv)

    root = os.path.abspath(args.aletheia_root)
    if not os.path.isdir(os.path.join(root, ".git")):
        print("aletheia_status: no checkout at %s" % root)
        return 1

    head = git(root, "log", "-1", "--format=%h %ad %s", "--date=short")
    changed = git(root, "status", "--porcelain")
    n_changed = len([ln for ln in changed.splitlines() if ln.strip()])

    print("aletheia status")
    print("  checkout   : %s" % root)
    print("  head       : %s" % head)
    if n_changed:
        print("  working    : %d changed files, so a wave is in progress"
              % n_changed)
    else:
        print("  working    : clean")

    measures = report_summary(os.path.join(root, "progress", "report.json"))
    if measures:
        print("  report     : %s" % ", ".join(
            "%s=%s" % (k, measures[k]) for k in sorted(measures)))
    else:
        print("  report     : no progress/report.json")

    print("  recent waves:")
    for line in git(root, "log", "-%d" % args.commits,
                    "--format=%h %ad %s", "--date=short").splitlines():
        print("    %s" % line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
