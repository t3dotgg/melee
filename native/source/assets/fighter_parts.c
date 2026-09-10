#include "fighter_parts.h"

#include <stdlib.h>
#include <string.h>

#include "archive_internal.h"
#include <melee/ft/types.h>

/* ftparts.c consumes three levels of counted visibility records. ftanim.c
 * consumes costume remaps and part animation lists. ftdynamics.c and
 * lb_80011710 consume bone descriptors and packed 0x3C float records. */
typedef enum PartsRecordType {
    PARTS_MODELS,
    PARTS_DESCRIPTION,
    PARTS_ANIMATION_LIST,
    PARTS_SHIELD,
    PARTS_DYNAMICS,
    PARTS_VIS_TABLE,
    PARTS_VIS_LOOKUP,
    PARTS_VIS_INDICES,
    PARTS_REMAP_LIST,
    PARTS_ANIMATION,
    PARTS_ANIMATION_JOINTS,
    PARTS_BONES,
    PARTS_LIMIT_LIST,
    PARTS_COLLISIONS,
    PARTS_BYTES,
    PARTS_SHORTS,
    PARTS_WORDS,
} PartsRecordType;

typedef struct PartsAllocation {
    struct PartsAllocation* next;
    uint32_t offset;
    PartsRecordType type;
    size_t count;
    void* data;
} PartsAllocation;

struct NativeFighterParts {
    const NativeArchive* archive;
    NativeArchiveGraph* graph;
    PartsAllocation* allocations;
    uint32_t* bounds;
    size_t bounds_count;
    NativeArchiveError* error;
    bool failed;
};

static void* fail(NativeFighterParts* parts, NativeArchiveStatus status,
                  uint32_t offset, const char* message)
{
    NativeArchiveFail(parts->error, status, 32u + offset, message);
    return NULL;
}

static bool range(NativeFighterParts* parts, uint32_t offset, size_t size)
{
    if (NativeArchiveDataRange(parts->archive, offset, size)) {
        return true;
    }
    fail(parts, NATIVE_ARCHIVE_BOUNDS, offset,
         "fighter parts record exceeds archive data");
    return false;
}

static u32 word(NativeFighterParts* parts, uint32_t offset)
{
    return NativeArchiveBE32(parts->archive->data + offset);
}

static u16 half(NativeFighterParts* parts, uint32_t offset)
{
    const u8* data = parts->archive->data + offset;
    return (u16) (data[0] << 8 | data[1]);
}

static void scalar_words(NativeFighterParts* parts, uint32_t offset,
                         void* output, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        u32 value = word(parts, offset + i * 4);
        memcpy((u8*) output + i * 4, &value, 4);
    }
}

static bool reference(NativeFighterParts* parts, uint32_t field,
                      uint32_t* offset, bool* present)
{
    return NativeArchiveReference(parts->archive, field, offset, present,
                                  parts->error) == NATIVE_ARCHIVE_OK;
}

/* DAT arrays without counts end at the next referenced object or public
 * root. Relocation sites identify pointers, including references to zero. */
static size_t span(NativeFighterParts* parts, uint32_t offset)
{
    size_t low = 0;
    size_t high = parts->bounds_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (parts->bounds[middle] <= offset) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low < parts->bounds_count ? parts->bounds[low] - offset : 0;
}

static void* allocate(NativeFighterParts* parts, PartsRecordType type,
                      uint32_t offset, size_t count, size_t size, bool* fresh)
{
    PartsAllocation* item;
    *fresh = false;
    for (item = parts->allocations; item != NULL; item = item->next) {
        if (item->offset == offset) {
            if (item->type != type || item->count != count) {
                return fail(parts, NATIVE_ARCHIVE_TYPE_CONFLICT, offset,
                            "fighter parts reference has conflicting types");
            }
            return item->data;
        }
    }
    if (count > SIZE_MAX / size) {
        return fail(parts, NATIVE_ARCHIVE_BOUNDS, offset,
                    "fighter parts array is too large");
    }
    item = calloc(1, sizeof(*item));
    if (item == NULL) {
        return fail(parts, NATIVE_ARCHIVE_NO_MEMORY, offset,
                    "cannot allocate fighter parts record");
    }
    item->data = calloc(count == 0 ? 1 : count, size);
    if (item->data == NULL) {
        free(item);
        return fail(parts, NATIVE_ARCHIVE_NO_MEMORY, offset,
                    "cannot allocate fighter parts array");
    }
    item->offset = offset;
    item->type = type;
    item->count = count;
    item->next = parts->allocations;
    parts->allocations = item;
    *fresh = true;
    return item->data;
}

