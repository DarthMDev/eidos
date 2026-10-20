#!/usr/bin/env python3
"""
progress.py -- eidos's advancement record, the counterpart of aletheia's
progress report.

aletheia measures matched bytes against the ROM.  eidos is a port, so the
question is different: how much of the *matched corpus* does the host build
actually compile, and does it build.  This script reads only what the build
already wrote (no game bytes, no build of its own) and prints one block:

  configure      did the last configure pass, and the boot plan's numbers
  port surface   matched sources compiled for the host / matched sources
  build32        failed targets and errors, by class, from a build log
  tests          the last ctest summary, when one is on disk

With --wave and --note it appends one row to progress/WAVES.md, so the wave
log is written by the same run that measures it.

Usage:
  python tools/progress.py                       # measure and print
  python tools/progress.py --log <build log>     # with the build's errors
  python tools/progress.py --wave 1 --note "Class B cleared"
  python tools/progress.py --json progress/status.json

It writes nothing unless --wave or --json is given.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))


def read_lines(path):
    """Lines of a text file, whatever wrote it.

    PowerShell's redirection writes UTF-16LE on this host, so the BOM decides
    the decode; anything else is read as UTF-8.
    """
    if not os.path.exists(path):
        return []
    with open(path, "rb") as fh:
        raw = fh.read()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        text = raw.decode("utf-16", errors="replace")
    else:
        text = raw.decode("utf-8", errors="replace")
    return [ln.rstrip("\r\n") for ln in text.splitlines() if ln.strip()]


def boot_plan(build):
    """The boot plan's numbers, from what the configure wrote."""
    plan = os.path.join(build, "boot", "boot_plan.txt")
    report = os.path.join(build, "boot", "gen", "boot_link_report.txt")
    out = {}
    text = "\n".join(read_lines(plan))
    m = re.search(r"closure from (\d+) seeds: (\d+) (\S+) translation units", text)
    if m:
        out["seeds"] = int(m.group(1))
        out["boot_matched"] = int(m.group(2))
        # The tree the plan was made in: src/game merges the matched files,
        # so the unit counts of the two trees are not comparable and the
        # surface is measured against the tree the build used.
        out["tree"] = m.group(3)
    for key, pat in (("shim_replaced",
                      r"replaced by a port shim \(cmake/boot.cmake\): (\d+)"
                      r"|functions cut because the port defines them: (\d+)"),
                     ("in_library", r"already in the eidos library: (\d+)"),
                     ("asm_bodies", r"mwcc asm bodies(?: cut)?: (\d+)"),
                     ("compiled", r"compiled for the boot: (\d+)"),
                     ("clashes", r"NOT EXCLUDED, defines a shim's name")):
        if key == "clashes":
            out[key] = len(re.findall(pat, text))
            continue
        m = re.search(pat, text)
        if m:
            # The plan names a count one way in each tree; the first group
            # that matched carries it.
            out[key] = int(next(g for g in m.groups() if g is not None))
    for line in read_lines(report):
        m = re.match(r"implicit declarations supplied: (\d+) in (\d+) files", line)
        if m:
            out["implicit_decls"] = int(m.group(1))
            out["implicit_files"] = int(m.group(2))
        m = re.match(r"functions stubbed: (\d+) open, (\d+) asm-pending", line)
        if m:
            out["stubbed_open"] = int(m.group(1))
            out["stubbed_asm"] = int(m.group(2))
    return out


def matched_total(aletheia_root, tree="matched"):
    """How many translation units the tree holds.

    `tree` is "matched" (src/matched, one file per original source) or
    "game" (src/game, the merged units the port compiles by default).  The
    two trees merge the same functions into different numbers of files, so
    the host surface is only meaningful against the tree the build used.
    """
    import closure
    closure.ROOT = os.path.abspath(aletheia_root)
    # The plan names the tree "src/game" or "matched"; closure.py knows it
    # as "game" or "matched".
    closure.SOURCE_TREE = "game" if str(tree).endswith("game") else "matched"
    idx = closure.Index()
    return len(idx.sources)


def build_errors(log):
    """Failed targets, compile errors and link failures, from a ninja build log.

    The two are counted apart on purpose: a build that compiles everything and
    fails to link is a different state from one that fails to compile, and the
    undefined symbols name what is missing.
    """
    out = {"failed_targets": 0, "compile_errors": 0, "implicit": 0,
           "conflicting": 0, "other": 0, "link_failed_targets": 0,
           "undefined_refs": 0, "undefined_symbols": 0, "failed_list": []}
    if not log or not os.path.exists(log):
        return out
    symbols = set()
    for line in read_lines(log):
        if line.startswith("FAILED:"):
            out["failed_targets"] += 1
            tgt = line.split()[1] if len(line.split()) > 1 else line
            out["failed_list"].append(tgt)
        if "ld returned" in line:
            out["link_failed_targets"] += 1
            continue
        if "undefined reference to" in line:
            out["undefined_refs"] += 1
            m = re.search(r"undefined reference to [`'\"]?([A-Za-z_][A-Za-z0-9_]*)", line)
            if m:
                symbols.add(m.group(1))
            continue
        if "error:" in line:
            out["compile_errors"] += 1
            if "implicit declaration of function" in line:
                out["implicit"] += 1
            elif "conflicting types for" in line:
                out["conflicting"] += 1
            else:
                out["other"] += 1
    out["undefined_symbols"] = len(symbols)
    return out


