# Files, overlays and the first-run extractor

## The file layer

The port's data pipeline starts with `sdk/vfs.c`, the one place a
NitroFS-relative path becomes a host path. It owns the **data root**
`tools/ndsrom.py`'s `extract` wrote (the player's own ROM dump) and a list
of **mod roots**, each laid at the same relative layout, in a load order in
which a later mod wins. `BW_DATA_ROOT` and `BW_MOD_ROOT` set them from the
environment (`BW_MOD_ROOT` may list several roots separated by `;`, on every
host, since a Windows drive letter uses `:`), and an explicit
`bw_set_data_root` / `bw_set_mod_root` call wins over both. `sdk/vfs.h` has
the rest of the API: `bw_add_mod_root`, `bw_clear_mods`, the list in load
order, and the merge calls below.

A mod overrides game files at two granularities:

- **Whole files.** `<mod>/a/0/1/6` as a regular file replaces the NitroFS
  file `a/0/1/6`. A path resolves to the latest layer holding a regular file
  there, the data root last (`bw_data_path`, `bw_data_exists`).
- **Single archive members.** `<mod>/a/0/1/6/` as a *directory* holds loose
  members: a file named `<index>` or `<index>.<ext>` (`12`, `0012.bin`,
  `12.nclr`; the extension is free, leading zeros are allowed) replaces
  member 12 of the archive, or **adds** it when the archive is shorter, so a
  mod grows an archive (a new Pokemon, move, map) without repacking it.
  Members past the original end that no mod fills are empty. Other names in
  the directory (`README.txt`, `12.bin.bak`) are ignored; two files naming
  the same index in one mod are an error, reported on stderr, and the
  archive then fails to open rather than pick one. The member file holds the
  member's own bytes in the game's format (an NCGR, a compressed blob, a
  data record); converting from a readable format is a tool's job, not the
  file layer's.

Load order. Mods load in the order given, re-sorted stably by the `order`
key of each mod's optional manifest, `<mod>/eidos_mod.txt`:

    name  = Fairy type    # shown by tools; default: the directory name
    order = 10            # signed integer, default 0; higher loads later

For one archive the rule is a walk through the layers in load order: the
latest layer holding a regular file supplies the base archive (a whole-file
replacement discards every earlier mod's members of that archive), and
every later mod's member directory is applied on top of it, a later mod
winning for the same index. A member directory with no file under it in any
layer is just a directory, not a new archive (`a/0/1` in a mod tree holds
the replacement files `a/0/1/0..9`, whose names look like indices); a
brand-new archive ships as a whole NARC. The format's own limit stays:
BTAF's count is a u16, so 65535 members.

How every reader sees it. The game's readers are matched code
(`func_02048E34`, `func_02049578` and the rest): they read the NARC's header,
BTAF and GMIF through `FS_SeekFile`/`FS_ReadFile` and trust the
`imageBase`/`fatOffset`/`count` fields filled from the same bytes. So
`sdk/vfs_narc.c` synthesises the merged archive as a complete NARC in
memory, laid out exactly as the cartridge's are (header, BTAF, BTNF, GMIF,
4-byte member alignment padded with 0xFF; repacking any of the 234 real
archives from its own tables reproduces it byte for byte), and the FS shim
serves it as a memory file under every open: `FS_OpenFile` by path,
`bw_fs_open_id` / `FS_OpenFileFast` by file id, `FS_OpenFileDirect` by ROM
range (a range covering the whole file opens the whole merged file), the
archive id table, and `bw_arc_open`, which now opens through
`bw_fs_open_rel`. Merged images are cached per path with a reference count
held by the open files (the `FSFile` record stores the cache's index, never
a pointer, so the 0x48-byte rule holds), dropped on any change to the roots
or the mod list, and kept alive for a file still open across one.
`bw_narc_pack`, `bw_vfs_build_merged` and `bw_vfs_member_overrides` are
callable directly, for tests and mod tools. Two limits, stated: the ROM FAT
the boot copies (`FS_LoadArchiveTables`) still holds the cartridge sizes,
and a strict sub-range of an archive opened by ROM offset keeps the
cartridge layout's offsets (no matched caller does either with an archive).
Paths are narrow strings throughout, as fopen takes them;
`sdk/plat_dir.c` is the one platform file (Win32 and POSIX) that tells a
file from a directory and lists one.

