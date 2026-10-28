# The runtime: frame, scheduler, heap, platform layer, boot

## The frame skeleton

`engine/frame/` is the per-frame shape the game will run on, and it contains
no SDL: it is the DS's frame *convention*, not the host's window, so a
headless test drives it exactly as the platform layer does.

One iteration is **step, then vblank, then render**, and that order is
attested by the hardware rather than chosen. The game's main loop runs
during the visible period and builds the next picture into shadow copies of
OAM and of the BG registers; the vblank interrupt is where those shadows may
be committed, because the display is scanning the real ones out at any other
time. A port that renders before it commits shows the previous frame's OAM,
which is a one-frame lag on everything that moves. The frame and tick
counters advance at the vblank too, because on hardware that interrupt is
what the OS counts: a step function therefore sees the number of the frame it
is building, and a render function the one it is about to show.

The game's own scheduler sits on top of this rather than inside it, and it is
in the build now: `src/matched/sub_020310C4.c` (tcbl.c's runner) **is** the
step function `demo/eidos_view` installs. See "The game's scheduler" below.

**The OS tick is not 64 Hz.** `src/matched/func_02021444.c` (palanm.c) stamps
its module state block with `OS_GetTick()`'s full `u64`, stored as one
4-aligned `long long`, which fixes both the width and the unit. The SDK tick
is the system clock divided by 64, **523655.97 Hz**; the 64 is the timer
prescaler's divisor, not a frequency. One DS frame is 560190 / 64 = 8752
ticks and 62/64 of another, so `engine/frame/frame.c` advances by 8752 and
carries the remainder, adding a whole tick every 32 frames. Dropping it would
run the game's clock half a second slow per hour. `sdk/os_tick.c` is
`OS_GetTick` and `OS_GetTickLo` over that counter, so a tick difference the
game measures is a whole number of frames' worth whatever the host was doing,
and a headless run that steps frames without sleeping advances the game's
clock exactly as a paced one does.

`tests/test_frame.c` is synthetic: the callback order, that a value the step
function writes reaches the renderer only through the vblank commit, that 32
frames are exactly 280095 ticks, and the tick/millisecond conversions.

**Under the game's own boot** (`demo/eidos_boot.c`, see "Running the game's
boot") the same loop is the driver, and NitroMain's own loop runs as
written: the loop has no step hook, and a frame is one call of the OS
layer's halt hook, made when every game thread waits (NitroMain in
`OS_WaitIrq` for the vblank). The vblank hook does the hardware's half of
the interrupt only: it sets DISPSTAT's vblank flag and raises IF bit 0
through the OS layer (`bw_os_raise_irq`). The OS layer owns IF; it takes
the request when the halt returns, runs the handler the game installed in
`OS_IRQTable` and wakes the threads waiting in `OS_WaitIrq`, as
`OS_IrqHandler` does on the DS (docs/boot.md, "Running the boot").

## The game's scheduler, and the first matched code inside the frame

`aletheia`'s tcbl.c is the game's task-callback list: a manager owning a
fixed pool of nodes, a caller registering a callback with a priority and
getting a node back, and one pass of a runner walking the live nodes in
priority order and calling each callback with the node and its work area.
That pass is what a DS game does once per frame, and it is now what
`engine/frame/` calls as the step.

**What runs through matched code now.** Everything in this list is
`src/matched/`, compiled unchanged for the host and actually executed by
`ctest`:

- **The heap.** `sub_02030734`, the allocation entry point with 1163 callers
  in the image, over `sub_02030098`, the only function in the game that takes
  memory from a heap: the 0x7fff heap-id mask and its bit-15 direction flag,
  the 0x1c-byte block header with the `0x194E` signature, the per-heap
  live-allocation counter, and the three result codes. Around it
  `sub_02030880` stamps the allocation-site provenance pair into the header,
  `sub_020308B8` reads it back and validates it, `sub_02030A00` filters on
  the watched-heap id and `sub_02030984` audits, `sub_02030428` /
  `sub_0203046C` / `sub_020304B8` / `sub_020304C0` are the lookups, and
  `unit_0202FBAC`'s four accessors resolve a heap id to its record.
