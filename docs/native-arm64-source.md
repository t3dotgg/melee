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

This compiles 984 game and engine files plus 20 native support files under
`native/source` and one MSL floating point source file. The current inventory
is 1005/1005 ARM64 files. It returns a failure status if any file fails. The
JSON report and compiler logs are under `build/native-source`.
It requires no game image and does not fetch or run a translator.

The forced-load link check verifies every compiled C object:

```sh
python3 native/source/archive.py --build-dir build/native-source
```

The report records unresolved host entry points. With only the C objects, the
current report contains the three `NativeAudioOutput*` entry points. The full
launcher link below adds `audio_output.m` and AudioToolbox and resolves them.
The native executable still imports normal macOS system frameworks at runtime.

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

The focused host tests use the default AddressSanitizer and UndefinedBehavior
Sanitizer build:

```sh
cmake -S native/source -B build/native-source/tests -G Ninja \
  -DMELEE_BUILD_NATIVE_GAME=OFF -DMELEE_SANITIZERS=ON
cmake --build build/native-source/tests --parallel 8
ctest --test-dir build/native-source/tests --output-on-failure
```

The current run passes all 13 tests: heap, memory, GObj links, math, scene
sorting and bytecode, fighter storage, effects, both archive readers,
scheduler, controller input, and GX.

Run it with an extracted game directory or a disc image:

```sh
build/native-source/game/melee-native --root /path/to/melee-files
build/native-source/game/melee-native --disc /path/to/melee.iso
```

A single path is accepted too. The launcher detects directories and regular
files. The same paths can be supplied with `MELEE_GAME_ROOT` and
`MELEE_DISC_IMAGE`.

The verified test image is Melee USA revision 2. Its disc ID is `GALE01` and
its SHA-256 is
`979c42a2cda2d022370ceeace9afb0bb6e1374287aea61c77e8f5b27f53ee526`.
For the local copy used during validation:

```sh
IMAGE="/Users/theo-mini-pro/Downloads/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).iso"
shasum -a 256 "$IMAGE"
build/native-source/game/melee-native --disc "$IMAGE"
```

Headless validation can drive controller 0 with a deterministic timeline. Use
semicolon-separated `frame=buttons` entries. Button names can be combined with
`+`, and the last entry at or before the current frame stays active:

```sh
build/native-source/game/melee-native --disc "$IMAGE" \
  --pad-script '0=START;2=NONE;60=A+STICK_RIGHT;61=NONE' --pad-trace
```

`MELEE_PAD_SCRIPT` and `MELEE_PAD_TRACE` provide the same settings through the
environment.

The latest launcher build starts the native game loop and stays alive during
an eight-second run with this image. It prints the game startup banner without
an archive error. Stop it with Control-C. A longer AddressSanitizer startup
run also stays alive without a sanitizer report. These runs have not yet
reached a playable menu or a real match.

## Current state

The full direct-source target now links successfully after the forced-load
check reports no missing game symbols. The host services include 64-bit heap
and context storage, typed archive loading for the supported descriptor
schemas, filesystem or ISO disc reads, ARAM, controller state, headless 60 Hz
retraces with OS alarm
callbacks, a software GX EFB for direct vertices and common display lists,
cache operations, card stubs, and an explicit unavailable THP decoder. The
scheduler uses the host monotonic clock by default. Tests can advance a
deterministic clock without sleeping.

The native archive bridge now loads `lbRumbleData`, `SIS_MessageData`, and
`MemCardIconData` from the real image. It also converts the camera animation
used by `ScNtcCommon_scene_data`. An AddressSanitizer startup run reaches this
typed scene conversion without a sanitizer report. It has not yet entered a
real match. The typed archive graph covers common joint display
descriptors, materials, texture metadata, skin polygon descriptors, vertex
descriptor lists, animations, cameras, and world objects. Remaining archive
work includes the full scene roots, stage and menu roots, shape and envelope
polygon descriptors, effects, and other callers. The graph can decode material
and texture metadata, but the renderer does not yet apply those materials or
sample those textures.

The software GX EFB rasterizes direct vertex calls and `GXCopyDisp` copies the
result to an RGB565 XFB. Its display-list decoder handles common big-endian
streams with direct or 8-bit and 16-bit indexed position, color, and texture
attributes, including NBT normal and binormal data. It recognizes common GX
state commands and skips their payload safely. Other vertex-array paths,
display-list commands, texture sampling, and material and TEV effects remain
unsupported. The Cocoa XFB preview only presents the resulting buffer.

Native SFX loading now keeps the big-endian voice payload separate from widened
host metadata, decodes voice address and ADPCM fields, rebases sample
addresses, and writes source ratios in host order. This removes the previous
LP64 overlay and endian failures, but full sound playback still needs runtime
validation in a real match.

The host PAD shim maps keyboard events to controller 0. Arrow keys provide the
D-pad, `A`/`D` and `W`/`S` provide the main stick, `F`/`H` and `G`/`T` provide
the C-stick, `J`/`K`/`U`/`I` provide A/B/X/Y, `O` provides Z, `Q` and `E`
provide L/R, Shift and Control provide the analog triggers, and Return or
Space provides Start. `C` and `V` provide the analog A and B buttons.

AI DMA stereo PCM is now queued to a macOS AudioToolbox output unit when the
device is available. Headless runs keep the state-only fallback. Persistent
card storage and complete archive and font handling from the disc image remain.
Do not treat a successful link as playable behavior.

## Work order

1. Complete the scene, stage, menu, shape, envelope, effects, and font archive
   schemas and their callers.
2. Load a real model and animation from the supplied Melee image.
3. Complete GX support beyond the common display-list streams, including
   remaining vertex-array paths and the texture, material, and TEV state used
   by the game, then present the result through the Cocoa XFB path.
4. Validate sound playback and add persistent card storage.
5. Boot menus, enter a match, check controls and match end, and return to the
   menu.

Separate agent worktrees isolate SDK headers, allocation and IDs, scene
objects, archive data, and host math. Integration happens on
`native-arm64-build`. Keep this record current as each stage is verified.
