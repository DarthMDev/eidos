# The installer

eidos ships no game data. On first run the player gives it their own dump of
Pokemon Black and the installer turns it into the data root the engine reads.
This document has two halves: what exists and is verified (the extractor core
in `install/` and its command-line front end `eidos_extract`), and the design
of the per-platform first-run wizard that will wrap it on Windows, macOS,
Linux, Android and iOS.

## What exists

`install/` is plain C99 and the standard library, plus one small file of
operating-system calls (`install/fsutil.c`, Win32 and POSIX behind one
`#ifdef`). It has no SDL and nothing of the game, so every front end links
the same code and shows the same messages.

| Function | What it does |
|---|---|
| `bw_rom_identify` | header fields, header checksum, declared capacity, a streamed SHA-1 of the whole image (and of the image padded back to capacity when it is trimmed) |
| `bw_rom_verdict` | the decision and the message for the player (below) |
| `bw_rom_extract` | the extraction, staged and renamed into place |
| `bw_*_utf8` | file calls taking UTF-8 paths on every platform |

`eidos_extract [--any] [--replace] <rom.nds> [out_dir]` is the command-line
front end: identify only without `out_dir`, identify then extract with it.

### Verified on the real ROM

Run against the dump aletheia is built against (SHA-1
`26ad0b9967aa279c4a266ee69f52b9b2332399a5`, 268,435,456 bytes) and compared
file by file (size and SHA-1) with aletheia's own `extract/` (written by
`tools/ndsrom.py extract` and `tools/delink.py`):

- **726 files written, 726 identical**: 247 NitroFS files, 237 overlay
  files, 229 decompressed overlays, `arm9_dec.bin` (681,920 bytes), the
  header, header extension, ARM9, ARM7, FNT, FAT, `y9.bin`, the banner and
  the four DSi regions.
- **Nothing missing**: every file of aletheia's tree is written, except the
  four listed under "Intended differences".
- Time on this machine (Windows 11, MinGW-w64 GCC 14, warm file cache): about
  3 s to identify (hash) and 3 s to extract with an optimised build, 8 s in
  total unoptimised. The hash is half the time; a front end that has already
  identified the image passes its hash in and the extraction does not hash it
  again.

`test_extract_rom` repeats this on every `ctest` run where the environment
variable `BW_ROM` names the ROM: it runs `eidos_extract` into a folder named
`extract é€ ü` (spaces and characters outside ASCII and Latin-1), compares
every written file with the tree that holds `BW_DATA_ROOT` (its parent
directory), checks nothing expected is missing, checks a second run is a
no-op and that no staging directory is left, then deletes the extraction.
The expected hashes are computed from that tree on each run, so no hash of
any game file is written into the repository. Without `BW_ROM`, or without
the reference tree, the test reports itself skipped.

### Intended differences from aletheia's extract

| aletheia writes | eidos | why |
|---|---|---|
| `manifest.json`, `residue/` | not written | `ndsrom.py`'s rebuild bookkeeping: only needed to turn the tree back into a ROM |
| `arm9i_dec.bin` | not written | the DSi ARM9 extension decrypted; needs a DSi key the player supplies, which the installer does not take yet, and nothing in the port reads it |
| `ov230d.bin` | not written | overlay 230 after its own decryptor, a view for aletheia's disassembler only; `overlays_dec/y9_0230.bin` (the ROM bytes) is written |
| (nothing) | `eidos_manifest.json` | what was extracted from which image, by which installer version |