- **The release side of the heap**, new since wave 26. `sub_020307B0`, 904
  branch references image-wide, is the counterpart of the allocation entry
  point: `sub_02030A2C` runs the free-side watch filter, `sub_020309B8` the
  free-side audit and `sub_02030344` the checked heap-id lookup under it,
  and then the raw free walks back over the 0x1c-byte header. The port used
  to reimplement the whole of that as one function.
- **The expanded heap's front door.** `func_02060CA0`: round the request up
  to a word, and take the block off the head of the arena for a non-negative
  alignment or off its tail for a negative one, which is how the raw
  allocator spells bit 15 of the public heap id. Its two callees are still
  the port's; see "The heap environment" below.
- **The task list's construction.** `sub_0203101C` allocates the 0x38-byte
  manager and the node pool, stores the count, the work size and the home
  heap, and self-links the sentinel; `sub_02030FC8` walks every row of the
  pool into the free list.
- **The runner.** `sub_020310C4`, one pass: clear the flag, call the node's
  callback with the node and its work area, and either advance or unlink,
  with the walk cursor published in the manager so a callback can destroy a
  node from inside another one.
- **The rest of tcbl.c, under ILP32.** Wave 26 matched the five functions the
  port used to supply -- `sub_020311B8` (register a callback in ascending key
  order), `sub_02031194` (destroy one node, deferring to the runner when it
  is the node being run), `sub_0203115C` (destroy every node), `sub_02030F50`
  (reset a node) and `sub_02030F70` (unlink one and release its work block).
  In the reference configuration **the whole of tcbl.c is aletheia's own C
  and nothing of the task-callback list is a reimplementation.** On an LP64
  host those five stay out, for a reason that is about the family's own
  declarations rather than about what is decompiled; see "Why nine matched
  files are out of the LP64 build" below.

`tests/test_tcbl.c` drives all of it with no SDL, no game data and no
oracle: **49 checks**, including that the constructor's two allocations show
up in the matched heap's own live-block count, that the pool is exactly as
deep as it was asked for (which is only true if the matched free-list build
reached every row), that the provenance the matched allocator stamped reads
back as `tcbl.c` through the matched validator, that a pass over an empty
list is a no-op, that three callbacks fire in priority order with their own
work areas, that a node removed from outside a pass drops out of the order
and can be re-registered at a new priority, that a node which destroys
itself from inside its own callback still lets the rest of the pass finish
and is gone from the next one, and that the heap balances back to zero.

**The ordering assertion no longer rests on a reimplementation.** "Three
callbacks fire in priority order" used to be half a statement about matched
code, the runner's forward walk, and half about the port's transcription of
0x020311B8's listing. Under ILP32 both halves are matched now: the
ascending-key insert is `src/matched/sub_020311B8.c` and the forward walk is
`src/matched/sub_020310C4.c`. Nothing in the test had to change for that,
which was the point of writing it against the behaviour rather than against
the stand-in.

`demo/eidos_view` then puts one task on such a list and makes the frame
loop's step `sub_020310C4`. The task advances the Pikachu pose along its
NANR timeline and writes the scroll, the sprite position and the animation
time into the pending register block; the vblank commits; the renderer
recomposes the pose at the committed time. `tests/test_view.c` takes a
screenshot after frame 1 and another after frame 12 and requires the bottom
screen to have moved by at least 200 pixels while the top screen, which has
no task, is **pixel-identical**. Measured: top 0, bottom 1223.

That is the demo's list. Under the game's own boot the step is NitroMain's
loop instead (previous section), and the lists the runner walks are the ones
the game's code builds.

### Why nine matched files are out of the LP64 build

This is the first place the port has had to leave an EXACT matched source
out, and the reason is a property of the game's data rather than of any
file: **the task-callback node and its manager are the first game-owned
structs in the port that store pointers.**

It is also the question that settled the port's pointer-width policy, and
the answer is that these four are out of the *LP64* build and in the ILP32
one: `docs/pointer-width.md` has the measurement and the decision, and
"Pointer width, and the reference configuration" below has the short form.
Configured with `cmake/ilp32-toolchain.cmake` all four are compiled as
written, straight out of aletheia, and every test still passes.

Portability rule 5 below says no pointer may be stored in a struct the
game's layout owns, and until now the port could honour it, because the one
such struct (`ArcFile`) is one the port itself fills in and it stores an
index. A tcbl node cannot: its `prev`, `next`, `owner` and `func` are
pointers the *game* writes. On the DS a pointer is four bytes and the node
header is 0x18 with the manager at 0x38; on an LP64 host the same
declarations produce a 40-byte node header and an 88-byte manager, and every
field after the first pointer moves.

