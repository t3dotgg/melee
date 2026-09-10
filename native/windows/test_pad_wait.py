# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the native PAD wait predicate without executing or modifying game code."""

import hashlib
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location("windows_build", Path(__file__).with_name("build.py"))
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class PadWaitTests(unittest.TestCase):
    def test_instruction_guard_matches_verified_local_executable(self):
        original = BUILD.ROOT / "orig/GALE01/sys/main.dol"
        if not original.is_file():
            self.skipTest("The original local executable is optional for tool tests")
        dol = original.read_bytes()
        self.assertEqual(hashlib.sha1(dol).hexdigest(), BUILD.DOL_SHA1)
        sections = [tuple(struct.unpack_from(">I", dol, base + index * 4)[0]
                          for base in (0, 0x48, 0x90)) for index in range(18)]
        words = []
        for address in range(0x801A4DA8, 0x801A4DB8, 4):
            section = next((offset, start, length) for offset, start, length in sections
                           if start <= address < start + length)
            words.append(struct.unpack_from(">I", dol, section[0] + address - section[1])[0])
        self.assertEqual(words, [0x4BE74829, 0x4BE74AE9, 0x547B063F, 0x4182FFF4])

    @unittest.skipUnless(BUILD.platform.system() == "Windows", "Uses the Windows C++ toolchain")
    def test_wait_rejects_ready_input_exceptions_and_different_control_flow(self):
        try:
            env = BUILD.toolchain_environment()
        except RuntimeError as error:
            self.skipTest(str(error))
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            subprocess.run(["git", "init", "-q", str(temporary)], check=True)
            patch = Path(__file__).with_name("patches") / "pad-wait.patch"
            subprocess.run(["git", "apply", "--include=Source/Core/Core/PowerPC/StaticRecomp/MeleePadWait.h"],
                           input=patch.read_text().encode(), cwd=temporary, check=True)
            source = temporary / "pad_wait.cpp"
            source.write_text('''#include "Source/Core/Core/PowerPC/StaticRecomp/MeleePadWait.h"
using Melee::CanIdleForPadWait;
static_assert(CanIdleForPadWait(0x800195D0, 0x801A4DAC, 0, 0, 0x8000, 0, false, 1));
// A nonempty original result must execute the frame immediately.
static_assert(!CanIdleForPadWait(0x800195D0, 0x801A4DAC, 1, 0, 0x8000, 0, false, 1));
// An alarm can enqueue input after the original count was sampled.
static_assert(!CanIdleForPadWait(0x800195D0, 0x801A4DAC, 0, 1, 0x8000, 0, false, 1));
// Do not wait at the unconditional post-loop reset/DVD call or another function.
static_assert(!CanIdleForPadWait(0x800195D0, 0x801A4DBC, 0, 0, 0x8000, 0, false, 1));
static_assert(!CanIdleForPadWait(0x80019894, 0x801A4DAC, 0, 0, 0x8000, 0, false, 1));
// Interrupts must be enabled; pending or raised exceptions run immediately.
static_assert(!CanIdleForPadWait(0x800195D0, 0x801A4DAC, 0, 0, 0, 0, false, 1));
static_assert(!CanIdleForPadWait(0x800195D0, 0x801A4DAC, 0, 0, 0x8000, 1, false, 1));
static_assert(!CanIdleForPadWait(0x800195D0, 0x801A4DAC, 0, 0, 0x8000, 0, true, 1));
// CoreTiming::Idle must not charge an already expired slice again.
static_assert(!CanIdleForPadWait(0x800195D0, 0x801A4DAC, 0, 0, 0x8000, 0, false, 0));
static_assert(!CanIdleForPadWait(0x800195D0, 0x801A4DAC, 0, 0, 0x8000, 0, false, -1));
constexpr std::array<std::uint32_t, 4> words{0x4be74829, 0x4be74ae9, 0x547b063f, 0x4182fff4};
static_assert(Melee::IsPadWaitLoop(words));
constexpr bool ModifiedInstructionsAreRejected()
{
    for (unsigned index = 0; index < words.size(); ++index)
    {
        auto changed = words;
        changed[index] ^= 4;
        if (Melee::IsPadWaitLoop(changed))
            return false;
    }
    return true;
}
static_assert(ModifiedInstructionsAreRejected());
int main() { return 0; }
''')
            binary = temporary / "pad_wait.exe"
            BUILD.run("cl", "/nologo", "/std:c++20", source, "/Fe:" + str(binary), cwd=temporary, env=env)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
