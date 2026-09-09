#include "archive_internal.h"

#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 32u

NativeArchiveStatus NativeArchiveFail(NativeArchiveError* error,
                                     NativeArchiveStatus status,
                                     size_t offset, const char* message)
{
    if (error != NULL) {
        error->status = status;
        error->offset = offset;
        error->message = message;
    }
    return status;
}

static NativeArchiveStatus success(NativeArchiveError* error)
{
    return NativeArchiveFail(error, NATIVE_ARCHIVE_OK, 0, "ok");
}

static bool consume(size_t* cursor, size_t count, size_t width, size_t end)
{
    if (*cursor > end || count > (end - *cursor) / width) {
        return false;
    }
    *cursor += count * width;
    return true;
}

static bool data_range(const NativeArchive* archive, uint32_t offset,
                       size_t size)
{
    return offset <= archive->data_size &&
           size <= (size_t) archive->data_size - offset;
}

static bool pointer_field(const NativeArchive* archive, uint32_t offset)
{
    return (offset & 3u) == 0 && data_range(archive, offset, 4);
}

static int compare_offsets(const void* lhs, const void* rhs)
{
    uint32_t a = *(const uint32_t*) lhs;
    uint32_t b = *(const uint32_t*) rhs;
    return (a > b) - (a < b);
}

static bool is_relocated(const NativeArchive* archive, uint32_t offset)
{
    return archive->reloc_count != 0 &&
           bsearch(&offset, archive->relocations, archive->reloc_count,
                   sizeof(*archive->relocations), compare_offsets) != NULL;
}

static bool is_external(const NativeArchive* archive, uint32_t offset)
{
    size_t word = offset / 4u;
    return archive->external_fields != NULL &&
           (archive->external_fields[word / 8u] & (1u << (word % 8u))) != 0;
}

static bool symbol_valid(const NativeArchive* archive, uint32_t offset)
{
    size_t length = archive->size - archive->symbols_at;
    return offset < length &&
           memchr(archive->file + archive->symbols_at + offset, 0,
                  length - offset) != NULL;
}

void NativeArchiveClose(NativeArchive* archive)
{
    if (archive != NULL) {
        free(archive->external_fields);
        free(archive->relocations);
        free(archive->file);
        free(archive);
    }
}

