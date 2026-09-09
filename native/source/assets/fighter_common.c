#include "fighter_common.h"

#include <stdlib.h>
#include <string.h>

#include "archive_internal.h"
#include <melee/ft/fighter.h>
#include <melee/sfx/crowdsfx.h>

/* Each cache entry names a serialized object and its explicit schema. */
typedef struct CommonObject {
    uint32_t offset;
    unsigned int schema;
    void* data;
    struct CommonObject* next;
} CommonObject;

struct NativeFighterCommonArchive {
    const NativeArchive* archive;
    NativeArchiveGraph* graph;
    CommonObject* objects;
    NativeArchiveError* error;
    void** root;
    NativeArchiveError failure;
};

enum {
    COMMON_BYTES,
    COMMON_WORDS,
    COMMON_ATTRIBUTES,
    COMMON_PART,
    COMMON_PART_LIST,
    COMMON_ACCESSORY,
    COMMON_ACCESSORY_LIST,
    COMMON_COLOR_ANIMATION,
    COMMON_SAMPLES,
    COMMON_CPU,
    COMMON_CPU_ENTRIES,
    COMMON_CPU_ENTRY_LIST,
    COMMON_CPU_SCRIPTS,
    COMMON_RESPAWN,
    COMMON_ROOT,
};

static void* fail(NativeFighterCommonArchive* common, uint32_t offset,
                  const char* message)
{
    NativeArchiveFail(common->error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                      message);
    return NULL;
}

static bool range(NativeFighterCommonArchive* common, uint32_t offset,
                  size_t size)
{
    if (NativeArchiveDataRange(common->archive, offset, size)) {
        return true;
    }
    NativeArchiveFail(common->error, NATIVE_ARCHIVE_BOUNDS, 32u + offset,
                      "fighter common record exceeds archive data");
    return false;
}

static void* cached(NativeFighterCommonArchive* common, uint32_t offset,
                    unsigned int schema)
{
    for (CommonObject* object = common->objects; object != NULL;
         object = object->next)
    {
        if (object->offset == offset && object->schema == schema) {
            return object->data;
        }
    }
    return NULL;
}

static void* allocate(NativeFighterCommonArchive* common, uint32_t offset,
                      unsigned int schema, size_t count, size_t size)
{
    CommonObject* object;
    void* data;
    if (size == 0 || count > SIZE_MAX / size) {
        return fail(common, offset, "fighter common array is too large");
    }
    data = calloc(count == 0 ? 1 : count, size);
    object = malloc(sizeof(*object));
    if (data == NULL || object == NULL) {
        free(data);
        free(object);
        NativeArchiveFail(common->error, NATIVE_ARCHIVE_NO_MEMORY, 0,
                          "cannot allocate fighter common record");
        return NULL;
    }
    *object = (CommonObject){ offset, schema, data, common->objects };
    common->objects = object;
    return data;
}

static bool reference(NativeFighterCommonArchive* common, uint32_t field,
                      uint32_t* target, bool* present)
{
    return NativeArchiveReference(common->archive, field, target, present,
                                  common->error) == NATIVE_ARCHIVE_OK;
}

/* Callers select a known record width. Relocation targets and public symbols
 * bound a serialized allocation, but never select its type or pointer fields.
 */
static size_t block_size(NativeFighterCommonArchive* common, uint32_t offset)
{
    uint32_t end = common->archive->data_size;
    if (!range(common, offset, 1)) {
        return 0;
    }
    for (size_t i = 0; i < common->archive->reloc_count; ++i) {
        uint32_t target = NativeArchiveBE32(common->archive->data +
                                            common->archive->relocations[i]);
        if (target > offset && target < end) {
            end = target;
        }
    }
    for (size_t i = 0; i < common->archive->public_count; ++i) {
        NativeArchiveSymbol symbol;
        if (NativeArchivePublic(common->archive, i, &symbol, common->error) !=
            NATIVE_ARCHIVE_OK)
        {
            return 0;
        }
        if (symbol.offset > offset && symbol.offset < end) {
            end = symbol.offset;
        }
    }
    return end - offset;
}

static size_t block_count(NativeFighterCommonArchive* common, uint32_t offset,
                          size_t width)
{
    size_t size = block_size(common, offset);
    if (size % width != 0) {
        fail(common, offset, "fighter common array has a partial record");
        return 0;
    }
    return size / width;
}

static bool scalar_range(NativeFighterCommonArchive* common, uint32_t offset,
                         size_t size)
{
    if (!range(common, offset, size)) {
        return false;
    }
    for (size_t i = 0; i < common->archive->reloc_count; ++i) {
        uint32_t field = common->archive->relocations[i];
        if (field >= offset && field - offset < size) {
            fail(common, field, "reference in fighter common scalar data");
            return false;
        }
    }
    for (size_t i = 0; i < size; i += 4) {
        uint32_t field = offset + i;
        if (common->archive->external_fields != NULL &&
            (common->archive->external_fields[field / 32] &
             (1u << ((field / 4) % 8))) != 0)
        {
            fail(common, field,
                 "external reference in fighter common scalar data");
            return false;
        }
    }
    return true;
}

