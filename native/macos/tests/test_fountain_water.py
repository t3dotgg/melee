# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "fountain_water", ROOT / "lighting/fountain_water.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class InstallTests(unittest.TestCase):
    def fixture(self):
        return '#include "../generated.h"\n' + "".join(
            f"label_{address}:\n// {address}: bl      0x{target}\n"
            for address, target, _callback in MODULE.HOOKS
        )

    def test_repeat_install_keeps_one_hook_per_call(self):
        patched = MODULE.patch_chunk(self.fixture())
        self.assertEqual(MODULE.patch_chunk(patched), patched)
        for _address, _target, callback in MODULE.HOOKS:
            self.assertEqual(patched.count(f"{callback}(ctx);"), 1)

    def test_reject_wrong_revision_and_incomplete_hooks(self):
        patched = MODULE.patch_chunk(self.fixture())
        for source in (
            self.fixture().replace("0x800121FC", "0x800122C8"),
            self.fixture().replace("label_801CCDD0:\n", ""),
            self.fixture() + "label_801CD2F4:\n",
            patched.replace("    melee_fountain_water_image(ctx);\n", ""),
        ):
            with self.subTest(source=source), self.assertRaises(ValueError):
                MODULE.patch_chunk(source)


class WaterTests(unittest.TestCase):
    def test_resolution_changes_keep_camera_image_and_capacity_together(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("A C compiler is required for the reflection test.")
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "test.c"
            executable = Path(directory) / "test"
            source.write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uint16_t u16;
typedef uint32_t u32;
typedef struct CPUState { u32 gpr[32]; } CPUState;
static unsigned char camera[0x38];
static int enabled;
int melee_stage_lighting_enabled(void) { return enabled; }
static void mem_write16(CPUState* ctx, u32 address, u16 value) {
    (void) ctx;
    assert(address + 2 <= sizeof(camera));
    camera[address] = value >> 8;
    camera[address + 1] = value;
}
static u16 read16(unsigned address) {
    return ((u16) camera[address] << 8) | camera[address + 1];
}
#include "FountainWater.h"
static void stage(int quality) {
    CPUState ctx = {{0}};
    enabled = quality;
    /* Inputs at GXGetTexBufferSize, as emitted by grIzumi_801CD2D4. */
    ctx.gpr[3] = 80;
    ctx.gpr[4] = 60;
    ctx.gpr[5] = 4;
    melee_fountain_reserve_water(&ctx);
    unsigned width = ctx.gpr[3], height = ctx.gpr[4];
    assert(width * 3 == height * 4);
    assert(width * height == 80 * 60 * (quality ? 16 : 1));
    assert(ctx.gpr[5] == 4);
    /* Toggling during loading cannot grow beyond the reserved allocation. */
    enabled = !quality;
    memset(camera, 0xA5, sizeof(camera));
    ctx.gpr[3] = 0;
    melee_fountain_water_camera(&ctx);
    assert(read16(0x0A) == width && read16(0x0E) == height);
    assert(read16(0x12) == width && read16(0x16) == height);
    for (unsigned i = 0; i < sizeof(camera); i++) {
        if (i / 2 != 5 && i / 2 != 7 && i / 2 != 9 && i / 2 != 11)
            assert(camera[i] == 0xA5);
    }
    ctx.gpr[3] = 0x80008000;
    ctx.gpr[4] = 80;
    ctx.gpr[5] = 60;
    ctx.gpr[6] = 4;
    ctx.gpr[7] = 2001;
    melee_fountain_water_image(&ctx);
    assert(ctx.gpr[4] == width && ctx.gpr[5] == height);
    assert(ctx.gpr[3] == 0x80008000 && ctx.gpr[6] == 4 && ctx.gpr[7] == 2001);
}
int main(void) {
    stage(1);
    stage(0);
    stage(1);
    return 0;
}
''')
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT / "lighting"), str(source), "-o", str(executable)],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
