#include "lbarchive.h"

#include <stdarg.h>
#include <string.h>

#include "lbdvd.h"
#include "lbfile.h"
#include "lbheap.h"
#include <dolphin/os.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/debug.h>

#ifdef MUST_MATCH
#pragma push
#pragma dont_inline on
#endif
void lbArchive_InitializeDAT(HSD_Archive* archive, void* data, size_t length)
{
#ifdef MELEE_NATIVE
    /* DAT files need a NativeArchive graph before their roots can be used.
     * Keep this legacy entry point fail-fast until its caller has migrated. */
    (void) archive;
    (void) data;
    (void) length;
    OSReport("lbArchive_InitializeDAT is unavailable on native hosts; use "
             "NativeArchive.\n");
    HSD_ASSERT(73, 0);
#else
    const char* extern_name;
    int extern_index = 0;

    if (HSD_ArchiveParse(archive, data, length) == -1) {
        OSReport("HSD_ArchiveParse error!\n");
        HSD_ASSERT(73, 0);
    }

    while (true) {
        extern_name = HSD_ArchiveGetExtern(archive, extern_index++);
        if (extern_name != NULL) {
            HSD_ArchiveLocateExtern(archive, extern_name, NULL);
        }
        if (extern_name == NULL) {
            return;
        }
    }
#endif
}
#ifdef MUST_MATCH
#pragma pop
#endif

void lbArchive_LoadSections(HSD_Archive* archive, void** symbol_dst, ...)
{
    const char* symbol_name;
    va_list symbol_args;

    va_start(symbol_args, symbol_dst);
    for (; symbol_dst != NULL; symbol_dst = va_arg(symbol_args, void**)) {
        symbol_name = va_arg(symbol_args, const char*);
        *symbol_dst = NULL;
        *symbol_dst = HSD_ArchiveGetPublicAddress(archive, symbol_name);
        if (*symbol_dst == NULL) {
            OSReport("Cannot find symbol %s.\n", symbol_name);
        }
    }
    va_end(symbol_args);
}

static inline HSD_Archive* loadArchive(const char* filename)
{
    HSD_Archive* archive;
    void* data;
    size_t length;

    data = lbHeap_80015BD0(0, OSRoundUp32B(lbFileGetSize(filename)));
    archive = lbHeap_80015BD0(0, sizeof(HSD_Archive));
    lbFile_8001668C(filename, data, &length);
    lbArchive_InitializeDAT(archive, data, length);
    return archive;
}

HSD_Archive* lbArchive_LoadArchive(const char* filename)
{
    return loadArchive(filename);
}

static inline void lbArchive_vLoadSectionsFatal(HSD_Archive* archive,
                                                void** symbol_dst,
                                                va_list symbol_args)
{
    const char* symbol_name;

    for (; symbol_dst != NULL; symbol_dst = va_arg(symbol_args, void**)) {
        symbol_name = va_arg(symbol_args, const char*);
        *symbol_dst = NULL;
        *symbol_dst = HSD_ArchiveGetPublicAddress(archive, symbol_name);
        if (*symbol_dst == NULL) {
            OSReport("Cannot find symbol %s.\n", symbol_name);
            HSD_ASSERT(112, 0);
        }
    }
}

static inline void lbArchive_vLoadSections(HSD_Archive* archive,
                                           void** symbol_dst,
                                           va_list symbol_args)
{
    const char* symbol_name;

    for (; symbol_dst != NULL; symbol_dst = va_arg(symbol_args, void**)) {
        symbol_name = va_arg(symbol_args, const char*);
        *symbol_dst = NULL;
        *symbol_dst = HSD_ArchiveGetPublicAddress(archive, symbol_name);
        if (*symbol_dst == NULL) {
            OSReport("Cannot find symbol %s.\n", symbol_name);
        }
    }
}

HSD_Archive* lbArchive_LoadSymbols(const char* filename, void* symbol_dst, ...)
{
    va_list symbol_args;
    HSD_Archive* archive;
    void* data;
    size_t length;
    u8 _[8];

    va_start(symbol_args, symbol_dst);

    data = lbHeap_80015BD0(0, OSRoundUp32B(lbFileGetSize(filename)));
    archive = lbHeap_80015BD0(0, sizeof(HSD_Archive));
    lbFile_8001668C(filename, data, &length);
    lbArchive_InitializeDAT(archive, data, length);
    lbArchive_vLoadSectionsFatal(archive, symbol_dst, symbol_args);

    va_end(symbol_args);
    return archive;
}

HSD_Archive* lbArchive_80016DBC(const char* filename, void* symbol_dst, ...)
{
    va_list symbol_args;
    HSD_Archive* archive;
    void* data;
    size_t length;
    u8 _[8];

    va_start(symbol_args, symbol_dst);

    data = lbHeap_80015BD0(0, OSRoundUp32B(lbFileGetSize(filename)));
    archive = lbHeap_80015BD0(0, sizeof(HSD_Archive));
    lbFile_8001668C(filename, data, &length);
    lbArchive_InitializeDAT(archive, data, length);
    lbArchive_vLoadSections(archive, symbol_dst, symbol_args);

    va_end(symbol_args);
    return archive;
}

void lbArchive_80016EFC(HSD_Archive* archive)
{
    HSD_ASSERT(0xFC, archive);
    HSD_ASSERT(0xFD, archive->flags & HSD_ARCHIVE_DONT_FREE);
    lbHeap_80015CA8(0, archive->data - sizeof(HSD_ArchiveHeader));
    lbHeap_80015CA8(0, archive);
}

