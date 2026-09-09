# Native Melee on Windows

This is Theo's fully automated slop experiment, based on `doldecomp/melee`.
It is not meant for serious use or investigation. No support, maintenance,
or human review is promised. Do not present it as an official port or a
maintained project.

The Windows target builds an x64 application and translated game DLL from
the verified Melee US v1.02 executable. ModernGekko supplies the Windows
window, graphics, audio, controller, and disc services. The launcher selects
static recompilation and enables strict mode, which rejects interpreter or
JIT fallback. It uses the original 60 Hz game speed.

Fast disc loading removes simulated seek waits while retaining real
asynchronous reads and completion callbacks. Startup also skips the Mac
app's optional workspace media scan. `-OriginalDiscTiming` restores the
simulated disc waits. The launcher applies the Mac app's portable 60 FPS
presentation settings: prompt presentation, precise frame timing, immediate
XFB presentation, and no VI overclock.

The Windows launcher enables `CPUThread` to run CPU and graphics work on
separate threads. This reduced frame-rate dips in the measured Onett match
while keeping normal 60 Hz game speed. `-SingleCore` disables that setting
for comparisons.

## Build

Use Windows x64, Python 3.11 or later, Git, CMake, Ninja, LLVM 22.1.8, and
Visual Studio 2022 Build Tools with **Desktop development with C++**
installed. Include the MSVC x64 compiler and Windows SDK. LLVM must provide
`clang-cl.exe`, `lld-link.exe`, and `llvm-lib.exe`. The build driver imports
Visual Studio's environment; a separate developer shell is unnecessary.

The default build uses MSVC for the runtime and translation tools, then
Clang with ThinLTO for the game DLL. The Clang module resolved the measured
performance shortfall in the initial MSVC game build. The driver finds
LLVM in `build/tools/llvm/bin`, on `PATH`, or in the standard LLVM install
directory. Use `--llvm-dir` to select another LLVM install root or `bin`
directory.

Run from the repository root:

```powershell
python -m pip install -r reqs/build.txt cmake ninja
python native/windows/build.py --iso 'your-local.iso'
```

An already extracted disc can be reused:

```powershell
python native/windows/build.py --disc-dir build/disc
```

The extracted root must contain `sys/main.dol` and `files/`. The executable
must have SHA-1 `08e0bf20134dfcb260699671004527b2d6bb1a45`. The driver verifies
the complete matching GameCube build before translating it. This build
requires local game data; public CI tool checks cannot certify a match.
See [the matching build guide](build-and-run.md) for the original build and
verification procedure.

The default runtime workspace is `build/native-windows/recomp`. Its launch
inputs are:

| Path within runtime workspace | Purpose |
| --- | --- |
| `build/runtime/moderngekko-run.exe` | Windows runtime |
| `build/runtime/Sys/` | Runtime resources |
| `build/game-clang/gGALE01_recomp.dll` | Optimized translated x64 game |
| `build/launch-manifest.json` | Successfully built runtime/module selection |
| `private/GALE01r2/` | Disc files when imported from an image |

`--jobs` controls build parallelism. `--stage prepare` prepares dependencies,
and `--stage tools` builds the runtime and translation tools. Those stages
alone do not produce a playable game. `--module-compiler clang-cl` is the
default; `--compiler` is an alias for that option. An explicit
`--module-compiler msvc` comparison build uses `build/game/` and disables
interprocedural optimization for that module. The runtime continues to use
MSVC. A missing Clang toolchain stops the default build.

## Launch

```powershell
powershell -ExecutionPolicy Bypass -File native/windows/run.ps1
```

The launcher uses `private/GALE01r2` in the runtime workspace if present,
otherwise `build/disc`. It stores settings, memory cards, states, and logs
under `build/native-windows/user`. It checks required files and the game
executable hash before launch. The game window title keeps the automated
slop notice visible.

After a successful build, the launcher reads `build/launch-manifest.json`
inside the runtime workspace. It validates the manifest version, compiler,
optimization mode, and expected relative output paths. Without a manifest,
it selects `build/game-clang/gGALE01_recomp.dll`. Invalid manifests or
missing selected modules stop launch; an old MSVC game DLL is never used
as an automatic fallback.

Use explicit paths for an alternate build, disc, or save folder:

```powershell
./native/windows/run.ps1 -RuntimeDir build/native-windows/recomp `
  -DiscDir build/disc -UserDir build/native-windows/user
