"""Compile and test the exact timing helpers shipped in frame-timing.patch."""

from __future__ import annotations

import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
PATCH = ROOT / "native/macos/patches/frame-timing.patch"


def extract_headers(destination: Path) -> None:
    """Apply only new helper headers, with no runtime checkout or game data."""
    subprocess.run(
        [
            "git", "apply", "--include=Source/Core/Common/FixedFrameTiming.h",
            "--include=Source/Core/Common/MacFrameTimer.h", str(PATCH),
        ],
        cwd=destination,
        check=True,
        capture_output=True,
        text=True,
    )


@unittest.skipUnless(shutil.which(os.environ.get("CXX", "clang++")), "C++ compiler required")
class FrameTimingTests(unittest.TestCase):
    def compile_and_run(self, source: str) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            extract_headers(directory)
            source_path = directory / "test.cpp"
            source_path.write_text(source)
            executable = directory / "test"
            subprocess.run(
                [
                    os.environ.get("CXX", "clang++"), "-std=c++20", "-O2",
                    "-Wall", "-Wextra", "-Werror",
                    "-I", str(directory / "Source/Core"),
                    str(source_path), "-o", str(executable),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(executable)], check=True, timeout=15)

    def test_full_frame_timing_has_no_rounding_drift(self) -> None:
        self.compile_and_run(r'''
#include "Common/FixedFrameTiming.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>

int main()
{
  for (const auto cpu : {486000000u, 729000000u})
  {
    for (const auto fps : {60u, 120u})
    {
      for (const auto lines : {1050u, 1052u, 1250u, 1049u})
      {
        std::uint64_t total = 0;
        std::uint32_t shortest = cpu;
        std::uint32_t longest = 0;
        for (std::uint32_t line = 0; line < lines; ++line)
        {
          const auto ticks = Common::FixedFrameTickOffset(line + 1, lines, fps, cpu) -
                             Common::FixedFrameTickOffset(line, lines, fps, cpu);
          assert(ticks > 0);
          shortest = std::min(shortest, ticks);
          longest = std::max(longest, ticks);
          total += ticks;
        }
        assert(longest - shortest <= 1);
        assert(total == cpu * 2u / fps);
        // A complete minute still matches the game timer, including each wrap.
        assert(total * (fps * 60u / 2u) == std::uint64_t(cpu) * 60u);
        // Both fields together match one original simulation update at 120 Hz.
        if (fps == 120)
          assert(total == cpu / 60);
      }
    }
  }
  assert(Common::FixedFrameTickOffset(1, 0, 120, 486000000) == 0);
  assert(Common::FixedFrameTickOffset(1, 1050, 0, 486000000) == 0);
}
''')

    @unittest.skipUnless(platform.system() == "Darwin", "Mach timer requires macOS")
    def test_mac_wait_does_not_return_before_deadline(self) -> None:
        self.compile_and_run(r'''
#include "Common/MacFrameTimer.h"
#include <cassert>

int main()
{
  using Clock = std::chrono::steady_clock;
  Common::SleepUntilMacFrame(Clock::now() - std::chrono::seconds{1});
  for (const auto wait : {0, 20, 200, 1000, 8333, 16667})
  {
    const auto target = Clock::now() + std::chrono::microseconds{wait};
    Common::SleepUntilMacFrame(target);
    assert(Clock::now() >= target);
  }
}
''')

    @unittest.skipUnless(platform.system() == "Darwin", "Mach timer requires macOS")
    def test_rejected_kernel_timer_uses_fallback(self) -> None:
        self.compile_and_run(r'''
#include <sys/event.h>
#include <cerrno>
#include <cassert>

static int rejected = 0;
static int RejectTimer(int, const kevent64_s*, int, kevent64_s* result, int,
                       unsigned int, const timespec*)
{
  ++rejected;
  result->flags = EV_ERROR;
  result->data = EINVAL;
  return 1;
}
#define kevent64 RejectTimer
#include "Common/MacFrameTimer.h"
#undef kevent64

int main()
{
  using Clock = std::chrono::steady_clock;
  const auto target = Clock::now() + std::chrono::milliseconds{2};
  Common::SleepUntilMacFrame(target);
  assert(Clock::now() >= target);
  assert(rejected > 0);
  // A rejected timer must fall back to sleeping, not retry in a busy loop.
  assert(rejected < 10);
}
''')


if __name__ == "__main__":
    if sys.argv[1:] == ["--benchmark"]:
        if platform.system() != "Darwin":
            raise SystemExit("The timer benchmark requires macOS.")
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            extract_headers(directory)
            executable = directory / "benchmark"
            subprocess.run(
                [
                    os.environ.get("CXX", "clang++"), "-x", "objective-c++",
                    "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I", str(directory / "Source/Core"),
                    str(Path(__file__).with_name("benchmark_timer.cpp")),
                    "-framework", "Foundation", "-o", str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)
    else:
        unittest.main()