static void* scalar_block(NativeFighterCommonArchive* common, uint32_t offset,
                          bool words)
{
    unsigned int schema = words ? COMMON_WORDS : COMMON_BYTES;
    void* result = cached(common, offset, schema);
    size_t size = block_size(common, offset);
    if (result != NULL) {
        return result;
    }
    if (size == 0 || (words && size % 4 != 0) ||
        !scalar_range(common, offset, size))
    {
        return fail(common, offset, "invalid fighter common scalar block");
    }
    result = allocate(common, offset, schema, size, 1);
    if (result != NULL) {
        if (words) {
            for (size_t i = 0; i < size; i += 4) {
                uint32_t value =
                    NativeArchiveBE32(common->archive->data + offset + i);
                memcpy((u8*) result + i, &value, 4);
            }
        } else {
            memcpy(result, common->archive->data + offset, size);
        }
    }
    return result;
}

static bool word_span(NativeFighterCommonArchive* common, uint32_t offset,
                      size_t size, void* output)
{
    if (!range(common, offset, size)) {
        return false;
    }
    for (size_t i = 0; i < size; i += 4) {
        u32 value = NativeArchiveBE32(common->archive->data + offset + i);
        memcpy((u8*) output + i, &value, 4);
    }
    return true;
}

static ftCommonData* attributes(NativeFighterCommonArchive* common,
                                uint32_t offset)
{
    ftCommonData* result = cached(common, offset, COMMON_ATTRIBUTES);
    if (result != NULL) {
        return result;
    }
    if (!scalar_range(common, offset, 0x818)) {
        return NULL;
    }
    result = allocate(common, offset, COMMON_ATTRIBUTES, 1, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    _Static_assert(sizeof(ftCommonData) == 0x818,
                   "fighter common attributes must keep their scalar layout");
    word_span(common, offset, 0x818, result);
    memcpy(result->x6DC_colorsByPlayer, common->archive->data + offset + 0x6DC,
           0x14);
    memcpy(&result->x7D8, common->archive->data + offset + 0x7D8, 4);
    return result;
}

typedef void* (*CommonReader)(NativeFighterCommonArchive*, uint32_t);

static void** pointer_list(NativeFighterCommonArchive* common, uint32_t offset,
                           unsigned int schema, CommonReader reader)
{
    void** result = cached(common, offset, schema);
    if (result != NULL) {
        return result;
    }
    size_t count = block_count(common, offset, 4);
    if (count == 0) {
        return NULL;
    }
    result = allocate(common, offset, schema, count, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        uint32_t target;
        bool present;
        if (!reference(common, offset + i * 4, &target, &present)) {
            return NULL;
        }
        if (present && (result[i] = reader(common, target)) == NULL) {
            return NULL;
        }
    }
    return result;
}

static void* part(NativeFighterCommonArchive* common, uint32_t offset)
{
    FighterPartsTable* result = cached(common, offset, COMMON_PART);
    uint32_t target;
    bool present;
    if (result != NULL) {
        return result;
    }
    if (!range(common, offset, 12)) {
        return NULL;
    }
    result = allocate(common, offset, COMMON_PART, 1, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    result->parts_num = NativeArchiveBE32(common->archive->data + offset + 8);
    for (size_t i = 0; i < 2; ++i) {
        if (!reference(common, offset + i * 4, &target, &present)) {
            return NULL;
        }
        if (present) {
            u8* bytes = scalar_block(common, target, false);
            if (bytes == NULL ||
                (i == 0 && block_size(common, target) < result->parts_num))
            {
                return fail(common, offset, "fighter part array is too short");
            }
            if (i == 0) {
                result->joint_to_part = bytes;
            } else {
                result->part_to_joint = bytes;
            }
        } else if (result->parts_num != 0) {
            return fail(common, offset, "fighter part array is missing");
        }
    }
    return result;
}

static void* accessory(NativeFighterCommonArchive* common, uint32_t offset)
{
    struct Fighter_804D6540_t* result =
        cached(common, offset, COMMON_ACCESSORY);
    uint32_t target;
    bool present;
    if (result != NULL) {
        return result;
    }
    if (!range(common, offset, 8)) {
        return NULL;
    }
    result = allocate(common, offset, COMMON_ACCESSORY, 1, sizeof(*result));
    if (result == NULL || !reference(common, offset, &target, &present)) {
        return NULL;
    }
    result->x4 = NativeArchiveBE32(common->archive->data + offset + 4);
    if (result->x4 < 0 || (result->x4 != 0 && !present)) {
        return fail(common, offset, "invalid fighter accessory count");
    }
    if (present) {
        if ((size_t) result->x4 > block_size(common, target) / 4) {
            return fail(common, offset,
                        "fighter accessory array is too short");
        }
        result->x0 = scalar_block(common, target, false);
        if (result->x0 == NULL) {
            return NULL;
        }
    }
    return result;
}

static void* color_animations(NativeFighterCommonArchive* common,
                              uint32_t offset)
{
    struct Fighter_804D653C_t* result =
        cached(common, offset, COMMON_COLOR_ANIMATION);
    size_t count = block_count(common, offset, 8);
    if (result != NULL) {
        return result;
    }
    if (count == 0) {
        return NULL;
    }
    result = allocate(common, offset, COMMON_COLOR_ANIMATION, count,
                      sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        uint32_t field = offset + i * 8, target;
        bool present;
        if (!reference(common, field, &target, &present)) {
            return NULL;
        }
        /* Keep ColorOverlay commands in the archive for the native stream
         * decoder and its reference resolver. */
        result[i].unk =
            present ? (void*) (common->archive->data + target) : NULL;
        result[i].unk4 = common->archive->data[field + 4];
        result[i].unk5 = common->archive->data[field + 5];
    }
    return result;
}

static void* samples(NativeFighterCommonArchive* common, uint32_t offset)
{
    /* DamageFallSamples and Fighter_ShakeTable have the same pointer/count
     * layout. Both contain arrays of Vec2 values. */
    struct Fighter_DamageFallSamples* result =
        cached(common, offset, COMMON_SAMPLES);
    size_t count = block_count(common, offset, 8);
    if (result != NULL) {
        return result;
    }
    if (count == 0) {
        return NULL;
    }
    result = allocate(common, offset, COMMON_SAMPLES, count, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        uint32_t field = offset + i * 8, target;
        bool present;
        if (!reference(common, field, &target, &present)) {
            return NULL;
        }
        result[i].count = NativeArchiveBE32(common->archive->data + field + 4);
        if (!present && result[i].count != 0) {
            return fail(common, field, "fighter sample array is missing");
        }
        if (present) {
            if (result[i].count > block_size(common, target) / sizeof(Vec2)) {
                return fail(common, field,
                            "fighter sample array is too short");
            }
            result[i].samples = scalar_block(common, target, true);
            if (result[i].samples == NULL) {
                return NULL;
            }
        }
    }
    return result;
}

static void* script(NativeFighterCommonArchive* common, uint32_t offset)
{
    return range(common, offset, 1) ? (void*) (common->archive->data + offset)
                                    : NULL;
}

static void* attack_entries(NativeFighterCommonArchive* common,
                            uint32_t offset)
{
    void* result = cached(common, offset, COMMON_CPU_ENTRIES);
    if (result != NULL) {
        return result;
    }
    size_t size = block_size(common, offset), used = 0;
    bool terminated = false;
    /* ftCo_AttackEntry is nine scalar words. cmd == 0 terminates each list.
     * The sentinel stores only its cmd word, not another complete record. */
    while (used + 4 <= size) {
        if (NativeArchiveBE32(common->archive->data + offset + used) == 0) {
            used += 4;
            terminated = true;
            break;
        }
        if (size - used < 0x24) {
            break;
        }
        used += 0x24;
    }
    if (!terminated) {
        return fail(common, offset, "unterminated CPU attack entry list");
    }
    result = allocate(common, offset, COMMON_CPU_ENTRIES, used, 1);
    if (result != NULL) {
        word_span(common, offset, used, result);
    }
    return result;
}

static void* cpu(NativeFighterCommonArchive* common, uint32_t offset)
{
    struct Fighter_804D64FC_t* result = cached(common, offset, COMMON_CPU);
    if (result != NULL) {
        return result;
    }
    if (!range(common, offset, 0x28)) {
        return NULL;
    }
    result = allocate(common, offset, COMMON_CPU, 1, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < 10; ++i) {
        uint32_t target;
        bool present;
        void* converted;
        if (!reference(common, offset + i * 4, &target, &present)) {
            return NULL;
        }
        if (!present) {
            continue;
        }
        if (i == 0) {
            converted =
                pointer_list(common, target, COMMON_CPU_SCRIPTS, script);
        } else if (i < 8) {
            converted = pointer_list(common, target, COMMON_CPU_ENTRY_LIST,
                                     attack_entries);
        } else {
            converted = scalar_block(common, target, true);
        }
        if (converted == NULL) {
            return NULL;
        }
        /* Fighter_804D64FC_t consists of ten host pointer fields. */
        memcpy((u8*) result + i * sizeof(void*), &converted,
               sizeof(converted));
    }
    return result;
}

static void* respawn(NativeFighterCommonArchive* common, uint32_t offset)
{
    void** result = cached(common, offset, COMMON_RESPAWN);
    if (result != NULL) {
        return result;
    }
    if (!range(common, offset, 8)) {
        return NULL;
    }
    result = allocate(common, offset, COMMON_RESPAWN, 2, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < 2; ++i) {
        uint32_t target;
        bool present;
        if (!reference(common, offset + i * 4, &target, &present)) {
            return NULL;
        }
        if (present) {
            NativeArchiveStatus status;
            if (i == 0) {
                struct HSD_Joint* joint;
                status = NativeArchiveJoint(common->graph, target, &joint,
                                            common->error);
                result[i] = joint;
            } else {
                struct HSD_AnimJoint* animation;
                status = NativeArchiveAnimation(common->graph, target,
                                                &animation, common->error);
                result[i] = animation;
            }
            if (status != NATIVE_ARCHIVE_OK) {
                return NULL;
            }
        }
    }
    return result;
}

NativeFighterCommonArchive*
NativeFighterCommonArchiveOpen(const NativeArchive* archive,
                               NativeArchiveGraph* graph)
{
    if (archive == NULL || graph == NULL) {
        return NULL;
    }
    NativeFighterCommonArchive* common = calloc(1, sizeof(*common));
    if (common != NULL) {
        common->archive = archive;
        common->graph = graph;
    }
    return common;
}

void NativeFighterCommonArchiveClose(NativeFighterCommonArchive* common)
{
    if (common == NULL) {
        return;
    }
    CommonObject* object = common->objects;
    while (object != NULL) {
        CommonObject* next = object->next;
        free(object->data);
        free(object);
        object = next;
    }
    free(common);
}

NativeArchiveStatus
NativeFighterCommonArchiveRead(NativeFighterCommonArchive* common,
                               const char* symbol, uint32_t offset,
                               void** output, NativeArchiveError* error)
{
    NativeArchiveError local = { NATIVE_ARCHIVE_OK, 0, "ok" };
    if (common == NULL || symbol == NULL || output == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "invalid fighter common reader argument");
    }
    common->error = error == NULL ? &local : error;
    *common->error = local;
    *output = NULL;
    if (strcmp(symbol, "ftLoadCommonData") != 0) {
        return NATIVE_ARCHIVE_NOT_FOUND;
    }
    if (common->failure.status != NATIVE_ARCHIVE_OK) {
        *common->error = common->failure;
        return common->error->status;
    }
    if (common->root != NULL) {
        *output = common->root;
        return NATIVE_ARCHIVE_OK;
    }
    if (!range(common, offset, 23 * 4)) {
        common->failure = *common->error;
        return common->error->status;
    }
    void** result = allocate(common, offset, COMMON_ROOT, 23, sizeof(*result));
    if (result == NULL) {
        common->failure = *common->error;
        return common->error->status;
    }
    for (size_t i = 0; i < 23; ++i) {
        uint32_t target;
        bool present;
        if (!reference(common, offset + i * 4, &target, &present)) {
            common->failure = *common->error;
            return common->error->status;
        }
        if (!present) {
            continue;
        }
        switch (i) {
        case 0:
            result[i] = attributes(common, target);
            break;
        case 4:
            result[i] = pointer_list(common, target, COMMON_PART_LIST, part);
            break;
        case 5:
            result[i] =
                pointer_list(common, target, COMMON_ACCESSORY_LIST, accessory);
            break;
        case 6:
        case 7:
            result[i] = color_animations(common, target);
            break;
        case 8:
            result[i] = respawn(common, target);
            break;
        case 9:
        case 10:
        case 11:
            result[i] = samples(common, target);
            break;
        case 16:
        case 20: {
            struct HSD_Joint* joint = NULL;
            if (NativeArchiveJoint(common->graph, target, &joint,
                                   common->error) == NATIVE_ARCHIVE_OK)
            {
                result[i] = joint;
            }
            break;
        }
        case 17:
        case 18:
        case 19:
            result[i] = scalar_block(common, target, false);
            break;
        case 22:
            result[i] = cpu(common, target);
            break;
        default:
            /* Throw attributes, damage multipliers, scaling and crowd
             * configuration contain only explicit 32-bit scalar fields. */
            result[i] = scalar_block(common, target, true);
            break;
        }
        if (result[i] == NULL) {
            if (common->error->status == NATIVE_ARCHIVE_OK) {
                fail(common, target, "cannot convert fighter common data");
            }
            common->failure = *common->error;
            return common->error->status;
        }
    }
    common->root = result;
    *output = result;
    return NATIVE_ARCHIVE_OK;
}
