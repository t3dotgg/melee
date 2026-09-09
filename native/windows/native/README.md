# Native Windows shell (experimental)

This directory is a bounded source-level Windows port slice. It builds a real
64-bit C++ executable with a host-owned frame loop, typed game interface,
bounds-checked game memory, validated asset archives, typed Xbox input,
fixed-rate timing, and renderer snapshot interpolation. On Windows with a
D3D12 adapter it creates a visible Win32 flip-model window, records a native
render-target clear pass, fences the direct queue, and presents at the 120 Hz
render cadence. It does not load the GameCube DOL, execute PowerPC code, or
include game data. The demo game remains a deterministic training shell rather
than a complete playable Melee port.

Build with Visual Studio or Clang from a x64 developer prompt:

```powershell
cmake -S native/windows/native -B build/native-shell -G Ninja
cmake --build build/native-shell
ctest --test-dir build/native-shell --output-on-failure
build/native-shell/melee_native_shell.exe --frames 120
```

Pass `--training` to run the executable-facing two-fighter native rules slice;
combine it with `--frames N` for a bounded smoke run:

```powershell
build/native-shell/melee_native_shell.exe --training --frames 120
```

The first Xbox pad drives player one. Player two is present in the simulation,
collision pass, and render snapshot but remains idle until a multi-pad input
source is wired in.

`NativeGameMemory` stores bytes in host memory and provides explicit big-endian
accessors for data that still needs that representation during migration.
`NativeArchive` validates big-endian headers, bounds, names, and duplicate
entries before exposing immutable data spans. `NativeTimingScheduler` advances
simulation at 60 Hz while a separate render clock can run at 120 Hz.
`XboxPadMapper` maps physical Xbox A to attack/confirm, B to special/back, and
X/Y to jump while retaining edge-triggered state. `RenderSnapshot` provides an
ordered, pointer-free handoff to the renderer;
`NativeWin32SwapChain::clear_and_present` is the first concrete D3D12 command
path and is covered by `native_gpu_pass`.
`build_proxy_geometry` turns ordered snapshots into validated host vertices and
indexed draw ranges for the next material and shader stage.
`NativeScene` adds stable object IDs, deterministic callback ordering, and safe
mutation during dispatch. `NativeFighter` and `NativeAudioMixer` demonstrate
typed gameplay and voice scheduling slices; the latter exposes a backend seam
for WASAPI/XAudio2. `NativeRendererBackend` provides a pointer-free submission
contract, a headless implementation for tests, and a D3D12 capability probe.

New native systems should use typed C++ fields and pointers instead of guest
addresses. The existing matching/recompiled runtime remains the reference
while subsystems are ported incrementally; this shell is still not a playable
Melee build.

This repository is Theo's fully automated slop experiment. It is not intended
for serious use or investigation, and no support or maintenance is promised.
