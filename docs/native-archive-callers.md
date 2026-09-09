# Native archive migration inventory

This list records every path that still uses the original `HSD_Archive` API. The
native build must move each path to `NativeArchive` and a typed conversion
before it can dereference data from a DAT file. `HSD_Archive` stores GameCube
layout and four-byte archive offsets. It cannot hold relocated host pointers.

## Legacy parser and relocation calls

These calls are the direct entry points that need conversion first:

| File | Function or branch | Native status |
| --- | --- | --- |
| `src/melee/lb/lbarchive.c` | `lbArchive_InitializeDAT` | Calls `HSD_ArchiveParse`. Native builds reject it. This is the shared entry point for most archive loads. |
| `src/melee/gr/grdatfiles.c` | `grDatFiles_801C5FC0`, `grDatFiles_801C6478` | Calls `lbArchive_InitializeDAT`; stage map roots and particle banks need typed stage and particle schemas. |
| `src/melee/ef/efasync.c` | `efAsync_OnLoad` | Calls `lbArchive_InitializeDAT`; effect archive and particle bank roots need typed schemas. |
| `src/melee/lb/lbdvd.c` | `lbDvd_GetPreloadedArchive` type 2, 3, and 4 entries | Dispatches to the three paths above after asynchronous disc reads. |
| `src/melee/ft/ftdata.c` | `ftData_80085CD8`, non-native branch | Calls `lbArchiveRelocate` or `HSD_ArchiveParse`; excluded by `#ifdef MELEE_NATIVE`. |
| `src/melee/ft/ftdata.c` | `ftData_80085E50`, non-native branch | Calls `lbArchiveRelocate` or `HSD_ArchiveParse`; excluded by `#ifdef MELEE_NATIVE`. |

The native fighter path in `ftData_80085A14` already opens one `NativeArchive`
per fighter kind, creates a `NativeArchiveGraph`, and converts each FigaTree.
The native branches of `ftData_80085CD8` and `ftData_80085E50` then use those
host FigaTree objects. The source still contains the legacy calls for the
original GameCube build, but they are not compiled into the native target.

## Shared wrapper callers

`lbArchive_LoadArchive`, `lbArchive_LoadSymbols`, `lbArchive_80016DBC`,
`lbArchive_80016F80`, `lbArchive_80017040`, and `lbArchive_800171CC` all funnel
through `lbArchive_InitializeDAT`. The wrappers are used by fighter common and
costume data, effects, stages, game modes, menus, interface scenes, trophies,
visuals, and toys. A native wrapper cannot return a raw serialized root to
existing code. Each symbol needs a schema conversion that creates host
structures and resolves references without modifying the DAT bytes.

High priority startup callers are:

- `src/melee/ft/ftdata.c`: fighter common data and costume archives.
- `src/melee/ef/efasync.c`: effect DAT files loaded during fighter setup.
- `src/melee/gr/grdatfiles.c`: stage collision, ground parameters, and particle
  banks.
- `src/sysdolphin/baselib/sislib.c` and `src/melee/if/ifall.c`: SIS fonts and
  common interface scenes.
- `src/melee/gm/gmmain.c` and its game mode initializers: menu and match data.

## Current typed schemas

`native/source/assets/descriptors.c` currently converts joint, animation,
AObj, WObj, FObj, matrices, strings, byte streams, and FigaTree graphs. It does
not yet convert stage parameter records, particle banks, SIS/font data, item
and effect tables, or general scene roots. Those schemas are required before a
real image can boot into a match.

To audit the source after changes:

```sh
rg -n 'HSD_ArchiveParse|lbArchiveRelocate|lbArchive_InitializeDAT' src
python3 native/source/compile.py --jobs 8
```

The first command may show the two original-build branches in `ftdata.c`.
The native preprocessor path must contain no executable call to either legacy
parser or relocation function.
