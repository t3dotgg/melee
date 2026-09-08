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
static unsigned char ram[0x480000];
static int enabled;
int melee_stage_lighting_enabled(void) { return enabled; }
static unsigned char* pointer(u32 address) {
    assert(address >= 0x80000000U && address < 0x80480000U);
    return &ram[address - 0x80000000U];
}
static u32 mem_read8(CPUState* ctx, u32 address) {
    (void) ctx;
    return *pointer(address);
}
static u32 mem_read16(CPUState* ctx, u32 address) {
    return (mem_read8(ctx, address) << 8) | mem_read8(ctx, address + 1);
}
static u32 mem_read32(CPUState* ctx, u32 address) {
    return (mem_read16(ctx, address) << 16) | mem_read16(ctx, address + 2);
}
static void mem_write16(CPUState* ctx, u32 address, u16 value) {
    (void) ctx;
    *pointer(address) = value >> 8;
    *pointer(address + 1) = value;
}
static void mem_write32(CPUState* ctx, u32 address, u32 value) {
    mem_write16(ctx, address, value >> 16);
    mem_write16(ctx, address + 2, value);
}
#include "FountainWater.h"
static void stage(int quality, unsigned capacity, int expected_sharp) {
    CPUState ctx = {{0}};
    unsigned char* camera = pointer(MELEE_FOUNTAIN_CAMERA_DESC);
    const u32 entry = 0x80432124U + 3 * 0x1CU;
    memset(ram, 0, sizeof(ram));
    enabled = quality;
    /* Inputs at GXGetTexBufferSize, as emitted by grIzumi_801CD2D4. */
    ctx.gpr[3] = 80;
    ctx.gpr[4] = 60;
    ctx.gpr[5] = 4;
    melee_fountain_reserve_water(&ctx);
    assert(ctx.gpr[3] * 3 == ctx.gpr[4] * 4);
    assert(ctx.gpr[3] * ctx.gpr[4] == 80 * 60 * (quality ? 16 : 1));
    assert(ctx.gpr[5] == 4);
    /* Existing entries keep their old capacity despite a larger request. */
    if (capacity) {
        *pointer(entry) = 4;
        mem_write16(&ctx, entry + 6, 2001);
        mem_write16(&ctx, entry + 8, 9999);
        mem_write32(&ctx, entry + 12, capacity);
    }
    memset(camera, 0xA5, 0x38);
    ctx.gpr[3] = MELEE_FOUNTAIN_CAMERA_DESC;
    melee_fountain_water_camera(&ctx);
    unsigned width = mem_read16(&ctx, MELEE_FOUNTAIN_CAMERA_DESC + 0x0A);
    unsigned height = mem_read16(&ctx, MELEE_FOUNTAIN_CAMERA_DESC + 0x0E);
    assert(width * height == 80 * 60 * (expected_sharp ? 16 : 1));
    assert(!expected_sharp || !capacity || width * height * 2 <= capacity);
    assert(mem_read16(&ctx, MELEE_FOUNTAIN_CAMERA_DESC + 0x12) == width);
    assert(mem_read16(&ctx, MELEE_FOUNTAIN_CAMERA_DESC + 0x16) == height);
    for (unsigned i = 0; i < 0x38; i++) {
        if (i / 2 != 5 && i / 2 != 7 && i / 2 != 9 && i / 2 != 11)
            assert(camera[i] == 0xA5);
    }
    /* Camera and image stay in sync if a user toggles during construction. */
    enabled = !quality;
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
    stage(1, 320 * 240 * 2, 1);
    stage(0, 80 * 60 * 2, 0);
    stage(1, 80 * 60 * 2, 0);
    stage(0, 320 * 240 * 2, 0);
    stage(1, 0, 1);
    stage(1, 320 * 240 * 2, 1);
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
