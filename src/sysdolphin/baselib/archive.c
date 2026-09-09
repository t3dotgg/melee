#include "archive.h"

#include <string.h>

#include <dolphin/os.h>

#ifdef MELEE_NATIVE
__attribute__((weak)) void*
HSD_ArchiveNativePublicAddress(HSD_Archive* archive, const char* symbol)
{
    if (archive == NULL || symbol == NULL || archive->symbols == NULL ||
        archive->public_info == NULL || archive->data == NULL)
    {
        return NULL;
    }
    for (u32 i = 0; i < archive->header.nb_public; i++) {
        u8* info = (u8*) archive->public_info + (size_t) i * 8u;
        u32 symbol_offset = ((u32) info[4] << 24) | ((u32) info[5] << 16) |
                            ((u32) info[6] << 8) | info[7];
        u32 target_offset = ((u32) info[0] << 24) | ((u32) info[1] << 16) |
                            ((u32) info[2] << 8) | info[3];
        size_t symbols_offset =
            archive->top_ptr == NULL
                ? archive->header.file_size
                : (size_t) (archive->symbols - (char*) archive->top_ptr);
        if (symbols_offset > archive->header.file_size ||
            symbol_offset >= archive->header.file_size - symbols_offset ||
            target_offset > archive->header.data_size)
        {
            continue;
        }
        if (strcmp(archive->symbols + symbol_offset, symbol) == 0) {
            return archive->data + target_offset;
        }
    }
    return NULL;
}

__attribute__((weak)) size_t HSD_ArchiveNativeSisCount(const void* table)
{
    (void) table;
    return 0;
}

__attribute__((weak)) void HSD_ArchiveNativeRelease(HSD_Archive* archive)
{
    (void) archive;
}
#endif

#ifdef MELEE_NATIVE
static u32 archive_be32(const u8* bytes)
{
    return ((u32) bytes[0] << 24) | ((u32) bytes[1] << 16) |
           ((u32) bytes[2] << 8) | bytes[3];
}
#endif

static inline void relocateInternalPointers(HSD_Archive* archive)
{
#ifdef MELEE_NATIVE
    (void) archive;
#else
    u32 reloc_index;
    u32* pointer_slot;

    for (reloc_index = 0; reloc_index < archive->header.nb_reloc;
         reloc_index++)
    {
        pointer_slot =
            (u32*) (archive->data + archive->reloc_info[reloc_index].offset);
        *pointer_slot += (u32) archive->data;
    }
#endif
}