That is harmless for a matched file that declares the whole struct, because
its own compiler lays it out consistently and every other file in the family
agrees. It is fatal for a matched file that reaches one field through
explicit padding, and four of them do:

| file | what it does | why it cannot run here |
| --- | --- | --- |
| `sub_02030EF0.c` | the pool row address | returns an address in a `u32`, and reads the table pointer at DS offset 0x14 (24 here) |
| `sub_02030F34.c` | free-list push | reaches the free-list head through `char pad[0x34]`; it is at 80 here |
| `func_02030F04.c` | free-list pop | the same `char pad[0x34]` |
| `sub_02030FB4.c` | the work-area accessor | reads the owned-work pointer at +0x10 (28 here) and returns node + 0x18 (40 here) |

Each one is byte-correct for the DS and nothing about it is wrong. They stay
in aletheia untouched, and on an LP64 host a `stubs/reimpl_` file stands in,
naming the matched source it replaces and transcribing its body against the
struct rather than against the numbers. `sdk/tcbl.h` carries the host layout
with the DS offsets beside it.

**Wave 26 added five more to that list, and for a different reason.** The two
findings the port had recorded as open questions -- the node's `+0x00` and
its `+0x10` are declared `u32` in the tcbl files that were in the build and
both of them hold pointers -- were answered by the five new EXACT sources,
and the answer is that they are pointers: `src/matched/sub_02030F50.c`,
`sub_02030F70.c` and `sub_020311B8.c` declare the node as six members with
`struct Tcbl *owner` at `+0x00` and `void *work` at `+0x10`.

That is a better declaration of the same object, and **on the DS and on any
ILP32 target it *is* the same object**: 0x18 for the node and 0x38 for the
manager either way. On an LP64 host it is not. The pointer form lays the node
out at 48 bytes and the manager at 96; the `u32` form, which
`src/matched/sub_0203101C.c` and `src/matched/sub_020310C4.c` still use,
gives 40 and 88. The family has stopped agreeing with itself, and something
has to pick one reading:

- the constructor allocates `sizeof(struct Tcbl)` as *its own* compiler sees
  it, so whatever it says the manager is, the manager is;
- the runner walks the node with *its own* offsets on every frame.

Both are in the build unconditionally, so the `u32` form is what an LP64
build has to hold, and the five pointer-form sources join the four
offset-reaching accessors outside it. **This is not a defect in either set of
declarations**: each is byte-correct for the DS, which is the only target
aletheia has to satisfy. It is the pointer-width policy earning its keep a
second time, and the sharpest argument yet for ILP32 being the reference
configuration -- there the port does not choose between two matched readings
of one struct, it configures a target on which there is only one.

Which of the two lists goes into the library is `EIDOS_TCBL_MATCHED`, whose
default CMake decides from `CMAKE_SIZEOF_VOID_P`: on a 32-bit target the nine
matched sources are compiled as written and the stand-ins are left out, and
the node and the manager come out at exactly `0x18` and `0x38`, the DS's own
sizes.

One consequence is visible in behaviour rather than in layout. The node's
`+0x10` is the work block a node owns when its request does not fit inside
the node, and on an LP64 host there is no padding to borrow the way there is
at `+0x00`. So the port refuses an oversize work request there rather than
storing a truncated pointer, and every node on that host carries its work
area inside itself; under ILP32 the matched registrar allocates the block
from the list's home heap and the matched unlink releases it, exactly as the
DS does. `sdk/tcbl.c` keeps the refusal in both configurations so that the
port has one behaviour to test rather than two.

And one arithmetic adjustment, which is the port's own and is confined to one
line: `sub_0203101C` sizes the pool as `(((workSize + 3) & ~3) + 0x18) *
count` with the DS node header written out as a literal. Rather than keep the
constructor out of the build over one constant, `bw_tcbl_create` adds the
difference between the two headers to the caller's work size before handing
it over, so the rows the matched arithmetic produces come out exactly `40 +
work` bytes apart and the last one ends exactly at the end of the table.

