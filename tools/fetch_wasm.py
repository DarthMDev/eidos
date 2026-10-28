#!/usr/bin/env python3
"""
tools/fetch_wasm.py -- the 64-bit build's toolchain, from the official
releases, into a cache outside the repository (docs/lp64.md, "The wasm32
configuration's architecture").

Three pieces, each pinned and verified against a SHA-256 recorded below:

  wasi-sdk 25    clang, wasm-ld and the WASI C library: stage one, the
                 module (eidos_boot.wasm)
  binaryen 123   wasm-opt: the module's optimisation and --fpcast-emu
  wabt 1.0.37    wasm2c, built here from the release's source with the
                 host's C++ compiler (USE_INTERNAL_SHA256: no OpenSSL), and
                 its runtime (wasm2c/wasm-rt-*.c), which stage two compiles

The prebuilt wasi-sdk and binaryen exist for Windows x86_64, Linux x86_64
and macOS (arm64 and x86_64); another host gets an error naming what is
missing.

Where things go.  --cache, else EIDOS_WASM_CACHE, else the per-user cache
(Windows %LOCALAPPDATA%\\eidos\\wasm, macOS ~/Library/Caches/eidos/wasm,
elsewhere $XDG_CACHE_HOME/eidos/wasm or ~/.cache/eidos/wasm).  An unpacked
tree is used only when it carries the stamp this script writes after a
verified extraction (.eidos-sha256).

Usage:

    python tools/fetch_wasm.py              # fetch, unpack, build wasm2c
    python tools/fetch_wasm.py --locate     # print the paths, fetch nothing
    python tools/fetch_wasm.py --print-sha FILE

--locate prints one NAME=path line each for WASI_SDK_PREFIX, WASM_OPT,
WABT_ROOT and WASM2C (what tools/build64.py and CI pass to CMake), and
exits 1 when any is missing.  Standard library only.
"""

import argparse
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request

WASI_SDK = "25.0"
BINARYEN = "123"
WABT = "1.0.37"

# the archive for each host
WASI_SDK_PKGS = {
    "windows-x86_64": "wasi-sdk-25.0-x86_64-windows.tar.gz",
    "linux-x86_64": "wasi-sdk-25.0-x86_64-linux.tar.gz",
    "macos-arm64": "wasi-sdk-25.0-arm64-macos.tar.gz",
    "macos-x86_64": "wasi-sdk-25.0-x86_64-macos.tar.gz",
}
BINARYEN_PKGS = {
    "windows-x86_64": "binaryen-version_123-x86_64-windows.tar.gz",
    "linux-x86_64": "binaryen-version_123-x86_64-linux.tar.gz",
    "macos-arm64": "binaryen-version_123-arm64-macos.tar.gz",
    "macos-x86_64": "binaryen-version_123-x86_64-macos.tar.gz",
}
WABT_SRC = "wabt-1.0.37.tar.xz"

URLS = {
    "wasi-sdk": "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-25/",
    "binaryen": "https://github.com/WebAssembly/binaryen/releases/download/version_123/",
    "wabt": "https://github.com/WebAssembly/wabt/releases/download/1.0.37/",
}

# Hashed from the downloads (--print-sha); binaryen's and wabt's agree with
# the .sha256 files their releases publish beside them.
SHA256 = {
    "wasi-sdk-25.0-x86_64-windows.tar.gz": "ebd4868f95a6e764bf29283e3bdf54df0df3902a29737176da9c17da218b939b",
    "wasi-sdk-25.0-x86_64-linux.tar.gz": "52640dde13599bf127a95499e61d6d640256119456d1af8897ab6725bcf3d89c",
    "wasi-sdk-25.0-arm64-macos.tar.gz": "e1e529ea226b1db0b430327809deae9246b580fa3cae32d31c82dfe770233587",
    "wasi-sdk-25.0-x86_64-macos.tar.gz": "55e3ff3fee1a15678a16eeccba0129276c9f6be481bc9c283e7f9f65bf055c11",
    "binaryen-version_123-x86_64-windows.tar.gz": "7b3568424a0f871a52865d5c78177db646b1832a8c487321e27703103f936880",
    "binaryen-version_123-x86_64-linux.tar.gz": "e959f2170af4c20c552e9de3a0253704d6a9d2766e8fdb88e4d6ac4bae9388fe",
    "binaryen-version_123-arm64-macos.tar.gz": "74428be348c1a09863e7b642a1fa948cabf8ec9561052233d8288e941951725b",
    "binaryen-version_123-x86_64-macos.tar.gz": "cc18b14d2b673d9c66bf54f31ff2b0ceb23ba5132455b893965ae2792f9e00dd",
    "wabt-1.0.37.tar.xz": "aca7fc4ba9974ad456372ab35f9cc1bac0b9fab3321b6df399d14dda9ca8a1a9",
}


def host_key():
    s = platform.system().lower()
    m = platform.machine().lower()
    m = {"amd64": "x86_64", "x64": "x86_64", "aarch64": "arm64"}.get(m, m)
    if s.startswith("win"):
        s = "windows"
    elif s == "darwin":
        s = "macos"
    return "%s-%s" % (s, m)


