# Melee for Windows

This is Theo's fully automated slop experiment, based on `doldecomp/melee`.
It is not meant for serious use or investigation. No support, maintenance,
or human review is promised. This is not an official port or a maintained
project.

The Windows target translates the verified Melee US v1.02 executable into
an optimized x64 game DLL using LLVM 22.1.8 and runs it with ModernGekko's
MSVC-built Windows graphics, audio, and input runtime. You need your own
game data. Generated executables, disc files, textures, and saves stay
under ignored `build/` directories.

From PowerShell in the repository root, after building:

```powershell
powershell -ExecutionPolicy Bypass -File native/windows/run.ps1
```

Use WASD to move, J to confirm or attack, K to go back or use a special,
Enter for Start, and U/I to jump. The first XInput gamepad shares player 1
with the keyboard. F11 or Alt+Enter toggles fullscreen. Space holds the
runtime's fast-forward hotkey. Escape exits fullscreen, then closes the
game when pressed again.

Xbox mappings follow the Mac app: A confirms and attacks, B cancels and
uses specials, X/Y jump, RB grabs, and the D-pad moves. LT/RT provide
analog shield and engage digital shield at 90 percent travel. Existing
profiles can adopt these defaults with `-ResetInput`, which saves a backup.

The launcher uses the successful build's module manifest, or the optimized
`build/game-clang/gGALE01_recomp.dll` when no manifest exists. A missing
optimized module stops launch instead of using an older game DLL.

Fast disc loading and the Mac app's portable 60 FPS presentation settings
are enabled. Use `-OriginalDiscTiming` to retain simulated disc seek waits.
CPU and graphics work run on separate threads at normal 60 Hz game speed;
`-SingleCore` is available for comparisons.

The verified default build measured median 59.9936 FPS over 90 seconds in
a saved three-fighter Onett match with textures enabled. This is reported
runtime FPS for that scene; other scenes may perform differently. The
complete matching GameCube executable retained its required SHA-1.
The prerecorded opening movie can still stutter; live four-player gameplay
has not been tested.

See [the Windows build and launch guide](../../docs/native-windows.md) for
dependencies, textures, controller mappings, logs, and verification limits.
