#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the experimental native Windows runtime and locally recompiled Melee DLL."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import uuid


ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
MAC = ROOT / "native/macos"
RUNTIME_URL = "https://github.com/McDandle/melee-macos-recomp.git"
RUNTIME_REV = "39e30dec9fa7d90fba960ca9189b573a7938e3df"
TEMPLATE_REV = "eedda2b02dde3aefc02796d859f0033b916aad03"
DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"


def run(*args, cwd=ROOT, env=None):
    print("+ " + subprocess.list2cmdline(list(map(str, args))), flush=True)
    command = list(map(str, args))
    # On Windows subprocess searches the parent's PATH, not env['PATH'].
    # Resolve tools in the imported VS environment before CreateProcess.
    if env is not None:
        command[0] = shutil.which(command[0], path=env.get("PATH")) or command[0]
    subprocess.run(command, cwd=cwd, env=env, check=True)


def digest(path, algorithm="sha1"):
    with path.open("rb") as source:
        return hashlib.file_digest(source, algorithm).hexdigest()


def require_local_build(path):
    """Generated game data and downloaded dependencies must remain ignored."""
    path = path.expanduser().resolve()
    if not path.is_relative_to((ROOT / "build").resolve()):
        raise RuntimeError("The runtime directory must be inside this checkout's ignored build directory.")
    return path


def check_source_image(runtime, iso, env):
    checksum = digest(iso, "sha256")
    private = runtime / "private"
    stamp = private / "source-image.sha256"
    if stamp.exists():
        if stamp.read_text().strip() != checksum:
            raise RuntimeError("The cached disc uses a different image. Choose a new --runtime-dir.")
        return checksum
    prepared = private / "GALE01r2.iso"
    if prepared.exists():
        # Adopt an interrupted or older cache only after checking the complete
        # normalized image. An executable hash alone does not identify assets.
        with tempfile.TemporaryDirectory(prefix="source-check-", dir=private) as directory:
            normalized = Path(directory) / "disc.iso"
            run(sys.executable, runtime / "scripts/prepare_disc.py", iso, normalized, cwd=runtime, env=env)
            if digest(normalized, "sha256") != digest(prepared, "sha256"):
                raise RuntimeError("The imported disc differs from --iso. Choose a new --runtime-dir.")
    return checksum


def validate_disc(disc):
    required = ("sys/main.dol", "sys/apploader.img", "sys/boot.bin", "sys/bi2.bin", "sys/fst.bin")
    if not (disc / "files").is_dir() or any(not (disc / name).is_file() for name in required):
        raise RuntimeError("The extracted disc is incomplete; extract its system data and files again.")
    if digest(disc / "sys/main.dol") != DOL_SHA1:
        raise RuntimeError("The extracted executable must match Melee USA v1.02.")


def prepare_disc(runtime, recompiler, iso, disc_dir, env):
    if disc_dir:
        disc = disc_dir.expanduser().resolve()
        validate_disc(disc)
        return disc
    iso = iso.expanduser().resolve()
    if not iso.is_file():
        raise RuntimeError(f"Disc image does not exist: {iso}")
    private = runtime / "private"
    private.mkdir(exist_ok=True)
    checksum = check_source_image(runtime, iso, env)
    prepared = private / "GALE01r2.iso"
    disc = private / "GALE01r2"
    complete = private / "extracted-image.sha256"
    if not prepared.exists():
        run(sys.executable, runtime / "scripts/prepare_disc.py", iso, prepared, cwd=runtime, env=env)
    (private / "source-image.sha256").write_text(checksum + "\n")
    if not complete.exists() or complete.read_text().strip() != checksum:
        # main.dol can exist even when extraction was interrupted before assets.
        # Publish only a completed extraction and preserve any older local cache.
        with tempfile.TemporaryDirectory(prefix="extract-", dir=private) as directory:
            fresh = Path(directory) / "disc"
            run(recompiler, "extract", prepared, fresh, env=env)
            validate_disc(fresh)
            if disc.exists():
                previous = private / ("previous-disc-" + uuid.uuid4().hex)
                disc.rename(previous)
                print(f"Preserved previous extraction: {previous}")
            fresh.rename(disc)
        complete.write_text(checksum + "\n")
    validate_disc(disc)
    return disc