NativeArchiveStatus NativeArchiveOpen(const void* input, size_t size,
                                     NativeArchive** output,
                                     NativeArchiveError* error)
{
    const uint8_t* bytes = input;
    NativeArchive* archive;
    size_t cursor;
    size_t reloc_at;
    uint32_t i;
    NativeArchiveStatus status = NATIVE_ARCHIVE_OK;

    if (output == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "archive output is null");
    }
    *output = NULL;
    if (input == NULL || size < HEADER_SIZE) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 0,
                                 "archive header is truncated");
    }
    if (size != NativeArchiveBE32(bytes)) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "archive size does not match its header");
    }
    archive = calloc(1, sizeof(*archive));
    if (archive == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_NO_MEMORY, 0,
                                 "cannot allocate archive");
    }
    archive->size = size;
    archive->data_size = NativeArchiveBE32(bytes + 4);
    archive->reloc_count = NativeArchiveBE32(bytes + 8);
    archive->public_count = NativeArchiveBE32(bytes + 12);
    archive->external_count = NativeArchiveBE32(bytes + 16);
    cursor = HEADER_SIZE;
    if (!consume(&cursor, archive->data_size, 1, size)) {
        status = NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 4,
                                   "archive data is truncated");
        goto fail;
    }
    reloc_at = cursor;
    if (!consume(&cursor, archive->reloc_count, 4, size)) {
        status = NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 8,
                                   "archive relocation table is truncated");
        goto fail;
    }
    archive->public_at = cursor;
    if (!consume(&cursor, archive->public_count, 8, size)) {
        status = NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 12,
                                   "archive public table is truncated");
        goto fail;
    }
    archive->external_at = cursor;
    if (!consume(&cursor, archive->external_count, 8, size)) {
        status = NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 16,
                                   "archive external table is truncated");
        goto fail;
    }
    archive->symbols_at = cursor;
    archive->file = malloc(size);
    if (archive->file == NULL) {
        status = NativeArchiveFail(error, NATIVE_ARCHIVE_NO_MEMORY, 0,
                                   "cannot copy archive file");
        goto fail;
    }
    memcpy(archive->file, bytes, size);
    archive->data = archive->file + HEADER_SIZE;

    if (archive->reloc_count != 0) {
        archive->relocations =
            malloc((size_t) archive->reloc_count * sizeof(uint32_t));
        if (archive->relocations == NULL) {
            status = NativeArchiveFail(error, NATIVE_ARCHIVE_NO_MEMORY, 8,
                                       "cannot allocate relocation index");
            goto fail;
        }
    }
    for (i = 0; i < archive->reloc_count; ++i) {
        size_t at = reloc_at + (size_t) i * 4;
        uint32_t field = NativeArchiveBE32(archive->file + at);
        if (!pointer_field(archive, field)) {
            status = NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, at,
                                       "invalid relocation field offset");
            goto fail;
        }
        if (NativeArchiveBE32(archive->data + field) > archive->data_size) {
            status = NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS,
                                       HEADER_SIZE + (size_t) field,
                                       "relocation target is outside data");
            goto fail;
        }
        archive->relocations[i] = field;
    }
    if (archive->reloc_count != 0) {
        qsort(archive->relocations, archive->reloc_count, sizeof(uint32_t),
              compare_offsets);
    }
    for (i = 1; i < archive->reloc_count; ++i) {
        if (archive->relocations[i - 1] == archive->relocations[i]) {
            status = NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID,
                                       HEADER_SIZE +
                                           (size_t) archive->relocations[i],
                                       "duplicate relocation field");
            goto fail;
        }
    }
    for (i = 0; i < archive->public_count; ++i) {
        size_t at = archive->public_at + (size_t) i * 8;
        if (NativeArchiveBE32(archive->file + at) > archive->data_size) {
            status = NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, at,
                                       "public symbol is outside data");
            goto fail;
        }
        if (!symbol_valid(archive, NativeArchiveBE32(archive->file + at + 4))) {
            status = NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, at + 4,
                                       "public symbol name is invalid");
            goto fail;
        }
    }
    if (archive->external_count != 0 && archive->data_size != 0) {
        size_t words = archive->data_size / 4u;
        archive->external_fields = calloc((words + 7u) / 8u, 1);
        if (words != 0 && archive->external_fields == NULL) {
            status = NativeArchiveFail(error, NATIVE_ARCHIVE_NO_MEMORY, 16,
                                       "cannot allocate external field index");
            goto fail;
        }
    }
    for (i = 0; i < archive->external_count; ++i) {
        size_t at = archive->external_at + (size_t) i * 8;
        uint32_t field = NativeArchiveBE32(archive->file + at);
        if (!symbol_valid(archive, NativeArchiveBE32(archive->file + at + 4))) {
            status = NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, at + 4,
                                       "external symbol name is invalid");
            goto fail;
        }
        while (field != UINT32_MAX) {
            size_t word;
            if (!pointer_field(archive, field)) {
                status = NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, at,
                                           "invalid external chain offset");
                goto fail;
            }
            if (is_relocated(archive, field) || is_external(archive, field)) {
                status = NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID,
                                           HEADER_SIZE + (size_t) field,
                                           "external chain overlaps or cycles");
                goto fail;
            }
            word = field / 4u;
            archive->external_fields[word / 8u] |= 1u << (word % 8u);
            at = HEADER_SIZE + (size_t) field;
            field = NativeArchiveBE32(archive->data + field);
        }
    }
    *output = archive;
    return success(error);

