#!/usr/bin/env python3
"""
title_closure.py -- which unmatched functions stand between power-on and the
title menu.

Regenerates docs/title-closure.md.  Everything is read from an aletheia
checkout (read-only); what is written out is addresses, sizes, overlay ids
and source file names that aletheia itself already records, never game
bytes.

Inputs (all inside --aletheia-root):

  layout/layout.json             overlay table: id, RAM range, static
                                 initialiser range, decompressed file
  asm/arm9/main.s, asm/overlays/ovNNN.s
                                 tools/disasm.py listings: function labels
                                 and their ISA (the denominator)
  layout/matched/<region>/*.json manifest shards: EXACT / EXACT* addresses
  layout/worklist.json           ARM9 coverage summary
  src/pending/**/*.c             drafts (header `address:` / `region:`)
  layout/file_anchors.txt        __FILE__ names per region
  extract/arm9_dec.bin, extract/overlays_dec/*.bin
                                 decompressed code (read, never committed)

Why it decodes code itself: disasm.py does not follow switch jump tables
(the cases after `add pc, rN` are emitted as data words) nor functions
reached only through a pointer, so the listing alone misses calls.  This
script flow-decodes every function from the image (Thumb and ARM, BL/BLX,
B, conditional branches, mwcc's Thumb `add pc` and ARM `addls pc` jump
tables) and uses the listing only for function labels and their ISA.

Method:

  1. NitroMain (0x02005124) is data words in aletheia's listing; its calls
     are decoded from the image.  The ARM9 closure from there is walked to
     constant-argument calls of the overlay loader (sub_02034AC4) and of
     the GFL proc API, read from aletheia's matched ARM9 code:
        0x020312CC  global CallProc     (r0 = overlay id, r1 = proc data)
        0x020312F8  global NextProc     (r0, r1)
        0x02031348  local  CallProc     (r0 = proc system, r1, r2)
        0x020314DC  local  NextProc     (r0 = proc system, r1, r2)
        0x020120B4  GameSystem CallProc (r0 = game system, r1, r2)
     A proc-data object is three code pointers (init, main, end);
     func_020315F0 loads the proc's overlay, runs them in turn and unloads
     the overlay after `end`.
  2. The root proc is ARM9 code.  Its procs, in the order its state
     machine starts them, are the top-level screens.
  3. Each screen's closure is seeded with init, main, end and the static
     initialisers of the overlays it loads.  Every edge gets a tier:
       static    BL/BLX/B to an address, or a `bx`/`blx` through a register
                 the backward scan resolves to a constant (including a load
                 through a constant pointer, and r0 after a call to a
                 function that always returns one constant);
       pointer   a code pointer the function loads from its literal pool or
                 builds with adr, or an entry of a constant function table it
                 calls through with an index (`ldr rN, [table, idx]; blx rN`);
       table     a code pointer inside a data object whose address the
                 function loads (bounded by the next address anything in the
                 region points at, at most 0x200 bytes);
     a node's tier is the best, over all paths, of the worst edge on the
     path.  Register calls the scan cannot resolve are listed as open.
     ARM9 targets are leaves (ARM9 is fully covered).  An overlay address is
     resolved against the overlays loaded at that point: the screen's, plus
     what the calling function and its direct ARM9 callees load
     (func_0200F17C swaps the net overlays in for its caller), the most
     recent first.
     Two switches have their selector pinned (SWITCH_CASE): the boot calls
     the net mode switches func_0200F17C / func_0200F234 with 1 only.
  4. Procs started inside a screen's static or pointer code become screens
     of their own, in discovery order.  Procs started by the last top-level
     screen (the title menu) are listed as exits and not followed.

A function counts as matched when a shard entry for its address (or a unit
entry naming it) is EXACT or EXACT*, the rule tools/ovstats.py uses.  Size
is the bytes of decoded instructions (jump-table cases included, pools and
tables excluded).

Usage:

    python tools/title_closure.py --aletheia-root ../bw_decomp
    python tools/title_closure.py --aletheia-root ../bw_decomp --out docs/title-closure.md
"""

from __future__ import annotations

import argparse
import bisect
import glob
import json
import os
import re
import struct
import sys

EIDOS_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

NITROMAIN = 0x02005124
NITROMAIN_END = 0x020051C4          # its literal pool starts here

PROC_API = {
    0x020312CC: ("global CallProc", 0, 1),
    0x020312F8: ("global NextProc", 0, 1),
    0x02031348: ("local CallProc", 1, 2),
    0x020314F4: ("local CallProc", 1, 2),
    0x020314DC: ("local NextProc", 1, 2),
    0x020120B4: ("GameSystem CallProc", 1, 2),
}
OVL_LOAD = 0x02034AC4
OVL_UNLOAD = 0x02034A5C

# Switches whose selector is fixed on this path.  func_0203CB34 (net.c, called
# by func_0200523C during boot) passes 1 to both; their other selectors swap in
# the overlays of the other communication modes, which only a menu starts.
SWITCH_CASE = {0x0200F17C: 1, 0x0200F234: 1}
MATCHED = ("EXACT", "EXACT*")
MAX_INSNS = 0x2000

HDR_RANGE = re.compile(r"address range\s+0x([0-9A-Fa-f]+)\.\.0x([0-9A-Fa-f]+)")
FUNCNAME = re.compile(r"^(?:func|sub)_([0-9A-Fa-f]{8})$")
LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_.]*):\s*$")


def h8(a):
    return "0x%08X" % a