The table in this section lists the four accessors. The five wave-26 files
are `sub_02030F50.c`, `sub_02030F70.c`, `sub_020311B8.c`, `sub_02031194.c`
and `sub_0203115C.c`, and none of them reaches a field through a byte offset
at all: every one declares the whole struct. They are out of the LP64 build
purely because the struct they declare is not the struct the constructor
allocates there.

### The heap environment, and what is under the allocator

`g_EnvModule` at 0x02146954 is the heap manager, and the matched code reads
it constantly: the id-table length at +8 bounds the id, the byte table at +4
maps a public heap id to an entry index, the entry array at +0 holds each
heap's handle and its live-allocation count, and the halfword at +0xe is the
result code every one of them writes.

**In the reference configuration the whole heap is the game's own, down to
the arena.** aletheia has since matched the heap environment constructor
(`sub_0202FC48`) and everything under it, so with `EIDOS_HEAP_MATCHED` on
(the default wherever pointers are four bytes) the port builds the manager
the way the game's boot does and allocation runs matched to the bottom:

- **the constructor**, `src/matched/sub_0202FC48.c`: the entry table (the
  caller's heaps plus 0x18 spares, 16 bytes each), the id table (clamped up
  to the entry count, every byte 0xff) and one region per heap, all taken off
  arena 0, each region turned into a NitroSystem expanded heap by
  `func_02060C5C` / `func_02060814` / `func_020606D0`, which also puts it on
  the root heap list;
- **allocation**, `func_02060CA0` into `func_02060A08` (first fit from the
  head of the free list) or `func_02060ABC` (from the tail, for a heap id
  with bit 15 set), and the carve that splits the chosen block, leaving any
  remainder of 0x14 bytes or more as a new free block;
- **release**, `func_02030164` (the raw free: signature 0x194E to 0x194D,
  the live counter down) into the expanded heap's free, which returns the
  region with `func_02060B6C`, merging it with a free neighbour on either
  side;
- **child heaps**, `sub_0202FDC4` and `func_0202FED8`: a region allocated
  from a parent heap and bound to the first unused spare entry, which is
  what the game's boot does with `func_0203064C(1, 7, 0x25000)`
  (`src/matched/sub_0200545C.c`). `bw_heap_create_child` is that call;
- **the queries** the audits use: free bytes (`func_02060E6C`), the largest
  allocatable block (`func_02060E94`), a block's size (`func_02060F90`).

What the port supplies is the arena and the heap sizes. `sdk/os_arena.c` is
`OS_AllocFromArenaLo` (`sub_02086788`) over one region the port allocates
itself, sized by `bw_heap_init_sizes` to exactly what the constructor will
take. The sizes are the port's because the game's own table (`d_0209D8A0`
on TWL, `d_0209D8B0` on NTR, two heaps and 0xAD ids) is ARM9 rodata in the
player's image; the boot path is where that table gets read.

**Two findings about the matched sources, both handled in the port layer.**

- *Two names, one object.* `func_020606A8.c` names the root heap list
  `g_02146B64` where `func_020606D0.c` says `sRootList`, and
  `func_0202FED8.c` names the manager `g_02146954` where everything else says
  `g_EnvModule`. The DS links by address and gets one object; a host links
  by name and would get two. CMake compiles each of those two files with its
  spelling `#define`d to the other's, so the files are untouched and the
  object is shared.
- *A frame-layout dependence.* `func_02060888.c` (the carve) and
  `func_02060E30.c` (the free) declare `int end; int start;`, let
  `func_02060764(&start, blk)` write a two-word region, and read the second
  word as `end` (and pass `&end - 1` as the region). That is right exactly
  when the compiler puts `end` in the word above `start`, which mwccarm does,
  and is out of bounds in C. Under GCC the carve read a word it never wrote
  and gave the first split block a garbage size; `test_heap` caught it on
  the first allocation. `stubs/reimpl_02060888.c` and
  `stubs/reimpl_02060E30.c` are the two bodies with the pair declared as an
  array and nothing else changed. They are the only two files in the corpus
  with the `&local - 1` shape. Declaring the pair as an array in aletheia
  would make both portable, if match.py agrees that the bytes survive; that
  is aletheia's call.

