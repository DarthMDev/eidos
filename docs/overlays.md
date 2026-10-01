# Overlays compiled into one executable

Everything past the boot's ARM9 code lives in overlays: 237 files the game
copies, one at a time per window, into fixed RAM windows. The title path alone
loads eight of them (ov009, ov010, ov011, ov013, ov017, ov228 during the boot,
ov088 for the save check and the title menu, ov179 for the opening cinematic;
`docs/title-closure.md`). This document is how the port compiles a set of
overlays into one host executable and loads them the way the DS does.

Pieces:

| File | What |
|---|---|
| `tools/ovlink.py` | the generator: per-overlay namespaces, trampolines, stubs, data tables |
| `tools/ovlink_exclusions.txt` | matched overlay sources left out on purpose, with the reason |
| `cmake/overlays.cmake` | `eidos_overlays()`: run the generator, compile the set |
| `sdk/overlay_image.h`, `sdk/overlay_image.c` | the runtime: the overlay files, load, trampolines' and stubs' panics |
| `sdk/overlay.c` | the loader; `bw_overlay_set_provider` is its hook for compiled overlays |
| `sdk/armdata.c` | the copy, bind and relocate steps, shared with the ARM9 image |
| `tests/test_ovlink.c`, `tests/ovl_synth/` | two overlays in one window, synthetic |
| `tests/test_ovlink_real.c` | ov088 and ov179 from the player's extract |

## The problem

On the DS an overlay's code runs from its window, and the linker gave each
overlay its own symbols. Overlays that share a window define the same names:
`func_02155244` is one function in ov009 and another in ov010, and about a
thousand names repeat this way. A host executable has one global namespace
and no windows, so compiling two overlays that share a window means renaming,
and a call into a window from outside it means asking which overlay is there.

## Names: a namespace per overlay

`tools/ovlink.py` writes a forced-include header for each overlay
(`include/ovNNN.h`) and one per translation unit that includes it. The
matched files are not edited (portability rule 1). For overlay N:

- every external name one of its sources defines, and every name its sources
  use whose address lies inside N's window, becomes `ovNNN_<name>`
  (`#define func_02155244 ov010_func_02155244`);
- a spelling that differs from the defining file's (an SDK name for a
  `func_` address, `sub_` for `func_`) is renamed to the definition, by
  address: inside the overlay to `ovNNN_<definition>`, and towards ARM9 to
  the name the rest of the link defines at that address;
- a name whose address lies in another overlay's window keeps its plain
  spelling: the plain name is a trampoline (below);
- ARM9 names, runtime helpers and port objects are left alone.

The address of a name comes from the file's own `symbols:` block, else the
address spelled into the name, else `layout/symbols_sdk.txt` (the overlay's
own region first). Which names a file defines, declares and calls is read
the same way the boot link reads the ARM9 sources.

Two things a host compiler needs that mwcc did not:

- a call with no declaration in scope (a declaration inside a function
  body counts as one) gets `int name();` in the unit's
  header (C89's implicit declaration, the one the DS code was compiled
  against; a definition returning 64 bits gets `long long`), as
  `tools/bootlink.py` does for the boot;
- mwcc's builtins `__builtin__clear` and `__memcpy`, called undeclared by a
  few SDK files, map to `bw_mwcc_clear` and `bw_mwcc_memcpy`.

## Calls into a window: trampolines