def ctest_console_summary(path):
    """The summary of a ctest console log.

    The log ctest writes itself (Testing/Temporary/LastTest.log) holds one
    run and is overwritten by the next, so a run of one test leaves only that
    one test in it.  The console log keeps the whole run's summary line.
    """
    if not path or not os.path.exists(path):
        return None
    for line in read_lines(path):
        m = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", line)
        if m:
            total, failed = int(m.group(3)), int(m.group(2))
            return {"passed": total - failed, "failed": failed, "total": total}
    return None


def ctest_summary(build):
    """The last ctest summary on disk, if one is there.

    Two formats: the log ctest writes itself (Testing/Temporary/
    LastTest.log) says "Test Passed." or "Test Failed." under each test,
    and the console listing says "N/M Test #K: name ... Passed".
    """
    for name in ("Testing/Temporary/LastTest.log", "LastTest.log"):
        path = os.path.join(build, name)
        if not os.path.exists(path):
            continue
        passed = failed = 0
        for line in read_lines(path):
            if line.startswith("Test Passed."):
                passed += 1
            elif line.startswith("Test Failed."):
                failed += 1
            elif re.match(r"\d+/\d+ Test\s+#\d+:", line):
                if "Passed" in line:
                    passed += 1
                elif "Failed" in line:
                    failed += 1
        return {"passed": passed, "failed": failed}
    return None


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--aletheia-root", default=os.path.join(ROOT, "..", "bw_decomp"))
    ap.add_argument("--build", default=os.path.join(ROOT, "build32"))
    ap.add_argument("--log", default=None, help="a ninja build log to count errors from")
    ap.add_argument("--ctest-log", default=None,
                    help="a ctest console log to take the test summary from")
    ap.add_argument("--wave", type=int, default=None)
    ap.add_argument("--note", default="")
    ap.add_argument("--json", default=None)
    args = ap.parse_args(argv)

    plan = boot_plan(args.build)
    total = matched_total(args.aletheia_root, plan.get("tree", "matched"))
    errs = build_errors(args.log)
    tests = ctest_console_summary(args.ctest_log) or ctest_summary(args.build)
    compiled = plan.get("compiled", 0)
    surface = (100.0 * compiled / total) if total else 0.0

    status = {
        "wave": args.wave,
        "note": args.note,
        "configure": "pass" if plan.get("boot_matched") is not None else "unknown",
        "tree": plan.get("tree"),
        "matched_total": total,
        "boot_matched": plan.get("boot_matched"),
        "compiled_for_host": compiled,
        "host_surface_pct": round(surface, 2),
        "shim_replaced": plan.get("shim_replaced"),
        "clashes": plan.get("clashes"),
        "asm_bodies": plan.get("asm_bodies"),
        "implicit_decls": plan.get("implicit_decls"),
        "stubbed_open": plan.get("stubbed_open"),
        "stubbed_asm": plan.get("stubbed_asm"),
        "failed_targets": errs["failed_targets"],
        "compile_errors": errs["compile_errors"],
        "errors_implicit": errs["implicit"],
        "errors_conflicting": errs["conflicting"],
        "errors_other": errs["other"],
        "link_failed_targets": errs["link_failed_targets"],
        "undefined_refs": errs["undefined_refs"],
        "undefined_symbols": errs["undefined_symbols"],
        "tests": tests,
    }

    print("eidos advancement")
    print("  wave            : %s" % (args.wave if args.wave is not None else "-"))
    print("  configure       : %s" % status["configure"])
    print("  matched sources : %d" % total)
    print("  boot closure    : %s matched TUs, %s compiled, %s shim-replaced, %s asm bodies"
          % (plan.get("boot_matched"), compiled, plan.get("shim_replaced"), plan.get("asm_bodies")))
    print("  host surface    : %d of %d matched sources compile for the host (%.2f%%)"
          % (compiled, total, surface))
    print("  implicit decls  : %s in %s files" % (plan.get("implicit_decls"), plan.get("implicit_files")))
    print("  stubbed         : %s open, %s asm-pending" % (plan.get("stubbed_open"), plan.get("stubbed_asm")))
    print("  shim clashes    : %s" % plan.get("clashes"))
    if args.log:
        print("  build           : %d failed targets, %d compile errors (implicit %d, conflicting %d, other %d), %d link failures on %d undefined symbols"
              % (errs["failed_targets"], errs["compile_errors"], errs["implicit"],
                 errs["conflicting"], errs["other"], errs["link_failed_targets"],
                 errs["undefined_symbols"]))
    if tests:
        print("  tests           : %d passed, %d failed" % (tests["passed"], tests["failed"]))

    if args.json:
        path = args.json if os.path.isabs(args.json) else os.path.join(ROOT, args.json)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(status, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print("  wrote           : %s" % path)

    if args.wave is not None:
        path = os.path.join(ROOT, "progress", "WAVES.md")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if not os.path.exists(path):
            with open(path, "w", encoding="utf-8", newline="\n") as fh:
                fh.write("# eidos waves\n\n"
                         "One row per wave, written by `python tools/progress.py --wave N --note ...`.\n"
                         "The figures come from the configure's own plan and the build log.\n\n"
                         "| wave | date | host surface | compiled | errors | failed targets | note |\n"
                         "|---|---|---|---|---|---|---|\n")
        import datetime
        row = "| %d | %s | %.2f%% | %d | %d | %d | %s |\n" % (
            args.wave, datetime.date.today().isoformat(), surface, compiled,
            errs["compile_errors"], errs["failed_targets"], args.note.replace("|", "/"))
        with open(path, "a", encoding="utf-8", newline="\n") as fh:
            fh.write(row)
        print("  appended        : %s" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