fail:
    NativeArchiveClose(archive);
    return status;
}

size_t NativeArchiveDataSize(const NativeArchive* archive)
{
    return archive == NULL ? 0 : archive->data_size;
}

size_t NativeArchivePublicCount(const NativeArchive* archive)
{
    return archive == NULL ? 0 : archive->public_count;
}

size_t NativeArchiveExternalCount(const NativeArchive* archive)
{
    return archive == NULL ? 0 : archive->external_count;
}

static NativeArchiveStatus get_symbol(const NativeArchive* archive,
                                      size_t index, bool external,
                                      NativeArchiveSymbol* output,
                                      NativeArchiveError* error)
{
    size_t at;
    if (archive == NULL || output == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "archive or symbol output is null");
    }
    if (index >= (external ? archive->external_count : archive->public_count)) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_NOT_FOUND, 0,
                                 "symbol index is outside table");
    }
    at = (external ? archive->external_at : archive->public_at) + index * 8;
    output->offset = NativeArchiveBE32(archive->file + at);
    output->name = (const char*) archive->file + archive->symbols_at +
                   NativeArchiveBE32(archive->file + at + 4);
    return success(error);
}

NativeArchiveStatus NativeArchivePublic(const NativeArchive* archive,
                                       size_t index,
                                       NativeArchiveSymbol* output,
                                       NativeArchiveError* error)
{
    return get_symbol(archive, index, false, output, error);
}

NativeArchiveStatus NativeArchiveExternal(const NativeArchive* archive,
                                         size_t index,
                                         NativeArchiveSymbol* output,
                                         NativeArchiveError* error)
{
    return get_symbol(archive, index, true, output, error);
}

NativeArchiveStatus NativeArchiveFind(const NativeArchive* archive,
                                     const char* name, uint32_t* offset,
                                     NativeArchiveError* error)
{
    size_t i;
    if (archive == NULL || name == NULL || offset == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "archive, name or offset output is null");
    }
    for (i = 0; i < archive->public_count; ++i) {
        NativeArchiveSymbol symbol;
        get_symbol(archive, i, false, &symbol, NULL);
        if (strcmp(symbol.name, name) == 0) {
            *offset = symbol.offset;
            return success(error);
        }
    }
    return NativeArchiveFail(error, NATIVE_ARCHIVE_NOT_FOUND, 0,
                             "public symbol was not found");
}

NativeArchiveStatus NativeArchiveReference(const NativeArchive* archive,
                                          uint32_t field_offset,
                                          uint32_t* target, bool* present,
                                          NativeArchiveError* error)
{
    uint32_t value;
    if (archive == NULL || target == NULL || present == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "archive or reference output is null");
    }
    *target = 0;
    *present = false;
    if (!pointer_field(archive, field_offset)) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS,
                                 HEADER_SIZE + (size_t) field_offset,
                                 "reference field is outside data or unaligned");
    }
    if (is_external(archive, field_offset)) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_UNSUPPORTED,
                                 HEADER_SIZE + (size_t) field_offset,
                                 "external reference needs typed binding");
    }
    value = NativeArchiveBE32(archive->data + field_offset);
    if (is_relocated(archive, field_offset)) {
        *target = value;
        *present = true;
    } else if (value != 0) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID,
                                 HEADER_SIZE + (size_t) field_offset,
                                 "non-null reference has no relocation");
    }
    return success(error);
}

NativeArchiveStatus NativeArchiveRead(const NativeArchive* archive,
                                     uint32_t offset, void* output, size_t size,
                                     NativeArchiveError* error)
{
    if (archive == NULL || (output == NULL && size != 0)) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "archive or data output is null");
    }
    if (!data_range(archive, offset, size)) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS,
                                 HEADER_SIZE + (size_t) offset,
                                 "read is outside archive data");
    }
    if (size != 0) {
        memcpy(output, archive->data + offset, size);
    }
    return success(error);
}