**On an LP64 host** none of this can run (an expanded-heap block header is
0x10 bytes with two pointers in it, and every file in the chain does its
address arithmetic in `int`), so `EIDOS_HEAP_MATCHED` is off there:
`sdk/game_globals.c` fills in the fields the upper layers read with a token
per heap, and `stubs/reimpl_02060A08.c`, `stubs/reimpl_02060ABC.c` and
`stubs/reimpl_02030164.c` take blocks from the host allocator. The manager's
contract (ids, result codes, the live counter, alignment) is the same, and
`test_heap` checks it in both configurations; the addresses are not, and the
arena checks run only under ILP32.

**`tests/test_heap.c`** needs no game data. Every expected number is worked
out on paper from the rules above and each check names its source: the
arena used to the byte (27 entries, a 28-byte id table and three regions),
the heap heads at the region starts with their 'EXPH' signature and a single
free block of region - 0x38 - 0x10 bytes, a head allocation placed right
after the first header and a tail one ending at the heap's end with its tail
bit set, both merges on free, first fit into a hole, the 0x14-byte
remainder rule in both directions, the exact fit that empties the free list
and the word more that fails with result 3, a region under 0x4c bytes that
the constructor turns into "no heap", child heaps from the head and the tail
of a parent (on the parent's child list, not the root list) and all four of
their failure codes including the 0x18-spare limit, and fifty interleaved
blocks over three heaps that never overlap and all come back. **206 checks
under ILP32, 89 under LP64, 0 failures**, and clean under AddressSanitizer
and UBSan in both.

The out-of-memory report (`sub_02030B50`) stays a stub: it is EXACT in
aletheia, but its closure reaches the game's debug print (`func_0209128C`,
open), and it runs one call before `OS_Terminate`.

## The platform layer

`engine/platform/` is the only part of eidos that includes SDL3 and the only
optional part of the build (`-DEIDOS_SDL=OFF`, or simply no SDL3 present:
the engine library and every other test still build and pass).

**The window** is an integer multiple of the DS layout: two 256x192 screens
stacked vertically, which is the hardware's own arrangement, or side by side
with `--side-by-side`, with a configurable gap. Integer scales only. The 2D
engine places every pixel exactly, against a Python oracle, and a fractional
scale would resample that; the render hint is nearest-neighbour for the same
reason.

**The keyboard** is a table of `SDL_Scancode` values, which are physical key
positions rather than characters, so the defaults land on the same keys on a
QWERTZ or AZERTY keyboard. A caller may pass its own table. The defaults:

| DS | key | DS | key |
| --- | --- | --- | --- |
| d-pad | the arrow keys | START | Return |
| A | X | B | Z |
| X | S | Y | A |
| L | Q | R | E |
| SELECT | Right Shift, Backspace | | |