bool lbArchive_80016F80(HSD_Archive** dst, const char* filename)
{
    void* data;
    size_t length;
    HSD_Archive* archive;
    bool preloaded;
    u8 _[8];

    archive = lbDvd_8001819C(filename);
    if (archive != NULL) {
        preloaded = true;
    } else {
        HSD_Archive* loaded_archive;
        data = lbHeap_80015BD0(0, OSRoundUp32B(lbFileGetSize(filename)));
        loaded_archive = lbHeap_80015BD0(0, sizeof(HSD_Archive));
        lbFile_8001668C(filename, data, &length);
        lbArchive_InitializeDAT(loaded_archive, data, length);
        archive = loaded_archive;
        preloaded = false;
    }
    if (dst != NULL) {
        *dst = archive;
    }
    return preloaded;
}

bool lbArchive_80017040(HSD_Archive** dst, const char* filename,
                        void* symbol_dst, ...)
{
    void* archive_data;
    HSD_Archive* loaded_archive;
    HSD_Archive* archive;
    bool preloaded;
    va_list symbol_args;

    va_start(symbol_args, symbol_dst);

    archive = lbDvd_8001819C(filename);
    if (archive != NULL) {
        preloaded = true;
    } else {
        // Inlined lbArchive_LoadArchive
        {
            void* data;
            size_t length;
            u32 pad;
            u32 pad2;
            data = lbHeap_80015BD0(0, OSRoundUp32B(lbFileGetSize(filename)));
            archive_data = data;
            loaded_archive = lbHeap_80015BD0(0, sizeof(HSD_Archive));
            lbFile_8001668C(filename, archive_data, &length);
            lbArchive_InitializeDAT(loaded_archive, archive_data, length);
            archive = loaded_archive;
        }
        preloaded = false;
    }

    lbArchive_vLoadSectionsFatal(archive, symbol_dst, symbol_args);

    va_end(symbol_args);

    if (dst != NULL) {
        *dst = archive;
    }
    return preloaded;
}

bool lbArchive_800171CC(HSD_Archive** dst, const char* filename,
                        void* symbol_dst, ...)
{
    void* archive_data;
    HSD_Archive* loaded_archive;
    HSD_Archive* archive;
    bool preloaded;
    va_list symbol_args;

    va_start(symbol_args, symbol_dst);

    archive = lbDvd_8001819C(filename);
    if (archive != NULL) {
        preloaded = true;
    } else {
        // Inlined lbArchive_LoadArchive
        {
            void* data;
            size_t length;
            u32 pad;
            u32 pad2;
            data = lbHeap_80015BD0(0, OSRoundUp32B(lbFileGetSize(filename)));
            archive_data = data;
            loaded_archive = lbHeap_80015BD0(0, sizeof(HSD_Archive));
            lbFile_8001668C(filename, archive_data, &length);
            lbArchive_InitializeDAT(loaded_archive, archive_data, length);
            archive = loaded_archive;
        }
        preloaded = false;
    }

    lbArchive_vLoadSections(archive, symbol_dst, symbol_args);

    va_end(symbol_args);

    if (dst != NULL) {
        *dst = archive;
    }
    return preloaded;
}

static inline void Locate(HSD_Archive* archive, intptr_t base_addr)
{
#ifdef MELEE_NATIVE
    (void) archive;
    (void) base_addr;
#else
    u32 reloc_index;
    u32 offset;

    for (reloc_index = 0; reloc_index < archive->header.nb_reloc;
         reloc_index++)
    {
        offset = archive->reloc_info[reloc_index].offset;
        *(intptr_t*) (archive->data + offset) += base_addr;
    }
#endif
}

int lbArchiveRelocate(HSD_Archive* archive, u8* src, size_t file_size,
                      intptr_t base_addr)
{
#ifdef MELEE_NATIVE
    (void) archive;
    (void) src;
    (void) file_size;
    (void) base_addr;
    OSReport("lbArchiveRelocate is unavailable on native hosts; use NativeArchive.\n");
    return -1;
#else
    size_t file_offset;

    if (archive == NULL) {
        return -1;
    }
    memset(archive, 0, sizeof(HSD_Archive));
    archive->flags |= HSD_ARCHIVE_DONT_FREE;
    memcpy(archive, src, sizeof(HSD_ArchiveHeader));

    if (archive->header.file_size != file_size) {
        OSReport("lbArchiveRelocate: byte-order mismatch! "
                 "Please check data format %x %x\n",
                 archive->header.file_size, file_size);
        return -1;
    }

    file_offset = sizeof(HSD_ArchiveHeader);
    if (archive->header.data_size != 0) {
        archive->data = src + file_offset;
        file_offset = archive->header.data_size + sizeof(HSD_ArchiveHeader);
    }
    if (archive->header.nb_reloc != 0) {
        archive->reloc_info = (HSD_ArchiveRelocationInfo*) (src + file_offset);
        file_offset +=
            archive->header.nb_reloc * sizeof(HSD_ArchiveRelocationInfo);
    }
    if (archive->header.nb_public != 0) {
        archive->public_info = (HSD_ArchivePublicInfo*) (src + file_offset);
        file_offset +=
            archive->header.nb_public * sizeof(HSD_ArchivePublicInfo);
    }
    if (archive->header.nb_extern != 0) {
        archive->extern_info = (HSD_ArchiveExternInfo*) (src + file_offset);
        file_offset +=
            archive->header.nb_extern * sizeof(HSD_ArchiveExternInfo);
    }
    if (file_offset < archive->header.file_size) {
        archive->symbols = (char*) (src + file_offset);
    }

    Locate(archive, base_addr);

    return 0;
#endif
}
