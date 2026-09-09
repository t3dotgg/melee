# Design: source-level native Windows port

**Melee for Mac is a fully automated slop experiment. This document records a
requested design direction; it is not a promise of support, maintenance, or
upstream work.**

## Meaning of "native"

The current Windows target is a native x64 host process, but its game module is
generated from the verified PowerPC DOL. Static recompilation removes the
interpreter from the normal path while retaining the PowerPC ABI, a 32-bit
GameCube address space, big-endian data, fixed globals, and GameCube service
calls. A source-level port has a different contract:

* game rules compile as ordinary Windows x64 C/C++ and use host pointers;
* simulation owns typed state rather than a CPUState register file and guest RAM;
* assets are decoded to host-endian values and stable handles;
* graphics, audio, input, storage, timing, and threading are Windows services;
* no generated dispatch, PowerPC exception emulation, or fixed 0x80xxxxxx
  address is required at runtime.

The matching DOL/recompiler remains the behavioral reference. This port must not
modify orig/GALE01/sys/main.dol, its SHA-1
08e0bf20134dfcb260699671004527b2d6bb1a45, or the matching target.

## Source assumptions and replacements

| Area | Evidence in this source | Native replacement |
| --- | --- | --- |
| CPU/control flow | DolRecomp functions read/write CPUState and return through generated dispatch; native/windows patches add strict-native and idle behavior. | Delete generated code from the game path. Call C functions through subsystem interfaces. Keep a reference adapter for replay comparison. |
| Address space/globals | M2C_FIELD, 0x803/0x804 globals, and u32-to-pointer casts occur throughout melee and baselib. debugconsole_main.c and lbheap.c model 0x80000000 memory ranges. | Replace address macros with GameState, Scene, Player, and AssetStore ownership. Keep offsets only in a compatibility decoder. |
| Width/layout | archive.h asserts HSD_Archive size 0x44; structs contain GameCube padding and callback pointers. | Use fixed-width Gc wire structs for files and separate Native runtime structs. Never reinterpret a wire struct as a native object. |
| Endianness | archive.c copies headers and relocation words directly and adds the data base. The archive guide documents 32-bit pointer words and GameCube byte order. | Bounds-checked read_be16/32 and float conversion; archive linker returns host pointers/handles and does not mutate source bytes. |
| Heaps/allocation | lbheap.c owns six slots, ARAM, compaction, and interrupt-protected allocation; class.c and objalloc.c implement pools. | Explicit Persistent/Scene/Frame/Audio arenas using VirtualAlloc or pmr, generation IDs, and RAII. Preserve pools only where profiling requires them. |
| Callbacks/objects | gobjproc.c, gobjplink.c, and gobjgxlink.c maintain priority lists, attachment cleanup, and render passes; callbacks are indirect data pointers. | Object IDs and typed component/callback vectors. Preserve priority and insertion order; queue mutations during iteration; render from immutable snapshots. |
| Archives/animation | archive.c, lbarchive.c, ftdata.c, and aobj.c rely on relocation chains, symbols, in-place buffers, and track bytecode. DAT files also provide attributes/models. | Validating importer converts tracks and metadata to host-endian arrays and AssetId handles, retaining source offsets for diagnostics. |
| GX rendering | cobj.c, jobj.c, dobj.c, tobj.c, gobjgxlink.c, and displayfunc.c use GX state, TEV, display-list order, XFB, and VI timing. | D3D12 renderer (Vulkan optional) consuming a Melee render IR for camera, material, mesh, texture, and ordered passes. Keep screenshot compatibility mode. |
| Audio | axdriver.c and synth.c use AX voices, command streams, ARAM sample addresses, DVD reads, and mixer callbacks. | Decode banks/streams to host buffers and schedule voices on WASAPI/XAudio2. Preserve priority, pan, pitch, aux effects, and completion callbacks. |
| Input | controller.c queues four PADStatus channels, clamps/scales sticks, derives direction bits, and maintains history; lb_0195.c schedules alarms and gm_1A45.c waits for the queue. | XInput/Raw Input (SDL adapter optional) feeds timestamped PadState. At each 60 Hz tick reproduce trigger/release/repeat semantics. Keep Xbox A attack/confirm, B special/back, X/Y jump. |
| Disc/loading | lbdvd.c, lbarchive.c, and devcom.c expect DVD entry numbers, asynchronous callbacks, modeled seek/transfer timing, and heap buffers. | Async package/file service rooted at extracted assets. Make ordering, cancellation, and fast-loading policy explicit. |
| Timing/threads | OSDisableInterrupts, alarms, VI retrace, scheduler queues, and OSTime are used throughout. | steady_clock, fixed 60 Hz simulation scheduler, separate 60/120 render clock, and mutex or single-thread ownership. |
| Save state/card | Runtime save states serialize guest RAM/registers; game saves use memory-card blocks and fixed ranges. | Versioned native snapshot with CRC and migration; separate memory-card import/export. Include RNG, timers, object IDs, animation cursors, and pending events. |
| Exceptions/diagnostics | OS vectors, OSReport, assertions, and uncovered-PC failures are part of strict-native behavior. | Structured Windows logs and crash dumps with subsystem/frame IDs; preserve reference traces to explain drift. |