`tests/test_vfs.c` checks whole-file resolution (13 checks).
`tests/test_vfs_members.c` builds a base tree and seven mod trees and reads
every case three independent ways: `sdk/narc.c`'s reader plus the matched
`func_02049578` on its handle, the matched readers by archive id
(`func_02048DDC`, `func_02048E34`, `func_02048F4C`) over a synthetic name
table, and `FS_OpenFile` of the archive, whose whole image must equal a NARC
packed from the expected member list. Cases: no mods, an empty mod, a
replaced member, members added past the end with a hole, two mods on one
member in both load orders, a manifest's `order` and `name`, a whole file
superseding earlier member directories and a member directory on top of a
whole file, a duplicate index, members over a non-NARC, and an open file
surviving a mod list change. With game data it also repacks all 234 real
archives byte for byte, then replaces member 5 of the real `a/0/1/6` and adds
one past its end, and reads both through the real ARM9 archive table and
`func_02048E34` / `func_02048F4C`, by file id and by ROM range (**369 checks
in both pointer widths**).

The other two items of the milestone are the
first-run extractor (below) and booting the game from the extracted tree:
the game's own `FS_Init` and overlay loads now reach the extract through
`sdk/fs.c` and `sdk/overlay.c` (the sections below), and the boot that
calls them is the work `docs/boot.md` plans ("Running the game's boot").

### Archives by id, the way the game opens them

Every asset the game loads goes through one family of matched readers that
take an **archive id** rather than a path: `func_02048DDC` opens archive N
through the name table the boot installs (`func_02048CEC(0x020A6C58, 0xEB)`
in `src/matched/sub_0200545C.c`: 235 ARM9 addresses of the strings
`a/0/0/0` to `a/2/3/4`), `func_02048E34` resolves one member's extent out of
the NARC's own tables, and `func_02048D00` / `func_02048F4C` read a member
into a buffer. `src/matched/unit_02048DDC.c` is now in the build (it was the
last stub under the two readers already there), and the port supplies the
bottom of it:

- `bw_arc_install_table` is the boot's call: it points `g_02146A68` at the
  table inside the ARM9 image. The object is declared the way the reader
  declares it (`u32 * volatile`), so it works at either pointer width, and
  the entries stay DS addresses.
- the open-by-path SDK call at 0x02078C18 resolves such an address through
  the ARM9 window to its string and the string through the file layer, **mods
  first**, so a mod's replacement archive, or the merged archive its member
  overrides make, is what the game's own readers see. aletheia's `layout/symbols_sdk.txt` now names 0x02078C18
  `FS_OpenFile`, but the matched file still calls it `CRYPTOi__BN_lshift1`,
  the name an earlier signature scan gave it, and passes the path as a DS
  address. The port defines that spelling as the address form of
  `FS_OpenFile` and records the history in `sdk/fs.c`.
- `bw_arm9_set_image` installs an ARM9 image from memory, for a caller that
  already holds it (the first-run extractor, a test).

`tests/test_arcid.c` needs no game data: it writes three NARCs and a mod
copy of one, and a synthetic ARM9 image with its own three-entry table, then
reads every member by id through the matched readers and checks every byte
against what it wrote and against `sdk/narc.c`'s reader, which shares no
code with the matched one. It also covers D00's byte-offset mode, the
out-of-range member reaching the game's assert sink, the mod overlay winning
and falling through, and the open shim's refusals. **82 checks in both
pointer widths.**

### The ROM file system the boot sets up

