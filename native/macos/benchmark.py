#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure a local Mac build with a saved match and separate settings."""

import argparse
import csv
import json
import math
import os
from pathlib import Path
import plistlib
import statistics
import subprocess
import time


def percentile(values, percent):
    ordered = sorted(values)
    index = (len(ordered) - 1) * percent / 100
    low = math.floor(index)
    high = math.ceil(index)
    return ordered[low] + (ordered[high] - ordered[low]) * (index - low)


def summarize(path, seconds=None, start_s=None, end_s=None):
    """Measure Core Animation callbacks. With VSync off these can exceed panel refresh."""
    with path.open(newline="") as source:
        rows = [{key: float(value) for key, value in row.items()}
                for row in csv.DictReader(source)]
    displayed = sorted((row for row in rows if row["display_s"] > 0),
                        key=lambda row: row["display_s"])
    if start_s is not None:
        displayed = [row for row in displayed if row["display_s"] >= start_s]
    if end_s is not None:
        displayed = [row for row in displayed if row["display_s"] <= end_s]
    if seconds is not None and displayed:
        begin = displayed[-1]["display_s"] - seconds
        displayed = [row for row in displayed if row["display_s"] >= begin]
    timestamps = sorted({row["display_s"] for row in displayed})
    if len(timestamps) < 2:
        raise RuntimeError("Need at least two display callbacks to measure frame times.")
    intervals = [(after - before) * 1000 for before, after in zip(timestamps, timestamps[1:])]
    queued = [(row["display_s"] - row["submit_s"]) * 1000 for row in displayed
                if 0 < row["submit_s"] <= row["display_s"]]
    busy = [row["gpu_busy_ms"] for row in displayed if row["gpu_busy_ms"] > 0]

    def distribution(values):
        if not values:
            return None
        return {"p50": percentile(values, 50), "p95": percentile(values, 95),
                "p99": percentile(values, 99), "max": max(values)}

    return {
        "presentation_callbacks": len(timestamps),
        "seconds": timestamps[-1] - timestamps[0],
        "presentation_fps": (len(timestamps) - 1) / (timestamps[-1] - timestamps[0]),
        "presentation_interval_ms": distribution(intervals),
        "submit_to_presentation_ms": distribution(queued),
        "gpu_busy_ms": distribution(busy),
        "intervals_over_12_5_ms": sum(value > 12.5 for value in intervals),
        "intervals_over_20_ms": sum(value > 20 for value in intervals),
        "same_presentation_timestamp": len(displayed) - len(timestamps),
        "frames_without_display_callback": sum(
            row["display_s"] <= 0 and row["submit_s"] > 0
            and (start_s is None or row["submit_s"] >= start_s)
            and (end_s is None or row["submit_s"] <= end_s)
            for row in rows),
    }


def status(directory):
    try:
        return dict(line.split("=", 1) for line in (directory / "status.txt").read_text().splitlines()
                    if "=" in line)
    except FileNotFoundError:
        return {}


def command(directory, process, action, **fields):
    name = str(time.time_ns())
    source = directory / (name + ".tmp")
    source.write_text("\n".join([f"command={action}", *(f"{key}={value}" for key, value in fields.items())]) + "\n")
    source.rename(directory / "commands" / (name + ".txt"))
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        current = status(directory)
        if (directory / "failed" / (name + ".txt")).exists():
            raise RuntimeError(current.get("last_error") or f"Runtime rejected {action}. Check runtime.log.")
        if current.get("last_command") == name + ".txt":
            if current.get("last_error"):
                raise RuntimeError(current["last_error"])
            return
        if process.poll() is not None:
            if action == "stop" and process.returncode == 0:
                return
            raise RuntimeError(f"Runtime exited with {process.returncode}. Check runtime.log.")
        time.sleep(0.02)
    raise RuntimeError(f"Runtime did not complete {action}. Check runtime.log.")


