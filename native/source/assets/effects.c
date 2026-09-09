#include "effects.h"

#include <stdlib.h>
#include <string.h>

#include "archive_internal.h"
#include <melee/ef/types.h>

typedef struct EffectRoot {
    uint32_t offset;
    void* table;
    struct EffectRoot* next;
} EffectRoot;

/* efAsync_LoadSync uses the address of EF_DAT_Entry.data as the first
 * EF_EffectDesc. The field is an inline array, not a serialized pointer. */
typedef struct EffectTable {
    char* particles;
    char* textures;
    EF_EffectDesc entries[];
} EffectTable;

_Static_assert(offsetof(EffectTable, entries) == offsetof(EF_DAT_Entry, data),
               "effect table prefix must match its runtime caller");

struct NativeEffectArchive {
    const NativeArchive* archive;
    NativeArchiveGraph* graph;
    EffectRoot* roots;
};

static bool is_effect_table(const char* symbol)
{
    const char suffix[] = "DataTable";
    size_t length = strlen(symbol);
    return length >= 3 + sizeof(suffix) - 1 &&
           strncmp(symbol, "eff", 3) == 0 &&
           strcmp(symbol + length - (sizeof(suffix) - 1), suffix) == 0;
}

/* DAT has no array length. Each referenced object starts at a relocation
 * target or a public symbol. This is also how descriptor buffers are sized. */
static size_t table_extent(const NativeArchive* archive, uint32_t offset)
{
    uint32_t end = archive->data_size;
    for (size_t i = 0; i < archive->reloc_count; ++i) {
        uint32_t target =
            NativeArchiveBE32(archive->data + archive->relocations[i]);
        if (target > offset && target < end) {
            end = target;
        }
    }
    for (size_t i = 0; i < archive->public_count; ++i) {
        uint32_t target =
            NativeArchiveBE32(archive->file + archive->public_at + i * 8);
        if (target > offset && target < end) {
            end = target;
        }
    }
    return end - offset;
}

static NativeArchiveStatus particle_bank(const NativeArchive* archive,
                                         uint32_t field, char** output,
                                         NativeArchiveError* error)
{
    uint32_t target;
    bool present;
    NativeArchiveStatus status =
        NativeArchiveReference(archive, field, &target, &present, error);
    if (status != NATIVE_ARCHIVE_OK || !present) {
        return status;
    }
    if (!NativeArchiveDataRange(archive, target, 12)) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 32u + field,
                                 "effect particle bank header is truncated");
    }
    /* particle.c decodes bank-relative offsets and big-endian scalars. */
    *output = (char*) archive->data + target;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus convert_table(NativeEffectArchive* effects,
                                         uint32_t offset, EffectTable** output,
                                         NativeArchiveError* error)
{
    const NativeArchive* archive = effects->archive;
    size_t extent;
    size_t count;
    EffectTable* table;
    NativeArchiveStatus status;
    if (!NativeArchiveDataRange(archive, offset, 8)) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 32u + offset,
                                 "effect table header is truncated");
    }
    extent = table_extent(archive, offset);
    if (extent < 8) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                                 "effect table overlaps another object");
    }
    count = (extent - 8) / 20;
    for (size_t i = 8 + count * 20; i < extent; ++i) {
        if (archive->data[offset + i] != 0) {
            return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID,
                                     32u + offset + i,
                                     "effect table ends in a partial record");
        }
    }
    if (count > (SIZE_MAX - sizeof(*table)) / sizeof(*table->entries)) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 32u + offset,
                                 "effect table is too large");
    }
    table = calloc(1, sizeof(*table) + count * sizeof(*table->entries));
    if (table == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_NO_MEMORY, 32u + offset,
                                 "cannot allocate effect table");
    }
    status = particle_bank(archive, offset, &table->particles, error);
    if (status == NATIVE_ARCHIVE_OK) {
        status = particle_bank(archive, offset + 4, &table->textures, error);
    }
    for (size_t i = 0; status == NATIVE_ARCHIVE_OK && i < count; ++i) {
        uint32_t at = offset + 8 + i * 20;
        uint32_t bits = NativeArchiveBE32(archive->data + at);
        EF_EffectDesc* entry = &table->entries[i];
        memcpy(&entry->lifetime, &bits, sizeof(bits));
        for (size_t j = 0; j < 4; ++j) {
            uint32_t target;
            bool present;
            status = NativeArchiveReference(archive, at + 4 + j * 4, &target,
                                            &present, error);
            if (status != NATIVE_ARCHIVE_OK) {
                break;
            }
            if (!present) {
                continue;
            }
            switch (j) {
            case 0:
                status = NativeArchiveJoint(effects->graph, target,
                                            &entry->model_desc.joint, error);
                break;
            case 1:
                status = NativeArchiveAnimation(effects->graph, target,
                                                &entry->model_desc.animjoint,
                                                error);
                break;
            case 2:
                status = NativeArchiveMatAnimJoint(
                    effects->graph, target, &entry->model_desc.matanim_joint,
                    error);
                break;
            case 3:
                status = NativeArchiveShapeAnimJoint(
                    effects->graph, target, &entry->model_desc.shapeanim_joint,
                    error);
                break;
            }
            if (status != NATIVE_ARCHIVE_OK) {
                break;
            }
        }
    }
    if (status != NATIVE_ARCHIVE_OK) {
        free(table);
        return status;
    }
    *output = table;
    return NATIVE_ARCHIVE_OK;
}

NativeEffectArchive* NativeEffectArchiveOpen(const NativeArchive* archive,
                                             NativeArchiveGraph* graph)
{
    NativeEffectArchive* effects;
    if (archive == NULL || graph == NULL) {
        return NULL;
    }
    effects = calloc(1, sizeof(*effects));
    if (effects != NULL) {
        effects->archive = archive;
        effects->graph = graph;
    }
    return effects;
}

void NativeEffectArchiveClose(NativeEffectArchive* effects)
{
    if (effects == NULL) {
        return;
    }
    while (effects->roots != NULL) {
        EffectRoot* root = effects->roots;
        effects->roots = root->next;
        free(root->table);
        free(root);
    }
    free(effects);
}

NativeArchiveStatus NativeEffectArchiveRead(NativeEffectArchive* effects,
                                            const char* symbol,
                                            uint32_t offset, void** output,
                                            NativeArchiveError* error)
{
    EffectRoot* root;
    EffectTable* table = NULL;
    NativeArchiveStatus status;
    if (output != NULL) {
        *output = NULL;
    }
    if (effects == NULL || symbol == NULL || output == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                                 "invalid effect archive request");
    }
    if (!is_effect_table(symbol)) {
        return NATIVE_ARCHIVE_NOT_FOUND;
    }
    for (root = effects->roots; root != NULL; root = root->next) {
        if (root->offset == offset) {
            *output = root->table;
            return NATIVE_ARCHIVE_OK;
        }
    }
    status = convert_table(effects, offset, &table, error);
    if (status != NATIVE_ARCHIVE_OK) {
        return status;
    }
    root = malloc(sizeof(*root));
    if (root == NULL) {
        free(table);
        return NativeArchiveFail(error, NATIVE_ARCHIVE_NO_MEMORY, 32u + offset,
                                 "cannot cache effect table");
    }
    root->offset = offset;
    root->table = table;
    root->next = effects->roots;
    effects->roots = root;
    *output = table;
    return NATIVE_ARCHIVE_OK;
}