The boot calls `FS_Init(1)` and then sizes and fills the FAT/FNT cache through
the matched `FS_TryLoadTable` (aletheia files it as `dwc_startutilityex.c`),
which bottoms out in `FS_LoadArchiveTables` (`func_0207A5C8`). aletheia
matches all of libfs, but every path through it ends in a cartridge read, so
`sdk/fs.c` is a shim over the extracted tree with the SDK's surface:
`FS_Init`, `FS_SetDefaultDMA`, `FS_InitFile`, `FS_OpenFile`/`FS_OpenFileEx`
by path, `FS_ConvertPathToFileID` + `FS_OpenFileFast` by file id,
`FS_OpenFileDirect` by ROM range, `FS_CreateFileFromMemory`, seek, read,
length, position and close, under the SDK names and the `func_<address>`
spellings the matched callers use.

- The extract's `fat.bin` and `fnt.bin` (beside the data root) are the tables.
  Paths resolve through the FNT the way the SDK resolves them (`rom:` prefix,
  leading slash, ASCII case, `.` and `..`), and a file id resolves back to its
  NitroFS path and then through the file layer, mod overlay first.
- `FS_LoadArchiveTables` answers the SDK's own size,
  `(fat + fnt + 32 + 31) & ~31`, and copies both tables into the block at its
  first 32-byte boundary, so the boot's arena block has its DS size and DS
  contents and every later arena address keeps its DS distance.
- `FS_OpenFileDirect` turns a ROM range into the FAT entry that holds it and a
  window into that file, which is what lets a reader built on ROM offsets run
  over a directory of extracted files.
- The 0x48-byte `FSFile` rule is unchanged: the record in it holds an index
  into a table of open host files or buffers, never a pointer.

`tests/test_fs.c` checks all of it against a hand-built four-file tree and
hand-built tables (no game data), and, given a data root, round-trips every
named file id of the real FNT, opens real archives by path and by ROM range
and compares their bytes, and checks the table cache against the two files.
**111 checks in both pointer widths.**

### Overlays

In the port an overlay is compiled in, not loaded. `sdk/overlay.c`
implements `FS_LoadOverlayInfo`, `FS_LoadOverlayImage`, `FS_StartOverlay`,
`FS_EndOverlay`, `FS_UnloadOverlayImage`, `FS_LoadOverlay` and
`FS_UnloadOverlay` (and `func_020792B8`, the unload's spelling in the game's
overlay manager, `sub_02034A3C`) over a registry with one entry per overlay:
the overlay table's own eight words, every function aletheia's metadata knows
in it with its status (open, matched, present in this build), and its static
initialisers.

- `FS_LoadOverlayInfo` returns the same eight words as the ROM's `y9.bin`,
  plus the file's ROM extent from the FAT.
- Loading an overlay that is not all present **panics at the load**, naming
  the overlay, its RAM range, the lowest-addressed missing function and
  whether aletheia has matched it, and the overlay's coverage:
  `overlay 9 (0x02155160..0x021A8260) is not in this build: first missing
  function func_02155168 at 0x02155168 (matched in aletheia, not compiled
  into this build); 0 of 1675 known functions present, 1675 matched`.
- Loading a present overlay calls the image hook
  (`bw_overlay_set_image_hook`), which must put the overlay's data and .bss
  back to load-time state, then runs its static initialisers, then marks it
  resident and evicts any resident overlay sharing its RAM. With no hook the
  load panics rather than running code over stale data.