def verify_matching(disc, env):
    original = ROOT / "orig/GALE01/sys/main.dol"
    if original.exists():
        if digest(original) != DOL_SHA1:
            raise RuntimeError("The existing orig/GALE01/sys/main.dol is not USA v1.02; it was left unchanged.")
    else:
        original.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(disc / "sys/main.dol", original)
    run(sys.executable, ROOT / "configure.py", "--map", "--no-always-apply", env=env)
    run(sys.executable, ROOT / "tools/verify.py", env=env)
    dol = ROOT / "build/GALE01/main.dol"
    if digest(dol) != DOL_SHA1:
        raise RuntimeError("The complete matching executable has the wrong SHA-1.")
    return dol


def pinned_checkout(path, url, revision):
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        run("git", "clone", url, path)
        run("git", "checkout", "--detach", revision, cwd=path)
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=path, text=True).strip()
    if actual != revision:
        raise RuntimeError(f"Expected pinned revision {revision}, found {actual} in {path}.")
    run("git", "config", "core.longpaths", "true", cwd=path)


def patch_state(checkout, patch, reverse=False):
    command = ["git", "apply", "--ignore-space-change", "--check"]
    if reverse:
        command.append("--reverse")
    return subprocess.run(command, input=patch.read_text().encode(), cwd=checkout, capture_output=True).returncode == 0


def apply_patch(checkout, patch, reverse=False):
    # Git's Windows checkout conversion can give patch context mixed CRLF/LF.
    # Normalize the patch itself and tolerate checked-out context whitespace.
    command = ["git", "apply", "--ignore-space-change"]
    if reverse:
        command.append("--reverse")
    print(f"+ applying {'reverse ' if reverse else ''}{patch.name}", flush=True)
    subprocess.run(command, input=patch.read_text().encode(), cwd=checkout, check=True)


def patches(runtime):
    template = runtime / "upstream/ModernGekko-Template"
    modern = template / "lib/ModernGekko"
    dolphin = modern / "vendor/dolphin"
    result = [
        (dolphin, runtime / "patches/strict-native.patch"),
        (modern, runtime / "patches/cpu-abi.patch"),
        (template / "lib/DolRecomp", runtime / "patches/native-spr-codegen.patch"),
        (modern, runtime / "patches/netplay.patch"),
        (modern, runtime / "patches/media-settings.patch"),
        (modern, MAC / "patches/runtime-cache.patch"),
        (modern, MAC / "patches/startup-inspection.patch"),
    ]
    result.extend((dolphin, MAC / "patches" / name) for name in (
        "strict-cpu.patch", "native-boot.patch", "disc-transfer.patch", "low-latency-input.patch",
        "native-timebase.patch", "native-idle.patch", "frame-timing.patch",
        "texture-cache.patch", "ending-stills.patch",
    ))
    result.extend((dolphin, patch) for patch in sorted((HERE / "patches").glob("*.patch")))
    return result


def prepare_sources(runtime):
    pinned_checkout(runtime, RUNTIME_URL, RUNTIME_REV)
    template = runtime / "upstream/ModernGekko-Template"
    pinned_checkout(template, "https://github.com/ExpansionPak/ModernGekko-Template.git", TEMPLATE_REV)
    # Long filenames occur in vendored dependencies on Windows.
    run("git", "-c", "core.longpaths=true", "submodule", "update", "--init", "--recursive", "--jobs", "8", cwd=template)
    # CMake's revision generator runs Git without our command-line setting.
    run("git", "-c", "core.longpaths=true", "submodule", "foreach", "--recursive",
        "git config core.longpaths true", cwd=template)
    ordered = patches(runtime)
    # Later patches share context with earlier ones. Restore only this driver's
    # known patches, in reverse order, before checking forward application.
    for checkout, patch in reversed(ordered):
        if patch_state(checkout, patch, reverse=True):
            apply_patch(checkout, patch, reverse=True)
    for checkout, patch in ordered:
        if not patch_state(checkout, patch):
            raise RuntimeError(f"Local changes conflict with {patch.name} in {checkout}.")
        apply_patch(checkout, patch)
    dolphin = template / "lib/ModernGekko/vendor/dolphin"
    shutil.copy2(MAC / "textures/MeleeTexturePack.h", dolphin / "Source/Core/VideoCommon")
    shutil.copy2(MAC / "textures/MeleeEndingStills.h", dolphin / "Source/Core/VideoCommon")
    return template


