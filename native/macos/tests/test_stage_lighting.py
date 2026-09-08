# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("stage_lighting", ROOT / "stage_lighting.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class InstallTests(unittest.TestCase):
    def fixture(self):
        return (
            MODULE.INCLUDE + "label_801A5034:\n"
            "// 801A5034: bl      0x8033C898\n"
            "// 801A5044: bl      0x80390FC0\n"
            "label_801A5048:\n"
            "label_801A5058:\n    if (melee_refresh_finish(ctx)) {}\n"
        )

    def test_render_transaction_is_idempotent_and_precedes_extra_render(self):
        result = MODULE.patch_loop(self.fixture())
        self.assertEqual(MODULE.patch_loop(result), result)
        self.assertLess(result.index("    melee_stage_lighting_finish(ctx);"),
                        result.index("    if (melee_refresh_finish(ctx))"))

    def test_reject_wrong_revision_missing_duplicate_and_partial_hooks(self):
        good = self.fixture()
        patched = MODULE.patch_loop(good)
        for source in (
            good.replace("0x80390FC0", "0x80390CFC"),
            good.replace("label_801A5048:\n", ""),
            good + "label_801A5034:\n",
            patched.replace("    melee_stage_lighting_finish(ctx);\n", ""),
        ):
            with self.subTest(source=source), self.assertRaises(ValueError):
                MODULE.patch_loop(source)

    def test_state_hook_is_idempotent_and_rejects_partial_install(self):
        source = "#include <time.h>\n" + MODULE.STATE_ENTRY + "}\n"
        result = MODULE.patch_state_header(source)
        self.assertEqual(MODULE.patch_state_header(result), result)
        with self.assertRaises(ValueError):
            MODULE.patch_state_header(result.replace(MODULE.STATE_HOOK, ""))

    def test_invalid_last_callback_does_not_write_earlier_chunks(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = Path(directory)
            (generated / "chunks").mkdir()
            sources = {
                MODULE.LOOP_CHUNK: self.fixture(),
                MODULE.GROUND_CHUNK: MODULE.INCLUDE + "label_801C4640:\n"
                "// 801C4650: bl      0x803668EC\n",
                MODULE.FIGHTER_CHUNK: MODULE.INCLUDE + "label_8009F54C:\n"
                "// 8009F55C: bl      0x803668EC\n",
                MODULE.GOBJ_CHUNK: MODULE.INCLUDE + "label_80391044:\n"
                "// Wrong original instruction\n",
            }
            for relative, source in sources.items():
                (generated / relative).write_text(source)
            with self.assertRaises(ValueError):
                MODULE.install(generated)
            for relative, source in sources.items():
                self.assertEqual((generated / relative).read_text(), source)

    def test_full_install_repeats_without_changing_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            generated = base / "generated"
            (generated / "chunks").mkdir(parents=True)
            (base / "lighting").mkdir()
            sources = {
                MODULE.LOOP_CHUNK: self.fixture(),
                MODULE.GROUND_CHUNK: MODULE.INCLUDE + "label_801C4640:\n"
                "// 801C4650: bl      0x803668EC\n",
                MODULE.FIGHTER_CHUNK: MODULE.INCLUDE + "label_8009F54C:\n"
                "// 8009F55C: bl      0x803668EC\n",
                MODULE.GOBJ_CHUNK: MODULE.INCLUDE + "label_80391044:\n"
                "// 80391054: bl      0x803668EC\n",
                "MeleeHighRefresh.h": "#include <time.h>\n" + MODULE.STATE_ENTRY + "}\n",
            }
            for relative, source in sources.items():
                (generated / relative).write_text(source)
            for name in MODULE.HEADERS:
                (base / "lighting" / name).write_text("// Test profile\n")
            with patch.object(MODULE, "HERE", base):
                MODULE.install(generated)
                outputs = {path: path.read_bytes() for path in generated.rglob("*")
                           if path.is_file()}
                MODULE.install(generated)
            for path, source in outputs.items():
                self.assertEqual(path.read_bytes(), source)


class LightingTests(unittest.TestCase):
    def test_source_fields_restore_and_unrelated_memory_is_unchanged(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("A C compiler is required for the native lighting test.")
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            # Fixed, visible colors isolate transaction behavior from art tuning.
            common = r'''
static void profile_lighting(MeleeStageLightingProfile* p, float frame) {
    (void)frame;
    p->ambient_rgb[0] = 64; p->ambient_rgb[1] = 88; p->ambient_rgb[2] = 112;
    p->key_rgb[0] = 240; p->key_rgb[1] = 180; p->key_rgb[2] = 96;
    p->fill_rgb[0] = 96; p->fill_rgb[1] = 160; p->fill_rgb[2] = 224;
    p->key_direction[0] = 1; p->key_direction[1] = 2; p->key_direction[2] = 3;
    p->fill_direction[0] = -1; p->fill_direction[1] = 2; p->fill_direction[2] = 3;
    p->light_mix = 1;
    p->fog_mix = 0.5f;
    p->fog_rgb[0] = 20; p->fog_rgb[1] = 40; p->fog_rgb[2] = 60;
}
static void profile_material(MeleeStageMaterialProfile* p, unsigned map, float frame) {
    (void)frame;
    if (map != 6) return;
    p->ambient_gain[0] = 0.5f;
    p->diffuse_gain[0] = 1.5f; p->diffuse_gain[1] = 0.5f;
    p->specular_gain[0] = 0.5f;
    p->shininess_gain = 2;
}
static unsigned melee_final_destination_diffuse(unsigned original, unsigned map) {
    if (map == 3 && (original >> 8) == 0xFF00FFU)
        return 0x3E8ED400U | (original & 255U);
    return original;
}
'''
            for index, (name, prefix) in enumerate((
                ("BattlefieldLighting.h", "battlefield"),
                ("FinalDestinationLighting.h", "final_destination"),
                ("FountainLighting.h", "fountain"),
            )):
                (base / name).write_text(
                    (common if index == 0 else "") +
                    f"#define melee_{prefix}_lighting profile_lighting\n"
                    f"#define melee_{prefix}_material profile_material\n"
                )
            # Quote includes resolve beside the common header first.
            shutil.copyfile(ROOT / "lighting/MeleeStageLighting.h", base / "MeleeStageLighting.h")
            source = base / "test.c"
            source.write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
typedef struct CPUState { u32 gpr[32]; u32 pc, lr, ctr, cr; } CPUState;
static unsigned char ram[0x1800000], original_ram[0x1800000];
static u32 mem_read8(CPUState* ctx, u32 address) {
    (void)ctx;
    assert(address >= 0x80000000U && address < 0x81800000U);
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
    assert(address >= 0x80000000U && address <= 0x817FFFFCU);
    for (unsigned i = 0; i < 4; i++) ram[address - 0x80000000U + i] = value >> (24 - i * 8);
}
#include "MeleeStageLighting.h"
static void scene(CPUState* ctx, unsigned kind) {
    memset(ram, 0, sizeof(ram));
    mem_write32(ctx, MELEE_STAGE_INFO + 0x88U, kind);
    mem_write32(ctx, MELEE_STAGE_FRAME, 100);
    mem_write32(ctx, MELEE_STAGE_MAPS + 6 * 4, 0x80005000U);
    ram[0x4D7849] = 1; ram[0x4D784A] = 2;
    ram[0x5006] = 1;
    mem_write32(ctx, 0x80005028U, 0x80006000U);
    mem_write32(ctx, 0x8000502CU, 0x80007000U);
    /* Ground.x0 is unrelated. Its map ID is +0x14 and its GObj is +4. */
    mem_write32(ctx, 0x80007000U, 0x12345678U);
    mem_write32(ctx, 0x80007004U, 0x80005000U);
    mem_write32(ctx, 0x80007014U, 6);
    mem_write32(ctx, 0x80006000U, 0x80004000U);
    mem_write32(ctx, 0x80006018U, 0x80008000U);
    mem_write32(ctx, 0x80008008U, 0x80009000U);
    /* Two draws share one material. It must get one gain, not two. */
    mem_write32(ctx, 0x80008004U, 0x80008100U);
    mem_write32(ctx, 0x80008108U, 0x80009000U);
    mem_write32(ctx, 0x80009000U, 0x80004100U);
    mem_write32(ctx, 0x80009004U, 0x1CU);
    mem_write32(ctx, 0x8000900CU, 0x8000A000U);
    mem_write32(ctx, 0x8000A000U, 0x80808011U);
    mem_write32(ctx, 0x8000A004U, 0x6496C822U);
    mem_write32(ctx, 0x8000A008U, 0x80402033U);
    mem_write32(ctx, 0x8000A00CU, melee_stage_bits(0.75f));
    mem_write32(ctx, 0x8000A010U, melee_stage_bits(20));
    /* Ambient, infinite key, point fill, and hidden lights. */
    ram[0xB006] = 2;
    mem_write32(ctx, 0x8000B028U, 0x8000C000U);
    for (unsigned i = 0; i < 4; i++) {
        u32 light = 0x8000C000U + i * 0x100;
        mem_write32(ctx, light, 0x80004200U);
        mem_write32(ctx, light + 8, ((i == 3 ? 0x25U : i + 4U) << 16));
        mem_write32(ctx, light + 0xC, i == 3 ? 0 : light + 0x100);
        mem_write32(ctx, light + 0x10, 0xA0B0C044U);
        mem_write32(ctx, light + 0x18, 0x8000D000U + i * 0x100);
        mem_write32(ctx, 0x8000D000U + i * 0x100, 0x80004300U);
        mem_write32(ctx, 0x8000D008U + i * 0x100, 1);
        mem_write32(ctx, 0x8000D00CU + i * 0x100, melee_stage_bits(20));
    }
    mem_write32(ctx, MELEE_STAGE_INFO + 0x12C, 0x8000E000U);
    mem_write32(ctx, 0x8000E028U, 0x8000F000U);
    mem_write32(ctx, 0x8000F000U, 0x80004400U);
    mem_write32(ctx, 0x8000F018U, 0x000000FFU);
}
int main(int argc, char** argv) {
    CPUState ctx, original_ctx;
    (void)argv;
    memset(&ctx, 0x5A, sizeof(ctx)); original_ctx = ctx;
    for (unsigned kind = 0; kind < 3; kind++) {
        unsigned kinds[3] = {0xC, 0x24, 0x25};
        scene(&ctx, kinds[kind]);
        memcpy(original_ram, ram, sizeof(ram));
        melee_stage_lighting_begin(&ctx);
        melee_stage_lighting_lights(&ctx, 0x8000B000U);
        if (argc > 1) {
            assert(!melee_stage_active);
        } else {
            assert(melee_stage_active && melee_stage_material_count == 2);
            assert(mem_read32(&ctx, 0x8000A004U) == 0x964BC822U);
            assert(mem_read32(&ctx, 0x8000A00CU) == melee_stage_bits(0.75f));
            assert(mem_read32(&ctx, 0x8000A010U) == melee_stage_bits(40));
            assert(mem_read32(&ctx, 0x8000C010U) == 0x40587044U);
            assert(mem_read32(&ctx, 0x8000C110U) == 0xF0B46044U);
            assert(mem_read32(&ctx, 0x8000C210U) == 0x60A0E044U);
            assert(mem_read32(&ctx, 0x8000C310U) == 0xA0B0C044U);
            assert(mem_read32(&ctx, 0x8000D10CU) == melee_stage_bits(1));
            assert(mem_read32(&ctx, 0x8000D108U) == 2);
            assert(mem_read32(&ctx, 0x8000D20CU) == melee_stage_bits(20));
            assert(mem_read32(&ctx, 0x8000D208U) == 1);
            assert(mem_read32(&ctx, 0x8000F018U) == 0x0A141EFFU);
            unsigned count = melee_stage_write_count;
            melee_stage_lighting_lights(&ctx, 0x8000B000U);
            assert(melee_stage_write_count == count);
        }
        melee_stage_lighting_finish(&ctx);
        assert(memcmp(ram, original_ram, sizeof(ram)) == 0);
        assert(memcmp(&ctx, &original_ctx, sizeof(ctx)) == 0);
    }
    if (argc > 1) return 0;
    /* The authored magenta trim gets a palette change only on FD map 3.
     * Vertex colors, textured/lit materials, and translucent effects retain
     * their source colors. A runtime toon flag must not hide the trim.
     */
    for (unsigned stage = 0; stage < 2; stage++) {
        unsigned modes[] = {1, 0x1001, 0x11, 0x12, 0x1C, 0x60000001U};
        for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
            scene(&ctx, stage ? 0x24 : 0x25);
            mem_write32(&ctx, MELEE_STAGE_MAPS + 6 * 4, 0);
            mem_write32(&ctx, MELEE_STAGE_MAPS + 3 * 4, 0x80005000U);
            mem_write32(&ctx, 0x80007014U, 3);
            mem_write32(&ctx, 0x80009004U, modes[i]);
            mem_write32(&ctx, 0x8000A004U, 0xFF00FF33U);
            memcpy(original_ram, ram, sizeof(ram));
            melee_stage_lighting_begin(&ctx);
            assert(mem_read32(&ctx, 0x8000A004U) ==
                   (stage == 0 && i < 2 ? 0x3E8ED433U : 0xFF00FF33U));
            melee_stage_lighting_finish(&ctx);
            assert(memcmp(ram, original_ram, sizeof(ram)) == 0);
        }
    }
    scene(&ctx, 0x25);
    mem_write32(&ctx, 0x80009004U, 1);
    mem_write32(&ctx, 0x8000A004U, 0xFF00FF33U);
    melee_stage_lighting_begin(&ctx);
    assert(mem_read32(&ctx, 0x8000A004U) == 0xFF00FF33U);
    melee_stage_lighting_finish(&ctx);
    /* A state save strips temporary fields, and loading clears the snapshot. */
    scene(&ctx, 0x24); memcpy(original_ram, ram, sizeof(ram));
    melee_stage_lighting_begin(&ctx);
    melee_stage_lighting_lights(&ctx, 0x8000B000U);
    melee_stage_lighting_prepare_state(&ctx, 0);
    assert(memcmp(ram, original_ram, sizeof(ram)) == 0);
    melee_stage_lighting_begin(&ctx);
    melee_stage_lighting_prepare_state(&ctx, 1);
    mem_write32(&ctx, 0x8000A004U, 0x11223344U);
    melee_stage_lighting_finish(&ctx);
    assert(mem_read32(&ctx, 0x8000A004U) == 0x11223344U);
    /* Render callbacks can replace a material or overwrite an animated color. */
    scene(&ctx, 0x24); melee_stage_lighting_begin(&ctx);
    mem_write32(&ctx, 0x8000A004U, 0x12345678U);
    melee_stage_lighting_finish(&ctx);
    assert(mem_read32(&ctx, 0x8000A004U) == 0x12345678U);
    scene(&ctx, 0x24); melee_stage_lighting_begin(&ctx);
    mem_write32(&ctx, 0x80009000U, 0x80009900U);
    melee_stage_lighting_finish(&ctx);
    assert(mem_read32(&ctx, 0x8000A004U) == 0x964BC822U);
    /* A missed load notification must not restore over another simulation frame. */
    scene(&ctx, 0x24); melee_stage_lighting_begin(&ctx);
    mem_write32(&ctx, MELEE_STAGE_FRAME, 20);
    melee_stage_lighting_finish(&ctx);
    assert(mem_read32(&ctx, 0x8000A004U) == 0x964BC822U);
    /* Stale scene ID, invalid Ground links, unsupported stages, particle union. */
    scene(&ctx, 0x24);
    mem_write32(&ctx, 0x80007014U, 7);
    memcpy(original_ram, ram, sizeof(ram)); melee_stage_lighting_begin(&ctx);
    assert(!melee_stage_active && memcmp(ram, original_ram, sizeof(ram)) == 0);
    scene(&ctx, 0x10);
    memcpy(original_ram, ram, sizeof(ram)); melee_stage_lighting_begin(&ctx);
    assert(!melee_stage_active && memcmp(ram, original_ram, sizeof(ram)) == 0);
    scene(&ctx, 0x24);
    mem_write32(&ctx, 0x80006014U, 1U << 5);
    melee_stage_lighting_begin(&ctx);
    assert(melee_stage_material_count == 0);
    melee_stage_lighting_finish(&ctx);
    /* Damaged links terminate and do not dereference invalid guest pointers. */
    scene(&ctx, 0x24);
    mem_write32(&ctx, 0x80006010U, 0x80006000U);
    mem_write32(&ctx, 0x80008004U, 0xFFFFFFFFU);
    melee_stage_lighting_begin(&ctx);
    melee_stage_lighting_finish(&ctx);
    puts("lighting restores colors and directions without changing game registers");
    return 0;
}
''')
            executable = base / "test"
            flags = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                     "-Wno-unused-function", str(source), "-lm", "-o", str(executable)]
            if os.uname().sysname != "Darwin":
                flags.append("-ldl")
            subprocess.run(flags, check=True, capture_output=True, text=True)
            for setting in ("1", "0", "unset"):
                env = dict(os.environ, MELEE_STAGE_LIGHTING=setting)
                if setting == "unset":
                    env.pop("MELEE_STAGE_LIGHTING")
                subprocess.run(
                    [str(executable), *(["disabled"] if setting == "0" else [])],
                    env=env, check=True, capture_output=True, text=True,
                )


if __name__ == "__main__":
    unittest.main()
