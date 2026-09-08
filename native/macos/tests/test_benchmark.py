# SPDX-License-Identifier: GPL-3.0-or-later
"""Check display timing analysis with delayed and missing callbacks."""

import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "macos_benchmark", Path(__file__).resolve().parents[1] / "benchmark.py"
)
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


class DisplayTimingTests(unittest.TestCase):
    def analyze(self, rows, seconds=None):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "frames.csv"
            path.write_text("frame,submit_s,gpu_start_s,gpu_end_s,gpu_busy_ms,display_s,gpu_submissions\n" + rows)
            return benchmark.summarize(path, seconds)

    def test_uses_display_order_and_ignores_missing_callbacks(self):
        result = self.analyze(
            "0,1.00,1.00,1.01,2,1.02,1\n"
            "1,1.01,1.01,1.02,3,1.01,1\n"
            "2,1.02,1.02,1.03,1,0,1\n"
            "3,1.02,1.02,1.03,2,1.03,1\n"
        )
        self.assertAlmostEqual(result["presentation_fps"], 100)
        self.assertAlmostEqual(result["presentation_interval_ms"]["p99"], 10)
        self.assertEqual(result["presentation_callbacks"], 3)
        self.assertEqual(result["frames_without_display_callback"], 1)

    def test_selects_last_seconds_and_does_not_count_same_display_twice(self):
        result = self.analyze(
            "0,1,1,1.1,2,1.5,1\n"
            "1,2,2,2.1,2,2.5,1\n"
            "2,3,3,3.1,2,3.5,1\n"
            "3,3,3,3.1,2,3.5,1\n",
            seconds=1.5,
        )
        self.assertEqual(result["presentation_callbacks"], 2)
        self.assertEqual(result["same_presentation_timestamp"], 1)
        self.assertAlmostEqual(result["presentation_fps"], 1)

    def test_rejects_submit_only_metrics(self):
        with self.assertRaisesRegex(RuntimeError, "display callbacks"):
            self.analyze("0,1,1,1.1,2,0,1\n1,2,2,2.1,2,0,1\n")


if __name__ == "__main__":
    unittest.main()