```

`-Graphics OGL` selects OpenGL; Vulkan is the default. The first launch may
take longer while compiling graphics shaders. Click the game window before
using the keyboard. Close it before launching another session with the
same save folder.

`-PrepareOnly` validates launch inputs and writes settings without opening
the game. For runtime options such as loading a local savestate, pass an
array using `-RuntimeArguments`, for example:

```powershell
./native/windows/run.ps1 -RuntimeArguments @('--load-state', 'build/example.sav')
```

## Controls

The launcher creates `Config/GCPadNew.ini` on first use. Existing mappings
are preserved. `-ResetInput` backs up that file before restoring these
defaults. The keyboard and first XInput gamepad both control player 1;
other player ports are initially unconfigured.

| GameCube control | Keyboard | First XInput gamepad |
| --- | --- | --- |
| Main stick / menu movement | WASD | Left stick or D-pad |
| A / confirm / normal attack | J | A |
| B / back / special | K | B |
| X and Y / jump | U and I | X and Y |
| Z / grab | O | Right shoulder |
| Start / pause match | Enter | Start |
| L and R / shield | Q and E | LT/RT; digital press at 90% travel |
| C-stick | Arrow keys | Right stick |
| GameCube D-pad up, down, left, right | T, G, F, H | — |
| Half main-stick range | Left Shift | — |

These gamepad bindings follow
[`MeleeControllerConfig.h`](../native/macos/input/MeleeControllerConfig.h),
which defines the Mac app's Xbox profile. Both sticks have 10 percent dead
zones. Triggers have no dead zone, preserving light shields, and become
digital presses at 90 percent travel. The SDL backend used on macOS and
XInput used here assign the same physical positions to A/B/X/Y: south,
east, west, and north respectively.

To replace a profile generated by an earlier Windows launcher, close the
game and run `./native/windows/run.ps1 -ResetInput`. It backs up the old
profile first. Ordinary launches continue to preserve input customizations.

Windows DirectInput key names are case-sensitive in the configuration:
`RETURN`, `LSHIFT`, `UP`, `DOWN`, `LEFT`, and `RIGHT`. The keyboard device is
`DInput/0/Keyboard Mouse`; the gamepad mappings use `XInput/0/Gamepad`.
Connect a controller before launch. Devices with another input backend or
device index need their own mapping in `GCPadNew.ini`.

F11 or Alt+Enter toggles fullscreen. Escape exits fullscreen, or closes the
game when windowed. Space is reserved for holding fast-forward, so jump is
mapped to U/I. The File menu has pause and savestate commands, and the View
menu has fullscreen and mute. F1 saves a state.

## Textures

The sibling `melee4mac-textures` repository contains archives. Restore its
pack to an ignored directory before launching; the runtime needs an
extracted `GALE01` directory containing DDS textures.

```powershell
python -m pip install -c tools/texture_upscale/requirements.txt Pillow numpy xxhash
python tools/texture_upscale/restore_pack.py --assets ../melee4mac-textures `
  --output build/native-windows/textures
```

The launcher automatically uses `build/native-windows/textures/GALE01`
when present. To use another restored pack:

```powershell
./native/windows/run.ps1 -TexturePack build/texture-pack
```

This creates a local directory junction at
`build/native-windows/user/Load/Textures/GALE01`, enables replacement
textures, and loads textures as requested instead of preloading the entire
pack. It also sets `MELEE_TEXTURE_PACK` for the runtime's bounded texture
cache. The source pack remains in place. An existing junction targeting
another pack is rejected; use a separate `-UserDir` for that pack.

Use `-NoTextures` to launch with original disc textures. That option updates
the graphics setting and leaves the local pack available for a later
launch. Full-resolution ending stills additionally depend on the runtime's
ending-stills patch.

## Logs and checks

Each launch writes timestamped stdout and stderr logs under
`build/native-windows/user/Logs`. A nonzero process exit reports the log
location. Settings are in `Config/GCPadNew.ini`, `Config/GFX.ini`, and
`config.ini`; close the game before editing them. `config.ini` starts at
`resolution=1920x1080`, with FPS in the title and fullscreen disabled.

The complete default build driver passed on September 8, 2026, producing
the MSVC runtime and Clang ThinLTO game DLL. After formatting, the runtime
rebuilt and `python tools/verify.py` passed with all 1,130 units matching
and complete executable SHA-1 `08e0bf20134dfcb260699671004527b2d6bb1a45`.
All eight patched source files passed clang-format 22.1.8 and the source
checker. All 25 Windows tool tests passed.

PowerShell launcher checks passed for input and settings preservation,
reset backups, texture junctions, session locking, argument quoting,
environment restoration, and manifest selection and rejection. Isolated
preparation also passed with the actual runtime, optimized DLL, verified
disc, and textures. Runtime logs confirmed strict native boot, replacement
texture indexing, and texture uploads.

The final 90-second benchmark loaded a saved three-fighter VS match on
Onett (Mario, Bowser, and Zelda), with textures enabled and CPU/graphics
work on separate threads. It measured median **59.9936 FPS**, minimum
**57.9646 FPS**, normal speed **1.0**, and **1.578 seconds** from the saved-state
launch to reported FPS. The process exited with code 0 without a forced stop; a
captured frame confirmed an active match. A prior 60-second comparison
with CPU/graphics work on one thread measured median 59.861 FPS and minimum
52.9434 FPS. These are runtime-reported FPS samples, not physical display
timing, and do not establish 60 FPS in every stage, fighter count, or scene.

The prerecorded THP opening movie still stutters. A separate 30-second
movie sample, including scene transitions, measured median 50.6829 FPS,
minimum 29.4672 FPS, and reported speed 0.8698. That clip was not a live
four-player match; live four-player gameplay remains untested.

To repeat a saved-scene measurement, first save a scene with F1, then use
its path and a new output directory under ignored `build/`:

```powershell
python native/windows/benchmark.py --state 'build/native-windows/user/StateSaves/your-scene.sav' `
  --output build/native-windows/benchmarks/onett-90s --seconds 90
```

Both `--state` and `--output` are required. The output directory must be
fresh. It contains `summary.json`, `runtime.log`, and `scene.png`. Add
`--profile` when dispatch samples are needed for a performance comparison.

The restored pack contains 13,060 DDS files, including 225 ending planes.
All layouts, dimensions, and mip counts passed validation; 13,050 files
matched recorded hashes. Ten regenerated DDS files differed from their
recorded hashes on Windows, while their base pixels matched the verified
archived PNGs. The local texture verification report records those files.

Theo confirmed the game launches and the controller works. The controller
defaults were then aligned with the Mac profile's D-pad movement and
trigger threshold; physical verification of those adjustments is pending.
Audible output and keyboard gameplay remain unverified. Boot logs and FPS
telemetry alone do not establish those results.

Keep disc images, extracted game data, DOLs, generated executables, texture
packs, and saves out of Git commits and uploads. Use this fork for requested
work; do not send changes or investigation requests to `doldecomp/melee`.