def toolchain_environment():
    """Import VS's supported x64 environment without changing the user's shell."""
    env = os.environ.copy()
    if not shutil.which("cl", path=env.get("PATH")):
        installer = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
        if not installer.is_file():
            raise RuntimeError("Install Visual Studio 2022 Build Tools with the Desktop development with C++ workload.")
        install = subprocess.check_output([
            str(installer), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property", "installationPath",
        ], text=True).strip()
        vcvars = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
        if not vcvars.is_file():
            raise RuntimeError("The Visual Studio x64 C++ toolchain is missing.")
        setup_env = env.copy()
        setup_env["PATH"] = str(installer.parent) + os.pathsep + setup_env.get("PATH", "")
        output = subprocess.check_output(
            f'cmd.exe /d /u /s /c ""{vcvars}" >nul && set"',
            encoding="utf-16-le", errors="replace", env=setup_env)
        for line in output.splitlines():
            if "=" in line and not line.startswith("="):
                name, value = line.split("=", 1)
                env[name.upper()] = value
        # Windows environment variable names are case insensitive, Python dicts
        # are not. Keep a single PATH for subprocess's environment block.
        for name in list(env):
            if name.upper() == "PATH" and name != "PATH":
                del env[name]
        cmake_root = Path(install) / "Common7/IDE/CommonExtensions/Microsoft/CMake"
        env["PATH"] = os.pathsep.join([
            str(Path(sys.executable).parent), str(cmake_root / "CMake/bin"),
            str(cmake_root / "Ninja"), env["PATH"],
        ])
    for tool in ("cl", "cmake", "ninja"):
        if not shutil.which(tool, path=env.get("PATH")):
            raise RuntimeError(f"Missing {tool} in the x64 build environment.")
    env["CMAKE_NINJA_FORCE_RESPONSE_FILE"] = "1"
    return env


def check_cpu_abi(modern, env, compiler):
    """Compile and compare every CPUState field using the actual Windows ABI."""
    headers = [modern / "include/moderngekko/cpu_state.h", modern / "vendor/dolphin/GXRuntime/include/core/cpu.h"]
    results = []
    with tempfile.TemporaryDirectory(prefix="abi-", dir=modern.parent.parent.parent.parent / "build") as directory:
        temporary = Path(directory)
        for index, header in enumerate(headers):
            body = re.search(r"struct CPUState\s*\{(.*?)\};", header.read_text(), re.S).group(1)
            fields = re.findall(r"\b(\w+)\s*(?:\[\d+\])?;", body)
            source = temporary / f"check{index}.c"
            binary = temporary / f"check{index}.exe"
            code = ["#include <stddef.h>", "#include <stdio.h>", f'#include "{header.as_posix()}"', "int main(void) {",
                    'printf("ABI %u size %zu\\n", GXRUNTIME_CPU_ABI_VERSION, sizeof(CPUState));']
            code.extend(f'printf("{field} %zu %zu\\n", offsetof(CPUState,{field}), sizeof(((CPUState*)0)->{field}));' for field in fields)
            source.write_text("\n".join([*code, "return 0; }"]))
            run(compiler, "/nologo", "/std:c11", "/I" + str(modern / "vendor/dolphin/GXRuntime/include"), source, "/Fe:" + str(binary), cwd=temporary, env=env)
            results.append(subprocess.check_output([str(binary)], text=True, env=env))
    if results[0] != results[1]:
        raise RuntimeError("CPU state ABI mismatch:\n" + "\n".join(results))
    print("CPU state ABI layouts match:", results[0].splitlines()[0], flush=True)


