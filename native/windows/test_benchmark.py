# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic Windows benchmark failure checks; no game data or runtime required."""

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location("windows_benchmark", Path(__file__).with_name("benchmark.py"))
BENCHMARK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BENCHMARK)

VALID_STATUS = ("state=running\nbooted=1\nfps=59.99\nspeed=1\nframe_count=10\n"
                "present_count=20\nlast_error=\n")


class BenchmarkTests(unittest.TestCase):
    def test_status_retries_sharing_violations_and_incomplete_snapshots(self):
        with mock.patch.object(Path, "read_text", side_effect=[PermissionError(),
                               "state=running\nfps=60\n", VALID_STATUS]):
            with mock.patch.object(BENCHMARK.time, "sleep") as sleep:
                result = BENCHMARK.status(Path("unused"), attempts=3)
        self.assertEqual(result["fps"], "59.99")
        self.assertEqual(sleep.call_count, 2)

    def test_status_rejects_truncation_invalid_counters_and_nonfinite_metrics(self):
        for text in (VALID_STATUS.replace("last_error=\n", ""),
                     VALID_STATUS.replace("frame_count=10", "frame_count=x"),
                     VALID_STATUS.replace("fps=59.99", "fps=nan"),
                     VALID_STATUS.replace("speed=1", "speed=-1")):
            with self.subTest(text=text), mock.patch.object(Path, "read_text", return_value=text):
                self.assertEqual(BENCHMARK.status(Path("unused"), attempts=1), {})

    def test_status_returns_empty_when_runtime_has_not_started(self):
        with mock.patch.object(Path, "read_text", side_effect=FileNotFoundError()):
            self.assertEqual(BENCHMARK.status(Path("unused"), attempts=1), {})

    def test_manifest_selects_built_module_and_rejects_path_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runtime = root / "build/native-windows/recomp"
            manifest = runtime / "build/launch-manifest.json"
            manifest.parent.mkdir(parents=True)
            data = {"version": 1, "runtime": "build/runtime/runner.exe", "module": "build/game/module.dll"}
            manifest.write_text(json.dumps(data))
            runner, module, _ = BENCHMARK.launch_paths(root)
            self.assertEqual(runner, runtime / data["runtime"])
            self.assertEqual(module, runtime / data["module"])
            override = root / "alternate.dll"
            self.assertEqual(BENCHMARK.launch_paths(root, override)[1], override)
            data["module"] = "../outside.dll"
            manifest.write_text(json.dumps(data))
            with self.assertRaisesRegex(RuntimeError, "leaves the runtime"):
                BENCHMARK.launch_paths(root)

    def test_missing_manifest_selects_clang_module(self):
        with tempfile.TemporaryDirectory() as directory:
            _, module, _ = BENCHMARK.launch_paths(Path(directory))
            self.assertEqual(module.name, "gGALE01_recomp.dll")
            self.assertEqual(module.parent.name, "game-clang")

    def test_existing_or_nonignored_output_is_rejected_without_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "build/old-run"
            output.mkdir(parents=True)
            sentinel = output / "previous.log"
            sentinel.write_text("preserve")
            args = argparse.Namespace(output=output, state=root / "state.sav", module=None)
            with mock.patch.object(BENCHMARK, "ROOT", root):
                with self.assertRaisesRegex(RuntimeError, "already exists"):
                    BENCHMARK.prepare(args)
                args.output = root / "report"
                with self.assertRaisesRegex(RuntimeError, "ignored build"):
                    BENCHMARK.prepare(args)
            self.assertEqual(sentinel.read_text(), "preserve")
            self.assertFalse((root / "report").exists())

    def test_command_failure_is_not_mistaken_for_completion(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("commands", "processed", "failed"):
                (root / name).mkdir()
            (root / "failed/fixed.txt").touch()
            process = mock.Mock()
            with mock.patch.object(BENCHMARK.uuid, "uuid4", return_value=mock.Mock(hex="fixed")):
                with mock.patch.object(BENCHMARK, "status", return_value={"last_error": "save failed"}):
                    with self.assertRaisesRegex(RuntimeError, "save failed"):
                        BENCHMARK.command(root, process, "screenshot", path=root / "scene.png")
            process.terminate.assert_not_called()

    def test_command_accepts_processed_file_without_racy_status_read(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("commands", "processed"):
                (root / name).mkdir()
            (root / "processed/fixed.txt").touch()
            with mock.patch.object(BENCHMARK.uuid, "uuid4", return_value=mock.Mock(hex="fixed")):
                with mock.patch.object(BENCHMARK, "status", side_effect=AssertionError("not needed")):
                    BENCHMARK.command(root, mock.Mock(), "stop")

    def test_stop_uses_runtime_command_before_forced_termination(self):
        process = mock.Mock()
        process.poll.return_value = None
        with mock.patch.object(BENCHMARK, "command") as command:
            self.assertFalse(BENCHMARK.stop(process, Path("automation")))
        command.assert_called_once_with(Path("automation"), process, "stop", timeout=10)
        process.terminate.assert_not_called()
        with mock.patch.object(BENCHMARK, "command", side_effect=RuntimeError("unresponsive")):
            self.assertTrue(BENCHMARK.stop(process, Path("automation")))
        process.terminate.assert_called_once()

    def test_early_exit_preserves_error_summary_and_log(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = (root, root / "runner.exe", root / "module.dll", root / "disc",
                     root / "state.sav", root / "user", root / "textures", root / "automation")
            args = argparse.Namespace(seconds=60, warmup=5, profile=False)
            process = mock.Mock(returncode=7)
            process.poll.return_value = 7
            with mock.patch.object(BENCHMARK, "prepare", return_value=paths), \
                    mock.patch.object(BENCHMARK.subprocess, "Popen", return_value=process), \
                    mock.patch.object(BENCHMARK, "status", return_value={}), \
                    mock.patch("builtins.print"):
                with self.assertRaisesRegex(RuntimeError, "No rendered frames"):
                    BENCHMARK.measure(args)
            report = json.loads((root / "summary.json").read_text())
            self.assertEqual(report["exit_code"], 7)
            self.assertEqual(report["samples"], [])
            self.assertIn("No rendered frames", report["error"])
            self.assertTrue((root / "runtime.log").is_file())


if __name__ == "__main__":
    unittest.main()
