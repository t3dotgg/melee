# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("high_refresh", ROOT / "high_refresh.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class InstallTests(unittest.TestCase):
    def fixture(self):
        return (
            '#include "../generated.h"\n'
            'label_801A4D34:\n'
            'label_801A5034:\n// 801A5034: bl      0x8033C898\n'
            '// 801A5054: bl      0x803761C0\n'
            'label_801A5058:\n'
        )

    def test_install_is_idempotent(self):
        patched = MODULE.patch_chunk(self.fixture())
        self.assertEqual(MODULE.patch_chunk(patched), patched)

    def test_reject_missing_and_duplicate_labels(self):
        for source in (
            self.fixture().replace("label_801A5058:\n", ""),
            self.fixture() + "label_801A5058:\n",
            self.fixture().replace("0x803761C0", "0x80375538"),
        ):
            with self.subTest(source=source), self.assertRaises(ValueError):
                MODULE.patch_chunk(source)

    def test_reject_partial_install(self):
        patched = MODULE.patch_chunk(self.fixture())
        with self.assertRaises(ValueError):
            MODULE.patch_chunk(patched.replace("    melee_refresh_reset();\n", ""))


class PoseTests(unittest.TestCase):
    def test_prediction_restoration_and_discontinuities(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("A C compiler is required for the native pose test.")
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "test.c"
            executable = Path(directory) / "test"
            source.write_text(r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct CPUState { int unused; } CPUState;
static unsigned char ram[0x1800000];
static u32 mem_read8(CPUState* ctx, u32 address) {
    (void)ctx;
    return ram[address - 0x80000000U];
}
static u32 mem_read16(CPUState* ctx, u32 address) {
    return (mem_read8(ctx, address) << 8) | mem_read8(ctx, address + 1);
}
static u32 mem_read32(CPUState* ctx, u32 address) {
    return (mem_read16(ctx, address) << 16) | mem_read16(ctx, address + 2);
}
static void mem_write32(CPUState* ctx, u32 address, u32 value) {
    (void)ctx;
    for (unsigned i = 0; i < 4; i++)
        ram[address - 0x80000000U + i] = value >> (24 - i * 8);
}
#include "MeleeHighRefresh.h"
static void tick(CPUState* ctx, unsigned frame, float x, unsigned action) {
    mem_write32(ctx, 0x80479D58U, frame);
    mem_write32(ctx, 0x80007010U, action);
    mem_write32(ctx, 0x80006038U, melee_bits(x));
    mem_write32(ctx, 0x8000A00CU, melee_bits(x * 0.5f));
}
int main(int argc, char** argv) {
    CPUState ctx = {0};
    (void)argv;
    melee_refresh_reset();
    /* One fighter, one bone, and one camera with its eye and interest. */
    mem_write32(&ctx, 0x804D782CU, 0x80004000U);
    mem_write32(&ctx, 0x80004000U, 0x80005000U);
    ram[0x4CE380] = 0;
    ram[0x4D7849] = 1;
    ram[0x4D784B] = 2;
    ram[0x5001] = 4;
    ram[0x5006] = 1;
    mem_write32(&ctx, 0x80005028U, 0x80006000U);
    mem_write32(&ctx, 0x8000502CU, 0x80007000U);
    mem_write32(&ctx, 0x80006000U, 0x80008000U);
    mem_write32(&ctx, 0x80005008U, 0x80005100U);
    ram[0x5106] = 2;
    mem_write32(&ctx, 0x80005128U, 0x80009000U);
    mem_write32(&ctx, 0x80009024U, 0x8000A000U);
    mem_write32(&ctx, 0x80009028U, 0x8000A100U);
    mem_write32(&ctx, 0x8000A000U, 0x80008000U);
    mem_write32(&ctx, 0x8000A100U, 0x80008000U);
    for (unsigned i = 0; i < 3; i++)
        mem_write32(&ctx, 0x8000602CU + i * 4, melee_bits(1.0f));
    tick(&ctx, 1, 10.0f, 0);
    if (argc > 1) {
        assert(melee_refresh_finish(&ctx) == 0);
        assert(mem_read32(&ctx, 0x80006038U) == melee_bits(10.0f));
        return 0;
    }
    assert(melee_refresh_finish(&ctx) == 1);
    assert(melee_refresh_finish(&ctx) == 0);
    tick(&ctx, 2, 14.0f, 0);
    assert(melee_refresh_finish(&ctx) == 1);
    assert(mem_read32(&ctx, 0x80006038U) == melee_bits(16.0f));
    assert(mem_read32(&ctx, 0x80479D58U) == 2);
    assert(mem_read32(&ctx, 0x8000A00CU) == melee_bits(8.0f));
    /* The draw recomputes matrices. Direct game readers must get originals. */
    mem_write32(&ctx, 0x80006044U, 0xBADU);
    mem_write32(&ctx, 0x80009054U, 0xBADU);
    assert(melee_refresh_finish(&ctx) == 0);
    assert(mem_read32(&ctx, 0x80006044U) == 0);
    assert(mem_read32(&ctx, 0x80009054U) == 0);
    assert(mem_read32(&ctx, 0x80006038U) == melee_bits(14.0f));
    assert(mem_read32(&ctx, 0x80006014U) & (1U << 6));
    assert(mem_read32(&ctx, 0x8000A00CU) == melee_bits(7.0f));
    assert(mem_read32(&ctx, 0x80009008U) & 0xC0000000U);
    /* A new action and a rewind discard history. */
    tick(&ctx, 3, 18.0f, 1);
    assert(melee_refresh_finish(&ctx) == 1);
    assert(mem_read32(&ctx, 0x80006038U) == melee_bits(18.0f));
    assert(melee_refresh_finish(&ctx) == 0);
    tick(&ctx, 1, 10.0f, 1);
    assert(melee_refresh_finish(&ctx) == 1);
    assert(mem_read32(&ctx, 0x80006038U) == melee_bits(10.0f));
    assert(melee_refresh_finish(&ctx) == 0);
    /* Teleports do not overshoot and angles cross pi on the short arc. */
    float a[10] = {3.13f, 0, 0, 0, 1, 1, 1, 0, 0, 0};
    float b[10] = {-3.13f, 0, 0, 0, 1, 1, 1, 0, 0, 0};
    float result[10];
    assert(melee_predict(result, b, a, 10, 1));
    assert(fabsf(result[0] - b[0]) < 0.02f);
    b[7] = 100.0f;
    assert(!melee_predict(result, b, a, 10, 1));
    b[7] = NAN;
    assert(!melee_predict(result, b, a, 10, 1));
    /* Render-time removal must not restore into reused object storage. */
    tick(&ctx, 2, 14.0f, 1);
    assert(melee_refresh_finish(&ctx) == 1);
    mem_write32(&ctx, 0x80006000U, 0);
    mem_write32(&ctx, 0x80006038U, 123);
    assert(melee_refresh_finish(&ctx) == 0);
    assert(mem_read32(&ctx, 0x80006038U) == 123);
    puts("poses predict, restore, and reject discontinuities");
    return 0;
}
''')
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Wno-unused-function",
                 "-I", str(ROOT / "refresh"), str(source), "-lm", "-o", str(executable)],
                check=True, capture_output=True, text=True,
            )
            for fps in ("120", "60"):
                env = dict(os.environ, MELEE_RENDER_FPS=fps)
                subprocess.run(
                    [str(executable), *([] if fps == "120" else ["disabled"])],
                    env=env, check=True, capture_output=True, text=True,
                )


if __name__ == "__main__":
    unittest.main()