static void* scalar_array(NativeFighterParts* parts, uint32_t offset,
                          size_t count, size_t width)
{
    bool fresh;
    u8* result;
    PartsRecordType type = width == 1   ? PARTS_BYTES
                           : width == 2 ? PARTS_SHORTS
                                        : PARTS_WORDS;
    if (count > SIZE_MAX / width || !range(parts, offset, count * width)) {
        return NULL;
    }
    result = allocate(parts, type, offset, count, width, &fresh);
    if (result == NULL || !fresh) {
        return result;
    }
    if (width == 1) {
        memcpy(result, parts->archive->data + offset, count);
    } else if (width == 2) {
        for (size_t i = 0; i < count; ++i) {
            u16 value = half(parts, offset + i * 2);
            memcpy(result + i * 2, &value, 2);
        }
    } else {
        scalar_words(parts, offset, result, count);
    }
    return result;
}

static bool scalar_reference(NativeFighterParts* parts, uint32_t field,
                             size_t count, size_t width, void** output)
{
    uint32_t offset;
    bool present;
    *output = NULL;
    if (!reference(parts, field, &offset, &present)) {
        return false;
    }
    if (!present) {
        if (count != 0) {
            fail(parts, NATIVE_ARCHIVE_INVALID, field,
                 "fighter parts array has a count without data");
            return false;
        }
        return true;
    }
    *output = scalar_array(parts, offset, count, width);
    return *output != NULL;
}

static void* read_record(NativeFighterParts* parts, PartsRecordType type,
                         uint32_t offset, size_t count);

static bool record_reference(NativeFighterParts* parts, uint32_t field,
                             PartsRecordType type, size_t count, void** output)
{
    uint32_t offset;
    bool present;
    *output = NULL;
    if (!reference(parts, field, &offset, &present)) {
        return false;
    }
    if (!present) {
        return true;
    }
    *output = read_record(parts, type, offset, count);
    return *output != NULL;
}

