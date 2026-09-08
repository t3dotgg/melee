# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("c++"), "A C++ compiler is required")
class StageAssetTests(unittest.TestCase):
    def check_route(self, exported_setting):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            source = directory / "stage_assets.cpp"
            source.write_text(r'''
#include "MeleeStageAssets.h"
#include <cassert>
#include <fstream>

#ifdef EXPORTED_SETTING
static int live_setting = 1;
extern "C" __attribute__((visibility("default")))
int MeleeStageLightingEnabled() { return live_setting; }
#endif

static void create_archive(const std::filesystem::path& path,
                            std::uintmax_t size = 611125)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.seekp(size - 1);
    output.put(0);
    assert(output.good());
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    const auto resources = std::filesystem::path(argv[1]) / "Test.app/Contents/Resources";
    const auto original = resources / "Game/files/GrNLa.dat";
    const auto replacement = resources / "Lighting/GrNLa.dat";
    create_archive(original);
    create_archive(replacement);
    const auto route = [&] { return MeleeStageAssets::Replacement(original.string()); };

    unsetenv("MELEE_APP_BUNDLE");
    unsetenv("MELEE_STAGE_LIGHTING");
    assert(!route());
    setenv("MELEE_APP_BUNDLE", "1", 1);
    assert(route() == replacement.string());
    assert(!MeleeStageAssets::Replacement("/Game/files/GrNLa.dat"));
    assert(!MeleeStageAssets::Replacement((resources / "Game/files/GrIz.dat").string()));
    assert(!MeleeStageAssets::Replacement((resources / "Game/files/xGrNLa.dat").string()));
    assert(!MeleeStageAssets::Replacement((resources / "Other/files/GrNLa.dat").string()));

#ifdef EXPORTED_SETTING
    // The exported live switch takes precedence over the launch setting.
    setenv("MELEE_STAGE_LIGHTING", "1", 1);
    live_setting = 0;
    assert(!route());
    live_setting = -1;
    assert(!route());
    setenv("MELEE_STAGE_LIGHTING", "0", 1);
    live_setting = 1;
    assert(route() == replacement.string());
#else
    setenv("MELEE_STAGE_LIGHTING", "0", 1);
    assert(!route());
    setenv("MELEE_STAGE_LIGHTING", "1", 1);
    assert(route() == replacement.string());
#endif

    std::filesystem::remove(replacement);
    assert(!route());
    create_archive(replacement, 611124);
    assert(!route());
    create_archive(replacement);
    create_archive(original, 611126);
    assert(!route());
    create_archive(original);
    assert(route() == replacement.string());
    // Route selection only reads file metadata.
    assert(std::filesystem::file_size(original) == 611125);
}
''')
            executable = directory / "stage_assets"
            flags = ["-Wl,-export_dynamic"] if sys.platform == "darwin" else ["-rdynamic", "-ldl"]
            if exported_setting:
                flags.append("-DEXPORTED_SETTING")
            subprocess.run(
                ["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I",
                    str(ROOT / "lighting"), str(source), "-o", str(executable), *flags],
                check=True,
                capture_output=True,
                text=True,
            )
            result = subprocess.run(
                [str(executable), str(directory)], capture_output=True, text=True
            )
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_bundled_route_with_environment_setting(self):
        self.check_route(exported_setting=False)

    def test_bundled_route_with_live_setting(self):
        self.check_route(exported_setting=True)


if __name__ == "__main__":
    unittest.main()
