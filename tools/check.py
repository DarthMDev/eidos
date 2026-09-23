#!/usr/bin/env python3
"""
check.py -- build and test eidos in every configuration this machine can do.

One command for "is the tree good": each configuration is configured,
built and tested in its own directory under --build-root, and the run fails
if any configuration fails to build, emits a compiler warning, or fails a
test.  A configuration whose compiler or runtime is not installed is
reported as skipped, not failed, and says why.

    python tools/check.py                         # everything available
    python tools/check.py --only gcc-ilp32 asan-ilp32
    python tools/check.py --list
    python tools/check.py --data-root ../aletheia/extract/data

The configurations:

    gcc-lp64      the default compiler, 64-bit pointers
    gcc-ilp32     the reference configuration: cmake/ilp32-toolchain.cmake
    clang-lp64    clang, 64-bit
    clang-ilp32   clang with the ILP32 toolchain file
    asan-lp64     AddressSanitizer + UBSan, 64-bit
    asan-ilp32    the same, 32-bit
    nosdl-lp64    -DEIDOS_SDL=OFF: the build a machine without SDL2 gets

Without game data the data-driven tests are registered as disabled and are
reported in the count; pass --data-root to run them.  Standard library only.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ALETHEIA = os.path.normpath(os.path.join(ROOT, "..", "aletheia"))
TOOLCHAIN = os.path.join(ROOT, "cmake", "ilp32-toolchain.cmake")
SAN = "-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"

CONFIGS = {
    "gcc-lp64":    dict(cc=None, args=[]),
    "gcc-ilp32":   dict(cc=None, args=["-DCMAKE_TOOLCHAIN_FILE=" + TOOLCHAIN]),
    "clang-lp64":  dict(cc="clang", args=[]),
    "clang-ilp32": dict(cc="clang", args=["-DCMAKE_TOOLCHAIN_FILE=" + TOOLCHAIN]),
    "asan-lp64":   dict(cc=None, args=["-DCMAKE_C_FLAGS=" + SAN,
                                       "-DCMAKE_EXE_LINKER_FLAGS=" + SAN]),
    "asan-ilp32":  dict(cc=None, args=["-DCMAKE_TOOLCHAIN_FILE=" + TOOLCHAIN,
                                       "-DCMAKE_C_FLAGS=-m32 " + SAN,
                                       "-DCMAKE_EXE_LINKER_FLAGS=-m32 " + SAN]),
    "nosdl-lp64":  dict(cc=None, args=["-DEIDOS_SDL=OFF"]),
}


def run(cmd, log, cwd=None):
    with open(log, "w", encoding="utf-8", errors="replace") as fh:
        p = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT, cwd=cwd)
    return p.returncode


def tail(path, n=25):
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            return "".join(fh.readlines()[-n:])
    except OSError:
        return ""


def check_one(name, cfg, build_root, aletheia, data_root, generator):
    bdir = os.path.join(build_root, name)
    shutil.rmtree(bdir, ignore_errors=True)
    os.makedirs(bdir, exist_ok=True)
    res = {"name": name, "status": "FAIL", "warnings": 0, "tests": "",
           "note": ""}

    if cfg["cc"] and not shutil.which(cfg["cc"]):
        res.update(status="skip", note="%s not installed" % cfg["cc"])
        return res

    cmd = ["cmake", "-S", ROOT, "-B", bdir, "-DALETHEIA_ROOT=" + aletheia]
    if generator:
        cmd += ["-G", generator]
    if cfg["cc"]:
        cmd.append("-DCMAKE_C_COMPILER=" + cfg["cc"])
    if data_root:
        cmd.append("-DBW_DATA_ROOT=" + os.path.abspath(data_root))
    cmd += cfg["args"]
    cfg_log = os.path.join(bdir, "check-configure.log")
    if run(cmd, cfg_log) != 0:
        text = tail(cfg_log)
        # A 32-bit configuration on a host without the 32-bit runtime fails
        # in CMake's own compiler check; that is a missing package, not a
        # defect in the tree.
        if "ilp32" in name and ("-m32" in text or "cannot find" in text
                                or "pointers" in text):
            res.update(status="skip", note="no 32-bit runtime (gcc-multilib?)")
        else:
            res["note"] = "configure failed, see " + cfg_log
            print(text)
        return res

    build_log = os.path.join(bdir, "check-build.log")
    rc = run(["cmake", "--build", bdir], build_log)
    with open(build_log, encoding="utf-8", errors="replace") as fh:
        blog = fh.read()
    res["warnings"] = len(re.findall(r"\bwarning:", blog))
    if rc != 0:
        res["note"] = "build failed, see " + build_log
        print(tail(build_log))
        return res

    test_log = os.path.join(bdir, "check-test.log")
    rc = run(["ctest", "--test-dir", bdir, "--output-on-failure"], test_log)
    tlog = tail(test_log, 400)
    m = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", tlog)
    disabled = len(re.findall(r"Not Run \(Disabled\)", tlog))
    if m:
        total = int(m.group(3))
        res["tests"] = "%d/%d" % (total - int(m.group(2)), total)
        if disabled:
            res["tests"] += " (+%d disabled)" % disabled
    if rc != 0:
        res["note"] = "tests failed, see " + test_log
        print(tail(test_log, 60))
        return res
    if res["warnings"]:
        res["note"] = "warnings, see " + build_log
        for line in blog.splitlines():
            if "warning:" in line:
                print("  " + line)
        return res
    res["status"] = "ok"
    return res


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--aletheia-root", default=DEFAULT_ALETHEIA)
    ap.add_argument("--data-root", default=None,
                    help="extracted NitroFS tree; enables the data-driven tests")
    ap.add_argument("--build-root", default=None,
                    help="where the per-configuration build directories go "
                         "(default: a fresh temporary directory)")
    ap.add_argument("--only", nargs="+", metavar="CONFIG",
                    help="run just these configurations")
    ap.add_argument("--list", action="store_true", help="list configurations")
    args = ap.parse_args(argv)

    if args.list:
        for name in CONFIGS:
            print(name)
        return 0
    if not shutil.which("cmake"):
        print("check.py: cmake is not on PATH")
        return 2

    names = args.only or list(CONFIGS)
    for n in names:
        if n not in CONFIGS:
            print("check.py: unknown configuration %r (--list)" % n)
            return 2

    build_root = args.build_root or tempfile.mkdtemp(prefix="eidos-check-")
    generator = "Ninja" if shutil.which("ninja") else None
    aletheia = os.path.abspath(args.aletheia_root)
    print("[*] build root: %s" % build_root)

    results = []
    for n in names:
        print("[*] %s ..." % n, flush=True)
        results.append(check_one(n, CONFIGS[n], build_root, aletheia,
                                 args.data_root, generator))

    print()
    print("%-12s %-6s %-9s %-20s %s" % ("config", "status", "warnings", "tests", "note"))
    for r in results:
        print("%-12s %-6s %-9d %-20s %s" % (r["name"], r["status"], r["warnings"],
                                             r["tests"], r["note"]))
    bad = [r for r in results if r["status"] == "FAIL"]
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