static void* read_record(NativeFighterParts* parts, PartsRecordType type,
                         uint32_t offset, size_t count)
{
    bool fresh;
    void* output;
    size_t bytes = span(parts, offset);
    size_t size;
    size_t disk_size;
    switch (type) {
    case PARTS_DESCRIPTION:
        size = sizeof(FtPartsDesc);
        disk_size = 8;
        break;
    case PARTS_MODELS:
        size = sizeof(struct ftData_x8);
        disk_size = 24;
        break;
    case PARTS_SHIELD:
        size = sizeof(struct ftData_x20);
        disk_size = 4;
        break;
    case PARTS_DYNAMICS:
        size = sizeof(ftDynamics);
        disk_size = 20;
        break;
    case PARTS_ANIMATION:
        size = sizeof(struct ftData_x1C);
        disk_size = 12;
        break;
    case PARTS_BONES:
        size = sizeof(BoneDynamicsDesc);
        disk_size = 24;
        break;
    case PARTS_COLLISIONS:
        size = sizeof(struct ftData_x38);
        disk_size = 20;
        break;
    case PARTS_VIS_LOOKUP:
        size = sizeof(FtPartsVisLookup);
        disk_size = 8;
        break;
    case PARTS_VIS_INDICES:
        size = sizeof(TempS);
        disk_size = 8;
        break;
    case PARTS_VIS_TABLE:
    case PARTS_REMAP_LIST:
    case PARTS_ANIMATION_LIST:
    case PARTS_ANIMATION_JOINTS:
    case PARTS_LIMIT_LIST:
        size = sizeof(void*);
        disk_size = 4;
        break;
    default:
        return fail(parts, NATIVE_ARCHIVE_UNSUPPORTED, offset,
                    "unsupported fighter parts record");
    }
    bool pointer_list = type == PARTS_VIS_TABLE || type == PARTS_REMAP_LIST ||
                        type == PARTS_ANIMATION_LIST ||
                        type == PARTS_ANIMATION_JOINTS ||
                        type == PARTS_LIMIT_LIST;
    size_t child_count = count;
    if (pointer_list) {
        if (bytes == 0 || bytes % 4 != 0 ||
            (type == PARTS_VIS_TABLE && bytes % 16 != 0) ||
            (type == PARTS_ANIMATION_LIST && bytes > 5 * 4))
        {
            return fail(parts, NATIVE_ARCHIVE_INVALID, offset,
                        "fighter parts pointer array has invalid length");
        }
        count = bytes / 4;
    }
    if (count > SIZE_MAX / disk_size ||
        !range(parts, offset, count * disk_size))
    {
        return NULL;
    }
    output = allocate(parts, type, offset, count, size, &fresh);
    if (output == NULL || !fresh) {
        return output;
    }
    for (size_t i = 0; i < count; ++i) {
        uint32_t at = offset + i * disk_size;
        void* target;
        if (pointer_list) {
            void** list = output;
            PartsRecordType child_type = PARTS_ANIMATION;
            if (type == PARTS_VIS_TABLE) {
                child_type = PARTS_VIS_LOOKUP;
            } else if (type == PARTS_REMAP_LIST || type == PARTS_LIMIT_LIST) {
                uint32_t scalar_offset;
                bool present;
                if (!reference(parts, at, &scalar_offset, &present)) {
                    return NULL;
                }
                if (!present) {
                    continue;
                }
                list[i] = scalar_array(parts, scalar_offset, child_count,
                                       type == PARTS_REMAP_LIST ? 2 : 4);
                if (list[i] == NULL) {
                    return NULL;
                }
                continue;
            } else if (type == PARTS_ANIMATION_JOINTS) {
                uint32_t anim_offset;
                bool present;
                HSD_AnimJoint* animation = NULL;
                if (!reference(parts, at, &anim_offset, &present) ||
                    (present && NativeArchiveAnimation(
                                    parts->graph, anim_offset, &animation,
                                    parts->error) != NATIVE_ARCHIVE_OK))
                {
                    return NULL;
                }
                list[i] = animation;
                continue;
            }
            if (!record_reference(parts, at, child_type, child_count,
                                  &list[i]))
            {
                return NULL;
            }
        } else if (type == PARTS_MODELS || type == PARTS_DESCRIPTION) {
            struct ftData_x8* item = output;
            FtPartsDesc* description =
                type == PARTS_MODELS ? &item->x0 : output;
            description->model_num = word(parts, at);
            if (description->model_num > 11) {
                return fail(parts, NATIVE_ARCHIVE_INVALID, at,
                            "fighter model count exceeds capacity");
            }
            if (!record_reference(parts, at + 4, PARTS_VIS_TABLE,
                                  description->model_num, &target))
            {
                return NULL;
            }
            description->vis_table = target;
            if (type == PARTS_MODELS) {
                item->x8.x8 = word(parts, at + 8);
                if (item->x8.x8 > 5) {
                    return fail(parts, NATIVE_ARCHIVE_INVALID, at + 8,
                                "fighter texture count exceeds capacity");
                }
                if (!record_reference(parts, at + 12, PARTS_REMAP_LIST,
                                      item->x8.x8, &target))
                {
                    return NULL;
                }
                item->x8.xC = target;
                memcpy(&item->x10, parts->archive->data + at + 16, 5);
            }
        } else if (type == PARTS_VIS_LOOKUP || type == PARTS_VIS_INDICES) {
            u32 elements = word(parts, at);
            if (elements > INT32_MAX) {
                return fail(parts, NATIVE_ARCHIVE_INVALID, at,
                            "fighter visibility count is negative");
            }
            if (type == PARTS_VIS_LOOKUP) {
                FtPartsVisLookup* item = &((FtPartsVisLookup*) output)[i];
                item->x0 = elements;
                if (!record_reference(parts, at + 4, PARTS_VIS_INDICES,
                                      elements, &target))
                {
                    return NULL;
                }
                item->x4 = target;
            } else {
                TempS* item = &((TempS*) output)[i];
                item->x0 = elements;
                if (!scalar_reference(parts, at + 4, elements, 1, &target)) {
                    return NULL;
                }
                item->x4 = target;
            }
            if (elements != 0 && target == NULL) {
                return fail(parts, NATIVE_ARCHIVE_INVALID, at,
                            "fighter visibility count has no data");
            }
        } else if (type == PARTS_ANIMATION) {
            struct ftData_x1C* item = output;
            item->x0 = half(parts, at);
            item->x2 = half(parts, at + 2);
            if (!scalar_reference(parts, at + 4, item->x2, 1, &target)) {
                return NULL;
            }
            item->x4 = target;
            if (!record_reference(parts, at + 8, PARTS_ANIMATION_JOINTS, 1,
                                  &target))
            {
                return NULL;
            }
            item->x8 = target;
        } else if (type == PARTS_SHIELD) {
            struct ftData_x20* item = output;
            HSD_Joint* joint = NULL;
            uint32_t joint_offset;
            bool present;
            if (!reference(parts, at, &joint_offset, &present) ||
                (present &&
                 NativeArchiveJoint(parts->graph, joint_offset, &joint,
                                    parts->error) != NATIVE_ARCHIVE_OK))
            {
                return NULL;
            }
            /* The disk field points to a joint. Guard reads its child. */
            item->x0 = (void*) joint;
        } else if (type == PARTS_DYNAMICS) {
            ftDynamics* item = output;
            item->dynamicsNum = word(parts, at);
            item->x4 = word(parts, at + 8);
            if (item->dynamicsNum < 0 ||
                item->dynamicsNum >= Ft_Dynamics_NumMax || item->x4 < 0 ||
                item->x4 > 11)
            {
                return fail(parts, NATIVE_ARCHIVE_INVALID, at,
                            "fighter dynamics count exceeds capacity");
            }
            if (!record_reference(parts, at + 4, PARTS_BONES,
                                  item->dynamicsNum, &target))
            {
                return NULL;
            }
            item->ftDynamicBones = target;
            if (item->dynamicsNum != 0 && target == NULL) {
                return fail(parts, NATIVE_ARCHIVE_INVALID, at,
                            "fighter dynamics count has no bones");
            }
            if (!record_reference(parts, at + 12, PARTS_COLLISIONS, item->x4,
                                  &target))
            {
                return NULL;
            }
            item->x8 = target;
            if (item->x4 != 0 && target == NULL) {
                return fail(parts, NATIVE_ARCHIVE_INVALID, at,
                            "fighter dynamics count has no collision data");
            }
            if (!record_reference(parts, at + 16, PARTS_LIMIT_LIST,
                                  item->dynamicsNum, &target))
            {
                return NULL;
            }
            item->x10 = target;
        } else if (type == PARTS_BONES) {
            BoneDynamicsDesc* item = &((BoneDynamicsDesc*) output)[i];
            item->bone_id = word(parts, at);
            item->dyn_desc.count = word(parts, at + 8);
            if (!scalar_reference(parts, at + 4,
                                  (size_t) item->dyn_desc.count * 15, 4,
                                  &target))
            {
                return NULL;
            }
            item->dyn_desc.data = target;
            scalar_words(parts, at + 12, &item->dyn_desc.pos, 3);
        } else if (type == PARTS_COLLISIONS) {
            struct ftData_x38* item = &((struct ftData_x38*) output)[i];
            item->x0 = word(parts, at);
            scalar_words(parts, at + 4, &item->x4, 3);
            scalar_words(parts, at + 16, &item->x10, 1);
        }
    }
    return output;
}

