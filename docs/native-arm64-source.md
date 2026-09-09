# Direct ARM64 source port

This work builds the decompiled Melee C source for a 64-bit macOS process.
It does not use DolRecomp, ModernGekko, RecompCore, a PowerPC interpreter,
or generated PowerPC instruction code.

The existing Mac app compiles translated PowerPC code into ARM64 machine
code. It still uses guest CPU state, 32-bit guest addresses, and Dolphin
services. That app is a separate build path.

The direct build is an experimental port. It is separate from the existing
translated Mac app.

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

This compiles 984 game and engine files plus the native support files under
`native/source` and the required MSL floating point constants. The current
inventory is 1005/1005 ARM64 files. It returns a failure status if any file
fails. The JSON report and compiler logs are under `build/native-source`.
It requires no game image and does not fetch or run a translator.

To check a changed directory:

```sh
python3 native/source/compile.py --source src/sysdolphin/baselib
```

`MELEE_NATIVE` selects host types and platform code. The original compiler
path keeps its default types. Source offset names refer to the original
GameCube layouts. They are not host byte offsets.

## Native launcher

The optional `native_melee` target links all direct C sources with the host
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
`MELEE_DISC_IMAGE`.

## Current state

The full direct-source target now links successfully with no undefined
symbols. The host services include 64-bit heap and context storage, typed
archive loading for the supported descriptor schemas, filesystem or ISO disc reads, ARAM, controller state,
headless 60 Hz retraces with OS alarm callbacks, headless GX state,
deterministic audio stubs, cache operations, card stubs, and an explicit
unavailable THP decoder. The scheduler uses the host monotonic clock by
default. Tests can advance a deterministic clock without sleeping.

The executable has not run a real match. An empty game directory reaches the
SIS initialization path and waits in the startup loop because the game data is
absent. The typed archive graph now covers common joint display descriptors,
materials, texture metadata, skin polygon descriptors, vertex descriptor lists,
animations, cameras, and world objects. The runtime still needs a SIS archive
bridge, stage and menu root schemas, shape and envelope polygon descriptors,
Metal rendering, audio output, persistent card storage, and real archive and
font data from the disc image.
Do not treat a successful link as playable behavior.

## Work order

1. Add asynchronous ARQ completion and the SIS archive bridge.
2. Load a real model and animation from the supplied Melee image.
3. Add a Metal renderer, audio output, and persistent card storage.
4. Boot menus, enter a match, check controls and match end, and return to the menu.

Separate agent worktrees isolate SDK headers, allocation and IDs, scene
objects, archive data, and host math. Integration happens on
`native-arm64-build`. Keep this record current as each stage is verified.