def measure(args):
    app = args.app.expanduser().resolve()
    state = args.state.expanduser().resolve()
    if not state.is_file():
        raise RuntimeError("--state must be a saved match from this game's native build.")
    output = args.output.expanduser().resolve()
    # Each run owns its settings, cache, saves, and log. Keep earlier runs for comparison.
    output.mkdir(parents=True, exist_ok=False)
    user = output / "user"
    config = user / "Config"
    config.mkdir(parents=True)
    (user / "config.ini").write_text("resolution=640x528\nshow_fps_in_title=true\nfullscreen=false\n")
    with (config / "MeleeFrontend.plist").open("wb") as target:
        plistlib.dump({"scale": args.scale, "vsync": args.vsync, "render_fps": args.fps,
                        "low_latency": not args.original_queues}, target)
    automation = output / "automation"
    (automation / "commands").mkdir(parents=True)
    frame_log = output / "frames.csv"
    resources = app / "Contents/Resources"
    runtime = app / "Contents/MacOS/MeleeRuntime"
    env = os.environ.copy()
    env.pop("MELEE_FRONTEND", None)
    env.update({"MELEE_APP_BUNDLE": "1", "MELEE_STRICT_NATIVE": "1",
                "MODERNGEKKO_STATICRECOMP": "1", "MELEE_RENDER_FPS": str(args.fps),
                "MELEE_LOW_LATENCY": "0" if args.original_queues else "1",
                "MELEE_METAL_LOW_LATENCY": "0" if args.original_queues else "1",
                "MELEE_METAL_FRAME_LOG": str(frame_log)})
    samples = []
    with (output / "runtime.log").open("w") as log:
        process = subprocess.Popen([
            str(runtime), "--game", str(resources / "Game"), "--module",
            str(resources / "Module/gGALE01_recomp.dylib"), "--user-dir", str(user),
            "--graphics", "Metal", "--audio", "Cubeb", "--no-mods",
            "--automation-dir", str(automation), "--load-state", str(state),
        ], env=env, cwd=resources, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 120
            while status(automation).get("state") != "running":
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("The match did not start. Check runtime.log.")
                time.sleep(0.1)
            print("Match started. Warming shaders for 5 seconds.", flush=True)
            time.sleep(5)
            command(automation, process, "load_state", path=state)
            scene = output / "scene.bin"
            command(automation, process, "read_memory", address="0x80479d30", size=4, path=scene)
            routing = scene.read_bytes()
            if len(routing) != 4 or routing[0] != 2 or routing[3] != 2:
                raise RuntimeError("The save state must contain an active Versus match, after the countdown.")

            def simulation_sample(name):
                path = output / name
                before = time.monotonic()
                command(automation, process, "read_memory", address="0x80479d58", size=4, path=path)
                after = time.monotonic()
                return int.from_bytes(path.read_bytes(), "big"), (before + after) / 2

            first_counter, first_time = simulation_sample("simulation-start.bin")
            begin = time.monotonic()
            while time.monotonic() - begin < args.seconds:
                if process.poll() is not None:
                    raise RuntimeError("The runtime exited during measurement. Check runtime.log.")
                samples.append(status(automation))
                time.sleep(0.1)
            end = time.monotonic()
            last_counter, last_time = simulation_sample("simulation-end.bin")
            command(automation, process, "screenshot", path=output / "match.png")
            screenshot_deadline = time.monotonic() + 10
            while not (output / "match.png").is_file():
                if process.poll() is not None or time.monotonic() > screenshot_deadline:
                    raise RuntimeError("The runtime did not write the match screenshot.")
                time.sleep(0.05)
            command(automation, process, "stop")
            process.wait(timeout=30)
            if process.returncode:
                raise RuntimeError(f"Runtime exited with {process.returncode}. Check runtime.log.")
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    if not frame_log.exists():
        raise RuntimeError("This build did not write Metal frame metrics. Rebuild with the fluidity patches.")
    # Python's Mac monotonic clock and Metal's CACurrentMediaTime use mach_absolute_time.
    # Exclude screenshot and shutdown work from the measured interval.
    result = summarize(frame_log, start_s=begin, end_s=end)
    result["settings"] = {"fps": args.fps, "scale": args.scale, "vsync": args.vsync,
                            "original_queues": args.original_queues,
                            "metal_drawables_override": env.get("MELEE_METAL_DRAWABLES")}
    result["runtime_fps_median"] = statistics.median(float(row["fps"]) for row in samples)
    result["simulation_hz_estimate"] = (last_counter - first_counter) / (last_time - first_time)
    (output / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--state", type=Path, required=True, help="Local native save state in an active match")
    parser.add_argument("--output", type=Path, required=True, help="New directory for this run")
    parser.add_argument("--fps", type=int, choices=(60, 120), default=120)
    parser.add_argument("--scale", type=int, choices=range(9), default=4)
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--vsync", action="store_true")
    parser.add_argument("--original-queues", action="store_true", help="Compare the original input and Metal queues")
    args = parser.parse_args()
    if not math.isfinite(args.seconds) or not 2 <= args.seconds <= 300:
        parser.error("--seconds must be between 2 and 300.")
    try:
        measure(args)
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    main()
