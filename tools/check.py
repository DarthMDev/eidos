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
    python tools/check.py --only msvc-x86 --define EIDOS_BOOT=ON

The configurations:

    gcc-lp64      the default compiler, 64-bit pointers
    gcc-ilp32     the reference configuration: cmake/ilp32-toolchain.cmake
    clang-lp64    clang, 64-bit
    clang-ilp32   clang with the ILP32 toolchain file
    asan-lp64     AddressSanitizer + UBSan, 64-bit
    asan-ilp32    the same, 32-bit
    nosdl-lp64    -DEIDOS_SDL=OFF: the build a machine without SDL2 gets
    mingw-x64     Windows x64 with MinGW-w64, tests under wine64 if present
    mingw-x86     Windows x86, the same, tests under a 32-bit wine if present
    msvc-x64      Visual Studio's cl.exe, x64 (LLP64), Windows only
    msvc-x86      Visual Studio's cl.exe, x86: the ILP32 reference under MSVC
    clangcl-x64   Visual Studio's clang-cl, x64, Windows only

The MSVC configurations find Visual Studio with vswhere, load its developer
environment (vcvarsall.bat x64 or x64_x86) into the child processes only,
and build with Ninja.  They are skipped, with the reason, on a host that is
not Windows, has no Visual Studio, or has Visual Studio without the C++
build tools.

The Windows configurations (mingw-*, msvc-*, clangcl-*) build the SDL2
platform layer and run test_view with it: they configure with
-DEIDOS_SDL_FETCH=ON, so tools/fetch_sdl.py fetches the official SDL2
package for that toolchain (MinGW or VC) into its per-user cache on first
use, and a configuration that still ends up without SDL2 fails rather than
quietly building less.  On a Windows host the MinGW configurations are
native builds and run their tests directly; elsewhere they cross-compile and
need Wine to run them.

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

# The Visual Studio component that carries cl.exe, its headers and its
# libraries for x86 and x64, and the one that adds clang-cl.  A Visual Studio
# installed without the first can still have a cl.exe under VC/Tools/MSVC,
# but no vcvarsall.bat, no headers and no CRT, and compiles nothing.
VS_VC_COMPONENT = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
VS_CLANG_COMPONENT = "Microsoft.VisualStudio.Component.VC.Llvm.Clang"

# GCC and Clang print "warning:", cl.exe "warning C4100:", link.exe
# "warning LNK4098:".
WARNING_RE = re.compile(r"\bwarning(?: [A-Z]+\d+)?\s*:")

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
    # Windows, cross-compiled with MinGW-w64.  The tests run under Wine
    # when it is installed (wine64 for x64; a 32-bit Wine for x86), and the
    # configuration is a warning-free compile and link check otherwise.
    # On a Windows host they are native and need neither.
    "mingw-x64":   dict(cc="x86_64-w64-mingw32-gcc", cross="wine64", sdl=True,
                        args=["-DEIDOS_SDL_FETCH=ON"]),
    "mingw-x86":   dict(cc="i686-w64-mingw32-gcc", cross="wine", sdl=True,
                        args=["-DEIDOS_SDL_FETCH=ON"]),
    # Visual Studio.  `vcvars` is the vcvarsall.bat argument that picks the
    # target: x64_x86 is the x64-hosted compiler for 32-bit x86, whose
    # executables run on the same machine.  No toolchain file: under MSVC the
    # pointer width comes from the environment (cmake/msvc.cmake).
    # Debug, stated rather than left to CMake's MSVC default: the debug CRT
    # checks the heap's guard bytes on every free and fills fresh allocations
    # with 0xCD, the closest thing to a sanitizer these configurations have.
    # Its report windows are turned off in every executable
    # (sdk/plat_nodialog.c), so it cannot stall the run.
    # SDL2 is the official VC development package (tools/fetch_sdl.py).
    "msvc-x64":    dict(cc="cl", vcvars="x64", sdl=True,
                        args=["-DEIDOS_SDL_FETCH=ON", "-DCMAKE_BUILD_TYPE=Debug"]),
    "msvc-x86":    dict(cc="cl", vcvars="x64_x86", sdl=True,
                        args=["-DEIDOS_SDL_FETCH=ON", "-DCMAKE_BUILD_TYPE=Debug"]),
    "clangcl-x64": dict(cc="clang-cl", vcvars="x64", clang=True, sdl=True,
                        args=["-DEIDOS_SDL_FETCH=ON", "-DCMAKE_BUILD_TYPE=Debug"]),
}