static int compare_offsets(const void* left, const void* right)
{
    u32 a = *(const u32*) left;
    u32 b = *(const u32*) right;
    return a < b ? -1 : a > b;
}

NativeArchiveStatus NativeFighterPartsOpen(const NativeArchive* archive,
                                           NativeArchiveGraph* graph,
                                           NativeFighterParts** output,
                                           NativeArchiveError* error)
{
    NativeFighterParts* parts;
    if (output == NULL || archive == NULL || graph == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "invalid fighter parts context");
    }
    *output = NULL;
    parts = calloc(1, sizeof(*parts));
    if (parts == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_NO_MEMORY, 0,
                                 "cannot allocate fighter parts context");
    }
    parts->archive = archive;
    parts->graph = graph;
    parts->bounds_count =
        (size_t) archive->reloc_count + archive->public_count + 1;
    parts->bounds = calloc(parts->bounds_count, sizeof(*parts->bounds));
    if (parts->bounds == NULL) {
        free(parts);
        return NativeArchiveFail(error, NATIVE_ARCHIVE_NO_MEMORY, 0,
                                 "cannot allocate fighter parts boundaries");
    }
    size_t i;
    for (i = 0; i < archive->reloc_count; ++i) {
        parts->bounds[i] = word(parts, archive->relocations[i]);
    }
    for (size_t j = 0; j < archive->public_count; ++j) {
        parts->bounds[i++] =
            NativeArchiveBE32(archive->file + archive->public_at + j * 8);
    }
    parts->bounds[i] = archive->data_size;
    qsort(parts->bounds, parts->bounds_count, sizeof(*parts->bounds),
          compare_offsets);
    *output = parts;
    return NATIVE_ARCHIVE_OK;
}

