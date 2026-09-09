# Direct ARM64 source port

This work builds the decompiled Melee C source for a 64-bit macOS process.
It does not use DolRecomp, ModernGekko, RecompCore, a PowerPC interpreter,
or generated PowerPC instruction code.

The existing Mac app compiles translated PowerPC code into ARM64 machine
code. It still uses guest CPU state, 32-bit guest addresses, and Dolphin
services. That app is a separate build path.

This is Theo's fully automated slop experiment. It is not meant for serious
use or investigation. No support, maintenance, or human review is promised.

## Completion requirements

The active goal is a working game build, not a compile check.

- Compile game code directly from C for ARM64 with eight-byte host pointers.
- Keep fixed-width game numbers at their specified widths.
- Decode big-endian disc structures into typed host structures. Preserve
  references and sharing without writing host pointers into four-byte slots.
- Replace console hardware services with host implementations.
- Link a real executable without missing gameplay functions or silent stubs.
- Load game data, enter a match, exercise controls, render and play sound,
  finish the match, and return to the menu.
- Record the tested modes and any remaining limits.
- Keep game data and build output in ignored directories. Do not modify
  installed apps or publish game data.

## Compile inventory

Run on an Apple Silicon Mac with Xcode command line tools:

```sh
python3 native/source/compile.py --jobs 8
```

This compiles every C file under `src/melee` and `src/sysdolphin` into
ARM64 objects. It returns a failure status if any file fails. The JSON
report and individual compiler logs are under `build/native-source`.
It requires no game image and does not fetch or run a translator.

To check a changed directory:

```sh
python3 native/source/compile.py --source src/sysdolphin/baselib
```

`MELEE_NATIVE` selects host types and platform code. The original compiler
path keeps its default types. Source offset names refer to the original
GameCube layouts. They are not host byte offsets.

## Native launcher

The optional `native_melee` target links the direct C sources with the host
platform services. Build it on an Apple Silicon Mac with:

```sh
cmake -S native/source -B build/native-source/game -G Ninja \
  -DMELEE_BUILD_NATIVE_GAME=ON -DMELEE_SANITIZERS=OFF
cmake --build build/native-source/game --target native_melee
```

Run it with an extracted game directory or a disc image:

```sh
build/native-source/game/melee-native --root /path/to/melee-files
build/native-source/game/melee-native --disc /path/to/melee.iso
```

A single path is accepted too. The launcher detects directories and regular
files. The same paths can be supplied with `MELEE_GAME_ROOT` and
`MELEE_DISC_IMAGE`. The target is opt-in while host rendering, controller
input, audio, scheduling, and save support are still incomplete. A successful
link does not mean that the game is playable.

## Current state

The initial inventory compiles 798 of 984 game and engine C files. It treats
pointer truncation as an error. This does not establish link completeness
or correct runtime behavior.

The port is in progress. There is no playable direct-source build yet.

## Work order

1. Host SDK types, allocator sizes, pointer IDs, math, and a reproducible build.
2. Typed archive conversion and a real model and animation load.
3. Full source compilation and a link inventory for missing code and data.
4. Host file access, scheduling, input, graphics, audio, and saves.
5. Game startup, menus, a match, and behavior checks against the existing game.

Separate agent worktrees isolate SDK headers, allocation and IDs, scene
objects, archive data, and host math. Integration happens on
`native-arm64-build`. Keep this record current as each stage is verified.
