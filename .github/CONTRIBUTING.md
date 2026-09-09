# Working on Melee for Mac

This is Theo's fully automated slop experiment. It is not meant for serious use
or investigation. No support, maintenance, or human review is promised.
Please do not volunteer time to investigate, audit, maintain, or upstream it.

These instructions apply when Theo explicitly requests a change. Keep that
work in [t3dotgg/melee4mac](https://github.com/t3dotgg/melee4mac).
Do not open pull requests, issues, or automated reviews on the official
`doldecomp/melee` repository for work from this fork.

The coding conventions below come from the original project. The fork workflow
and automation rules in this document apply to this fork.

# Sections

- [Introduction](#introduction)
- [Coding Style and Formatting](#coding-style-and-formatting)
- [Compiler Notes](#compiler-notes)
- [Pull Requests](#prs)
- [AI Assistance](#ai)


# <a name="introduction"></a>Introduction

Read the [build guide](../docs/build-and-run.md) and [source map](../docs/code-map.md)
before changing unfamiliar code. Use callers, data, and SDK definitions to support
names and types. Keep uncertain meanings marked as uncertain.

Keep cleanup changes small and preserve gameplay behavior. Put gameplay mods on
separate branches. Give parallel agents separate worktrees and owned files.

# <a name="coding-style-and-formatting"></a>Coding Style and Formatting

- [Caveats](#caveats)
- [Auto formatting](#auto-formatting)
- [Functions](#functions)
- [Structs](#structs)
- [Conditionals](#conditionals)
- [Variables](#variables)
- [Primitives](#primitives)
- [Enums](#enums)
- [Literals](#literals)
- [Headers](#headers)

## <a name="caveats"></a>Caveats

The original source is the gold standard for this decompilation project.

While the style-guide below should be closely followed, there are times where convention may be reasonably broken in order to more closely reflect what the original developers wrote.  Some strings embedded in the binary provide some indication about what some things were named.

In cases where there is a clear connection between a string embedded in the binary and an identifier in the decompiled code, we break convention and prioritize the identifier name from the original source.

## <a name="auto-formatting"></a>Auto Formatting

Run the pinned `clang-format` on edited C and header files. Avoid formatting
unrelated files during a cleanup. [`pre-commit`](https://pre-commit.com/) is
included in the development packages:

```
pip install -r reqs/dev.txt # Install the pre-commit package
pre-commit install # Install the commit hook
pre-commit run --files src/path/to/edited.c src/path/to/edited.h
```

## <a name="functions"></a>Functions

- Avoid naming a function that you are not matching. If you have not matched to understand the functionality, don't expect that someone else did just because they named it.
- Naming exported methods (the majority of cases)
    - Applies to any method that is defined in a .h file, with a .c implementation.
    - Prefix functions within Melee related code with the file's name. While asserts may indicate this was not the case for your function, this is to make the code easier to read.
    - Use `lowerCamelCasing` for the function's filename.
    - Use `UpperCamelCasing` for the function's name itself, unless it is a standard library reimplementation.
    - Examples:
      ```
      lbVector_sin
      lbArchive_Parse
      Player_DoThing
      Stage_GetLeftBlastZone
      ftFox_LaserOnDeath
      ```
- Naming non-exported/local static inlines
    - Applies only to methods that have no accomanying .h definition. These are local and only used in the .c file where they are created.
    - Use lowerCamelCasing for the function name
    - Examples:
      ```
      someInlineMethod
      someNonExportedThing
      getFooID
      getFighterCKind
      ```
- Empty stub functions should use `{}` rather than `{ return; }`
- Arguments should be snake_case
- Oftentimes a method will return a u8, but the associated values are enum values. In these situations, document the actual enum type clearly in doxygen. See the [Enum Types](#enum-types) section of this doc for more details.

## <a name="structs"></a>Structs

- Structs should include explicit information about offsets
    - Top-level Struct offsets begin at 0.
    - Struct members should include the offsets of their members as an inline comment before the type declaration.
    - Even when the struct member is well understood and named, the offset comment should be retained.
    - Struct offset comments must all be the same width and right-aligned.
    - Bitfields should include their bit offset.
- Struct definitions should never contain child structs, except as placeholders for poorly understood structs
- When a struct members name/purpose is unknown, it should be written as `x<Hex-Offset>`.
    - If it's an unknown bit field, the format would be `x<Hex-Offset>_<Bit_Index>`
- Struct offsets should contain bit offsets when applicable
- Bitmasks should be defined via union
- When a section of a struct is understood to be padding, it should be written as `pad_<Hex-Offset>` and use a char array.
- Struct member names should be snake_case.
- Struct definitions should be defined in `types.h` for a given module.
- Typedefs associated with a given struct should be in the sibling `forward.h` file in the same module.
- Examples
  - Yes:
    ```c
    struct Player {
        /*  +0   */ u32 x0;
        /*  +4   */ char pad_4[0xC];
        /* +10   */ short x10;
        /* +12   */ u8 well_known_something;
        /* +13:0 */ bool enable_rumble : 1;
        /* +13:1 */ bool x13_1 : 1;
        /* +13:2 */ bool x13_2 : 1;
        /* +13:3 */ bool is_invisible : 1;
        /* +13:4 */ bool x13_4 : 1;
        /* +13:5 */ bool is_metal : 1;
        /* +13:6 */ bool x13_6 : 1;
        /* +13:7 */ bool x13_7 : 1;
    };
    ```
  - No:
    ```c
    struct Player {
        u8 thing;
        u32 something;
        u32 something2;
        u32 something3;
        u8 well_known_something;
        u8 unkb : 1;
        u8 unkb2 : 1;
        u8 unkb3 : 1;
        u8 unkb4 : 1;
        u8 unkb5 : 1;
        u8 unkb6 : 1;
        u8 unkb7 : 1;
        u8 unkb8 : 1;
    };
    ```

- **Do not copy structs from another project**

## <a name="conditionals"></a>Conditionals
- Make NULL checks explicit
    - Yes: `if (ptr != NULL)`
    - No: `if (!ptr)`

## <a name="variables"></a>Variables
- Variable names should be `snake_case`
- Variable names should be descriptive for functionality that is well understood
- Examples:
    - Good:
      ```c
      bool luigi_unlocked = gm_IsCKindUnlocked(CKIND_LUIGI)
      ```
    - Acceptable:
      ```c
      bool intr = gm_8015CDC8(CKIND_LUIGI)
      ```
    - Bad:
      ```c
      bool b = gm_IsCKindUnlocked(CKIND_LUIGI)
      ```

## <a name="primitives"></a>Primitives

### Numeric types

Generally speaking, `int` or `unsigned int` should be the preferred type for whole numbers, unless there is some specific context about a piece of code or struct that dictates a specific bit width. In practice, `int` is more likely to be used throughout a codebase, and the goal of this decmopilation project is to try to match the original source.

When explicit fixed-width numbers are necessary, use the following types:
- Booleans: `bool`
- 8-bit  whole: `u8` or `s8`
- 16-bit whole: `unsigned short` or `short`
- 32-bit whole: `u32` or `s32`
- 64-bit whole: `u64` or `s64`
- 32-bit floating point: `float`
- 64-bit floating point: `double`

See the [Integer Types](#integer-types) section for more information about techinical quirks surrounding various integer types.

### Bitfields and Bitflags

Melee sometimes packs bits in to an unsigned integer type as bitflags and sometimes uses bitfields as structs with individual fields for each flag. The appropriate type/structure to use is highly dependent on what the decompilation dictates.

- When decompilation dictates that a field use packed bitflags, use the appropriate fixed-width unsigned type (`u8`, `u16`, `u32`, or `u64`)
- When decompilation dictates that a field use bitfields, follow the [struct rules](#structs) for defining those fields.

### Specialized types
- Single character: `char`
- Strings: `char*`
- Unknown 32-bit pointer: `UNK_T`.
- Enums: `enum_t` as a placeholder until the actual enum is defined.
- OS Timestamps: `OSTime`
- Struct padding: `char`

## <a name="enums"></a>Enums
- Enum declarations should include a clear means of determining the value for each entry
  - May be a comment prefix with the hexadecimal value. There must be enough left-padded 0s so that each comment is the same width.
  - May be an explicit value decl such as `FOO_BAR = 4`
- Each enum value for a given type should have a unique prefix that makes them easy to identify as a group.
- Enum value names should be descriptive when well understood.
- Enum value names should contain their offset when not well understood.
- Enum value names should try to follow whatever is embedded in the melee
  binary's strings.
    - When not found in the binary or uncertain, use `SCREAMING_SNAKE_CASE`
- Enum definitions should be defined in `forward.h` for a given module.
- Examples:
    - Good:
      ```c
      typedef enum CharacterKind {
          /*  0x0 */ CKIND_CAPTAIN,
          /*  0x1 */ CKIND_DONKEY,
          /*  0x2 */ CKIND_FOX,
          ...
          /* 0x20 */ CHKIND_POPO,
          /* 0x21 */ CHKIND_NONE,
          /* 0x22 */ CHKIND_MAX = CHKIND_NONE
      } CharacterKind;
      ```
    - Acceptable:
      ```c
      typedef enum FooKind {
          /* 0x0 */ FOO_BAR,
          /* 0x1 */ FOO_BAZ,
          /* 0x2 */ FOO_0x2,
          /* 0x3 */ FOO_WOOZLE,
          /* 0x4 */ FOO_0x4,
          ...
          /* 0xA */ FOO_WOZZLE,
          /* 0xB */ FOO_MAX,
      } FooKind;
      ```
    - Bad:
      ```c
      typedef enum FooKind {
          FOO_BAR,
          FOO_BAZ,
          FOO_0x2,
          FOO_WOOZLE,
          /* 0x4 */  FOO_0x3,
          ...
          /* 0x1A */ FOO_WOZZLE,
          /* 0x1B */ FOO_MAX,
      } FooKind;
      ```

## <a name="literals"></a>Literals
- Integer literals
  - Yes: `123U` for `u32`
  - Yes: `123456L` for `s64`
  - Yes: `123456LU` for `u64`
  - Yes: `0xABC` for hexidecimal
  - No: `0XABC`
- Floating-point literals
  - Yes: `1.23F` for `f32`
  - No: `1.23f`
  - Yes: `1.23L` for `f64`
  - No: `1.23`
  - Yes: `1.23e-5F`
  - No: `1.23E-5F`
  - No:
    - `1.`
    - `1.F`
    - `1.f`
    - `1.L`
  - No: `1` for `f32` or `f64`

## <a name="headers"></a>Headers

- Every TU header (meaning things that actually appear in `splits.txt`) **must** contain its own symbols according to the split, and **only** those members. Extra inlines, types, etc. are not allowed. Symbols must appear in address order. `static` symbols should appear at the top of the C file, not the header.
- A TU may define a `*.static.h` file. This is a temporary file to aid in m2c decompilation runs, so it should be considered as part of the C file, and entirely private. It should be deleted when the TU is linked (marked as `Matching`).
- `types.h` for each module contains relevant types primary to that module. It's difficult to discern where a type truly belongs, so we're not very strict about that. Types should be only `struct X` and/or `union X` definitions, not `typedef`s.
- One `forward.h` corresponds to each `types.h` and is responsible for providing a `typedef` for each struct and union, and typedefs of function pointers. It is also the place to define `enum`s, global `const` simple types like integers that don't get emitted as symbols, and `#define` constants. The purpose of this separation is to avoid circular inclusions.
- If an `inline` function is shared by multiple TUs but its function body isn't a real symbol, it goes in `inlines.h` for the most relevant module. An exception is made if that inline function contains an assert which proves its source file.
- Keep short explanations of non-obvious behavior and matching constraints near the C or header code. Put longer walkthroughs in `docs/code/` or existing `*.dox` files. Avoid comments that only repeat the code.

# <a name="compiler-notes"></a>Compiler Notes

This section documents quirks/gotchas related to the C language and mwcc.

## <a name="integer-types"></a>Integer Types

`char`, `short`, `long`, and `long long` all tell the compiler specific bit widths to use for a given type. For the gamecube, these are `8`, `16`, `32`, and `64` bits respectively.

The `int` type provides no such indication to the compiler, specifying only that the platform-native int type should be used. This lets the compiler try to be smart about what it does for a given value, contextually making optimizations in the binary.  This can lead to situations where the compiler may emit opcodes that make an int type appear as any width from 8 to 32 bits in decomp.  In practice, A signed 8-bit integer (`s8`) declaration in particular is extremely rare, and is usually really an `int` in disguise.

## <a name="enum-types"></a>Enum Types

Enum typedefs in this project always resolve to `int` currently.  This is a quirk of the current compiler setup. Often a method will return a `u8`, but it's for an enum return value. Check related docs and use-sites to determine the actual type.

# <a name="prs"></a>Pull Requests

- Rebase onto the latest `master` from `t3dotgg/melee4mac` before opening a pull request.
- Use `gh pr create --repo t3dotgg/melee4mac --base master` to set the target explicitly.
- Open a real pull request within this fork when a review record is useful.
- State the problem, the change, and the actual checks run. Name the public model and harness that made the change. If the model is unknown, name only the harness.
- Check the target repository before every push or GitHub write.

For matching GameCube code, headers, and build changes, run `python tools/verify.py` after
integration. The US v1.02 executable must keep SHA-1
`08e0bf20134dfcb260699671004527b2d6bb1a45`. The command checks source completion,
runs `ninja diff`, and checks the complete executable. Do not change the expected
hash or mark a source file incomplete to make a cleanup pass.

For the direct ARM64 port, use the native compile inventory, sanitizer tests,
and real native game runs described in `docs/native-arm64-source.md`. Skip the
Intel compiler comparison. Do not install or use Rosetta for this work.

Run clang-format on edited C and header files. Run the source checker and focused
tests for changed tools. Public CI also builds the native static library. It has
no original game data, so its success does not certify a matching GameCube build.

Keep game images, extracted assets, and built game files out of commits and
GitHub uploads. Use ignored `orig/` and `build/` paths for local files.

# <a name="ai"></a>AI Assistance

When Theo requests a change, agents can implement it and run targeted checks.
Keep the slop notice in the change record. Do not present an agent's review as
human review or imply that this fork has support or maintenance commitments.
Use independent review when requested work needs source or SDK evidence.

Prefer local names and private helpers before changing shared interfaces.
Preserve known original names and struct layouts. A matching executable proves
that the binary is unchanged. It does not prove that new names or comments are
correct.
