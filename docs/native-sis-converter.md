# Native SIS archive conversion

SIS font data is a typed archive root. It cannot be used by casting a DAT
pointer on an ARM64 host.

Each serialized SIS entry is eight bytes. The two big-endian words are offsets
to kerning data and glyph texture data. `SIS` stores two host pointers. A
converter must read the words as four-byte offsets, allocate a host `SIS`
array, and copy or share the referenced byte ranges. It must never write a
host pointer into the DAT data.

The kerning table is addressed as a byte array by the text renderer. Current
code can read offsets through `0x1FFFE`, so a converter must validate a
`0x20000` byte range before exposing a non-null kerning pointer. A
`TextGlyphTexture` is `0x200` bytes and the built-in atlas has 287 entries.
Texture ranges must be checked before conversion. Shared offsets must return
the same host allocation, and the allocation must remain live while the SIS
archive is registered.

The public SIS root is a table. Its length is not present in the archive
header. Callers use indices through at least `0x153`, and some roots overlay
`SisFontData` with pointer arrays at offsets `0x4B8` and `0x4E8`. A converter
must receive an explicit entry count from the caller, or derive and validate a
root-specific schema. It must reject a table that exceeds the archive data
range.

`grpstadium.c` replaces `HSD_SisLib_804D1124[1][2].textures` with a temporary
scratch atlas. The converted table must be mutable. Its lifetime must outlast
the replacement and the related stage state.

The remaining bridge is between `HSD_SisLib_803A62A0`, which currently gets an
`HSD_Archive*`, and `NativeArchiveGraph`, which owns typed allocations. The
bridge must register the graph and archive together, convert the public symbol
through a native API, and release the graph only after all users release the
archive. Returning a raw DAT root or a pointer into `NativeArchive` is unsafe.
