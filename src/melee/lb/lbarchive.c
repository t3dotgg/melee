#include "lbarchive.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "lbdvd.h"
#include "lbfile.h"
#include "lbheap.h"
#include "types.h"
#include <dolphin/os.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/sobjlib.h>

#ifdef MELEE_NATIVE
#include "../../../native/source/assets/archive_internal.h"
#include "../../../native/source/assets/effects.h"
#include "../../../native/source/assets/fighters.h"
#include "../../../native/source/assets/items.h"
#include "../../../native/source/assets/stage.h"
#include "../../../native/source/command.h"
#include "assets/events.h"
#include <sysdolphin/baselib/sislib.h>
#endif

#ifdef MELEE_NATIVE
typedef struct NativeArchiveBinding NativeArchiveBinding;
typedef struct NativeSisRoot NativeSisRoot;
typedef struct NativeSceneAllocation NativeSceneAllocation;
static void* native_scene_alloc(NativeArchiveBinding* binding, size_t size);
static bool native_scene_reference(NativeArchiveBinding* binding,
                                   uint32_t field, uint32_t* target,
                                   bool* present, NativeArchiveError* error);

struct NativeSisRoot {
    SIS* table;
    /* Number of serialized pointer words. Host SIS records pack two words,
     * but callers index the original word sequence. */
    size_t count;
    NativeSisRoot* next;
};

struct NativeArchiveBinding {
    HSD_Archive* legacy;
    void* input;
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeStageArchive* stage;
    NativeItemArchive* items;
    NativeFighterArchive* fighters;
    NativeEffectArchive* effects;
    NativeEventArchive* events;
    NativeSisRoot* sis_roots;
    NativeSceneAllocation* scene_allocations;
    NativeArchiveBinding* next;
};

struct NativeSceneAllocation {
    void* pointer;
    NativeSceneAllocation* next;
};

struct NativeRefractionData {
    u8 count;
    u8 padding[7];
    f32* values;
};

struct NativeAudioLoadData {
    int** x0;
    int** x4;
    int** x8;
    int** xC;
};

/* MnSelectStageDataTable starts with the camera and lighting descriptors,
 * followed by twelve static stage-preview models. The matching C struct
 * stores the final model as four fields at xB0, so keep the serialized table
 * contiguous here and let the caller's existing field offsets apply. */
typedef struct NativeStageSelectData {
    HSD_CObjDesc* camera;
    HSD_LightDesc* light1;
    HSD_LightDesc* light2;
    HSD_FogDesc* fog;
    StaticModelDesc models[12];
} NativeStageSelectData;

static bool native_stage_select_model(NativeArchiveBinding* binding,
                                      uint32_t offset, StaticModelDesc* model,
                                      NativeArchiveError* error)
{
    uint32_t target;
    bool present;
    if (!NativeArchiveDataRange(binding->archive, offset, 0x10)) {
        NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, offset,
                          "stage select model is truncated");
        return false;
    }
    if (!native_scene_reference(binding, offset, &target, &present, error)) {
        return false;
    }
    model->joint = NULL;
    if (present && NativeArchiveJoint(binding->graph, target, &model->joint,
                                      error) != NATIVE_ARCHIVE_OK) {
        return false;
    }
    if (!native_scene_reference(binding, offset + 4, &target, &present,
                                error)) {
        return false;
    }
    model->animjoint = NULL;
    if (present && NativeArchiveAnimation(binding->graph, target,
                                          &model->animjoint,
                                          error) != NATIVE_ARCHIVE_OK) {
        return false;
    }
    if (!native_scene_reference(binding, offset + 8, &target, &present,
                                error)) {
        return false;
    }
    model->matanim_joint = NULL;
    if (present && NativeArchiveMatAnimJoint(binding->graph, target,
                                             &model->matanim_joint,
                                             error) != NATIVE_ARCHIVE_OK) {
        return false;
    }
    if (!native_scene_reference(binding, offset + 12, &target, &present,
                                error)) {
        return false;
    }
    model->shapeanim_joint = NULL;
    if (present && NativeArchiveShapeAnimJoint(binding->graph, target,
                                               &model->shapeanim_joint,
                                               error) != NATIVE_ARCHIVE_OK) {
        return false;
    }
    return true;
}

static NativeStageSelectData*
native_stage_select_root(NativeArchiveBinding* binding, uint32_t offset,
                          NativeArchiveError* error)
{
    NativeStageSelectData* root;
    uint32_t target;
    bool present;
    if (!NativeArchiveDataRange(binding->archive, offset, 0xD0)) {
        NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, offset,
                          "stage select table is truncated");
        return NULL;
    }
    root = native_scene_alloc(binding, sizeof(*root));
    if (root == NULL) {
        return NULL;
    }
    if (!native_scene_reference(binding, offset, &target, &present, error)) {
        return NULL;
    }
    if (present && NativeArchiveCObj(binding->graph, target, &root->camera,
                                     error) != NATIVE_ARCHIVE_OK) {
        return NULL;
    }
    if (!native_scene_reference(binding, offset + 4, &target, &present,
                                error)) {
        return NULL;
    }
    if (present && NativeArchiveLight(binding->graph, target, &root->light1,
                                      error) != NATIVE_ARCHIVE_OK) {
        return NULL;
    }
    if (!native_scene_reference(binding, offset + 8, &target, &present,
                                error)) {
        return NULL;
    }
    if (present && NativeArchiveLight(binding->graph, target, &root->light2,
                                      error) != NATIVE_ARCHIVE_OK) {
        return NULL;
    }
    if (!native_scene_reference(binding, offset + 12, &target, &present,
                                error)) {
        return NULL;
    }
    if (present && NativeArchiveFog(binding->graph, target, &root->fog,
                                    error) != NATIVE_ARCHIVE_OK) {
        return NULL;
    }
    for (size_t i = 0; i < 12; ++i) {
        if (!native_stage_select_model(binding, offset + 0x10 + i * 0x10,
                                       &root->models[i], error)) {
            return NULL;
        }
    }
    return root;
}

