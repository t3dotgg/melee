#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the matching game, compile it for ARM64, and package a local Mac app."""

import argparse
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
RUNTIME_URL = "https://github.com/McDandle/melee-macos-recomp.git"
RUNTIME_REV = "39e30dec9fa7d90fba960ca9189b573a7938e3df"
DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"


def run(*args, cwd=ROOT, env=None):
    print("+ " + " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=True)


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha1").hexdigest()


def source_digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def check_source_image(runtime, iso):
    """Require the requested image to match an existing import before reusing it."""
    checksum = source_digest(iso)
    stamp = runtime / "private/source-image.sha256"
    if stamp.exists():
        if stamp.read_text().strip() != checksum:
            raise RuntimeError("This runtime cache uses a different disc image. Use a new --runtime-dir.")
        return checksum
    cached_image = runtime / "private/GALE01r2.iso"
    if cached_image.exists():
        # Adopt an older cache only after comparing the complete normalized image.
        # prepare_disc also supports a CISO with an .iso extension.
        with tempfile.TemporaryDirectory(dir=cached_image.parent, prefix="source-check-") as directory:
            expanded = Path(directory) / "disc.iso"
            run(sys.executable, runtime / "scripts/prepare_disc.py", iso, expanded, cwd=runtime)
            if source_digest(expanded) != source_digest(cached_image):
                raise RuntimeError("The imported disc differs from --iso. Use a new --runtime-dir.")
    return checksum


def patch_state(checkout, patch, reverse=False):
    args = ["git", "apply", "--check"]
    if reverse:
        args.append("--reverse")
    return subprocess.run(
        [*args, str(patch)], cwd=checkout, capture_output=True
    ).returncode == 0


def apply_patch(checkout, patch):
    if patch_state(checkout, patch, reverse=True):
        return
    if not patch_state(checkout, patch):
        raise RuntimeError(f"Local changes conflict with {patch.name} in {checkout}")
    run("git", "apply", patch, cwd=checkout)


def remove_patch(checkout, patch):
    if not checkout.is_dir() or not patch.exists():
        return
    if patch_state(checkout, patch, reverse=True):
        run("git", "apply", "--reverse", patch, cwd=checkout)
    # An interrupted first build can have none of the preceding patches.
    # Check forward application later, after their required context is restored.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True, help="Your Melee USA v1.02 disc image")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 8, 12))
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / "build/native/recomp")
    parser.add_argument("--output", type=Path, default=ROOT / "build/native/Melee.app")
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("This build requires an Apple Silicon Mac.")
    if sys.version_info < (3, 11):
        parser.error("Use Python 3.11 or later.")
    if args.jobs < 1:
        parser.error("--jobs must be positive.")
    iso = args.iso.expanduser().resolve()
    if not iso.is_file():
        parser.error(f"Disc image does not exist: {iso}")
    for tool in ("git", "clang", "cmake", "ninja", "pkg-config"):
        if shutil.which(tool) is None:
            parser.error(f"Missing {tool}. See docs/native-macos.md for build dependencies.")

    run(sys.executable, ROOT / "configure.py", "--map", "--no-always-apply")
    run(sys.executable, ROOT / "tools/verify.py")
    dol = ROOT / "build/GALE01/main.dol"
    if digest(dol) != DOL_SHA1:
        raise RuntimeError("This runtime currently requires the matching US v1.02 executable.")

    runtime = args.runtime_dir.expanduser().resolve()
    if not runtime.exists():
        runtime.parent.mkdir(parents=True, exist_ok=True)
        run("git", "clone", RUNTIME_URL, runtime)
        run("git", "checkout", "--detach", RUNTIME_REV, cwd=runtime)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=runtime, text=True).strip()
    if revision != RUNTIME_REV:
        raise RuntimeError(f"Runtime must be pinned to {RUNTIME_REV}, found {revision}.")
    image_checksum = check_source_image(runtime, iso)
    dolphin = runtime / "upstream/ModernGekko-Template/lib/ModernGekko/vendor/dolphin"
    patches = [
        (runtime, HERE / "patches/apple-input.patch"),
        (runtime, HERE / "patches/app-bundle.patch"),
        (runtime, HERE / "patches/fast-load.patch"),
        (runtime, HERE / "patches/fluidity-settings.patch"),
        (runtime / "upstream/ModernGekko-Template/lib/ModernGekko", HERE / "patches/runtime-sdl.patch"),
        (runtime / "upstream/ModernGekko-Template/lib/ModernGekko", HERE / "patches/runtime-cache.patch"),
        (runtime / "upstream/ModernGekko-Template/lib/ModernGekko", HERE / "patches/benchmark-automation.patch"),
        (dolphin, HERE / "patches/strict-cpu.patch"),
        (dolphin, HERE / "patches/native-boot.patch"),
        (dolphin, HERE / "patches/low-latency-input.patch"),
        (dolphin, HERE / "patches/native-timebase.patch"),
        (dolphin, HERE / "patches/native-idle.patch"),
        (dolphin, HERE / "patches/frame-timing.patch"),
        (dolphin, HERE / "patches/fluid-render.patch"),
        (dolphin, HERE / "patches/benchmark-state.patch"),
        (dolphin, HERE / "patches/game-refresh.patch"),
    ]
    # Restore only our known patches before the upstream script checks its patches.
    # This keeps repeat builds safe when patch hunks touch the same source file.
    for checkout, patch in reversed(patches):
        remove_patch(checkout, patch)
    apply_patch(runtime, HERE / "patches/source-dol.patch")
    env = os.environ.copy()
    env["JOBS"] = str(args.jobs)
    env["MELEE_SOURCE_DOL"] = str(dol)
    env["MELEE_BOOT_BUILDER"] = str(HERE / "recompile_boot.py")
    env["PATH"] = str(Path(sys.executable).parent) + os.pathsep + env["PATH"]
    run("bash", runtime / "scripts/build_macos.sh", iso, cwd=runtime, env=env)
    (runtime / "private/source-image.sha256").write_text(image_checksum + "\n")
    if digest(runtime / "private/GALE01r2/sys/main.dol") != digest(dol):
        raise RuntimeError("The native build used a different executable.")
    for checkout, patch in patches:
        apply_patch(checkout, patch)
    frontend = dolphin / "Source/Core/DolphinNoGUI"
    shutil.copy2(runtime / "macos/MeleeFrontend.inc", frontend)
    for name in ("MeleeControllerConfig.h", "MeleeInputTest.inc"):
        shutil.copy2(HERE / "input" / name, frontend)
    shutil.copy2(HERE / "render/MeleeMetalFrameLog.h", dolphin / "Source/Core/VideoBackends/Metal")
    shutil.copy2(HERE / "render/MeleeRenderConfig.h", dolphin / "Source/Core/VideoCommon")
    # Reapply after generation. The verified DOL and extracted disc remain unchanged.
    run(sys.executable, HERE / "high_refresh.py", "--generated", runtime / "private/recompiled/generated")
    run("cmake", "--build", runtime / "build/game", "-j", args.jobs)
    run("cmake", "--build", runtime / "build/runtime", "--target", "moderngekko-run", "-j", args.jobs)
    run(sys.executable, HERE / "package.py", "--runtime-dir", runtime, "--output", args.output, "--replace")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
