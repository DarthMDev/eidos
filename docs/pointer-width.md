# Pointer width

The DS is LP32: `sizeof(void *)` is 4, and every struct the game's own layout
owns was laid out by a compiler that knew that. A host is usually LP64 or
LLP64, where a pointer is 8 bytes and needs 8-byte alignment. This document
settles what eidos does about that, with the measurement it is settled on.

Regenerate the numbers below with:

```
python tools/closure.py --aletheia-root ../aletheia --pointer-audit
```

The script reads only aletheia's `layout/` manifests and the C text of the
sources they name. It writes nothing and it does not modify aletheia.

## The two configurations, and that both work

`cmake/ilp32-toolchain.cmake` configures the port with 32-bit pointers. It
looks for an i686 compiler driver (on Windows, the `i686-w64-mingw32-gcc`
that sits next to the x86_64 one in the MinGW-w64 archive under aletheia's
`tools/gcc-host/`; on Linux, an `i686-linux-gnu-gcc`), and falls back to
`-m32` on whatever compiler is the default. It does not set
`CMAKE_SYSTEM_NAME`: this is a same-OS, different-ABI build, the executables
run on the build machine, and ctest drives them exactly as it does the
64-bit ones.

`CMakeLists.txt` then reads `CMAKE_SIZEOF_VOID_P` and sets one option:

```
option(EIDOS_TCBL_MATCHED_ACCESSORS ...)   # default: ON iff sizeof(void *) == 4
```

ON puts the four EXACT tcbl accessors (`sub_02030EF0`, `sub_02030F34`,
`func_02030F04`, `sub_02030FB4`) into the library straight out of aletheia's
`src/matched/`, compiled as written, and leaves the `stubs/reimpl_` stand-ins
out. OFF does the reverse, which is the state README.md's "Why four matched
files are out of the build" describes. The symbols are the same either way,
so nothing else in the build knows which it got. Turning the option on for a
target whose pointers are 8 bytes is a configure-time fatal error rather than
a link that silently truncates addresses.

Measured on this machine, both configurations, same tree, same tests:

| configuration | compiler | node / manager | accessors | ctest | test_tcbl |
| --- | --- | --- | --- | --- | --- |
| LP64 (default) | x86_64-w64-mingw32-gcc 14.2.0 | 40 / 88 bytes | `stubs/reimpl_` | 10/10 | 49 checks, 0 failures |
| ILP32 | i686-w64-mingw32-gcc 14.2.0 | 24 / 56 bytes | the matched four | 10/10 | 49 checks, 0 failures |

24 and 56 are `0x18` and `0x38`: on an ILP32 host the task-callback node and
its manager are byte-identical in layout to the DS's, `EIDOS_TCBL_HEADER_BIAS`
is zero, and the `char pad[0x34]` that makes `sub_02030F34` a DS-only file on
an LP64 host lands on exactly the field it was written for. The SDL2 devel
package `tools/fetch_sdl.py` unpacks ships an `i686-w64-mingw32` tree beside
the `x86_64-w64-mingw32` one, so the ILP32 configuration builds the platform
layer and `eidos_view` too and `test_view` runs; `-DEIDOS_SDL=OFF` is
available and passes the other nine, exactly as on the 64-bit side.

`tests/test_tcbl.c` asserts both layouts rather than one: on an ILP32 target
it requires the DS's own `0x18` and `0x38` and a zero header bias, and on an
LP64 target the 40 and 88 the port's own table records.

## What LP64 actually costs, measured

The question is not whether the port *can* run LP64 (it does, today) but how
much matched code has to be rewritten, and re-verified against the ROM, for
LP64 to be the configuration the game is trusted on. So: every matched source
the manifest names, read three ways.

```
matched sources audited: 1110
A  integer-typed address (u32/int holding a pointer): 410
B  hardcoded struct size or field offset:             375
C  cast between an integer and a pointer:              48
any of the three:                                     642  (57.8%)

per signal, the number of files at least one hit came from:
  A1    6      an integer-typed name cast to a pointer
  A2  312      a cross-file prototype disagreement, pointer vs integer
  A3  152      an integer-returning function that returns an address
  B1  355      explicit padding: a byte array that is not its struct's last member
  B2   30      an index multiply by a word-multiple literal
  B3    1      a literal size argument to an alloc call
```

Each of the three is a heuristic over C text rather than a type check, and
the exact rule is written out in `tools/closure.py` above `INT_TYPE_NAMES`.
The point is a defensible order of magnitude and a list of files to look at.
Two of the signals are worth reading closely:

**A2 is the strongest, because the evidence is two independent
decompilations of the same signature.** aletheia's matched files each declare
the externs they call with the prototype that call site needs, so the same
game function is declared many times across the corpus. 94 game functions are
declared with a pointer in one matched file and an integer in another at the
same position. `func_02030734`, the allocation entry point with 1163 callers,
is declared as returning a pointer by 37 matched files and as returning an
`int` by `sub_0200F6D8.c`; `func_02012A30` returns a pointer in fourteen
files and an `int` in one. Both spellings are EXACT on the DS, which is the
finding: the DS build resolves the relocation by address and the width is the
same either way. On an LP64 host the integer spelling truncates the address
and the two files cannot both be right.

**B1 is the biggest, and it is the one that cannot be fixed by a rule.** 355
matched sources reach a field through an explicit padding array: `u8
pad[0x14]; u32 active;` is the ordinary shape of a matched file that knows
one field of a struct it does not otherwise understand. That padding *is* a
field offset frozen into the source, and it is correct on the DS and wrong on
any target where something in front of it got wider. There is no portable way
to write it: to replace `u8 pad[0x14]` with declarations a host compiler can
lay out, somebody has to know what those 0x14 bytes are, which means
decompiling the rest of the struct. A coding rule cannot produce that
knowledge; only more decompilation can.

The four tcbl accessors this whole question started with are in the list
under their own signals: `sub_02030EF0` under A2 (its return is a pointer in
`sub_02030FC8.c` and a `u32` in its own definition) and under B1, and
`sub_02030F34`, `func_02030F04` and `sub_02030FB4` under B1 (`char pad[0x34]`,
`char pad[4]`, `char pad[0x10]`).

C is small (48 files) and mostly honest: the majority are hardware register
and fixed-address accesses, `(vu32 *)0x04000000` and friends, which are LP32
assumptions of a different kind (the address space, not the pointer width)
and are the platform layer's business rather than a portability defect.

## The decision

**ILP32 is the reference configuration for eidos until the field code is in.
LP64 keeps building, through `EIDOS_TCBL_MATCHED_ACCESSORS=OFF` and the
`stubs/reimpl_` stand-ins, and every test passes there too.**

The trade-offs, one paragraph each.

**Platform reach.** ILP32 gives up macOS and iOS outright: Apple removed
32-bit support from macOS in Catalina and from iOS in 11, and there is no
i386 slice to build. Windows, Linux and Android all still run 32-bit binaries
natively and will for the life of this project. That is the entire cost, and
it is deferred rather than paid: macOS and iOS are Milestone 9 (Packaging &
platforms), which is the last milestone, and by the time the port is a
shippable executable the matched corpus will look nothing like it does at
18.7% coverage. Choosing ILP32 now does not choose it forever; it chooses it
for the phase where the answer changes every wave.

**Save-file and struct-layout fidelity.** This is what ILP32 buys, and it is
not a convenience. A DS save file, an archive record and a network packet are
all struct images, and on ILP32 the matched declarations produce exactly the
DS's bytes: no field moves, no padding appears, `sizeof` is the number the
game wrote down. The tcbl measurement above is the small end of it (`0x18`
and `0x38` reproduced exactly); the field, battle and save code that Milestone
7 brings in is the large end, where structs are bigger, nested, and written
straight to disk. On LP64 every one of those needs either a full declaration
(so the host compiler lays it out itself) or a serialiser, and until the
struct is fully decompiled neither is available. ILP32 makes the whole class
of question not arise, which is worth more than it sounds while the corpus is
half explicit padding.

**The amount of aletheia work implied.** LP64 as the reference means 642 of
1110 matched sources (57.8%) carry at least one assumption that has to be
re-examined, and 355 of them carry explicit padding, which as argued above is
not a rule violation to be corrected but missing knowledge to be acquired.
Every change to a matched source costs a full DS re-verify in the same commit
(README.md, portability rule 1), so this is not a mechanical sweep; it is
hundreds of re-verifies on files that are already EXACT and already correct.
ILP32 as the reference implies zero aletheia work, and it retires the one
concession the port has already made: the four accessors come back into the
build as matched code, and `EIDOS_TCBL_HEADER_BIAS`, the port's one piece of
arithmetic that exists only to pay for pointer width, becomes zero.

**What other ports did.** sm64 and Ship of Harkinian both run 64-bit, and the
reason is the same in both cases and does not transfer: their decompilations
type pointers as pointers. sm64 is 100% matched with fully declared structs,
so a host compiler lays every one of them out itself and there is no offset
written down anywhere to be wrong; Ship of Harkinian sits on a decompilation
of the same shape. Neither project ever had a `u8 pad[0x14]` standing in for
a field nobody had identified yet. aletheia is at 18.7% coverage and the
padding *is* the current state of knowledge. Their conclusion is the right
one to arrive at, from the state they were in; it is not the right one to
start from.

**What would change this.** The B1 count is the metric to watch. As waves
replace padding with real fields the number falls, and when it is small
enough that the remaining files can be re-verified in a wave or two, LP64
becomes the reference and ILP32 becomes the fidelity cross-check. Re-run
`--pointer-audit` at the end of each wave; it is a coverage number for
portability the same way the matched-byte percentage is one for accuracy.
That is the exit condition, and it has to be met before Milestone 9 in any
case, because macOS is not optional for a released port.

**If the LP64 route is taken instead**, the aletheia-side rule to add to
`docs/AGENT-BRIEF.md` would be, exactly:

> A struct field that holds an address is declared as a pointer type (`void
> *`, or the pointed-to type when it is known), never as `u32`, `int` or
> `s32`. On the DS a pointer is four bytes, so a pointer-typed field
> generates byte-identical code and the file stays EXACT; the same
> declaration is then also correct on a 64-bit host. The same rule applies to
> a function's return type and to each of its parameters: if a call site
> passes an address, the extern declares that position as a pointer. Where a
> struct has to be reached through explicit padding because the fields in
> front of it are not identified yet, the padding array stays, and the file
> is recorded in `docs/pointer-width.md` rather than changed.

This rule is not being added, because it addresses signals A1, A2 and A3
(410 files) and leaves B1 (355 files) untouched, and B1 is the half that
decides whether a struct image is byte-identical to the DS's.

## The file lists

Every source below is one the manifest names, listed once per category it
falls into, with the first hit of that category as the reason. The listing is
`tools/closure.py --pointer-audit`'s own output, verbatim.

