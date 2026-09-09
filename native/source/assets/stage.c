#include "stage.h"

#include <stdlib.h>
#include <string.h>

#include "archive_internal.h"
#include <melee/gr/types.h>
#include <melee/mp/types.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/jobj.h>

typedef struct StageAllocation {
    void* data;
    struct StageAllocation* next;
} StageAllocation;

struct NativeStageArchive {
    const NativeArchive* archive;
    NativeArchiveGraph* graph;
    StageAllocation* allocations;
    NativeArchiveError* error;
    void* roots[5];
};

typedef struct {
    HSD_Joint* joint;
    s16* pairs;
    s32 count;
} StageJointBinding;

typedef struct {
    HSD_LightDesc* desc;
    u8 a : 1;
    u8 b : 1;
    u8 c : 1;
    u8 unused : 5;
    u8 padding[3];
} StageLightOverride;

static void* fail(NativeStageArchive* stage, uint32_t offset,
                  const char* message)
{
    NativeArchiveFail(stage->error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                      message);
    return NULL;
}

static bool range(NativeStageArchive* stage, uint32_t offset, size_t size)
{
    if (NativeArchiveDataRange(stage->archive, offset, size)) {
        return true;
    }
    NativeArchiveFail(stage->error, NATIVE_ARCHIVE_BOUNDS, 32u + offset,
                      "stage record exceeds archive data");
    return false;
}

static void* allocate(NativeStageArchive* stage, size_t count, size_t size)
{
    StageAllocation* allocation;
    void* data;
    if (size != 0 && count > SIZE_MAX / size) {
        return fail(stage, 0, "stage array is too large");
    }
    data = calloc(count == 0 ? 1 : count, size);
    allocation = malloc(sizeof(*allocation));
    if (data == NULL || allocation == NULL) {
        free(data);
        free(allocation);
        NativeArchiveFail(stage->error, NATIVE_ARCHIVE_NO_MEMORY, 0,
                          "cannot allocate stage record");
        return NULL;
    }
    allocation->data = data;
    allocation->next = stage->allocations;
    stage->allocations = allocation;
    return data;
}

static u16 read16(const u8* data)
{
    return (u16) ((u16) data[0] << 8 | data[1]);
}

static f32 read_float(const u8* data)
{
    u32 value = NativeArchiveBE32(data);
    f32 result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static bool reference(NativeStageArchive* stage, uint32_t field,
                      uint32_t* target, bool* present)
{
    return NativeArchiveReference(stage->archive, field, target, present,
                                  stage->error) == NATIVE_ARCHIVE_OK;
}

/* Scalar blocks have no host pointers. The caller specifies each element's
 * serialized width, so float bits and signed integer bits stay unchanged. */
static void* scalar_array(NativeStageArchive* stage, uint32_t offset,
                          size_t count, size_t width)
{
    u8* result;
    if (count > SIZE_MAX / width || !range(stage, offset, count * width)) {
        return NULL;
    }
    result = allocate(stage, count, width);
    if (result == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (width == 2) {
            u16 value = read16(stage->archive->data + offset + i * width);
            memcpy(result + i * width, &value, width);
        } else {
            u32 value =
                NativeArchiveBE32(stage->archive->data + offset + i * width);
            memcpy(result + i * width, &value, width);
        }
    }
    return result;
}

static bool array_reference(NativeStageArchive* stage, uint32_t field,
                            size_t count, size_t width, uint32_t* target)
{
    bool present;
    if (!reference(stage, field, target, &present)) {
        return false;
    }
    if (count == 0) {
        return true;
    }
    if (!present || count > SIZE_MAX / width) {
        fail(stage, field, "stage array has a count without data");
        return false;
    }
    return range(stage, *target, count * width);
}

typedef enum {
    STAGE_NativeArchiveJoint,
    STAGE_NativeArchiveMObj,
    STAGE_NativeArchiveAnimation,
    STAGE_NativeArchiveMatAnimJoint,
    STAGE_NativeArchiveShapeAnimJoint,
    STAGE_NativeArchiveCObj,
    STAGE_NativeArchiveCameraAnimation,
    STAGE_NativeArchiveLight,
    STAGE_NativeArchiveLightAnimation,
    STAGE_NativeArchiveFog,
} DescriptorReader;

static NativeArchiveStatus read_descriptor(NativeStageArchive* stage,
                                           uint32_t offset,
                                           DescriptorReader reader,
                                           void* output)
{
    switch (reader) {
#define DESCRIPTOR_CASE(function)                                             \
    case STAGE_##function:                                                    \
        return function(stage->graph, offset, output, stage->error)
        DESCRIPTOR_CASE(NativeArchiveJoint);
        DESCRIPTOR_CASE(NativeArchiveMObj);
        DESCRIPTOR_CASE(NativeArchiveAnimation);
        DESCRIPTOR_CASE(NativeArchiveMatAnimJoint);
        DESCRIPTOR_CASE(NativeArchiveShapeAnimJoint);
        DESCRIPTOR_CASE(NativeArchiveCObj);
        DESCRIPTOR_CASE(NativeArchiveCameraAnimation);
        DESCRIPTOR_CASE(NativeArchiveLight);
        DESCRIPTOR_CASE(NativeArchiveLightAnimation);
        DESCRIPTOR_CASE(NativeArchiveFog);
#undef DESCRIPTOR_CASE
    }
    return NATIVE_ARCHIVE_UNSUPPORTED;
}

static void** descriptor_list(NativeStageArchive* stage, uint32_t offset,
                              DescriptorReader reader)
{
    size_t count = 0;
    size_t limit = (stage->archive->data_size - offset) / 4;
    uint32_t target;
    bool present;
    void** list;
    while (count < limit) {
        if (!reference(stage, offset + count * 4, &target, &present)) {
            return NULL;
        }
        if (!present) {
            break;
        }
        ++count;
    }
    if (count == limit) {
        return fail(stage, offset, "unterminated stage descriptor list");
    }
    list = allocate(stage, count + 1, sizeof(*list));
    if (list == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!reference(stage, offset + i * 4, &target, &present) ||
            read_descriptor(stage, target, reader, &list[i]) !=
                NATIVE_ARCHIVE_OK)
        {
            return NULL;
        }
    }
    return list;
}