Escape quits, F12 writes a screenshot and a held Tab runs the game twice
as fast, left Shift+Tab as fast as the computer can (the window keeps its
frame rate, the sound keeps playing and skips ahead); those three are the platform
layer's own and are not in the table. The button word keeps the DS keypad's bit order
(`KEYINPUT`, the SDK's `PAD_*`) but is active high, because every caller asks
"is A down" and the inversion belongs at the one place that reads the
register. No key may be bound to two buttons: the word is rebuilt from the
whole table on every pump, which is also why a key held across a focus change
cannot leave a bit stuck down.

**Gamepads** go through SDL's game controller API with a second table of the
same kind (`EidosPadBinding`: a button, or an axis past half its travel). The
default is positional, DS A/B/X/Y on the east/south/north/west face buttons
(SDL is told to report positions on Nintendo pads too), the shoulders and
triggers as L and R, Start and Back as START and SELECT, the d-pad and the
left stick as the d-pad. Pads plugged in while running are picked up.

**The stylus** is the left mouse button, or a finger, on the bottom screen,
mapped through the window's current size. Every pump hands the buttons and
the stylus to the port's input layer (`sdk/input.h`), which puts them where
the game reads them, `KEYINPUT`, the ARM7's X/Y/hinge halfword and the touch
panel's PXI protocol, at the next frame boundary. `docs/input.md` is the
design, including the scripted input tests and the headless boot use.

**The pacer runs at the DS's rate, not at 60 Hz.** A frame is 355 dots x 263
lines x 6 clocks at 33513982 Hz = 560190 clocks, i.e. 59.8261 Hz and a period
of 16715113 ns plus 3790034/33513982 of a nanosecond. The pacer keeps that
exact rational period and accumulates the remainder, so the n-th frame is due
at `round(n * 560190 * 10^9 / 33513982)` ns rather than n times a rounded
constant: 16 ms would run 4.3 percent fast, and even 16666667 ns gains a
frame every six minutes. A caller that falls more than four frames behind (a
breakpoint, a suspended laptop) is resynchronised rather than made to run the
backlog, and the count of those resynchronisations is readable. The renderer
is created **without** `SDL_RENDERER_PRESENTVSYNC`: a 60.000 Hz or 144 Hz
panel would otherwise pull the game off the DS's rate.

**The audio device** is opened at 32768 Hz, stereo, signed 16-bit, which is
the DS mixer's own output rate, so the sequencer does not resample twice,
and the buffer is 1024 frames, 31 ms, under two DS frames. A machine with
no sound device prints a line and runs silent rather than refusing to
start. The callback itself is a plain function pointer over that buffer,
which is exactly what `bw_audio_player_mix` fills, so the whole of the
wiring between the platform layer and `engine/audio` is one thunk and one
`eidos_platform_set_audio` call. It runs on SDL's audio thread and never
allocates: the player prepared every note and decoded every wave it needs
when it was created, and mixing touches nothing but its own sixteen voices
and a block buffer on the stack.

**The frame upload** is two 256x192 RGBA buffers into two streaming textures
created as `SDL_PIXELFORMAT_ABGR8888`, which is the packed pixel `0xAABBGGRR`
and therefore the byte order R, G, B, A that `g2d_frame_compose` writes.

**The screenshot** (`SDL_SaveBMP`) is composed from the port's own
last-uploaded buffers rather than read back out of the renderer: a read-back
is driver-dependent and under the dummy video driver there is nothing to read
back at all, while the shadow copies are exactly what the engine produced. It
is written at 1x in the configured layout, as RGB24, so SDL writes a plain
24-bit BMP that a test can parse with no image library.

## eidos_view, the first visible output

```
build/eidos_view [--data-root <extract/data>] [--scale N] [--side-by-side]
                 [--gap N] [--species N] [--map N] [--frames N]
                 [--shot PATH] [--shot-first PATH] [--anim-speed N]
                 [--no-audio] [--music [SEQ]]
```

`--map N` replaces the top screen with the 3D layer: one `a/0/0/8` map,
building or dungeon model, decoded once into a geometry buffer in model
space and transformed and rasterised every frame by `engine/g3d`, with
its textures out of `a/0/1/4`. `--map 120` is `pl_00_00`, the interior
`tests/test_g3d.c` compares vertex for vertex against aletheia's OBJ, and
`--map 0` is `map24_23`, an outdoor terrain chunk. The camera is a
**second task on the same task-callback list**, at a higher priority than
the animation task so the matched runner reaches it first; it writes the
eye's yaw, pitch and distance into the pending register block and the
vblank commits them, exactly as the animation task writes the sprite's
position. The arrows orbit, X and Y (or L and R) move in and out, START
recentres. The angles are 16-bit binary angles into `engine/g2d`'s own
sine table, so the demo still needs no libm.

`--music` plays a sequence out of the player's own `wb_sound_data.sdat`
through `engine/audio`, looping; with no argument it plays SEQ 1062,
`SEQ_BGM_GYM`, which is one of the three the audio test checks sample for
sample against the Python synth. It is behind a flag because it is the
demo's only reason to open a 50 MB archive, and the title music replaces it
once the boot path names its SEQ id. On exit the demo reports how many
frames it mixed and how many note-ons found no free channel.

The top screen is `titledemo.narc` member 0, the 256x256 8bpp title screen
the asset lane identified, over the NCGR/NCLR pair `background.py`'s coverage
rule paired with it (members 1 and 2, the same triple `tests/test_bg.c`
checks pixel for pixel against the Python renderer). The bottom screen is one
composed rig pose out of `a/0/0/4`, species 25 by default. Both come from the
player's own `extract/`; nothing is bundled.

**The frame loop's step is game code.** The demo builds a task-callback list
through the matched constructor and registers one task on it; the step
function is `src/matched/sub_020310C4.c`, one pass over that list, and the
loop does not know what is on it. The task advances the pose along its NANR
timeline and writes the next frame's scroll, sprite position and animation
time into a pending register block; `view_vblank` commits it; `view_render`
reads only the committed copy and recomposes the pose at the committed time,
which is what OAM and the BG registers do on hardware. The sprite's bob comes
from the engine's own fixed-point sine table, so the demo needs no libm.

`tests/test_view.c` runs it headless. ctest sets `SDL_VIDEODRIVER=dummy` and
`SDL_AUDIODRIVER=dummy` on the test, so the window, the textures, the pacer
and `SDL_SaveBMP` all run on a machine with no display and no sound card;
the test then parses the BMP: 256 x 384 at 24 bits, the two screens stacked
at 1x, and neither half blank. The thresholds for "not blank" are
deliberately low (eight distinct colours and five hundred pixels off the
backdrop per half), because the bottom screen is one 48x52 sprite on a
backdrop. It then compares the screenshot taken after frame 1 with the one
after frame 12: the bottom screen must have moved by at least 200 pixels and
the top screen, which has no task, must be pixel-identical. Measured: top 0,
bottom 1223. What the picture *is* stays `test_bg`'s and `test_g2d`'s
business, against the Python renderers.

## Running the game's boot

`docs/boot.md` is the design for running the game's real start-up rather
than a demo's: what runs, the three layers every SDK name falls into, how
fixed DS addresses are rewritten at build time, the address space, and the
thread and interrupt model. `docs/title-closure.md` lists what still blocks
the title screen. The pieces in the tree:

- `sdk/dsmem.c`, the DS address space every matched fixed-address access
  goes through (`tools/dsaddr.py` rewrites each matched source's literal
  casts into `bw_ds_ptr` calls in a build-tree copy, `cmake/dsaddr.cmake`);
- the OS layer (`sdk/os_*.c`, `sdk/plat_thread.c`): threads on host threads
  under one run token, the interrupt controller, alarms and the tick;
- `sdk/pxi.c` and `sdk/arm7/`, the FIFO between the processors and the ARM7
  stand-in that answers it;
- `sdk/firmware.c`, `sdk/save.c` and the save chip, firmware flash and
  microphone responders in `sdk/arm7/`: the owner's settings at power-on
  from an editable `firmware.ini`, the game's save in `pokeblack.sav` (a
  raw image, emulator-compatible, replaced atomically), the firmware flash
  in `firmware.bin` (`docs/firmware.md`; the save folder per platform is in
  `docs/installer.md`);
