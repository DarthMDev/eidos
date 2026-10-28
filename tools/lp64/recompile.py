#!/usr/bin/env python3
"""
tools/lp64/recompile.py -- recompile an ILP32 build's translation units with
clang for wasm32 (and, as a control, for native i686), and report which ones
fail and why.

Why this exists: docs/lp64.md evaluates running the game's matched C inside a
wasm32 linear memory (compiled to wasm, then translated back to portable C by
wasm2c).  wasm32 is an ILP32 target, so the matched corpus should compile for
it exactly as it does for i686.  This script measures that claim over a real
build: it reads the compile_commands.json of an ILP32 configuration (configure
with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON), replaces the GCC driver by clang
with the wasm32 target and a WASI sysroot, and compiles every entry into an
object directory of its own.  The same entries are compiled a second time for
i686 with the same clang, so a failure can be attributed: "clang rejects this
file" (both fail) or "wasm32 rejects this file" (only wasm32 fails).

It writes only into --out.  It never modifies the build directory, the eidos
tree or aletheia.

    python tools/lp64/recompile.py --build <ilp32-build> --out <dir> \
        --clang <path-to-clang> --sysroot <wasi-sysroot> [--filter matched]
"""

import argparse
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys

DROP_FLAGS = {"-pthread", "-mno-align-double", "-malign-double", "-msse2", "-mfpmath=sse",
              "-mstackrealign", "-m32", "-mno-ms-bitfields", "-mms-bitfields"}


def split_command(entry):
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"].replace("\\", "/"), posix=True)


def translate(args, clang, target, extra, obj):
    """The GCC command line as a clang one for `target`, writing `obj`."""
    out = [clang, "--target=" + target]
    out += extra
    i = 1
    while i < len(args):
        a = args[i]
        if a == "-o":
            i += 2
            continue
        if a in DROP_FLAGS:
            i += 1
            continue
        out.append(a)
        i += 1
    out += ["-o", obj]
    return out


def first_error(text):
    for line in text.splitlines():
        if ": error:" in line or "fatal error:" in line:
            return re.sub(r"^.*?: (fatal )?error: ", "", line).strip()
    return text.strip().splitlines()[0] if text.strip() else ""


def run(job):
    cmd, cwd = job
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--clang", required=True)
    ap.add_argument("--sysroot", required=True)
    ap.add_argument("--target", default="wasm32-wasip1")
    ap.add_argument("--control-target", default="i686-w64-windows-gnu")
    ap.add_argument("--filter", default="", help="substring a source path must contain")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--json", help="write the per-file result here")
    ap.add_argument("--wasm-flag", action="append", default=[],
                    help="extra flag for the wasm32 compile (repeatable), e.g. -flto")
    ap.add_argument("--no-control", action="store_true",
                    help="skip the i686 control compile")
    a = ap.parse_args()

    entries = json.load(open(os.path.join(a.build, "compile_commands.json")))
    entries = [e for e in entries if a.filter in e["file"].replace("\\", "/")]
    entries = [e for e in entries if "/tests/" not in e["file"].replace("\\", "/")]
    seen = set()
    uniq = []
    for e in entries:
        key = e["output"] if "output" in e else e["file"]
        if key in seen:
            continue
        seen.add(key)
        uniq.append(e)

    os.makedirs(a.out, exist_ok=True)
    wasm_extra = ["--sysroot=" + a.sysroot, "-O2", "-c", "-Wno-unused-command-line-argument"] + a.wasm_flag
    ctl_extra = ["-O2", "-c", "-Wno-unused-command-line-argument"]
    jobs = []
    meta = []
    for n, e in enumerate(uniq):
        args = split_command(e)
        base = os.path.basename(e["file"]).replace(".c", "")
        gcc_obj = os.path.join(a.build, e.get("output", "")) if "output" in e else None
        gcc_ok = bool(gcc_obj and os.path.exists(gcc_obj))
        wobj = os.path.join(a.out, "wasm", "%04d_%s.o" % (n, base))
        cobj = os.path.join(a.out, "ctl", "%04d_%s.o" % (n, base))
        os.makedirs(os.path.dirname(wobj), exist_ok=True)
        os.makedirs(os.path.dirname(cobj), exist_ok=True)
        jobs.append((translate(args, a.clang, a.target, wasm_extra, wobj), e["directory"]))
        if a.no_control:
            jobs.append((["python", "-c", "pass"], e["directory"]))
        else:
            jobs.append((translate(args, a.clang, a.control_target, ctl_extra, cobj), e["directory"]))
        meta.append({"file": e["file"].replace("\\", "/"), "gcc_i686": gcc_ok,
                     "wasm_obj": wobj})

    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        results = list(ex.map(run, jobs))

    rows = []
    for k, m in enumerate(meta):
        wrc, wout = results[2 * k]
        crc, cout = results[2 * k + 1]
        m["wasm32"] = wrc == 0
        m["clang_i686"] = crc == 0
        m["wasm32_error"] = "" if wrc == 0 else first_error(wout)
        m["clang_i686_error"] = "" if crc == 0 else first_error(cout)
        m["wasm32_warnings"] = wout.count("warning:")
        rows.append(m)

    def count(pred):
        return sum(1 for r in rows if pred(r))

    print("translation units:            %d" % len(rows))
    print("GCC i686 objects present:     %d" % count(lambda r: r["gcc_i686"]))
    print("clang i686 compiles:          %d" % count(lambda r: r["clang_i686"]))
    print("clang wasm32 compiles:        %d" % count(lambda r: r["wasm32"]))
    print("wasm32 only fails (control ok): %d" % count(lambda r: r["clang_i686"] and not r["wasm32"]))
    print("both clang targets fail:      %d" % count(lambda r: not r["clang_i686"] and not r["wasm32"]))
    print("clang ok where GCC i686 failed: %d" % count(lambda r: r["wasm32"] and not r["gcc_i686"]))
    reasons = {}
    for r in rows:
        if r["clang_i686"] and not r["wasm32"]:
            reasons.setdefault(r["wasm32_error"][:110], []).append(os.path.basename(r["file"]))
    if reasons:
        print("\nwasm32-only failures, by first error:")
        for why, files in sorted(reasons.items(), key=lambda kv: -len(kv[1])):
            print("  %3d  %s   (%s)" % (len(files), why, ", ".join(files[:4])))
    both = {}
    for r in rows:
        if not r["clang_i686"] and not r["wasm32"]:
            both.setdefault(r["clang_i686_error"][:110], []).append(os.path.basename(r["file"]))
    if both:
        print("\nfailures shared by both clang targets, by first error:")
        for why, files in sorted(both.items(), key=lambda kv: -len(kv[1]))[:25]:
            print("  %3d  %s   (%s)" % (len(files), why, ", ".join(files[:4])))
    if a.json:
        with open(a.json, "w") as f:
            json.dump(rows, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
