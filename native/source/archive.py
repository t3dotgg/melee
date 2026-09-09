#!/usr/bin/env python3
"""Archive native source objects and record a forced-load link attempt.

Run ``compile.py`` first.  The link uses ``-force_load`` so the linker checks
every object in the archive, including objects that an empty probe executable
would otherwise discard.
"""

import argparse
import json
from pathlib import Path
import platform
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def run(command: list[str], *, cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=ROOT / "build/native-source",
        help="Directory containing compile-report.json and objects/",
    )
    parser.add_argument(
        "--archive",
        type=Path,
        help="Static library path (default: BUILD_DIR/libmelee_native.a)",
    )
    parser.add_argument(
        "--report",
        type=Path,
        help="Link report path (default: BUILD_DIR/link-report.json)",
    )
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("Run this ARM64 link check on an Apple Silicon Mac")

    build_dir = args.build_dir.resolve()
    archive = (args.archive or build_dir / "libmelee_native.a").resolve()
    report_path = (args.report or build_dir / "link-report.json").resolve()
    compile_report_path = build_dir / "compile-report.json"
    if not compile_report_path.is_file():
        parser.error(f"Missing compile report: {compile_report_path}")
    compile_report = json.loads(compile_report_path.read_text())
    sources = compile_report.get("sources", [])
    objects = [build_dir / entry["object"] for entry in sources if entry.get("success")]
    missing = [str(path) for path in objects if not path.is_file()]
    failed_sources = [entry.get("source", "") for entry in sources if not entry.get("success")]

    archive.parent.mkdir(parents=True, exist_ok=True)
    if missing:
        report = {
            "architecture": "arm64",
            "archive": str(archive),
            "compiled": len(objects),
            "total": len(sources),
            "failed_sources": failed_sources,
            "missing_objects": missing,
            "archived": False,
            "linked": False,
            "error": "One or more compiled objects are missing",
        }
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Cannot archive: {len(missing)} object files are missing")
        return 1
    if not objects:
        parser.error("Compile report contains no successful objects")

    archive_result = run(["ar", "-rcs", str(archive), *map(str, objects)], cwd=ROOT)
    if archive_result.returncode:
        report = {
            "architecture": "arm64",
            "archive": str(archive),
            "compiled": len(objects),
            "total": len(sources),
            "failed_sources": failed_sources,
            "archived": False,
            "linked": False,
            "archive_command": ["ar", "-rcs", str(archive), *map(str, objects)],
            "archive_stderr": archive_result.stderr,
        }
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        print(archive_result.stderr, end="")
        return archive_result.returncode

    with tempfile.TemporaryDirectory(prefix="melee-native-link-", dir=build_dir) as temp:
        temp_dir = Path(temp)
        probe_source = temp_dir / "main.c"
        probe_object = temp_dir / "main.o"
        probe_binary = temp_dir / "link-probe"
        probe_source.write_text("int main(void) { return 0; }\n")
        compile_probe = run(
            ["clang", "-arch", "arm64", "-std=gnu11", "-c", str(probe_source), "-o", str(probe_object)],
            cwd=ROOT,
        )
        link_command = [
            "clang",
            "-arch",
            "arm64",
            str(probe_object),
            "-Wl,-force_load," + str(archive),
            "-o",
            str(probe_binary),
        ]
        link_result = (
            run(link_command, cwd=ROOT)
            if compile_probe.returncode == 0
            else compile_probe
        )
    linker_output = (link_result.stdout or "") + (link_result.stderr or "")
    # ld64 prints one quoted symbol per line followed by `, referenced from:`.
    # Keep the names separate from the verbose reference list so callers can
    # count and group missing platform services.
    undefined_symbols = sorted(
        set(re.findall(r'^\s+"([^"]+)", referenced from:', linker_output, re.MULTILINE))
        | set(re.findall(r"symbol not found: ([^\n]+)", linker_output))
    )
    report = {
        "architecture": "arm64",
        "archive": str(archive),
        "compiled": len(objects),
        "total": len(sources),
        "failed_sources": failed_sources,
        "archived": archive_result.returncode == 0,
        "linked": link_result.returncode == 0,
        "archive_command": ["ar", "-rcs", str(archive), *map(str, objects)],
        "link_command": link_command,
        "link_returncode": link_result.returncode,
        "undefined_symbol_count": len(undefined_symbols),
        "link_output": linker_output,
        "undefined_symbols": undefined_symbols,
    }
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Archived {len(objects)}/{len(sources)} objects: {archive}")
    print(f"Link probe: {'success' if link_result.returncode == 0 else 'failed'}")
    print(f"Link report: {report_path}")
    return 0 if link_result.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
