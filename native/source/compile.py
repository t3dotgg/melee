#!/usr/bin/env python3
"""Compile every game and engine C file for the native host and report failures."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import platform
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/native-source")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--source", action="append", help="Limit to a source path or directory")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("Run this ARM64 compile check on an Apple Silicon Mac")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sources = set()
    for name in args.source or ["src/melee", "src/sysdolphin"]:
        path = ROOT / name
        if path.is_file() and path.suffix == ".c":
            sources.add(path)
        elif path.is_dir():
            sources.update(path.rglob("*.c"))
        else:
            parser.error(f"No C source at {name}")
    flags = [
        "clang", "-arch", "arm64", "-std=gnu11", "-DMELEE_NATIVE", "-DTARGET_PC",
        "-Isrc", "-Iextern/dolphin/include", "-Inative/source", "-include", "math.h",
        "-fno-strict-aliasing", "-ffp-contract=off", "-fno-common",
        "-Werror=pointer-to-int-cast", "-Werror=int-to-pointer-cast",
        "-Werror=implicit-function-declaration", "-Werror=int-conversion",
        "-ferror-limit=0", "-fno-color-diagnostics",
    ]

    def compile_source(source):
        relative = source.relative_to(ROOT)
        obj = output / "objects" / relative.with_suffix(".o")
        log = output / "logs" / relative.with_suffix(".log")
        obj.parent.mkdir(parents=True, exist_ok=True)
        log.parent.mkdir(parents=True, exist_ok=True)
        obj.unlink(missing_ok=True)
        command = [*flags, "-c", str(relative), "-o", str(obj)]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        log.write_text(result.stdout + result.stderr)
        return {
            "source": str(relative), "success": result.returncode == 0,
            "object": str(obj.relative_to(output)), "log": str(log.relative_to(output)),
            "errors": [line for line in result.stderr.splitlines() if "error:" in line],
            "command": command,
        }

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(compile_source, sorted(sources)))
    passed = sum(result["success"] for result in results)
    report = {"architecture": "arm64", "compiled": passed, "total": len(results), "sources": results}
    (output / "compile-report.json").write_text(json.dumps(report, indent=2) + "\n")
    (output / "compile_commands.json").write_text(json.dumps([
        {"directory": str(ROOT), "file": result["source"], "arguments": result["command"]}
        for result in results
    ], indent=2) + "\n")
    print(f"Compiled {passed}/{len(results)} source files for ARM64")
    print(f"Report: {output / 'compile-report.json'}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
