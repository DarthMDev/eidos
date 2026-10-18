# Seam cost

Measurements for the two mod-seam mechanisms `docs/mod-interface.md` weighs:
a uniform slot for every cross-unit call, and a slot for a declared set only.
Every figure below is attributed to the file or command that produced it.
Estimates carry a range.

The build was not run and `build32` was not touched. All boot figures are read
from the artifacts of a prior generator run under `build32/boot/gen` and
`build32/armdata`, and the overlay figures from `build32/ovl/boot`.

## 1. Cross-unit call sites

The generator binds a cross-unit call by name in two ways: it renames the
spelling in the caller's forced-include header when the caller's spelling
differs from the definition, and it lets the linker bind a call whose spelling
already matches the definition.

The report `build32/boot/gen/boot_link_report.txt` line 2 states "renamed
spellings: 1647". That is `sum(len(u.renames))` over all units (`tools/bootlink.py`,
the `report` list in `generate`).

Counted from the headers for the current plan set, 5,726 matched units
(`build32/boot/boot_matched.txt`) plus 79 port units (`build32/boot/port_sources.txt`
and `build32/boot/boot_asm_subst.txt`), the 5,805 expected headers under
`build32/boot/gen/include` carry 4,215 `#define` lines. Of these, 2,525 are data
binds of the form `(*bw_dsref_NAME)`, 41 are private-copy defines ("another boot
file owns"), and 1,649 are name renames, every one with a function target. Split
by group: 1,624 renames in matched units and 25 in port units. Command:
`python seam_count.py`.

Which number to trust: 1,649 and 1,647 agree within 2. The report's run used 14
port units where the current plan lists 79, which accounts for the difference.
A raw count of every `#define` in `build32/boot/gen/include` gives 4,350 and is
wrong: the directory holds 5,951 files against 5,805 units, so 146 are stale, and
2,552 files date from 2026-10-03 with a further 3,379 from 2026-10-15 22:34 to
22:35. The header directory is not a trustworthy count by itself.

The rename count is not the cross-unit call surface. A caller that already uses
the defined spelling gets no `#define` and is bound directly by the linker.
Measured with the generator's own classifier, `closure.Index.refs`, over the
5,726 matched sources (`python seam_refs.py`):

| class | (source, name) pairs | distinct names |
|---|---|---|
| matched (same spelling, no rename) | 10,596 | 4,664 |
| alias (spelling differs, renamed) | 1,662 | 493 |
| open (stubbed) | 381 | 176 |
| sdk | 42 | 31 |
| runtime | 17 | 10 |
| data | 2,406 | 1,092 |
| const | 64 | 37 |

The cross-unit function surface is 10,596 + 1,662 + 381 = 12,639 (unit, name)
pairs, or 5,333 distinct names. The 1,649 renames are of the same order as the
alias class (1,662) and cover only that subset. A uniform slot scheme must
redirect the whole surface, about 12,600 header lines in the boot, not 1,649.

For the title overlay set, `build32/ovl/boot/ovl_report.txt` line 1 reports 3,925
translation units and 407 trampolines. The per-overlay headers
`build32/ovl/boot/include/ov0*.h` carry 5,716 `#define` lines, 1,371 of them data
binds, so 4,345 are name redirects. These redirect every exported name to its
`ovNNN_` spelling, so they count definitions as well as references and are not a
call-site count. The 407 trampolines are the cross-window calls that already
exist.

## 2. Intra-unit calls

A uniform slot reaches a cross-unit call only. A call inside one translation
unit is not redirected, because the generator excludes the defining unit (a
textual `#define` cannot separate a reference from a definition).

A regex scan of the 5,726 matched sources, with function definitions and
prototypes removed using `closure.SourceScan`, counts 1,499 call occurrences to
names the same source defines, over 901 distinct (unit, name) pairs. Command:
`python seam_calls2.py`. This is an
estimate; the parser noise from definitions, prototypes and function-like macros
puts the range at roughly 1,300 to 1,600 occurrences. These sites stay unhookable
under a uniform slot scheme unless the defining unit's own calls are also
redirected, which is decision 3 in `docs/mod-interface.md`.

## 3. Address-taken functions

A function whose address is stored in data escapes a call-site slot and needs
the pointer redirected.

ARM9 data, `build32/armdata/bw_arm9_data.c`: 777 relocation words, 484 of kind
`BW_DATA_RELOC_CODE` and 293 of kind `BW_DATA_RELOC_DATA`; `k_funcs` holds 6,606
entries, the functions whose address can be stored. Counted with
`python seam_tables.py`.

Title overlay data, `build32/ovl/boot/ov0*_link.c`: 1,022 code relocations and
138 data relocations across the eight overlays; the `k_entries` tables hold 4,183
entries. These counts come from the per-overlay lines of
`build32/ovl/boot/ovl_report.txt` and from `seam_tables.py`.

The relocation paths are `sdk/armdata.c`, function `bw_data_relocate`, which
rewrites `BW_DATA_RELOC_CODE`, `BW_DATA_RELOC_HOST` and `BW_DATA_RELOC_DATA`
words, and `scan_code_pointers`, which scans the data ranges and calls
`bw_ovl_code_entry` for a word that points into an overlay window; and
`sdk/overlay_image.c`, function `bw_ovl_code_entry`, which answers from each
overlay's `k_entries` table.

Intersected with the 44 declared addresses (`python ...\seam_taken.py`): none of
the 44 appears among the ARM9 code relocation targets, though 8 appear in the
ARM9 `k_funcs` table; 2 (`0x0215B5A8`, `0x0215CD24`) are overlay code relocation
targets, and 13 appear in the overlay `k_entries` tables. A pointer built at run
time and passed to the game is not covered by either path and cannot be counted
statically.

## 4. Generated-code cost

Measured sizes:

| artifact | files | bytes |
|---|---|---|
| boot forced-include headers (`build32/boot/gen/include`) | 5,805 current (5,951 on disk) | 787,557 current (806,446 on disk) |
| `build32/boot/gen/boot_link.c` | 1 | 27,484 (977 lines, 189 stubs) |
| overlay per-overlay headers `ov0*.h` | 8 | 388,805 |
| overlay per-unit headers `include/tu` | 3,933 | 729,713 |
| overlay link C `ov*_link.c` | 8 | 994,718 |
| `ovl_dispatch.c` | 1 | 170,587 |
| `ovl_arm9_stubs.c` | 1 | 6,308 |

Slots a uniform scheme implies. The boot has 4,664 distinct cross-unit matched
names plus 176 open names, so roughly 4,840 slots, or up to 5,333 if the alias
spellings are counted as separate names. The title overlays define 3,775
functions in total (`build32/ovl/boot/ovl_report.txt`, the "defined" column), so
up to about 3,775 more. The uniform total is roughly 8,000 to 9,100 slots.

Growth, as an estimate. A uniform boot scheme raises the boot renames from 1,649
to about 12,639, roughly 11,000 added `#define` lines. At about 45 to 55 bytes a
line that is 0.5 to 0.6 MB on the boot headers, about +65%. One slot function per
address at roughly 250 to 400 bytes adds 1.2 to 2.1 MB to the boot link source.
The declared set instead adds 44 slots, on the order of 20 KB, and about 73
redirects (section 5).

## 5. The declared set's size

`docs/mod-interface.md` section 1 lists 44 candidate addresses: 25 in arm9 and 19
in overlays (ov009, ov010, ov020, ov021, ov093). Parsed with
`python ...\seam_decl.py`.

Call sites those addresses cover, counting call occurrences to any spelling of
each address's function:

| closure | addresses with a site | call occurrences | address-taken (`&name`) |
|---|---|---|---|
| boot, 5,726 matched sources | 25 arm9 | 36 | 0 |
| title overlay set, 3,925 units | overlay rows | 37 | 0 |

29 of the 44 addresses have at least one site across the two closures. The other
15 have none, mostly the ov093 and ov020 addresses, which the title set does not
compile, and several arm9 Wm and overlay-loader entry points. The `ov021`
addresses do appear (13 and 2 occurrences), because title-overlay sources call
them through the existing trampolines. The two overlay addresses reached by a
data pointer (`0x0215B5A8`, `0x0215CD24`) and the 13 in `k_entries` need the
registry consulted by the pointer path, not only at a call site.

## 6. What could not be measured

- No build or generator run was made, so the boot figures come from artifacts of
  a prior run. The report's unit set differs from the current plan (14 port units
  against 79), a 2-count difference in renames.