```
A  integer-typed address -- 410 files
  src/matched/func_02006C2C.c              A2 func_02048F4C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/func_02011264.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02011D60.c              A2 func_020306F0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0201C6FC.c              A2 sub_0201C758: its param 2 is a pointer in one matched file and an integer in another
  src/matched/func_0201F7A8.c              A2 func_02041B6C: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0201F868.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0201F878.c              A2 func_02045EC0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02020D3C.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02020E08.c              A3 FS_SeekFile returns the pointer buf in u32
  src/matched/func_02020FE4.c              A2 func_02020FE4: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02021518.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/func_0202155C.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/func_020215A0.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_020215EC.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02021660.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0202208C.c              A2 func_020223C0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0202223C.c              A2 func_0202223C: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_020222CC.c              A2 func_0207D534: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02022430.c              A2 func_02022430: its param 10 is a pointer in one matched file and an integer in another
  src/matched/func_020225F4.c              A2 func_020225F4: its ret is a pointer in one matched file and an integer in another
  src/matched/func_02022670.c              A2 func_0202223C: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_020242C0.c              A2 func_020242A4: its param 1 is a pointer in one matched file and an integer in another
  src/matched/func_02027858.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02028784.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0202889C.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02029F44.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0202AF28.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0202B1EC.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0202C124.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/func_0202C140.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_020306F0.c              A2 func_020306F0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02031590.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0203159C.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/func_020338F8.c              A2 func_020338F8: its ret is a pointer in one matched file and an integer in another
  src/matched/func_020345DC.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02035BBC.c              A2 func_02035760: its ret is a pointer in one matched file and an integer in another
  src/matched/func_02036BD4.c              A3 func_02036D64 returns the pointer h in u32
  src/matched/func_0203CD98.c              A3 func_0203CD98 returns the pointer s in int
  src/matched/func_0203D1A4.c              A2 func_020395EC: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0203D2CC.c              A2 func_020338F8: its ret is a pointer in one matched file and an integer in another
  src/matched/func_0203D394.c              A3 func_0203D394 returns the pointer e in u32
  src/matched/func_0203D3A8.c              A2 func_020395EC: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0203D4D0.c              A2 func_020395EC: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0203F9B4.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_020409E0.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02040B1C.c              A2 func_02040B1C: its param 1 is a pointer in one matched file and an integer in another
  src/matched/func_02040B58.c              A2 func_02040B58: its param 1 is a pointer in one matched file and an integer in another
  src/matched/func_02040B94.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02040C14.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02041090.c              A2 func_020410D0: its param 5 is a pointer in one matched file and an integer in another
  src/matched/func_020410D0.c              A2 func_020410D0: its param 5 is a pointer in one matched file and an integer in another
  src/matched/func_020414C4.c              A2 func_02040B94: its param 1 is a pointer in one matched file and an integer in another
  src/matched/func_02043678.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/func_020436FC.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/func_02043754.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/func_020437AC.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_020437DC.c              A3 func_020437DC returns the pointer bmp in u16
  src/matched/func_020437E4.c              A3 func_020437E4 returns the pointer bmp in u16
  src/matched/func_020437EC.c              A3 func_020437EC returns the pointer bmp in u32
  src/matched/func_02045C04.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02046084.c              A1 hdr is declared with an integer type and cast to a pointer
  src/matched/func_02048AC4.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_02048D00.c              A2 func_02048DDC: its param 1 is a pointer in one matched file and an integer in another
  src/matched/func_02048D54.c              A3 func_02048E34 returns the pointer buf in u32
  src/matched/func_02048F4C.c              A2 func_02048F4C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/func_02048F74.c              A2 func_02048F74: its param 4 is a pointer in one matched file and an integer in another
  src/matched/func_02049250.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/func_02049578.c              A3 func_02049578 returns the pointer arc in u32
  src/matched/func_0204A03C.c              A3 func_02062670 returns the pointer res in int
  src/matched/func_0204A380.c              A2 func_0204A380: its ret is a pointer in one matched file and an integer in another
  src/matched/func_0204BA7C.c              A2 func_02063800: its ret is a pointer in one matched file and an integer in another
  src/matched/func_0204BB04.c              A3 func_0204BB04 returns the pointer g_02146A74 in u32
  src/matched/func_0204BB38.c              A3 func_0204BB38 returns the pointer g_02146A74 in u32
  src/matched/func_0204C6E4.c              A2 sub_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0204C788.c              A2 sub_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0204D8AC.c              A2 func_0204A8D4: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0204D9D4.c              A2 func_02049250: its ret is a pointer in one matched file and an integer in another
  src/matched/func_0204DC64.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0204E010.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0204E01C.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/func_0204E074.c              A2 func_02062670: its param 1 is a pointer in one matched file and an integer in another
  src/matched/func_0204E0C4.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0204E0D0.c              A2 func_02062A00: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0204E824.c              A2 func_0207D534: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_020590A4.c              A3 func_020590A4 returns the pointer p in int
  src/matched/func_020590C0.c              A3 func_020590C0 returns the pointer p in int
  src/matched/func_02062A00.c              A2 func_02062A00: its param 0 is a pointer in one matched file and an integer in another
  src/matched/func_0207457C.c              A3 func_0207457C returns the pointer tbl in u32
  src/matched/func_0207466C.c              A3 func_0207466C returns the pointer tbl in u32
  src/matched/func_02159BC4.c              A3 func_02159BC4 returns the pointer m in u16
  src/matched/func_02168800.c              A2 func_02048F4C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/func_0216880C.c              A2 func_0204A1BC: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_0200F6D8.c               A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_0200F800.c               A2 func_0207D534: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0200FB24.c               A3 sub_0200FB24 returns the pointer obj in u32
  src/matched/sub_0200FB6C.c               A2 func_020306F0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0201407C.c               A2 func_02020E08: its param 2 is a pointer in one matched file and an integer in another
  src/matched/sub_02014CF8.c               A2 func_020163C4: its param 1 is a pointer in one matched file and an integer in another
  src/matched/sub_020159C8.c               A2 func_020163C4: its param 1 is a pointer in one matched file and an integer in another
  src/matched/sub_02016078.c               A2 func_020163C4: its param 1 is a pointer in one matched file and an integer in another
  src/matched/sub_02018A7C.c               A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/sub_02018DB4.c               A1 v is declared with an integer type and cast to a pointer
  src/matched/sub_02018E34.c               A1 v is declared with an integer type and cast to a pointer
  src/matched/sub_02019308.c               A1 v is declared with an integer type and cast to a pointer
  src/matched/sub_02019388.c               A1 v is declared with an integer type and cast to a pointer
  src/matched/sub_0201AE1C.c               A3 sub_0201AE1C returns the pointer p in u32
  src/matched/sub_0201C724.c               A2 sub_0201C758: its param 2 is a pointer in one matched file and an integer in another
  src/matched/sub_0201D26C.c               A2 sub_0201D26C: its param 1 is a pointer in one matched file and an integer in another
  src/matched/sub_0201D304.c               A2 sub_0201D26C: its param 1 is a pointer in one matched file and an integer in another
  src/matched/sub_0201D33C.c               A2 sub_0201D26C: its param 1 is a pointer in one matched file and an integer in another
  src/matched/sub_020242A4.c               A2 func_020242A4: its param 1 is a pointer in one matched file and an integer in another
  src/matched/sub_0202C2C0.c               A2 sub_0202C358: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0202C2DC.c               A2 sub_0202C338: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0202C3A0.c               A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0202C3E0.c               A2 sub_0202C510: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_0202C48C.c               A2 sub_0202C510: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_0202C498.c               A2 sub_0202C510: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_0202C4D8.c               A2 sub_0202C4D8: its param 1 is a pointer in one matched file and an integer in another
  src/matched/sub_0202C504.c               A2 sub_0202C510: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_0202C5AC.c               A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0202CB4C.c               A2 func_020410D0: its param 5 is a pointer in one matched file and an integer in another
  src/matched/sub_0202DCC4.c               A3 sub_0202DCC4 returns the pointer p in u32
  src/matched/sub_0202DFD4.c               A2 func_0204A8D4: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0202E794.c               A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0202FDC4.c               A2 sub_0202FDC4: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0202FF98.c               A2 sub_0202FBD0: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_02030098.c               A3 OS_DisableInterrupts returns the pointer p in u32
  src/matched/sub_020304CC.c               A2 sub_020304CC: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_0203064C.c               A2 sub_0202FDC4: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_02030678.c               A2 sub_02030524: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_02030734.c               A2 sub_02030524: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_0203084C.c               A3 sub_02030524 returns the pointer p in u32
  src/matched/sub_02030A5C.c               A2 sub_02030A5C: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_02030EE8.c               A3 sub_02030EE8 returns the pointer p in u32
  src/matched/sub_02030EF0.c               A2 sub_02030EF0: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_02030FC8.c               A2 sub_02030EF0: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_02032A9C.c               A2 func_020319A4: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_02032BF8.c               A2 func_020319A4: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_02032C84.c               A2 func_02031ECC: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_020332A4.c               A3 func_02033B68 returns the pointer q in u32
  src/matched/sub_02033C00.c               A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_02034768.c               A2 func_02043678: its param 3 is a pointer in one matched file and an integer in another
  src/matched/sub_0203499C.c               A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_02034A5C.c               A2 func_02034D08: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_02034BB0.c               A2 func_02034D08: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_02034D08.c               A3 sub_02034D08 returns the pointer unk_021469B8 in u8
  src/matched/sub_020353F8.c               A3 sub_020353F8 returns the pointer p in int
  src/matched/sub_020393D0.c               A3 sub_020393D0 returns the pointer root in int
  src/matched/sub_02039594.c               A3 sub_02039594 returns the pointer unk_021469D0 in int
  src/matched/sub_020403F4.c               A2 sub_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_020409B4.c               A2 func_020409E0: its param 1 is a pointer in one matched file and an integer in another
  src/matched/sub_02043994.c               A3 func_02043994 returns the pointer b in u8
  src/matched/sub_02045088.c               A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_020450C8.c               A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_020450F0.c               A2 sub_02045210: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_02045210.c               A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_020452E8.c               A2 sub_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_02045374.c               A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_02045808.c               A2 sub_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_020458F8.c               A3 sub_020458F8 returns the pointer vec in u16
  src/matched/sub_02045A38.c               A2 sub_02045A38: its param 1 is a pointer in one matched file and an integer in another
  src/matched/sub_02045B38.c               A3 func_02049578 returns the pointer bank in u32
  src/matched/sub_02045F50.c               A3 sub_02045F50 returns the pointer o in u16
  src/matched/sub_020490F4.c               A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_02049658.c               A2 sub_0204968C: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0204968C.c               A2 func_02040B1C: its param 1 is a pointer in one matched file and an integer in another
  src/matched/sub_020496E4.c               A2 func_0204A1BC: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_02049740.c               A2 func_0204A380: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_0204979C.c               A2 func_02040B58: its param 1 is a pointer in one matched file and an integer in another
  src/matched/sub_0204989C.c               A2 func_02049A18: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0204A5AC.c               A2 sub_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0204A8D4.c               A2 sub_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0204ADEC.c               A2 func_02063800: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_0204AF28.c               A2 func_02049250: its ret is a pointer in one matched file and an integer in another
  src/matched/sub_0204AFD8.c               A2 sub_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0204B1CC.c               A2 sub_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0204B294.c               A2 sub_0204CFA4: its param 3 is a pointer in one matched file and an integer in another
  src/matched/sub_0204B560.c               A3 sub_0204B560 returns the pointer actor in s16
  src/matched/sub_0204B728.c               A2 sub_0204B728: its param 1 is a pointer in one matched file and an integer in another
  src/matched/sub_0204CA74.c               A3 sub_0204B838 returns the pointer n in u32
  src/matched/sub_0204CAAC.c               A3 sub_0204B838 returns the pointer n in u32
  src/matched/sub_0204CAE4.c               A2 sub_02082BCC: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0204CF40.c               A2 sub_0204B728: its param 1 is a pointer in one matched file and an integer in another
  src/matched/sub_0204CFA4.c               A2 sub_0204CFA4: its param 3 is a pointer in one matched file and an integer in another
  src/matched/sub_0204CFDC.c               A2 sub_0204CFDC: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0204D048.c               A2 sub_0204CFDC: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0204D2DC.c               A2 sub_02066B38: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_0204D300.c               A2 sub_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/sub_02066B38.c               A2 sub_02066B38: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0200575C.c              A3 func_0203630C returns the pointer sub_0200575C_Table in u32
  src/matched/unit_020057DC.c              A3 func_02074894 returns an address in u32
  src/matched/unit_02005A4C.c              A2 func_02074AC4: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_02005DAC.c              A2 func_020755A8: its param 4 is a pointer in one matched file and an integer in another
  src/matched/unit_02005FF4.c              A3 sub_020058D0 returns an address in u16
  src/matched/unit_0200616C.c              A1 a is declared with an integer type and cast to a pointer
  src/matched/unit_02006238.c              A2 func_02074AC4: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_020064B8.c              A2 func_0207466C: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_020065D8.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_020066E8.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_02006B20.c              A3 sub_02006B20 returns the pointer chan in s8
  src/matched/unit_02006D44.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_02006E40.c              A3 sub_02006E7C returns the pointer obj in u8
  src/matched/unit_02007084.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_020071CC.c              A2 func_020319A4: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_020071E4.c              A3 sub_020071E4 returns the pointer ctrl in u8
  src/matched/unit_020073D8.c              A2 func_020319A4: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_02007454.c              A2 sub_020071CC: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_02007664.c              A3 sub_02007698 returns the pointer h in u32
  src/matched/unit_02007774.c              A3 sub_02017E88 returns the pointer h in int
  src/matched/unit_020079BC.c              A3 sub_02017E88 returns an address in int
  src/matched/unit_02007AC8.c              A3 sub_02007AEC returns the pointer p in u32
  src/matched/unit_02007B54.c              A3 sub_02007B50 returns the pointer p in u32
  src/matched/unit_02007CB0.c              A3 func_02020FE4 returns the pointer e in u32
  src/matched/unit_02008254.c              A3 sub_02008284 returns the pointer p in u16
  src/matched/unit_020084BC.c              A3 sub_0201F41C returns the pointer p in u32
  src/matched/unit_0200852C.c              A3 sub_02008530 returns the pointer obj in u32
  src/matched/unit_0200857C.c              A3 sub_0200864C returns the pointer p in u16
  src/matched/unit_02008700.c              A3 sub_02008700 returns an address in u32
  src/matched/unit_02008828.c              A3 sub_02008828 returns the pointer obj in u32
  src/matched/unit_020089A8.c              A3 sub_020089A8 returns the pointer obj in u32
  src/matched/unit_02008A7C.c              A3 RTC_ConvertDateTimeToSecond returns the pointer obj in u64
  src/matched/unit_02008B08.c              A3 OS_GetOwnerRtcOffset returns an address in s64
  src/matched/unit_02008B4C.c              A3 OS_GetOwnerRtcOffset returns the pointer obj in s64
  src/matched/unit_02008C58.c              A3 sub_0203F2FC returns the pointer obj in u16
  src/matched/unit_02008F80.c              A3 sub_02008F80 returns the pointer obj in u32
  src/matched/unit_02009050.c              A3 sub_02008EF8 returns the pointer copy in int
  src/matched/unit_020090D8.c              A3 sub_020090E0 returns the pointer obj in u8
  src/matched/unit_02009110.c              A3 sub_02009110 returns the pointer obj in u32
  src/matched/unit_02009160.c              A3 sub_02009160 returns the pointer obj in u32
  src/matched/unit_02009210.c              A3 func_020173EC returns the pointer obj in u32
  src/matched/unit_02009448.c              A2 sub_020071CC: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_02009470.c              A3 sub_02009584 returns the pointer obj in u8
  src/matched/unit_02009590.c              A2 func_0200856C: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_020095C8.c              A3 sub_02009794 returns the pointer obj in u32
  src/matched/unit_02009860.c              A3 DWC_IsValidFriendData returns an address in int
  src/matched/unit_02009CE4.c              A3 DWC_CheckValidConsole returns the pointer obj in int
  src/matched/unit_02009DB8.c              A3 sub_02008468 returns an address in u32
  src/matched/unit_0200A3FC.c              A3 func_0203F2FC returns the pointer obj in u16
  src/matched/unit_0200A6F8.c              A3 func_0200A644 returns an address in u32
  src/matched/unit_0200A864.c              A3 func_0200A8AC returns an address in s32
  src/matched/unit_0200ADB8.c              A3 func_020173EC returns the pointer p in u32
  src/matched/unit_0200B348.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0200B394.c              A3 func_0200B3AC returns the pointer p in s32
  src/matched/unit_0200B400.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0200B448.c              A2 func_0200B900: its param 1 is a pointer in one matched file and an integer in another
  src/matched/unit_0200B4E0.c              A3 func_020072CC returns the pointer p in u32
  src/matched/unit_0200B5CC.c              A2 func_0200B900: its param 1 is a pointer in one matched file and an integer in another
  src/matched/unit_0200B67C.c              A2 func_0200B908: its param 1 is a pointer in one matched file and an integer in another
  src/matched/unit_0200B900.c              A2 func_0200B900: its param 1 is a pointer in one matched file and an integer in another
  src/matched/unit_0200B938.c              A2 func_0200B900: its param 1 is a pointer in one matched file and an integer in another
  src/matched/unit_0200BA38.c              A2 func_0200B900: its param 1 is a pointer in one matched file and an integer in another
  src/matched/unit_0200BA78.c              A2 func_0200B900: its param 1 is a pointer in one matched file and an integer in another
  src/matched/unit_0200BAD4.c              A2 func_0200B900: its param 1 is a pointer in one matched file and an integer in another
  src/matched/unit_0200BBA8.c              A2 func_0200B900: its param 1 is a pointer in one matched file and an integer in another
  src/matched/unit_0200BBC8.c              A3 func_0200BBFC returns an address in u32
  src/matched/unit_0200BDBC.c              A3 func_0200BDBC returns the pointer p in u32
  src/matched/unit_0200BF08.c              A3 func_0200BF10 returns the pointer p in u8
  src/matched/unit_0200C0E8.c              A3 func_0200C104 returns the pointer p in u8
  src/matched/unit_0200C3F4.c              A3 func_0200C3F4 returns the pointer a in u32
  src/matched/unit_0200C5A4.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0200C5F8.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0200C6D8.c              A3 func_0203EFC8 returns the pointer p in u32
  src/matched/unit_0200C9EC.c              A3 func_0200C9EC returns the pointer p in u32
  src/matched/unit_0200CED4.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0200CFA0.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0200D0AC.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0200D368.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0200D414.c              A3 func_02005728 returns the pointer p in u8
  src/matched/unit_0200D59C.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0200D5D0.c              A2 func_0200856C: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0200D74C.c              A3 func_020248D8 returns the pointer h in u32
  src/matched/unit_0200D858.c              A3 func_0200D884 returns the pointer p in u32
  src/matched/unit_0200DA64.c              A3 func_0200DAC0 returns the pointer p in u32
  src/matched/unit_0200DE5C.c              A3 func_0200DE5C returns the pointer p in u8
  src/matched/unit_0200DF10.c              A3 func_0200DF40 returns the pointer p in u8
  src/matched/unit_0200E6E8.c              A2 sub_020071CC: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0200E884.c              A2 sub_020071CC: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0201126C.c              A3 func_02011330 returns the pointer vm in u32
  src/matched/unit_02011330.c              A3 func_02011330 returns the pointer r in u16
  src/matched/unit_020120F0.c              A3 func_02012BB0 returns the pointer p in u32
  src/matched/unit_02012204.c              A3 func_02012204 returns the pointer p in u32
  src/matched/unit_0201283C.c              A3 func_0201283C returns the pointer p in u32
  src/matched/unit_02012924.c              A3 func_02012924 returns the pointer p in u32
  src/matched/unit_02012AE4.c              A3 func_02008448 returns the pointer p in u32
  src/matched/unit_02013A48.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_02013AD0.c              A3 sub_02013A94 returns an address in u32
  src/matched/unit_02013BA8.c              A3 sub_02013BA8 returns the pointer r in u8
  src/matched/unit_02013C10.c              A3 sub_02013C10 returns the pointer r in u32
  src/matched/unit_02013DF0.c              A3 sub_02013A94 returns the pointer r in u32
  src/matched/unit_02013EC0.c              A3 sub_02013EC0 returns the pointer r in u32
  src/matched/unit_02013ED8.c              A3 sub_02013A94 returns the pointer r in u32
  src/matched/unit_020140B4.c              A3 sub_020307B0 returns the pointer p in u32
  src/matched/unit_02014274.c              A2 sub_02082BCC: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_020143CC.c              A2 sub_02082BCC: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_02014444.c              A3 sub_0201444C returns the pointer o in u32
  src/matched/unit_02015BA4.c              A2 func_02016C0C: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_02016BA0.c              A2 func_02016C0C: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_02017100.c              A2 func_02017E88: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_02017E1C.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_02017E64.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201808C.c              A2 func_02017E88: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_020181A0.c              A2 func_02017E88: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_02018430.c              A2 func_02017E88: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201854C.c              A2 func_02017E88: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_02018598.c              A2 func_02017E88: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_02018820.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_020188B4.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_02018A00.c              A2 func_02017E88: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_02019B58.c              A2 func_02017E88: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_02019C54.c              A2 sub_0201ADA4: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_02019C7C.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201A920.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0201AA1C.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201AB48.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201AC2C.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201ADA4.c              A2 func_02049578: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0201ADD0.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0201AE14.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0201AF6C.c              A3 sub_0201AF6C returns the pointer tab in u32
  src/matched/unit_0201AFD4.c              A2 func_02049578: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0201B100.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201B220.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201B70C.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201B920.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201B9F0.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201BA68.c              A2 func_02017E88: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201BD28.c              A3 sub_0201BDB4 returns the pointer p in u32
  src/matched/unit_0201BEB8.c              A2 func_02048F4C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201C020.c              A2 func_02048F4C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201C158.c              A2 sub_0201BD28: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0201C1C8.c              A2 sub_0201BD28: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0201C1F4.c              A2 func_02048F4C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201C314.c              A2 sub_0201BD28: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0201C328.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0201C440.c              A2 sub_0201C450: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0201C450.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0201C6B4.c              A2 sub_0201C758: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201CA2C.c              A2 func_0201DB04: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0201D5C4.c              A3 func_0201D5C4 returns the pointer p in u32
  src/matched/unit_0201D69C.c              A3 func_020458F8 returns the pointer p in u32
  src/matched/unit_0201D9C8.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0201DAF0.c              A2 func_0201DB04: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0201DC6C.c              A3 func_0201DB20 returns the pointer p in u32
  src/matched/unit_0201DD58.c              A3 func_0201DD58 returns the pointer p in u32
  src/matched/unit_0201ED04.c              A2 func_02017E1C: its param 2 is a pointer in one matched file and an integer in another
  src/matched/unit_0201F554.c              A2 func_02049624: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_02020CF8.c              A3 func_02020D0C returns the pointer p in u32
  src/matched/unit_02020F08.c              A2 func_02049250: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_02021014.c              A3 func_020210C8 returns the pointer p in int
  src/matched/unit_020223C0.c              A2 func_020223C0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_02022608.c              A3 func_02022608 returns the pointer b in u16
  src/matched/unit_020242D8.c              A2 func_020242A4: its param 1 is a pointer in one matched file and an integer in another
  src/matched/unit_020295D8.c              A3 func_020289B8 returns the pointer task in u32
  src/matched/unit_02029658.c              A3 func_020288C4 returns the pointer task in s32
  src/matched/unit_020296C8.c              A3 func_020288C4 returns the pointer task in s32
  src/matched/unit_02029760.c              A3 func_020288C4 returns the pointer task in s32
  src/matched/unit_020297EC.c              A3 func_020288C4 returns the pointer task in s32
  src/matched/unit_02029868.c              A3 func_02029868 returns the pointer task in u32
  src/matched/unit_02029938.c              A3 func_020289B8 returns the pointer task in u32
  src/matched/unit_020299B8.c              A3 func_020289B8 returns the pointer task in u32
  src/matched/unit_02029A3C.c              A3 func_02029A3C returns the pointer task in u32
  src/matched/unit_02029AA8.c              A3 func_020289B8 returns the pointer task in u32
  src/matched/unit_02029B5C.c              A3 func_020289B8 returns the pointer task in u32
  src/matched/unit_02029C00.c              A3 func_02029658 returns the pointer task in u32
  src/matched/unit_02029C38.c              A3 func_02028988 returns the pointer task in u32
  src/matched/unit_02029C94.c              A3 func_020289D4 returns the pointer task in u32
  src/matched/unit_02029CF0.c              A2 func_02012984: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0202C244.c              A3 sub_0202C244 returns the pointer p in u32
  src/matched/unit_0202C318.c              A2 sub_0202C338: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0202C470.c              A2 sub_0202C510: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0202C4A4.c              A2 sub_0202C510: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0202C4CC.c              A3 sub_0202C4CC returns the pointer p in u32
  src/matched/unit_0202C510.c              A3 sub_0202C510 returns the pointer p in u32
  src/matched/unit_0202C518.c              A2 sub_0202C510: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0202C6B8.c              A3 sub_0202C6B8 returns the pointer p in u32
  src/matched/unit_0202DE6C.c              A2 func_02045758: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0202E54C.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0202E888.c              A2 func_0201298C: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0202EA20.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0202FBAC.c              A2 sub_0202FBD0: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_02033BF0.c              A3 sub_02033BF0 returns the pointer p in u32
  src/matched/unit_02033C9C.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_02034630.c              A3 sub_02034640 returns the pointer p in u32
  src/matched/unit_020355C4.c              A3 sub_020355C4 returns the pointer g_021469BC in u8
  src/matched/unit_02035C70.c              A2 func_02035760: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_02039C68.c              A3 func_020395EC returns the pointer p in u32
  src/matched/unit_020407E8.c              A3 sub_020407E8 returns the pointer g_BgConfig in u32
  src/matched/unit_0204152C.c              A2 func_02041B6C: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0204162C.c              A3 func_0204162C returns the pointer ptr_02146A44 in u32
  src/matched/unit_0204384C.c              A3 func_02062A4C returns the pointer b in u32
  src/matched/unit_02043900.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_02045730.c              A3 sub_02045730 returns the pointer p in u8
  src/matched/unit_02045C44.c              A3 func_020461B8 returns the pointer bank in u8
  src/matched/unit_02046EFC.c              A2 func_020307B0: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_02048AD0.c              A3 func_02048B78 returns the pointer l in u32
  src/matched/unit_02048BC4.c              A2 func_02030734: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0204961C.c              A2 func_02049624: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0204994C.c              A2 func_02049A18: its param 0 is a pointer in one matched file and an integer in another
  src/matched/unit_0204A07C.c              A3 func_02062670 returns the pointer res in int
  src/matched/unit_0204A0FC.c              A3 func_02062920 returns the pointer res in int
  src/matched/unit_0204AEC0.c              A2 func_02063800: its ret is a pointer in one matched file and an integer in another
  src/matched/unit_0204B22C.c              A3 sub_0204B248 returns the pointer bank in u32
  src/matched/unit_0204B3DC.c              A3 sub_0204B3F4 returns the pointer p in u32
  src/matched/unit_0204B5D4.c              A3 sub_0204B5E4 returns the pointer actor in u32
  src/matched/unit_0204B5F4.c              A3 sub_0204B5FC returns the pointer actor in u16
  src/matched/unit_0204B688.c              A3 sub_0204B6E4 returns the pointer actor in u32
  src/matched/unit_0204B898.c              A3 sub_0204B898 returns the pointer p in u16
  src/matched/unit_0204B92C.c              A3 sub_0204B944 returns the pointer p in u32
  src/matched/unit_0204E720.c              A3 func_0209C0A4 returns the pointer g_02146A7C in s32
  src/matched/unit_020640B0.c              A3 func_020640D4 returns the pointer tbl in s32

B  hardcoded size or offset -- 375 files
  src/matched/func_02006368.c              B1 struct StructA: u8 pad[0x14] fixes the offset of active
  src/matched/func_02012374.c              B1 struct Evt: u8 pad_00[0x10] fixes the offset of *unk_10
  src/matched/func_02020D3C.c              B1 struct D: u8 pad[4] fixes the offset of *f4
  src/matched/func_0202208C.c              B1 struct G: u8 pad00[4] fixes the offset of *f04
  src/matched/func_02022174.c              B1 struct Sub: u8 pad00[0x0C] fixes the offset of f0C
  src/matched/func_020221A4.c              B1 struct G: u8 pad00[0x14C] fixes the offset of f14C
  src/matched/func_020221B8.c              B1 struct Sub: u8 pad00[0x0C] fixes the offset of f0C
  src/matched/func_0202223C.c              B1 struct G: u8 pad[0x10] fixes the offset of f10
  src/matched/func_02022268.c              B1 struct G: u8 pad[0x10] fixes the offset of f10
  src/matched/func_020222EC.c              B1 struct Sub: u8 pad00[0x28] fixes the offset of *f28
  src/matched/func_020225F4.c              B1 struct TextObj: u8 pad00[0x150] fixes the offset of f150
  src/matched/func_02022640.c              B1 struct Screen: u8 pad00[0x24] fixes the offset of f24
  src/matched/func_02022670.c              B1 struct Screen: u8 pad00[0x10] fixes the offset of *f10
  src/matched/func_020226FC.c              B1 struct Obj: u8 pad00[0xC] fixes the offset of unk0C
  src/matched/func_02022804.c              B1 struct Obj: u8 pad00[0xC] fixes the offset of unk0C
  src/matched/func_02022830.c              B1 struct Obj: u8 pad00[0xC] fixes the offset of unk0C
  src/matched/func_0202285C.c              B1 struct Obj: u8 pad00[0xC] fixes the offset of unk0C
  src/matched/func_02022888.c              B1 struct Obj: u8 pad00[0xC] fixes the offset of unk0C
  src/matched/func_020228B4.c              B1 struct Obj: u8 pad00[0xC] fixes the offset of unk0C
  src/matched/func_020228E0.c              B1 struct Obj: u8 pad00[0xC] fixes the offset of unk0C
  src/matched/func_02027858.c              B1 struct TaskMenuOwner: u8 pad_00[0x14] fixes the offset of *unk_14
  src/matched/func_0202889C.c              B1 struct Sys: u8 unk08[0x14] fixes the offset of unk1C
  src/matched/func_020288C4.c              B1 struct Sys: u8 unk08[0x14] fixes the offset of unk1C
  src/matched/func_02028988.c              B1 struct Sys: u8 unk08[0x14] fixes the offset of unk1C
  src/matched/func_020289B8.c              B1 struct Sys: u8 unk08[0x14] fixes the offset of unk1C
  src/matched/func_020289D4.c              B1 struct Sys: u8 unk08[0x14] fixes the offset of unk1C
  src/matched/func_020305C4.c              B3 literal size 4 passed to OS_AllocFromHeap
  src/matched/func_02030F04.c              B1 struct Node: char pad[4] fixes the offset of *prev
  src/matched/func_020344C8.c              B1 struct Struct_020344C8: u8 pad[2] fixes the offset of count
  src/matched/func_02036BD4.c              B1 struct Handler: u8 pad08[4] fixes the offset of unk0c
  src/matched/func_02036EC8.c              B1 struct Handler: u8 unk00[0x0c] fixes the offset of unk0c
  src/matched/func_02037010.c              B1 struct Handler: u8 pad04[9] fixes the offset of kind
  src/matched/func_0203CD98.c              B1 struct Table: char pad_00[0x6c] fixes the offset of unk_6c
  src/matched/func_0203D210.c              B1 struct NetVtbl: u8 pad_00[0x6c] fixes the offset of (*unk_6c)(void)
  src/matched/func_0203D24C.c              B1 struct NetState: u8 pad_00[0x5c] fixes the offset of unk_5c
  src/matched/func_0203D26C.c              B1 struct NetSys: u8 pad_00[0x65] fixes the offset of mode
  src/matched/func_0203D29C.c              B1 struct NetSys: u8 pad_00[0x65] fixes the offset of mode
  src/matched/func_0203D2CC.c              B1 struct NetState: u8 pad_00[0x56] fixes the offset of unk_56
  src/matched/func_0203D394.c              B1 struct NetEntry: u8 pad_00[0x4a] fixes the offset of state
  src/matched/func_0203D798.c              B1 struct Singleton: u8 pad_00[8] fixes the offset of *unk_08
  src/matched/func_0203F8F4.c              B1 struct BgSys: u8 pad_02[2] fixes the offset of unk_04
  src/matched/func_0203F9B4.c              B1 struct BgSys: u8 pad_02[2] fixes the offset of unk_04
  src/matched/func_0203FB44.c              B1 struct BgSys: u8 pad_00[0x168] fixes the offset of *unk_168
  src/matched/func_020409E0.c              B1 struct Slot: u8 pad_02[6] fixes the offset of *tiles
  src/matched/func_02040AF0.c              B1 struct Slot: u8 pad_00[8] fixes the offset of *tiles
  src/matched/func_02040B1C.c              B1 struct Slot: u8 pad_00[0x1e] fixes the offset of depth
  src/matched/func_02040C14.c              B1 struct Slot: u8 pad_02[0x1d] fixes the offset of depth
  src/matched/func_020410D0.c              B1 struct Entry: u8 pad_00[8] fixes the offset of unk_08[0x14]
  src/matched/func_020411E8.c              B1 struct Entry: u8 pad_00[8] fixes the offset of unk_08[0x14]
  src/matched/func_020413B0.c              B1 struct Slot: u8 pad_00[8] fixes the offset of *screen
  src/matched/func_020414EC.c              B1 struct Entry: u8 pad_00[8] fixes the offset of *unk_08
  src/matched/func_02042E34.c              B1 struct Surface: u8 pad_04[0x11] fixes the offset of size_code
  src/matched/func_02042ED8.c              B1 struct Screen: u8 pad_04[0x11] fixes the offset of size_code
  src/matched/func_02043678.c              B2 index multiply by 8: bmp->width = width * 8
  src/matched/func_020436FC.c              B2 index multiply by 8: bmp->width = width * 8
  src/matched/func_02043754.c              B2 index multiply by 8: bmp->width = width * 8
  src/matched/func_02045C04.c              B1 struct Rec: u8 pad_08[0xc] fixes the offset of *unk_14
  src/matched/func_02046084.c              B2 index multiply by 4: file = (struct MsgFile *)sub_02030734(heapId, hp->langCount * 4 + 0xc, 0, g_020A7E8C, 0x1f
  src/matched/func_02049250.c              B1 struct ArcFile: u8 file[0x48] fixes the offset of imageBase
  src/matched/func_0204932C.c              B1 struct ArcFile: u8 file[0x48] fixes the offset of imageBase
  src/matched/func_020493D4.c              B1 struct ArcFile: u8 file[0x48] fixes the offset of imageBase
  src/matched/func_0204944C.c              B1 struct ArcFile: u8 file[0x48] fixes the offset of imageBase
  src/matched/func_020494F4.c              B1 struct ArcFile: u8 file[0x48] fixes the offset of imageBase
  src/matched/func_02049578.c              B1 struct ArcFile: u8 file[0x48] fixes the offset of imageBase
  src/matched/func_0204B604.c              B1 struct Actor: u8 pad_00[0x60] fixes the offset of 8
  src/matched/func_0204B954.c              B1 struct Actor: u8 pad_00[0x54] fixes the offset of unk_54
  src/matched/func_0204B95C.c              B1 struct Anim: u8 pad_00[0x0c] fixes the offset of flag
  src/matched/func_0204B974.c              B1 struct Anim: u8 pad_00[0x0c] fixes the offset of flag
  src/matched/func_0204B98C.c              B1 struct Rec: u8 pad_00[0x0c] fixes the offset of value
  src/matched/func_0204BA20.c              B1 struct Actor: u8 pad_00[0x64] fixes the offset of 1
  src/matched/func_0204BA7C.c              B1 struct Sys: u8 pad_000[0xf0] fixes the offset of unk_0f0
  src/matched/func_0204BAEC.c              B1 struct Sys: u8 pad_000[0xf8] fixes the offset of unk_0f8
  src/matched/func_0204BB04.c              B1 struct Sys: u8 pad_000[0xf8] fixes the offset of unk_0f8
  src/matched/func_0204BB20.c              B1 struct Sys: u8 pad_000[0xfc] fixes the offset of unk_0fc
  src/matched/func_0204BB38.c              B1 struct Sys: u8 pad_000[0xfc] fixes the offset of unk_0fc
  src/matched/func_0204BBC8.c              B1 struct Bank: u8 pad_00[0x0f] fixes the offset of prio
  src/matched/func_0204BC40.c              B1 struct Bank: u8 pad_00[0x10] fixes the offset of *next
  src/matched/func_0204BCD8.c              B1 struct Sub: u8 pad_00[0x1c] fixes the offset of bound
  src/matched/func_0204C6E4.c              B1 struct Pool: u8 pad_04[8] fixes the offset of ready
  src/matched/func_0204C718.c              B1 struct Pool: u8 pad_00[4] fixes the offset of *ents
  src/matched/func_0204C788.c              B1 struct Pool: u8 pad_00[4] fixes the offset of *ents
  src/matched/func_0204C804.c              B1 struct Pool: u8 pad_00[4] fixes the offset of *ents
  src/matched/func_0204C83C.c              B1 struct Sys: u8 pad_000[0xe0] fixes the offset of unk_0e0
  src/matched/func_0204CBB0.c              B1 struct Elem: u8 pad_00[0x60] fixes the offset of 8
  src/matched/func_0204D8AC.c              B1 struct RecC: u8 pad_00[0x3c] fixes the offset of 1
  src/matched/func_0204DA5C.c              B1 struct Rec: u8 pad_00[0x2c] fixes the offset of a
  src/matched/func_0204DC70.c              B1 struct TargetStruct: char pad[8] fixes the offset of dispcnt
  src/matched/func_0204DCA8.c              B1 struct Rec: u8 pad_00[0x14] fixes the offset of 31
  src/matched/func_0204E01C.c              B1 struct Entry: u8 pad_00[0x10] fixes the offset of unk_10
  src/matched/func_0204E074.c              B1 struct Entry: u8 pad_00[8] fixes the offset of *unk_08
  src/matched/func_0204E0D0.c              B1 struct Slot: u8 pad_00[0x3c] fixes the offset of unk_3c
  src/matched/func_0206285C.c              B2 index multiply by 16: } if (bank->bankAttr & 1) { return (u8 *)bank->cells + idx * 16
  src/matched/func_02062FF0.c              B1 struct CellAnim: u8 pad00[0x1C] fixes the offset of *bank
  src/matched/func_02063018.c              B1 struct CellAnim: u8 pad00[0x20] fixes the offset of unk20
  src/matched/func_02063030.c              B1 struct CellAnim: u8 pad00[0x18] fixes the offset of attrOverride
  src/matched/func_020643A4.c              B1 struct CellAnim: u8 pad00[0xC] fixes the offset of active
  src/matched/func_020643C4.c              B1 struct MultiCellAnim: u8 pad00[0x14] fixes the offset of unk14
  src/matched/func_02065C78.c              B1 struct Obj: u8 pad_00[0x74] fixes the offset of unk_74
  src/matched/func_02067AB8.c              B1 struct G2dOamState: u8 pad_00[4] fixes the offset of mode
  src/matched/func_02067B78.c              B1 struct G2dOamState: u8 pad_00[0x24] fixes the offset of flip
  src/matched/func_0207457C.c              B1 struct Context: char pad[0x88] fixes the offset of *table
  src/matched/func_020745A4.c              B1 struct Context: char pad[0x34] fixes the offset of file[0x54]
  src/matched/func_0207466C.c              B1 struct Context: char pad[0x88] fixes the offset of *table
  src/matched/func_02074694.c              B1 struct Context: char pad[0x88] fixes the offset of *table
  src/matched/func_02075A94.c              B1 struct SndObj2: char pad[0x128] fixes the offset of ref_count
  src/matched/sub_0201057C.c               B1 struct Ctx: u8 unk00[0x2C] fixes the offset of unk2C
  src/matched/sub_02018DB4.c               B1 struct Mon: u8 unk08[0x80] fixes the offset of unk88
  src/matched/sub_02018E34.c               B1 struct BlockA: u8 unk10[6] fixes the offset of unk16[6]
  src/matched/sub_02019308.c               B1 struct Mon: u8 unk08[0x80] fixes the offset of unk88
  src/matched/sub_02019388.c               B1 struct BlockA: u8 unk10[6] fixes the offset of unk16[6]
  src/matched/sub_0201F094.c               B1 struct Entry: u8 tag[5] fixes the offset of *str
  src/matched/sub_0201F250.c               B1 struct Entry: u8 pad05[3] fixes the offset of *unk08
  src/matched/sub_02022D88.c               B2 index multiply by 0x604: void *sub_02022D88(struct Obj *obj, u32 idx) { return obj->base + idx * 0x604
  src/matched/sub_02025B3C.c               B2 index multiply by 0xc: i++) { if (key == sub_02025B3C_Table[i * 0xc]) { return i
  src/matched/sub_020279E8.c               B1 struct TaskMenu: u8 pad02[0x12] fixes the offset of height
  src/matched/sub_0202C3E0.c               B1 struct Obj0202C3E0: u8 pad04[4] fixes the offset of unk04[9]
  src/matched/sub_0202C5C4.c               B1 struct Obj0202C5C4: u8 pad[6] fixes the offset of unk06
  src/matched/sub_0202CAC4.c               B1 struct Win: u8 pad_08[2] fixes the offset of bg
  src/matched/sub_0202CB4C.c               B1 struct Win: u8 pad_08[2] fixes the offset of bg
  src/matched/sub_0202DCC4.c               B1 struct Rec0202DCC4: u8 pad[0x18] fixes the offset of 1
  src/matched/sub_0202DFD4.c               B1 struct Obj0202DFD4: u8 pad[8] fixes the offset of *unk08
  src/matched/sub_0202E0C8.c               B1 struct Obj0202E0C8: u8 pad[8] fixes the offset of *unk08[2]
  src/matched/sub_0202FDC4.c               B1 struct EnvModule: u8 pad_08[6] fixes the offset of error
  src/matched/sub_0202FE68.c               B1 struct EnvModule: u8 pad_08[6] fixes the offset of error
  src/matched/sub_0202FF98.c               B1 struct G02146954: u8 pad08[6] fixes the offset of error
  src/matched/sub_02030098.c               B1 struct HeapBlock: char name[0x12] fixes the offset of tag
  src/matched/sub_020304C0.c               B1 struct S020304C0: char pad[4] fixes the offset of field4
  src/matched/sub_02030880.c               B1 struct HeapBlockSite: char name[0x12] fixes the offset of tag
  src/matched/sub_02030EAC.c               B1 struct Owner: u8 pad_00[0x34] fixes the offset of *head
  src/matched/sub_02030EE8.c               B1 struct S02030EE8: char pad[0x10] fixes the offset of field10
  src/matched/sub_02030EF0.c               B1 struct S02030EF0: char pad0[4] fixes the offset of field4
  src/matched/sub_02030F34.c               B1 struct S02030F34: char pad[0x34] fixes the offset of *head
  src/matched/sub_02030FB4.c               B1 struct S02030FB4: char pad[0x10] fixes the offset of *field10
  src/matched/sub_02031140.c               B1 struct S02031140: char pad[0x14] fixes the offset of *field14
  src/matched/sub_02031290.c               B1 struct S02030FB4: char pad[0x10] fixes the offset of *field10
  src/matched/sub_020314B8.c               B1 struct S020314B8: char pad[8] fixes the offset of *field8
  src/matched/sub_020314DC.c               B1 struct S020314DC: char pad[0x10] fixes the offset of field10
  src/matched/sub_020315D4.c               B1 struct S020315D4: char pad[0x14] fixes the offset of *field14
  src/matched/sub_02032930.c               B1 struct SaveBackup: u8 pad00[0x10] fixes the offset of unk10
  src/matched/sub_02032970.c               B1 struct SaveBackup: u8 pad00[0x10] fixes the offset of unk10
  src/matched/sub_020329AC.c               B1 struct SaveBackup: u8 pad00[0x2C] fixes the offset of unk2C
  src/matched/sub_02032A9C.c               B1 struct SaveBackup: u8 pad00[0x2C] fixes the offset of unk2C
  src/matched/sub_02032BF8.c               B1 struct SaveBackup: u8 pad00[0x2C] fixes the offset of unk2C
  src/matched/sub_02032C84.c               B1 struct SaveBackup: u8 pad00[0x2C] fixes the offset of unk2C
  src/matched/sub_020331A4.c               B1 struct SaveBackup: u8 pad00[0x10] fixes the offset of unk10
  src/matched/sub_02033200.c               B1 struct SaveBackup: u8 pad00[0x2C] fixes the offset of unk2C
  src/matched/sub_02033264.c               B1 struct SaveBackup: u8 pad00[0x2C] fixes the offset of unk2C
  src/matched/sub_020332A4.c               B1 struct SaveBackup: u8 pad00[0x2C] fixes the offset of unk2C
  src/matched/sub_02033CFC.c               B2 index multiply by 8: if (tail != 0 && i < lastByte) { if (g_RunLenHead[p->bits[i + 1]] >= len - tail) { found =
  src/matched/sub_02033E30.c               B2 index multiply by 8: } else if (g_RunLenHead[p->bits[j]] >= r) { found = (8 - run) + i * 8
  src/matched/sub_02033FB0.c               B2 index multiply by 8: if (head != 0 && i > lastByte) { if (g_RunLenTail[p->bits[i - 1]] >= len - head) { found =
  src/matched/sub_020340D0.c               B2 index multiply by 8: } else if (j >= lastByte && g_RunLenTail[p->bits[j]] >= r) { found = (8 - r) + j * 8
  src/matched/sub_02035080.c               B1 struct Obj02035080: char pad[0x1c] fixes the offset of unk1C
  src/matched/sub_0203515C.c               B1 struct Obj0203515C: char pad[0x39] fixes the offset of unk39
  src/matched/sub_02035188.c               B1 struct Obj0203515C: char pad[0x39] fixes the offset of unk39
  src/matched/sub_020351BC.c               B1 struct Obj0203515C: char pad[0x39] fixes the offset of unk39
  src/matched/sub_020351EC.c               B1 struct Obj020351EC: char pad[8] fixes the offset of unk08
  src/matched/sub_0203524C.c               B1 struct UiCtx: u8 pad00[0x10] fixes the offset of on_10
  src/matched/sub_02035268.c               B1 struct UiCtx: u8 pad00[0x10] fixes the offset of on_10
  src/matched/sub_02035284.c               B1 struct UiCtx: u8 pad00[0x10] fixes the offset of on_10
  src/matched/sub_020352AC.c               B1 struct UiCtx: u8 pad00[0x10] fixes the offset of on_10
  src/matched/sub_020353C8.c               B1 struct UiCtx: u8 pad00[0x10] fixes the offset of on_10
  src/matched/sub_020353F8.c               B1 struct UiCtx: u8 pad00[0x10] fixes the offset of on_10
  src/matched/sub_02035660.c               B1 struct Sys: u8 pad_00[0x3c] fixes the offset of unk_3c
  src/matched/sub_02035E54.c               B1 struct Sio: u8 pad_00[0x64] fixes the offset of unk64
  src/matched/sub_02036390.c               B1 struct Rec: u8 pad_00[0x24] fixes the offset of unk24
  src/matched/sub_02039338.c               B1 struct Slots: u8 pad_00[0x80] fixes the offset of (*unk80)(void)
  src/matched/sub_020393A8.c               B1 struct LinkDriver: char pad00[0x7c] fixes the offset of (*get_mask)(void)
  src/matched/sub_020393D0.c               B1 struct LinkState: u8 pad000[0x1e9] fixes the offset of unk_1e9
  src/matched/sub_020394DC.c               B1 struct LinkState: u8 pad000[0x1e7] fixes the offset of unk_1e7
  src/matched/sub_02039548.c               B1 struct LinkState: u8 pad000[0x1e7] fixes the offset of unk_1e7
  src/matched/sub_02039560.c               B1 struct LinkState: u8 pad000[0x1e7] fixes the offset of unk_1e7
  src/matched/sub_02039578.c               B1 struct LinkState: u8 pad000[0x1e7] fixes the offset of unk_1e7
  src/matched/sub_02039594.c               B1 struct LinkState: u8 pad000[0x1eb] fixes the offset of unk_1eb
  src/matched/sub_0203996C.c               B1 struct Owner: u8 pad_00[0x70] fixes the offset of table[9]
  src/matched/sub_020403F4.c               B1 struct Entry: u8 pad_00[8] fixes the offset of *unk_08
  src/matched/sub_020409B4.c               B1 struct Entry02146A44: u8 pad_00[8] fixes the offset of unk_08
  src/matched/sub_02041B6C.c               B1 struct Unk02146A44: u8 pad_00[6] fixes the offset of mask
  src/matched/sub_02042FB0.c               B1 struct Slot: u8 pad_00[8] fixes the offset of *screen
  src/matched/sub_02042FF8.c               B1 struct Ctx: u8 pad_00[0x168] fixes the offset of *unk_168
  src/matched/sub_02043094.c               B1 struct Ctx: u8 pad_00[0x168] fixes the offset of *unk_168
  src/matched/sub_02043994.c               B1 struct Bmp: u8 pad00[8] fixes the offset of unk08
  src/matched/sub_020452E8.c               B2 index multiply by 32: } sub_0203FB44(d->unk2, d->unkA, size * 32)
  src/matched/sub_02045374.c               B1 struct Cfg: u8 pad_00[2] fixes the offset of unk_02
  src/matched/sub_02045EC0.c               B2 index multiply by 8: sub_0204960C(bank->stream, pos + (index * 8 + 4))
  src/matched/sub_02045F50.c               B1 struct Head: u8 pad_00[2] fixes the offset of unk_02
  src/matched/sub_02046180.c               B1 struct Obj: u8 pad_04[0x16] fixes the offset of index
  src/matched/sub_02046FCC.c               B1 struct Record: u8 pad00[0x14] fixes the offset of size
  src/matched/sub_0204968C.c               B1 struct Res: u8 pad_00[0x10] fixes the offset of unk_10
  src/matched/sub_0204979C.c               B1 struct Res: u8 pad_00[0x10] fixes the offset of unk_10
  src/matched/sub_0204A8D4.c               B1 struct Slot: u8 pad_00[0x24] fixes the offset of *unk_24
  src/matched/sub_0204ADA4.c               B1 struct Entry: u8 pad_00[0x14] fixes the offset of 31
  src/matched/sub_0204ADEC.c               B1 struct Ctx: u8 pad_00[0x110] fixes the offset of *slots
  src/matched/sub_0204AF28.c               B1 struct Entry: u8 pad_08[8] fixes the offset of unk_10
  src/matched/sub_0204AFD8.c               B1 struct Slot: u8 pad_08[8] fixes the offset of unk_10
  src/matched/sub_0204B0C8.c               B1 struct Owner: u8 pad_00[0x98] fixes the offset of *items
  src/matched/sub_0204B100.c               B1 struct Bank: u8 pad_04[8] fixes the offset of count
  src/matched/sub_0204B1CC.c               B1 struct Bank: u8 pad_04[8] fixes the offset of count
  src/matched/sub_0204B270.c               B1 struct Bank: u8 pad_00[8] fixes the offset of *unk_08
  src/matched/sub_0204B294.c               B1 struct RecB: u8 pad_00[8] fixes the offset of unk_08
  src/matched/sub_0204B3B4.c               B1 struct Obj: u8 pad_00[8] fixes the offset of *unk_08
  src/matched/sub_0204B454.c               B1 struct Actor: u8 pad_00[0x0c] fixes the offset of pos
  src/matched/sub_0204B4EC.c               B1 struct Actor: u8 pad_00[0x0c] fixes the offset of pad_0c[4]
  src/matched/sub_0204B528.c               B1 struct Actor: u8 pad_00[0x0c] fixes the offset of pos
  src/matched/sub_0204B53C.c               B1 struct Actor: u8 pad_00[0x0c] fixes the offset of pos
  src/matched/sub_0204B550.c               B1 struct Actor: u8 pad_00[0x0c] fixes the offset of pos
  src/matched/sub_0204B560.c               B1 struct Actor: u8 pad_00[0x0c] fixes the offset of pos
  src/matched/sub_0204B658.c               B1 struct Actor: u8 pad_00[0x60] fixes the offset of 8
  src/matched/sub_0204B728.c               B1 struct Obj: u8 pad_00[0x40] fixes the offset of unk_40
  src/matched/sub_0204B878.c               B1 struct Obj: u8 pad_00[0x5a] fixes the offset of unk_5a
  src/matched/sub_0204B8C4.c               B1 struct Obj: u8 pad_00[0x68] fixes the offset of unk_68
  src/matched/sub_0204B8D4.c               B1 struct Obj: u8 pad_00[0x68] fixes the offset of unk_68
  src/matched/sub_0204B8F4.c               B1 struct Node: u8 pad_00[0xc] fixes the offset of unk_0c
  src/matched/sub_0204B918.c               B1 struct Node: u8 pad_00[0xc] fixes the offset of unk_0c
  src/matched/sub_0204C8E4.c               B1 struct Owner: u8 pad_04[8] fixes the offset of count
  src/matched/sub_0204CAE4.c               B1 struct Obj: u8 pad_00[0x14] fixes the offset of unk_14
  src/matched/sub_0204CEB4.c               B1 struct Obj: u8 pad_00[0x54] fixes the offset of unk_54
  src/matched/sub_0204CED8.c               B1 struct Obj: u8 pad_00[0x60] fixes the offset of 8
  src/matched/sub_0204CF40.c               B1 struct Obj: u8 pad_00[0x5c] fixes the offset of unk_5c
  src/matched/sub_0204CF74.c               B1 struct Inner: u8 pad_00[8] fixes the offset of *unk_08
  src/matched/sub_0204CFDC.c               B1 struct Obj: u8 pad_00[8] fixes the offset of *owner
  src/matched/sub_0204D048.c               B1 struct Req: u8 pad_00[8] fixes the offset of unk_08
  src/matched/sub_0204D134.c               B1 struct Req: u8 pad_00[0x14] fixes the offset of *unk_14
  src/matched/sub_0204D158.c               B1 struct Src: u8 pad_00[0xc] fixes the offset of *unk_0c
  src/matched/sub_0204D2DC.c               B1 struct Obj: u8 pad_00[0x64] fixes the offset of *unk_64
  src/matched/sub_0204D300.c               B1 struct Obj: u8 pad_00[0x78] fixes the offset of *unk_78
  src/matched/sub_0204D340.c               B1 struct Obj: u8 pad_00[8] fixes the offset of *unk_08
  src/matched/sub_0204D360.c               B1 struct Obj: u8 pad_00[8] fixes the offset of *unk_08
  src/matched/sub_0204D380.c               B1 struct Obj: u8 pad_00[0xc] fixes the offset of unk_0c
  src/matched/sub_0204D3A0.c               B1 struct Rec: u8 pad_00[0x10] fixes the offset of unk_10
  src/matched/sub_02061C0C.c               B1 struct Queue: u8 pad_08[8] fixes the offset of total
  src/matched/sub_02062F1C.c               B1 struct Owner: u8 pad_00[0xc] fixes the offset of base
  src/matched/sub_02066B38.c               B1 struct Cell: u8 pad_00[0x0c] fixes the offset of unk_0c
  src/matched/unit_020067E0.c              B1 struct Sub020067E0Block: u8 unk0C[0x30] fixes the offset of unk3C
  src/matched/unit_02006E40.c              B1 struct Sub02006E40Obj: u8 pad01[3] fixes the offset of *unk04
  src/matched/unit_02007084.c              B1 struct Sub02007084Ctrl: u8 pad06[6] fixes the offset of unk0C
  src/matched/unit_020071CC.c              B1 struct Sub020071CCCtrl: u8 pad06[6] fixes the offset of unk0C
  src/matched/unit_020071E4.c              B1 struct Sub020071E4Ctrl: u8 pad06[6] fixes the offset of unk0C
  src/matched/unit_020072A4.c              B1 struct Sub020072A4Ctrl: u8 pad06[6] fixes the offset of unk0C
  src/matched/unit_02007324.c              B1 struct Sub02007324Ctrl: u8 pad06[6] fixes the offset of unk0C
  src/matched/unit_020073D8.c              B1 struct Sub020073D8Ctrl: u8 pad06[6] fixes the offset of unk0C
  src/matched/unit_02007454.c              B1 struct Sub02007454Header: u8 unk3C4[0x18] fixes the offset of unk3DC
  src/matched/unit_020075B0.c              B1 struct Sub020075B0Header: u8 unk3C4[0x18] fixes the offset of unk3DC
  src/matched/unit_02007664.c              B1 struct Sub02007664Header: u8 unk3C4[0x18] fixes the offset of unk3DC
  src/matched/unit_020076D0.c              B1 struct Sub020076D0Header: u8 unk3C4[0x18] fixes the offset of unk3DC
  src/matched/unit_02007774.c              B1 struct Sub02007774Header: u8 unk3C4[0x18] fixes the offset of unk3DC
  src/matched/unit_0200785C.c              B1 struct Sub0200785CHeader: u8 unk3C4[0x18] fixes the offset of unk3DC
  src/matched/unit_020079BC.c              B1 struct Sub020079BCHeader: u8 unk3C4[0x18] fixes the offset of unk3DC
  src/matched/unit_02008700.c              B1 struct Sub02008700Obj: u8 unk2D[3] fixes the offset of unk30[10]
  src/matched/unit_02008828.c              B1 struct Sub02008860Buf: u8 unk1C[0x24] fixes the offset of unk40
  src/matched/unit_02008B08.c              B1 struct Sub02008AECObj: u8 mac[6] fixes the offset of birthdayMonth
  src/matched/unit_02008B4C.c              B1 struct Sub02008AECObj: u8 mac[6] fixes the offset of birthdayMonth
  src/matched/unit_02008EF8.c              B1 struct Sub02008F80Obj: u8 unk00[7] fixes the offset of unk07
  src/matched/unit_02008F80.c              B1 struct Sub02008F80Obj: u8 unk08[0x10] fixes the offset of unk18[6]
  src/matched/unit_02009210.c              B1 struct Sub02009210Obj: u8 unk008[0xDC] fixes the offset of unk0E4[20]
  src/matched/unit_02009470.c              B1 struct Sub02009470Obj: u8 unk000[0x344] fixes the offset of unk344
  src/matched/unit_020095C8.c              B1 struct Sub02009470Ent: u8 unk00[0x20] fixes the offset of unk20
  src/matched/unit_02009860.c              B1 struct Sub020097BCEnt: u8 unk00[0x1C] fixes the offset of unk1C
  src/matched/unit_02009A34.c              B1 struct Sub020097BCEnt: u8 unk00[0x1C] fixes the offset of unk1C
  src/matched/unit_02009C24.c              B1 struct Sub020097BCEnt: u8 unk00[0x1C] fixes the offset of unk1C
  src/matched/unit_02009CE4.c              B1 struct Sub020097BCEnt: u8 unk00[0x1C] fixes the offset of unk1C
  src/matched/unit_02009DB8.c              B1 struct Sub0200852CObj: u8 unk00[0x1B] fixes the offset of unk1B
  src/matched/unit_0200A3FC.c              B1 struct Big: u8 unk000[0x1B0] fixes the offset of unk1B0[8]
  src/matched/unit_0200A6F8.c              B1 struct Big: u8 unk000[0x1B0] fixes the offset of unk1B0[8]
  src/matched/unit_0200A840.c              B1 struct Rec2DC: u8 unk000[0x1E8] fixes the offset of unk1E8[0x14]
  src/matched/unit_0200A864.c              B1 struct Rec2: u8 unk000[0x1E8] fixes the offset of unk1E8[0x14]
  src/matched/unit_0200ADB8.c              B1 struct Reg: u8 unk0A[0x52] fixes the offset of unk5C[0x4C]
  src/matched/unit_0200B448.c              B1 struct RecC4: u8 unk0000[0x17DC] fixes the offset of unk17DC
  src/matched/unit_0200B4E0.c              B1 struct RecC4: u8 unk0000[0x17DC] fixes the offset of unk17DC
  src/matched/unit_0200B5CC.c              B1 struct RecC4: u8 unk0000[0x17DC] fixes the offset of unk17DC
  src/matched/unit_0200B67C.c              B1 struct QRec: u8 unk18[0x0C] fixes the offset of unk24
  src/matched/unit_0200B7D8.c              B1 struct QRec: u8 unk18[0x0C] fixes the offset of unk24
  src/matched/unit_0200B888.c              B1 struct QRec: u8 unk18[0x0C] fixes the offset of unk24
  src/matched/unit_0200B900.c              B1 struct ObjC4: u8 unk00[0x80] fixes the offset of unk80[0x44]
  src/matched/unit_0200B938.c              B1 struct RecC4: u8 unk0000[0x17DC] fixes the offset of unk17DC
  src/matched/unit_0200BA38.c              B1 struct RecC4: u8 unk0000[0x17DC] fixes the offset of unk17DC
  src/matched/unit_0200BA78.c              B1 struct RecC4: u8 unk0000[0x17DC] fixes the offset of unk17DC
  src/matched/unit_0200BAD4.c              B1 struct RecC4: u8 unk0000[0x17DC] fixes the offset of unk17DC
  src/matched/unit_0200BBA8.c              B1 struct RecC4: u8 unk0000[0x17DC] fixes the offset of unk17DC
  src/matched/unit_0200BDBC.c              B1 struct Rec1B0: u8 unk00[0x0C] fixes the offset of unk0C
  src/matched/unit_0200BF08.c              B1 struct Rec1B0: u8 unk000[0x1A0] fixes the offset of unk1A0
  src/matched/unit_0200C0E8.c              B1 struct Blk34: u8 unk0C[8] fixes the offset of 28
  src/matched/unit_0200C6D8.c              B1 struct Blk2C: u8 unk00[0x20] fixes the offset of *unk20
  src/matched/unit_0200CED4.c              B1 struct Blk37: u8 unk008[0x54] fixes the offset of unk05C[4][0x54]
  src/matched/unit_0200CFA0.c              B1 struct Blk37: u8 unk008[0x54] fixes the offset of unk05C[4][0x54]
  src/matched/unit_0200D0AC.c              B1 struct Blk37: u8 unk008[0x54] fixes the offset of unk05C[4][0x54]
  src/matched/unit_0200D368.c              B1 struct Blk37: u8 unk008[0x54] fixes the offset of unk05C[4][0x54]
  src/matched/unit_0200D414.c              B1 struct Blk38: u8 unk2C[2] fixes the offset of unk2E[2]
  src/matched/unit_0200D59C.c              B1 struct GdsProfile: u8 unk00[0x10] fixes the offset of unk10
  src/matched/unit_0200D5D0.c              B1 struct GdsProfile: u8 unk00[0x10] fixes the offset of unk10
  src/matched/unit_0200D6EC.c              B1 struct RecGet: u8 unk00[0x14] fixes the offset of unk14
  src/matched/unit_0200D74C.c              B1 struct GdsProfile: u8 unk00[0x15] fixes the offset of unk15
  src/matched/unit_0200D858.c              B1 struct Blk3C: u8 unk08[4] fixes the offset of unk0C[0x1C]
  src/matched/unit_0200DA64.c              B1 struct Blk1AC: u8 unk44[0xB4] fixes the offset of unkF8[0xB4]
  src/matched/unit_0200DD10.c              B1 struct BlkB90: u8 unk04[250] fixes the offset of unkFE[0xA92]
  src/matched/unit_0200DD8C.c              B1 struct BlkB90: u8 unk04[250] fixes the offset of unkFE[0xA92]
  src/matched/unit_0200DE5C.c              B1 struct RecSlot: u8 unk000[0xFE] fixes the offset of unkFE
  src/matched/unit_0200DF10.c              B1 struct SubEnt: u8 unk02[0x12] fixes the offset of unk14[4]
  src/matched/unit_0200E730.c              B2 index multiply by 0x20: u32 sub_0200E740(const void *obj, u32 idx) { return ((const Flags *)((const u8 *)obj + idx
  src/matched/unit_020111EC.c              B2 index multiply by 4: MI_CpuFill8(vm->stack, 0, vm->params.stack_cap * 4)
  src/matched/unit_02011330.c              B1 struct Stream: u8 unk00[0x14] fixes the offset of *cursor
  src/matched/unit_02011F24.c              B1 struct Ctx: u8 unk00[0xC] fixes the offset of unk0C
  src/matched/unit_020120F0.c              B1 struct Ctx: u8 unk00[0xC] fixes the offset of unk0C
  src/matched/unit_02012204.c              B1 struct Ctx: u8 unk00[0x33] fixes the offset of 1
  src/matched/unit_020127A4.c              B1 struct SaveBlock: u8 pad_000[0x114] fixes the offset of unk_114[0xc0]
  src/matched/unit_0201283C.c              B1 struct BigObj: u8 unk000[0x158] fixes the offset of unk158
  src/matched/unit_02012914.c              B1 struct BigObj: u8 unk000[0x15C] fixes the offset of unk15C
  src/matched/unit_02012924.c              B1 struct Rec44: u8 unk1E[0x12] fixes the offset of unk30
  src/matched/unit_02012AE4.c              B1 struct Rec44: u8 unk00[0x18] fixes the offset of unk18
  src/matched/unit_02012C0C.c              B1 struct BigObj: u8 unk000[0x5E8] fixes the offset of unk5E8[4]
  src/matched/unit_02012EBC.c              B1 struct Big: u8 pad_04[0x5e8] fixes the offset of unk_5ec
  src/matched/unit_02013AD0.c              B2 index multiply by 0x30: sub_0204944C(g_sub02013AD0Obj->table, 0, rid * 0x30, 0x30, &g_sub02013AD0Obj->record)
  src/matched/unit_02013EC0.c              B1 struct Sub02013AD0Rec: u8 pad00[0x14] fixes the offset of unk14
  src/matched/unit_02013ED8.c              B1 struct Sub02013AD0Rec: u8 pad00[0x1e] fixes the offset of 5
  src/matched/unit_02013FD8.c              B1 struct <anonymous>: u8 pad[0x1e] fixes the offset of 5
  src/matched/unit_02014444.c              B1 struct Sub02014444Obj: u8 pad0A[7] fixes the offset of f11
  src/matched/unit_02017528.c              B1 struct Sav: u8 unk08[0x80] fixes the offset of unk88[0x54]
  src/matched/unit_02017D30.c              B1 struct Sav: u8 unk08[0x80] fixes the offset of unk88[0x54]
  src/matched/unit_020188B4.c              B1 struct RecA: u8 unk00[0x1C] fixes the offset of unk1C
  src/matched/unit_02018D10.c              B1 struct Sav: u8 unk08[0x80] fixes the offset of unk88[0x54]
  src/matched/unit_0201AC2C.c              B2 index multiply by 0xDC: i++) { void *entry = &p->unk08[i * 0xDC]
  src/matched/unit_0201AFD4.c              B1 struct Personal: u8 unk00[0x1C] fixes the offset of unk1C
  src/matched/unit_0201BD28.c              B1 struct Waza: u8 unk15[3] fixes the offset of unk18[3]
  src/matched/unit_0201BEB8.c              B1 struct Waza: u8 unk00[0x0B] fixes the offset of unk0B
  src/matched/unit_0201C020.c              B1 struct Waza: u8 unk00[0x15] fixes the offset of unk15[3]
  src/matched/unit_0201C1F4.c              B1 struct Waza: u8 unk00[4] fixes the offset of unk04
  src/matched/unit_0201CE5C.c              B1 struct Rec: u8 pad00[0x18] fixes the offset of unk18
  src/matched/unit_0201D9C8.c              B1 struct Font: u8 pad10[0x40] fixes the offset of *unk50
  src/matched/unit_0201DAF0.c              B1 struct Font: u8 pad10[0x40] fixes the offset of *unk50
  src/matched/unit_0201DC6C.c              B1 struct Font: u8 pad10[0x40] fixes the offset of *unk50
  src/matched/unit_0201DD58.c              B1 struct Font: u8 pad10[0x40] fixes the offset of *unk50
  src/matched/unit_0201F020.c              B1 struct Entry: u8 tag[5] fixes the offset of *str
  src/matched/unit_0201F0F8.c              B1 struct Entry: u8 tag[5] fixes the offset of *str
  src/matched/unit_0201F31C.c              B1 struct Entry: u8 tag[5] fixes the offset of *str
  src/matched/unit_02020CF8.c              B1 struct Obj: u8 pad[0x3C] fixes the offset of *unk3C
  src/matched/unit_02022608.c              B1 struct Sub: u8 pad00[0x24] fixes the offset of unk24
  src/matched/unit_020226A0.c              B1 struct Obj: u8 pad00[0xC] fixes the offset of unk0C
  src/matched/unit_02028F18.c              B1 struct SoundParam2: char pad00[4] fixes the offset of group
  src/matched/unit_0202C4CC.c              B1 struct Obj0202C4CC: u8 pad[0x16] fixes the offset of unk16
  src/matched/unit_0202C510.c              B1 struct Arr0202C510: u8 pad[0x18] fixes the offset of unk18[1]
  src/matched/unit_0202DE6C.c              B2 index multiply by 8: void sub_0202DE6C(u32 a, u32 b, u32 c, const struct Pos0202DE6C *base, struct Pos0202DE6C 
  src/matched/unit_0202E54C.c              B1 struct Obj0202E54C: u8 pad[0x20] fixes the offset of *unk20
  src/matched/unit_0202EC24.c              B1 struct Ent0202EC24: u8 pad[0x26] fixes the offset of 4
  src/matched/unit_02030C68.c              B1 struct Ctx02030C68: u8 buf[0x1c] fixes the offset of **stack
  src/matched/unit_02030DA8.c              B1 struct Cfg: u8 pad_00[0xc] fixes the offset of unk_0c
  src/matched/unit_020332FC.c              B1 struct Obj020332FC: char pad[0x138] fixes the offset of *slots
  src/matched/unit_02034508.c              B1 struct Obj02034508: char pad[8] fixes the offset of unk08
  src/matched/unit_02035120.c              B1 struct Obj02035120: char pad[0x18] fixes the offset of unk18
  src/matched/unit_020355C4.c              B1 struct Sys: u8 pad_00[0x3c] fixes the offset of unk_3c
  src/matched/unit_02035D5C.c              B1 struct Sio: u8 pad_00[0x0c] fixes the offset of buf[0x4c]
  src/matched/unit_020362BC.c              B1 struct Rec: u8 pad_00[0x1c] fixes the offset of *unk_1c
  src/matched/unit_020362EC.c              B1 struct Rec: u8 pad_00[0x18] fixes the offset of *unk_18
  src/matched/unit_02039C68.c              B1 struct Link: u8 pad_00[0x44] fixes the offset of unk44
  src/matched/unit_0204152C.c              B1 struct Channel: u8 pad00[8] fixes the offset of *field_08
  src/matched/unit_0204162C.c              B1 struct Channel: u8 pad00[8] fixes the offset of field_08
  src/matched/unit_02043900.c              B1 struct BmpInfo: u8 pad08[8] fixes the offset of size
  src/matched/unit_02045C44.c              B2 index multiply by 8: func_0204960C(bank->stream, pos + (index * 8 + 4))
  src/matched/unit_02048AD0.c              B1 struct Light: u8 pad0C[8] fixes the offset of unk14
  src/matched/unit_0204AEC0.c              B1 struct Ctx: u8 pad_00[0x110] fixes the offset of *slots
  src/matched/unit_0204B22C.c              B1 struct Bank: u8 pad_00[8] fixes the offset of *unk_08
  src/matched/unit_0204B3DC.c              B1 struct Obj: u8 pad_00[8] fixes the offset of *unk_08
  src/matched/unit_0204B570.c              B1 struct Actor: u8 pad_00[0x10] fixes the offset of unk_10
  src/matched/unit_0204B59C.c              B1 struct Obj: u8 pad_00[0x10] fixes the offset of unk_10
  src/matched/unit_0204B5D4.c              B1 struct Actor: u8 pad_00[0x14] fixes the offset of unk_14
  src/matched/unit_0204B5F4.c              B1 struct Actor: u8 pad_00[0x58] fixes the offset of unk_58
  src/matched/unit_0204B688.c              B1 struct Actor: u8 pad_00[0x60] fixes the offset of 8
  src/matched/unit_0204B898.c              B1 struct Obj: u8 pad_00[0x5a] fixes the offset of unk_5a
  src/matched/unit_0204B9C4.c              B1 struct Anim: u8 pad_00[8] fixes the offset of unk_08
  src/matched/unit_0204D3C4.c              B1 struct Obj: u8 pad_04[8] fixes the offset of unk_0c
  src/matched/unit_0204D410.c              B1 struct Obj: u8 pad_04[8] fixes the offset of unk_0c
  src/matched/unit_0204D444.c              B1 struct Obj: u8 pad_04[8] fixes the offset of unk_0c
  src/matched/unit_0204D494.c              B1 struct Obj: u8 pad_04[8] fixes the offset of unk_0c
  src/matched/unit_0204D4C4.c              B1 struct Obj: u8 pad_04[8] fixes the offset of unk_0c
  src/matched/unit_02060614.c              B1 struct Elem: char pad00[0x0c] fixes the offset of sublist
  src/matched/unit_020742E4.c              B1 struct Header: char pad00[8] fixes the offset of offset_08

C  integer/pointer cast -- 48 files
  src/calib/card_getownromheader.c         C integer literal cast to a pointer: 0x02FFFE00
  src/calib/fx_divs32.c                    C integer literal cast to a pointer: 0x04000280
  src/matched/func_020216D4.c              C integer literal cast to a pointer: 0x05000000
  src/matched/func_0203FC28.c              C integer literal cast to a pointer: 0x04000000
  src/matched/func_02046084.c              C hdr (integer) cast to a pointer
  src/matched/func_0204CBB0.c              C a pointer cast to an integer type (e)
  src/matched/func_0204DC70.c              C integer literal cast to a pointer: 0x04000000
  src/matched/func_0204E824.c              C integer literal cast to a pointer: 0x0400006c
  src/matched/func_02062A3C.c              C a pointer cast to an integer type (block)
  src/matched/func_02062A88.c              C a pointer cast to an integer type (block)
  src/matched/func_02062B10.c              C a pointer cast to an integer type (block)
  src/matched/func_02062B20.c              C a pointer cast to an integer type (block)
  src/matched/func_02065C78.c              C integer literal cast to a pointer: 0x04000448
  src/matched/sub_02008534.c               C a pointer cast to an integer type (obj)
  src/matched/sub_0200F6D8.c               C integer literal cast to a pointer: 0x0214617C
  src/matched/sub_0200F800.c               C integer literal cast to a pointer: 0x0214617C
  src/matched/sub_0200FB24.c               C integer literal cast to a pointer: 0x0214617C
  src/matched/sub_0200FB6C.c               C integer literal cast to a pointer: 0x0214617C
  src/matched/sub_020159C8.c               C a pointer cast to an integer type (obj)
  src/matched/sub_02018DB4.c               C v (integer) cast to a pointer
  src/matched/sub_02018E34.c               C v (integer) cast to a pointer
  src/matched/sub_02019308.c               C v (integer) cast to a pointer
  src/matched/sub_02019388.c               C v (integer) cast to a pointer
  src/matched/sub_020303D8.c               C a pointer cast to an integer type (arena)
  src/matched/sub_02034768.c               C integer literal cast to a pointer: 0x04000000
  src/matched/sub_02039338.c               C a pointer cast to an integer type (owner)
  src/matched/sub_02040860.c               C integer literal cast to a pointer: 0x04000020
  src/matched/sub_02040968.c               C a pointer cast to an integer type (dest)
  src/matched/sub_0204335C.c               C integer literal cast to a pointer: 0x06800000
  src/matched/sub_02043598.c               C integer literal cast to a pointer: 0x04001000
  src/matched/sub_020435F4.c               C integer literal cast to a pointer: 0x04001000
  src/matched/sub_02043B5C.c               C a pointer cast to an integer type (buf)
  src/matched/unit_02005C4C.c              C integer literal cast to a pointer: 0x04000210
  src/matched/unit_0200616C.c              C a (integer) cast to a pointer
  src/matched/unit_02006DAC.c              C a pointer cast to an integer type (a)
  src/matched/unit_0200852C.c              C a pointer cast to an integer type (obj)
  src/matched/unit_0200A3FC.c              C integer literal cast to a pointer: 0x02FFFC3C
  src/matched/unit_0200DA64.c              C a pointer cast to an integer type (p)
  src/matched/unit_0200DD8C.c              C a pointer cast to an integer type (p)
  src/matched/unit_0201042C.c              C integer literal cast to a pointer: 0x021461E0
  src/matched/unit_02013FD8.c              C integer literal cast to a pointer: 0x0209DFDC
  src/matched/unit_02014CB8.c              C a pointer cast to an integer type (o)
  src/matched/unit_020169D4.c              C integer literal cast to a pointer: 0x040004C0
  src/matched/unit_0201D5C4.c              C a pointer cast to an integer type (p)
  src/matched/unit_0202DE9C.c              C integer literal cast to a pointer: 0x04000000
  src/matched/unit_020434DC.c              C integer literal cast to a pointer: 0x04000000
  src/matched/unit_0204361C.c              C integer literal cast to a pointer: 0x04001000
  src/matched/unit_02079164.c              C a pointer cast to an integer type (cur)

```
