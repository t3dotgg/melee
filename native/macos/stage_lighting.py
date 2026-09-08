#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install reversible stage lighting in the translated US v1.02 renderer."""

import argparse
from pathlib import Path


HERE = Path(__file__).resolve().parent
MARKER = "// Native Melee stage lighting hooks v1"
INCLUDE = '#include "../generated.h"\n'
LOOP_CHUNK = "chunks/chunk_0104_text1_801A1940.c"
GROUND_CHUNK = "chunks/chunk_0112_text1_801C1940.c"
FIGHTER_CHUNK = "chunks/chunk_0039_text1_8009D940.c"
GOBJ_CHUNK = "chunks/chunk_0227_text1_8038D940.c"
LOOP_DECLARATIONS = (
    "void melee_stage_lighting_begin(CPUState* ctx);\n"
    "void melee_stage_lighting_finish(CPUState* ctx);\n"
)
LIGHT_DECLARATION = "void melee_stage_lighting_lights(CPUState* ctx, u32 gobj);\n"
STATE_DECLARATION = (
    "void melee_stage_lighting_prepare_state(CPUState* ctx, u32 loading);\n"
)
STATE_ENTRY = "MeleeNativePrepareState(CPUState* ctx, u32 loading)\n{\n"
STATE_HOOK = "    melee_stage_lighting_prepare_state(ctx, loading);\n"
HEADERS = (
    "MeleeStageLighting.h",
    "BattlefieldLighting.h",
    "FinalDestinationLighting.h",
    "FountainLighting.h",
)


def patch_chunk(source: str, declarations: str, hooks: dict[str, str],
                instructions: tuple[str, ...]) -> str:
    """Validate every hook before changing a generated chunk."""
    if source.count(INCLUDE) != 1:
        raise ValueError("Unexpected generated header include.")
    for instruction in instructions:
        if source.count(instruction) != 1:
            raise ValueError(f"Expected US v1.02 instruction: {instruction}")
    for address in hooks:
        if source.count(f"label_{address}:\n") != 1:
            raise ValueError(f"Expected one generated label at {address}.")
    if MARKER in source:
        if source.count(MARKER + "\n" + declarations) != 1:
            raise ValueError("Incomplete stage lighting declarations.")
        for address, hook in hooks.items():
            if source.count(f"label_{address}:\n{hook}") != 1:
                raise ValueError(f"Incomplete stage lighting hook at {address}.")
        return source
    result = source.replace(INCLUDE, INCLUDE + MARKER + "\n" + declarations)
    for address, hook in hooks.items():
        result = result.replace(f"label_{address}:\n", f"label_{address}:\n{hook}")
    return result


def patch_loop(source: str) -> str:
    # 5048 is immediately after HSD_GObj_80390FC0 has drawn every camera.
    # It runs before copying the frame and before the optional extra render.
    return patch_chunk(
        source, LOOP_DECLARATIONS,
        {"801A5034": "    melee_stage_lighting_begin(ctx);\n",
         "801A5048": "    melee_stage_lighting_finish(ctx);\n"},
        ("// 801A5034: bl      0x8033C898",
         "// 801A5044: bl      0x80390FC0"),
    )


def patch_light_callback(source: str, address: str, call: str,
                         implementation: bool = False) -> str:
    declarations = ('#include "../MeleeStageLighting.h"\n'
                    if implementation else LIGHT_DECLARATION)
    return patch_chunk(
        source, declarations,
        {address: "    melee_stage_lighting_lights(ctx, ctx->gpr[3]);\n"},
        (f"// {call}: bl      0x803668EC",),
    )


def patch_state_header(source: str) -> str:
    if source.count(STATE_ENTRY) != 1:
        raise ValueError("Expected the native state preparation hook.")
    if STATE_DECLARATION in source:
        if source.count(STATE_DECLARATION) != 1 or source.count(
                STATE_ENTRY + STATE_HOOK) != 1:
            raise ValueError("Incomplete stage lighting state hook.")
        return source
    include = "#include <time.h>\n"
    if source.count(include) != 1:
        raise ValueError("Unexpected high refresh header.")
    return source.replace(include, include + "\n" + STATE_DECLARATION).replace(
        STATE_ENTRY, STATE_ENTRY + STATE_HOOK,
    )


def install(generated: Path) -> None:
    transforms = (
        (LOOP_CHUNK, patch_loop),
        (GROUND_CHUNK, lambda source: patch_light_callback(
            source, "801C4640", "801C4650", implementation=True)),
        (FIGHTER_CHUNK, lambda source: patch_light_callback(
            source, "8009F54C", "8009F55C")),
        (GOBJ_CHUNK, lambda source: patch_light_callback(
            source, "80391044", "80391054")),
        ("MeleeHighRefresh.h", patch_state_header),
    )
    pending = []
    # Read and validate the complete set before writing any output. A stale
    # generated module must not acquire only half of a render transaction.
    for relative, transform in transforms:
        path = generated / relative
        source = path.read_text()
        result = transform(source)
        if result != source:
            pending.append((path, result.encode()))
    for name in HEADERS:
        source = (HERE / "lighting" / name).read_bytes()
        destination = generated / name
        if not destination.exists() or destination.read_bytes() != source:
            pending.append((destination, source))
    for path, data in pending:
        path.write_bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", type=Path, required=True)
    args = parser.parse_args()
    try:
        install(args.generated)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print("Installed native stage lighting and material hooks.")


if __name__ == "__main__":
    main()