s32 HSD_ArchiveParse(HSD_Archive* archive, u8* src, size_t file_size)
{
#ifdef MELEE_NATIVE
    size_t offset;
    if (archive == NULL || src == NULL ||
        file_size < sizeof(HSD_ArchiveHeader))
    {
        return -1;
    }
    if (archive_be32(src) != file_size) {
        return -1;
    }
    memset(archive, 0, sizeof(*archive));
    archive->header.file_size = archive_be32(src + 0);
    archive->header.data_size = archive_be32(src + 4);
    archive->header.nb_reloc = archive_be32(src + 8);
    archive->header.nb_public = archive_be32(src + 12);
    archive->header.nb_extern = archive_be32(src + 16);
    memcpy(archive->header.version, src + 20, sizeof(archive->header.version));
    archive->data = src + sizeof(HSD_ArchiveHeader);
    offset = sizeof(HSD_ArchiveHeader) + archive->header.data_size;
    if (offset > file_size ||
        archive->header.nb_reloc > (file_size - offset) / 4u)
    {
        return -1;
    }
    archive->reloc_info = (HSD_ArchiveRelocationInfo*) (src + offset);
    offset += (size_t) archive->header.nb_reloc * 4u;
    if (archive->header.nb_public > (file_size - offset) / 8u) {
        return -1;
    }
    archive->public_info = (HSD_ArchivePublicInfo*) (src + offset);
    offset += (size_t) archive->header.nb_public * 8u;
    if (archive->header.nb_extern > (file_size - offset) / 8u) {
        return -1;
    }
    archive->extern_info = (HSD_ArchiveExternInfo*) (src + offset);
    offset += (size_t) archive->header.nb_extern * 8u;
    archive->symbols = offset < file_size ? (char*) (src + offset) : NULL;
    archive->top_ptr = src;
    archive->flags |= HSD_ARCHIVE_DONT_FREE;
    return 0;
#else
    u32 file_offset;

    if (archive == NULL) {
        return -1;
    }

    memset(archive, 0, sizeof(HSD_Archive));
    archive->flags |= HSD_ARCHIVE_DONT_FREE;
    memcpy(&archive->header, src, sizeof(HSD_ArchiveHeader));

    if (archive->header.file_size != file_size) {
        OSReport("HSD_ArchiveParse: byte-order mismatch! Please check data "
                 "format %x %x\n",
                 archive->header.file_size, file_size);
        return -1;
    }

    file_offset = sizeof(HSD_ArchiveHeader);
    if (archive->header.data_size != 0) {
        archive->data = src + sizeof(HSD_ArchiveHeader);
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

    archive->top_ptr = src;
    relocateInternalPointers(archive);

    return 0;
#endif
}

void* HSD_ArchiveGetPublicAddress(HSD_Archive* archive,
                                  const char* symbol_name)
{
#ifdef MELEE_NATIVE
    return HSD_ArchiveNativePublicAddress(archive, symbol_name);
#else
    u32 public_index;

    if (archive == NULL || symbol_name == NULL || archive->symbols == NULL ||
        archive->public_info == NULL || archive->data == NULL)
    {
        return NULL;
    }

    for (public_index = 0; public_index < archive->header.nb_public;
         public_index++)
    {
        int comparison = strcmp(archive->symbols +
                                    archive->public_info[public_index].symbol,
                                symbol_name);

        if (comparison == 0) {
            return archive->data + archive->public_info[public_index].offset;
        }
    }

    return NULL;
#endif
}

char* HSD_ArchiveGetExtern(HSD_Archive* archive, int extern_index)
{
    if (archive == NULL || archive->symbols == NULL ||
        archive->extern_info == NULL || extern_index < 0 ||
        archive->header.nb_extern <= (unsigned) extern_index)
    {
        return NULL;
    }

#ifdef MELEE_NATIVE
    return archive->symbols + archive_be32((u8*) archive->extern_info +
                                           (size_t) extern_index * 8u + 4);
#else
    return archive->symbols + archive->extern_info[extern_index].symbol;
#endif
}

void HSD_ArchiveLocateExtern(HSD_Archive* archive, const char* symbol_name,
                             void* address)
{
#ifdef MELEE_NATIVE
    /* DAT relocation fields are four-byte big-endian offsets. A host pointer
     * does not fit in those fields, so the legacy in-place binding path is
     * unsafe on a 64-bit build. Native callers must bind through
     * NativeArchiveGraph instead. */
    (void) archive;
    (void) symbol_name;
    (void) address;
    OSReport("HSD_ArchiveLocateExtern is unavailable on native hosts; use "
             "NativeArchive.\n");
    return;
#else
    uintptr_t next_offset;
    uintptr_t reference_offset = -1;
    u32 extern_index;

    for (extern_index = 0; extern_index < archive->header.nb_extern;
         extern_index++)
    {
        int comparison =
            strcmp(symbol_name, archive->symbols +
                                    archive->extern_info[extern_index].symbol);

        if (comparison == 0) {
            reference_offset = archive->extern_info[extern_index].offset;
            break;
        }
    }

    if (reference_offset == -1U) {
        return;
    }

    // Each slot holds the next data offset until it is replaced by address.
    while (reference_offset != -1U &&
           reference_offset < archive->header.data_size)
    {
        next_offset = *(uintptr_t*) (archive->data + reference_offset);
        *(u32*) (archive->data + reference_offset) = (uintptr_t) address;
        reference_offset = next_offset;
    }
#endif
}