static void* ground_param(NativeStageArchive* stage, uint32_t offset)
{
    GroundParam* result;
    StageParam* params;
    const u8* data;
    uint32_t target;
    if (!range(stage, offset, 0xDC)) {
        return NULL;
    }
    data = stage->archive->data + offset;
    result = allocate(stage, 1, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    /* GroundParam has only scalars before the pointer at 0xB0. */
    memcpy(result, data, 0xB0);
    for (size_t i = 0; i < 0x68; i += 4) {
        u32 value = NativeArchiveBE32(data + i);
        memcpy((u8*) result + i, &value, 4);
    }
    result->x4 = read16(data + 4);
    result->x6_pad[0] = data[6];
    result->x6_pad[1] = data[7];
    result->x8 = read16(data + 8);
    result->xA = read16(data + 10);
    result->x2C_pad[0] = data[0x2C];
    result->x2C_pad[1] = data[0x2D];
    result->x2E = read16(data + 0x2E);
    result->x68 = read16(data + 0x68);
    for (size_t i = 0; i < 35; ++i) {
        result->x6A[i] = read16(data + 0x6A + i * 2);
    }
    result->stage_param_count = NativeArchiveBE32(data + 0xB4);
    if (result->stage_param_count < 0 ||
        !array_reference(stage, offset + 0xB0, result->stage_param_count, 0x64,
                         &target))
    {
        return NULL;
    }
    params = allocate(stage, result->stage_param_count, sizeof(*params));
    if (params == NULL) {
        return NULL;
    }
    result->stage_params = params;
    for (size_t i = 0; i < (size_t) result->stage_param_count; ++i) {
        const u8* row = stage->archive->data + target + i * 0x64;
        for (size_t j = 0; j < 0x14; j += 4) {
            u32 value = NativeArchiveBE32(row + j);
            memcpy((u8*) &params[i] + j, &value, 4);
        }
        for (size_t j = 0x14; j < 0x64; j += 2) {
            u16 value = read16(row + j);
            memcpy((u8*) &params[i] + j, &value, 2);
        }
    }
    memcpy(&result->xB8, data + 0xB8, 9 * sizeof(GXColor));
    return result;
}

static void* collision(NativeStageArchive* stage, uint32_t offset)
{
    MapCollData* result;
    const u8* data;
    uint32_t target;
    if (!range(stage, offset, 0x2C)) {
        return NULL;
    }
    data = stage->archive->data + offset;
    result = allocate(stage, 1, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    result->vert_count = NativeArchiveBE32(data + 4);
    result->line_count = NativeArchiveBE32(data + 12);
    result->joint_count = NativeArchiveBE32(data + 40);
    if (result->vert_count < 0 || result->line_count < 0 ||
        result->joint_count < 0)
    {
        return fail(stage, offset, "negative stage collision count");
    }
    if (!array_reference(stage, offset, result->vert_count, 8, &target)) {
        return NULL;
    }
    result->verts = scalar_array(stage, target, result->vert_count * 2, 4);
    if (!array_reference(stage, offset + 8, result->line_count, 16, &target)) {
        return NULL;
    }
    result->lines = scalar_array(stage, target, result->line_count * 8, 2);
    for (size_t i = 0; i < 10; ++i) {
        u16 value = read16(data + 16 + i * 2);
        memcpy((u8*) &result->floor_start + i * 2, &value, 2);
    }
    if (!array_reference(stage, offset + 36, result->joint_count, 40, &target))
    {
        return NULL;
    }
    result->joints =
        allocate(stage, result->joint_count, sizeof(*result->joints));
    if (result->verts == NULL || result->lines == NULL ||
        result->joints == NULL)
    {
        return NULL;
    }
    for (size_t i = 0; i < (size_t) result->joint_count; ++i) {
        const u8* row = stage->archive->data + target + i * 40;
        for (size_t j = 0; j < 20; j += 2) {
            u16 value = read16(row + j);
            memcpy((u8*) &result->joints[i] + j, &value, 2);
        }
        for (size_t j = 20; j < 36; j += 4) {
            u32 value = NativeArchiveBE32(row + j);
            memcpy((u8*) &result->joints[i] + j, &value, 4);
        }
        result->joints[i].vtx_start = read16(row + 36);
        result->joints[i].vtx_count = read16(row + 38);
    }
    return result;
}

static bool descriptor_field(NativeStageArchive* stage, uint32_t field,
                             DescriptorReader reader, void* output)
{
    uint32_t target;
    bool present;
    if (!reference(stage, field, &target, &present)) {
        return false;
    }
    return !present ||
           read_descriptor(stage, target, reader, output) == NATIVE_ARCHIVE_OK;
}

#define READER(function) STAGE_##function

static bool animation_fields(NativeStageArchive* stage, uint32_t offset,
                             HSD_AnimJoint*** anims,
                             HSD_MatAnimJoint*** matanims,
                             HSD_ShapeAnimJoint*** shapeanims)
{
    DescriptorReader readers[] = { READER(NativeArchiveAnimation),
                                   READER(NativeArchiveMatAnimJoint),
                                   READER(NativeArchiveShapeAnimJoint) };
    void* outputs[] = { anims, matanims, shapeanims };
    for (size_t i = 0; i < 3; ++i) {
        uint32_t target;
        bool present;
        if (!reference(stage, offset + i * 4, &target, &present)) {
            return false;
        }
        if (present) {
            void** list = descriptor_list(stage, target, readers[i]);
            if (list == NULL) {
                return false;
            }
            memcpy(outputs[i], &list, sizeof(list));
        }
    }
    return true;
}

static LightList** lights(NativeStageArchive* stage, uint32_t offset)
{
    size_t count = 0;
    uint32_t target;
    bool present;
    LightList** result;
    while (range(stage, offset + count * 4, 4)) {
        if (!reference(stage, offset + count * 4, &target, &present)) {
            return NULL;
        }
        if (!present) {
            break;
        }
        ++count;
    }
    if (stage->error->status != NATIVE_ARCHIVE_OK) {
        return NULL;
    }
    result = allocate(stage, count + 1, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        uint32_t animations;
        if (!reference(stage, offset + i * 4, &target, &present) ||
            !range(stage, target, 8))
        {
            return NULL;
        }
        result[i] = allocate(stage, 1, sizeof(**result));
        if (result[i] == NULL ||
            !descriptor_field(stage, target, READER(NativeArchiveLight),
                              &result[i]->desc) ||
            !reference(stage, target + 4, &animations, &present))
        {
            return NULL;
        }
        if (present) {
            result[i]->anims = (HSD_LightAnim**) descriptor_list(
                stage, animations, READER(NativeArchiveLightAnimation));
            if (result[i]->anims == NULL) {
                return NULL;
            }
        }
    }
    return result;
}

static bool model(NativeStageArchive* stage, uint32_t offset,
                  struct UnkStageDat_x8_t* result)
{
    uint32_t target;
    bool present;
    HSD_CObjDesc* camera = NULL;
    if (!range(stage, offset, 0x34) ||
        !descriptor_field(stage, offset, READER(NativeArchiveJoint),
                          &result->unk0) ||
        !animation_fields(stage, offset + 4, &result->unk4, &result->unk8,
                          &result->unkC) ||
        !descriptor_field(stage, offset + 0x10, READER(NativeArchiveCObj),
                          &camera))
    {
        return false;
    }
    result->x10 = camera == NULL ? NULL : &camera->perspective;
    if (!reference(stage, offset + 0x14, &target, &present)) {
        return false;
    }
    if (present) {
        result->x14 = descriptor_list(stage, target,
                                      READER(NativeArchiveCameraAnimation));
        if (result->x14 == NULL) {
            return false;
        }
    }
    if (!reference(stage, offset + 0x18, &target, &present)) {
        return false;
    }
    if (present && (result->x18 = lights(stage, target)) == NULL) {
        return false;
    }
    if (!descriptor_field(stage, offset + 0x1C, READER(NativeArchiveFog),
                          &result->x1C))
    {
        return false;
    }
    result->unk24 = NativeArchiveBE32(stage->archive->data + offset + 0x24);
    if (result->unk24 < 0 ||
        !array_reference(stage, offset + 0x20, result->unk24, 6, &target))
    {
        return false;
    }
    if (result->unk24 != 0) {
        result->unk20 = scalar_array(stage, target, result->unk24 * 3, 2);
        if (result->unk20 == NULL) {
            return false;
        }
    }
    if (!reference(stage, offset + 0x28, &target, &present)) {
        return false;
    }
    if (present) {
        result->x28 = (void*) (stage->archive->data + target);
    }
    result->x30 = NativeArchiveBE32(stage->archive->data + offset + 0x30);
    if (result->x30 < 0 ||
        !array_reference(stage, offset + 0x2C, result->x30, 2, &target))
    {
        return false;
    }
    if (result->x30 != 0) {
        result->x2C = scalar_array(stage, target, result->x30, 2);
        if (result->x2C == NULL) {
            return false;
        }
    }
    return true;
}

static UnkStageDat* map_head(NativeStageArchive* stage, uint32_t offset)
{
    UnkStageDat* result;
    const u8* data;
    uint32_t target;
    if (!range(stage, offset, 48)) {
        return NULL;
    }
    data = stage->archive->data + offset;
    result = allocate(stage, 1, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    result->unk4 = NativeArchiveBE32(data + 4);
    result->unkC = NativeArchiveBE32(data + 12);
    result->unk14 = NativeArchiveBE32(data + 20);
    /* The light override length counts 32-bit words. Each light record
     * contains one reference word and one flags word. */
    u32 light_words = NativeArchiveBE32(data + 28);
    if (light_words & 1) {
        return fail(stage, offset, "odd stage light table word count");
    }
    result->unk1C = light_words / 2;
    result->unk24 = NativeArchiveBE32(data + 36);
    result->unk2C = NativeArchiveBE32(data + 44);
    if (result->unk4 < 0 || result->unkC < 0 || result->unk14 < 0 ||
        result->unk2C < 0)
    {
        return fail(stage, offset, "negative stage map count");
    }
    if (!array_reference(stage, offset, result->unk4, 12, &target)) {
        return NULL;
    }
    StageJointBinding* bindings =
        allocate(stage, result->unk4, sizeof(*bindings));
    if (bindings == NULL) {
        return NULL;
    }
    result->unk0 = bindings;
    for (size_t i = 0; i < (size_t) result->unk4; ++i) {
        uint32_t field = target + i * 12, pairs;
        if (!descriptor_field(stage, field, READER(NativeArchiveJoint),
                              &bindings[i].joint))
        {
            return NULL;
        }
        bindings[i].count =
            NativeArchiveBE32(stage->archive->data + field + 8);
        if (bindings[i].count < 0 ||
            !array_reference(stage, field + 4, bindings[i].count, 4, &pairs))
        {
            return NULL;
        }
        bindings[i].pairs =
            scalar_array(stage, pairs, bindings[i].count * 2, 2);
        if (bindings[i].pairs == NULL) {
            return NULL;
        }
    }
    if (!array_reference(stage, offset + 8, result->unkC, 0x34, &target)) {
        return NULL;
    }
    result->unk8 = allocate(stage, result->unkC, sizeof(*result->unk8));
    if (result->unk8 == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < (size_t) result->unkC; ++i) {
        if (!model(stage, target + i * 0x34, &result->unk8[i])) {
            return NULL;
        }
    }
    if (!array_reference(stage, offset + 16, result->unk14, 4, &target)) {
        return NULL;
    }
    result->unk10 = allocate(stage, result->unk14, sizeof(*result->unk10));
    if (result->unk10 == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < (size_t) result->unk14; ++i) {
        uint32_t item;
        bool present;
        if (!reference(stage, target + i * 4, &item, &present)) {
            return NULL;
        }
        if (present &&
            NativeArchiveSpline(stage->graph, item, &result->unk10[i],
                                stage->error) != NATIVE_ARCHIVE_OK)
        {
            return NULL;
        }
    }
    if (!array_reference(stage, offset + 24, result->unk1C, 8, &target)) {
        return NULL;
    }
    StageLightOverride* overrides =
        allocate(stage, result->unk1C, sizeof(*overrides));
    if (overrides == NULL) {
        return NULL;
    }
    result->unk18 = overrides;
    for (size_t i = 0; i < (size_t) result->unk1C; ++i) {
        if (!descriptor_field(stage, target + i * 8,
                              READER(NativeArchiveLight), &overrides[i].desc))
        {
            return NULL;
        }
        u8 flags = stage->archive->data[target + i * 8 + 4];
        overrides[i].a = (flags >> 7) & 1;
        overrides[i].b = (flags >> 6) & 1;
        overrides[i].c = (flags >> 5) & 1;
    }
    if (!array_reference(stage, offset + 32, result->unk24, 8, &target)) {
        return NULL;
    }
    result->unk20 = allocate(stage, result->unk24, sizeof(*result->unk20));
    if (result->unk20 == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < (size_t) result->unk24; ++i) {
        if (!descriptor_field(stage, target + i * 8,
                              READER(NativeArchiveLightAnimation),
                              &result->unk20[i].unk0))
        {
            return NULL;
        }
        result->unk20[i].flag =
            (stage->archive->data[target + i * 8 + 4] >> 7) & 1;
    }
    if (!array_reference(stage, offset + 40, result->unk2C, 4, &target)) {
        return NULL;
    }
    result->unk28 = allocate(stage, result->unk2C, sizeof(*result->unk28));
    if (result->unk28 == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < (size_t) result->unk2C; ++i) {
        if (!descriptor_field(stage, target + i * 4, READER(NativeArchiveMObj),
                              &result->unk28[i]))
        {
            return NULL;
        }
    }
    return result;
}

static void* zebes_parameters(NativeStageArchive* stage, uint32_t offset)
{
    grZe_YakumonoParam* result;
    uint32_t hit_offset;
    bool present;
    if (!range(stage, offset, 0x190)) {
        return NULL;
    }
    result = allocate(stage, 1, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    const u8* data = stage->archive->data + offset;
    for (size_t i = 0; i < 0x2C; i += 4) {
        u32 value = NativeArchiveBE32(data + i);
        memcpy((u8*) result + i, &value, 4);
    }
    if (!reference(stage, offset + 0x2C, &hit_offset, &present)) {
        return NULL;
    }
    if (present) {
        result->x2C = scalar_array(stage, hit_offset, 9, 4);
        if (result->x2C == NULL) {
            return NULL;
        }
    }
    for (size_t i = 0x30; i < 0xA0; i += 4) {
        u32 value = NativeArchiveBE32(data + i);
        memcpy((u8*) result + offsetof(grZe_YakumonoParam, x30) + i - 0x30,
               &value, 4);
    }
    for (size_t i = 0; i < 30; ++i) {
        grZe_AcidLevelEntry* entry = &result->xA0_entries[i];
        const u8* source = data + 0xA0 + i * 8;
        entry->x0_base = (s16) read16(source);
        entry->x2_delay_min = (s16) read16(source + 2);
        entry->x4_delay_max = (s16) read16(source + 4);
        entry->x6_level = (s16) read16(source + 6);
    }
    return result;
}

typedef struct NativeCastleEntry {
    s16 x0;
    u8 padding[2];
    f32 x4;
    Vec3 rot;
} NativeCastleEntry;

typedef struct NativeCastleParameters {
    s16 x0, x2, x4, x6, x8, xA, xC, xE;
    f32 x10, x14, x18;
    u8 padding1C[4];
    f32 x20, x24, x28, x2C, x30, x34, x38, x3C;
    s16 x40, x42, x44;
    u8 padding46[2];
    f32 x48, x4C, x50;
    s16 x54;
    u8 padding56[2];
    s16 x58;
    u8 padding5A[2];
    NativeCastleEntry entries[9];
    f32 x110;
    void* x114;
    f32 x118, x11C, x120, x124;
    u8 padding128[4];
    s16 x12C[4];
    f32 x134, x138, x13C, x140;
} NativeCastleParameters;
_Static_assert(sizeof(NativeCastleParameters) == 0x150, "native castle parameter layout");

static void* castle_parameters(NativeStageArchive* stage, uint32_t offset)
{
    NativeCastleParameters* result;
    const u8* data;
    uint32_t target;
    bool present;
    if (!range(stage, offset, 0x144)) {
        return NULL;
    }
    result = allocate(stage, 1, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    data = stage->archive->data + offset;
    /* Scalar fields keep their serialized big-endian bit patterns. */
    for (size_t i = 0; i < 8; ++i) {
        ((s16*) result)[i] = (s16) read16(data + i * 2);
    }
    for (size_t i = 0; i < 3; ++i) {
        u32 value = NativeArchiveBE32(data + 0x10 + i * 4);
        memcpy((u8*) result + offsetof(NativeCastleParameters, x10) + i * 4,
               &value, 4);
    }
    for (size_t i = 0x20; i < 0x40; i += 4) {
        u32 value = NativeArchiveBE32(data + i);
        memcpy((u8*) result + offsetof(NativeCastleParameters, x20) + i - 0x20,
               &value, 4);
    }
    result->x40 = (s16) read16(data + 0x40);
    result->x42 = (s16) read16(data + 0x42);
    result->x44 = (s16) read16(data + 0x44);
    result->x48 = read_float(data + 0x48);
    result->x4C = read_float(data + 0x4C);
    result->x50 = read_float(data + 0x50);
    result->x54 = (s16) read16(data + 0x54);
    result->x58 = (s16) read16(data + 0x58);
    for (size_t i = 0; i < 9; ++i) {
        const u8* source = data + 0x5C + i * sizeof(NativeCastleEntry);
        NativeCastleEntry* entry = &result->entries[i];
        entry->x0 = (s16) read16(source);
        entry->x4 = read_float(source + 4);
        entry->rot.x = read_float(source + 8);
        entry->rot.y = read_float(source + 12);
        entry->rot.z = read_float(source + 16);
    }
    result->x110 = read_float(data + 0x110);
    if (!reference(stage, offset + 0x114, &target, &present)) {
        return NULL;
    }
    if (present) {
        if (!range(stage, target, 4)) {
            return NULL;
        }
        result->x114 = (void*) (stage->archive->data + target);
    }
    for (size_t i = 0; i < 4; ++i) {
        u32 value = NativeArchiveBE32(data + 0x118 + i * 4);
        memcpy((u8*) result + offsetof(NativeCastleParameters, x118) + i * 4,
               &value, 4);
    }
    for (size_t i = 0; i < 4; ++i) {
        result->x12C[i] = (s16) read16(data + 0x12C + i * 2);
    }
    for (size_t i = 0; i < 4; ++i) {
        u32 value = NativeArchiveBE32(data + 0x134 + i * 4);
        memcpy((u8*) result + offsetof(NativeCastleParameters, x134) + i * 4,
               &value, 4);
    }
    return result;
}

typedef struct NativeKongoParameters {
    f32 unk0, unk4, unk8, unkC, unk10, unk14, unk18, unk1C;
    f32 unk20, unk24, unk28, unk2C, unk30, unk34, unk38, unk3C, unk40;
    s16 unk44, unk46, unk48, unk4A, unk4C, unk4E, unk50, unk52;
    f32 unk54, unk58, unk5C, unk60;
    s32 unk64, unk68;
    f32 unk6C, unk70, unk74, unk78, unk7C, unk80;
    void* unk84;
    f32 unk88, unk8C, unk90, unk94, unk98, unk9C, unkA0, unkA4, unkA8,
        unkAC, unkB0, unkB4, unkB8;
} NativeKongoParameters;
_Static_assert(sizeof(NativeKongoParameters) == 0xC8,
               "native kongo parameter layout");

static void* kongo_parameters(NativeStageArchive* stage, uint32_t offset)
{
    NativeKongoParameters* result;
    const u8* data;
    uint32_t target;
    bool present;
    if (!range(stage, offset, 0xBC)) {
        return NULL;
    }
    result = allocate(stage, 1, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    data = stage->archive->data + offset;
    for (size_t i = 0; i < 17; ++i) {
        u32 value = NativeArchiveBE32(data + i * 4);
        memcpy((u8*) result + i * 4, &value, 4);
    }
    for (size_t i = 0; i < 8; ++i) {
        ((s16*) result)[0x44 / 2 + i] = (s16) read16(data + 0x44 + i * 2);
    }
    for (size_t i = 0; i < 4; ++i) {
        u32 value = NativeArchiveBE32(data + 0x54 + i * 4);
        memcpy((u8*) result + offsetof(NativeKongoParameters, unk54) + i * 4,
               &value, 4);
    }
    for (size_t i = 0; i < 2; ++i) {
        u32 value = NativeArchiveBE32(data + 0x64 + i * 4);
        memcpy((u8*) result + offsetof(NativeKongoParameters, unk64) + i * 4,
               &value, 4);
    }
    for (size_t i = 0; i < 6; ++i) {
        u32 value = NativeArchiveBE32(data + 0x6C + i * 4);
        memcpy((u8*) result + offsetof(NativeKongoParameters, unk6C) + i * 4,
               &value, 4);
    }
    if (!reference(stage, offset + 0x84, &target, &present)) {
        return NULL;
    }
    if (present) {
        if (!range(stage, target, 4)) {
            return NULL;
        }
        result->unk84 = (void*) (stage->archive->data + target);
    }
    for (size_t i = 0; i < 13; ++i) {
        u32 value = NativeArchiveBE32(data + 0x88 + i * 4);
        memcpy((u8*) result + offsetof(NativeKongoParameters, unk88) + i * 4,
               &value, 4);
    }
    return result;
}

typedef struct NativeCorneriaParameters {
    f32 x0, x4, x8, xC, x10, x14, x18, x1C, x20, x24, x28, x2C, x30, x34,
        x38, x3C, x40, x44, x48, x4C;
    u8 padding50[0x18];
    f32 x68;
    u8 padding6C[4];
    f32 x70;
    s32 x74, x78, x7C, x80;
    void* x84;
    f32 x88;
} NativeCorneriaParameters;
_Static_assert(sizeof(NativeCorneriaParameters) == 0x98,
               "native corneria parameter layout");

static void* corneria_parameters(NativeStageArchive* stage, uint32_t offset)
{
    NativeCorneriaParameters* result;
    const u8* data;
    uint32_t target;
    bool present;
    if (!range(stage, offset, 0x8C)) {
        return NULL;
    }
    result = allocate(stage, 1, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    data = stage->archive->data + offset;
    for (size_t i = 0; i < 20; ++i) {
        u32 value = NativeArchiveBE32(data + i * 4);
        memcpy((u8*) result + i * 4, &value, 4);
    }
    result->x68 = read_float(data + 0x68);
    result->x70 = read_float(data + 0x70);
    for (size_t i = 0; i < 4; ++i) {
        u32 value = NativeArchiveBE32(data + 0x74 + i * 4);
        memcpy((u8*) result + offsetof(NativeCorneriaParameters, x74) + i * 4,
               &value, 4);
    }
    if (!reference(stage, offset + 0x84, &target, &present)) {
        return NULL;
    }
    if (present) {
        if (!range(stage, target, 4)) {
            return NULL;
        }
        result->x84 = (void*) (stage->archive->data + target);
    }
    result->x88 = read_float(data + 0x88);
    return result;
}

static void* stage_parameters(NativeStageArchive* stage, uint32_t offset)
{
    uint32_t ground_offset;
    GroundParam* ground = stage->roots[0];
    if (ground == NULL) {
        if (NativeArchiveFind(stage->archive, "grGroundParam", &ground_offset,
                              stage->error) != NATIVE_ARCHIVE_OK)
        {
            return NULL;
        }
        ground = ground_param(stage, ground_offset);
        if (ground == NULL) {
            return NULL;
        }
        stage->roots[0] = ground;
    }
    if (ground->stage_param_count == 0) {
        return fail(stage, offset, "stage parameters need a stage ID");
    }
    switch (ground->stage_params[0].stkind) {
    case St_Kind_Battle:
    case St_Kind_Last: {
        size_t count =
            ground->stage_params[0].stkind == St_Kind_Battle ? 2 : 4;
        void** scripts = allocate(stage, count, sizeof(*scripts));
        if (scripts == NULL) {
            return NULL;
        }
        for (size_t i = 0; i < count; ++i) {
            uint32_t target;
            bool present;
            if (!reference(stage, offset + i * 4, &target, &present)) {
                return NULL;
            }
            if (present) {
                if (!range(stage, target, 4)) {
                    return NULL;
                }
                /* ColorOverlay reads the serialized command words. */
                scripts[i] = (void*) (stage->archive->data + target);
            }
        }
        return scripts;
    }
    case St_Kind_Zebes:
        return zebes_parameters(stage, offset);
    case St_Kind_Castle:
        return castle_parameters(stage, offset);
    case St_Kind_Kongo:
        return kongo_parameters(stage, offset);
    case St_Kind_Corneria:
        return corneria_parameters(stage, offset);
    case St_Kind_Izumi:
        return scalar_array(stage, offset, 21, 4);
    case St_Kind_Story:
        return scalar_array(stage, offset, 9, 4);
    case St_Kind_PStadium: {
        u8* parameters = scalar_array(stage, offset, 21, 4);
        if (parameters == NULL) {
            return NULL;
        }
        memcpy(parameters + 0x1C, stage->archive->data + offset + 0x1C, 4);
        for (size_t i = 0x48; i < 0x52; i += 2) {
            u16 value = read16(stage->archive->data + offset + i);
            memcpy(parameters + i, &value, 2);
        }
        return parameters;
    }
    default:
        NativeArchiveFail(stage->error, NATIVE_ARCHIVE_UNSUPPORTED,
                          offset + 32,
                          "stage-specific parameters need a typed reader");
        return NULL;
    }
}

NativeStageArchive* NativeStageArchiveOpen(const NativeArchive* archive,
                                           NativeArchiveGraph* graph)
{
    NativeStageArchive* stage = calloc(1, sizeof(*stage));
    if (stage != NULL) {
        stage->archive = archive;
        stage->graph = graph;
    }
    return stage;
}

void NativeStageArchiveClose(NativeStageArchive* stage)
{
    if (stage == NULL) {
        return;
    }
    StageAllocation* allocation = stage->allocations;
    while (allocation != NULL) {
        StageAllocation* next = allocation->next;
        free(allocation->data);
        free(allocation);
        allocation = next;
    }
    free(stage);
}

NativeArchiveStatus NativeStageArchiveRead(NativeStageArchive* stage,
                                           const char* symbol, uint32_t offset,
                                           void** output,
                                           NativeArchiveError* error)
{
    NativeArchiveError local = { NATIVE_ARCHIVE_OK, 0, "ok" };
    stage->error = error == NULL ? &local : error;
    *stage->error = local;
    *output = NULL;
    const char* names[] = { "grGroundParam", "coll_data", "map_head",
                            "map_plit", "yakumono_param" };
    size_t index;
    for (index = 0; index < 5; ++index) {
        if (strcmp(symbol, names[index]) == 0) {
            break;
        }
    }
    if (index == 5) {
        return NATIVE_ARCHIVE_NOT_FOUND;
    }
    if (stage->roots[index] != NULL) {
        *output = stage->roots[index];
        return NATIVE_ARCHIVE_OK;
    }
    switch (index) {
    case 0:
        *output = ground_param(stage, offset);
        break;
    case 1:
        *output = collision(stage, offset);
        break;
    case 2:
        *output = map_head(stage, offset);
        break;
    case 3:
        *output = lights(stage, offset);
        break;
    case 4:
        *output = stage_parameters(stage, offset);
        break;
    }
    if (*output != NULL) {
        stage->roots[index] = *output;
        return NATIVE_ARCHIVE_OK;
    }
    if (stage->error->status == NATIVE_ARCHIVE_OK) {
        fail(stage, offset, "cannot convert stage root");
    }
    return stage->error->status;
}