def module_toolchain(kind, llvm_dir, env):
    """Keep the measured Clang module independent from the MSVC runtime build."""
    if kind == "msvc":
        return "cl", [], env
    if llvm_dir:
        llvm_root = llvm_dir.expanduser().resolve()
        candidates = [llvm_root / "bin", llvm_root]
    else:
        candidates = [ROOT / "build/tools/llvm/bin"]
        on_path = shutil.which("clang-cl", path=env.get("PATH"))
        if on_path:
            candidates.append(Path(on_path).parent)
        candidates.append(Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "LLVM/bin")
    required = ("clang-cl.exe", "lld-link.exe", "llvm-lib.exe")
    llvm = next((path for path in candidates if all((path / name).is_file() for name in required)), None)
    if llvm is None:
        raise RuntimeError("Clang module builds require clang-cl, lld-link, and llvm-lib. "
                           "Provide --llvm-dir or place LLVM in build/tools/llvm. "
                           "Use --module-compiler msvc only for an explicit comparison build.")
    module_env = env.copy()
    module_env["PATH"] = str(llvm) + os.pathsep + module_env.get("PATH", "")
    options = ["-DCMAKE_LINKER=" + (llvm / "lld-link.exe").as_posix(),
               "-DCMAKE_AR=" + (llvm / "llvm-lib.exe").as_posix()]
    return (llvm / "clang-cl.exe").as_posix(), options, module_env


def require_module_ipo(output):
    # CMake's check_ipo_supported can quietly disable IPO even when its option
    # is ON. Verify generated compile flags before accepting a slower build.
    ninja = (output / "build.ninja").read_text()
    if not re.search(r"^\s*FLAGS = .*\s-flto=thin(?:\s|$)", ninja, re.M):
        raise RuntimeError("Clang module ThinLTO was not enabled. Check CMake's IPO diagnostic and LLVM tools.")


def write_launch_manifest(runtime, module, compiler):
    runner = runtime / "build/runtime/moderngekko-run.exe"
    if not runner.is_file() or not module.is_file():
        raise RuntimeError("Cannot publish launch paths before the runtime and game module exist.")
    manifest = {
        "version": 1,
        "runtime": runner.relative_to(runtime).as_posix(),
        "module": module.relative_to(runtime).as_posix(),
        "module_compiler": compiler,
        "module_ipo": compiler == "clang-cl",
    }
    destination = runtime / "build/launch-manifest.json"
    temporary = destination.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    temporary.replace(destination)