void NativeFighterPartsClose(NativeFighterParts* parts)
{
    if (parts != NULL) {
        while (parts->allocations != NULL) {
            PartsAllocation* item = parts->allocations;
            parts->allocations = item->next;
            free(item->data);
            free(item);
        }
        free(parts->bounds);
        free(parts);
    }
}

NativeArchiveStatus NativeFighterPartsRead(NativeFighterParts* parts,
                                           NativeFighterPartsType type,
                                           uint32_t offset, void** output,
                                           NativeArchiveError* error)
{
    NativeArchiveError local_error = { NATIVE_ARCHIVE_OK, 0, NULL };
    PartsRecordType record;
    if (output != NULL) {
        *output = NULL;
    }
    if (parts == NULL || output == NULL || parts->failed) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "invalid fighter parts context");
    }
    switch (type) {
    case NATIVE_FIGHTER_PARTS_MODELS:
        record = PARTS_MODELS;
        break;
    case NATIVE_FIGHTER_PARTS_ANIMATIONS:
        record = PARTS_ANIMATION_LIST;
        break;
    case NATIVE_FIGHTER_PARTS_SHIELD:
        record = PARTS_SHIELD;
        break;
    case NATIVE_FIGHTER_PARTS_DYNAMICS:
        record = PARTS_DYNAMICS;
        break;
    case NATIVE_FIGHTER_PARTS_DESCRIPTION:
        record = PARTS_DESCRIPTION;
        break;
    default:
        return NativeArchiveFail(error, NATIVE_ARCHIVE_UNSUPPORTED,
                                 32u + offset,
                                 "unsupported fighter parts root");
    }
    parts->error = &local_error;
    void* result = read_record(parts, record, offset, 1);
    parts->error = NULL;
    if (result == NULL) {
        parts->failed = true;
        if (error != NULL) {
            *error = local_error;
        }
        return local_error.status;
    }
    *output = result;
    return NATIVE_ARCHIVE_OK;
}

NativeArchiveStatus
NativeFighterPartsVisibility(NativeFighterParts* parts, uint32_t offset,
                             size_t count, struct FtPartsVisLookup** output,
                             NativeArchiveError* error)
{
    NativeArchiveError local_error = { NATIVE_ARCHIVE_OK, 0, NULL };
    if (output != NULL) {
        *output = NULL;
    }
    if (parts == NULL || output == NULL || parts->failed || count > 11) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "invalid fighter visibility request");
    }
    parts->error = &local_error;
    void* result = read_record(parts, PARTS_VIS_LOOKUP, offset, count);
    parts->error = NULL;
    if (result == NULL) {
        parts->failed = true;
        if (error != NULL) {
            *error = local_error;
        }
        return local_error.status;
    }
    *output = result;
    return NATIVE_ARCHIVE_OK;
}