static struct NativeAudioLoadData*
native_audio_load_data(NativeArchiveBinding* binding, uint32_t offset,
                       NativeArchiveError* error)
{
    struct NativeAudioLoadData* root =
        native_scene_alloc(binding, sizeof(*root));
    if (root == NULL || !NativeArchiveDataRange(binding->archive, offset, 16))
    {
        return NULL;
    }
    for (size_t group = 0; group < 4; ++group) {
        uint32_t table_offset = 0;
        bool table_present = false;
        int** values;
        if (NativeArchiveReference(binding->archive, offset + group * 4,
                                   &table_offset, &table_present,
                                   error) != NATIVE_ARCHIVE_OK ||
            !table_present ||
            !NativeArchiveDataRange(binding->archive, table_offset, 30 * 4))
        {
            return NULL;
        }
        values = native_scene_alloc(binding, 30 * sizeof(*values));
        if (values == NULL) {
            return NULL;
        }
        for (size_t entry = 0; entry < 30; ++entry) {
            uint32_t list_offset = 0;
            bool list_present = false;
            size_t count = 0;
            if (NativeArchiveReference(
                    binding->archive, table_offset + entry * 4, &list_offset,
                    &list_present, error) != NATIVE_ARCHIVE_OK ||
                !list_present ||
                !NativeArchiveDataRange(binding->archive, list_offset, 4))
            {
                return NULL;
            }
            while (count < 0x1000 &&
                   NativeArchiveDataRange(binding->archive,
                                          list_offset + count * 4, 4) &&
                   NativeArchiveBE32(binding->archive->data + list_offset +
                                     count * 4) != 0x83D60)
            {
                ++count;
            }
            if (count == 0x1000 ||
                !NativeArchiveDataRange(binding->archive, list_offset,
                                        (count + 1) * 4))
            {
                return NULL;
            }
            values[entry] = native_scene_alloc(binding, (count + 1) * 4);
            if (values[entry] == NULL) {
                return NULL;
            }
            for (size_t i = 0; i <= count; ++i) {
                values[entry][i] = (int) NativeArchiveBE32(
                    binding->archive->data + list_offset + i * 4);
            }
        }
        ((int***) root)[group] = values;
    }
    return root;
}

static NativeArchiveBinding* native_archive_bindings;

static void* native_scene_alloc(NativeArchiveBinding* binding, size_t size)
{
    NativeSceneAllocation* allocation;
    void* pointer = calloc(1, size);
    if (pointer == NULL) {
        return NULL;
    }
    allocation = calloc(1, sizeof(*allocation));
    if (allocation == NULL) {
        free(pointer);
        return NULL;
    }
    allocation->pointer = pointer;
    allocation->next = binding->scene_allocations;
    binding->scene_allocations = allocation;
    return pointer;
}

static NativeArchiveBinding* native_binding(HSD_Archive* archive)
{
    NativeArchiveBinding* binding;
    for (binding = native_archive_bindings; binding != NULL;
         binding = binding->next)
    {
        if (binding->legacy == archive) {
            return binding;
        }
    }
    return NULL;
}

/* Resolve a relocated branch word without putting an eight-byte pointer in
 * the four-byte command stream. Scripts keep their original byte order. */
void* native_archive_command_target(const void* command_word)
{
    NativeArchiveBinding* binding;
    for (binding = native_archive_bindings; binding != NULL;
         binding = binding->next)
    {
        uintptr_t start = (uintptr_t) binding->archive->data;
        uintptr_t address = (uintptr_t) command_word;
        size_t size = NativeArchiveDataSize(binding->archive);
        if (address >= start && address - start < size) {
            uint32_t target;
            bool present;
            NativeArchiveError error;
            if (NativeArchiveReference(binding->archive, address - start,
                                       &target, &present,
                                       &error) != NATIVE_ARCHIVE_OK ||
                !present ||
                !NativeArchiveDataRange(binding->archive, target, 4))
            {
                OSPanic(__FILE__, __LINE__,
                        "invalid native archive command target\n");
                return NULL;
            }
            return (void*) (binding->archive->data + target);
        }
    }
    OSPanic(__FILE__, __LINE__,
            "command branch is outside a native archive\n");
    return NULL;
}

static bool native_name_ends_with(const char* name, const char* suffix)
{
    size_t name_len;
    size_t suffix_len;
    if (name == NULL || suffix == NULL) {
        return false;
    }
    name_len = strlen(name);
    suffix_len = strlen(suffix);
    return name_len >= suffix_len &&
           strcmp(name + name_len - suffix_len, suffix) == 0;
}

static void native_archive_error(const char* operation,
                                 const NativeArchiveError* error)
{
    OSReport("native archive %s failed at %zu: %s\n", operation,
             error == NULL ? 0 : error->offset,
             error == NULL || error->message == NULL ? "unknown error"
                                                     : error->message);
}

/* SIS roots are contiguous four-byte pointer words. The first two words
 * hold kerning and glyph texture metadata. The remaining words point at
 * encoded text streams. Pair the words into host SIS records while keeping
 * the original byte ranges immutable. */