`overlays_dec/` is written because the port's data tests read it
(`test_overlay` checks overlay 230's static initialisers against it) and
`tools/title_closure.py` does; it is `delink.py`'s step 3 with its checks
(compressed size non-zero and inside the file, the decompressed size equal
to the table's).

### Refusing a wrong game or a bad dump

`bw_rom_identify` refuses outright what is not a cartridge image at all: an
archive (ZIP, 7-Zip, RAR, gzip; "unpack it and choose the .nds file
inside"), a file smaller than a header, a file larger than any cartridge.
Everything else is identified and `bw_rom_verdict` decides:

| Verdict | When | The player is told |
|---|---|---|
| `OK` | the known good dump, or it trimmed of padding only | proceed |
| `NOT_DS` | unknown game code and a wrong header checksum | not a DS cartridge image |
| `OTHER_GAME` | a valid DS header, another game | the title and game code found, and what is needed |
| `UNSUPPORTED` | Pokemon White, or Black of another region | which release it is, and that only IRBO is supported |
| `DAMAGED` | Black with a wrong header checksum, or cut short by more than padding | dump the cartridge again (without trimming) |
| `MODIFIED` | Black, whole, another SHA-1 | damaged, patched (a hack or a translation) or another revision; both hashes shown |

A trimmed dump is accepted when padding it back with 0xFF to the capacity
the header declares (byte 0x14) gives the known hash: trimmers cut only the
0xFF tail, and on this cartridge everything past 0x0C45D400 is 0xFF. Only
`OK` is extracted; `eidos_extract --any` overrides the verdict for testing
(the extraction's own structural checks still apply).

### An existing output folder

The choice is **reuse a finished extraction, never resume a partial one**.
Extraction takes seconds, and a resumed run would have to trust files a
killed run may have half-written; restarting is both simpler and safer.

| `out_dir` is | Result |
|---|---|
| absent, or an empty folder | extracted into |
| an extraction of the same image by this installer version | left alone, success, "already extracted" |
| an extraction of the same image by another installer version | replaced (the layout changed, `BW_INSTALL_VERSION`) |
| an extraction of another image | refused, unless `replace` (`--replace`) |
| a file, or a folder with files eidos did not write | refused; the installer never deletes what it did not create |

"Finished" means `eidos_manifest.json` is present: it is the last file
written, and it records the installer version and the image's SHA-1.

### Atomic output

Everything is written into `<out_dir>.partial`, a sibling (so the final
rename never crosses file systems), starting with a marker file
`.eidos_partial` and ending with the manifest. The marker is then removed and
the folder renamed to `out_dir`; when replacing, the old extraction is first
renamed to `<out_dir>.old` and deleted after the swap (restored if the swap
fails). On any error the staging folder is deleted. A staging or set-aside
folder left by a killed process is recognised by its marker or manifest and
removed on the next run; one that is not recognisably eidos's is refused,
never deleted.

### Paths and progress

Paths are UTF-8 everywhere. On Windows they are converted to UTF-16 for the
wide API, and long ones are made absolute with the `\\?\` prefix so the
260-character limit does not apply; `eidos_extract` reads its arguments
from the wide command line. Renames and deletes on Windows retry for up to
two seconds on sharing errors, which antivirus scanners and OneDrive cause
on files that have just been written. Removal never follows a link or
junction.

Progress is a callback: bytes for the hash (`what` = "hashing"), KiB for
the extraction with the file just written.

### Not done in the core yet

- Cancellation: the progress callback returns nothing; a wizard needs it to
  return "stop" (the staging cleanup already makes stopping safe).
- A free-space check before extracting (about 210 MB are written).
- Reading the image from an open descriptor rather than a path, for
  Android's content URIs (see below).
- Per-file hashes in the manifest, so the engine can verify its data
  without the ROM.
- White, other regions, other revisions: one line each in the known-dump
  table once the port supports them.

## The first-run wizard (design)

One flow on every platform; only the file picker, the data location and the
window around it differ.

1. **Welcome**: one screen saying eidos needs the player's own Pokemon Black
   ROM, what a ROM is, and that nothing is downloaded.
2. **Pick the ROM**: the platform's file picker, filtered to `.nds` but not
   restricted to it (dumps arrive with other extensions). Desktop also
   accepts drag and drop onto the window and a `--rom <path>` argument. The
   wizard never scans the disk for ROMs.
3. **Identify**: `bw_rom_identify` with a progress bar ("Checking your
   ROM"), then `bw_rom_verdict`. On `OK`, continue; otherwise show the
   verdict's message with one button, "Choose another file". The wizard
   offers no override: `--any` stays a command-line tool for developers.
4. **Extract**: free-space check, then `bw_rom_extract` into the data root
   with a progress bar and a Cancel button, passing the hash from step 3.
5. **Done**: start the game.

The ROM itself is never copied. Desktop builds remember its path in the
config file, so a later installer version that changes the layout can
re-extract without asking (if the file is still there and still hashes the
same); mobile builds read it once.

### Where the data goes

The data root is the extraction folder (`data/` inside it is what the engine
opens as `BW_DATA_ROOT`). It is re-derivable from the ROM, so it goes where
an operating system keeps large local application data, never in a roaming,
synced or backed-up location, and never in a cache directory the system may
purge.

| Platform | Data root | Notes |
|---|---|---|
| Windows | `%LOCALAPPDATA%\eidos\extract` | `SHGetKnownFolderPath(FOLDERID_LocalAppData)`; Local, not Roaming, so 210 MB do not roam. A `portable.txt` beside the executable switches to `<exe dir>\extract` for USB-stick installs |
| macOS | `~/Library/Application Support/eidos/extract` | `NSApplicationSupportDirectory`; inside the app container when sandboxed, which the same call returns |
| Linux | `$XDG_DATA_HOME/eidos/extract` (default `~/.local/share/eidos/extract`) | Flatpak and Snap set `XDG_DATA_HOME` to their own per-app folder, so the same rule holds |
| Android | `Context.getFilesDir()/eidos/extract` | app-private internal storage: no permission needed, removed with the app; not shared storage, which scoped storage makes slow and permission-bound |
| iOS | `Library/Application Support/eidos/extract` in the app container | marked `NSURLIsExcludedFromBackupKey`, since it is re-derivable and large |

The engine's resolution order is: `--data <dir>` on the command line, then
the `BW_DATA_ROOT` environment variable (what the tests and developers use),
then the path saved in the config file, then the platform default above.
The mod overlay root (see "The file layer" in the README) is a separate
folder beside it, `.../eidos/mods`.

### Picking the ROM, per platform

| Platform | Picker | What the core receives |
|---|---|---|
| Windows | `IFileOpenDialog` | a UTF-16 path, converted to UTF-8 |
| macOS | `NSOpenPanel` (security-scoped when sandboxed) | a UTF-8 path, while the scope is open |
| Linux | the XDG desktop portal's file chooser (`org.freedesktop.portal.FileChooser`), falling back to GTK's `GtkFileChooserNative` | a path (under Flatpak a `/run/user/.../doc/` path the portal grants) |
| Android | Storage Access Framework, `ACTION_OPEN_DOCUMENT` | a `content://` URI with no path: the front end opens it with `ContentResolver.openFileDescriptor`, and the core needs an entry point taking an open `FILE *` or descriptor (`fdopen` of the detached fd); planned |
| iOS | `UIDocumentPickerViewController` (open in place) | a security-scoped URL; between `startAccessingSecurityScopedResource` and `stop...` its path opens with `fopen` |

The port is on SDL2 today, which has no file dialog; SDL 3's
`SDL_ShowOpenFileDialog` covers Windows, macOS and Linux with one call and
returns UTF-8 paths, so the desktop wizard can be one SDL screen once the
platform layer moves to SDL 3 (until then, the three native calls in the
table behind one small shim). Android and iOS need their native pickers
through that shim either way.

### When the data is missing

At start-up, before the game's own boot, the engine checks the data root:
`eidos_manifest.json` present, its `version` equal to the engine's
`BW_INSTALL_VERSION`, and the files it opens first present
(`bw_data_exists`).

- **No data root, or no manifest**: a build with a window opens the wizard;
  a headless build (tests, servers, `--no-window`) prints where it looked
  and the `eidos_extract` command that would fill it, and exits with a
  non-zero status. The engine never starts the game on partial data.
- **An older installer's extraction**: re-extract from the remembered ROM
  path if it is still there and hashes the same, with a progress screen;
  otherwise ask for the ROM again with a sentence saying why.
- **Another image's extraction** (the manifest's `rom_sha1` is not a known
  good dump): the wizard, as for missing data.
- **A file missing during play** (deleted by hand, a failing disk): the
  file layer stops with a message naming the file and telling the player to
  run the installer again, not a crash inside the game's code.

The wizard writes only into the data root (through the staged rename) and
the config file; the player's saves live elsewhere and are never touched by
an installation or a re-extraction.
