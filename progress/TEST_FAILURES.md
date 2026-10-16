# The six failures (wave 31)

Diagnosis only. No build, no ctest run. Every claim names the file it comes from.

The build in `build32` links for the first time and `ctest` reports 55 passed, 6
failed, 3 skipped of 61. None of the six executables linked before, so none has a
baseline. The six are `test_arcid`, `test_vfs_members`, `test_armdata_real`,
`test_ovlink_real_stub`, `test_boot_run`, `test_boot_window`.

---

## 1. `test_arcid` and `test_vfs_members` (SEGFAULT, no output)

**Cause.** The matched archive reader passes a DS address to the port's
host-path open.

`src/matched/arm9/unit_02048DDC.c` declares and calls
`FS_OpenFile(arc, ARC_TOOL_gArchiveTable.unk00[id])` (line 145, line 156). The
second argument is an entry of the archive name table, a DS address of a string
(the synthetic test image holds `0x02004000 + offset`; the real table holds
`0x020A6C58`'s entries). The port's `FS_OpenFile` is the host-path open:
`sdk/fs.c` `FS_OpenFileEx` treats the second argument as a `const char *` and
reaches `strncmp(p, "rom:", 4)` (line 812). The DS address is not a host
pointer, so that read faults. In both tests the file layer has no FAT/FNT
(`sdk/fs.c` `tables()` returns false, so `bw_fs_id_of_path` returns -1 at line
808), so `strncmp` is the first dereference and the process dies with no output.

The call chain is `ARC_TOOL_OpenArchive` (unit_02048DDC.c:152) ->
`FS_OpenFile`, reached from the tests through `ARC_TOOL_OpenArchive` directly
and through `ARC_TOOL_SeekMember`, `ARC_TOOL_ReadMember`
(`src/matched/arm9_sdk/func_02048F4C.c`) and `ARC_TOOL_ReadMemberRangeImpl`
(`src/matched/arm9/func_02048D00.c`).

The port already has the right function for this call: `CRYPTOi__BN_lshift1`
in `sdk/fs.c` (line 945) resolves a DS address through the ARM9 window and then
opens it (`sdk/fs.c` lines 926 to 941 describe exactly this call site). The
tests use `CRYPTOi__BN_lshift1` for the open-by-path refusals and
`ARC_TOOL_OpenArchive` for the normal path, which is the design.

**Why it is broken now.** `aletheia` renamed the matched side's symbol. The
eidos commit `fc19d8b` ("arc: open archives by id through the game's own
readers") records that the call site said `CRYPTOi__BN_lshift1`; aletheia's
later rename (`git log -S CRYPTOi__BN_lshift1` on
`src/matched/arm9/unit_02048DDC.c` names commit `b02f92d774`) changed it to
`FS_OpenFile`. The CMake rename list for this unit
(`CMakeLists.txt:876-894`, the row for `arm9/unit_02048DDC.c` at line 879)
renames `ARC_TOOL_gArchiveTable`, `arc_tool_msg_member_range` and
`arc_tool_file`, but not `FS_OpenFile`. So the matched call resolves to the
port's host-path `FS_OpenFile` instead of the DS-address shim.

**Fix (one line, CMakeLists.txt:879).** Add `FS_OpenFile=CRYPTOi__BN_lshift1` to
the `unit_02048DDC.c` compile definitions, beside the existing three. The
matched file then calls the DS-address shim, which is what `sdk/fs.c`'s comment
already documents. `CMakeLists.txt` is held by W30, so this is stated, not
applied.

**Test or port?** The port's build wiring. The tests match the documented design
and the two shims in `sdk/fs.c`; only the per-file rename is missing.

**Alternative, not preferred.** Teach `FS_OpenFile` to accept both a host path
and a DS address. That merges two callers with different meanings into one
function; the rename keeps `FS_OpenFile` (host path) and `CRYPTOi__BN_lshift1`
(DS address) separate, as they are now.

---

## 2. `test_armdata_real` (`FAIL no generated name d_020A6C58`)

**Cause.** A stale expected name in the test, not a missing table.

The generated tables do bind the archive name table at `0x020A6C58`. The name
is `_ArcPathTable`: `build32/armdata/test_armdata_real.h:21` and
`build32/armdata/bw_arm9_data.c:7999` (`{ "_ArcPathTable", 0x020A6C58u, ... }`).
The generator takes that name from the only source that uses the table,
`src/matched/arm9/sub_0200545C.c`, which declares `extern u8 _ArcPathTable[];`
(line 87) and passes it to `ARC_TOOL_SetTable` (line 151); its `symbols:` block
names `_ArcPathTable 0x020A6C58` (line 17). The generator's name set is built
from matched sources' externs (`tools/armdata.py` `_collect_names`), and
`sub_0200545C.c` is in the test's source list (`CMakeLists.txt:1914`).

The test looks up `d_020A6C58` (`tests/test_armdata_real.c:148`, and the comment
at line 11). `d_020A6C58` is aletheia's older spelling: it survives only in
`bw_decomp/docs/notes/sub_0200545C.c.txt` (line 23, line 70). No source in the
current checkout uses it (`grep d_020A6C58` finds only eidos docs and this
test). So the generator emits one symbol for the table and the test asks for a
different, obsolete name. `need` is called twice because `CHECK_PTR` evaluates
its `got` argument in the condition and again in the `printf`, which is why the
log shows two `FAIL no generated name` lines plus one `bound` line (three
failures, `build32/Testing/Temporary/LastTest.log:996-999`).

**Fix.** Rename the lookup to the name the source uses: change
`d_020A6C58` to `_ArcPathTable` in `tests/test_armdata_real.c:148` and in the
comment at line 11. The same stale spelling appears in `docs/armdata.md`
(lines 39 to 43) and in the `CMakeLists.txt:1914` comment. The by-address checks
in the test (`reloc_at(0x020A6C58u + 4u*i)`, `arc_entry`) already pass; only the
name lookup fails.

If the old spelling must be kept, the test can `#define d_020A6C58
_ArcPathTable` before its lookups. The generator cannot bind `d_020A6C58` from
this checkout, because nothing in the build names the table that way.

---

## 3. `test_ovlink_real_stub` (stale expected panic text)

**Cause.** `CMakeLists.txt`'s expected panic string is stale. The port's text is
correct.

The test runs `test_ovlink_real --arm9` and expects
`PANIC in overlay ARM9 stub at 0x0202F0B0: func_0202F0B0: ARM9 function an
overlay calls` (`CMakeLists.txt:1876`). The run prints
`PANIC in overlay ARM9 stub at 0x0202F0B0: TIME_ICON_Update: ARM9 function an
overlay calls` (`build32/Testing/Temporary/LastTest.log:919-921`).

The name comes from aletheia's manifest. `tools/ovlink.py` `arm9_canonical`
(line 1263) returns the ARM9 manifest name for the address, and the stub
printer `sdk/overlay_image.c:345-352` formats it. The manifest entry
`layout/matched/arm9/pkt_0202EF98.json:58-62` names `0x0202F0B0`
`TIME_ICON_Update` (source `src/matched/arm9/func_0202F0B0.c`). So
`TIME_ICON_Update` is the manifest's current name and the port's print is
correct; `func_0202F0B0` is the address spelling the test was written with.

**Fix (one line, CMakeLists.txt:1876).** Replace `func_0202F0B0` with
`TIME_ICON_Update` in the `-DEXPECT` string. `CMakeLists.txt` is held by W30, so
this is stated, not applied.

---

## 4. `test_boot_run` and `test_boot_window` (exit 3)

**What exit 3 means.** Status 3 is the port's crash and watchdog status, not a
test status. `sdk/plat_crash.c` terminates the process with
`TerminateProcess(GetCurrentProcess(), 3)` from the unhandled-exception filter
(line 144) and from the watchdog (line 234), and `sdk/plat_crash.h` documents
status 3 for both (lines 13, 41). `demo/eidos_boot.c` documents the watchdog
status 3 too (line 35) and returns 3 after the post-`NitroMain` panic (line
1258). Both boot tests report `exit 3` with empty output
(`build32/Testing/Temporary/LastTest.log:1284-1309`).

**Where `NitroMain returned` is printed.** `demo/eidos_boot.c:1257`,
`bw_panic("NitroMain returned", 0x02005124u, "its loop has no exit")`, reached
only after `NitroMain()` (line 1256) returns. The linked `NitroMain` is
aletheia's matched `src/matched/arm9/func_02005124.c` (it exists, so
`cmake/boot.cmake:331-333` seeds it and does not use
`stubs/reimpl_02005124.c`; it is compiled, `build32/boot/boot_matched.txt:16`),
and its body is `for (;;)` (func_02005124.c:103-120). A correct link cannot
return from it, and no `NitroMain` stub exists in the boot link
(`grep NitroMain build32/boot` finds nothing).

**Why the run ends before the documented stop.** The documented stop is a
generated boot-stub panic at or past frame 6000 (`cmake/boot.cmake:695` sets
`KNOWN_STOP` to `PANIC in boot stub reached` and `MIN_FRAMES` to 6000). The
observed runs end in 0.13 to 0.15 seconds with status 3 and no captured output.
That is the crash filter: `sdk/plat_crash.c:131` does
`freopen("eidos_crash.log", "w", stderr)` before it prints, so the report goes to
the file and not to the pipe the test reads. `build32/eidos_crash.log` holds an
access-violation report (regenerated at 01:27:41, `read of host address
0x00000064`; the copy at the repository root, 01:20:50, reads `0x00000066`).
So the boot faults before it runs the frame loop to frame 6000. No map file is
in `build32`, so the faulting function (`exe+0x87785`) cannot be resolved from
the report alone.

**Regression or first run?** The first run. The boot linked for the first time
in this wave (`progress/status.json`, `link_failed_targets: 0`; `WAVES.md` wave
2, "build links"), so there is no earlier passing result to regress from. The
test's `KNOWN_STOP` and `MIN_FRAMES` describe a run that reaches a stub, which
this build has not yet done.

**What would settle the remaining question.** A run of `eidos_boot` with the
crash filter not installed, or a build with a map file, names the faulting
function. The `NitroMain returned` line is the driver's message for a returned
`NitroMain`; the captured evidence (status 3 and empty output) matches the crash
filter, not a `bw_panic` (which the other expected-panic tests report as exit
`0xC0000409`, for example `build32/Testing/Temporary/LastTest.log:452`).

---

## 5. The pre-existing duplicate (`MYSTATUS_NameToStrbuf`)

**The collision.** `boot_link.c` generates a panic stub named
`MYSTATUS_NameToStrbuf` at `0x02008500`
(`build32/boot/gen/boot_link.c:175-179`, listed at
`build32/boot/gen/boot_link_report.txt:236`), and
`src/matched/arm9/unit_020084BC.c:111` defines `MYSTATUS_NameToStrbuf` as C at
the same address. aletheia's manifest lists both: `unit_020084BC.c` names it in
its `functions` and `addresses` arrays
(`layout/matched/arm9/legacy.json:50,59`), and
`src/matched/arm9/MYSTATUS_NameToStrbuf.c` is the separate mwcc-asm file at the
same address (its header line 2, its body line 13). `unit_020084BC.c` is
compiled for the boot (`build32/boot/boot_matched.txt:3550`); the asm file is
not, it is asm-pending (`build32/boot/boot_asm.txt:1`).

**Why the generator emits the stub.** References reach `0x02008500` by the
spelling `func_02008500`. `tools/closure.py`'s `Index` fills `member_addr` only
for manifest `functions` whose name spells an address (`func_`/`sub_`) or is in
`layout/symbols_sdk.txt` (`closure.py:568-571` and `_name_addr`, lines
603-609). `unit_020084BC.c`'s members are named `MYSTATUS_*`, so
`defined_addr` returns None for `MYSTATUS_NameToStrbuf`; `func_at` gets no
`0x02008500`, so the reference is not renamed to the C definition and falls into
`missing_refs`. The stub name is then taken from `asm_at[0x02008500]`, which the
asm file's name supplies (`tools/bootlink.py:707-715`, and the stub naming at
lines 801-824). The result is one name with two definitions.