static SIS* native_sis_root(NativeArchiveBinding* binding, uint32_t offset,
                            NativeArchiveError* error)
{
    NativeSisRoot* root;
    SIS* table;
    size_t word_count = 0;
    size_t record_count;
    size_t i;
    bool saw_entry = false;
    const size_t data_size = NativeArchiveDataSize(binding->archive);
    const size_t max_words =
        offset <= data_size ? (data_size - offset) / 4u : 0;

    /* A SIS record has two serialized pointer words.  Require both words to
     * be relocated before adding the record.  Accepting a trailing single
     * relocation can consume the first field of the next archive object and
     * produce a host table whose indexing no longer matches the file. */
    for (i = 0; i + 1 < max_words; i += 2) {
        uint32_t targets[2] = { 0, 0 };
        bool present[2] = { false, false };
        size_t field = (size_t) offset + i * 4u;
        NativeArchiveStatus status0;
        NativeArchiveStatus status1;
        status0 = NativeArchiveReference(binding->archive, (uint32_t) field,
                                         &targets[0], &present[0], error);
        status1 =
            NativeArchiveReference(binding->archive, (uint32_t) (field + 4u),
                                   &targets[1], &present[1], error);
        if (status0 != NATIVE_ARCHIVE_OK || status1 != NATIVE_ARCHIVE_OK) {
            if (!saw_entry) {
                return NULL;
            }
            break;
        }
        if (!present[0] || !present[1]) {
            if (saw_entry) {
                break;
            }
            return NULL;
        }
        /* A target equal to data_size is the archive's end sentinel. Keep
         * the table slot, but expose it as NULL below. */
        if (targets[0] > data_size || targets[1] > data_size) {
            return NULL;
        }
        saw_entry = true;
        word_count = i + 2;
    }
    if (word_count == 0 || word_count > SIZE_MAX / 4u) {
        return NULL;
    }
    record_count = (word_count + 1u) / 2u;
    if (record_count > SIZE_MAX / sizeof(*table)) {
        return NULL;
    }
    table = calloc(record_count, sizeof(*table));
    if (table == NULL) {
        if (error != NULL) {
            error->status = NATIVE_ARCHIVE_NO_MEMORY;
            error->offset = offset;
            error->message = "cannot allocate SIS root";
        }
        return NULL;
    }
    for (i = 0; i < word_count; ++i) {
        uint32_t target = 0;
        bool present = false;
        if (NativeArchiveReference(binding->archive,
                                   offset + (uint32_t) (i * 4u), &target,
                                   &present, error) != NATIVE_ARCHIVE_OK ||
            !present)
        {
            free(table);
            return NULL;
        }
        if ((i & 1u) == 0) {
            table[i / 2u].kerning =
                target < NativeArchiveDataSize(binding->archive)
                    ? (TextKerning*) (binding->archive->data + target)
                    : NULL;
        } else {
            table[i / 2u].textures =
                target < NativeArchiveDataSize(binding->archive)
                    ? (TextGlyphTexture*) (binding->archive->data + target)
                    : NULL;
        }
    }
    root = calloc(1, sizeof(*root));
    if (root == NULL) {
        free(table);
        return NULL;
    }
    root->table = table;
    root->count = word_count;
    root->next = binding->sis_roots;
    binding->sis_roots = root;
    return table;
}

/* Return the number of valid serialized SIS pointer words for a converted
 * root. This lets native callers reject an out-of-range text index before
 * doing the host record/parity mapping. */
size_t HSD_ArchiveNativeSisCount(const void* table)
{
    NativeArchiveBinding* binding;
    for (binding = native_archive_bindings; binding != NULL;
         binding = binding->next)
    {
        NativeSisRoot* root;
        for (root = binding->sis_roots; root != NULL; root = root->next) {
            if (root->table == table) {
                return root->count;
            }
        }
    }
    return 0;
}

static struct Fighter_804D653C_t*
native_rumble_root(NativeArchiveBinding* binding, uint32_t offset,
                   NativeArchiveError* error)
{
    size_t count = 0;
    size_t i;
    const size_t max_entries =
        (NativeArchiveDataSize(binding->archive) - offset) / 8u;
    struct Fighter_804D653C_t* table;
    for (i = 0; i < max_entries; ++i) {
        uint32_t target = 0;
        bool present = false;
        NativeArchiveStatus status = NativeArchiveReference(
            binding->archive, offset + (uint32_t) (i * 8u), &target, &present,
            error);
        if (status != NATIVE_ARCHIVE_OK || !present) {
            break;
        }
        if (!NativeArchiveDataRange(binding->archive, target, 1)) {
            return NULL;
        }
        count = i + 1;
    }
    if (count == 0) {
        return NULL;
    }
    table = calloc(count, sizeof(*table));
    if (table == NULL) {
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        uint32_t target = 0;
        bool present = false;
        const uint8_t* bytes = binding->archive->data + offset + i * 8u;
        if (NativeArchiveReference(binding->archive,
                                   offset + (uint32_t) (i * 8u), &target,
                                   &present, error) != NATIVE_ARCHIVE_OK)
        {
            free(table);
            return NULL;
        }
        table[i].unk =
            present ? (void*) (binding->archive->data + target) : NULL;
        table[i].unk4 = bytes[4];
        table[i].unk5 = bytes[5];
    }
    return table;
}

static bool native_scene_reference(NativeArchiveBinding* binding,
                                   uint32_t field, uint32_t* target,
                                   bool* present, NativeArchiveError* error)
{
    return NativeArchiveReference(binding->archive, field, target, present,
                                  error) == NATIVE_ARCHIVE_OK;
}

static DynamicModelDesc* native_scene_model(NativeArchiveBinding* binding,
                                            uint32_t offset,
                                            NativeArchiveError* error)
{
    DynamicModelDesc* model = native_scene_alloc(binding, sizeof(*model));
    uint32_t target;
    bool present;
    if (model == NULL) {
        return NULL;
    }
    if (!native_scene_reference(binding, offset, &target, &present, error)) {
        return NULL;
    }
    if (present && NativeArchiveJoint(binding->graph, target, &model->joint,
                                      error) != NATIVE_ARCHIVE_OK)
    {
        return NULL;
    }
    /* Each animation channel is a terminated array of typed roots. */
    for (size_t channel = 0; channel < 3; ++channel) {
        size_t count = 0;
        void** animations;
        if (!native_scene_reference(binding, offset + 4 + channel * 4, &target,
                                    &present, error))
        {
            return NULL;
        }
        if (!present) {
            continue;
        }
        size_t limit = (NativeArchiveDataSize(binding->archive) - target) / 4;
        for (; count < limit; ++count) {
            uint32_t animation_offset;
            bool animation_present;
            if (!native_scene_reference(binding, target + count * 4,
                                        &animation_offset, &animation_present,
                                        error))
            {
                return NULL;
            }
            if (!animation_present) {
                break;
            }
        }
        if (count == limit) {
            NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, target,
                              "unterminated scene animation list");
            return NULL;
        }
        animations =
            native_scene_alloc(binding, (count + 1) * sizeof(*animations));
        if (animations == NULL) {
            return NULL;
        }
        for (size_t i = 0; i < count; ++i) {
            uint32_t animation_offset;
            bool animation_present;
            NativeArchiveStatus status;
            if (!native_scene_reference(binding, target + i * 4,
                                        &animation_offset, &animation_present,
                                        error))
            {
                return NULL;
            }
            switch (channel) {
            case 0:
                status = NativeArchiveAnimation(
                    binding->graph, animation_offset,
                    (HSD_AnimJoint**) &animations[i], error);
                break;
            case 1:
                status = NativeArchiveMatAnimJoint(
                    binding->graph, animation_offset,
                    (HSD_MatAnimJoint**) &animations[i], error);
                break;
            default:
                status = NativeArchiveShapeAnimJoint(
                    binding->graph, animation_offset,
                    (HSD_ShapeAnimJoint**) &animations[i], error);
                break;
            }
            if (status != NATIVE_ARCHIVE_OK) {
                return NULL;
            }
        }
        if (channel == 0) {
            model->anims = (HSD_AnimJoint**) animations;
        } else if (channel == 1) {
            model->matanims = (HSD_MatAnimJoint**) animations;
        } else {
            model->shapeanims = (HSD_ShapeAnimJoint**) animations;
        }
    }
    return model;
}