def configure(source, output, options, env, compiler):
    run("cmake", "-S", source, "-B", output, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_C_COMPILER=" + compiler, "-DCMAKE_CXX_COMPILER=" + compiler,
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5", "-DPython3_EXECUTABLE=" + sys.executable,
        *options, env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--iso", type=Path, help="Your USA v1.02 ISO, GCM, or CISO")
    source.add_argument("--disc-dir", type=Path, help="Existing extracted game directory with sys and files")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / "build/native-windows/recomp")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 8, 12))
    parser.add_argument("--module-compiler", "--compiler", choices=("msvc", "clang-cl"), default="clang-cl",
                        help="Game module compiler (default clang-cl); runtime and tools use MSVC")
    parser.add_argument("--llvm-dir", type=Path, help="LLVM install directory or bin directory")
    parser.add_argument("--stage", choices=("prepare", "tools", "all"), default="all")
    args = parser.parse_args()
    if platform.system() != "Windows" or platform.machine().lower() not in ("amd64", "x86_64"):
        parser.error("This build requires 64-bit x86 Windows.")
    if sys.version_info < (3, 11):
        parser.error("Use Python 3.11 or later.")
    if args.jobs < 1:
        parser.error("--jobs must be positive.")
    if args.stage == "all" and not (args.iso or args.disc_dir):
        parser.error("Provide --iso or --disc-dir.")
    if args.iso and not args.iso.expanduser().is_file():
        parser.error("The supplied disc image does not exist.")
    if args.disc_dir:
        validate_disc(args.disc_dir.expanduser().resolve())
    runtime = require_local_build(args.runtime_dir)
    template = prepare_sources(runtime)
    if args.stage == "prepare":
        return
    (runtime / "build").mkdir(exist_ok=True)
    env = toolchain_environment()
    compiler = "cl"
    if args.stage == "all":
        game_compiler, game_options, game_env = module_toolchain(args.module_compiler, args.llvm_dir, env)
    modern = template / "lib/ModernGekko"
    dolphin = modern / "vendor/dolphin"
    check_cpu_abi(modern, env, compiler)
    configure(template / "lib/DolRecomp", runtime / "build/dolrecomp", ["-DDOLRECOMP_ENABLE_LLVM=OFF"], env, compiler)
    run("cmake", "--build", runtime / "build/dolrecomp", "--target", "dolrecomp", "-j", args.jobs, env=env)
    recompiler = runtime / "build/dolrecomp/dolrecomp.exe"
    if args.stage == "all":
        disc = prepare_disc(runtime, recompiler, args.iso, args.disc_dir, env)
        dol = verify_matching(disc, env)
    configure(modern, runtime / "build/runtime", [
        "-DBUILD_TESTING=OFF", "-DDOLRECOMP_ENABLE_LLVM=OFF", "-DMODERNGEKKO_REQUIRED_DISC_ID=GALE01",
        "-DMODERNGEKKO_GAMECUBE_CONTROLLERS=ON", "-DMODERNGEKKO_FRONTEND_NAME=Melee for Windows",
        "-DMODERNGEKKO_DEFAULT_WINDOW_TITLE=Melee for Windows - fully automated slop experiment",
    ], env, compiler)
    run("cmake", "--build", runtime / "build/runtime", "--target", "moderngekko-run", "-j", args.jobs, env=env)
    if args.stage == "tools":
        return
    native_dol = runtime / "private/native-boot.dol"
    run(sys.executable, MAC / "recompile_boot.py", "--dol", dol, "--apploader", disc / "sys/apploader.img", "--output", native_dol, env=env)
    # A fresh temporary output prevents stale chunks when section boundaries
    # change. Its absolute path is known to be inside this ignored runtime.
    with tempfile.TemporaryDirectory(prefix="generated-", dir=runtime / "private") as directory:
        scratch = Path(directory)
        run(recompiler, f"-j{args.jobs}", "--cpu", "gekko", "--gamecube", native_dol, scratch, env=env)
        target = runtime / "private/recompiled/generated"
        source = scratch / "generated"
        for path in source.rglob("*"):
            if path.is_file():
                destination = target / path.relative_to(source)
                if not destination.exists() or path.read_bytes() != destination.read_bytes():
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(path, destination)
        for path in (target / "chunks").glob("*.c"):
            if not (source / path.relative_to(target)).exists():
                path.unlink()
    shutil.copy2(native_dol, target / "main.dol")
    game_build = runtime / ("build/game-clang" if args.module_compiler == "clang-cl" else "build/game")
    configure(dolphin / "module-template", game_build, [
        "-DGAME_ID=GALE01", "-DGENERATED_DIR=" + target.as_posix(),
        "-DGXRUNTIME_DIR=" + (dolphin / "GXRuntime").as_posix(),
        "-DRECOMPCORE_MODULE_OPT_LEVEL=2",
        "-DRECOMPCORE_MODULE_ENABLE_IPO=" + ("ON" if args.module_compiler == "clang-cl" else "OFF"),
        *game_options,
    ], game_env, game_compiler)
    if args.module_compiler == "clang-cl":
        require_module_ipo(game_build)
    run("cmake", "--build", game_build, "-j", args.jobs, env=game_env)
    module = game_build / "gGALE01_recomp.dll"
    write_launch_manifest(runtime, module, args.module_compiler)
    print(f"Native Windows game module: {module}")
    print(f"Extracted game: {disc}")
    print("This is Theo's fully automated slop experiment. No support, maintenance, or human review is promised.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
