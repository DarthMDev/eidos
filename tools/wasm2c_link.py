#!/usr/bin/env python3
"""Link existing WASI CMake objects with signature adapters, then validate.

Build the object targets first. This stage never overwrites CMake's objects
or edits Aletheia: adapted callers and generated adapters live under --out.
Every selected object is linked directly, because archive extraction can hide
wasm-ld's signature diagnostics. Unresolved mismatches are fatal.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / "lp64"))
import sigfix


def run(command, **kwargs):
    return subprocess.run([str(x) for x in command], check=True, **kwargs)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--wasi-sdk", required=True, type=Path)
    ap.add_argument("--wabt", required=True, type=Path)
    ap.add_argument("--target", required=True)
    ap.add_argument("--object-target", action="append", default=[])
    args = ap.parse_args()
    build, out = args.build.resolve(), args.out.resolve()
    sdk, wabt = args.wasi_sdk.resolve(), args.wabt.resolve()
    out.mkdir(parents=True, exist_ok=True)
    targets = {"eidos", args.target, *args.object_target}
    entries = json.loads((build / "compile_commands.json").read_text())
    entries = [e for e in entries if any(
        Path(e["output"]).is_relative_to(build / "CMakeFiles" / (t + ".dir"))
        for t in targets)]
    if not entries:
        raise SystemExit("no objects selected")
    # Compute archive-style reachability before linking objects directly.
    # Including unused members can turn a guarded weak game reference into
    # a required one from code the archive would never have extracted.
    selected = {e["output"] for e in entries
                if not Path(e["output"]).is_relative_to(build / "CMakeFiles/eidos.dir")}
    nm_all = subprocess.check_output([str(sdk / "bin/llvm-nm"),
                                     *[e["output"] for e in entries]], text=True)
    symbols, providers, current_obj = {}, {}, None
    for line in nm_all.splitlines():
        if line.endswith(":"):
            current_obj = line[:-1]
            symbols[current_obj] = []
            continue
        fields = line.split()
        if current_obj and len(fields) >= 2:
            kind, symbol = fields[-2:]
            symbols[current_obj].append((kind, symbol))
            if kind in ("T", "D", "B", "R", "W", "V"):
                old = providers.get(symbol)
                if old is None or kind not in ("W", "V"):
                    providers[symbol] = current_obj
    while True:
        needed = {providers[sym] for obj in selected for kind, sym in symbols[obj]
                  if kind == "U" and sym in providers}
        if needed <= selected:
            break
        selected.update(needed)
    entries = [e for e in entries if e["output"] in selected]
    commands = {str(Path(e["output"]).resolve()): e for e in entries}
    objects = list(commands)
    for obj in objects:
        if not Path(obj).exists():
            raise SystemExit("build object target first: " + obj)
    replacements, defines, adapters = {}, {}, {}
    module = out / (args.target + ".wasm")
    # wasm-ld rejects direct calls even to unresolved weak functions unless
    # explicitly allowed. Keep their function-pointer value null and their
    # unreachable direct calls as traps; never invent providers or imports.
    nm = subprocess.check_output([str(sdk / "bin/llvm-nm"), *objects], text=True)
    weak, required, provided = set(), set(), set()
    for line in nm.splitlines():
        fields = line.split()
        if len(fields) >= 2:
            kind, symbol = fields[-2:]
            if kind == "w": weak.add(symbol)
            elif kind == "U": required.add(symbol)
            elif kind in ("T", "W", "D", "B", "V"): provided.add(symbol)
    weak_file = out / "optional-weak.txt"
    weak_file.write_text("\n".join(sorted(weak - required - provided)) + "\n")
    mismatches_seen = set()
    for attempt in range(32):
        extra = []
        if adapters:
            source = out / "adapters.c"
            source.write_text("/* generated signature adapters */\n" +
                              "\n".join(adapters[k] for k in sorted(adapters)))
            obj = out / "adapters.o"
            run([sdk / "bin/clang", "-O2", "-c", source, "-o", obj])
            extra.append(str(obj))
        linked = [replacements.get(o, o) for o in objects] + extra
        command = [sdk / "bin/clang", *linked, "-lm",
                   "-Wl,--error-limit=0",
                   "-Wl,--export=__stack_pointer", "-Wl,-z,stack-size=8388608",
                   "-Wl,--initial-memory=134217728", "-Wl,--max-memory=134217728",
                   "-Wl,--allow-undefined-file=" + str(weak_file),
                   "-o", module]
        p = subprocess.run(list(map(str, command)), text=True, capture_output=True)
        log = p.stdout + p.stderr
        (out / ("link-%02d.log" % attempt)).write_text(log)
        current_mismatches = tuple(sorted(
            (sym, tuple(sorted((sigfix.sig_tag(s), o) for s, o in sides)))
            for sym, sides in sigfix.parse_log(log)
        ))
        if current_mismatches and current_mismatches in mismatches_seen:
            unresolved = sorted({sym for sym, _ in sigfix.parse_log(log)})
            raise SystemExit(
                f"signature adapter loop made no progress (repeated mismatches on pass {attempt + 1}): "
                f"unresolvable symbols: {', '.join(unresolved)}\n{log}"
            )
        mismatches_seen.add(current_mismatches)
        objmap = {o: o for o in objects}
        objmap.update({new: old for old, new in replacements.items()})
        # sigfix must inspect the current object, but plans are keyed by its
        # original compilation unit so subsequent passes retain all renames.
        current = {name: replacements.get(old, old) for name, old in objmap.items()}
        changes, new_adapters = sigfix.plan(log, str(sdk / "bin/llvm-nm"), current)
        reverse = {new: old for old, new in replacements.items()}
        if not changes:
            if p.returncode or sigfix.parse_log(log):
                unresolved = sorted({sym for sym, _ in sigfix.parse_log(log)})
                diag = (f"signature adapter loop halted: unresolvable symbols ({', '.join(unresolved)}):\n{log}"
                        if unresolved else (log or "link failed"))
                raise SystemExit(diag)
            break
        new_defines_count = 0
        for current_obj, renames in changes.items():
            original = reverse.get(current_obj, current_obj)
            existing = defines.get(original, {})
            for sym, adapter_name in renames.items():
                if existing.get(sym) != adapter_name:
                    new_defines_count += 1
        if new_defines_count == 0 and not new_adapters:
            unresolved = sorted({sym for sym, _ in sigfix.parse_log(log)})
            raise SystemExit(
                f"signature adapter loop made no progress on pass {attempt + 1}: "
                f"unresolvable symbols: {', '.join(unresolved)}\n{log}"
            )
        for current_obj, renames in changes.items():
            original = reverse.get(current_obj, current_obj)
            defines.setdefault(original, {}).update(renames)
            entry = commands[original]
            command = shlex.split(entry["command"])
            obj = out / ("caller-%04d.o" % objects.index(original))
            command[command.index("-o") + 1] = str(obj)
            command.extend("-D%s=%s" % pair for pair in defines[original].items())
            run(command, cwd=entry["directory"])
            replacements[original] = str(obj)
        adapters.update(new_adapters)
        print("signature pass %d: %d callers, %d adapters" %
              (attempt + 1, len(changes), len(adapters)), flush=True)
    else:
        raise SystemExit("signature adapter loop did not converge")
    run([wabt / "bin/wasm-validate", module])
    (out / "signature-plan.json").write_text(json.dumps(defines, indent=2) + "\n")
    run([wabt / "bin/wasm2c", module, "--module-name=game",
         "-o", out / "game.c"])
    print("validated:", module)


if __name__ == "__main__":
    main()