/* Public model tables can end at the next archive object without a null
 * entry. Relocation targets and public roots mark those object boundaries. */
static size_t native_scene_pointer_limit(NativeArchiveBinding* binding,
                                         uint32_t offset)
{
    NativeArchive* archive = binding->archive;
    uint32_t end = archive->data_size;
    if (offset >= end) {
        return 0;
    }
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
    return (end - offset) / 4;
}

static DynamicModelDesc** native_scene_models(NativeArchiveBinding* binding,
                                              uint32_t offset,
                                              size_t fixed_count,
                                              size_t* count,
                                              NativeArchiveError* error)
{
    size_t i;
    DynamicModelDesc** models;
    *count = 0;
    size_t limit = fixed_count != 0
                       ? fixed_count
                       : native_scene_pointer_limit(binding, offset);
    if (fixed_count != 0) {
        *count = limit;
    } else {
        for (i = 0; i < limit; ++i) {
            uint32_t target;
            bool present;
            if (!native_scene_reference(binding, offset + (uint32_t) (i * 4u),
                                        &target, &present, error))
            {
                return NULL;
            }
            if (!present) {
                break;
            }
            ++*count;
        }
    }
    if (*count == 0) {
        return NULL;
    }
    models = native_scene_alloc(binding, (*count + 1) * sizeof(*models));
    if (models == NULL) {
        return NULL;
    }
    for (i = 0; i < *count; ++i) {
        uint32_t target;
        bool present;
        if (!native_scene_reference(binding, offset + (uint32_t) (i * 4u),
                                    &target, &present, error) ||
            !present)
        {
            return NULL;
        }
        models[i] = native_scene_model(binding, target, error);
        if (models[i] == NULL) {
            return NULL;
        }
    }
    return models;
}

static struct SceneCameraDesc*
native_scene_cameras(NativeArchiveBinding* binding, uint32_t offset,
                     NativeArchiveError* error)
{
    /* The scene camera field points to one eight-byte record. */
    struct SceneCameraDesc* camera =
        native_scene_alloc(binding, sizeof(*camera));
    uint32_t target;
    bool present;
    if (camera == NULL ||
        !native_scene_reference(binding, offset, &target, &present, error) ||
        !present ||
        NativeArchiveCObj(binding->graph, target, &camera->desc, error) !=
            NATIVE_ARCHIVE_OK)
    {
        return NULL;
    }
    if (!native_scene_reference(binding, offset + 4, &target, &present, error))
    {
        return NULL;
    }
    if (present) {
        size_t count = 0;
        size_t limit = native_scene_pointer_limit(binding, target);
        for (; count < limit; ++count) {
            uint32_t animation_offset;
            bool animation_present;
            if (!native_scene_reference(binding, target + count * 4,
                                        &animation_offset, &animation_present,
                                        error))
            {
                return NULL;
            }
            if (!animation_present) {
                break;
            }
        }
        camera->anims =
            native_scene_alloc(binding, (count + 1) * sizeof(*camera->anims));
        if (camera->anims == NULL) {
            return NULL;
        }
        for (size_t i = 0; i < count; ++i) {
            uint32_t animation_offset;
            bool animation_present;
            if (!native_scene_reference(binding, target + i * 4,
                                        &animation_offset, &animation_present,
                                        error) ||
                !animation_present ||
                NativeArchiveCameraAnimation(binding->graph, animation_offset,
                                             &camera->anims[i],
                                             error) != NATIVE_ARCHIVE_OK)
            {
                return NULL;
            }
        }
    }
    return camera;
}

static struct SceneFogDesc* native_scene_fog(NativeArchiveBinding* binding,
                                             uint32_t offset,
                                             NativeArchiveError* error)
{
    struct SceneFogDesc* fog = native_scene_alloc(binding, sizeof(*fog));
    uint32_t target;
    bool present;
    if (fog == NULL ||
        !native_scene_reference(binding, offset, &target, &present, error) ||
        !present ||
        NativeArchiveFog(binding->graph, target, &fog->desc, error) !=
            NATIVE_ARCHIVE_OK)
    {
        return NULL;
    }
    if (!native_scene_reference(binding, offset + 4, &target, &present, error))
    {
        return NULL;
    }
    if (!present) {
        return fog;
    }
    size_t count = 0;
    size_t limit = native_scene_pointer_limit(binding, target);
    for (; count < limit; ++count) {
        uint32_t animation_offset;
        bool animation_present;
        if (!native_scene_reference(binding, target + count * 4,
                                    &animation_offset, &animation_present,
                                    error))
        {
            return NULL;
        }
        if (!animation_present) {
            break;
        }
    }
    fog->anims =
        native_scene_alloc(binding, (count + 1) * sizeof(*fog->anims));
    if (fog->anims == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        uint32_t animation_offset, aobj_offset;
        bool animation_present;
        if (!native_scene_reference(binding, target + i * 4, &animation_offset,
                                    &animation_present, error) ||
            !animation_present ||
            !native_scene_reference(binding, animation_offset, &aobj_offset,
                                    &present, error))
        {
            return NULL;
        }
        /* SceneDesc exposes this wrapper through HSD_CameraAnim. The fog
         * caller reads aobjdesc. Its serialized wrapper occupies two words. */
        fog->anims[i] = native_scene_alloc(binding, sizeof(*fog->anims[i]));
        if (fog->anims[i] == NULL) {
            return NULL;
        }
        if (present && NativeArchiveAObj(binding->graph, aobj_offset,
                                         &fog->anims[i]->aobjdesc,
                                         error) != NATIVE_ARCHIVE_OK)
        {
            return NULL;
        }
        if (!native_scene_reference(binding, animation_offset + 4,
                                    &aobj_offset, &present, error))
        {
            return NULL;
        }
        if (present) {
            NativeArchiveFail(error, NATIVE_ARCHIVE_UNSUPPORTED,
                              animation_offset + 4 + 32,
                              "fog adjustment animation needs a typed reader");
            return NULL;
        }
    }
    return fog;
}

