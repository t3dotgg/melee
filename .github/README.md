# Melee for Mac

**A fully automated slop experiment. Not meant for serious use or investigation.**

Theo asked AI agents to make Melee run on an Apple Silicon Mac and improve the
fork's code and tools. This is the result. No support, maintenance, or human
review is promised. Please do not spend time investigating, auditing, or
maintaining this fork, and do not send its problems to upstream projects.

This is an unofficial experiment. It builds on the work of
[doldecomp/melee](https://github.com/doldecomp/melee),
[Melee macOS Recompilation](https://github.com/McDandle/melee-macos-recomp),
ModernGekko, DolRecomp, RecompCore, Dolphin, and their contributors.
[Full credits and licenses](../native/macos/README.md#source-and-licenses).

## Try the experiment

For the native Windows x64 build, see [the Windows build and play guide](../docs/native-windows.md).

You need an Apple Silicon Mac and your own Melee USA v1.02 disc image.
No game image, extracted game data, generated game code, or playable binary is
included here.

1. Clone `https://github.com/t3dotgg/melee4mac.git` and use `master`.
2. Follow the [build and play guide](../docs/native-macos.md), including its
   matching-build setup and dependency instructions.
3. Build and open the app:

```sh
. .venv/bin/activate
python native/macos/build.py --iso '/path/to/Melee.iso'
open "build/native/Melee for Mac.app"
```

The build compiles the verified game executable and disc loader for ARM64.
The Dolphin-derived runtime supplies Metal graphics, audio, input, and GameCube
services. This is static recompilation. It is not a direct macOS port of the
decompiled C source.

The app uses normal macOS controller support through SDL. Pair a controller in
System Settings, Bluetooth, then select it in the app's input settings. Local
play has faster loading enabled. Older builds keep their settings and saves
under `Application Support/t3.melee.native` after the rename.

## What the checks mean

Local checks covered builds, software controller input, complete matches, saves,
and several loading transitions. The [build guide](../docs/native-macos.md)
records the measurements and their limits. These checks are not a support or
reliability promise. Physical Bluetooth and rumble have not been tested here.

The matching GameCube executable keeps SHA-1
`08e0bf20134dfcb260699671004527b2d6bb1a45`. Public CI checks tools, formatting,
and a native static library. It has no game data and does not certify the
matching executable or the playable Mac app.

## Work on this fork

Changes happen at Theo's explicit request. The [contribution rules](CONTRIBUTING.md)
and [agent instructions](../AGENTS.md) define that work. The
[source map](../docs/code-map.md) and [matching build reference](../docs/build-and-run.md)
are references, not an invitation to investigate or maintain this experiment.

Keep all work in [t3dotgg/melee4mac](https://github.com/t3dotgg/melee4mac).
Never open a pull request, issue, review, or comment on `doldecomp/melee` for work
from this fork.