def find_vs(clang=False):
    """(installation path, None) for a Visual Studio with the C++ build tools
    (and clang-cl, if `clang`), or (None, the reason there is none)."""
    if os.name != "nt":
        return None, "not a Windows host"
    pf86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = os.path.join(pf86, "Microsoft Visual Studio", "Installer",
                           "vswhere.exe")
    if not os.path.isfile(vswhere):
        return None, "no Visual Studio (vswhere.exe not found)"

    def ask(*requires):
        cmd = [vswhere, "-latest", "-products", "*",
               "-property", "installationPath"]
        if requires:
            cmd += ["-requires"] + list(requires)
        out = subprocess.run(cmd, capture_output=True, text=True).stdout.strip()
        return out.splitlines()[0] if out else None

    if not ask():
        return None, "no Visual Studio installation"
    path = ask(VS_VC_COMPONENT)
    if not path:
        return None, ("Visual Studio without the C++ build tools (%s)"
                      % VS_VC_COMPONENT)
    if clang:
        path = ask(VS_VC_COMPONENT, VS_CLANG_COMPONENT)
        if not path:
            return None, "Visual Studio without clang-cl (%s)" % VS_CLANG_COMPONENT
    return path, None


def vs_env(install, arch, clang=False):
    """(the environment `vcvarsall.bat arch` produces, None), or (None, why).

    The environment is handed to the child processes only; this process's
    own is left alone, so one run can mix MSVC and MinGW configurations."""
    bat = os.path.join(install, "VC", "Auxiliary", "Build", "vcvarsall.bat")
    if not os.path.isfile(bat):
        return None, "no vcvarsall.bat in " + install
    p = subprocess.run('cmd /s /c ""%s" %s >nul && set"' % (bat, arch),
                       capture_output=True, text=True)
    if p.returncode != 0:
        return None, "vcvarsall.bat %s failed" % arch
    env = {}
    for line in p.stdout.splitlines():
        key, sep, val = line.partition("=")
        if sep and key:
            # Windows names are case-insensitive ("Path"); a dict is not.
            env[key.upper()] = val
    if clang:
        # vcvarsall does not put Visual Studio's own LLVM on PATH.
        llvm = os.path.join(install, "VC", "Tools", "Llvm", "x64", "bin")
        env["PATH"] = llvm + os.pathsep + env.get("PATH", "")
    return env, None


def find_wine(name):
    """A Wine loader for `name` ("wine64" or "wine"), or None."""
    for cand in (shutil.which(name), "/usr/lib/wine/" + name):
        if cand and os.path.exists(cand):
            return cand
    return None