static HSD_LightAnim** native_scene_light_anims(NativeArchiveBinding* binding,
                                                uint32_t offset,
                                                NativeArchiveError* error)
{
    size_t count = 0;
    size_t i;
    HSD_LightAnim** animations;
    for (i = 0; i < 256; ++i) {
        uint32_t target;
        bool present;
        if (!native_scene_reference(binding, offset + (uint32_t) (i * 4u),
                                    &target, &present, error))
        {
            return NULL;
        }
        if (!present) {
            break;
        }
        ++count;
    }
    if (count == 0) {
        return NULL;
    }
    animations =
        native_scene_alloc(binding, (count + 1) * sizeof(*animations));
    if (animations == NULL) {
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        uint32_t target;
        bool present;
        if (!native_scene_reference(binding, offset + (uint32_t) (i * 4u),
                                    &target, &present, error) ||
            !present ||
            NativeArchiveLightAnimation(binding->graph, target, &animations[i],
                                        error) != NATIVE_ARCHIVE_OK)
        {
            return NULL;
        }
    }
    return animations;
}

static LightList* native_scene_light(NativeArchiveBinding* binding,
                                     uint32_t offset,
                                     NativeArchiveError* error)
{
    LightList* light = native_scene_alloc(binding, sizeof(*light));
    uint32_t target;
    bool present;
    if (light == NULL ||
        !native_scene_reference(binding, offset, &target, &present, error))
    {
        return NULL;
    }
    if (present && NativeArchiveLight(binding->graph, target, &light->desc,
                                      error) != NATIVE_ARCHIVE_OK)
    {
        return NULL;
    }
    if (!native_scene_reference(binding, offset + 4, &target, &present, error))
    {
        return NULL;
    }
    if (present) {
        light->anims = native_scene_light_anims(binding, target, error);
        if (light->anims == NULL) {
            return NULL;
        }
    }
    return light;
}

static LightList** native_scene_lights(NativeArchiveBinding* binding,
                                       uint32_t offset,
                                       NativeArchiveError* error)
{
    size_t count = 0;
    size_t i;
    LightList** lights;
    for (i = 0; i < 256; ++i) {
        uint32_t target;
        bool present;
        if (!native_scene_reference(binding, offset + (uint32_t) (i * 4u),
                                    &target, &present, error))
        {
            return NULL;
        }
        if (!present) {
            break;
        }
        ++count;
    }
    if (count == 0) {
        return NULL;
    }
    lights = native_scene_alloc(binding, (count + 1) * sizeof(*lights));
    if (lights == NULL) {
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        uint32_t target;
        bool present;
        if (!native_scene_reference(binding, offset + (uint32_t) (i * 4u),
                                    &target, &present, error) ||
            !present)
        {
            return NULL;
        }
        lights[i] = native_scene_light(binding, target, error);
        if (lights[i] == NULL) {
            return NULL;
        }
    }
    return lights;
}

static SceneDesc* native_scene_root(NativeArchiveBinding* binding,
                                    uint32_t offset, NativeArchiveError* error)
{
    SceneDesc* scene = native_scene_alloc(binding, sizeof(*scene));
    uint32_t models_offset, cameras_offset, lights_offset, fog_offset;
    bool models_present, cameras_present, lights_present, fog_present;
    size_t model_count;
    if (scene == NULL ||
        !native_scene_reference(binding, offset, &models_offset,
                                &models_present, error) ||
        !native_scene_reference(binding, offset + 4, &cameras_offset,
                                &cameras_present, error))
    {
        return NULL;
    }
    if (models_present) {
        scene->models = native_scene_models(binding, models_offset, 0,
                                            &model_count, error);
        if (scene->models == NULL) {
            return NULL;
        }
    }
    if (cameras_present) {
        scene->cameras = native_scene_cameras(binding, cameras_offset, error);
        if (scene->cameras == NULL) {
            return NULL;
        }
    }
    if (!native_scene_reference(binding, offset + 8, &lights_offset,
                                &lights_present, error))
    {
        return NULL;
    }
    if (lights_present) {
        scene->lights = native_scene_lights(binding, lights_offset, error);
        if (scene->lights == NULL) {
            return NULL;
        }
    }
    if (!native_scene_reference(binding, offset + 12, &fog_offset,
                                &fog_present, error))
    {
        return NULL;
    }
    if (fog_present) {
        scene->fogs = native_scene_fog(binding, fog_offset, error);
        if (scene->fogs == NULL) {
            return NULL;
        }
    }
    return scene;
}

static void* native_css_root(NativeArchiveBinding* binding, uint32_t offset,
                             NativeArchiveError* error)
{
    void** root;
    /* The table has four scene descriptors and nine animation sets. */
    if (!NativeArchiveDataRange(binding->archive, offset, 0xA0)) {
        NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 32u + offset,
                          "character select table is truncated");
        return NULL;
    }
    root = native_scene_alloc(binding, 40 * sizeof(*root));
    if (root == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < 40; ++i) {
        uint32_t target;
        bool present;
        NativeArchiveStatus status = NativeArchiveReference(
            binding->archive, offset + (uint32_t) (i * 4), &target, &present,
            error);
        if (status != NATIVE_ARCHIVE_OK) {
            return NULL;
        }
        if (!present) {
            root[i] = NULL;
            continue;
        }
        switch (i < 4 ? i : 4 + ((i - 4) % 4)) {
        case 0:
            status = NativeArchiveCObj(binding->graph, target,
                                       (HSD_CObjDesc**) &root[i], error);
            break;
        case 1:
        case 2:
            status = NativeArchiveLight(binding->graph, target,
                                        (HSD_LightDesc**) &root[i], error);
            break;
        case 3:
            status = NativeArchiveFog(binding->graph, target,
                                      (HSD_FogDesc**) &root[i], error);
            break;
        case 4:
            status = NativeArchiveJoint(binding->graph, target,
                                        (HSD_Joint**) &root[i], error);
            break;
        case 5:
            status = NativeArchiveAnimation(binding->graph, target,
                                            (HSD_AnimJoint**) &root[i], error);
            break;
        case 6:
            status = NativeArchiveMatAnimJoint(
                binding->graph, target, (HSD_MatAnimJoint**) &root[i], error);
            break;
        default:
            status = NativeArchiveShapeAnimJoint(
                binding->graph, target, (HSD_ShapeAnimJoint**) &root[i],
                error);
            break;
        }
        if (status != NATIVE_ARCHIVE_OK) {
            return NULL;
        }
    }
    return root;
}

