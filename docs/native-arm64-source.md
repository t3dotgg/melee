# Direct ARM64 source port

This work builds the decompiled Melee C source for a 64-bit macOS process.
It does not use DolRecomp, ModernGekko, RecompCore, a PowerPC interpreter,
or generated PowerPC instruction code.

The existing Mac app compiles translated PowerPC code into ARM64 machine
code. It still uses guest CPU state, 32-bit guest addresses, and Dolphin
services. That app is a separate build path.

The direct build is an experimental port. It is separate from the existing
translated Mac app.

Validate this port with ARM64 compilation, sanitizer tests, and actual native
game runs. The original GameCube compiler comparison is outside this workflow.
Do not install or use Rosetta for the direct port.

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

The current run passes all 15 tests: heap, memory, GObj links, math, scene
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
IMAGE="/path/to/Melee.iso"
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
environment. Add `--seed 1` to repeat the game's random choices during a test.
`MELEE_RANDOM_SEED` supplies the same setting.

For timing and input checks that do not need a rendered image, set
`MELEE_SKIP_RENDER=1`. This keeps the software GX state updates but skips the
CPU rasterizer, so scripted frames advance at host speed.

For isolated startup checks, `MELEE_SKIP_CARD=1` skips the memory card screen.
`MELEE_SKIP_INTRO=1` skips the unavailable THP intro movie decoder. Save tests
must set `MELEE_SAVE_ROOT` to an ignored build directory.

The game reaches the title screen, VS scene setup, character select, stage
select, and the gameplay scene on the real Rev 2 ISO. A bounded sanitizer run
reached `mode=2 state=2 scene=2` and stayed there for 150 seconds with no
sanitizer or archive errors. Button-only A and B attack routes also stayed in
the gameplay scene for 110 seconds. Combined movement and attack input still
hits an item assertion. Match end and return to the menu remain unverified.

## Current state

The complete game builds as an ARM64 executable. The integrated compile check
passes all 1016 C sources. All 35 focused sanitizer tests pass. These checks do
not prove that a match works.

The port has native heap and context storage, filesystem and ISO disc reads,
ARAM, controller input, audio output, alarm callbacks, and 60 Hz retraces.
The scheduler uses the host monotonic clock. Tests can use a deterministic
clock. The THP intro movie decoder remains unavailable.

The archive bridge converts typed records with host pointers. It loads the
stage graphs, Great Bay parameters, Castle dynamics roots, scene roots, effect
tables, command streams, item data, and fighter data used by the tested match.
Real item tests cover both common regional archives and all 71 extracted stage
archives. The runtime routes above exercise these converted records in the
game.

The software GX renderer applies matrix palettes, skinning, lighting, texture
sampling, TEV materials, fog, clipping, depth tests, and framebuffer copies.
The native Metal backend renders the title screen and character select screen.
The gameplay routes above skip rasterization. Match rendering speed and the
complete match lifecycle remain under test.

Native audio decodes SFX and HPS music and sends stereo PCM to AudioToolbox.
An independent HPS decoder check matched all 1920000 samples from a 30-second
music extract. The output device consumed a one-second test. DSP resampling
uses linear interpolation. Sound during a real match remains unverified.

Persistent native card storage and HSD save/load now pass focused tests,
including process restart, corruption, full capacity, and queued callbacks.
The game creates a save during normal startup and reads it on restart.
Both runs advance beyond the memory card screen. Match and menu faults still
need to be resolved before the full save flow can be verified.

The host PAD shim maps keyboard events to controller 0. Arrow keys provide the
D-pad, `A`/`D` and `W`/`S` provide the main stick, `F`/`H` and `G`/`T` provide
the C-stick, `J`/`K`/`U`/`I` provide A/B/X/Y, `O` provides Z, `Q` and `E`
provide L/R, Shift and Control provide the analog triggers, and Return or
Space provides Start. `C` and `V` provide the analog A and B buttons.

Do not treat a successful link as playable behavior.

## Remaining validation

1. Check native rendering at playable speed during a match.
2. Test movement and item interactions without archive assertions.
3. Test sound, match end, and return to the menu.
4. Repeat save creation and loading through the game screens.

Agent worktrees isolate each subsystem. The integrated changes are published
to the fork's `native-arm64-build` branch. Keep this record current as each
step is verified.
