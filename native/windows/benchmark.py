#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure reported Windows runtime FPS from a local native save state.

Theo's fully automated slop experiment. No support, maintenance, or human
review is promised. Outputs include local game imagery and must stay in build/.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import time
import uuid
import shutil


ROOT = Path(__file__).resolve().parents[2]
DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"
STATUS_FIELDS = {"state", "booted", "fps", "speed", "frame_count", "present_count", "last_error"}


def status(directory, attempts=100):
    """Retry Windows sharing violations and incomplete fallback-copy snapshots."""
    for attempt in range(attempts):
        try:
            data = dict(line.split("=", 1) for line in
                        (directory / "status.txt").read_text(encoding="utf-8").splitlines()
                        if "=" in line)
            if STATUS_FIELDS <= data.keys():
                if all(math.isfinite(float(data[key])) and float(data[key]) >= 0
                       for key in ("fps", "speed")):
                    int(data["frame_count"])
                    int(data["present_count"])
                    return data
        except (OSError, UnicodeError, ValueError):
            pass
        if attempt + 1 < attempts:
            time.sleep(0.01)
    return {}


def command(directory, process, action, timeout=30, **fields):
    name = uuid.uuid4().hex + ".txt"
    source = directory / (name + ".tmp")
    values = {"command": action, **fields}
    if any("\n" in str(value) or "\r" in str(value) for value in values.values()):
        raise RuntimeError("Automation command values cannot contain line breaks.")
    source.write_text("".join(f"{key}={value}\n" for key, value in values.items()), encoding="utf-8")
    source.rename(directory / "commands" / name)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if (directory / "failed" / name).exists():
            raise RuntimeError(status(directory).get("last_error") or f"Runtime rejected {action}.")
        if (directory / "processed" / name).exists():
            return
        if process.poll() is not None:
            if action == "stop" and process.returncode == 0:
                return
            raise RuntimeError(f"Runtime exited with {process.returncode} during {action}.")
        time.sleep(0.02)
    raise RuntimeError(f"Runtime did not complete {action}. Check runtime.log.")


def launch_paths(root, module=None):
    runtime = root / "build/native-windows/recomp"
    runner = runtime / "build/runtime/moderngekko-run.exe"
    manifest = runtime / "build/launch-manifest.json"
    if manifest.is_file():
        data = json.loads(manifest.read_text(encoding="utf-8"))
        if data.get("version") != 1:
            raise RuntimeError("Unsupported launch manifest version.")
        selected = []
        for key in ("runtime", "module"):
            value = data.get(key)
            if not isinstance(value, str) or not value:
                raise RuntimeError(f"Launch manifest is missing {key}.")
            path = (runtime / value).resolve()
            if not path.is_relative_to(runtime.resolve()):
                raise RuntimeError(f"Launch manifest {key} leaves the runtime directory.")
            selected.append(path)
        runner, default_module = selected
    else:
        default_module = runtime / "build/game-clang/gGALE01_recomp.dll"
    module = module.expanduser().resolve() if module else default_module
    disc = runtime / "private/GALE01r2"
    if not (disc / "sys/main.dol").is_file():
        disc = root / "build/disc"
    return runner, module, disc


def prepare(args):
    root = ROOT.resolve()
    output = args.output.expanduser().resolve()
    if output == root / "build" or not output.is_relative_to(root / "build"):
        raise RuntimeError("--output must be a fresh directory inside this checkout's ignored build/.")
    if output.exists():
        raise RuntimeError("--output already exists; use a fresh directory to preserve earlier runs.")
    runner, module, disc = launch_paths(root, args.module)
    state = args.state.expanduser().resolve()
    base_user = root / "build/native-windows/user"
    textures = root / "build/native-windows/textures"
    for path in (runner, module, state, disc / "sys/main.dol", base_user / "config.ini"):
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f"Missing or empty benchmark input: {path}")
    for path in (base_user / "Config", disc / "files", runner.parent / "Sys", textures / "GALE01"):
        if not path.is_dir() or next(path.iterdir(), None) is None:
            raise RuntimeError(f"Missing or empty benchmark resource directory: {path}")
    with (disc / "sys/main.dol").open("rb") as source:
        if hashlib.file_digest(source, "sha1").hexdigest() != DOL_SHA1:
            raise RuntimeError("The executable must match Melee USA v1.02.")
    output.mkdir(parents=True, exist_ok=False)
    user = output / "user"
    shutil.copytree(base_user / "Config", user / "Config")
    shutil.copy2(base_user / "config.ini", user / "config.ini")
    texture_link = user / "Load/Textures/GALE01"
    texture_link.parent.mkdir(parents=True)
    # Values travel through the child environment, never through shell code.
    env = os.environ.copy()
    env.update(MELEE_BENCHMARK_LINK=str(texture_link), MELEE_BENCHMARK_TEXTURES=str(textures / "GALE01"))
    subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
                    "New-Item -ItemType Junction -Path $env:MELEE_BENCHMARK_LINK "
                    "-Target $env:MELEE_BENCHMARK_TEXTURES -ErrorAction Stop | Out-Null"],
                   env=env, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    automation = output / "automation"
    for name in ("commands", "processed", "failed"):
        (automation / name).mkdir(parents=True, exist_ok=True)
    return output, runner, module, disc, state, user, textures, automation