void* HSD_ArchiveNativePublicAddress(HSD_Archive* archive, const char* symbol)
{
    NativeArchiveBinding* binding = native_binding(archive);
    NativeArchiveError error = { NATIVE_ARCHIVE_OK, 0, "ok" };
    uint32_t offset;
    void* root = NULL;

    if (binding == NULL || symbol == NULL ||
        NativeArchiveFind(binding->archive, symbol, &offset, &error) !=
            NATIVE_ARCHIVE_OK)
    {
        return NULL;
    }
    if (strcmp(symbol, "MnSelectChrDataTable") == 0) {
        root = native_css_root(binding, offset, &error);
        if (root != NULL) {
            return root;
        }
        native_archive_error(symbol, &error);
        return NULL;
    }
    if (strcmp(symbol, "lbAudioLoadData") == 0) {
        root = native_audio_load_data(binding, offset, &error);
        if (root != NULL) {
            return root;
        }
        native_archive_error(symbol, &error);
        return NULL;
    }
    NativeArchiveStatus stage_status =
        NativeStageArchiveRead(binding->stage, symbol, offset, &root, &error);
    if (stage_status == NATIVE_ARCHIVE_OK) {
        return root;
    }
    if (stage_status != NATIVE_ARCHIVE_NOT_FOUND) {
        native_archive_error(symbol, &error);
        return NULL;
    }
    NativeArchiveStatus fighter_status = NativeFighterArchiveRead(
        binding->fighters, symbol, offset, &root, &error);
    if (fighter_status == NATIVE_ARCHIVE_OK) {
        return root;
    }
    if (fighter_status != NATIVE_ARCHIVE_NOT_FOUND) {
        native_archive_error(symbol, &error);
        return NULL;
    }
    NativeArchiveStatus item_status =
        NativeItemArchiveRead(binding->items, symbol, offset, &root, &error);
    if (item_status == NATIVE_ARCHIVE_OK) {
        return root;
    }
    if (item_status != NATIVE_ARCHIVE_NOT_FOUND) {
        native_archive_error(symbol, &error);
        return NULL;
    }
    NativeArchiveStatus effect_status = NativeEffectArchiveRead(
        binding->effects, symbol, offset, &root, &error);
    if (effect_status == NATIVE_ARCHIVE_OK) {
        return root;
    }
    if (effect_status != NATIVE_ARCHIVE_NOT_FOUND) {
        native_archive_error(symbol, &error);
        return NULL;
    }
    NativeArchiveStatus event_status =
        NativeEventArchiveRead(binding->events, symbol, offset, &root, &error);
    if (event_status == NATIVE_ARCHIVE_OK) {
        return root;
    }
    if (event_status != NATIVE_ARCHIVE_NOT_FOUND) {
        native_archive_error(symbol, &error);
        return NULL;
    }
    if (strcmp(symbol, "lbAudioLoadData") == 0) {
        root = native_audio_load_data(binding, offset, &error);
        if (root != NULL) {
            return root;
        }
        native_archive_error(symbol, &error);
        return NULL;
    }
    if (strcmp(symbol, "map_ptcl") == 0 || strcmp(symbol, "map_texg") == 0) {
        /* The particle loader decodes these bank-relative byte streams. */
        if (NativeArchiveDataRange(binding->archive, offset, 12)) {
            return (void*) (binding->archive->data + offset);
        }
        return NULL;
    }
    if (strcmp(symbol, "quake_model_set") == 0) {
        return native_scene_model(binding, offset, &error);
    }
    if (strncmp(symbol, "SIS_", 4) == 0) {
        return native_sis_root(binding, offset, &error);
    }
    if (strcmp(symbol, "lbRumbleData") == 0) {
        return native_rumble_root(binding, offset, &error);
    }
    if (strcmp(symbol, "MnSelectStageDataTable") == 0) {
        root = native_stage_select_root(binding, offset, &error);
        if (root != NULL) {
            return root;
        }
        native_archive_error(symbol, &error);
        return NULL;
    }
    if (strcmp(symbol, "lbBgFlashColAnimData") == 0) {
        /* LbBf stores an eight-byte header before its animation records. */
        return native_rumble_root(binding, offset + 8, &error);
    }
    if (strcmp(symbol, "MemCardIconData") == 0 ||
        strcmp(symbol, "MemSnapIconData") == 0)
    {
        size_t count = strcmp(symbol, "MemCardIconData") == 0 ? 4 : 2;
        void** images = native_scene_alloc(binding, count * sizeof(*images));
        if (images == NULL) {
            return NULL;
        }
        for (size_t i = 0; i < count; ++i) {
            uint32_t target;
            bool present;
            if (!native_scene_reference(binding, offset + i * 4, &target,
                                        &present, &error))
            {
                return NULL;
            }
            if (present) {
                if (!NativeArchiveDataRange(binding->archive, target, 1)) {
                    return NULL;
                }
                images[i] = (void*) (binding->archive->data + target);
            }
        }
        return images;
    }
    if (strcmp(symbol, "lbRefData") == 0) {
        struct NativeRefractionData* data;
        uint32_t values_offset;
        bool values_present;
        size_t count;
        if (!NativeArchiveDataRange(binding->archive, offset, 8)) {
            return NULL;
        }
        count = binding->archive->data[offset];
        if (NativeArchiveReference(binding->archive, offset + 4,
                                   &values_offset, &values_present,
                                   &error) != NATIVE_ARCHIVE_OK ||
            (count != 0 && (!values_present ||
                            !NativeArchiveDataRange(
                                binding->archive, values_offset, count * 8))))
        {
            return NULL;
        }
        data = native_scene_alloc(binding, sizeof(*data));
        if (data == NULL) {
            return NULL;
        }
        data->count = (u8) count;
        data->values =
            count == 0 ? NULL
                       : native_scene_alloc(binding,
                                            count * 2 * sizeof(*data->values));
        if (count != 0 && data->values == NULL) {
            return NULL;
        }
        for (size_t i = 0; i < count * 2; ++i) {
            uint32_t bits =
                ((uint32_t) binding->archive->data[values_offset + i * 4]
                 << 24) |
                ((uint32_t) binding->archive->data[values_offset + i * 4 + 1]
                 << 16) |
                ((uint32_t) binding->archive->data[values_offset + i * 4 + 2]
                 << 8) |
                binding->archive->data[values_offset + i * 4 + 3];
            memcpy(&data->values[i], &bits, sizeof(bits));
        }
        return data;
    }
    if (strcmp(symbol, "plLoadCommonData") == 0) {
        /* PdPm.dat exports a pointer field. Its target is the first 0x188
         * bytes of the archive, which are big-endian scalar values. */
        uint32_t target;
        bool present;
        void** slot;
        uint8_t* converted;
        if (NativeArchiveReference(binding->archive, offset, &target, &present,
                                   &error) != NATIVE_ARCHIVE_OK ||
            !present ||
            !NativeArchiveDataRange(binding->archive, target, 0x188))
        {
            return NULL;
        }
        slot = native_scene_alloc(binding, sizeof(*slot));
        converted = native_scene_alloc(binding, 0x188);
        if (slot == NULL || converted == NULL) {
            return NULL;
        }
        for (size_t i = 0; i < 0x188; i += 4) {
            uint32_t bits =
                ((uint32_t) binding->archive->data[target + i] << 24) |
                ((uint32_t) binding->archive->data[target + i + 1] << 16) |
                ((uint32_t) binding->archive->data[target + i + 2] << 8) |
                binding->archive->data[target + i + 3];
            memcpy(converted + i, &bits, sizeof(bits));
        }
        *slot = converted;
        return slot;
    }
    if (strcmp(symbol, "ScInfCnt_scene_models") == 0 ||
        strcmp(symbol, "Stc_scemdls") == 0 ||
        strcmp(symbol, "Stc_rarwmdls") == 0 || strcmp(symbol, "lupe") == 0 ||
        strcmp(symbol, "tdsce") == 0 ||
        native_name_ends_with(symbol, "_scene_modelset") ||
        native_name_ends_with(symbol, "_scene_models"))
    {
        size_t count;
        root = native_scene_models(
            binding, offset,
            strcmp(symbol, "ScInfCnt_scene_models") == 0 ? 8 : 0, &count,
            &error);
        if (root != NULL) {
            return root;
        }
        native_archive_error(symbol, &error);
        return NULL;
    }
    if (native_name_ends_with(symbol, "_scene_data")) {
        root = native_scene_root(binding, offset, &error);
        if (root != NULL) {
            return root;
        }
    }
    if (native_name_ends_with(symbol, "_scene_lights")) {
        root = native_scene_lights(binding, offset, &error);
        if (root != NULL) {
            return root;
        }
    }
    if (native_name_ends_with(symbol, "_animjoint") ||
        native_name_ends_with(symbol, "_animation"))
    {
        if (NativeArchiveAnimation(binding->graph, offset,
                                   (HSD_AnimJoint**) &root,
                                   &error) == NATIVE_ARCHIVE_OK)
        {
            return root;
        }
    } else if (native_name_ends_with(symbol, "_matanim_joint")) {
        if (NativeArchiveMatAnimJoint(binding->graph, offset,
                                      (HSD_MatAnimJoint**) &root,
                                      &error) == NATIVE_ARCHIVE_OK)
        {
            return root;
        }
    } else if (native_name_ends_with(symbol, "_camera") ||
               native_name_ends_with(symbol, "_cobjdesc"))
    {
        if (NativeArchiveCObj(binding->graph, offset, (HSD_CObjDesc**) &root,
                              &error) == NATIVE_ARCHIVE_OK)
        {
            return root;
        }
    } else if (native_name_ends_with(symbol, "_shapeanim_joint")) {
        if (NativeArchiveShapeAnimJoint(binding->graph, offset,
                                        (HSD_ShapeAnimJoint**) &root,
                                        &error) == NATIVE_ARCHIVE_OK)
        {
            return root;
        }
    } else if (native_name_ends_with(symbol, "_joint")) {
        if (NativeArchiveJoint(binding->graph, offset, (HSD_Joint**) &root,
                               &error) == NATIVE_ARCHIVE_OK)
        {
            return root;
        }
    } else if (native_name_ends_with(symbol, "_light")) {
        if (NativeArchiveLight(binding->graph, offset, (HSD_LightDesc**) &root,
                               &error) == NATIVE_ARCHIVE_OK)
        {
            return root;
        }
    } else if (native_name_ends_with(symbol, "_fog")) {
        if (NativeArchiveFog(binding->graph, offset, (HSD_FogDesc**) &root,
                             &error) == NATIVE_ARCHIVE_OK)
        {
            return root;
        }
    } else if (native_name_ends_with(symbol, "_sobjdesc")) {
        if (NativeArchiveSObj(binding->graph, offset, (HSD_SObjDesc**) &root,
                              &error) == NATIVE_ARCHIVE_OK)
        {
            return root;
        }
    } else if (native_name_ends_with(symbol, "_wobj")) {
        if (NativeArchiveWObj(binding->graph, offset, (HSD_WObjDesc**) &root,
                              &error) == NATIVE_ARCHIVE_OK)
        {
            return root;
        }
    }
    /* Byte-only DAT roots are safe to expose through the old API. */
    if (binding->archive->reloc_count == 0 &&
        NativeArchiveDataRange(binding->archive, offset, 1))
    {
        return (void*) (binding->archive->data + offset);
    }
    if (error.status == NATIVE_ARCHIVE_OK) {
        error.status = NATIVE_ARCHIVE_UNSUPPORTED;
        error.offset = offset;
        error.message = "no typed reader for this public root";
    }
    native_archive_error(symbol, &error);
    return NULL;
}

