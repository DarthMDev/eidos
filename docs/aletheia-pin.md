# The pinned aletheia revision

eidos compiles aletheia's matched C, its `layout/` manifests and its `tools/`
scripts out of the checkout at `ALETHEIA_ROOT` (CMakeLists.txt, "Where the
aletheia decompilation checkout lives"). That checkout is edited by its own
lanes, so the port's build is only reproducible against one revision of it.
This file records that revision.

## The pin

- Revision: `999783ad9ded29326c183c89f2af52102bf6dcbe`
- Date: 2026-10-20 19:49:48 +0100
- Subject: Wave 20 phase 2: overlay types and per-file headers; host build
  compiles; original bugs archived

At this pin every overlay but ov214 compiles: ov214 has a data object
named `site`, and eidos's data-name macro rewrites the parameter of that
name in arm9/gfl_heap_wrap.h.  aletheia gives such generic globals module
names next; until then configure with an EIDOS_OVERLAYS list that leaves
214 out.  The functions
that fall off their end return the DS value under `#ifndef __MWERKS__`
(aletheia docs/ORIGINAL_BUGS.tsv), so tools/gamereturns.txt is empty, and
every variadic function takes the host <stdarg.h> through
src/game/sdk_stdarg.h.

The port compiles this revision's `src/game` (EIDOS_SOURCE_TREE=game, the
default, docs/game-tree.md).  tools/gametree.py takes each function's
address from `layout/function_addrs.tsv`, so the manifest gaps that
tools/gametree_extra.txt used to fill are gone.  The previous pin, `e06766e42c` (cleanup wave
10), is the last one the `src/matched` build was run against; at the new pin
some tests name src/matched functions by their wave 19 names (TP_, MIC_,
RtcSendPxiCommand), so `matched` is kept for comparison runs against that
older export only.

To move an export to a newer pin without losing the generators' caches,
`tools/syncexport.py REPO COMMIT DEST` rewrites only the files the two
commits differ in (docs/game-tree.md, "Build speed").

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