def cache_dir(arg):
    if arg:
        return os.path.abspath(arg)
    env = os.environ.get("EIDOS_WASM_CACHE")
    if env:
        return os.path.abspath(env)
    if sys.platform.startswith("win"):
        base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
        return os.path.join(base, "eidos", "wasm")
    if sys.platform == "darwin":
        return os.path.expanduser("~/Library/Caches/eidos/wasm")
    base = os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache")
    return os.path.join(base, "eidos", "wasm")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(cache, kind, name):
    want = SHA256.get(name)
    if want is None:
        sys.exit("fetch_wasm: no recorded SHA-256 for %s" % name)
    dl = os.path.join(cache, "downloads")
    os.makedirs(dl, exist_ok=True)
    path = os.path.join(dl, name)
    if os.path.exists(path) and sha256(path) == want:
        return path
    url = URLS[kind] + name
    print("fetch_wasm: downloading %s" % url)
    tmp = path + ".part"
    with urllib.request.urlopen(url) as r, open(tmp, "wb") as f:
        shutil.copyfileobj(r, f)
    got = sha256(tmp)
    if got != want:
        os.remove(tmp)
        sys.exit("fetch_wasm: %s has SHA-256 %s, expected %s" % (name, got, want))
    os.replace(tmp, path)
    return path


def unpack(cache, archive, dest_name):
    dest = os.path.join(cache, dest_name)
    stamp = os.path.join(dest, ".eidos-sha256")
    want = SHA256[os.path.basename(archive)]
    if os.path.exists(stamp) and open(stamp).read().strip() == want:
        return dest
    if os.path.exists(dest):
        shutil.rmtree(dest)
    tmp = dest + ".part"
    if os.path.exists(tmp):
        shutil.rmtree(tmp)
    os.makedirs(tmp)
    with tarfile.open(archive) as t:
        try:
            t.extractall(tmp, filter="data")
        except TypeError:                      # Python before 3.12
            t.extractall(tmp)
    # each archive holds one top directory
    tops = os.listdir(tmp)
    src = os.path.join(tmp, tops[0]) if len(tops) == 1 else tmp
    shutil.move(src, dest)
    shutil.rmtree(tmp, ignore_errors=True)
    with open(stamp, "w") as f:
        f.write(want + "\n")
    return dest


def exe(name):
    return name + (".exe" if sys.platform.startswith("win") else "")


def build_wasm2c(cache, src):
    out = os.path.join(cache, "wasm2c-build")
    tool = os.path.join(out, exe("wasm2c"))
    stamp = os.path.join(out, ".eidos-sha256")
    want = SHA256[WABT_SRC]
    if os.path.exists(tool) and os.path.exists(stamp) and open(stamp).read().strip() == want:
        return tool
    os.makedirs(out, exist_ok=True)
    args = ["cmake", "-S", src, "-B", out, "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_TESTS=OFF", "-DBUILD_LIBWASM=OFF", "-DUSE_INTERNAL_SHA256=ON",
            "-DWITH_WASI=OFF"]
    if shutil.which("ninja"):
        args += ["-G", "Ninja"]
    subprocess.check_call(args)
    subprocess.check_call(["cmake", "--build", out, "--target", "wasm2c", "--parallel"])
    with open(stamp, "w") as f:
        f.write(want + "\n")
    return tool


def locate(cache, key):
    found = {}
    sdk = os.path.join(cache, "wasi-sdk-%s" % WASI_SDK)
    if os.path.exists(os.path.join(sdk, ".eidos-sha256")):
        found["WASI_SDK_PREFIX"] = sdk
    bn = os.path.join(cache, "binaryen-%s" % BINARYEN)
    if os.path.exists(os.path.join(bn, "bin", exe("wasm-opt"))):
        found["WASM_OPT"] = os.path.join(bn, "bin", exe("wasm-opt"))
    wabt = os.path.join(cache, "wabt-%s" % WABT)
    if os.path.exists(os.path.join(wabt, "wasm2c", "wasm-rt-impl.c")):
        found["WABT_ROOT"] = wabt
    w2c = os.path.join(cache, "wasm2c-build", exe("wasm2c"))
    if os.path.exists(w2c):
        found["WASM2C"] = w2c
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--cache")
    ap.add_argument("--locate", action="store_true")
    ap.add_argument("--print-sha", metavar="FILE")
    a = ap.parse_args()
    if a.print_sha:
        print(sha256(a.print_sha), os.path.basename(a.print_sha))
        return 0
    cache = cache_dir(a.cache)
    key = host_key()
    if not a.locate:
        if key not in WASI_SDK_PKGS:
            sys.exit("fetch_wasm: no prebuilt wasi-sdk and binaryen for %s" % key)
        unpack(cache, download(cache, "wasi-sdk", WASI_SDK_PKGS[key]), "wasi-sdk-%s" % WASI_SDK)
        unpack(cache, download(cache, "binaryen", BINARYEN_PKGS[key]), "binaryen-%s" % BINARYEN)
        src = unpack(cache, download(cache, "wabt", WABT_SRC), "wabt-%s" % WABT)
        build_wasm2c(cache, src)
    found = locate(cache, key)
    for k in ("WASI_SDK_PREFIX", "WASM_OPT", "WABT_ROOT", "WASM2C"):
        if k in found:
            print("%s=%s" % (k, found[k].replace("\\", "/")))
    return 0 if len(found) == 4 else 1


if __name__ == "__main__":
    sys.exit(main())