size_t HSD_ArchiveNativeDataLimit(const void* pointer)
{
    NativeArchiveBinding* binding;
    for (binding = native_archive_bindings; binding != NULL;
         binding = binding->next)
    {
        uintptr_t start = (uintptr_t) binding->archive->data;
        uintptr_t address = (uintptr_t) pointer;
        size_t offset;
        size_t limit = NativeArchiveDataSize(binding->archive);
        if (address < start || address - start >= limit) {
            continue;
        }
        offset = (size_t) (address - start);
        for (size_t i = 0; i < NativeArchivePublicCount(binding->archive); ++i)
        {
            NativeArchiveSymbol symbol;
            if (NativeArchivePublic(binding->archive, i, &symbol, NULL) ==
                    NATIVE_ARCHIVE_OK &&
                symbol.offset > offset && symbol.offset < limit)
            {
                limit = symbol.offset;
            }
        }
        return limit - offset;
    }
    return 0;
}

void HSD_ArchiveNativeRelease(HSD_Archive* archive)
{
    NativeArchiveBinding** cursor = &native_archive_bindings;
    NativeArchiveBinding* binding;
    NativeSisRoot* sis;
    NativeSceneAllocation* allocation;
    while (*cursor != NULL && (*cursor)->legacy != archive) {
        cursor = &(*cursor)->next;
    }
    binding = *cursor;
    if (binding == NULL) {
        return;
    }
    *cursor = binding->next;
    sis = binding->sis_roots;
    while (sis != NULL) {
        NativeSisRoot* next = sis->next;
        free(sis->table);
        free(sis);
        sis = next;
    }
    allocation = binding->scene_allocations;
    while (allocation != NULL) {
        NativeSceneAllocation* next = allocation->next;
        free(allocation->pointer);
        free(allocation);
        allocation = next;
    }
    NativeEffectArchiveClose(binding->effects);
    NativeEventArchiveClose(binding->events);
    NativeFighterArchiveClose(binding->fighters);
    NativeItemArchiveClose(binding->items);
    NativeStageArchiveClose(binding->stage);
    NativeArchiveGraphClose(binding->graph);
    NativeArchiveClose(binding->archive);
    /* The owning heap releases the serialized input.  Preloaded archives can
     * use a scene heap, so this bridge must not guess its heap id. */
    free(binding);
}
#endif

