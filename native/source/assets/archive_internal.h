#ifndef MELEE_NATIVE_ASSETS_ARCHIVE_INTERNAL_H
#define MELEE_NATIVE_ASSETS_ARCHIVE_INTERNAL_H

#include "archive.h"

struct NativeArchive {
    uint8_t* file;
    const uint8_t* data;
    size_t size;
    uint32_t data_size;
    uint32_t reloc_count;
    uint32_t public_count;
    uint32_t external_count;
    size_t public_at;
    size_t external_at;
    size_t symbols_at;
    uint32_t* relocations;
    uint8_t* external_fields;
    bool null_externals;
};

static inline uint32_t NativeArchiveBE32(const uint8_t* bytes)
{
    return ((uint32_t) bytes[0] << 24) | ((uint32_t) bytes[1] << 16) |
           ((uint32_t) bytes[2] << 8) | bytes[3];
}

static inline bool NativeArchiveDataRange(const NativeArchive* archive,
                                          uint32_t offset, size_t size)
{
    return archive != NULL && offset <= archive->data_size &&
           size <= (size_t) archive->data_size - offset;
}

NativeArchiveStatus NativeArchiveFail(NativeArchiveError* error,
                                      NativeArchiveStatus status,
                                      size_t offset, const char* message);

#endif