def run(cmd, log, cwd=None, env=None):
    with open(log, "w", encoding="utf-8", errors="replace") as fh:
        p = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT, cwd=cwd,
                           env=env)
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
           "sdl": "", "note": ""}
    # A cross configuration is native on a host of its own OS: MinGW on
    # Windows needs no CMAKE_SYSTEM_NAME and no Wine.
    cross = cfg.get("cross") if os.name != "nt" else None

    env = None
    if cfg.get("vcvars"):
        install, why = find_vs(cfg.get("clang", False))
        if install:
            env, why = vs_env(install, cfg["vcvars"], cfg.get("clang", False))
        if env is None:
            res.update(status="skip", note=why)
            return res
        # Visual Studio's CMake component ships a Ninja; without it, NMake is
        # the generator every developer environment has.
        generator = ("Ninja" if shutil.which("ninja", path=env.get("PATH"))
                     else "NMake Makefiles")

    if cfg["cc"] and not shutil.which(cfg["cc"], path=(env or {}).get("PATH")):
        res.update(status="skip", note="%s not installed" % cfg["cc"])
        return res

    cmd = ["cmake", "-S", ROOT, "-B", bdir, "-DALETHEIA_ROOT=" + aletheia]
    if generator:
        cmd += ["-G", generator]
    if cfg["cc"]:
        cmd.append("-DCMAKE_C_COMPILER=" + cfg["cc"])
    if data_root:
        cmd.append("-DBW_DATA_ROOT=" + os.path.abspath(data_root))
    if cross:
        cmd.append("-DCMAKE_SYSTEM_NAME=Windows")
    cmd += cfg["args"]
    wine = find_wine(cross) if cross else None
    if wine:
        cmd.append("-DCMAKE_CROSSCOMPILING_EMULATOR=" + wine)
    cfg_log = os.path.join(bdir, "check-configure.log")
    if run(cmd, cfg_log, env=env) != 0:
        text = tail(cfg_log)
        # A 32-bit configuration on a host without the 32-bit runtime fails
        # in CMake's own compiler check; that is a missing package, not a
        # defect in the tree.
        # Only CMake's own compiler check counts: the port's guard ("asked
        # for ILP32, got 8-byte pointers") is a real failure, and matching
        # it here once hid a toolchain bug as a skip.
        if "ilp32" in name and "is not able to compile" in text:
            res.update(status="skip", note="no 32-bit runtime (gcc-multilib?)")
        else:
            res["note"] = "configure failed, see " + cfg_log
            print(text)
        return res

    with open(cfg_log, encoding="utf-8", errors="replace") as fh:
        m = re.search(r"^-- SDL2: (.*)$", fh.read(), re.M)
    found = bool(m) and not m.group(1).startswith(("not found", "off"))
    res["sdl"] = "yes" if found else "no"
    if cfg.get("sdl") and not found:
        res["note"] = ("SDL2 required but not found (%s), see %s"
                       % (m.group(1) if m else "no SDL2 line", cfg_log))
        return res

    build_log = os.path.join(bdir, "check-build.log")
    rc = run(["cmake", "--build", bdir], build_log, env=env)
    with open(build_log, encoding="utf-8", errors="replace") as fh:
        blog = fh.read()
    res["warnings"] = len(WARNING_RE.findall(blog))
    if rc != 0:
        res["note"] = "build failed, see " + build_log
        print(tail(build_log))
        return res

    if cross and not wine:
        res["status"] = "ok" if res["warnings"] == 0 else "FAIL"
        res["note"] = "built only: no %s to run the tests" % cross
        return res

    test_log = os.path.join(bdir, "check-test.log")
    rc = run(["ctest", "--test-dir", bdir, "--output-on-failure"], test_log,
             env=env)
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
            if WARNING_RE.search(line):
                print("  " + line)
        return res
    res["status"] = "ok"
    return res


def no_error_dialogs():
    """On Windows, no "has stopped working" window for a crashing child.

    The error mode is inherited by every process this one starts (CMake,
    the compilers, CTest and the tests), so a crash is reported as an exit
    code instead of a window that waits for a click.  The tests also set it
    themselves (sdk/plat_nodialog.c), with the CRT's own report windows."""
    if os.name != "nt":
        return
    import ctypes
    SEM_FAILCRITICALERRORS = 0x0001
    SEM_NOGPFAULTERRORBOX = 0x0002
    SEM_NOOPENFILEERRORBOX = 0x8000
    ctypes.windll.kernel32.SetErrorMode(SEM_FAILCRITICALERRORS
                                        | SEM_NOGPFAULTERRORBOX
                                        | SEM_NOOPENFILEERRORBOX)


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
    ap.add_argument("--define", action="append", default=[], metavar="NAME=VALUE",
                    help="an extra CMake cache entry for every configuration "
                         "(repeatable), e.g. EIDOS_BOOT=ON for the game's boot")
    args = ap.parse_args(argv)
    for d in args.define:
        if "=" not in d:
            print("check.py: --define wants NAME=VALUE, got %r" % d)
            return 2
    extra = ["-D" + d for d in args.define]

    if args.list:
        for name in CONFIGS:
            print(name)
        return 0
    if not shutil.which("cmake"):
        print("check.py: cmake is not on PATH")
        return 2

    no_error_dialogs()
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
        cfg = dict(CONFIGS[n], args=CONFIGS[n]["args"] + extra)
        results.append(check_one(n, cfg, build_root, aletheia,
                                 args.data_root, generator))

    print()
    print("%-12s %-6s %-9s %-4s %-20s %s" % ("config", "status", "warnings",
                                             "sdl", "tests", "note"))
    for r in results:
        print("%-12s %-6s %-9d %-4s %-20s %s" % (r["name"], r["status"],
                                                 r["warnings"], r["sdl"],
                                                 r["tests"], r["note"]))
    bad = [r for r in results if r["status"] == "FAIL"]
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