#ifdef MUST_MATCH
#pragma push
#pragma dont_inline on
#endif
void lbArchive_InitializeDAT(HSD_Archive* archive, void* data, size_t length)
{
#ifdef MELEE_NATIVE
    NativeArchive* native = NULL;
    NativeArchiveGraph* graph = NULL;
    NativeArchiveBinding* state;
    NativeArchiveError error;
    NativeArchiveStatus status;
    if (archive == NULL || data == NULL) {
        HSD_ASSERT(73, 0);
        return;
    }
    HSD_ArchiveNativeRelease(archive);
    status = NativeArchiveOpen(data, length, &native, &error);
    if (status != NATIVE_ARCHIVE_OK) {
        native_archive_error("open", &error);
        HSD_ASSERT(73, 0);
        return;
    }
    NativeArchiveNullExternals(native);
    status = NativeArchiveGraphOpen(native, &graph, &error);
    if (status != NATIVE_ARCHIVE_OK) {
        native_archive_error("graph", &error);
        NativeArchiveClose(native);
        HSD_ASSERT(73, 0);
        return;
    }
    state = calloc(1, sizeof(*state));
    if (state == NULL) {
        NativeArchiveGraphClose(graph);
        NativeArchiveClose(native);
        HSD_ASSERT(73, 0);
        return;
    }
    state->legacy = archive;
    state->input = data;
    state->archive = native;
    state->graph = graph;
    state->stage = NativeStageArchiveOpen(native, graph);
    state->items = NativeItemArchiveOpen(native, graph);
    state->effects = NativeEffectArchiveOpen(native, graph);
    state->events = NativeEventArchiveOpen(native);
    state->fighters = NativeFighterArchiveOpen(native, graph, state->items);
    if (state->stage == NULL || state->items == NULL ||
        state->effects == NULL || state->fighters == NULL ||
        state->events == NULL)
    {
        NativeEffectArchiveClose(state->effects);
        NativeEventArchiveClose(state->events);
        NativeFighterArchiveClose(state->fighters);
        NativeItemArchiveClose(state->items);
        NativeStageArchiveClose(state->stage);
        free(state);
        NativeArchiveGraphClose(graph);
        NativeArchiveClose(native);
        HSD_ASSERT(73, 0);
        return;
    }
    state->next = native_archive_bindings;
    native_archive_bindings = state;
    memset(archive, 0, sizeof(*archive));
    archive->header.file_size = (u32) length;
    archive->header.data_size = (u32) NativeArchiveDataSize(native);
    archive->header.nb_reloc = (u32) native->reloc_count;
    archive->header.nb_public = (u32) native->public_count;
    archive->header.nb_extern = (u32) native->external_count;
    archive->flags = HSD_ARCHIVE_DONT_FREE;
    archive->top_ptr = state;
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
#ifdef MELEE_NATIVE
    NativeArchiveBinding* binding = native_binding(archive);
    HSD_ASSERT(0xFC, archive);
    HSD_ASSERT(0xFD, binding);
    void* input = binding->input;
    HSD_ArchiveNativeRelease(archive);
    lbHeap_80015CA8(0, input);
    lbHeap_80015CA8(0, archive);
    return;
#else
    HSD_ASSERT(0xFC, archive);
    HSD_ASSERT(0xFD, archive->flags & HSD_ARCHIVE_DONT_FREE);
    lbHeap_80015CA8(0, archive->data - sizeof(HSD_ArchiveHeader));
    lbHeap_80015CA8(0, archive);
#endif
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
    OSReport("lbArchiveRelocate is unavailable on native hosts; use "
             "NativeArchive.\n");
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
