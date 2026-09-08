#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Increase the native Fountain of Dreams reflection from 80x60 to 320x240."""

import argparse
from pathlib import Path


HERE = Path(__file__).resolve().parent
CHUNK = "chunks/chunk_0114_text1_801C9940.c"
MARKER = "// Native Fountain of Dreams reflection hooks v1"
HOOKS = (
    ("801CD2F4", "8033E78C", "melee_fountain_reserve_water"),
    ("801CCDD0", "80013B14", "melee_fountain_water_camera"),
    ("801CCE5C", "800121FC", "melee_fountain_water_image"),
)


def patch_chunk(source: str) -> str:
    include = '#include "../generated.h"\n'
    if source.count(include) != 1:
        raise ValueError("Unexpected generated header include.")
    for address, target, callback in HOOKS:
        label = f"label_{address}:\n"
        if source.count(label) != 1:
            raise ValueError(f"Expected one Fountain label at {address}.")
        if f"// {address}: bl      0x{target}" not in source:
            raise ValueError("The generated Fountain code is not Melee USA v1.02.")
        if MARKER in source and source.count(label + f"    {callback}(ctx);\n") != 1:
            raise ValueError(f"Incomplete Fountain hook at {address}.")
    if MARKER in source:
        return source
    result = source.replace(
        include, include + f'{MARKER}\n#include "../FountainWater.h"\n'
    )
    for address, _target, callback in HOOKS:
        label = f"label_{address}:\n"
        result = result.replace(label, label + f"    {callback}(ctx);\n")
    return result


def install(generated: Path) -> None:
    chunk = generated / CHUNK
    source = chunk.read_text()
    result = patch_chunk(source)
    header = (HERE / "FountainWater.h").read_bytes()
    destination = generated / "FountainWater.h"
    if not destination.exists() or destination.read_bytes() != header:
        destination.write_bytes(header)
    if result != source:
        chunk.write_text(result)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", type=Path, required=True)
    args = parser.parse_args()
    try:
        install(args.generated)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print("Installed native Fountain of Dreams reflection hooks.")


if __name__ == "__main__":
    main()
