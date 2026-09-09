# Native Windows shell (experimental)

This directory is the first bounded slice of a future source-level Windows
port. It builds a real 64-bit C++ executable with a host-owned frame loop,
typed game interface, and bounds-checked game memory. It does not load the
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
New native systems should use typed C++ fields and pointers instead of guest
addresses. The existing matching/recompiled runtime remains the reference
while subsystems are ported incrementally.

This repository is Theo's fully automated slop experiment. It is not intended
for serious use or investigation, and no support or maintenance is promised.
