# The pinned aletheia revision

eidos compiles aletheia's matched C, its `layout/` manifests and its `tools/`
scripts out of the checkout at `ALETHEIA_ROOT` (CMakeLists.txt, "Where the
aletheia decompilation checkout lives"). That checkout is edited by its own
lanes, so the port's build is only reproducible against one revision of it.
This file records that revision.

## The pin

- Revision: `e06766e42c915278b376fdd9e6b1ddfcdd4ea7f4`
- Date: 2026-10-15 22:09:06 +0100
- Subject: Cleanup wave 10 and the gap fixes: game code fully linked at
  100.00 percent; ov194 namein_graphic split into five originals; arm9 ui
  merged; non-contiguous pieces now verifiable

The pin lives in `cmake/aletheia_pin.cmake` as `EIDOS_ALETHEIA_PIN`.

## What it is for

- A reproducible build. The same revision of aletheia gives the same matched
  sources, so a difference in a build is a difference in eidos.
- A deliberate update. A rename or a split in an aletheia lane shows up as a
  warning at configure time, so moving the port to a new revision is a
  recorded act rather than a surprise.

## How the check fires

`cmake/aletheia_pin.cmake` runs at configure time. `cmake/dsaddr.cmake`
includes it, and CMakeLists.txt includes `cmake/dsaddr.cmake`
unconditionally, so the check runs on every configure. It compares
`git -C $ALETHEIA_ROOT rev-parse HEAD` against the pin and warns, without
failing the build, when either holds:

- HEAD is another commit. The warning names the pin and the checkout.
- Tracked files have uncommitted changes. The pin names a clean tree, so the
  compiled sources are not that revision even when HEAD matches it. The
  warning counts the changed tracked files. A warning that names many of them
  means a wave is in flight: the port is compiling a tree that is not the pin,
  so the result is not the pin's result. The clean copy below exists for
  exactly this case. Untracked files do not warn, because the build compiles
  only the files `CMakeLists.txt` names, and those are tracked.

A build against a newer checkout is allowed. The warning is a record, not a
gate. The check prints a `STATUS` line when the checkout is the pin and is
clean.

When `ALETHEIA_ROOT` is a clean copy made by `tools/pinned_checkout.py`, it
has no `.git`. The check reads the revision from the copy's
`.eidos-aletheia-rev` stamp instead, and prints a `STATUS` line when it is the
pin.

## Building against a clean copy

A wave in an aletheia lane leaves uncommitted changes, and the port then
builds something other than the pin. `tools/pinned_checkout.py` exports the
pin to a read-only copy outside the repository and prints the path to pass as
`ALETHEIA_ROOT`:

```
python tools/pinned_checkout.py            # make or reuse the copy
python tools/pinned_checkout.py --where    # print the copy's path
python tools/pinned_checkout.py --check    # report the copy, export nothing
python tools/pinned_checkout.py --force    # rebuild the copy
```

The copy lands at `%LOCALAPPDATA%\eidos\aletheia-<short-rev>` on Windows
(`~/Library/Caches/eidos/...` on macOS, `$XDG_CACHE_HOME/eidos/...` or
`~/.cache/eidos/...` elsewhere). The revision is in the directory name, so a
second run reuses a copy that is already the pin, and a moved pin makes a
second directory. The tool reads the revision from `EIDOS_ALETHEIA_PIN` in
`cmake/aletheia_pin.cmake` and never writes to the source checkout.

The copy holds the tracked files at the pin and nothing else. `extract/data`
is not tracked, so pass `BW_DATA_ROOT` when the extracted assets live in the
working checkout:

```
cmake -S . -B build \
    -DALETHEIA_ROOT=%LOCALAPPDATA%\eidos\aletheia-e06766e42c \
    -DBW_DATA_ROOT=<working checkout>/extract/data
```

## Updating the pin

Move the pin when the port is verified against a new aletheia revision. Edit
`EIDOS_ALETHEIA_PIN` in `cmake/aletheia_pin.cmake`, update the revision, the
date and the subject above, and rebuild. `tools/pinned_checkout.py` reads the
same name, so run it once for the new revision to make the clean copy.
Nothing else reads the pin.

This file is about the aletheia revision and the build's reproducibility.
`docs/status.md` is the port's own status and is a separate document.

Note: this file is named `docs/aletheia-pin.md`, not `docs/STATUS.md`.
`docs/` is case-insensitive on Windows, so a `docs/STATUS.md` is the same
file as the port's `docs/status.md`; the two cannot both exist.