The table is generated at build time by `tools/overlay_table.py` from
aletheia's `layout/layout.json`, its manifest shards, `layout/symbols_sdk.txt`
and the `symbols:` blocks and externs of the matched overlay sources, plus the
build's own source list (which decides "present"). Addresses, sizes and names
only. It is generated rather than committed because its coverage columns move
with every aletheia wave and every source the build adds; `python
tools/overlay_table.py --summary` prints it (237 overlays, 29,374 known
functions, 28,458 of them matched in aletheia; `present` comes from the
build's own source list).

Two limits, stated so nobody relies on more. The function list is a **lower
bound**: aletheia commits no per-overlay list of function starts, only the
matched ones, so a function nothing matched names is invisible here, and an
overlay could be reported present while one is missing. And 1046 function
names repeat across overlays that share RAM (`func_02155244` is in ov9 and
ov10), so compiling two such overlays into one executable needs per-overlay
symbol names: `tools/ovlink.py` gives each compiled overlay its own
(`ov010_func_02155244`), as the next section describes. Only ov230 has static
initialisers (five; aletheia's `docs/MATCHING.md`), and the generator refuses
any other non-empty list rather than guess.

`tests/test_overlay.c` checks the registry against a synthetic four-overlay
table, checks the generated table's shape, and, given a data root, compares
all 237 `FS_LoadOverlayInfo` headers word for word with `y9.bin`, the file
extents with the FAT, and ov230's initialisers with its decompressed image.
`test_overlay_refuses` checks the panic message. **70 checks in both pointer
widths.**

#### Overlays compiled in

`docs/overlays.md` is the design. `tools/ovlink.py` compiles a set of
overlays into one executable (`eidos_overlays()` in `cmake/overlays.cmake`;
`EIDOS_OVERLAYS` puts one into `eidos_boot`, the title path by default when
`EIDOS_BOOT` is on):

- each overlay's names are renamed `ovNNN_<name>` by a forced-include header,
  so overlays sharing a window link side by side; a call into a window from
  outside it goes through a generated trampoline that asks which overlay is
  resident and calls its function, or panics naming what is there;
- loading one copies its file from the player's `extract/overlays_dec/` into
  its window in the port's main RAM, zeroes the `.bss`, binds the overlay's
  own data names and relocates the pointer words the metadata lists, the
  ARM9 image's model (`docs/armdata.md`) applied at load time
  (`sdk/overlay_image.c`);
- a function the build lacks (unmatched, an mwcc `asm` body) is a stub that
  panics naming the overlay and the address, so the loader's presence check
  is per function: a compiled overlay with stubs loads and runs until it
  reaches one.

The title set (ov009, ov010, ov011, ov013, ov017, ov228, ov088, ov179) is
about 3200 matched sources (aletheia's count on 2026-10-01; it grows with
every wave), 30 `asm` files and 2 files that call another file's `static`
functions left to stubs, 340 trampolines; it compiles warning-free and
links at both pointer widths, and `test_ovlink_title` loads all eight from
the extract, every listed pointer word checked against the player's files.
`tests/test_ovlink.c` checks two synthetic overlays sharing one window, both
loaded alternately and reached from ARM9 code and from a third overlay;
`tests/test_ovlink_real.c` loads ov088 and ov179 from the extract and runs a
matched function of each.

## The first-run extractor (install/)

The end state is an installer: the player gives it their own ROM (and,
optionally later, a DSi BIOS) and gets a playable game on Windows, macOS,
Linux, Android or iOS. `install/` is the part of that which is the same on
every platform, plain C99 with no SDL and nothing of the game, plus one
small file of OS calls (`install/fsutil.c`, Win32 and POSIX):

- `bw_rom_identify`: the header (title, game code, unit code, checksum,
  capacity) and a streamed SHA-1 of the whole image (`install/sha1.c`, FIPS
  vectors in the test), plus the hash of a trimmed image padded back to
  its capacity;
- `bw_rom_verdict`: whether eidos takes the image and, if not, why, in
  words a player can act on: not a DS image, another game, White or another
  region, a damaged header or a dump cut short, a modified dump. A ZIP or
  7-Zip archive is named as one. A dump trimmed of padding only is accepted;
- `bw_rom_extract`: the tree aletheia's `tools/ndsrom.py extract` writes
  (`data/` NitroFS files, ARM9/ARM7, FNT/FAT, both overlay tables and their
  files, orphans, the banner, the DSi regions) plus what `tools/delink.py`
  derives: `arm9_dec.bin` and `overlays_dec/`, BLZ-decompressed by
  `install/blz.c` exactly as `tools/blz.py` does. `eidos_manifest.json`
  records which image and which installer version. The output is atomic
  (staged in `<out>.partial`, renamed into place when complete); a finished
  extraction of the same image is reused, an interrupted one restarts, and
  a folder holding anything eidos did not write is refused, never deleted;
- the ROM is untrusted input: every offset is checked, the whole plan is
  validated (bounds, overlaps, a cyclic FNT) before a single file is
  written, names are sanitised exactly as ndsrom.py sanitises them, and
  paths are UTF-8 on every platform (the wide API on Windows, with the
  long-path prefix past 200 characters).

`eidos_extract [--any] [--replace] <rom.nds> [out_dir]` is the command-line
front end. **On the real ROM it writes 726 files, all 726 identical (size
and SHA-1) to aletheia's own extract/**, with nothing of that tree missing
but four files written on purpose by aletheia's tools only (rebuild
bookkeeping, the DSi-key-only `arm9i_dec.bin`, a disassembler view). It
takes about 6 s with an optimised build on this machine, half of it the
hash. `docs/installer.md` has the details, the intended differences, and the
design of the per-platform first-run wizard (where the data goes, how the
ROM is picked, what the engine does without data).

**`tests/test_extract.c`** needs no game data. `tools/synth_rom.py` builds
a synthetic DSi-enhanced cartridge from a fixed seed (nested directories,
names that need sanitising, overlays in both tables, a BLZ-compressed ARM9
overlay, an orphan, an empty FAT slot, a zero-length file, an animated
banner, both digest tables, a residue blob, a correct header checksum, an
ARM9 packed by aletheia's own `blz.py`) and runs aletheia's
`ndsrom.py extract` and `blz.py` on it. The C extractor's output, written
into a folder named outside ASCII, is compared with that **file for file by
SHA-1: 28 of 28 identical**. Around it: the SHA-1 and BLZ vectors, 17 name
cases, every verdict, the padded hash of a trimmed image, the existing-folder
rules (foreign files, empty, another image, an older installer, a killed
run's staging), 20 broken images each refused with the right message and
nothing left behind, and 300 randomly damaged images (43 extracted, 257
refused, none crashing, run under ASan by `tools/check.py`). 148 checks.

**`test_extract_rom`** (`tests/extract_rom.cmake`) runs `eidos_extract` on
the ROM the `BW_ROM` environment variable names, into a folder named
`extract é€ ü`, and compares every file with the extract/ tree that holds
`BW_DATA_ROOT`; the expected hashes are computed from that tree each run,
never stored in the repository. It also checks that a second run is a no-op
and no staging is left. Without `BW_ROM` it reports itself skipped.

### First run: the setup window

On top of the core, `install/setup.c` decides where the data root is and
whether it is playable, and `engine/platform/wizard.c` asks for the ROM
when it is not. A front end calls `eidos_firstrun` before anything else;
`demo/eidos_view` does, so it now starts the way the game will:

- the data root is `--data-root`, else `BW_DATA_ROOT`, else the folder
  saved in `config.txt`, else the platform default (`%LOCALAPPDATA%\eidos`,
  `~/Library/Application Support/eidos`, `$XDG_DATA_HOME/eidos`, the app's
  internal storage on Android; a `portable.txt` beside the executable
  keeps everything beside it);
- it is playable when its manifest names this installer version and the
  known good dump and the first files are there (a folder named explicitly
  needs only the NitroFS tree, so aletheia's own extract/ still runs);
- when it is not, a window opens: one SDL screen drawn by eidos with its
  own bitmap font, the same on every platform. It says what is needed,
  takes the ROM from the platform's own file dialog (`install/picker.c`:
  `IFileOpenDialog` on Windows, `NSOpenPanel` on macOS, the desktop portal
  then zenity or kdialog on Linux) or dropped on the window, checks and
  writes it on a worker thread with a progress bar and Cancel, and says in
  plain words why a file was refused (an archive, another game, White, a
  damaged or modified dump, no space);
- `--rom <file>` does the same without a window, `--no-window` prints
  what is missing instead of opening one, `--reset-data` deletes the data
  eidos made, `--move-data <folder>` moves it.

`test_dataroot` (every configuration, MSVC included) and `test_wizard` (the
window under the dummy video driver, driven by scripted drops, clicks and
keys) need no game data; with `BW_ROM` set, `test_wizard` also installs
the real ROM through the window and `test_firstrun_rom` runs the headless
path (install, reuse, move, reset) through `eidos_view`. `test_picker`
opens the real Windows dialog and answers it from a helper thread when
`EIDOS_TEST_DIALOGS=1`. `docs/installer.md` has the rest.