**The workaround in place.** `CMakeLists.txt:2188-2189` compiles `boot_link.c`
with `-DMYSTATUS_NameToStrbuf=MYSTATUS_NameToStrbuf_bootstub`, which renames the
spurious stub out of the way and leaves `unit_020084BC.c`'s real C under the
name callers use. `CMakeLists.txt` is held by W30.

**Durable fix, option A (tools/bootlink.py, preferred).** When a missing
reference's chosen stub name is already in `defined` (a compiled unit or a shim
defines it), do not emit the stub; rename the reference to that defined name
instead. This fixes the class, not the instance, and it lets the `CMakeLists.txt`
workaround be removed.

**Durable fix, option B (cmake/boot.cmake).** Add
`src/matched/arm9/MYSTATUS_NameToStrbuf.c` to `EIDOS_BOOT_SHIM_REPLACED`
(`cmake/boot.cmake:95`). `bootlink.py plan` then skips the file (`plan`, the
loop at lines 509-530 skips excluded sources), so no asm row, no `asm_pending`
and no stub. The plan reports it as "excluded without a name clash (kept out by
decision)" (bootlink.py:506, 554-555), which is not fatal.

**Which to take.** Option A. It is general, it is where the mistake is (the
generator naming a stub for a name a compiled unit already defines), and
`EIDOS_BOOT_SHIM_REPLACED` means "a port shim replaces this file", which is not
true here: no shim replaces the file, a compiled unit defines the function. Option
B also leaves the same collision to reappear for the next asm-pending file that a
unit covers.

**Note.** The `CMakeLists.txt:2186` comment refers to "bootlink.py's `spurious
stubs` check in the plan report". No such check exists in the current
`tools/bootlink.py` (`grep spurious` finds only the comment and an unrelated
word in `sdk/plat_thread.h`). That check is a plan, not a present behaviour.

---

## Not determined

- The boot's faulting function. `build32/eidos_crash.log` gives only
  `exe+0x87785` and no map file exists in `build32`. A build with a map, or a
  run with `bw_plat_crash_install` skipped, names it.
- Whether the boot reaches `demo/eidos_boot.c:1257` at all. The captured test
  output is empty and the exit status is 3, which the crash filter explains
  without that line; the crash filter's `freopen` (`sdk/plat_crash.c:131`) is why
  the report is not in the captured output. A direct run's console output would
  settle it.
