# Build native Melee on an Apple Silicon Mac

This experimental build uses native ARM64 game code, Metal graphics, and the
macOS controller APIs through SDL. It requires an Apple Silicon Mac and your
own Melee USA v1.02 disc image. Build on the Mac where you intend to play.
The app records the minimum macOS version required by its compiled libraries.

The [runtime notes](../native/macos/README.md) explain static recompilation and
list the projects this build uses. This is separate from the native static
library compile check and the normal Dolphin launch described in
[build-and-run.md](build-and-run.md).

## Build

First follow [the matching build setup](build-and-run.md) to create `.venv` and
put your original executable at `orig/GALE01/sys/main.dol`. Then install the
Mac runtime's build dependencies with Homebrew:

```sh
brew install cmake ninja pkgconf python fmt libusb lzo lz4 zstd pugixml
. .venv/bin/activate
python native/macos/build.py --iso '/path/to/Melee.iso'
```

Use Python 3.11 or later. Xcode's command line tools must be installed. The
first build downloads pinned source dependencies and compiles the runtime and
game. Generated code and game data stay under the ignored `build/native/` path.
Use `--jobs 8` to limit compiler processes.

The build verifies the entire matching GameCube executable before translation.
Its SHA-1 must be `08e0bf20134dfcb260699671004527b2d6bb1a45`. The current native
runtime uses fixed OS vector addresses for that revision. Other revisions and
modified executables are rejected.

The result is `build/native/Melee.app`. It contains the runtime, native game
module, and local disc data. The app can be moved as one directory. It uses
separate settings and saves, so it does not change Dolphin's user folder.

## Play

```sh
open build/native/Melee.app
```

Select Play. Escape opens the app menu. Command-comma opens Settings.

This branch defaults to experimental 120 FPS rendering and 4x internal
resolution, 2560 × 2112. Settings, Video has 60 FPS, 120 FPS, Auto resolution,
and fixed resolution choices through 8x, 5120 × 4224. Auto follows the
window's pixel size, including Retina scaling. Resolution changes apply when
you resume. Frame rate, vertical sync, and the input delay switch apply on the next launch.

The 120 FPS mode draws an extra frame between game updates. It predicts
joint positions, rotations, and camera movement half a frame ahead. Game
rules, collision, and animation clocks still run at 60 updates per second.
Prediction resets on action changes, teleports, and scene changes. Some
effects and interface elements still update at 60 Hz. Use a 120 Hz display
to see all the extra motion. A 60 Hz display limits what you can see.

Lower input delay samples controllers when the game reads them and presents
completed frames sooner. The renderer submits GPU work before display waits. It uses three drawable
buffers at 120 FPS and two at 60 FPS. The native video clock uses exact 60 or
120 Hz, which removes the drift between Melee's 60 Hz input alarm and its
original 59.94 Hz video timing. Mac frame waits use critical timers with a
short final CPU wait. The app prevents App Nap during play and skips the verified GameCube scheduler
idle loop. Vertical sync selects 60 FPS on a detected 60 Hz display to keep
the game at normal speed.

The [visual update notes](../native/macos/refresh/README.md),
[timer notes](../native/macos/timing/README.md), and
[Metal notes](../native/macos/render/README.md) describe the code and limits.

Faster loading is enabled for local play. It removes simulated GameCube disc
seek delays and raises the modeled buffer transfer rate to 256 MiB/s. Reads
keep their asynchronous completion order and 600 microsecond command delay.
To use the original disc timing, turn off Faster loading in Settings, General.
The preference is saved. It does not override netplay settings.

Bundled local startup skips unused whole-asset hashes. Executable checks still
run. Netplay, import tools, and builds that require an asset digest retain full
asset hashing.

The app keeps its pipeline list under `~/Library/Application Support/t3.melee.native/Cache`.
Known pipelines can compile before gameplay on later launches. Metal does not
store compiled pipeline binaries in this runtime, so a new effect can still
cause a first-use pause.

| Action | Keyboard | Xbox controller |
| --- | --- | --- |
| Move | W, A, S, D | Left stick |
| Attack | J | A |
| Special | K | B |
| Jump | Space or U | X or Y |
| Grab | O | RB |
| Shield | Q or E | LT or RT |
| C-stick | Arrow keys | Right stick |
| Start | Return | Menu |

Pair the Xbox controller in System Settings, Bluetooth. Select the connected
device in the app's input settings. macOS handles the Bluetooth connection.
Controller tests can use a software controller through Apple's GameController
framework. Those tests cannot verify the physical Bluetooth link.

## Limits

Strict native mode stops on uncovered game CPU code. This makes missing
translation visible instead of hiding it through a PowerPC interpreter.
Debugger single-step and interpreter comparison are unavailable in that mode.

The app is a local development build with an ad hoc signature. It is not a
published or notarized game. Intel Macs, Slippi, and online play are outside
this build path.

Do not commit or upload the app, game module, generated C, disc data, or saves.

## Earlier checks on the macOS base branch

On an M5 Max with macOS 26.5.2, the native app completed a two-minute Mario
versus Fox match on Yoshi's Island, displayed results, and returned to
character select. Attacks, jumping, shielding, damage, stock losses, sound
generation, save-state loading, and settings pause/resume were checked.
The sampled gameplay section ran at 59.91 to 59.96 FPS. Shutdown reported zero
CPU fallback steps and zero failed code checks. This is one tested match,
not full coverage of every mode and stage.

The controller path passed 103 checks using Apple's software controller API.
Physical Bluetooth and rumble still need hardware testing.

