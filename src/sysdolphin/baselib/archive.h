#ifndef _archive_h_
#define _archive_h_

#include <Runtime/platform.h>

#include <sysdolphin/baselib/forward.h> // IWYU pragma: export

#define HSD_ARCHIVE_DONT_FREE 1

struct HSD_ArchiveHeader {
    u32 file_size; /* 0x00 */
    u32 data_size; /* 0x04 */
    u32 nb_reloc;  /* 0x08 */
    u32 nb_public; /* 0x0C */
    u32 nb_extern; /* 0x10 */
    u8 version[4]; /* 0x14 */
    u32 pad[2];    /* 0x18 */
};
ASSERT_SIZE(struct HSD_ArchiveHeader, 0x20);

struct HSD_ArchiveRelocationInfo {
    u32 offset;
};

struct HSD_ArchivePublicInfo {
    u32 offset; /* 0x00 */
    u32 symbol; /* 0x04 */
};

struct HSD_ArchiveExternInfo {
    u32 offset; /* 0x00 */
    u32 symbol; /* 0x04 */
};

struct HSD_Archive {
    HSD_ArchiveHeader header;              /* 0x00 */
    u8* data;                              /* 0x20 */
    HSD_ArchiveRelocationInfo* reloc_info; /* 0x24 */
    HSD_ArchivePublicInfo* public_info;    /* 0x28 */
    HSD_ArchiveExternInfo* extern_info;    /* 0x2C */
    char* symbols;                         /* 0x30 */
    HSD_Archive* next;                     /* 0x34 */
    char* name;                            /* 0x38 */
    u32 flags;                             /* 0x3C */
    void* top_ptr;                         /* 0x40 */
};
ASSERT_SIZE(struct HSD_Archive, 0x44);

/// Relocates src in place. The archive borrows this writable GameCube data.
/// Call only on data that has not yet been relocated.
s32 HSD_ArchiveParse(HSD_Archive* archive, u8* src, size_t file_size);
/// Returns data for the first matching public name, or NULL if absent.
void* HSD_ArchiveGetPublicAddress(HSD_Archive* archive,
                                  const char* symbol_name);
/// Returns a borrowed external name by index, or NULL if the index is invalid.
char* HSD_ArchiveGetExtern(HSD_Archive* archive, int extern_index);
/// Patches the first matching name and consumes its encoded reference chain.
void HSD_ArchiveLocateExtern(HSD_Archive* archive, const char* symbol_name,
                             void* address);

#ifdef MELEE_NATIVE
/* The legacy archive handle is only an API token on the native host.  The
 * loader keeps the serialized file and its typed graph in a side table. */
void* HSD_ArchiveNativePublicAddress(HSD_Archive*, const char*);
/* Return the number of serialized data bytes from a native archive object
 * until the next public root. Returns zero when the pointer is not archive
 * backed. */
size_t HSD_ArchiveNativeDataLimit(const void*);
/* Native SIS roots retain the serialized pointer-word count so text indices
 * can be checked before mapping them to widened host records. */
size_t HSD_ArchiveNativeSisCount(const void* table);
void HSD_ArchiveNativeRelease(HSD_Archive*);
#endif

#endif
