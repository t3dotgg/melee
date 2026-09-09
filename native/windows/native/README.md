# Native Windows shell (experimental)

This directory is the first bounded slice of a future source-level Windows
port. It builds a real 64-bit C++ executable with a host-owned frame loop,
typed game interface, bounds-checked game memory, validated asset archives,
typed Xbox input, fixed-rate timing, and renderer snapshot interpolation. It does not load the
GameCube DOL, execute PowerPC code, or include game data. The demo game only
proves the interfaces and is not a playable Melee build.

Build with Visual Studio or Clang from a x64 developer prompt:

```powershell
cmake -S native/windows/native -B build/native-shell -G Ninja
cmake --build build/native-shell
ctest --test-dir build/native-shell --output-on-failure
```

`NativeGameMemory` stores bytes in host memory and provides explicit big-endian
accessors for data that still needs that representation during migration.
`NativeArchive` validates big-endian headers, bounds, names, and duplicate
entries before exposing immutable data spans. `NativeTimingScheduler` advances
simulation at 60 Hz while a separate render clock can run at 120 Hz.
`XboxPadMapper` maps physical Xbox A to attack/confirm, B to special/back, and
X/Y to jump while retaining edge-triggered state. `RenderSnapshot` provides an
ordered, pointer-free handoff to a future D3D12 or Vulkan backend.
`NativeScene` adds stable object IDs, deterministic callback ordering, and safe
mutation during dispatch. `NativeFighter` and `NativeAudioMixer` demonstrate
typed gameplay and voice scheduling slices; the latter exposes a backend seam
for WASAPI/XAudio2.

New native systems should use typed C++ fields and pointers instead of guest
addresses. The existing matching/recompiled runtime remains the reference
while subsystems are ported incrementally; this shell is still not a playable
Melee build.

This repository is Theo's fully automated slop experiment. It is not intended
for serious use or investigation, and no support or maintenance is promised.
