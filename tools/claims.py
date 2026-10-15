#!/usr/bin/env python3
"""
claims.py -- who owns which path, so workstreams can run in parallel.

Aletheia's lanes solved this with a claims registry: a lane edits only the
files inside its claim, one owner per object, and the registry says who that
is.  eidos needs the same thing for the same reason: subagents are meant to
run at the same time, and two of them editing one file loses work.

A claim is a path, or a directory prefix, owned by one id.  A path belongs to
a claim when it is the path, or sits under it.  Two claims overlap when
either owns the other's path; `claim` refuses an overlapping claim unless
--force is given, so a collision is caught before the work starts rather than
by a merge.

Each id also gets a build directory by convention, `build-<id>`, so parallel
workstreams do not fight over one build tree.  `show` prints it.

Usage:
  python tools/claims.py claim W1 tools/closure.py tools/bootlink.py --note "Class B"
  python tools/claims.py show
  python tools/claims.py check tools/closure.py
  python tools/claims.py conflicts
  python tools/claims.py release W1

The registry is claims.json at the repository root.  Paths are stored with
forward slashes, relative to the root.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REGISTRY = os.path.join(ROOT, "claims.json")

# Never worth claiming: generated trees and caches.
IGNORED = ("build", "build32", "third_party", ".git", "research_notes", "reports")


def norm(path):
    p = path.replace("\\", "/").strip()
    while p.startswith("./"):
        p = p[2:]
    return p.rstrip("/") or "."


def load():
    if not os.path.exists(REGISTRY):
        return {"claims": {}}
    with open(REGISTRY, encoding="utf-8") as fh:
        data = json.load(fh)
    data.setdefault("claims", {})
    return data


def save(data):
    with open(REGISTRY, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(data, fh, indent=2, sort_keys=True)
        fh.write("\n")


def owns(claim, path):
    """True when `path` is `claim`, or sits under it."""
    if claim == path:
        return True
    if claim == ".":
        return True
    return path.startswith(claim + "/")


def overlap(a, b):
    return owns(a, b) or owns(b, a)


def all_paths(data, exclude_id=None):
    for cid, entry in data["claims"].items():
        if cid == exclude_id:
            continue
        for p in entry["paths"]:
            yield cid, p


def cmd_claim(args):
    data = load()
    paths = [norm(p) for p in args.paths]
    if not paths:
        print("claims.py: give at least one path", file=sys.stderr)
        return 2
    for p in paths:
        if p.split("/")[0] in IGNORED:
            print("claims.py: %s is a generated or ignored tree" % p, file=sys.stderr)
            return 2
    clashes = []
    for p in paths:
        for cid, other in all_paths(data, exclude_id=args.id):
            if overlap(p, other):
                clashes.append((p, cid, other))
    if clashes and not args.force:
        for p, cid, other in clashes:
            print("claims.py: %s overlaps %s's claim on %s" % (p, cid, other),
                  file=sys.stderr)
        print("claims.py: use --force only when the overlap is intended",
              file=sys.stderr)
        return 1
    entry = data["claims"].get(args.id, {"paths": [], "note": "", "started": ""})
    entry["paths"] = sorted(set(entry["paths"]) | set(paths))
    if args.note:
        entry["note"] = args.note
    if not entry.get("started"):
        entry["started"] = datetime.date.today().isoformat()
    data["claims"][args.id] = entry
    save(data)
    print("claims.py: %s now owns %d path(s); build dir build-%s"
          % (args.id, len(entry["paths"]), args.id))
    return 0


def cmd_release(args):
    data = load()
    if args.id not in data["claims"]:
        print("claims.py: %s holds no claim" % args.id, file=sys.stderr)
        return 1
    if args.paths:
        keep = [p for p in data["claims"][args.id]["paths"]
                if not any(owns(norm(q), p) for q in args.paths)]
        if keep:
            data["claims"][args.id]["paths"] = keep
        else:
            del data["claims"][args.id]
    else:
        del data["claims"][args.id]
    save(data)
    print("claims.py: released %s" % args.id)
    return 0


def cmd_show(args):
    data = load()
    if not data["claims"]:
        print("claims.py: no active claims")
        return 0
    for cid in sorted(data["claims"]):
        entry = data["claims"][cid]
        print("%-6s build-%-6s %-11s %s"
              % (cid, cid, entry.get("started", ""), entry.get("note", "")))
        for p in entry["paths"]:
            print("         %s" % p)
    return 0


def cmd_check(args):
    data = load()
    p = norm(args.path)
    owners = [cid for cid, other in all_paths(data) if owns(other, p)]
    if owners:
        print("%s: owned by %s" % (p, ", ".join(owners)))
        return 1
    print("%s: unclaimed" % p)
    return 0


def cmd_conflicts(args):
    data = load()
    found = 0
    ids = sorted(data["claims"])
    for i, a in enumerate(ids):
        for b in ids[i + 1:]:
            for pa in data["claims"][a]["paths"]:
                for pb in data["claims"][b]["paths"]:
                    if overlap(pa, pb):
                        print("conflict: %s (%s) and %s (%s)" % (a, pa, b, pb))
                        found += 1
    if not found:
        print("claims.py: no conflicts")
    return 1 if found else 0


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("claim")
    p.add_argument("id")
    p.add_argument("paths", nargs="*")
    p.add_argument("--note", default="")
    p.add_argument("--force", action="store_true")
    p.set_defaults(func=cmd_claim)

    p = sub.add_parser("release")
    p.add_argument("id")
    p.add_argument("paths", nargs="*")
    p.set_defaults(func=cmd_release)

    sub.add_parser("show").set_defaults(func=cmd_show)

    p = sub.add_parser("check")
    p.add_argument("path")
    p.set_defaults(func=cmd_check)

    sub.add_parser("conflicts").set_defaults(func=cmd_conflicts)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
