import csv
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


RENDER = Path(__file__).resolve().parents[1] / "render"


@unittest.skipUnless(shutil.which("c++"), "A C++ compiler is required")
class MetalFrameLogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temporary.name)
        source = cls.directory / "frame_log.cpp"
        source.write_text(
            r'''
#include "MeleeMetalFrameLog.h"
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
    Metal::MeleeFrameLog log(argv[1], 2);
    log.RecordPresent(0, 10.0);
    log.NextFrame();
    log.RecordPresent(1, 10.01);
    log.NextFrame();
    // The cap must not overwrite earlier frames or allocate more records.
    log.RecordPresent(log.CurrentFrame(), 99.0);
    log.NextFrame();
    if (log.CanRecord() || log.CurrentFrame() != 2)
    return 2;

    // Completion callbacks can arrive after later frames have been submitted.
    std::vector<std::thread> callbacks;
    for (int i = 0; i < 4; ++i)
    {
    callbacks.emplace_back([&] {
        for (int j = 0; j < 1000; ++j)
        log.RecordGPU(0, 10.0, 10.000001);
    });
    }
    callbacks.emplace_back([&] { log.RecordDisplay(1, 10.02); });
    callbacks.emplace_back([&] { log.RecordDisplay(0, 10.01); });
    for (auto& callback : callbacks)
    callback.join();
    log.RecordGPU(1, 10.012, 10.013);
    log.RecordGPU(1, 10.010, 10.011);
    log.RecordGPU(1, 0, 0);
    log.RecordGPU(1, 20, 19);
    return log.Write() ? 0 : 1;
}
'''
        )
        cls.executable = cls.directory / "frame_log"
        subprocess.run(
            ["c++", "-std=c++20", "-pthread", "-I", str(RENDER), str(source),
                "-o", str(cls.executable)],
            check=True,
            capture_output=True,
            text=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_concurrent_callbacks_keep_frame_identity_and_respect_cap(self):
        output = self.directory / "frames.csv"
        subprocess.run([self.executable, output], check=True)
        with output.open() as source:
            frames = list(csv.DictReader(source))
        self.assertEqual(len(frames), 2)
        first, second = frames
        self.assertEqual(int(first["gpu_submissions"]), 4000)
        self.assertAlmostEqual(float(first["gpu_busy_ms"]), 4.0, places=5)
        self.assertEqual(float(first["display_s"]), 10.01)
        self.assertEqual(float(second["submit_s"]), 10.01)
        self.assertEqual(float(second["display_s"]), 10.02)
        self.assertEqual(float(second["gpu_start_s"]), 10.010)
        self.assertEqual(float(second["gpu_end_s"]), 10.013)
        self.assertEqual(int(second["gpu_submissions"]), 2)
        self.assertAlmostEqual(float(second["gpu_busy_ms"]), 2.0)

    def test_output_failure_is_reported(self):
        result = subprocess.run(
            [self.executable, self.directory / "missing" / "frames.csv"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("Melee Metal frame log", result.stderr)


if __name__ == "__main__":
    unittest.main()