Unknown fields and pointer meanings remain in Gc wire structs until a fixture or
trace establishes their semantics. A field offset alone is not evidence.

## Target interfaces

Create native/windows/source_port as a library separate from the matching
target:

    Platform: Clock, FileSystem/Disc, Input, Audio, Renderer, Window
    Simulation: GameState, SceneLoader, Scheduler, SaveState

Simulation is the only writer of GameState. Renderer receives an immutable
RenderSnapshot and may interpolate two snapshots at 120 Hz without advancing
rules. Platform owns threads and OS handles. Interfaces return status objects
with frame IDs for diagnostics.

## Staged implementation

1. **Harness and trace (2–4 weeks).** Add a null-renderer source-port executable.
   Capture deterministic input, RNG, frame counters, object lifetime, collision
   results, and asset names from the static build. Golden traces cover menus,
   one stage, and one fighter matchup.
2. **Data/platform primitives (2–4 weeks).** Implement endian-safe readers,
   archive validation/linking, package I/O, clocks, PadState, and arenas. Fuzz
   malformed archives and test scene lifetime.
3. **Object/scene kernel (4–8 weeks).** Port GObj ordering, JObj/DObj
   transforms, callbacks, animation tracks, and scene transitions. Compare
   callback order, transforms, and frame hashes.
4. **Simulation slices.** Port input/state machines, collision and stage
   geometry, items, camera, menus, and results one slice at a time. Start with a
   training stage and small roster; expand only after deterministic replay.
5. **Renderer/assets (6–12 weeks).** Translate GX state, TEV/materials, fog,
   particles, UI, texture formats, and readback. Add interpolation/120 Hz only
   after 60 Hz simulation is stable.
6. **Audio/hardware input (2–4 weeks).** Add mixer/streaming, rumble, XInput,
   focus, and calibration; validate latency and voice ordering.
7. **Persistence/packaging (2–6 weeks).** Add save migration, card import/export,
   launcher, crash reports, full roster/stage/assets, and unsupported-content
   compatibility mode.

Estimates are order-of-magnitude engineering time, not promises. A playable
slice is feasible before full mode/roster parity; complete parity requires
substantial DAT and unknown-field reverse engineering.

## Current implementation status

The `native/windows/native` target currently contains the host-side foundation:
validated big-endian MARC asset parsing, rooted synchronous/asynchronous asset
I/O, typed XInput mapping, a deterministic 60 Hz simulation scheduler with an
independent 120 Hz render clock, and an ordered render snapshot/interpolation
interface. These components build as a 64-bit Windows executable without the
DOL or GameCube SDK and have focused CTest coverage. The executable is a shell
and deterministic demo game; a small typed fighter rules slice now covers
movement, jump, gravity, attack, and special actions. Full fighter rules,
scenes, collision, GX-compatible rendering, audio, and the complete asset
catalog still require source-level ports.
The object/scene kernel now provides stable IDs, deterministic priority order,
safe callback mutation, and pointer-free snapshots. A deterministic null audio
mixer also provides typed voices, priority eviction, stop/completion semantics,
and a backend seam for WASAPI/XAudio2.

