#!/usr/bin/env python3
"""The launcher's update file (latest.json), as the Tauri updater reads it.

Each launcher job writes the part for its own platform:

    python tools/launcher_update_json.py part --platform windows-x86_64 \\
        --signature launcher/src-tauri/target/release/bundle/nsis/x.exe.sig \\
        --url https://github.com/<repo>/releases/download/v0.1.6/<file> \\
        > updater-windows.json

and the release job joins the parts:

    python tools/launcher_update_json.py join --version 0.1.6 \\
        [--notes TEXT] updater-*.json > latest.json

A release carries latest.json; running launchers fetch the newest one from
.../releases/latest/download/latest.json (launcher/src-tauri/tauri.conf.json)
and install the signed bundle named for their platform when its version is
newer.  Platform names are Tauri's: windows-x86_64, linux-x86_64,
darwin-aarch64.
"""

import argparse
import datetime
import json
import sys


def part(args):
    sig = open(args.signature, encoding="utf-8").read().strip()
    return {args.platform: {"signature": sig, "url": args.url}}


def join(args):
    platforms = {}
    for path in args.parts:
        with open(path, encoding="utf-8") as f:
            platforms.update(json.load(f))
    if not platforms:
        sys.exit("no platform parts")
    return {
        "version": args.version.lstrip("vV"),
        "notes": args.notes,
        "pub_date": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "platforms": platforms,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("part", help="one platform's entry")
    p.add_argument("--platform", required=True)
    p.add_argument("--signature", required=True, help="the bundle's .sig file")
    p.add_argument("--url", required=True, help="where the bundle is downloaded from")
    j = sub.add_parser("join", help="latest.json from the platform entries")
    j.add_argument("--version", required=True)
    j.add_argument("--notes", default="")
    j.add_argument("parts", nargs="+")
    args = ap.parse_args()
    doc = part(args) if args.cmd == "part" else join(args)
    json.dump(doc, sys.stdout, indent=2)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
