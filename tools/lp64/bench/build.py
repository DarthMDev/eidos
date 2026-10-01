#!/usr/bin/env python3
"""
tools/lp64/bench/build.py -- build tools/lp64/bench/bench.c three ways and run
each: natively for i686 (the ILP32 reference), and through wasm32 + wasm2c
for x86-64 with MinGW GCC and with MSVC.

Why this exists: docs/lp64.md needs the cost of the wasm32 route measured on
the game's own code, not estimated.  Every configuration compiles the same
sources (the eidos library's, read from an ILP32 build's
compile_commands.json, plus the bench and MTX_Concat43.c) at -O2 / /O2.

    python tools/lp64/bench/build.py --build <ilp32-build> --out <dir> \
        --aletheia-root <aletheia> --clang <clang> --wasm-ld <wasm-ld> \
        --llvm-ar <llvm-ar> --sysroot <wasi-sysroot> \
        --builtins <wasm32-wasip1 libclang_rt.builtins.a> --wabt <wabt-dir> \
        --gcc32 <i686-gcc> --gcc64 <x86_64-gcc> [--msvc-env <json>]

--msvc-env is a JSON object of the Visual Studio developer environment
(what tools/check.py's vs_env returns); without it the MSVC leg is skipped.
--wasm-opt (binaryen) adds two more modules: wasm-opt -O2, and -O2 with
--fpcast-emu (every indirect call through one signature).  The wasm leg
runs tools/lp64/sigfix.py until wasm-ld reports no signature mismatch, then
checks the module with wasm-validate.  Each executable runs --runs times
(median) and the table is relative to the i686 reference; a checksum that
differs from it is flagged.  Writes only into --out.
"""

import argparse
import concurrent.futures
import json
import os
import re
import shlex
import shutil
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HOST = os.path.join(os.path.dirname(HERE), "w2c_host")
sys.path.insert(0, os.path.dirname(HERE))
import sigfix  # noqa: E402  (tools/lp64/sigfix.py)


def sh(cmd, env=None, cwd=None):
    p = subprocess.run(cmd, capture_output=True, text=True, env=env, cwd=cwd)
    if p.returncode != 0:
        sys.stderr.write(" ".join(cmd)[:600] + "\n" + p.stdout + p.stderr)
        raise SystemExit(1)
    return p.stdout + p.stderr


def lib_entries(build):
    out = []
    for e in json.load(open(os.path.join(build, "compile_commands.json"))):
        o = e["output"].replace("\\", "/")
        if o.startswith("CMakeFiles/eidos.dir/"):
            out.append(e)
    return out


def flags(entry):
    args = shlex.split(entry["command"].replace("\\", "/"), posix=True)[1:]
    res = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == "-o":
            i += 2
            continue
        if a in ("-c", "-pthread", "-msse2", "-mfpmath=sse"):
            i += 1
            continue
        res.append(a)
        i += 1
    return [x for x in res if x != entry["file"].replace("\\", "/")]


