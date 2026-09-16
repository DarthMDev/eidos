#!/usr/bin/env python3
"""
hostbuild.py -- build and run the port tests without CMake.

CMake is the supported build (CMakeLists.txt, and README.md tells you how).
This script exists for two cases CMake does not cover:

  * a machine with a C compiler but no CMake, which is where this lane was
    written;
  * `--check`, a compile-and-link-only pass with the cross compiler already
    in the aletheia checkout's tools/gcc-arm/.  It proves every source
    compiles clean and that the symbol closure is complete (the link fails
    on the first missing name), which is most of what the CMake build would
    tell you, without needing a native toolchain at all.  It cannot run the
    tests: the resulting binary is an ARM ELF and there is no runner for it
    here.

It reads the source lists straight out of CMakeLists.txt, so there is one
list, not two.  Matched sources and the ARM cross compiler are read out of
the aletheia checkout (--aletheia-root, default ../aletheia).

    python tools/hostbuild.py                  # find a native compiler, build, run
    python tools/hostbuild.py --check          # cross compile + link only
    python tools/hostbuild.py --probe          # say what is available, do nothing
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = ROOT
CMAKELISTS = os.path.join(PORT, "CMakeLists.txt")
DEFAULT_ALETHEIA_ROOT = os.path.normpath(os.path.join(ROOT, "..", "aletheia"))

TESTS = ["test_text", "test_stats", "test_tcbl"]


def read_source_lists(aletheia_root, ilp32=False):
    """Pull BW_MATCHED_SOURCES, BW_SDK_SOURCES and BW_STUB_SOURCES out of
    CMakeLists.txt so the two builds can never drift apart.

    The nine pointer-width-dependent tcbl sources live in their own two
    lists there, because which of them is compiled is decided by the target's
    pointer width (README.md, "Pointer width, and the reference
    configuration").  `ilp32` makes the
    same choice CMake's EIDOS_TCBL_MATCHED does: the matched
    sources where a pointer is four bytes, the stubs/reimpl_ stand-ins
    otherwise.
    """
    with open(CMAKELISTS, encoding="utf-8") as fh:
        text = fh.read()
    out = []
    accessors = "BW_TCBL_MATCHED" if ilp32 else "BW_TCBL_REIMPL"
    for var in ("BW_MATCHED_SOURCES", "BW_SDK_SOURCES", "BW_STUB_SOURCES",
                accessors):
        m = re.search(r"set\(%s\s*(.*?)\n\)" % var, text, re.S)
        if not m:
            raise SystemExit("hostbuild.py: %s not found in CMakeLists.txt" % var)
        for line in m.group(1).splitlines():
            line = line.split("#")[0].strip()
            if not line:
                continue
            line = line.replace("${ALETHEIA_ROOT}", aletheia_root)
            line = line.replace("${CMAKE_CURRENT_SOURCE_DIR}", PORT)
            out.append(os.path.normpath(line))
    return out


def find_cross(aletheia_root):
    hits = sorted(glob.glob(os.path.join(
        aletheia_root, "tools", "gcc-arm", "*", "bin", "arm-none-eabi-gcc.exe")))
    hits += sorted(glob.glob(os.path.join(
        aletheia_root, "tools", "gcc-arm", "*", "bin", "arm-none-eabi-gcc")))
    env = os.environ.get("BW_ARM_TOOLCHAIN")
    if env:
        for name in ("arm-none-eabi-gcc.exe", "arm-none-eabi-gcc"):
            cand = os.path.join(env, name)
            if os.path.exists(cand):
                hits.insert(0, cand)
    return hits[0] if hits else None


def find_native():
    """Return (kind, path) for the first usable native C compiler."""
    for name in ("cc", "gcc", "clang"):
        path = shutil.which(name)
        if path:
            return ("unix", path)
    path = shutil.which("cl")
    if path:
        return ("msvc", path)
    return (None, None)


def probe(aletheia_root):
    kind, native = find_native()
    print("cmake:          %s" % (shutil.which("cmake") or "NOT FOUND"))
    print("ctest:          %s" % (shutil.which("ctest") or "NOT FOUND"))
    print("native C:       %s" % (native or "NOT FOUND (cc, gcc, clang, cl)"))
    if native:
        print("                kind: %s" % kind)
    print("cross (arm):    %s" % (find_cross(aletheia_root) or
                                   "NOT FOUND under %s/tools/gcc-arm/" % aletheia_root))
    print("python:         %s" % sys.executable)
    return 0


def run(cmd, **kw):
    r = subprocess.run(cmd, **kw)
    return r.returncode


def gen_fixtures(builddir, aletheia_root, data_root):
    os.makedirs(builddir, exist_ok=True)
    out = os.path.join(builddir, "bw_fixtures.h")
    rc = run([sys.executable, os.path.join(ROOT, "tools", "fixtures.py"),
              "--aletheia-root", aletheia_root,
              "--data-root", data_root, "--out", out])
    if rc != 0:
        raise SystemExit("hostbuild.py: fixture generation failed")
    return out


def build(cc, kind, builddir, aletheia_root, data_root, link, run_tests,
          ilp32=False):
    # The cross check builds for arm-none-eabi, which is LP32; a native
    # build here is whatever this Python's host is, which on every machine
    # the port is developed on is LP64.
    sources = read_source_lists(aletheia_root, ilp32=ilp32)
    gen_fixtures(builddir, aletheia_root, data_root)

    if kind == "msvc":
        raise SystemExit("hostbuild.py: the MSVC path is not implemented; "
                         "use CMake, which knows how to drive cl.exe")

    warn = ["-std=c99", "-Wall", "-Wextra", "-fsigned-char"]
    # The same include directories CMakeLists.txt puts on the eidos
    # target: the shim, the 2D engine and the frame skeleton each have
    # headers the port's own sources reach by bare name.
    inc = []
    for d in (("sdk",), ("engine", "g2d"), ("engine", "frame")):
        inc += ["-I", os.path.join(PORT, *d)]
    objs = []
    failed = 0
    for src in sources:
        obj = os.path.join(builddir, os.path.splitext(os.path.basename(src))[0] + ".o")
        extra = ["-Wno-unused-parameter"] if os.sep + "matched" + os.sep in src else []
        rc = run([cc] + warn + inc + extra + ["-c", src, "-o", obj])
        if rc != 0:
            failed += 1
            print("hostbuild.py: FAILED to compile %s" % src)
        else:
            objs.append(obj)
    for t in TESTS:
        src = os.path.join(PORT, "tests", t + ".c")
        obj = os.path.join(builddir, t + ".o")
        rc = run([cc] + warn + inc + ["-I", builddir, "-c", src, "-o", obj])
        if rc != 0:
            failed += 1
            print("hostbuild.py: FAILED to compile %s" % src)
    if failed:
        return 1
    print("[*] %d objects compiled clean" % (len(objs) + len(TESTS)))

    if not link:
        return 0

    # Only the basename: the directory can contain "gcc" too (tools/gcc-arm).
    cc_dir, cc_base = os.path.split(cc)
    ar = os.path.join(cc_dir, cc_base.replace("gcc", "ar").replace("clang", "ar"))
    if not os.path.exists(ar) and shutil.which("ar"):
        ar = shutil.which("ar")
    lib = os.path.join(builddir, "libeidos.a")
    if os.path.exists(lib):
        os.remove(lib)
    if run([ar, "rcs", lib] + objs) != 0:
        return 1

    extra_link = ["--specs=nosys.specs"] if "arm-none-eabi" in cc else []
    rc_all = 0
    for t in TESTS:
        exe = os.path.join(builddir, t + (".elf" if extra_link else ""))
        obj = os.path.join(builddir, t + ".o")
        p = subprocess.run([cc, "-std=c99"] + extra_link + [obj, lib, "-o", exe],
                           capture_output=True, text=True)
        bad = [ln for ln in p.stderr.splitlines()
               if "undefined reference" in ln or "multiple definition" in ln]
        if p.returncode != 0 or bad:
            print("hostbuild.py: LINK FAILED for %s" % t)
            for ln in bad:
                print("    " + ln)
            rc_all = 1
        else:
            print("[*] linked %s" % exe)
    if rc_all:
        return rc_all

    if not run_tests:
        print("[*] link closure complete; not running (cross-built binary)")
        return 0

    for t in TESTS:
        exe = os.path.join(builddir, t)
        print("---- %s ----" % t)
        if run([exe, data_root]) != 0:
            rc_all = 1
    return rc_all


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--aletheia-root", default=DEFAULT_ALETHEIA_ROOT,
                    help="path to the aletheia checkout (default: ../aletheia)")
    ap.add_argument("--build-dir", default=os.path.join(ROOT, "build", "host"))
    ap.add_argument("--data-root", default=None,
                    help="default: <aletheia-root>/extract/data")
    ap.add_argument("--check", action="store_true",
                    help="compile and link with the cross compiler; do not run")
    ap.add_argument("--probe", action="store_true", help="report tools and exit")
    args = ap.parse_args(argv)

    aletheia_root = os.path.abspath(args.aletheia_root)
    data_root = args.data_root or os.path.join(aletheia_root, "extract", "data")

    if args.probe:
        return probe(aletheia_root)

    if args.check:
        cc = find_cross(aletheia_root)
        if cc is None:
            raise SystemExit("hostbuild.py: no arm-none-eabi-gcc under %s/tools/gcc-arm/"
                             % aletheia_root)
        print("[*] cross compile check with %s" % cc)
        return build(cc, "unix", args.build_dir, aletheia_root, data_root,
                    link=True, run_tests=False, ilp32=True)

    kind, cc = find_native()
    if cc is None:
        print("hostbuild.py: no native C compiler (cc, gcc, clang, cl) on PATH.")
        print("              try `--check` for a cross compile-and-link pass,")
        print("              or install a compiler and use CMake.")
        return 2
    print("[*] native build with %s" % cc)
    return build(cc, kind, args.build_dir, aletheia_root, data_root,
                link=True, run_tests=True)


if __name__ == "__main__":
    sys.exit(main())
