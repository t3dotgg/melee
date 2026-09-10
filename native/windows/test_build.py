# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic checks for the local Windows build driver; no game data needed."""

import importlib.util
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location("windows_build", Path(__file__).with_name("build.py"))
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class BuildDriverTests(unittest.TestCase):
    def synthetic_disc(self, destination):
        (destination / "sys").mkdir(parents=True, exist_ok=True)
        (destination / "files").mkdir(exist_ok=True)
        for name in ("main.dol", "apploader.img", "boot.bin", "bi2.bin", "fst.bin"):
            (destination / "sys" / name).write_bytes(b"synthetic " + name.encode())
        (destination / "files/asset.dat").write_bytes(b"synthetic asset")

    def test_rejects_generated_data_outside_ignored_build(self):
        with self.assertRaisesRegex(RuntimeError, "ignored build"):
            BUILD.require_local_build(BUILD.ROOT / "orig/windows-runtime")
        with self.assertRaisesRegex(RuntimeError, "ignored build"):
            BUILD.require_local_build(BUILD.ROOT / "build/../not-ignored")
        self.assertEqual(BUILD.require_local_build(BUILD.ROOT / "build/windows-test"),
                         (BUILD.ROOT / "build/windows-test").resolve())

    def test_process_uses_imported_toolchain_path(self):
        env = {"PATH": "the-visual-studio-toolchain"}
        with mock.patch.object(BUILD.shutil, "which", return_value="C:/compiler/cl.exe") as which:
            with mock.patch.object(BUILD.subprocess, "run") as command:
                BUILD.run("cl", "/nologo", env=env)
        which.assert_called_once_with("cl", path=env["PATH"])
        self.assertEqual(command.call_args.args[0], ["C:/compiler/cl.exe", "/nologo"])

    @unittest.skipUnless(shutil.which("git"), "Git is required to check patch application")
    def test_mixed_line_endings_and_conflicting_patch(self):
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            subprocess.run(["git", "init", "-q", str(checkout)], check=True)
            target = checkout / "sample.txt"
            target.write_bytes(b"before\r\n")
            patch = checkout / "change.patch"
            patch.write_bytes(b"--- a/sample.txt\r\n+++ b/sample.txt\r\n@@ -1 +1 @@\r\n-before\r\n+after\r\n")
            self.assertTrue(BUILD.patch_state(checkout, patch))
            BUILD.apply_patch(checkout, patch)
            self.assertEqual(target.read_text(), "after\n")
            self.assertTrue(BUILD.patch_state(checkout, patch, reverse=True))
            BUILD.apply_patch(checkout, patch, reverse=True)
            self.assertEqual(target.read_text(), "before\n")
            target.write_text("unrelated user edit\n")
            self.assertFalse(BUILD.patch_state(checkout, patch))
            self.assertFalse(BUILD.patch_state(checkout, patch, reverse=True))

    def test_different_source_image_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            runtime = Path(directory)
            (runtime / "private").mkdir()
            iso = runtime / "source.iso"
            iso.write_bytes(b"synthetic image A")
            stamp = runtime / "private/source-image.sha256"
            stamp.write_text(BUILD.digest(iso, "sha256"))
            self.assertEqual(BUILD.check_source_image(runtime, iso, {}), stamp.read_text())
            iso.write_bytes(b"synthetic image B")
            with self.assertRaisesRegex(RuntimeError, "different image"):
                BUILD.check_source_image(runtime, iso, {})

    def test_unstamped_cache_requires_complete_image_match(self):
        with tempfile.TemporaryDirectory() as directory:
            runtime = Path(directory)
            (runtime / "private").mkdir()
            iso = runtime / "source.iso"
            cached = runtime / "private/GALE01r2.iso"
            iso.write_bytes(b"synthetic image A")
            cached.write_bytes(iso.read_bytes())
            def normalize(*args, **kwargs):
                shutil.copyfile(args[-2], args[-1])
            with mock.patch.object(BUILD, "run", side_effect=normalize):
                self.assertEqual(BUILD.check_source_image(runtime, iso, {}), BUILD.digest(iso, "sha256"))
                cached.write_bytes(b"synthetic image B")
                with self.assertRaisesRegex(RuntimeError, "differs from --iso"):
                    BUILD.check_source_image(runtime, iso, {})

    def test_fresh_iso_seeds_original_before_matching_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runtime = root / "build/native-windows/recomp"
            runtime.mkdir(parents=True)
            iso = root / "input.iso"
            iso.write_bytes(b"synthetic image")
            original = root / "orig/GALE01/sys/main.dol"
            def command(*args, **kwargs):
                if len(args) > 1 and Path(args[1]).name == "prepare_disc.py":
                    shutil.copyfile(args[-2], args[-1])
                elif len(args) > 1 and args[1] == "extract":
                    self.synthetic_disc(args[-1])
                elif len(args) > 1 and Path(args[1]).name == "configure.py":
                    self.assertEqual(original.read_bytes(), b"synthetic main.dol")
                    raise RuntimeError("reached matching configuration after extraction")
            with mock.patch.multiple(BUILD, ROOT=root, DOL_SHA1=hashlib.sha1(b"synthetic main.dol").hexdigest()):
                with mock.patch.object(BUILD, "prepare_sources", return_value=runtime / "upstream/ModernGekko-Template"), \
                     mock.patch.object(BUILD, "toolchain_environment", return_value={}), \
                     mock.patch.object(BUILD, "check_cpu_abi"), \
                     mock.patch.object(BUILD, "configure"), \
                     mock.patch.object(BUILD, "run", side_effect=command), \
                     mock.patch.object(BUILD.platform, "system", return_value="Windows"), \
                     mock.patch.object(BUILD.platform, "machine", return_value="AMD64"), \
                     mock.patch.object(BUILD.sys, "argv", ["build.py", "--iso", str(iso), "--module-compiler", "msvc"]):
                    with self.assertRaisesRegex(RuntimeError, "reached matching configuration after extraction"):
                        BUILD.main()

    def test_partial_extraction_retries_without_reusing_missing_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            runtime = Path(directory)
            private = runtime / "private"
            private.mkdir()
            iso = runtime / "source.iso"
            iso.write_bytes(b"synthetic image")
            prepared = private / "GALE01r2.iso"
            prepared.write_bytes(iso.read_bytes())
            (private / "source-image.sha256").write_text(BUILD.digest(iso, "sha256"))
            disc = private / "GALE01r2"
            (disc / "sys").mkdir(parents=True)
            (disc / "sys/main.dol").write_bytes(b"partial stale cache")
            def extract(*args, **kwargs):
                self.assertEqual(args[1], "extract")
                self.synthetic_disc(args[-1])
            with mock.patch.object(BUILD, "DOL_SHA1", hashlib.sha1(b"synthetic main.dol").hexdigest()):
                with mock.patch.object(BUILD, "run", side_effect=extract) as command:
                    self.assertEqual(BUILD.prepare_disc(runtime, Path("dolrecomp.exe"), iso, None, {}), disc)
                    self.assertTrue((disc / "files/asset.dat").is_file())
                    BUILD.prepare_disc(runtime, Path("dolrecomp.exe"), iso, None, {})
                    self.assertEqual(command.call_count, 1)
            previous = list(private.glob("previous-disc-*"))
            self.assertEqual(len(previous), 1)
            self.assertEqual((previous[0] / "sys/main.dol").read_bytes(), b"partial stale cache")

    def test_existing_nonmatching_original_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / "orig/GALE01/sys/main.dol"
            original.parent.mkdir(parents=True)
            original.write_bytes(b"different existing game")
            with mock.patch.object(BUILD, "ROOT", root), mock.patch.object(BUILD, "run") as command:
                with self.assertRaisesRegex(RuntimeError, "left unchanged"):
                    BUILD.verify_matching(root / "disc", {})
            self.assertEqual(original.read_bytes(), b"different existing game")
            command.assert_not_called()

    def test_explicit_incomplete_llvm_does_not_select_another_install(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            local = root / "build/tools/llvm/bin"
            local.mkdir(parents=True)
            for name in ("clang-cl.exe", "lld-link.exe", "llvm-lib.exe"):
                (local / name).touch()
            explicit = root / "incomplete"
            explicit.mkdir()
            (explicit / "clang-cl.exe").touch()
            with mock.patch.object(BUILD, "ROOT", root):
                with self.assertRaisesRegex(RuntimeError, "require clang-cl, lld-link, and llvm-lib"):
                    BUILD.module_toolchain("clang-cl", explicit, {"PATH": str(local)})

    def test_local_llvm_selection_preserves_runtime_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            local = root / "build/tools/llvm/bin"
            local.mkdir(parents=True)
            for name in ("clang-cl.exe", "lld-link.exe", "llvm-lib.exe"):
                (local / name).touch()
            env = {"PATH": "msvc-runtime-toolchain", "INCLUDE": "windows-sdk"}
            with mock.patch.object(BUILD, "ROOT", root):
                compiler, options, module_env = BUILD.module_toolchain("clang-cl", None, env)
            self.assertEqual(Path(compiler), local / "clang-cl.exe")
            self.assertEqual(module_env["INCLUDE"], "windows-sdk")
            self.assertTrue(module_env["PATH"].startswith(str(local)))
            self.assertEqual(env["PATH"], "msvc-runtime-toolchain")
            self.assertIn("-DCMAKE_LINKER=" + (local / "lld-link.exe").as_posix(), options)

    def test_requested_ipo_without_actual_thinlto_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "CMakeCache.txt").write_text("RECOMPCORE_MODULE_ENABLE_IPO:BOOL=ON\n")
            ninja = output / "build.ninja"
            ninja.write_text("  FLAGS = /O2 /fp:strict\n")
            with self.assertRaisesRegex(RuntimeError, "ThinLTO was not enabled"):
                BUILD.require_module_ipo(output)
            ninja.write_text("  FLAGS = /O2 -flto=thin /fp:strict\n")
            BUILD.require_module_ipo(output)

    def test_manifest_is_published_only_for_complete_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            runtime = Path(directory)
            runner = runtime / "build/runtime/moderngekko-run.exe"
            runner.parent.mkdir(parents=True)
            runner.write_bytes(b"synthetic runtime")
            module = runtime / "build/game-clang/gGALE01_recomp.dll"
            destination = runtime / "build/launch-manifest.json"
            destination.write_text('{"version":1,"module":"previous successful build"}')
            previous = destination.read_bytes()
            with self.assertRaisesRegex(RuntimeError, "before the runtime and game module exist"):
                BUILD.write_launch_manifest(runtime, module, "clang-cl")
            self.assertEqual(destination.read_bytes(), previous)
            module.parent.mkdir()
            module.write_bytes(b"synthetic module")
            BUILD.write_launch_manifest(runtime, module, "clang-cl")
            manifest = json.loads(destination.read_text())
            self.assertEqual(manifest["module"], "build/game-clang/gGALE01_recomp.dll")
            self.assertEqual(manifest["runtime"], "build/runtime/moderngekko-run.exe")
            self.assertEqual(manifest["module_compiler"], "clang-cl")
            self.assertIs(manifest["module_ipo"], True)
            self.assertFalse(destination.with_suffix(".json.tmp").exists())

    @unittest.skipUnless(BUILD.platform.system() == "Windows", "Uses the Windows C++ toolchain")
    def test_native_frame_clock_has_no_scanline_rounding_drift(self):
        try:
            env = BUILD.toolchain_environment()
        except RuntimeError as error:
            self.skipTest(str(error))
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            subprocess.run(["git", "init", "-q", str(temporary)], check=True)
            # Compile the actual shared helper introduced by the runtime patch.
            # The synthetic test never needs the game or downloaded runtime.
            patch = BUILD.MAC / "patches/frame-timing.patch"
            subprocess.run(["git", "apply", "--include=Source/Core/Common/FixedFrameTiming.h"],
                           input=patch.read_text().encode(), cwd=temporary, check=True)
            source = temporary / "timing.cpp"
            source.write_text('''#include "Source/Core/Common/FixedFrameTiming.h"
using Common::FixedFrameTickOffset;
constexpr bool FractionalCyclesCarry()
{
    unsigned previous = 0;
    for (unsigned line = 1; line <= 1050; ++line)
    {
        const unsigned now = FixedFrameTickOffset(line, 1050, 60, 486000000);
        const unsigned interval = now - previous;
        if (interval != 15428 && interval != 15429)
            return false;
        previous = now;
    }
    return previous == 16200000;
}
static_assert(FractionalCyclesCarry());
static_assert(FixedFrameTickOffset(525, 1050, 60, 486000000) == 8100000);
static_assert(FixedFrameTickOffset(1050, 1050, 120, 486000000) == 8100000);
static_assert(FixedFrameTickOffset(1, 0, 60, 486000000) == 0);
static_assert(FixedFrameTickOffset(1, 1050, 0, 486000000) == 0);
int main() { return 0; }
''')
            binary = temporary / "timing.exe"
            BUILD.run("cl", "/nologo", "/std:c++20", source, "/Fe:" + str(binary), cwd=temporary, env=env)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