def main():
    ap = argparse.ArgumentParser()
    for k in ("build", "out", "aletheia-root", "clang", "wasm-ld", "llvm-ar",
              "sysroot", "builtins", "wabt", "gcc32", "gcc64"):
        ap.add_argument("--" + k, required=True)
    ap.add_argument("--msvc-env")
    ap.add_argument("--scale", default="1")
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--wasm-opt", help="binaryen wasm-opt; adds the -O2 and --fpcast-emu modules")
    a = ap.parse_args()

    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    entries = lib_entries(a.build)
    common = flags(entries[0])
    incs = [x for x in common if x.startswith("-I")]
    defs = [x for x in common if x.startswith("-D")]
    extra = [
        (os.path.join(HERE, "bench.c"), incs + defs + ["-std=c99"]),
        (os.path.join(a.aletheia_root, "src", "matched", "arm9_sdk", "MTX_Concat43.c"),
         ["-std=c99", "-fsigned-char"]),
    ]
    units = [(e["file"].replace("\\", "/"), flags(e)) for e in entries] + extra

    def obj_path(tag, n, src):
        return os.path.join(out, tag, "%03d_%s.o" % (n, os.path.basename(src)[:-2]))

    def compile_all(tag, cc_prefix, suffix, defines=None, only=None):
        d = os.path.join(out, tag)
        os.makedirs(d, exist_ok=True)
        jobs = []
        for n, (src, fl) in enumerate(units):
            obj = obj_path(tag, n, src)
            if only is not None and obj not in only:
                continue
            dfl = ["-D%s=%s" % kv for kv in sorted((defines or {}).get(obj, {}).items())]
            jobs.append((cc_prefix + ["-O2", "-w", "-c"] + fl + dfl + [src, "-o", obj], obj, src))
        with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as ex:
            res = list(ex.map(lambda j: (subprocess.run(j[0], capture_output=True, text=True), j), jobs))
        objs = []
        for p, j in res:
            if p.returncode == 0:
                objs.append(j[1])
            else:
                print("  [%s] skipped %s: %s" % (tag, os.path.basename(j[2]),
                                                  (p.stderr.strip().splitlines() or ["?"])[-1][:120]))
        return objs

    # 1. native i686, the reference
    print("native i686 (GCC -O2)")
    objs = compile_all("i686", [a.gcc32], ".o")
    lib = os.path.join(out, "i686", "libbench.a")
    bench_o = [o for o in objs if "_bench.o" in o]
    rest = [o for o in objs if o not in bench_o]
    ar32 = os.path.join(os.path.dirname(a.gcc32),
                        os.path.basename(a.gcc32).replace("gcc", "ar"))
    if os.path.exists(lib):
        os.remove(lib)
    sh([ar32, "rcs", lib] + rest)
    exe32 = os.path.join(out, "bench_i686.exe")
    sh([a.gcc32, "-O2"] + bench_o + [lib, "-o", exe32, "-lm"])

    # 2. wasm32
    print("wasm32 (clang -O2)")
    wcc = [a.clang, "--target=wasm32-wasip1", "--sysroot=" + a.sysroot]
    objs = compile_all("wasm", wcc, ".o")
    wlib = os.path.join(out, "wasm", "libbench.a")
    wasm = os.path.join(out, "bench.wasm")
    sysl = os.path.join(a.sysroot, "lib", "wasm32-wasip1")
    adapters_c = os.path.join(out, "wasm", "sigfix_adapters.c")
    adapters_o = os.path.join(out, "wasm", "sigfix_adapters.o")
    defines = {}
    adapters = {}
    nm = os.path.join(os.path.dirname(a.llvm_ar), "llvm-nm" + os.path.splitext(a.llvm_ar)[1])

    def objmap_of(log):
        m = {}
        for name in re.findall(r" in (\S+)$", log, re.M):
            k = re.match(r".*\((\d{3}_[^)]*\.o)\)$", name)
            m[name] = os.path.join(out, "wasm", k.group(1)) if k else name
        return m

    for attempt in range(8):
        bench_o = [o for o in objs if "_bench.o" in o]
        rest = [o for o in objs if o not in bench_o]
        if adapters:
            with open(adapters_c, "w") as f:
                f.write("/* generated by tools/lp64/sigfix.py */\n\n")
                f.write("\n".join(adapters[k] for k in sorted(adapters)))
            sh(wcc + ["-O2", "-c", adapters_c, "-o", adapters_o])
            rest = rest + [adapters_o]
        # Objects, not an archive: wasm-ld 20 skips the signature check when
        # a call is resolved by fetching an archive member, and writes an
        # invalid module instead of a trap stub (docs/lp64.md).
        log = sh([a.wasm_ld, "-O2", "--gc-sections", os.path.join(sysl, "crt1-command.o")] +
                 bench_o + rest +
                 ["-L" + sysl, "-lc", a.builtins, "-o", wasm, "-z", "stack-size=1048576"])
        new_defs, new_adapters = sigfix.plan(log, nm, objmap_of(log))
        if not new_defs:
            if log.strip():
                print(log)
            break
        for obj, d in new_defs.items():
            defines.setdefault(obj, {}).update(d)
        adapters.update(new_adapters)
        print("  sigfix pass %d: %d adapters, %d callers recompiled"
              % (attempt + 1, len(new_adapters), len(new_defs)))
        compile_all("wasm", wcc, ".o", defines, set(new_defs))
    print("  bench.wasm: %d bytes, %d signature adapters" % (os.path.getsize(wasm), len(adapters)))
    sh([os.path.join(a.wabt, "bin", "wasm-validate"), wasm])

    # 3. wasm2c, optionally after binaryen's wasm-opt
    modules = [("plain", wasm)]
    if a.wasm_opt:
        opt = os.path.join(out, "bench_opt.wasm")
        sh([a.wasm_opt, wasm, "-O2", "-o", opt])
        modules.append(("wasm-opt", opt))
        fpc = os.path.join(out, "bench_fpcast.wasm")
        sh([a.wasm_opt, wasm, "--fpcast-emu", "-O2", "-o", fpc])
        modules.append(("wasm-opt+fpcast", fpc))
    rtdir = os.path.join(a.wabt, "share", "wabt", "wasm2c")
    inc = os.path.join(a.wabt, "include")
    env = json.load(open(a.msvc_env)) if a.msvc_env else None
    cl = shutil.which("cl", path=env.get("PATH", "")) if env else None

    # The wasm2c runtime's checking modes (wasm-rt.h).  "guard": linear
    # memory reserved with guard pages, no bounds check in the code; "bounds":
    # an explicit check on every access (for a host that cannot reserve
    # 8 GiB of address space).  "stack": wasm2c counts call depth to trap on
    # exhaustion, which it does by default where it has no signal handler
    # (every Windows build); "nostack" turns that off.
    GUARD = ["WASM_RT_USE_MMAP=1", "WASM_RT_NONCONFORMING_ALLOW_OOB_READ_ELIMINATION=1"]
    BOUNDS = ["WASM_RT_USE_MMAP=0"]
    NOSTACK = ["WASM_RT_NONCONFORMING_UNCHECKED_STACK_EXHAUSTION=1"]
    variants = [("default", []), ("guard,nostack", GUARD + NOSTACK),
                ("bounds,nostack", BOUNDS + NOSTACK)]

    exes = [("native i686 GCC", exe32)]
    for mtag, mod in modules:
        w2c = os.path.join(out, "w2c_" + mtag.replace("+", "_"))
        os.makedirs(w2c, exist_ok=True)
        sh([os.path.join(a.wabt, "bin", "wasm2c"), mod, "-o", os.path.join(w2c, "bench.c"),
            "-n", "bench"])
        host_src = [os.path.join(w2c, "bench.c"), os.path.join(rtdir, "wasm-rt-impl.c"),
                    os.path.join(rtdir, "wasm-rt-mem-impl.c"),
                    os.path.join(rtdir, "wasm-rt-exceptions-impl.c"),
                    os.path.join(HOST, "wasi_min.c"), os.path.join(HOST, "main.c")]
        for vtag, defs in variants:
            if mtag != "plain" and vtag != "guard,nostack":
                continue
            name = "%s %s" % (mtag, vtag)
            exe = os.path.join(w2c, "mingw_%s.exe" % vtag.replace(",", "_"))
            sh([a.gcc64, "-O2", "-w", "-I" + inc, "-I" + rtdir, "-I" + w2c, "-I" + HOST,
                "-DW2C_MODULE_HEADER=\"bench.h\"", "-DW2C_MODULE=bench"] +
               ["-D" + d for d in defs] + host_src + ["-o", exe])
            exes.append(("w2c x64 MinGW GCC, " + name, exe))
            if cl:
                exe = os.path.join(w2c, "msvc_%s.exe" % vtag.replace(",", "_"))
                sh([cl, "/nologo", "/O2", "/w", "/I" + inc, "/I" + rtdir, "/I" + w2c,
                    "/I" + HOST, "/DW2C_MODULE_HEADER=\"bench.h\"", "/DW2C_MODULE=bench"] +
                   ["/D" + d for d in defs] + host_src +
                   ["/Fe:" + exe, "/Fo:" + os.path.join(w2c, "")], env=env, cwd=w2c)
                exes.append(("w2c x64 MSVC, " + name, exe))

    # Median of --runs, against the i686 reference.
    results = []
    for label, exe in exes:
        runs = []
        for _ in range(a.runs):
            p = subprocess.run([exe, a.scale], capture_output=True, text=True)
            got = dict((m.group(1), (float(m.group(2)), m.group(3))) for m in
                       re.finditer(r"^(\w+)\s+([\d.]+) ms\s+sum (\w+)", p.stdout, re.M))
            if not got:
                print("%s: no result" % label)
                print(p.stdout + p.stderr)
                break
            runs.append(got)
        if runs:
            results.append((label, {k: (statistics.median(r[k][0] for r in runs), runs[0][k][1])
                                    for k in runs[0]}))
    base = results[0][1]
    print()
    print("%-46s %18s %18s %18s" % ("configuration", "heap", "tcbl", "mtx"))
    for label, r in results:
        cells = []
        for k in ("heap", "tcbl", "mtx"):
            ms, ck = r[k]
            ok = "" if ck == base[k][1] else " SUM!"
            cells.append("%7.1f ms %5.2fx%s" % (ms, ms / base[k][0], ok))
        print("%-46s %18s %18s %18s" % ((label,) + tuple(cells)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
