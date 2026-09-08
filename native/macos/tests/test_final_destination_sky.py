# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "lighting/remaster_final_destination.py"
SPEC = importlib.util.spec_from_file_location("final_destination_sky", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SkyColorTests(unittest.TestCase):
    def test_draw_parser_keeps_vertex_indices_and_packed_alpha(self):
        # Two indexed positions/UVs with direct six-bit RGBA colors.
        original = bytes.fromhex("980002 1234 fedcba 56 789a bcdef0 12 000000")
        offsets = MODULE._color_offsets(original, 0, len(original), 6, (2,))
        self.assertEqual(offsets, [5, 11])
        output = bytearray(original)
        for offset in offsets:
            packed = int.from_bytes(original[offset:offset + 3], "big")
            recolored = MODULE._recolor_rgba6(packed, (0.0, 1.0, 2.0))
            output[offset:offset + 3] = recolored.to_bytes(3, "big")
            self.assertEqual(recolored & 63, packed & 63)
            self.assertEqual(recolored >> 18, 0)
            self.assertEqual((recolored >> 12) & 63, (packed >> 12) & 63)
        color_bytes = {offset + byte for offset in offsets for byte in range(3)}
        for index, byte in enumerate(original):
            if index not in color_bytes:
                self.assertEqual(output[index], byte)

    def test_every_alpha_value_survives_recoloring(self):
        for gains in MODULE.SKY_GAINS.values():
            for alpha in range(64):
                packed = (43 << 18) | (63 << 12) | (17 << 6) | alpha
                result = MODULE._recolor_rgba6(packed, gains)
                self.assertEqual(result & 63, alpha)
                self.assertLessEqual(result, 0xFFFFFF)

    def test_refuse_commands_and_vertices_outside_declared_list(self):
        for data, length in [
            (b"\x98\x00", 2),
            (b"\x98\x00\x02" + bytes(6), 9),
            (b"\x61" + bytes(31), 32),
            (bytes(31), 32),
        ]:
            with self.subTest(data=data), self.assertRaises(ValueError):
                MODULE._color_offsets(data, 0, length, 6, (2,))

    def test_refuse_unexpected_color_layout(self):
        for kind, fmt in [(2, 4), (3, 4), (1, 5)]:
            with self.subTest(kind=kind, fmt=fmt), self.assertRaises(ValueError):
                MODULE._attribute_size(11, kind, 1, fmt)
        descriptor = struct.pack(">4IBxHI", 11, 1, 1, 4, 0, 4, 0)
        with self.assertRaises(ValueError):
            MODULE._layout(descriptor * 33, 0)

    def test_refuse_other_game_data_before_writing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.dat"
            output = Path(directory) / "output.dat"
            source.write_bytes(bytes(64))
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(source), str(output)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("original US v1.02", result.stderr)
            self.assertFalse(output.exists())
            self.assertEqual(source.read_bytes(), bytes(64))

    def test_refuse_output_hard_link_to_original(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.dat"
            output = Path(directory) / "output.dat"
            source.write_bytes(b"original")
            output.hardlink_to(source)
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(source), str(output)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("separate output", result.stderr)
            self.assertEqual(source.read_bytes(), b"original")


if __name__ == "__main__":
    unittest.main()
