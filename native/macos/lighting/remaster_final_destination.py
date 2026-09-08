#!/usr/bin/env python3
"""Recolor the original Final Destination sky in a separate build asset.

This keeps the archive layout, geometry, texture data, alpha, and animation.
Only direct GX_RGBA6 color bytes in sky display lists change. The source
archive must be the unmodified US v1.02 GrNLa.dat. Do not commit either DAT.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

ORIGINAL_SHA256 = "2a295a733414fb65a7061a5c092144fb197570682562b87ba5567ff3f08abb02"

# These map IDs and layer names come from GrNLa.dat and grLast_8021B920.
SKY_GAINS = {
    4: (0.84, 0.91, 1.12),  # Milky Way and stars.
    5: (0.76, 1.12, 1.20),  # Energy lines.
    6: (0.68, 0.85, 1.10),  # Walls in the tunnel.
    7: (0.80, 0.94, 1.18),  # Rotating energy sphere.
    8: (0.98, 1.08, 1.12),  # Clouds and distant ground.
    9: (0.82, 0.94, 1.10),  # Ripples in the later sky phase.
}


def _u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ValueError("Archive pointer is outside the data section")
    return struct.unpack_from(">I", data, offset)[0]


def _attribute_size(attr: int, kind: int, count: int, fmt: int) -> int:
    if kind == 0:
        return 0
    if kind in (2, 3):
        if attr in (11, 12):
            raise ValueError("Expected direct sky colors, found indexed colors")
        return (kind - 1) * (3 if attr == 10 and count == 2 else 1)
    if kind != 1:
        raise ValueError("Unknown vertex attribute type")
    if 0 <= attr <= 8:
        return 1
    if attr in (11, 12):
        if fmt != 4 or count != 1:
            raise ValueError("Expected direct GX_RGBA6 sky colors")
        return 3
    if fmt > 4 or count > 2:
        raise ValueError("Unknown vertex component format")
    unit = (1, 1, 2, 2, 4)[fmt]
    if attr == 9:
        return unit * (2 if count == 0 else 3)
    if attr == 10:
        return unit * (3 if count == 0 else 9)
    if 13 <= attr <= 20:
        return unit * (1 if count == 0 else 2)
    raise ValueError("Unknown direct vertex attribute")


def _layout(data: bytes, address: int) -> tuple[int, tuple[int, ...]]:
    attributes = []
    seen = set()
    for index in range(32):
        cursor = address + index * 24
        attr = _u32(data, cursor)
        if attr == 255:
            break
        if attr in seen or attr > 20:
            raise ValueError("Invalid vertex attribute list")
        seen.add(attr)
        attributes.append((attr, *struct.unpack_from(">3I", data, cursor + 4)))
    else:
        raise ValueError("Unterminated vertex attribute list")
    stride = 0
    colors = []
    for attr, kind, count, fmt in sorted(attributes):
        if attr in (11, 12) and kind:
            colors.append(stride)
        stride += _attribute_size(attr, kind, count, fmt)
    if not stride:
        raise ValueError("Empty vertex layout")
    return stride, tuple(colors)


def _color_offsets(
    data: bytes, start: int, length: int, stride: int, colors: tuple[int, ...]
) -> list[int]:
    if start < 0 or start + length > len(data) or stride <= 0:
        raise ValueError("Invalid display list bounds")
    cursor = start
    end = start + length
    result = []
    while cursor < end:
        command = data[cursor]
        if command == 0:
            cursor += 1
            continue
        # HSD_PObj uses format zero. Its lists contain draw commands and NOPs.
        if command not in (0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8):
            raise ValueError(f"Unexpected display list command 0x{command:02x}")
        if cursor + 3 > end:
            raise ValueError("Truncated draw command")
        count = struct.unpack_from(">H", data, cursor + 1)[0]
        cursor += 3
        if cursor + count * stride > end:
            raise ValueError("Draw command crosses display list bounds")
        for vertex in range(count):
            result.extend(cursor + vertex * stride + color for color in colors)
        cursor += count * stride
    return result


def _recolor_rgba6(packed: int, gains: tuple[float, float, float]) -> int:
    result = packed & 63
    for shift, gain in zip((18, 12, 6), gains):
        channel = (packed >> shift) & 63
        result |= max(0, min(63, round(channel * gain))) << shift
    return result


def remaster(data: bytes) -> tuple[bytes, dict[str, object]]:
    if hashlib.sha256(data).hexdigest() != ORIGINAL_SHA256:
        raise ValueError("Expected the original US v1.02 GrNLa.dat")
    size, data_size, relocations, public, external = struct.unpack_from(">5I", data)
    if size != len(data):
        raise ValueError("Archive size does not match the file")
    body = data[32 : 32 + data_size]
    symbols_start = 32 + data_size + relocations * 4
    names_start = symbols_start + (public + external) * 8
    map_head = None
    for index in range(public):
        address, name = struct.unpack_from(">II", data, symbols_start + index * 8)
        name_start = names_start + name
        name_end = data.index(b"\0", name_start)
        if data[name_start:name_end] == b"map_head":
            map_head = address
    if map_head is None or _u32(body, map_head + 12) != 10:
        raise ValueError("Expected the ten Final Destination map entries")
    maps = _u32(body, map_head + 8)
    changes: dict[int, tuple[float, float, float]] = {}
    map_counts = {}
    for map_id, gains in SKY_GAINS.items():
        joints = [_u32(body, maps + map_id * 0x34)]
        visited_joints = set()
        visited_polygons = set()
        map_colors = set()
        while joints:
            joint = joints.pop()
            if not joint:
                continue
            if joint in visited_joints:
                raise ValueError("Cycle in the sky joint tree")
            visited_joints.add(joint)
            joints.extend((_u32(body, joint + 8), _u32(body, joint + 12)))
            if _u32(body, joint + 4) & ((1 << 5) | (1 << 14)):
                continue
            display = _u32(body, joint + 16)
            visited_displays = set()
            while display:
                if display in visited_displays:
                    raise ValueError("Cycle in the display object list")
                visited_displays.add(display)
                polygon = _u32(body, display + 12)
                while polygon:
                    if polygon in visited_polygons:
                        raise ValueError("Shared or cyclic sky polygon")
                    visited_polygons.add(polygon)
                    stride, colors = _layout(body, _u32(body, polygon + 8))
                    length = struct.unpack_from(">H", body, polygon + 14)[0] * 32
                    start = _u32(body, polygon + 16)
                    for color in _color_offsets(body, start, length, stride, colors):
                        previous = changes.setdefault(color, gains)
                        if previous != gains:
                            raise ValueError("Sky maps share a color with different gains")
                        map_colors.add(color)
                    polygon = _u32(body, polygon + 4)
                display = _u32(body, display + 4)
        map_counts[str(map_id)] = len(map_colors)
    output = bytearray(data)
    changed_colors = 0
    for offset, gains in sorted(changes.items()):
        original = int.from_bytes(body[offset : offset + 3], "big")
        color = _recolor_rgba6(original, gains)
        output[32 + offset : 35 + offset] = color.to_bytes(3, "big")
        changed_colors += color != original
    report = {
        "source_sha256": ORIGINAL_SHA256,
        "output_sha256": hashlib.sha256(output).hexdigest(),
        "vertex_colors": len(changes),
        "changed_colors": changed_colors,
        "map_colors": map_counts,
        "bytes": len(output),
    }
    return bytes(output), report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve() or (
        args.source.exists()
        and args.output.exists()
        and args.source.samefile(args.output)
    ):
        parser.error("Use a separate output file. The source must stay unchanged.")
    try:
        output, report = remaster(args.source.read_bytes())
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"Final Destination sky: {error}\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