def stop(process, automation):
    """Try the runtime's own shutdown before terminating an unresponsive child."""
    if process.poll() is not None:
        return False
    try:
        command(automation, process, "stop", timeout=10)
        process.wait(timeout=20)
        return False
    except (OSError, RuntimeError, subprocess.TimeoutExpired):
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)
        return True


def measure(args):
    output, runner, module, disc, state, user, textures, automation = prepare(args)
    env = os.environ.copy()
    env.update(MELEE_STRICT_NATIVE="1", MODERNGEKKO_STATICRECOMP="1", MELEE_APP_BUNDLE="1",
               MELEE_RENDER_FPS="60", MELEE_TEXTURE_PACK=str(textures), MELEE_PAD_WAIT="1")
    if args.profile:
        env["STATICRECOMP_DISPATCH_SAMPLES"] = "1"
    else:
        env.pop("STATICRECOMP_DISPATCH_SAMPLES", None)
    samples = []
    result = {"module": str(module), "state": str(state), "requested_seconds": args.seconds,
              "dispatch_sampling": args.profile,
              "warmup_seconds": args.warmup, "samples": samples,
              "metric": "Runtime-reported FPS and emulation speed; not physical display timing."}
    process = None
    error = None
    with (output / "runtime.log").open("w", encoding="utf-8") as log:
        try:
            start = time.monotonic()
            process = subprocess.Popen([
                str(runner), "--game", str(disc), "--module", str(module), "--user-dir", str(user),
                "--graphics", "Vulkan", "--audio", "Cubeb", "--no-mods",
                "--automation-dir", str(automation), "--load-state", str(state),
            ], cwd=runner.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
            deadline = start + 120
            while True:
                current = status(automation)
                if current.get("state") == "running" and float(current.get("fps", 0)) > 0:
                    break
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("No rendered frames. Check runtime.log.")
                time.sleep(0.1)
            result["startup_to_fps_seconds"] = time.monotonic() - start
            time.sleep(args.warmup)
            begin = time.monotonic()
            last_frame = None
            last_progress = begin
            while time.monotonic() - begin < args.seconds:
                if process.poll() is not None:
                    raise RuntimeError("Runtime exited during measurement. Check runtime.log.")
                current = status(automation)
                if not current or current["state"] != "running":
                    raise RuntimeError("Runtime status unavailable or game stopped during measurement.")
                if current["frame_count"] != last_frame:
                    last_frame, last_progress = current["frame_count"], time.monotonic()
                elif time.monotonic() - last_progress > 5:
                    raise RuntimeError("Runtime frame count stopped advancing during measurement.")
                samples.append({"elapsed_seconds": time.monotonic() - begin, **current})
                time.sleep(0.1)
            result["measured_seconds"] = time.monotonic() - begin
            command(automation, process, "screenshot", path=output / "scene.png")
            deadline = time.monotonic() + 10
            while True:
                try:
                    png = (output / "scene.png").read_bytes()
                    if png.startswith(b"\x89PNG\r\n\x1a\n") and png.endswith(b"\x00\x00\x00\x00IEND\xaeB`\x82"):
                        break
                except OSError:
                    pass
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("Runtime did not finish writing scene.png.")
                time.sleep(0.05)
        except (OSError, RuntimeError, subprocess.SubprocessError, KeyboardInterrupt) as exc:
            error = str(exc) or "Benchmark interrupted."
        finally:
            if process is not None:
                result["forced_stop"] = stop(process, automation)
                result["exit_code"] = process.returncode
                if process.returncode != 0 and error is None:
                    error = f"Runtime exited with {process.returncode}."
    if samples:
        fps = [float(sample["fps"]) for sample in samples]
        result.update(fps_median=statistics.median(fps), fps_min=min(fps),
                      speed_median=statistics.median(float(sample["speed"]) for sample in samples))
    if error:
        result["error"] = error
    (output / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "samples"}, indent=2))
    if error:
        raise RuntimeError(error)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state", type=Path, required=True, help="A saved scene from this native build")
    parser.add_argument("--output", type=Path, required=True, help="Fresh output directory under build/")
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--warmup", type=float, default=5)
    parser.add_argument("--module", type=Path, help="Override the built module selected by the launch manifest")
    parser.add_argument("--profile", action="store_true", help="Include dispatch sampling for bottleneck analysis")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This benchmark requires Windows.")
    if not math.isfinite(args.seconds) or args.seconds <= 0 or not math.isfinite(args.warmup) or args.warmup < 0:
        parser.error("--seconds must be positive and --warmup must be nonnegative.")
    print("Theo's fully automated slop experiment. No support, maintenance, or human review is promised.")
    try:
        measure(args)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    main()
