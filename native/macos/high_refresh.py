#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Add a second visual update to the generated Melee USA v1.02 game loop."""

import argparse
from pathlib import Path


HERE = Path(__file__).resolve().parent
CHUNK = "chunks/chunk_0104_text1_801A1940.c"
CAMERA_CHUNK = "chunks/chunk_0011_text1_8002D940.c"
CAMERA_HOOK = "    melee_refresh_camera_update(ctx, ctx->gpr[29]);\n"
CAMERA_DECLARATION = "void melee_refresh_camera_update(CPUState* ctx, u32 object);\n"
MARKER = "// Native Melee high refresh hooks v1"
HOOKS = (
    ("801A4D34", "    melee_refresh_reset();\n"),
    ("801A5034", "    melee_refresh_pace();\n"),
    (
        "801A5058",
        "    if (melee_refresh_finish(ctx)) {\n"
        "        goto label_801A5034;\n"
        "    }\n",
    ),
)


def patch_chunk(source: str) -> str:
    if MARKER in source:
        for address, hook in HOOKS:
            if source.count(f"label_{address}:\n{hook}") != 1:
                raise ValueError(f"Incomplete high refresh hook at {address}.")
        return source
    if source.count('#include "../generated.h"\n') != 1:
        raise ValueError("Unexpected generated header include.")
    for address, _ in HOOKS:
        if source.count(f"label_{address}:\n") != 1:
            raise ValueError(f"Expected one game-loop label at {address}.")
    # These calls delimit drawing. No simulation callback runs in this range.
    for address, target in (("801A5034", "8033C898"), ("801A5054", "803761C0")):
        if f"// {address}: bl      0x{target}" not in source:
            raise ValueError("The generated game loop is not Melee USA v1.02.")
    result = source.replace(
        '#include "../generated.h"\n',
        '#include "../generated.h"\n'
        f'{MARKER}\n#include "../MeleeHighRefresh.h"\n',
    )
    for address, hook in HOOKS:
        result = result.replace(f"label_{address}:\n", f"label_{address}:\n{hook}")
    return result


def patch_camera_chunk(source: str) -> str:
    label = "label_80030200:\n"
    if CAMERA_DECLARATION in source:
        if source.count(label + CAMERA_HOOK) != 1:
            raise ValueError("Incomplete high refresh camera hook.")
        return source
    if source.count(label) != 1 or source.count('#include "../generated.h"\n') != 1:
        raise ValueError("Unexpected generated camera function.")
    if "// 800301FC: bl      0x8002A4AC" not in source:
        raise ValueError("The generated camera is not Melee USA v1.02.")
    result = source.replace(
        '#include "../generated.h"\n',
        '#include "../generated.h"\n' + CAMERA_DECLARATION,
    )
    return result.replace(label, label + CAMERA_HOOK)


def install(generated: Path) -> None:
    chunk = generated / CHUNK
    source = chunk.read_text()
    result = patch_chunk(source)
    camera_chunk = generated / CAMERA_CHUNK
    camera_source = camera_chunk.read_text()
    camera_result = patch_camera_chunk(camera_source)
    header = (HERE / "refresh/MeleeHighRefresh.h").read_bytes()
    destination = generated / "MeleeHighRefresh.h"
    if not destination.exists() or destination.read_bytes() != header:
        destination.write_bytes(header)
    if result != source:
        chunk.write_text(result)
    if camera_result != camera_source:
        camera_chunk.write_text(camera_result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", type=Path, required=True)
    args = parser.parse_args()
    try:
        install(args.generated)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print("Installed native 120 FPS visual update hooks.")


if __name__ == "__main__":
    main()