A name whose address lies in a window the referring code does not own (ARM9
code calling `func_021DFC20`, ov002 calling ov010) is defined once, in the
generated `ovl_dispatch.c`, as a trampoline: it asks `bw_ovl_resident_at`
(the loader's residency, over the window `[ram_address, ram_address +
ram_size + bss_size)`) which overlay is there and calls that overlay's
function. With nothing resident, or a resident overlay this build does not
compile, it panics naming the address and what is resident. Code pointers
the overlay's data holds into another window, and ARM9 sources' calls into a
window, take the same trampolines; ovlink scans the link's other sources for
the latter.

A trampoline forwards its arguments, so it has a prototype: the definition's
of the first compiled overlay that has a function at the address, normalised
to types every host passes alike (every pointer, data or function, is
`void *`; every integer typedef its C type). An overlay whose function there
has another normalised prototype panics in the trampoline instead of being
called through the wrong type; a prototype that cannot be normalised (a
struct by value, a float, `...`) makes a trampoline that only panics.

## Code addresses written as literals

`(void *)0x021B6279` in an ov179 source is a Thumb function's address passed
as a callback. `tools/dsaddr.py` rewrites every DS-range literal cast into
`bw_ds_ptr`, which would hand out the copied code bytes. For a source that
casts a known function address (a Thumb address, or a function start the
metadata knows), its header renames `bw_ds_ptr` to the overlay's translator
`bw_ovl_ds_ptr_ovNNN`, which answers the host function for those addresses
(the overlay's own, an ARM9 function, or a trampoline) and `bw_ds_ptr` for
every other. Casts in static initialisers are not rewritten by dsaddr at all
and stay as they are.

## Data

An overlay's data is not a set of host objects. The overlay's file is copied
to its window in the port's flat main RAM at every load, the same model as
the ARM9 image (`docs/armdata.md`), so a name, a literal cast and a pointer
read out of a table reach the same bytes, and whichever overlay was loaded
last is what the window holds, as on the DS. A load is, in
`sdk/overlay_image.c`:

1. `bw_data_load`: copy the file (`extract/overlays_dec/y9_NNNN.bin`, or
   `extract/overlays/y9_NNNN.bin` for an overlay stored uncompressed) to
   `[ram_address, ram_address + ram_size)` and zero the `.bss`;
2. `bw_data_bind`: point every data name the overlay's sources use at its
   address. Each overlay has its own pointer variables
   (`#define d_021BD8E8 (*bw_dsref_ov088_d_021BD8E8)`), including for ARM9
   names it uses, so no two overlays and no overlay and the ARM9 image ever
   define the same variable. Bindings are DS (`bw_ds_ptr`, which returns a
   registered object where one owns the address), HOST (a port object) or
   CONST (a linker constant such as `ov228_id`), as for ARM9;
3. `bw_data_relocate`: check each listed pointer word against the metadata
   and rewrite it: to the overlay's own function (`ovNNN_<name>`, or its
   stub), to an ARM9 function the link defines (a trap that panics if not),
   to a trampoline for another window, or to `bw_ds_ptr` of a data target.

Unloading needs nothing: the bytes stay until the next overlay is copied over
them, and the loader stops calling the overlay resident.

Which pointer words are listed. aletheia's `data_objects.json` describes the
ARM9 image and ov203 only; for overlays the listing annotations of
`asm/overlays/ovNNN.s` are the source, and they mark code pointers (`->
func_`, `-> veneer_`) and a few literal-pool targets (`-> pool_`), not
pointers from overlay data to data. A data-to-data pointer word the metadata
does not list stays a DS address in the copy; nothing guesses at it. That is
a metadata limit to close in aletheia (a `data_objects.json` region per
overlay), not something the runtime can repair.

Pointer width: as for ARM9, a relocated word holds a host pointer in the
DS's 32-bit slot, which only fits where pointers are four bytes. On an LP64
host the copy and the bindings work and the relocation is refused; the load
panics unless the set was installed with `BW_DATA_ALLOW_UNRELOCATED`.

