#!/usr/bin/env python3
"""Build a local, movable Melee for Mac app from an existing runtime build.

The app contains the user's game data. Do not commit or upload its output.
Runtime integration: McDandle/melee-macos-recomp, GPL-3.0-or-later.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile


APP_ID = "t3.melee.native"
DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"
HERE = Path(__file__).resolve().parent
SYSTEM_PREFIXES = ("/System/Library/", "/usr/lib/")


class PackageError(Exception):
    """A build input or bundle is not usable."""


def run(*args: str | Path) -> str:
    result = subprocess.run([str(arg) for arg in args], capture_output=True, text=True)
    if result.returncode:
        raise PackageError(f"{args[0]} failed:\n{result.stderr or result.stdout}")
    return result.stdout


def validate_inputs(root: Path) -> None:
    for relative in (
        "build/runtime/moderngekko-run",
        "build/game/gGALE01_recomp.dylib",
        "private/GALE01r2/sys/main.dol",
        "config/GCPadNew.ini",
        "LICENSE",
        "CREDITS.md",
    ):
        path = root / relative
        if not path.is_file() or path.stat().st_size == 0:
            raise PackageError(f"Missing or empty build input: {path}")
    for relative in ("private/GALE01r2/files", "build/runtime/Sys"):
        path = root / relative
        if not path.is_dir() or next(path.iterdir(), None) is None:
            raise PackageError(f"Missing or empty resource directory: {path}")
    dol = root / "private/GALE01r2/sys/main.dol"
    if hashlib.sha1(dol.read_bytes()).hexdigest() != DOL_SHA1:
        raise PackageError("The executable must match Melee USA revision 2 (GALE01).")


def validate_output(output: Path, replace: bool) -> None:
    if output.suffix != ".app":
        raise PackageError("The output path must end in .app.")
    if output.is_symlink():
        raise PackageError("The output app must not be a symbolic link.")
    if not output.exists():
        return
    if not replace:
        raise PackageError(f"The app already exists: {output}. Use --replace to rebuild it.")
    try:
        with (output / "Contents/Info.plist").open("rb") as source:
            app_id = plistlib.load(source).get("CFBundleIdentifier")
    except (OSError, ValueError, AttributeError) as error:
        raise PackageError("Refusing to replace a directory without this app's Info.plist.") from error
    if app_id != APP_ID:
        raise PackageError("Refusing to replace an app with a different bundle identifier.")


@dataclass(frozen=True)
class MachOInfo:
    dependencies: tuple[str, ...]
    rpaths: tuple[str, ...]
    install_id: str | None
    minimum_macos: tuple[int, int, int] | None


def parse_version(value: str) -> tuple[int, int, int]:
    if re.fullmatch(r"\d+(?:\.\d+){0,2}", value) is None:
        raise PackageError(f"Invalid minimum macOS version in a binary: {value}")
    components = [int(part) for part in value.split(".")]
    components += [0] * (3 - len(components))
    return components[0], components[1], components[2]


def parse_load_commands(text: str) -> MachOInfo:
    dependencies = []
    rpaths = []
    install_id = None
    minimum_macos = None
    for block in re.split(r"Load command \d+\n", text)[1:]:
        command = re.search(r"^\s*cmd (\S+)$", block, re.MULTILINE)
        if command is None:
            continue
        kind = command.group(1)
        version_field = None
        if kind == "LC_VERSION_MIN_MACOSX":
            version_field = "version"
        elif kind == "LC_BUILD_VERSION" and re.search(
                r"^\s*platform (?:1|macos)\s*$", block, re.MULTILINE | re.IGNORECASE):
            version_field = "minos"
        if version_field is not None:
            version = re.search(rf"^\s*{version_field} (\S+)\s*$", block, re.MULTILINE)
            if version is None:
                raise PackageError(f"Missing {version_field} in {kind}.")
            parsed = parse_version(version.group(1))
            minimum_macos = max(minimum_macos or parsed, parsed)
        value = re.search(r"^\s*(?:name|path) (.+) \(offset \d+\)$", block, re.MULTILINE)
        if value is None:
            continue
        if kind == "LC_RPATH":
            rpaths.append(value.group(1))
        elif kind == "LC_ID_DYLIB":
            install_id = value.group(1)
        elif kind in ("LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB", "LC_LOAD_UPWARD_DYLIB"):
            dependencies.append(value.group(1))
    return MachOInfo(tuple(dependencies), tuple(rpaths), install_id, minimum_macos)


def minimum_system_version(binaries: list[Path]) -> str:
    """Use the strongest deployment requirement, including copied libraries."""
    minimum = (14, 0, 0)
    for binary in binaries:
        info = parse_load_commands(run("xcrun", "otool", "-l", binary))
        if info.minimum_macos is None:
            raise PackageError(f"No minimum macOS version is recorded in {binary}.")
        minimum = max(minimum, info.minimum_macos)
    parts = minimum if minimum[2] else minimum[:2]
    return ".".join(str(part) for part in parts)


def expand_path(value: str, loader: Path, executable: Path) -> Path | None:
    for token, base in (("@loader_path", loader.parent), ("@executable_path", executable.parent)):
        if value == token or value.startswith(token + "/"):
            return base / value[len(token):].lstrip("/")
    return Path(value) if value.startswith("/") else None


def resolve_dependency(name: str, loader: Path, executable: Path, rpaths: tuple[Path, ...]) -> Path:
    if name.startswith("@rpath/"):
        candidates = [base / name.removeprefix("@rpath/") for base in rpaths]
    else:
        candidate = expand_path(name, loader, executable)
        candidates = [candidate] if candidate is not None else []
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise PackageError(f"Cannot resolve {name}, required by {loader}.")


def bundle_libraries(images: list[tuple[Path, Path]], frameworks: Path) -> list[Path]:
    """Copy the full non-system dependency graph and remove build-machine paths."""
    executable = images[0][0]
    executable_info = parse_load_commands(run("xcrun", "otool", "-l", executable))
    executable_rpaths = tuple(
        path for value in executable_info.rpaths
        if (path := expand_path(value, executable, executable)) is not None
    )
    copied = {source.resolve(): target for source, target in images}
    names: dict[str, Path] = {}
    pending = [(source.resolve(), target, executable_rpaths) for source, target in images]
    signed = []
    while pending:
        source, target, inherited = pending.pop(0)
        info = parse_load_commands(run("xcrun", "otool", "-l", source))
        local_rpaths = tuple(
            path for value in info.rpaths
            if (path := expand_path(value, source, executable)) is not None
        )
        rpaths = local_rpaths + inherited
        for name in info.dependencies:
            if name.startswith(SYSTEM_PREFIXES):
                continue
            dependency = resolve_dependency(name, source, executable, rpaths)
            if any(part.endswith(".framework") for part in dependency.parts):
                raise PackageError(f"External framework needs explicit bundle support: {dependency}")
            destination = copied.get(dependency)
            if destination is None:
                if dependency.name in names and names[dependency.name] != dependency:
                    raise PackageError(f"Two libraries have the same bundle name: {dependency.name}")
                names[dependency.name] = dependency
                destination = frameworks / dependency.name
                shutil.copy2(dependency, destination)
                destination.chmod(destination.stat().st_mode | 0o200)
                copied[dependency] = destination
                pending.append((dependency, destination, rpaths))
            relative = os.path.relpath(destination, target.parent)
            run("xcrun", "install_name_tool", "-change", name, f"@loader_path/{relative}", target)
        for rpath in dict.fromkeys(info.rpaths):
            run("xcrun", "install_name_tool", "-delete_rpath", rpath, target)
        if info.install_id is not None:
            run("xcrun", "install_name_tool", "-id", f"@rpath/{target.name}", target)
        signed.append(target)
    return signed


def validate_texture_pack(path: Path) -> Path:
    path = path.expanduser().resolve()
    game = path / "GALE01"
    if game.is_symlink():
        raise PackageError("Texture packs must contain real files, not symbolic links.")
    if not game.is_dir() or not any(game.glob("**/*.dds")):
        raise PackageError("The texture pack must contain GALE01 with DDS textures.")
    for item in game.rglob("*"):
        if item.is_symlink():
            raise PackageError("Texture packs must contain real files, not symbolic links.")
    return path


def assemble(root: Path, app: Path, texture_pack: Path | None = None) -> None:
    contents = app / "Contents"
    macos = contents / "MacOS"
    resources = contents / "Resources"
    frameworks = contents / "Frameworks"
    for directory in (macos, resources / "Module", resources / "Defaults", resources / "Licenses", frameworks):
        directory.mkdir(parents=True)
    runtime_source = root / "build/runtime/moderngekko-run"
    module_source = root / "build/game/gGALE01_recomp.dylib"
    runtime = macos / "MeleeRuntime"
    module = resources / "Module/gGALE01_recomp.dylib"
    shutil.copy2(runtime_source, runtime)
    shutil.copy2(module_source, module)
    runtime.chmod(runtime.stat().st_mode | 0o700)
    module.chmod(module.stat().st_mode | 0o200)
    for directory in ("sys", "files"):
        shutil.copytree(root / "private/GALE01r2" / directory, resources / "Game" / directory)
    original_sky = resources / "Game/files/GrNLa.dat"
    if original_sky.is_file():
        run(sys.executable, HERE / "lighting/remaster_final_destination.py",
            original_sky, resources / "Lighting/GrNLa.dat")
    shutil.copytree(root / "build/runtime/Sys", resources / "Sys")
    if texture_pack is not None:
        shutil.copytree(texture_pack / "GALE01", resources / "Textures/GALE01")
    shutil.copy2(root / "config/GCPadNew.ini", resources / "Defaults/GCPadNew.ini")
    for name in ("LICENSE", "CREDITS.md"):
        shutil.copy2(root / name, resources / "Licenses" / name)
    readme = (HERE / "README.md").read_text().replace(
        "../../docs/native-macos.md",
        "https://github.com/t3dotgg/melee4mac/blob/master/docs/native-macos.md",
    )
    (resources / "README.md").write_text(readme)
    upstream = root / "upstream/ModernGekko-Template/lib/ModernGekko"
    for source, name in (
        (upstream / "LICENSE", "ModernGekko-LICENSE"),
        (upstream / "vendor/dolphin/COPYING", "Dolphin-COPYING"),
    ):
        if source.is_file():
            shutil.copy2(source, resources / "Licenses" / name)
    run("xcrun", "clang", "-O2", "-Wall", "-Wextra", "-Werror", "-arch", "arm64",
        "-mmacosx-version-min=14.0", "-fobjc-arc", "-framework", "AppKit",
        HERE / "launcher.m", "-o", macos / "Melee")
    binaries = bundle_libraries([(runtime_source, runtime), (module_source, module)], frameworks)
    binaries.append(macos / "Melee")
    info = {
        "CFBundleExecutable": "Melee",
        "CFBundleIdentifier": APP_ID,
        "CFBundleName": "Melee for Mac",
        "CFBundleDisplayName": "Melee for Mac",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": "0.1",
        "CFBundleVersion": "2",
        "LSApplicationCategoryType": "public.app-category.games",
        "LSMinimumSystemVersion": minimum_system_version(binaries),
        "NSHighResolutionCapable": True,
        "NSHumanReadableCopyright": "Melee for Mac. Automated slop experiment. Not for serious use or investigation. Credits: Contents/Resources/Licenses.",
    }
    icon = root / "build/AppIcon.icns"
    if icon.is_file():
        shutil.copy2(icon, resources / "AppIcon.icns")
        info["CFBundleIconFile"] = "AppIcon.icns"
    with (contents / "Info.plist").open("wb") as output:
        plistlib.dump(info, output)
    for binary in binaries:
        run("/usr/bin/codesign", "--force", "--sign", "-", binary)
    run("/usr/bin/codesign", "--force", "--sign", "-", app)
    run("/usr/bin/codesign", "--verify", "--deep", "--strict", app)


def package(root: Path, output: Path, replace: bool = False, texture_pack: Path | None = None) -> Path:
    root = root.expanduser().resolve()
    # Resolve the parent, but preserve a final symlink so validation can reject it.
    output = output.expanduser().absolute()
    output = output.parent.resolve() / output.name
    for relative in ("private/GALE01r2", "build/runtime/Sys"):
        if output.is_relative_to((root / relative).resolve()):
            raise PackageError("The app output must be outside the input resource directories.")
    validate_inputs(root)
    if texture_pack is not None:
        texture_pack = validate_texture_pack(texture_pack)
        if output.is_relative_to(texture_pack):
            raise PackageError("The app output must be outside the texture pack.")
    validate_output(output, replace)
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise PackageError("Packaging requires an Apple Silicon Mac with Xcode command line tools.")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".melee-package-", dir=output.parent) as temporary:
        staged = Path(temporary) / output.name
        assemble(root, staged, texture_pack)
        backup = Path(temporary) / "previous.app"
        if output.exists():
            output.rename(backup)
        try:
            staged.rename(output)
        except OSError:
            if backup.exists():
                backup.rename(output)
            raise
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-dir", type=Path, required=True, help="Built melee-macos-recomp checkout")
    parser.add_argument("--output", type=Path, required=True, help="Local output .app path")
    parser.add_argument("--replace", action="store_true", help="Replace an earlier app made by this tool")
    parser.add_argument("--texture-pack", type=Path, help="Replacement texture root containing GALE01")
    options = parser.parse_args()
    try:
        result = package(options.runtime_dir, options.output, options.replace, options.texture_pack)
    except (PackageError, OSError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")
    print(f"Local app: {result}\nThis app includes your game data. Do not upload it.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