- The run-time cost of an added indirect call, in cycles or in wall time, was not
  measured. No benchmark was run.
- The intra-unit count and the call-occurrence counts come from a regex scan
  (`seam_calls2.py`, `seam_decl.py`), not from the generator. They are estimates
  with parser noise, about plus or minus 10%.
- The overlay cross-unit call surface, distinct (unit, name) pairs inside an
  overlay, was not computed. Only the namespace rename count (4,345) and the
  defined-function count (3,775) are available.
- Coverage of the 44 addresses outside the boot and title closures (ov020, ov021,
  ov093, and the multiplayer transport) was not measured; those overlays are not
  compiled in this build.

## 7. What the numbers imply

The declared set costs 44 slots and about 73 redirects in the boot and title
closures. A uniform scheme costs roughly 8,000 to 9,100 slots, about 12,600
redirects in the boot alone, and 2 to 3 MB of added generated code, and it makes
every cross-unit call indirect. On generated-code size the declared set is
cheaper by roughly two orders of magnitude, and on slot count by about 200 to 1.

The price of the declared set is fixed coverage. 15 of the 44 addresses have no
call site in these two closures, so the set must be chosen for the mods, not
grown for free. Two declared addresses are reached by an overlay data pointer,
so their slots must also be consulted by `bw_ovl_code_entry` (decision 12). The
numbers support the declared set that `docs/mod-interface.md` already chooses.