- `sdk/input.c`, `sdk/arm7/pad.c` and `sdk/arm7/tp.c`, the buttons, the
  hinge and the touch panel from the host or a script (`docs/input.md`),
  and `sdk/cp.c`, the divider and square-root unit the touch calibration
  divides on;
- `sdk/fs.c` and `sdk/overlay.c`, the ROM file system and overlays onto the
  extract;
- `bw_port_reset` (`sdk/port.h`), the one power-on: `bw_ds_reset`, then
  `bw_os_reset`, then `bw_pxi_reset`, then `bw_cp_reset` (the divider), then
  the joins (PXI interrupt requests go to the OS layer's latch, and every
  `OS_RestoreInterrupts` gives the ARM7 its turn). The boot and every test
  that runs game code on the whole machine call it rather than the resets;
- NitroMain (aletheia's `src/matched/arm9/func_02005124.c`, or
  `stubs/reimpl_02005124.c` with an older checkout) and
  `demo/eidos_boot.c`, its driver (headless, or `--window` with SDL3).

```sh
cmake -S . -B build -G Ninja -DALETHEIA_ROOT=../bw_decomp -DEIDOS_BOOT=ON
cmake --build build --target eidos_boot
./build/eidos_boot --data-root ../bw_decomp/extract/data --frames 600
```