## Acceptance tests

* Source-port x64 target links without DolRecomp, RecompCore, CPUState, or
  GameCube SDK symbols/headers.
* Archive fixtures round-trip big-endian integers/floats, reject truncation and
  out-of-range tables, and produce stable named assets.
* Scene arenas reclaim all allocations; ASan/UBSan and guard pages find no
  use-after-free; no native pointer is serialized as 32 bits.
* Callback priority, equal-priority insertion, removal during callbacks, and
  render-list order match golden gobj traces.
* Recorded PAD queues reproduce trigger/release/repeat and analog values at
  frame boundaries; Xbox face mapping is verified.
* Fixed-input replay matches frame counters, stocks, damage, collision events,
  RNG checkpoints, and scene transitions for each golden slice.
* Menu/stage screenshot hashes match documented tolerance; 120 Hz presents two
  snapshots per 60 Hz rule tick.
* Audio voice start/stop/priority/pitch/pan traces match, including null backend
  completion callbacks.
* Async reads preserve completion ordering and cancellation; cold/warm loading
  are measured separately without skipped simulation frames.
* Native save round-trip/migration passes; card import/export remains byte
  identical; full smoke matrix has clean shutdown and no leaked threads.

## Mechanical versus reverse-engineering work

Mechanical work includes endian-safe accessors, obvious OS/allocator wrappers,
typed IDs, controller adapters, and platform backends. It does not establish
gameplay semantics. Reverse engineering is required for unknown structure
fields, DAT/animation formats, GX TEV and display-list ordering, audio command
streams, scheduler edge cases, save compatibility, and rules dependent on the
GameCube scheduler. Keep the static build as an oracle and require a trace or
fixture before deleting a compatibility path.
The shell demo now drives the fighter through the scene kernel, so its host
loop exercises typed simulation state, callback dispatch, and render-object
updates together.
The collision slice adds typed stage bounds, hitboxes, deterministic overlap
events, and damage ordering. It is a foundation for replacing the original
collision and stage geometry routines; it is not yet Melee-complete geometry.
`NativeTrainingMatch` composes two typed fighters, hitbox generation, stage
collision, and render snapshots into a deterministic training-mode slice.
The Windows input boundary now dynamically loads XInput 1.4/1.3 and converts
real controller packets into the typed mapper, while cleanly handling systems
with no XInput DLL or connected controller.
Typed Persistent, Scene, Frame, and Audio arenas now provide alignment-safe
allocation, generation-checked handles, and deterministic reset behavior in
place of the GameCube heap and raw pointer ranges.
The renderer backend contract now has a headless implementation for deterministic
tests and a Windows D3D12 capability probe; a swap-chain frontend and GX/TEV
translation still remain to be implemented.
The native target also initializes and releases a real D3D12 device through the
Windows loader, selecting the highest available feature level. Command lists,
swap-chain ownership, shaders, and GX/TEV translation remain separate work.
The native frontend now owns a real Win32 window handle and message pump,
including hidden-window operation for headless tests.
The current Windows target now also creates an `IDXGISwapChain3` flip-model
chain with optional tearing and exercises `Present` successfully on this PC;
render-target resources, command recording, shaders, and GX/TEV translation
remain to be connected.
The asset layer now decodes GX I4/I8/IA4/IA8, RGB565, RGB5A3, and RGBA8 tiled
blocks into host RGBA8 pixels with dimension and truncation checks. Material,
palette, mipmap, and TEV state conversion still remain.
`NativeDisc` now maps validated numeric entry IDs to the rooted asset service,
providing synchronous and ordered asynchronous reads with cancellation. DVD
seek timing and the complete original disc table still need to be derived from
game traces.
