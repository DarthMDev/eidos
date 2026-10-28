#!/usr/bin/env python3
"""The notes of a GitHub release: the version's changelog, what to download,
and the notice.

    python tools/release_notes.py VERSION [--assets DIR] > notes.md

VERSION is the release's version (0.1.5).  The changelog section is the one
headed "## VERSION" in CHANGELOG.md.  --assets names the folder of the
release's files: a platform's download line is only written when its file is
there (no macOS line without a macOS build).
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (file name without the version, what it is): the order of the table.
DOWNLOADS = [
    ("eidos-launcher-windows-setup.exe",
     "Windows: the launcher. It installs and updates eidos and keeps your "
     "profiles and mods. Start here."),
    ("eidos-windows.zip",
     "Windows: eidos on its own. Unzip it anywhere and run `eidos.exe`."),
    ("eidos-launcher-macos.zip",
     "macOS (Apple silicon): the launcher. The build is not signed: "
     "right-click it and choose Open the first time."),
    ("eidos-macos.zip",
     "macOS (Apple silicon): eidos on its own. The build is not signed: "
     "right-click `eidos` and choose Open the first time."),
    ("eidos-launcher-linux-x86_64.AppImage",
     "Linux: the launcher. Make it executable and run it."),
    ("eidos-linux-x86_64.AppImage",
     "Linux: eidos on its own. Make it executable and run it."),
]

NOTICE = (
    "eidos is an unofficial fan project, not affiliated with or endorsed by "
    "Nintendo, Game Freak, Creatures Inc. or The Pokemon Company. It contains "
    "no game data: you need your own copy of Pokemon Black or White. "
    "Pokemon and Nintendo DS are trademarks of their respective owners."
)


def changelog_section(version):
    path = os.path.join(ROOT, "CHANGELOG.md")
    try:
        text = open(path, encoding="utf-8").read()
    except OSError:
        return None
    m = re.search(r"^## %s\s*$(.*?)(?=^## |\Z)" % re.escape(version), text,
                  re.M | re.S)
    return m.group(1).strip() if m else None


def notes(version, assets):
    have = set(os.listdir(assets)) if assets and os.path.isdir(assets) else None
    out = []
    section = changelog_section(version)
    out.append("## What's new in %s" % version)
    out.append("")
    out.append(section or "- Fixes and small changes.")
    out.append("")
    out.append("## Download")
    out.append("")
    out.append("| File | |")
    out.append("| --- | --- |")
    for name, what in DOWNLOADS:
        if have is not None and name not in have:
            continue
        out.append("| `%s` | %s |" % (name, what))
    out.append("")
    out.append("On the first start, eidos asks for your own Pokemon Black or "
               "White ROM. The same files are also attached with the version "
               "in their names, and `SHA256SUMS` lists the checksum of each.")
    out.append("")
    out.append("---")
    out.append("")
    out.append("<sub>%s</sub>" % NOTICE)
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("version")
    ap.add_argument("--assets", help="the folder of the release's files")
    args = ap.parse_args()
    sys.stdout.write(notes(args.version.lstrip("vV"), args.assets))


if __name__ == "__main__":
    main()