With `EIDOS_BOOT` off (the default) `eidos_boot` builds without the game's
code, says so and exits 0. With it on, CMake links every matched ARM9 game
file and its closure (`tools/closure.py`, `tools/bootlink.py`: names by
address, the ARM9 image's data, the stubs it lists) and the title-path
overlays (`tools/ovlink.py`), and the boot runs to the title menu: the
logos, the copyright screen, the opening, the title screen, and with START
(`--press START@4800`, or the key in a window) the menu, which
`test_boot_run` asserts. In a 32-bit build the DS address space sits at its
own addresses in the process, so the pointers the game keeps in its data
are valid as they are. `docs/boot.md`, "Running the boot", has how far it
gets and what it took; `--screenshot N[,N...]` saves both screens as PNG.

## Growing the source list

`CMakeLists.txt` carries a curated list of matched TUs. It is grown
mechanically, not by guessing:

```
python tools/closure.py --seed-func func_02018A7C
python tools/closure.py --seed-func func_02018A7C --cmake
python tools/closure.py --seed-func sub_0203101C
python tools/closure.py --list-closed
```

The `sub_0203101C` seed is the one the heap layer and the task-callback list
came in on, and `sub_02030734` (the allocation entry point) and
`sub_020311B8` (the registrar) are the other two the current source list is
grown from. Re-run after wave 26 they report:

- **`sub_0203101C`**: 20 matched translation units, five `sdk` names
  (`MIi_CpuClear32`, `OS_DisableInterrupts`, `OS_RestoreInterrupts`,
  `OS_Terminate`, `sub_02030524`), one `alias` (`sub_02060CA0`, defined as
  `func_02060CA0`), four `open` ones (`func_02060888`, `sub_02030B50`,
  `sub_02060E6C`, `sub_02060E94`) and three `data` ones (`g_EnvModule`,
  `g_020A7820`, `sub_0203101C_File`).
- **`sub_02030734`**: the same minus the three tcbl files.
- **`sub_020311B8`**: adds `func_02030F04`, a second `alias`
  (`sub_02030F04`) and the `data` name `sub_020311B8_File`, which is the
  same ARM9 word `0x020A7834` as `sub_0203101C_File` under the registrar's
  own spelling.
- **`sub_0203115C`** and **`sub_020307B0`**: the release closure, which is
  where `sub_02030344`, `sub_020309B8`, `sub_02030A2C` and the two open names
  `sub_02030164` and `sub_02060F90` come from.

`sub_02030F50` and `sub_02060CA0` have left the `open` lists entirely: both
are matched now.

**Two places the report and the build disagree, both on purpose.** Nine
matched units (`sub_02030EF0`, `sub_02030F34`, `func_02030F04`,
`sub_02030FB4`, `sub_02030F50`, `sub_02030F70`, `sub_020311B8`,
`sub_02031194`, `sub_0203115C`) are in the build only where pointers are four
bytes, see "Why nine matched files are out of the LP64 build". And
`func_02060A08` / `func_02060ABC` are matched, so the closure walks through
them and reports `func_02060888` as open; the port stops one level higher,
at `func_02060CA0`, and stands in for those two itself, because they walk an
arena the port has not got. The ILP32 configuration answers the rest of the
report exactly.

`tools/closure.py` also answers the pointer-width question over the whole
corpus rather than one closure:

```
python tools/closure.py --pointer-audit
```

counts the matched sources that hold an address in an integer, hardcode a
struct size or field offset, or cast between an integer and a pointer, and
prints the file list behind each count. `docs/pointer-width.md` is that
output plus the decision it supports.

`tools/closure.py` reads the manifest shards in aletheia's `layout/matched/`
and, from each matched source, its `symbols:` header block and every name it
needs from elsewhere (`extern` declarations, file-scope prototypes, and calls
with no declaration in scope, which mwcc accepts); it resolves a name by
spelling, then by DS address, including functions a unit defines beyond the
lead its manifest entry names. It walks the call graph through matched files
only and sorts every remaining reference into one class (the per-seed figures
above predate this reader, which finds more):

- `matched`, another matched source defines it: link that one too.
- `sdk` / `runtime`, an SDK library symbol or an MSL runtime helper: it
  belongs in `sdk/`.
- `alias`, a matched function referenced under a different name prefix than
  the file that defines it uses. The DS build resolves relocations by
  address and does not care; a host linker resolves by name and does. Each
  one needs a forwarder in `sdk/aliases.c`, or, in the boot, the per-file
  rename `tools/bootlink.py` generates (docs/boot.md, "Linking the boot").
- `const`, a linker symbol whose address is its value (`SDK_IRQ_STACKSIZE`,
  the overlay ids): the boot's generated headers supply the value.
- `open`, a game function at a known address that nothing in aletheia's
  `src/matched/` defines yet: it needs a `stubs/` entry.
- `data`, a global object: it needs a `sdk/game_globals.c` entry.

The point of the script is that the closure report and the linker always
agree. A name in the report that has no home is a link error waiting to
happen, and a link error that the report did not predict is a bug in the
script.
