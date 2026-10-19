#!/usr/bin/env python3
"""alcache.py -- remember what the generators read out of aletheia.

closure.py, gametree.py, bootlink.py, ovlink.py and gamespell.py each read
tens of thousands of aletheia files (every per-packet manifest, every
src/game and src/matched header) in their own process, several times per
configure.  On Windows each open costs milliseconds (the file system
filter drivers, an antivirus above all), so one pass over the checkout
takes minutes and a configure repeats it a dozen times.

memo(root, name, subdirs, build) runs build() once and keeps its result in
a pickle under the user's cache directory, keyed by the checkout and the
name.  The result is used again while the files under `subdirs` are the
same: same paths, sizes and modification times.  Listing a directory gives
those without opening a file, so checking the key costs a fraction of a
second.  EIDOS_ALCACHE=0 turns the cache off; EIDOS_ALCACHE_DIR moves it.
"""

from __future__ import annotations

import hashlib
import os
import pickle

_FP = {}
VERSION = 1


def _cache_dir():
    d = os.environ.get("EIDOS_ALCACHE_DIR")
    if not d:
        base = os.environ.get("LOCALAPPDATA") or os.path.join(
            os.path.expanduser("~"), ".cache")
        d = os.path.join(base, "eidos", "alcache")
    return d


def _walk(top, out, rel=""):
    try:
        it = os.scandir(top)
    except OSError:
        return
    with it:
        for e in it:
            r = rel + "/" + e.name if rel else e.name
            try:
                if e.is_dir(follow_symlinks=False):
                    _walk(e.path, out, r)
                else:
                    st = e.stat(follow_symlinks=False)
                    out.append("%s\0%d\0%d" % (r, st.st_size, st.st_mtime_ns))
            except OSError:
                continue


def fingerprint(root, subdirs):
    """A digest of the paths, sizes and times of every file under the
    subdirectories of `root`."""
    key = (os.path.abspath(root), tuple(subdirs))
    fp = _FP.get(key)
    if fp is None:
        h = hashlib.sha1()
        for sub in subdirs:
            lines = []
            _walk(os.path.join(root, sub), lines)
            lines.sort()
            h.update(("\1" + sub + "\1").encode("utf-8"))
            h.update("\n".join(lines).encode("utf-8", "surrogatepass"))
        fp = _FP[key] = h.hexdigest()
    return fp


def memo(root, name, subdirs, build):
    """build()'s result, from the cache while the files under `subdirs` of
    `root` are unchanged."""
    if os.environ.get("EIDOS_ALCACHE", "1") == "0":
        return build()
    fp = fingerprint(root, subdirs)
    ident = hashlib.sha1(("%d\0%s\0%s" % (VERSION, os.path.abspath(root).lower(), name))
                         .encode("utf-8")).hexdigest()
    path = os.path.join(_cache_dir(), ident + ".pickle")
    try:
        with open(path, "rb") as fh:
            saved_fp, value = pickle.load(fh)
        if saved_fp == fp:
            return value
    except (OSError, EOFError, pickle.UnpicklingError, ValueError, TypeError):
        pass
    value = build()
    try:
        os.makedirs(_cache_dir(), exist_ok=True)
        tmp = path + ".%d.tmp" % os.getpid()
        with open(tmp, "wb") as fh:
            pickle.dump((fp, value), fh, protocol=pickle.HIGHEST_PROTOCOL)
        os.replace(tmp, path)
    except OSError:
        pass
    return value


class ParseCache:
    """Parsed source files, kept across runs.  The generators parse the same
    thousand C files every configure (armdata.SourceFile, ovlink.Scan,
    closure.SourceScan); a parse is a pure function of the file's text and
    the parser's own code, so the result is looked up by a hash of both.
    One pickle per cache name and running script; written at exit with the
    entries this run used, so it does not grow without bound."""

    def __init__(self, name, parser_files):
        import __main__
        import atexit
        h = hashlib.sha1(str(VERSION).encode())
        for p in parser_files:
            try:
                with open(p, "rb") as fh:
                    h.update(fh.read())
            except OSError:
                h.update(p.encode())
        self.salt = h.hexdigest()
        script = os.path.basename(getattr(__main__, "__file__", "") or "interactive")
        self.path = os.path.join(_cache_dir(), "parse-%s-%s.pickle" % (name, script))
        self.old, self.new = {}, {}
        self.enabled = os.environ.get("EIDOS_ALCACHE", "1") != "0"
        if self.enabled:
            try:
                with open(self.path, "rb") as fh:
                    salt, table = pickle.load(fh)
                if salt == self.salt:
                    self.old = table
            except Exception:       # a stale or partial pickle: start over
                self.old = {}
            atexit.register(self.save)

    def get(self, key_parts, text, build):
        if not self.enabled:
            return build()
        k = hashlib.sha1(("\0".join(str(p) for p in key_parts) + "\0" + text)
                         .encode("utf-8", "surrogatepass")).hexdigest()
        # Entries are kept pickled, taken right after the parse: a caller
        # that changes its object afterwards changes its own copy only.
        b = self.new.get(k)
        if b is None:
            b = self.old.get(k)
            if b is None:
                v = build()
                self.new[k] = pickle.dumps(v, protocol=pickle.HIGHEST_PROTOCOL)
                return v
            self.new[k] = b
        return pickle.loads(b)

    def save(self):
        if not self.new or self.new.keys() == self.old.keys():
            return
        try:
            os.makedirs(_cache_dir(), exist_ok=True)
            tmp = self.path + ".%d.tmp" % os.getpid()
            with open(tmp, "wb") as fh:
                pickle.dump((self.salt, self.new), fh, protocol=pickle.HIGHEST_PROTOCOL)
            os.replace(tmp, self.path)
        except Exception:
            pass