def s32(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v & 0x80000000 else v


# --------------------------------------------------------------------------
# decoding
#
# IR: (addr, size, op, x, y, z)
#   const rd val | lit rd litaddr | adr rd val | copy rd rs | addi rd rs imm
#   mvn rd rs | lsl rd rs imm | w mask (registers clobbered)
#   call target isa | callr rs | jmpr rs | ret | b target | bc target
#   jt [targets] | other

def thumb_decode(img, base, a):
    """(ir, successors, terminal) for the Thumb instruction at a."""
    off = a - base
    h = struct.unpack_from("<H", img, off)[0]
    nxt = [a + 2]
    if (h >> 11) == 0x1E and off + 4 <= len(img):
        h2 = struct.unpack_from("<H", img, off + 2)[0]
        d = ((h & 0x7FF) << 12) | ((h2 & 0x7FF) << 1)
        if d & 0x400000:
            d -= 0x800000
        t = a + 4 + d
        if (h2 >> 11) == 0x1D:
            return (a, 4, "call", t & ~3, "arm", 0), [a + 4], False
        if (h2 >> 11) == 0x1F:
            return (a, 4, "call", t, "thumb", 0), [a + 4], False
        return (a, 2, "other", 0, 0, 0), nxt, False
    top3 = h >> 13
    if top3 == 0:
        op = (h >> 11) & 3
        rd, rs = h & 7, (h >> 3) & 7
        if op < 3:
            imm = (h >> 6) & 31
            if op == 0:
                return (a, 2, "lsl", rd, rs, imm) if imm else (a, 2, "copy", rd, rs, 0), nxt, False
            return (a, 2, "w", 1 << rd, 0, 0), nxt, False
        isimm, sub, rn = (h >> 10) & 1, (h >> 9) & 1, (h >> 6) & 7
        if isimm:
            return (a, 2, "addi", rd, rs, -rn if sub else rn), nxt, False
        return (a, 2, "w", 1 << rd, 0, 0), nxt, False
    if top3 == 1:
        op, rd, imm = (h >> 11) & 3, (h >> 8) & 7, h & 0xFF
        if op == 0:
            return (a, 2, "const", rd, imm, 0), nxt, False
        if op == 1:
            return (a, 2, "other", 0, 0, 0), nxt, False
        return (a, 2, "addi", rd, rd, imm if op == 2 else -imm), nxt, False
    if (h >> 10) == 0x10:
        op, rd, rs = (h >> 6) & 15, h & 7, (h >> 3) & 7
        if op == 0xF:
            return (a, 2, "mvn", rd, rs, 0), nxt, False
        if op in (8, 10, 11):
            return (a, 2, "other", 0, 0, 0), nxt, False
        return (a, 2, "w", 1 << rd, 0, 0), nxt, False
    if (h >> 10) == 0x11:
        op = (h >> 8) & 3
        rs = ((h >> 3) & 7) | (((h >> 6) & 1) << 3)
        rd = (h & 7) | (((h >> 7) & 1) << 3)
        if op == 3:
            if (h >> 7) & 1:
                return (a, 2, "callr", rs, 0, 0), nxt, False
            if rs == 14:
                return (a, 2, "ret", 0, 0, 0), [], True
            return (a, 2, "jmpr", rs, 0, 0), [], True
        if op == 2:
            if rd == 15:
                if rs == 14:
                    return (a, 2, "ret", 0, 0, 0), [], True
                return (a, 2, "jmpr", rs, 0, 0), [], True
            return (a, 2, "copy", rd, rs, 0), nxt, False
        if op == 0 and rd == 15:
            return thumb_jump_table(img, base, a), [], True
        if op == 1:
            return (a, 2, "other", 0, 0, 0), nxt, False
        return (a, 2, "w", 1 << rd, 0, 0), nxt, False
    if (h >> 11) == 9:
        rd = (h >> 8) & 7
        return (a, 2, "lit", rd, ((a + 4) & ~3) + (h & 0xFF) * 4, 0), nxt, False
    if (h >> 9) == 0x2C:                          # ldr rd, [rb, ro]
        return (a, 2, "ldro", h & 7, (h >> 3) & 7, (h >> 6) & 7), nxt, False
    if (h >> 11) == 0x0D:                         # ldr rd, [rb, #imm5 * 4]
        return (a, 2, "ldi", h & 7, (h >> 3) & 7, ((h >> 6) & 31) * 4), nxt, False
    if (h >> 12) in (5, 6, 7, 8):
        load = (h >> 11) & 1
        return ((a, 2, "w", 1 << (h & 7), 0, 0) if load else (a, 2, "other", 0, 0, 0)), nxt, False
    if (h >> 12) == 9:
        load = (h >> 11) & 1
        return ((a, 2, "w", 1 << ((h >> 8) & 7), 0, 0) if load else (a, 2, "other", 0, 0, 0)), nxt, False
    if (h >> 12) == 0xA:
        rd = (h >> 8) & 7
        if (h >> 11) & 1:
            return (a, 2, "w", 1 << rd, 0, 0), nxt, False
        return (a, 2, "adr", rd, ((a + 4) & ~3) + (h & 0xFF) * 4, 0), nxt, False
    if (h >> 12) == 0xB:
        if (h & 0xFF00) == 0xBD00:
            return (a, 2, "ret", 0, 0, 0), [], True
        if (h & 0xFE00) == 0xBC00:
            return (a, 2, "w", h & 0xFF, 0, 0), nxt, False
        if (h & 0xFF00) == 0xBE00:
            return (a, 2, "ret", 0, 0, 0), [], True
        return (a, 2, "other", 0, 0, 0), nxt, False
    if (h >> 12) == 0xC:
        if (h >> 11) & 1:
            return (a, 2, "w", h & 0xFF, 0, 0), nxt, False
        return (a, 2, "other", 0, 0, 0), nxt, False
    if (h >> 12) == 0xD:
        cond = (h >> 8) & 0xF
        if cond == 0xF:
            return (a, 2, "w", 0xF, 0, 0), nxt, False
        if cond == 0xE:
            return (a, 2, "ret", 0, 0, 0), [], True
        d = (h & 0xFF) << 1
        if d & 0x100:
            d -= 0x200
        return (a, 2, "bc", a + 4 + d, 0, 0), [a + 2, a + 4 + d], False
    if (h >> 11) == 0x1C:
        d = (h & 0x7FF) << 1
        if d & 0x800:
            d -= 0x1000
        return (a, 2, "b", a + 4 + d, 0, 0), [], True
    return (a, 2, "other", 0, 0, 0), nxt, False


def thumb_jump_table(img, base, a):
    """mwcc's `add pc, rN` switch: halfword table right after, target = a + 4 + e."""
    tab = a + 2
    targets = []
    lo = None
    for i in range(256):
        p = tab + 2 * i
        if lo is not None and p >= lo:
            break
        off = p - base
        if off + 2 > len(img):
            break
        e = struct.unpack_from("<h", img, off)[0]
        t = a + 4 + e
        if t <= a or t - a > 0x2000:
            break
        targets.append(t)
        lo = t if lo is None else min(lo, t)
    return (a, 2, "jt", targets, 0, 0)


def arm_imm(w):
    rot = ((w >> 8) & 0xF) * 2
    v = w & 0xFF
    return ((v >> rot) | (v << (32 - rot))) & 0xFFFFFFFF if rot else v


def arm_decode(img, base, a):
    off = a - base
    w = struct.unpack_from("<I", img, off)[0]
    cond = w >> 28
    nxt = [a + 4]
    if cond == 0xF:
        if (w >> 25) & 7 == 5:          # blx imm
            d = s32((w & 0xFFFFFF) << 8) >> 6
            t = a + 8 + d + (((w >> 24) & 1) << 1)
            return (a, 4, "call", t, "thumb", 0), nxt, False
        return (a, 4, "other", 0, 0, 0), nxt, False
    uncond = cond == 0xE
    if (w & 0x0FFFFFF0) == 0x012FFF10:              # bx
        rm = w & 0xF
        if rm == 14:
            return (a, 4, "ret", 0, 0, 0), ([] if uncond else nxt), uncond
        return (a, 4, "jmpr", rm, 0, 0), ([] if uncond else nxt), uncond
    if (w & 0x0FFFFFF0) == 0x012FFF30:              # blx reg
        return (a, 4, "callr", w & 0xF, 0, 0), nxt, False
    if (w >> 25) & 7 == 5:                          # b / bl
        d = s32((w & 0xFFFFFF) << 8) >> 6
        t = a + 8 + d
        if (w >> 24) & 1:
            return (a, 4, "call", t, "arm", 0), nxt, False
        if uncond:
            return (a, 4, "b", t, 0, 0), [], True
        return (a, 4, "bc", t, 0, 0), [a + 4, t], False
    if (w >> 25) & 7 == 4:                          # ldm/stm
        if (w >> 20) & 1:
            if (w >> 15) & 1:
                return (a, 4, "ret", 0, 0, 0), ([] if uncond else nxt), uncond
            return (a, 4, "w", w & 0xFFFF, 0, 0), nxt, False
        return (a, 4, "other", 0, 0, 0), nxt, False
    if (w >> 26) & 3 == 1:                          # ldr/str
        load = (w >> 20) & 1
        rd = (w >> 12) & 0xF
        rn = (w >> 16) & 0xF
        if load and rd == 15:
            return (a, 4, "jmpr", -1, 0, 0), ([] if uncond else nxt), uncond
        if load and rn == 15 and not (w >> 25) & 1 and (w >> 24) & 1:
            imm = w & 0xFFF
            la = a + 8 + (imm if (w >> 23) & 1 else -imm)
            if uncond:
                return (a, 4, "lit", rd, la, 0), nxt, False
            return (a, 4, "w", 1 << rd, 0, 0), nxt, False
        if load and uncond and (w >> 25) & 1 and (w >> 24) & 1 and not (w >> 22) & 1                 and not (w >> 21) & 1 and (w & 0xFF0) in (0, 0x100):
            return (a, 4, "ldro", rd, rn, w & 0xF), nxt, False   # ldr rd, [rn, rm{, lsl #2}]
        if load and uncond and not (w >> 25) & 1 and (w >> 24) & 1 and not (w >> 22) & 1                 and not (w >> 21) & 1:
            imm = w & 0xFFF
            return (a, 4, "ldi", rd, rn, imm if (w >> 23) & 1 else -imm), nxt, False
        if load:
            return (a, 4, "w", 1 << rd, 0, 0), nxt, False
        return (a, 4, "other", 0, 0, 0), nxt, False
    if (w >> 26) & 3 == 0:                          # data processing (and misc)
        if (w & 0x0E000090) == 0x00000090 and not (w >> 25) & 1:
            # multiplies and halfword transfers
            if (w >> 20) & 1 or (w & 0x0FC000F0) == 0x00000090:
                return (a, 4, "w", 1 << ((w >> 12) & 0xF) | 1 << ((w >> 16) & 0xF), 0, 0), nxt, False
            return (a, 4, "other", 0, 0, 0), nxt, False
        opc = (w >> 21) & 0xF
        rd = (w >> 12) & 0xF
        rn = (w >> 16) & 0xF
        isimm = (w >> 25) & 1
        if 8 <= opc <= 11:
            return (a, 4, "other", 0, 0, 0), nxt, False
        if rd == 15:
            if opc == 4 and rn == 15 and not isimm and not uncond:
                return arm_jump_table(img, base, a), [a + 4], False
            if opc == 0xD and not isimm and (w & 0xFF0) == 0:
                rm = w & 0xF
                if rm == 14:
                    return (a, 4, "ret", 0, 0, 0), ([] if uncond else nxt), uncond
                return (a, 4, "jmpr", rm, 0, 0), ([] if uncond else nxt), uncond
            return (a, 4, "jmpr", -1, 0, 0), ([] if uncond else nxt), uncond
        if not uncond:
            return (a, 4, "w", 1 << rd, 0, 0), nxt, False
        if opc == 0xD:
            if isimm:
                return (a, 4, "const", rd, arm_imm(w), 0), nxt, False
            if (w & 0xFF0) == 0:
                return (a, 4, "copy", rd, w & 0xF, 0), nxt, False
        if opc == 0xF and isimm:
            return (a, 4, "const", rd, (~arm_imm(w)) & 0xFFFFFFFF, 0), nxt, False
        if opc in (4, 2) and isimm:
            v = arm_imm(w)
            if rn == 15:
                return (a, 4, "adr", rd, (a + 8 + (v if opc == 4 else -v)) & 0xFFFFFFFF, 0), nxt, False
            return (a, 4, "addi", rd, rn, v if opc == 4 else -v), nxt, False
        return (a, 4, "w", 1 << rd, 0, 0), nxt, False
    return (a, 4, "other", 0, 0, 0), nxt, False


def arm_jump_table(img, base, a):
    """`addls pc, pc, rN, lsl #2`: a table of `b` instructions at a + 8."""
    targets = []
    p = a + 8
    for _ in range(256):
        off = p - base
        if off + 4 > len(img):
            break
        w = struct.unpack_from("<I", img, off)[0]
        if (w >> 24) != 0xEA:
            break
        targets.append(p + 8 + (s32((w & 0xFFFFFF) << 8) >> 6))
        p += 4
    return (a, 4, "jt", targets, 0, 0)


# --------------------------------------------------------------------------
# regions

class Region:
    """One code region: its labels (from the listing) and its image."""

    def __init__(self, name, listing, image, image_base, lo, hi):
        self.name = name
        self.image = image
        self.base = image_base
        self.lo, self.hi = lo, hi
        self.labels = {}                 # addr -> isa, from the listing
        self._bodies = {}
        isa = "thumb"
        with open(listing, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                s = line.strip()
                if s == ".thumb":
                    isa = "thumb"
                elif s == ".arm":
                    isa = "arm"
                else:
                    m = LABEL.match(line)
                    if m:
                        fm = FUNCNAME.match(m.group(1))
                        if fm:
                            self.labels[int(fm.group(1), 16)] = isa
        self.label_list = sorted(self.labels)
        self.extra_isa = {}

    def contains(self, a):
        return self.lo <= a < self.hi

    def read32(self, a):
        off = a - self.base
        if off < 0 or off + 4 > len(self.image):
            return None
        return struct.unpack_from("<I", self.image, off)[0]

    def isa(self, a):
        return self.labels.get(a) or self.extra_isa.get(a)

    def body(self, f, isa=None):
        """Decoded IR of the function at f, address-sorted."""
        if f in self._bodies:
            return self._bodies[f]
        isa = isa or self.isa(f) or "thumb"
        self.extra_isa.setdefault(f, isa)
        dec = thumb_decode if isa == "thumb" else arm_decode
        seen = {}
        work = [f]
        case = SWITCH_CASE.get(f)
        while work and len(seen) < MAX_INSNS:
            a = work.pop()
            while self.contains(a) and a not in seen and len(seen) < MAX_INSNS:
                if a != f and a in self.labels:
                    break                  # fell or branched into the next function
                try:
                    ir, succ, term = dec(self.image, self.base, a)
                except struct.error:
                    break
                seen[a] = ir
                if ir[2] == "jt":
                    if case is not None and case < len(ir[3]):
                        work.append(ir[3][case])
                        case = None
                    else:
                        work.extend(ir[3])
                if ir[2] == "b":
                    t = ir[3]
                    if t in self.labels and t != f or not self.contains(t):
                        break               # tail call, recorded by the walker
                for s in succ[1:]:
                    work.append(s)
                if term or not succ:
                    break
                a = succ[0]
        out = sorted(seen.values(), key=lambda x: x[0])
        self._bodies[f] = out
        return out

    def size(self, f):
        return sum(i[1] for i in self.body(f))


def dispatch_base(region, body, idx, r, env):
    """When r was loaded as table[i] (`ldr r, [rb, ro]`) and one of rb/ro is a
    constant, that constant: the call through r goes through a table."""
    for k in range(idx - 1, -1, -1):
        a, size, op, x, y, z = body[k]
        if op in ("call", "callr") and r in (0, 1, 2, 3, 12):
            return None
        if op == "w" and x & (1 << r):
            return None
        if x == r and op in ("const", "lit", "adr", "copy", "addi", "mvn", "lsl", "ldi"):
            return None
        if op == "ldro" and x == r:
            for base in (y, z):
                v = track(region, body, k, base, 0, env)
                if v is not None and v >= 0x01000000:
                    return v
            return None
    return None


def track(region, body, idx, r, depth=0, env=None):
    """Constant in register r just before body[idx] (linear backward scan).

    With env (an object with read(addr) and retconst(target, isa)), a load
    from a constant address reads the image, and r0 after a call is the
    callee's return value when that is a constant."""
    if depth > 8 or r is None or r < 0:
        return None
    for k in range(idx - 1, -1, -1):
        a, size, op, x, y, z = body[k]
        if op == "call" and r == 0 and env is not None:
            return env.retconst(x, y, depth + 1)
        if op in ("call", "callr") and r in (0, 1, 2, 3, 12):
            return None
        if op == "ldro" and x == r:
            return None
        if op == "ldi" and x == r:
            if env is None:
                return None
            v = track(region, body, k, y, depth + 1, env)
            return None if v is None else env.read((v + z) & 0xFFFFFFFF)
        if op == "w":
            if x & (1 << r):
                return None
            continue
        if op in ("const", "lit", "adr", "copy", "addi", "mvn", "lsl") and x == r:
            if op == "const":
                return y & 0xFFFFFFFF
            if op == "adr":
                return y
            if op == "lit":
                return region.read32(y)
            if op == "copy":
                return track(region, body, k, y, depth + 1, env)
            v = track(region, body, k, y, depth + 1, env)
            if v is None:
                return None
            if op == "addi":
                return (v + z) & 0xFFFFFFFF
            if op == "mvn":
                return (~v) & 0xFFFFFFFF
            return (v << z) & 0xFFFFFFFF
    return None


# --------------------------------------------------------------------------
# manifest, drafts, names

def shard_status(root, region):
    st = {}
    for shard in sorted(glob.glob(os.path.join(root, "layout", "matched", region, "*.json"))):
        try:
            d = json.load(open(shard, encoding="utf-8"))
        except Exception:
            continue
        for f in d.get("functions", []):
            if not isinstance(f, dict):
                continue
            status = f.get("status", "")
            keys = []
            try:
                keys.append(int(str(f.get("address")), 16))
            except (TypeError, ValueError):
                pass
            if str(f.get("kind", "")).endswith("_unit"):
                for mem in f.get("functions", []):
                    m = FUNCNAME.match(mem) if isinstance(mem, str) else None
                    if m:
                        keys.append(int(m.group(1), 16))
            for k in keys:
                if k not in st or (status.startswith(MATCHED) and not st[k].startswith(MATCHED)):
                    st[k] = status
    return {k for k, v in st.items() if v.startswith(MATCHED)}


def pending_drafts(root):
    out = {}
    for path in glob.glob(os.path.join(root, "src", "pending", "**", "*.c"), recursive=True):
        try:
            head = open(path, encoding="utf-8", errors="replace").read(2000)
        except OSError:
            continue
        ma = re.search(r"address:\s*0x([0-9A-Fa-f]{8})", head)
        mr = re.search(r"region:\s*(\S+)", head)
        ms = re.search(r"status:\s*(\S+)", head)
        if not ma:
            continue
        region = mr.group(1) if mr else "?"
        if region.startswith("overlay_"):
            region = "ov%03d" % int(region[8:])
        rel = os.path.relpath(path, root).replace("\\", "/")
        out[(region, int(ma.group(1), 16))] = (rel, ms.group(1) if ms else "?")
    return out


def file_anchors(root):
    out = {}
    path = os.path.join(root, "layout", "file_anchors.txt")
    if not os.path.exists(path):
        return out
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("#"):
            continue
        p = line.split()
        if len(p) >= 5:
            r = p[2]
            if r.startswith("overlay_"):
                r = "ov%03d" % int(r[8:])
            out.setdefault(r, set()).add(p[4])
    return out


# --------------------------------------------------------------------------
# the walk

class World:
    def __init__(self, root):
        self.root = root
        lay = json.load(open(os.path.join(root, "layout", "layout.json"), encoding="utf-8"))
        self.ovinfo = {o["id"]: o for o in lay["overlays"]}
        a9 = lay["arm9"]
        img = open(os.path.join(root, "extract", a9.get("decompressed_file", "arm9_dec.bin")), "rb").read()
        self.arm9 = Region("arm9", os.path.join(root, "asm", "arm9", "main.s"), img,
                           a9["load_address"], a9["static"]["ram_start"], a9["static"]["ram_end"])
        self.arm9_extra = [(x["ram_start"], x["ram_end"]) for x in a9["autoloads"]]
        tw = lay.get("twl") or {}
        if "ram_start" in tw:
            self.arm9_extra.append((tw["ram_start"], tw["ram_end"]))
        self.ov = {}
        self.matched = {"arm9": shard_status(root, "arm9") | shard_status(root, "arm9_autoload0_ITCM")
                        | shard_status(root, "twl")}
        self.sdk_names = {}
        for fn in ("symbols_sdk.txt", "runtime_arm9.txt"):
            path = os.path.join(root, "layout", fn)
            if os.path.exists(path):
                for line in open(path, encoding="utf-8", errors="replace"):
                    p = line.split()
                    if len(p) >= 2 and not line.startswith("#") and re.match(r"^[0-9A-Fa-f]{8}$", p[0]):
                        self.sdk_names[int(p[0], 16)] = p[1]
        self.drafts = pending_drafts(root)
        self.anchors = file_anchors(root)
        self.bounds = {}
        try:
            self.arm9_summary = json.load(open(os.path.join(root, "layout", "worklist.json"),
                                               encoding="utf-8")).get("summary", {})
        except Exception:
            self.arm9_summary = {}

    def overlay(self, oid):
        if oid not in self.ov:
            info = self.ovinfo[oid]
            path = os.path.join(self.root, "asm", "overlays", "ov%03d.s" % oid)
            img = open(os.path.join(self.root, "extract", info["decompressed_file"]), "rb").read()
            self.ov[oid] = Region("ov%03d" % oid, path, img, info["ram_start"],
                                  info["ram_start"], info["ram_start"] + len(img))
            self.matched["ov%03d" % oid] = shard_status(self.root, "ov%03d" % oid)
        return self.ov[oid]

    def region(self, name):
        return self.arm9 if name == "arm9" else self.overlay(int(name[2:]))

    def in_arm9(self, a):
        return self.arm9.contains(a) or any(lo <= a < hi for lo, hi in self.arm9_extra)

    def overlays_at(self, a):
        return [i for i, o in self.ovinfo.items() if o["ram_start"] <= a < o["ram_end"]]

    def static_inits(self, oid):
        info = self.ovinfo[oid]
        r = self.overlay(oid)
        out = []
        a = info.get("static_init_start")
        end = info.get("static_init_end", a)
        while a is not None and a < end:
            v = r.read32(a)
            if v:
                out.append(v)
            a += 4
        return out

    def data_bounds(self, r):
        if r.name not in self.bounds:
            s = set(r.label_list)
            p = r.lo & ~3
            while p + 4 <= r.hi:
                v = r.read32(p)
                if v is not None and r.contains(v & ~1):
                    s.add(v & ~1)
                p += 4
            self.bounds[r.name] = sorted(s)
        return self.bounds[r.name]


STATIC, CALLBACK, TABLE = 0, 1, 2
TIER_NAME = {STATIC: "static", CALLBACK: "pointer", TABLE: "table"}


class Walk:
    """Closure of one screen.  Nodes are (region name, address).

    Every edge has a tier: STATIC (a call or branch), CALLBACK (a code
    pointer the function itself loads from its literal pool or builds with
    adr) or TABLE (a code pointer inside a data object the function loads
    the address of).  A node's tier is the smallest, over all paths from a
    seed, of the largest edge tier on the path."""

    def __init__(self, world, loaded, leaf_arm9=True):
        self.w = world
        self.loaded = list(loaded)
        self.leaf_arm9 = leaf_arm9
        self.nodes = set()
        self.edges = {}          # src -> {dst: tier}
        self.seeds = set()
        self.open = []           # (node, site, text)
        self.procs = []          # (node, site, api, ovl, data)
        self.loads = []          # (node, site, ovl)
        self.unknown = []        # (node, target, candidates)
        self.queue = []
        self.parent = {}         # node -> the node that first reached it
        self.cur = None
        self.ctx = {}            # node -> overlays loaded when it runs
        self.curctx = tuple(self.loaded)
        self.tier = {}
        self.procdata = set()
        self._ret = {}

    # ---- resolution helpers

    def chain(self, node):
        out = []
        while node is not None and len(out) < 40:
            out.append(node)
            node = self.parent.get(node)
        return out

    def region_of(self, a, prefer=None, ctx=None):
        if prefer is not None and prefer.contains(a):
            return prefer
        if self.w.in_arm9(a):
            return self.w.arm9
        hits = [self.w.overlay(o) for o in reversed(self.curctx if ctx is None else ctx)
                if self.w.overlay(o).contains(a)]
        if not hits:
            return None
        # the most recently loaded overlay wins, unless no resident overlay has a
        # function there and a non-resident one does: then the call belongs to a
        # mode this path does not enter
        if not any(a in r.labels for r in hits) and                 any(a in self.w.overlay(o).labels for o in self.w.overlays_at(a)):
            return None
        return hits[0]

    def read(self, a):
        r = self.region_of(a)
        return r.read32(a) if r is not None else None

    def own_loads(self, r, f):
        """(loaded, unloaded) overlay ids with constant ids in f's own body."""
        body = r.body(f)
        ld, ul = [], []
        for idx, ins in enumerate(body):
            if ins[2] == "call" and ins[3] in (OVL_LOAD, OVL_UNLOAD):
                v = track(r, body, idx, 0)
                if v is not None and v in self.w.ovinfo:
                    (ld if ins[3] == OVL_LOAD else ul).append(v)
        return ld, ul

    def fn_ctx(self, r, f, base):
        """Overlays loaded while f runs: base, minus what f unloads, plus what f
        and its direct callees in the same region load (a callee such as
        func_0200F17C loads an overlay its caller then calls into).  The most
        recently added overlay wins when two share an address."""
        ld, ul = self.own_loads(r, f)
        for ins in r.body(f):
            if ins[2] == "call" and r.contains(ins[3]) and ins[3] not in (OVL_LOAD, OVL_UNLOAD)                     and ins[3] != f and r.name == "arm9":
                ld += self.own_loads(r, ins[3])[0]
        ctx = [o for o in base if o not in ul or o in ld]
        ctx += [o for o in dict.fromkeys(ld) if o not in ctx]
        return tuple(ctx)

    def retconst(self, t, isa, depth=0):
        """The constant a function returns in r0 on every path, else None (a
        function returning either one constant or 0 counts as the constant)."""
        if depth > 6:
            return None
        r = self.region_of(t)
        if r is None:
            return None
        key = (r.name, t, self.curctx)
        if key in self._ret:
            return self._ret[key]
        self._ret[key] = None
        saved = self.curctx
        self.curctx = self.fn_ctx(r, t, saved)
        body = r.body(t, isa)
        vals = set()
        for idx, ins in enumerate(body):
            if ins[2] == "ret":
                vals.add(track(r, body, idx, 0, depth + 1, self))
            elif ins[2] == "jmpr" or (ins[2] == "b" and not (r.contains(ins[3])
                                                             and ins[3] not in r.labels)):
                vals.add(None)
        self.curctx = saved
        # "a table or NULL" counts as the table: callers test for NULL first
        if len(vals) > 1:
            vals.discard(0)
        v = vals.pop() if len(vals) == 1 else None
        self._ret[key] = v
        return v

    def add(self, r, a, tier, isa=None, src=None):
        if isa and r.isa(a) is None:
            r.extra_isa[a] = isa
        key = (r.name, a)
        if src is not None:
            d = self.edges.setdefault(src, {})
            d[key] = min(d.get(key, tier), tier)
        if key not in self.nodes:
            self.nodes.add(key)
            self.parent[key] = src
            self.ctx[key] = self.curctx
            self.queue.append((r, a))

    def code_pointer(self, v, prefer):
        """(region, addr, isa) when v looks like a pointer to code, else None."""
        if v is None or v < 0x01000000:
            return None
        a = v & ~1
        r = self.region_of(a, prefer)
        if r is None:
            return None
        isa = "thumb" if v & 1 else "arm"
        known = r.isa(a)
        if known:
            return (r, a, known) if known == isa else None
        if r.name == "arm9":
            return None
        if isa == "thumb":
            # a Thumb pointer to an unlabelled address: accept it when it decodes as a
            # plausible function start (push, a leaf `bx lr`, a mov/ldr first)
            off = a - r.base
            if off < 0 or off + 2 > len(r.image):
                return None
            h = struct.unpack_from("<H", r.image, off)[0]
            if (h & 0xFE00) == 0xB400 or h == 0x4770 or (h >> 11) in (1, 4, 9):
                return (r, a, "thumb")
        return None

    def pointer_run(self, r, a):
        """The code pointers of a function table at a: consecutive words that
        are all code pointers (a zero ends it too)."""
        out = []
        p = a & ~3
        while p < r.hi and len(out) < 256:
            cp = self.code_pointer(r.read32(p), r)
            if not cp:
                break
            out.append(cp)
            p += 4
        return out

    def data_window(self, r, a):
        bounds = self.w.data_bounds(r)
        i = bisect.bisect_right(bounds, a)
        end = min(bounds[i] if i < len(bounds) else r.hi, a + 0x200, r.hi)
        out = []
        p = (a + 3) & ~3
        while p < end:
            cp = self.code_pointer(r.read32(p), r)
            if cp:
                out.append(cp)
            p += 4
        return out

    # ---- the walk

    def run(self, seeds):
        for r, a, isa in seeds:
            self.curctx = tuple(self.loaded)
            self.seeds.add((r.name, a))
            self.add(r, a, STATIC, isa)
        while self.queue:
            r, a = self.queue.pop()
            if r.name == "arm9" and self.leaf_arm9:
                continue
            self.visit(r, a)
        self.tiers()

    def tiers(self):
        best = {n: STATIC for n in self.seeds}
        for tier in (STATIC, CALLBACK, TABLE):
            work = list(best)
            while work:
                n = work.pop()
                for d, et in self.edges.get(n, {}).items():
                    if et <= tier and d not in best:
                        best[d] = tier
                        work.append(d)
        self.tier = best

    def pointer_ref(self, r, node, v):
        if v in self.procdata:
            return
        cp = self.code_pointer(v, r)
        if cp:
            self.add(cp[0], cp[1], CALLBACK, cp[2], node)
            return
        rr = self.region_of(v, r) if v is not None else None
        if rr is not None and rr.name != "arm9" and rr.contains(v):
            for cr, ca, ci in self.data_window(rr, v):
                self.add(cr, ca, TABLE, ci, node)

    def visit(self, r, f):
        node = (r.name, f)
        self.cur = node
        self.curctx = self.fn_ctx(r, f, self.ctx.get(node, tuple(self.loaded)))
        body = r.body(f)
        addrs = {i[0] for i in body}
        isa = r.isa(f) or "thumb"
        # proc-data objects handed to the proc API are screens or exits, not tables
        procdata = set()
        for idx, ins in enumerate(body):
            if ins[2] == "call" and ins[3] in PROC_API:
                v = track(r, body, idx, PROC_API[ins[3]][2], 0, self)
                if v is not None:
                    procdata.add(v)
        self.procdata = procdata
        for idx, (a, size, op, x, y, z) in enumerate(body):
            if op in ("lit", "adr"):
                v = r.read32(y) if op == "lit" else y
                # a literal consumed by a bx/blx right after is a call, handled there
                nxt = body[idx + 1] if idx + 1 < len(body) else None
                if not (nxt and nxt[2] in ("jmpr", "callr") and nxt[3] == x):
                    self.pointer_ref(r, node, v)
            elif op == "call":
                self.edge(r, node, a, x, y, body, idx)
            elif op == "b":
                if x in addrs:
                    continue
                if (r.contains(x) and x in r.labels and x != f) or not r.contains(x):
                    self.edge(r, node, a, x, isa, body, idx)
            elif op in ("callr", "jmpr"):
                v = track(r, body, idx, x, 0, self)
                cp = self.code_pointer(v, r)
                if cp:
                    self.add(cp[0], cp[1], STATIC, cp[2], node)
                    self.api(r, node, a, cp[1], body, idx)
                elif x >= 0 and dispatch_base(r, body, idx, x, self) is not None:
                    base = dispatch_base(r, body, idx, x, self)
                    rr = self.region_of(base, r)
                    hits = self.pointer_run(rr, base) if rr is not None and rr.contains(base) else []
                    for cr, ca, ci in hits:
                        self.add(cr, ca, CALLBACK, ci, node)
                    if not hits:
                        self.open.append((node, a, "%s r%d through table %s" % (
                            "blx" if op == "callr" else "bx", x, h8(base))))
                elif x >= 0:
                    self.open.append((node, a, "%s r%d" % ("blx" if op == "callr" else "bx", x)))
                else:
                    self.open.append((node, a, "load into pc"))
        # a function the decoder ran into (fall-through into the next label)
        last = body[-1] if body else None
        if last is not None:
            end = last[0] + last[1]
            if end in r.labels and end != f and last[2] not in ("ret", "b", "jmpr", "jt"):
                self.add(r, end, STATIC, None, node)

    def edge(self, r, node, site, t, isa, body, idx):
        tr = self.region_of(t, r)
        if tr is None:
            self.unknown.append((node, t, tuple(self.w.overlays_at(t))))
            return
        self.add(tr, t, STATIC, isa, node)
        self.api(r, node, site, t, body, idx)

    def api(self, r, node, site, t, body, idx):
        if t in PROC_API:
            name, ri, rd = PROC_API[t]
            ovl = track(r, body, idx, ri, 0, self)
            data = track(r, body, idx, rd, 0, self)
            if ovl is None or data is None:
                self.open.append((node, site, "%s(ovl=%s, data=%s)" % (
                    name, "?" if ovl is None else hex(ovl), "?" if data is None else h8(data))))
            self.procs.append((node, site, name, ovl, data))
        elif t == OVL_LOAD:
            v = track(r, body, idx, 0)
            if v is None:
                self.open.append((node, site, "overlay load, id not constant"))
            self.loads.append((node, site, v))


def nitromain_calls(w):
    out = []
    r = w.arm9
    a = NITROMAIN
    while a < NITROMAIN_END:
        ir, succ, term = thumb_decode(r.image, r.base, a)
        if ir[2] == "call":
            out.append((a, ir[3], ir[4]))
        a += ir[1]
    return out


# --------------------------------------------------------------------------
# driver

def ovname(w, o):
    return "ov%03d" % o if o is not None and o in w.ovinfo else "arm9"


def proc_callbacks(w, ovl, data):
    """(region, [init, main, end]) of a proc-data object."""
    if ovl is not None and ovl in w.ovinfo and w.overlay(ovl).contains(data):
        r = w.overlay(ovl)
    else:
        r = w.arm9
    return r, [r.read32(data + 4 * i) for i in range(3)]


def analyse(root):
    w = World(root)
    out = {"world": w}

    # 1. boot: NitroMain, then the ARM9 closure
    nm = nitromain_calls(w)
    out["nitromain"] = nm
    first = Walk(w, [], leaf_arm9=False)
    first.run([(w.arm9, t, isa) for _, t, isa in nm])
    boot = first
    # resident: loaded by a direct NitroMain callee and not unloaded by it
    direct = {("arm9", t) for _, t, _ in nm}
    resident = []
    for node, site, v in sorted(boot.loads, key=lambda x: x[1]):
        if node in direct and v is not None and v in w.ovinfo and v not in resident:
            body = w.arm9.body(node[1])
            unl = [track(w.arm9, body, i, 0) for i, ins in enumerate(body)
                   if ins[2] == "call" and ins[3] == OVL_UNLOAD]
            if v not in unl:
                resident.append(v)
    # second pass: the frame loop and everything after boot set-up run with them resident
    boot = Walk(w, resident, leaf_arm9=False)
    boot.run([(w.arm9, t, isa) for _, t, isa in nm])
    out["boot"] = boot
    out["resident"] = resident
    transient = [v for n, _, v in sorted(boot.loads, key=lambda x: x[1])
                 if v is not None and v in w.ovinfo and boot.tier.get(n, TABLE) <= CALLBACK]
    out["boot_screen"] = {"label": "boot: NitroMain and its ARM9 closure", "ovl": None, "data": None,
                          "ptrs": [], "loaded": list(dict.fromkeys(resident + transient)),
                          "walk": boot, "depth": 0,
                          "children": [], "exits": []}
    roots = sorted([p for p in boot.procs if p[4] is not None], key=lambda p: p[1])
    out["roots"] = []
    screens = []
    expanded = set()

    def run_screen(ovl, data, loaded, label, depth, follow):
        loaded = list(loaded)
        if ovl is not None and ovl in w.ovinfo and ovl not in loaded:
            loaded.append(ovl)
        reg_, ptrs = proc_callbacks(w, ovl, data)
        expanded.add((ovl, data))
        while True:
            seeds = []
            for p in ptrs:
                if p:
                    rr = reg_ if reg_.contains(p & ~1) else w.arm9
                    seeds.append((rr, p & ~1, "thumb" if p & 1 else "arm"))
            for oid in loaded:
                if oid not in resident:
                    for p in w.static_inits(oid):
                        seeds.append((w.overlay(oid), p & ~1, "thumb" if p & 1 else "arm"))
            wk = Walk(w, loaded)
            wk.run(seeds)
            # loads in static or callback code extend what this screen has resident
            extra = [v for n, _, v in wk.loads if v is not None and v in w.ovinfo and v not in loaded
                     and wk.tier.get(n, TABLE) <= CALLBACK]
            if not extra:
                break
            loaded.extend(dict.fromkeys(extra))
        sc = {"label": label, "ovl": ovl, "data": data, "ptrs": ptrs, "loaded": loaded,
              "walk": wk, "depth": depth, "children": [], "exits": []}
        screens.append(sc)
        kids = []
        for node, site, name, povl, pdata in sorted(wk.procs, key=lambda p: (p[0], p[1])):
            if pdata is None or any(k[0] == povl and k[1] == pdata for k in kids):
                continue
            if wk.tier.get(node, TABLE) > CALLBACK and follow:
                continue                    # started from table-only code: not followed
            kids.append((povl, pdata, name, node, site))
        for povl, pdata, name, node, site in kids:
            if not follow:
                sc["exits"].append((povl, pdata, name, node, site))
                continue
            if (povl, pdata) in expanded:
                sc["children"].append((povl, pdata, name, node, site, "already listed"))
                continue
            sc["children"].append((povl, pdata, name, node, site, "followed"))
            run_screen(povl, pdata, loaded, "%s, started by %s at %s" % (
                ovname(w, povl), name, h8(site)), depth + 1, True)

    for node, site, name, ovl, data in roots:
        r, ptrs = proc_callbacks(w, ovl, data)
        # the root proc's callbacks are ARM9: walk them (not as leaves) for their proc starts
        rw = Walk(w, resident, leaf_arm9=False)
        rw.run([(w.arm9, p & ~1, "thumb" if p & 1 else "arm") for p in ptrs if p])
        own = set()
        for p in ptrs:
            if p:
                own.add(("arm9", p & ~1))
        order = []
        for pnode, psite, pname, povl, pdata in sorted(rw.procs, key=lambda p: p[1]):
            if pnode in own and pdata is not None and (povl, pdata) not in order:
                order.append((povl, pdata))
        out["roots"].append({"site": site, "api": name, "ovl": ovl, "data": data, "ptrs": ptrs,
                             "order": order})
        for i, (povl, pdata) in enumerate(order):
            if (povl, pdata) in expanded:
                continue
            last = i == len(order) - 1
            run_screen(povl, pdata, resident, "root step %d: %s proc %s (%s)" % (
                i + 1, ovname(w, povl), h8(pdata), ", ".join(sorted(w.anchors.get(ovname(w, povl), ()))) or
                "no __FILE__ anchor"), 1, not last)
    out["screens"] = screens
    return out


# --------------------------------------------------------------------------
# report

def report(res):
    w = res["world"]
    L = []
    P = L.append
    screens = [res["boot_screen"]] + res["screens"]

    def anchor(region):
        a = sorted(w.anchors.get(region, ()))
        if len(a) > 4:
            return ", ".join(a[:4]) + " and %d more" % (len(a) - 4)
        return ", ".join(a) if a else "no __FILE__ anchor"

    def draft(rn, a):
        return w.drafts.get((rn, a))

    def cls(rn, a, tier):
        return TIER_NAME[tier] + ("" if a in w.region(rn).labels else ", unlabelled")

    def is_open(rn, a):
        return a not in w.matched.get(rn, set())

    # every unmatched overlay function: its best (group, screen, tier)
    best = {}
    for si, sc in enumerate(screens):
        wk = sc["walk"]
        for n in wk.nodes:
            rn, a = n
            if rn == "arm9" or not is_open(rn, a):
                continue
            t = wk.tier.get(n, TABLE)
            g = 0 if t <= CALLBACK else (1 if int(rn[2:]) in sc["loaded"] else 2)
            k = (g, si, t)
            if n not in best or k < best[n]:
                best[n] = k

    P("# From power-on to the title menu: what blocks running it natively")
    P("")
    P("Generated by `tools/title_closure.py` from an aletheia checkout; do not edit by")
    P("hand. Regenerate with")
    P("")
    P("    python tools/title_closure.py --aletheia-root ../bw_decomp --out docs/title-closure.md")
    P("")
    P("Addresses, sizes, overlay ids and source file names only; no game data. The method")
    P("and its limits are in the script's header and under [How to read this](#how-to-read-this).")
    P("")

    # ---- summary
    P("## Summary")
    P("")
    P("| # | screen | overlays loaded | overlay functions reached | unmatched: static / pointer / table | unmatched bytes, static + pointer |")
    P("|---|---|---|---|---|---|")
    for si, sc in enumerate(screens):
        wk = sc["walk"]
        ov = [n for n in wk.nodes if n[0] != "arm9"]
        um = [n for n in ov if is_open(*n)]
        by = [sum(1 for n in um if wk.tier.get(n, TABLE) == t) for t in (STATIC, CALLBACK, TABLE)]
        nb = sum(w.region(n[0]).size(n[1]) for n in um if wk.tier.get(n, TABLE) <= CALLBACK)
        P("| %d | %s | %s | %d | %d / %d / %d | %d |" % (
            si, sc["label"], ", ".join(ovname(w, o) for o in sc["loaded"]) or "-", len(ov),
            by[0], by[1], by[2], nb))
    P("")
    groups = [[n for n, k in best.items() if k[0] == g] for g in (0, 1, 2)]

    def gsum(ns):
        return (len(ns), sum(w.region(n[0]).size(n[1]) for n in ns), sum(1 for n in ns if draft(*n)))

    P("Distinct unmatched overlay functions between power-on and the title menu:")
    P("")
    P("| part | what | functions | instruction bytes | with a draft in aletheia `src/pending` |")
    P("|---|---|---|---|---|")
    P("| A | called, or reached through a code pointer the code itself loads (static and pointer tiers) | %d | %d | %d |" % gsum(groups[0]))
    P("| B | reached only through a data table, in an overlay the screen has loaded | %d | %d | %d |" % gsum(groups[1]))
    P("| C | reached only through a data table, in an overlay the screen never loads from static or pointer code | %d | %d | %d |" % gsum(groups[2]))
    P("")
    cregs = sorted({n[0] for n in groups[2]})
    P("Part A is the floor: those functions run. Part B is what the screens can reach")
    P("through their state and command tables, an over-approximation; in the boot it is")
    P("the rest of the net library's interface table (`func_0215536C` returns it, the boot")
    P("calls entry 0 only). Part C sits in %s, overlays the title menu loads only from" % (
        ", ".join("%s (%s)" % (r, anchor(r)) for r in cregs) or "no overlay"))
    P("table code next to the procs it starts as exits: it is past the title menu and is")
    P("listed for completeness. ARM9 has nothing open")
    P("(`layout/worklist.json`: %s of %s functions matched, %s open) except `NitroMain`, which"
      % (w.arm9_summary.get("functions_matched", "?"), w.arm9_summary.get("functions_total", "?"),
         w.arm9_summary.get("functions_open", "?")))
    P("the port supplies as `stubs/reimpl_02005124.c`. The [priority order](#priority-order) at")
    P("the end is the list to match from.")
    P("")

    # ---- boot
    P("## Boot path")
    P("")
    P("`NitroMain` (`0x02005124`, Thumb) is data words in aletheia's listing: `crt0` reaches")
    P("it through a register, so disasm.py never traced it. Its calls, decoded from the ARM9")
    P("image:")
    P("")
    P("| site | callee | own shard entry |")
    P("|---|---|---|")
    for site, t, isa in res["nitromain"]:
        P("| %s | %s | %s |" % (h8(site), h8(t), "yes" if t in w.matched["arm9"] else
                                "no: unlabelled, inside a matched neighbour's extent"))
    P("")
    P("Overlays are loaded by `sub_02034AC4(id)` (GFL overlay loader) and unloaded by")
    P("`sub_02034A5C(id)`. Procs are started through the GFL proc API, read from aletheia's")
    P("matched ARM9 code:")
    P("")
    P("| API | address | overlay id in | proc data in |")
    P("|---|---|---|---|")
    for t, (name, ri, rd) in sorted(PROC_API.items()):
        P("| %s | %s | r%d | r%d |" % (name, h8(t), ri, rd))
    P("")
    P("A proc-data object is three code pointers, init / main / end. `func_020315F0` loads")
    P("the proc's overlay, runs init, main and end until each returns 1, then unloads the")
    P("overlay. Overlay id `-1` means ARM9 code.")
    P("")
    boot = res["boot"]
    loads = sorted(set((s_, v, n) for n, s_, v in boot.loads))
    P("Constant-id overlay loads in the boot closure:")
    P("")
    P("| site | overlay | in function | tier | what it is |")
    P("|---|---|---|---|---|")
    notes = {
        0x0200523C: "boot set-up (`func_0200523C`, called by NitroMain): stays resident",
        0x02006F20: "save_control.c set-up: loaded, used, unloaded in the same function",
        0x0200F17C: "net mode switch, selector 1 (what `func_0203CB34` passes at boot): "
                    "unloads ov010/ov011, loads the net overlays",
        0x0200F234: "the reverse switch: unloads the net overlays, reloads ov010/ov011",
        0x020315F0: "the proc runner loading a proc's own overlay (the id is a field)",
    }
    for s_, v, n in loads:
        P("| %s | %s | %s %s | %s | %s |" % (h8(s_), "?" if v is None else ovname(w, v), n[0], h8(n[1]),
                                       TIER_NAME[boot.tier.get(n, TABLE)],
                                       notes.get(n[1], "loaded by net code reached above")))
    P("")
    P("So ov010 and ov011 are resident from boot on. During `func_0200523C` the net layer")
    P("is initialised: `func_0203CB34` (net.c) calls `func_0200F17C(1)`, which swaps")
    P("ov010/ov011 out for ov009 and ov013 and returns ov009's function table; the boot")
    P("calls its first entry, then `func_0200F234(1)` swaps back. That is why ov009, ov013")
    P("and ov017 appear in the boot closure. ov228 is loaded and unloaded by the save-data")
    P("set-up. The selector of both switches is pinned to 1 in the script (`SWITCH_CASE`):")
    P("their other selectors swap in other communication modes, which only a menu starts.")
    P("")
    a9 = sorted(a for (rn, a) in boot.nodes if rn == "arm9" and boot.tier.get((rn, a)) == STATIC
                and a not in w.matched["arm9"])
    lib = [a for a in a9 if a in w.sdk_names]
    game = [a for a in a9 if a not in w.sdk_names]
    P("ARM9 functions on the static boot path with no shard entry of their own: %d SDK or" % len(lib))
    P("runtime symbols (%s), which the port provides from `sdk/` or links as matched SDK" %
      ", ".join("`%s`" % w.sdk_names[a] for a in lib))
    P("code, and %d game addresses (%s). The game ones are covered inside a matched" % (
        len(game), ", ".join(h8(a) for a in game) or "none"))
    P("neighbour's extent or a unit, so aletheia counts them, but no matched file defines a")
    P("symbol with their own name; `tools/closure.py` reports them as `open` when a seed")
    P("reaches them.")
    P("")
    for rp in res["roots"]:
        P("The boot starts one proc with a constant argument: `%s(-1, %s)` at %s, the **root" % (
            rp["api"], h8(rp["data"]), h8(rp["site"])))
        P("proc**, ARM9 code: init %s, main %s, end %s. Its main (`func_0202F9F8`) is a" %
          tuple(h8(p & ~1) for p in rp["ptrs"]))
        P("five-state machine that starts, in order:")
        P("")
        for i, (o, d) in enumerate(rp["order"]):
            P("%d. %s proc data %s (%s)" % (i + 1, ovname(w, o), h8(d), anchor(ovname(w, o))))
        P("")
    P("State 0 starts the first proc. States 1, 2 and 3 start the cdemo proc with a mode")
    P("of 0, 1 and 2 in its shared work area (`g_02146948`, mode at +0, skip byte at +3).")
    P("The cdemo main (`func_021B6380`) sets the skip byte when A, B or Start is pressed")
    P("(key mask 0xB). State 2 tests it after mode 0: set, the machine goes to state 4;")
    P("clear, modes 1 and 2 run (state 3 does not test it). State 4 starts the second")
    P("ov088 proc and the machine returns to state 0 when that proc ends. By the source")
    P("file names: a save-data check (`save_control_intr.c`, ov088), the opening cinematic")
    P("as up to three cdemo segments (`cdemo_main.c`, ov179), then the title menu")
    P("(`startmenu.c`, ov088). The procs the title menu starts are the exits listed under")
    P("its screen; they lead past the title menu and are not followed.")
    P("")

    # ---- reading
    P("## How to read this")
    P("")
    P("A screen is a proc: the closure of its init, main and end, plus the static")
    P("initialisers of overlays it loads. The script decodes the code itself (Thumb and")
    P("ARM, including mwcc's switch jump tables, which aletheia's listings leave as data")
    P("words), so it sees calls the listings do not. A function is reached as")
    P("")
    P("- **static**: every edge on its best path is a direct BL/BLX/B, or a call or tail call")
    P("  through a register the backward scan resolves to a constant (this includes a load")
    P("  through a constant pointer and a callee that always returns the same pointer, as")
    P("  in the boot's net-table call);")
    P("- **pointer**: the best path needs a code pointer that a function loads from its own")
    P("  literal pool or builds with `adr` (a callback it registers, a task, a sequence")
    P("  function), or an entry of a constant function table that the function calls")
    P("  through with an index (`ldr rN, [table, idx]; blx rN`, the shape of cdemo's state")
    P("  machine; the table is the run of consecutive code pointers at its address).")
    P("  Resolved, and nearly always run, but through a pointer;")
    P("- **table**: the best path needs a code pointer found in a data object whose address")
    P("  a function loads (state tables, vtables, command tables). The object is bounded by")
    P("  the next address anything in the region points at, at most 0x200 bytes, so this")
    P("  tier over-approximates: it is the right list for \"what could this screen run\",")
    P("  not for \"what does it run\";")
    P("- **unlabelled**: reached by pointer but with no label in aletheia's listing (code")
    P("  disasm.py never traced, so it has no manifest entry by construction).")
    P("")
    P("**Open** sites are register calls the scan cannot resolve (struct fields, vtables,")
    P("callbacks handed in by a caller). What they reach is not listed, so the totals are a")
    P("floor for static and pointer code. ARM9 callees are leaves of a screen (ARM9 is")
    P("fully matched); the boot walk does descend through ARM9.")
    P("")

    # ---- screens
    P("## Screens")
    P("")
    for si, sc in enumerate(screens):
        wk = sc["walk"]
        P("### %d. %s" % (si, sc["label"]))
        P("")
        if sc["ptrs"]:
            P("Proc data %s; init %s, main %s, end %s." % (
                h8(sc["data"]), *[h8(p & ~1) if p else "0" for p in sc["ptrs"]]))
            P("")
        P("Overlays loaded: %s." % (", ".join("%s (%s)" % (ovname(w, o), anchor(ovname(w, o)))
                                                for o in sc["loaded"]) or "none"))
        P("")
        byreg = {}
        for n in wk.nodes:
            byreg.setdefault(n[0], []).append(n)
        P("| region | reached: static / pointer / table | matched | unmatched: static / pointer / table | overlay functions | overlay matched |")
        P("|---|---|---|---|---|---|")
        for rn in sorted(byreg, key=lambda x: (x != "arm9", x)):
            items = byreg[rn]
            rt = [sum(1 for n in items if wk.tier.get(n, TABLE) == t) for t in (STATIC, CALLBACK, TABLE)]
            if rn == "arm9":
                P("| arm9 | %d / %d / %d | all | - | - | - |" % tuple(rt))
                continue
            r = w.region(rn)
            um = [n for n in items if is_open(*n)]
            ut = [sum(1 for n in um if wk.tier.get(n, TABLE) == t) for t in (STATIC, CALLBACK, TABLE)]
            ms = w.matched.get(rn, set())
            P("| %s | %d / %d / %d | %d | %d / %d / %d | %d | %d |" % (
                rn, rt[0], rt[1], rt[2], len(items) - len(um), ut[0], ut[1], ut[2],
                len(r.labels), sum(1 for a in r.labels if a in ms)))
        P("")
        opens = sorted(set(o for o in wk.open if o[0][0] != "arm9"))
        strong = [o for o in opens if wk.tier.get(o[0], TABLE) <= CALLBACK]
        if opens:
            P("Open sites in overlay code: %d in static or pointer functions, %d in table-only" % (
                len(strong), len(opens) - len(strong)))
            P("functions.%s" % (" The first group:" if strong else ""))
            P("")
            for node, site, text in strong[:40]:
                P("- %s `%s` at %s: `%s`" % (node[0], h8(node[1]), h8(site), text))
            if len(strong) > 40:
                P("- and %d more" % (len(strong) - 40))
            if strong:
                P("")
        a9open = len(set(o for o in wk.open if o[0][0] == "arm9"))
        if a9open:
            P("Open sites in ARM9 code walked by the boot: %d (ARM9 is matched; they matter only" % a9open)
            P("for overlay code they might reach, which is not listed).")
            P("")
        unk = sorted(set(wk.unknown))
        if unk:
            P("Calls to an address no overlay resident at that point holds (%d):" % len(unk))
            P("")
            for node, t, cands in unk[:20]:
                P("- %s `%s` (%s) calls %s; overlays at that address: %s" % (
                    node[0], h8(node[1]), TIER_NAME[wk.tier.get(node, TABLE)], h8(t),
                    ", ".join(ovname(w, c) for c in cands) or "none"))
            P("")
        lds = sorted(set((s_, v, wk.tier.get(n, TABLE)) for n, s_, v in wk.loads))
        if lds and si > 0:
            P("Overlay loads in the closure: %s." % ", ".join(
                "%s at %s (%s)" % ("?" if v is None else ovname(w, v), h8(s_), TIER_NAME[t])
                for s_, v, t in lds))
            P("")
        if sc["children"]:
            P("Procs it starts: %s." % "; ".join(
                "%s %s (%s at %s, %s)" % (ovname(w, o), h8(d), n_, h8(s_), st)
                for o, d, n_, node, s_, st in sc["children"]))
            P("")
        if sc["exits"]:
            P("Exits, not followed (past the title menu):")
            P("")
            for o, d, n_, node, s_ in sc["exits"]:
                tgt = "-1" if o == 0xFFFFFFFF else ("?" if o is None else ovname(w, o))
                P("- %s(%s, %s) at %s in %s `%s` (%s)%s" % (
                    n_, tgt, h8(d), h8(s_), node[0], h8(node[1]), TIER_NAME[wk.tier.get(node, TABLE)],
                    ": %s" % anchor(tgt) if tgt.startswith("ov") else ""))
            P("")

    # ---- overlays on the path
    P("## Overlays on the path")
    P("")
    P("Functions, matched and unmatched count the labels of aletheia's listing; the")
    P("on-path columns also count unlabelled functions the walk found, so they can exceed")
    P("the unmatched column.")
    P("")
    P("| overlay | source files | loaded by | functions | matched | unmatched | unmatched on the path: A / B / C |")
    P("|---|---|---|---|---|---|---|")
    seen_ov = []
    for si, sc in enumerate(screens):
        for o in sc["loaded"]:
            if o not in [x[0] for x in seen_ov]:
                seen_ov.append((o, si))
    for si, sc in enumerate(screens):
        for n in sc["walk"].nodes:
            if n[0] != "arm9" and int(n[0][2:]) not in [x[0] for x in seen_ov]:
                seen_ov.append((int(n[0][2:]), si))
    for o, si in sorted(seen_ov):
        rn = ovname(w, o)
        r = w.region(rn)
        ms = w.matched.get(rn, set())
        mt = sum(1 for a in r.labels if a in ms)
        on = [k for n, k in best.items() if n[0] == rn]
        how = ("boot, resident" if o in res["resident"] else
               "screen %d" % si if o in screens[si]["loaded"] else "table code in screen %d" % si)
        P("| %s | %s | %s | %d | %d | %d | %d / %d / %d |" % (
            rn, anchor(rn), how, len(r.labels), mt, len(r.labels) - mt,
            sum(1 for k in on if k[0] == 0), sum(1 for k in on if k[0] == 1),
            sum(1 for k in on if k[0] == 2)))
    P("")

    # ---- per overlay
    P("## Unmatched functions by overlay")
    P("")
    P("Overlay totals count the function labels of aletheia's listing (the `ovstats.py`")
    P("denominator). \"Screen\" is the first screen whose closure reaches the function at")
    P("its best tier.")
    P("")
    regions = {}
    for n, k in best.items():
        regions.setdefault(n[0], []).append((n[1], k))
    for rn in sorted(regions):
        r = w.region(rn)
        ms = w.matched.get(rn, set())
        tot = len(r.labels)
        mt = sum(1 for a in r.labels if a in ms)
        P("### %s (%s)" % (rn, anchor(rn)))
        P("")
        on = regions[rn]
        P("Overlay: %d functions, %d matched, %d unmatched. Unmatched on the path: %d"
          " (part A %d, part B %d, part C %d)." % (
              tot, mt, tot - mt, len(on), sum(1 for _, k in on if k[0] == 0),
              sum(1 for _, k in on if k[0] == 1), sum(1 for _, k in on if k[0] == 2)))
        P("")
        P("| address | bytes | tier | screen | draft in aletheia src/pending |")
        P("|---|---|---|---|---|")
        for a, k in sorted(on):
            d = draft(rn, a)
            P("| %s | %d | %s | %d | %s |" % (h8(a), r.size(a), cls(rn, a, k[2]), k[1],
                                            "`%s` (%s)" % d if d else "no"))
        P("")

    # ---- priority
    P("## Priority order")
    P("")
    P("Match top to bottom. Part A is what the screens call or reach through pointers")
    P("their own code loads, in screen order (0 boot, 1 save check, 2 opening cinematic,")
    P("3 title menu); inside a screen static before pointer, then by address. Finishing a")
    P("screen's block makes that screen's known code native. Part B is table-only code in")
    P("overlays the screen has loaded, again in screen order: what the screens could reach")
    P("through their data tables; it shrinks as part A's sources show which entries are")
    P("used. Part C is table-only code in overlays the screen only loads from table code,")
    P("which is past the title menu.")
    P("")
    order = sorted(best.items(), key=lambda x: (x[1], x[0][0], x[0][1]))
    for part, title in ((0, "A: static and pointer"), (1, "B: table only, loaded overlays"),
                        (2, "C: table only, overlays loaded only from table code")):
        rows = [(n, k) for n, k in order if k[0] == part]
        P("### Part %s (%d functions, %d bytes)" % (
            title, len(rows), sum(w.region(n[0]).size(n[1]) for n, _ in rows)))
        P("")
        P("| # | screen | overlay | address | bytes | tier | draft |")
        P("|---|---|---|---|---|---|---|")
        for i, (n, k) in enumerate(rows):
            d = draft(*n)
            P("| %d | %d | %s | %s | %d | %s | %s |" % (
                i + 1, k[1], n[0], h8(n[1]), w.region(n[0]).size(n[1]), cls(n[0], n[1], k[2]),
                "`%s`" % d[0] if d else "no"))
        P("")
    return "\n".join(L)


def main(argv=None):
    ap = argparse.ArgumentParser(description="power-on to title menu closure over aletheia")
    ap.add_argument("--aletheia-root", default=os.path.normpath(os.path.join(EIDOS_ROOT, "..", "aletheia")))
    ap.add_argument("--out", default=None, help="write the markdown here (default: stdout)")
    args = ap.parse_args(argv)
    root = os.path.abspath(args.aletheia_root)
    for need in ("layout/layout.json", "asm/arm9/main.s", "extract/arm9_dec.bin"):
        if not os.path.exists(os.path.join(root, need)):
            sys.stderr.write("title_closure: %s not found under %s (pass --aletheia-root; the\n"
                             "listings and the extracted images are generated, never committed)\n"
                             % (need, root))
            return 2
    res = analyse(root)
    md = report(res)
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(md)
    else:
        sys.stdout.write(md)
    return 0


if __name__ == "__main__":
    sys.exit(main())