The app also reopened from `~/Applications/Melee.app` using the saved memory
card. Its runtime and game data were contained in that app directory.
The cache check wrote 215 pipeline entries and read them on the next launch.
The Faster loading switch saved and restored its preference, and its disabled
state restored the slower transition time.

The loading comparison used identical saved transitions, four runs per
setting, and the median of the last three runs. Each measurement includes
the same input hold and 30 rendered frames after it. These are transition
times on this Mac, not measurements of file I/O alone.

| Transition | Original disc timing | Faster loading |
| --- | ---: | ---: |
| VS menu to character select | 3.71 s | 0.94 s |
| Stage select to Yoshi's Island | 2.26 s | 0.85 s |

A second pass removed two full asset-hash scans at startup and tested a higher
local transfer rate. The comparison used the same Mac at 3x internal resolution,
the same assets and pipeline cache, and no diagnostic tracing during timing.

| Test | Before the second pass | After |
| --- | ---: | ---: |
| Normal launch to first rendered frame | 10.46 to 13.68 s | 1.60 to 1.65 s |
| Launch into the same saved menu, median | 14.99 s | 0.75 s |
| Warm character-select transition, 32 versus 256 MiB/s | 0.92 s | 0.79 s |
| Warm Yoshi's Island transition, 32 versus 256 MiB/s | 0.86 s | 0.81 s |

Normal startup used two old-build launches and three new-build launches. Saved
menu startup used three of each. The transfer comparison kept the startup
change enabled on both sides and used the last three of four transitions per
launch, across three 32 MiB/s launches and two 256 MiB/s launches. The same
input hold and 30-frame settling interval are included in each transition.
A 1024 MiB/s trial gave little additional benefit. `MELEE_DISC_MIB_PER_SECOND`
can select a rate from 32 to 1024 for developer comparisons. Normal local play
uses 256. The override only applies when Faster loading is enabled, the app is
bundled, and netplay is inactive.

The final build also completed a two-minute Mario versus Peach match on Fountain
of Dreams and restored a saved result screen. Its 242 sampled frame-rate readings
had a 59.94 FPS median. Shutdown reported no CPU fallback or failed code checks.
Turning Faster loading off restored approximately 3.74-second character-select
and 2.27-second stage transitions in the same test.

The original game keeps a 20-frame input wait on main-menu entry and five-frame
waits on submenu changes. This update preserves those waits and the normal game
clock. Graphics prewarming was also tested. An unused variant added about
102 ms to startup, so broader prewarming was not retained.

The complete GameCube executable still matches SHA-1
`08e0bf20134dfcb260699671004527b2d6bb1a45`. Public CI checks tools and the native
static library. It does not have the game data needed for this matching check
or the playable app build.

## Measure this branch

Save a state during an active Versus match, after the countdown. Use the
same state for each comparison. The benchmark creates separate settings,
cache, saves, screenshots, and logs under the output directory. It does not
use the installed app's save folder.

```sh
python native/macos/benchmark.py --app build/native/Melee.app \
  --state /path/to/match.sav --output build/native/perf-120 \
  --fps 120 --scale 4 --seconds 30
```

Use `--fps 60` to compare 60 FPS. Use `--original-queues` to compare the
original controller sampling and Metal queue settings. Add `--vsync` for
vertical sync. Run one game at a time and let other builds finish first.

`summary.json` reports render FPS, an estimate of simulation updates per
second, GPU time, frame intervals, and the time from presentation submission
to the Core Animation presentation callback. That interval is not total
controller-to-screen latency. With vertical sync off, presentation callbacks
can outnumber complete display refreshes. Check the monitor's refresh rate
before treating those callbacks as complete visible frames.

The benchmark rejects failed state loads and states outside a Versus match.
Screenshots, game states, and built apps contain game data and stay local.

## Fluidity checks on 2026-09-08

The test Mac has an M5 Max and macOS 26.5.2. The monitor was set to
3840 × 2160 at 60 Hz. Tests used the same saved Mario versus Peach match on
Yoshi's Island, with five seconds of warmup before each measurement.

| Internal resolution | Target | Measured render FPS | Median GPU time |
| --- | ---: | ---: | ---: |
| 4x, 2560 × 2112 | 120 | 119.97 | 3.30 ms |
| 6x, 3840 × 3168 | 120 | 119.99 | 5.14 ms |
| 4x, 2560 × 2112, vertical sync | 60 | 60.00 | 1.93 ms |
| 8x, 5120 × 4224, vertical sync | 60 | 60.00 | 9.07 ms |

The first three runs measured 20 seconds. The 8x run measured 15 seconds.
Simulation counters advanced at about 60 updates per second in every run.
Render counters and changed pose counts confirmed the extra visual updates.
The 60 Hz monitor cannot verify 120 complete visible refreshes. A 120 Hz
display still needs a visual check. These are short local measurements,
not coverage of all characters, stages, or effects.

The final app also passed attacks, jumping, shielding, a 120 FPS save/load
round trip, invalid-state rejection, settings pause/resume, and saved Auto
resolution. The controller test passed 103 checks through Apple's software
controller API. Physical Bluetooth and rumble were not tested. Shutdown
reported zero CPU fallback steps and zero failed code checks.

The full repeat native build and app signature check passed. The 37 native
tool and pose tests, three timing tests, and source and style checks passed.
`tools/verify.py` confirmed that all original source units still match and
the complete GameCube executable keeps SHA-1
`08e0bf20134dfcb260699671004527b2d6bb1a45`.