A name a matched file also uses as a member, tag, local or in a static
initialiser cannot be a macro over a pointer; it becomes a host object of
its own (`ovNNN_<name>`), filled from the window at load. Unlike the ARM9
case it is **not registered** at its address: the window passes to the next
overlay and a registration cannot be withdrawn (`sdk/dsmem.h` has no
unregister). Writes through such a name do not reach the window, and
reads through it see the window as it was at load. ovlink's report lists
every one: the title set has 23 (16 in ov009's network library, mostly SDK
state such as `SOCLiConfigPtr`; 2 in ov010, one of them the ARM9 address
`d_02148BCC`; 5 in ov013). Closing this needs either a
`bw_ds_unregister` in `sdk/dsmem.h` (then the objects could be registered at
load and withdrawn when the window passes on) or a narrower conflict test
than armdata's deliberately broad one.

Data defined by a matched overlay source (a `const` table written in C)
is a host object under the overlay's namespace, not the window's bytes; the
same report lists them.

## What the build lacks: stubs

Every function known in a compiled overlay that no compiled source defines
is a stub `ovNNN_func_XXXXXXXX` that panics with the overlay and the
address. "Known" is the listing's `.global` function labels, the manifest
shards, the SDK symbols in the overlay's region, every address the sources
or the data reach in the window, and the static initialisers. A source left
out (an mwcc `asm` body, or a line in `tools/ovlink_exclusions.txt`) leaves
its functions to stubs. So does a source that declares a `static` function
it uses and does not define (mwcc's link resolved it against another file's
static definition; a host compiler cannot): ovlink leaves it out by itself
and names the functions in its report (two files of ov009's network
library in the title set). So a partly matched overlay links and runs until
it reaches one.

The loader's presence check is therefore per function, not per overlay: for
an overlay compiled into the executable (`bw_overlay_set_provider`, which
`bw_ovl_install` connects) `FS_LoadOverlayImage` skips the all-present check
and loads through the provider; an overlay that is not compiled in keeps the
old behaviour (the panic naming the first missing function, or the image
hook).

ARM9 functions the overlays call that the rest of the link does not define
are listed in `ovl_arm9_needs.txt`; the standard C functions among them
(`memcpy`, `strlen`, `sprintf`, ...: the ARM9's MSL copies on the DS) are
the host C library's and never stubbed. With `--arm9-stubs`
(`EIDOS_OVERLAYS_ARM9_STUBS`, the default) each is a stub too, which is what
lets a set link without the boot.

## Static initialisers

Only ov230 has any (five, `tools/overlay_table.py`'s `KNOWN_SINIT`). A
compiled overlay carries its initialisers as `ovNNN_` functions (stubs when
unmatched), and `FS_StartOverlay` runs them through the provider, in table
order.

## Building

```cmake
eidos_overlays(<name>
    TARGET <executable>
    OVERLAYS 88 179            # or ALL, or TITLE
    LINK_SOURCES <every other C source in the link>
    [ARM9_STUBS])
```

and before the first load, `bw_ovl_install(&bw_ovl_set_<name>, flags)`.
`EIDOS_OVERLAYS` (ids separated by `;`, `ALL`, `TITLE`; empty means `TITLE`
when `EIDOS_BOOT` is on) puts a set named `boot` into `eidos_boot`.

The generator runs at configure time and writes under
`<build>/ovl/<name>/`; `ovl_report.txt` there has the counts and every
decision worth a second look (excluded sources, host objects, unresolved
names, code literals, trampolines). Each matched source is rewritten by
`tools/dsaddr.py` into that directory rather than the shared `dsaddr/`
tree, because two sets may compile one file with different headers.

## Joining the boot link

The boot link (`tools/bootlink.py`, `cmake/boot.cmake`) and this one meet in
three places:

- the trampolines in `ovl_dispatch.c` define plain names (`func_021DFC20`)
  that the boot's ARM9 sources call and that bootlink would otherwise stub as
  open: bootlink has to see `ovl_dispatch.c` as a port source;
- the ARM9 functions in `ovl_arm9_needs.txt` marked `missing` are what the
  boot's closure has to grow by for the overlays to run without ARM9 stubs;
- eidos_boot has to call `bw_ovl_install(&bw_ovl_set_boot, 0)` before the
  first overlay load.
