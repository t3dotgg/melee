#include "archive.h"

#include <string.h>

#include <dolphin/os.h>

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
    (void) archive;
    (void) src;
    (void) file_size;
    OSReport("HSD_ArchiveParse is unavailable on native hosts; use NativeArchive.\n");
    return -1;
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
    u32 public_index;

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
}

char* HSD_ArchiveGetExtern(HSD_Archive* archive, int extern_index)
{
    if (extern_index < 0 ||
        archive->header.nb_extern <= (unsigned) extern_index)
    {
        return NULL;
    }

    return archive->symbols + archive->extern_info[extern_index].symbol;
}

void HSD_ArchiveLocateExtern(HSD_Archive* archive, const char* symbol_name,
                             void* address)
{
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
}
