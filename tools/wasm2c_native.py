#!/usr/bin/env python3
"""Compile a translated game.c plus the native macOS/POSIX host.

Use wasm2c_link.py first. Generated C and WABT runtime sources are compiled
separately from host sources; handwritten host code must pass -Werror.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess


def run(command):
    subprocess.run([str(x) for x in command], check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--module-dir", required=True, type=Path)
    ap.add_argument("--wabt", required=True, type=Path)
    ap.add_argument("--cc", default="cc")
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--debug", action="store_true")
    args = ap.parse_args()
    module, wabt = args.module_dir.resolve(), args.wabt.resolve()
    host = Path(__file__).resolve().parent.parent / "engine/wasm2c"
    runtime = wabt / "share/wabt/wasm2c"
    # WABT leaves WASM_RT_THREAD_LOCAL empty on Apple without C11. C99
    # therefore silently shares trap state and alternate stacks across
    # workers; every native translation unit must use C11 or newer.
    common = [args.cc, "-O2", "-std=c11", "-pthread",
              "-I" + str(module), "-I" + str(wabt / "include"),
              "-I" + str(runtime), "-I" + str(host)]
    if args.debug: common.append("-g")
    sources = [module / "game.c", *[runtime / name for name in
        ("wasm-rt-impl.c", "wasm-rt-mem-impl.c", "wasm-rt-exceptions-impl.c")],
        *sorted(host.glob("*.c"))]
    jobs, objects = [], []
    for i, source in enumerate(sources):
        obj = module / ("native-%02d.o" % i)
        objects.append(obj)
        if obj.exists() and obj.stat().st_mtime >= source.stat().st_mtime:
            continue
        warnings = ["-Wall", "-Wextra", "-Werror"] if source.parent == host else []
        jobs.append(common + warnings + ["-c", source, "-o", obj])
    if jobs:
        with ThreadPoolExecutor(args.jobs) as pool:
            list(pool.map(run, jobs))
    run([args.cc, "-pthread", *objects, "-lm", "-o", args.output.resolve()])
    print("native executable:", args.output.resolve())


if __name__ == "__main__":
    main()
