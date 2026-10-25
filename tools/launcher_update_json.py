#!/usr/bin/env python3
"""The launcher's update file (latest.json), as the Tauri updater reads it.

    python tools/launcher_update_json.py --version 0.1.6 \\
        --signature launcher/src-tauri/target/release/bundle/nsis/x.exe.sig \\
        --url https://github.com/<repo>/releases/download/v0.1.6/<installer> \\
        [--notes TEXT] > latest.json

A release carries it; running launchers fetch the newest one from
.../releases/latest/download/latest.json (launcher/src-tauri/tauri.conf.json)
and install the signed installer it names when its version is newer.
"""

import argparse
import datetime
import json
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--version", required=True)
    ap.add_argument("--signature", required=True, help="the installer's .sig file")
    ap.add_argument("--url", required=True, help="where the installer is downloaded from")
    ap.add_argument("--notes", default="")
    args = ap.parse_args()
    sig = open(args.signature, encoding="utf-8").read().strip()
    doc = {
        "version": args.version.lstrip("vV"),
        "notes": args.notes,
        "pub_date": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "platforms": {
            "windows-x86_64": {"signature": sig, "url": args.url},
        },
    }
    json.dump(doc, sys.stdout, indent=2)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
