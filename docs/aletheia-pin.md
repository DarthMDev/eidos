# The pinned aletheia revision

eidos compiles aletheia's matched C, its `layout/` manifests and its `tools/`
scripts out of the checkout at `ALETHEIA_ROOT` (CMakeLists.txt, "Where the
aletheia decompilation checkout lives"). That checkout is edited by its own
lanes, so the port's build is only reproducible against one revision of it.
This file records that revision.

## The pin

- Revision: `80f06a261e8731f2db94c1a66627bb351ad47673`
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
  compiled sources are not that revision even when HEAD matches it.

A build against a newer checkout is allowed. The warning is a record, not a
gate. The check prints a `STATUS` line when the checkout is the pin and is
clean.

## Updating the pin

Move the pin when the port is verified against a new aletheia revision. Edit
`EIDOS_ALETHEIA_PIN` in `cmake/aletheia_pin.cmake`, update the revision, the
date and the subject above, and rebuild. Nothing else reads the pin.

This file is about the aletheia revision and the build's reproducibility.
`docs/status.md` is the port's own status and is a separate document.

Note: this file is named `docs/aletheia-pin.md`, not `docs/STATUS.md`.
`docs/` is case-insensitive on Windows, so a `docs/STATUS.md` is the same
file as the port's `docs/status.md`; the two cannot both exist.
