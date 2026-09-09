#include "lbarchive.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "lbdvd.h"
#include "lbfile.h"
#include "lbheap.h"
#include "types.h"
#include <melee/sc/types.h>
#include <dolphin/os.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/debug.h>

#ifdef MELEE_NATIVE
#include <sysdolphin/baselib/sislib.h>
#include "../../../native/source/assets/archive_internal.h"
#endif

#ifdef MELEE_NATIVE
typedef struct NativeArchiveBinding NativeArchiveBinding;
typedef struct NativeSisRoot NativeSisRoot;
typedef struct NativeSceneAllocation NativeSceneAllocation;

struct NativeSisRoot {
    SIS* table;
    size_t count;
    NativeSisRoot* next;
};

struct NativeArchiveBinding {
    HSD_Archive* legacy;
    void* input;
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeSisRoot* sis_roots;
    NativeSceneAllocation* scene_allocations;
    NativeArchiveBinding* next;
};

struct NativeSceneAllocation {
    void* pointer;
    NativeSceneAllocation* next;
};

static NativeArchiveBinding* native_archive_bindings;

static void* native_scene_alloc(NativeArchiveBinding* binding, size_t size)
{
    NativeSceneAllocation* allocation;
    void* pointer = calloc(1, size);
    if (pointer == NULL) return NULL;
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
         binding = binding->next) {
        if (binding->legacy == archive) return binding;
    }
    return NULL;
}

static bool native_name_ends_with(const char* name, const char* suffix)
{
    size_t name_len;
    size_t suffix_len;
    if (name == NULL || suffix == NULL) return false;
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

/* SIS roots are arrays of two serialized four-byte offsets.  Convert the
 * pointer-bearing table while retaining immutable byte ranges in the archive
 * owned copy.  A pair of null entries terminates the table. */
static SIS* native_sis_root(NativeArchiveBinding* binding, uint32_t offset,
                            NativeArchiveError* error)
{
    NativeSisRoot* root;
    SIS* table;
    size_t count = 0;
    size_t i;
    bool saw_entry = false;
    const size_t max_entries = NativeArchiveDataSize(binding->archive) / 8u;

    for (i = 0; i < max_entries; ++i) {
        uint32_t field0 = offset + (uint32_t) (i * 8u);
        uint32_t field1 = field0 + 4u;
        uint32_t target0 = 0;
        uint32_t target1 = 0;
        bool present0 = false;
        bool present1 = false;
        NativeArchiveStatus status0;
        NativeArchiveStatus status1;
        status0 = NativeArchiveReference(binding->archive, field0, &target0,
                                          &present0, error);
        status1 = NativeArchiveReference(binding->archive, field1, &target1,
                                          &present1, error);
        if (status0 != NATIVE_ARCHIVE_OK || status1 != NATIVE_ARCHIVE_OK) {
            /* The first non-SIS pair marks the end of this root. */
            if (!saw_entry) return NULL;
            break;
        }
        if (!present0 && !present1) {
            if (saw_entry) break;
            return NULL;
        }
        if (!present0 || !present1) {
            native_archive_error("SIS root", error);
            return NULL;
        }
        if (!NativeArchiveDataRange(binding->archive, target0, 1) ||
            !NativeArchiveDataRange(binding->archive, target1, 1)) {
            /* Some message tables use end-of-data sentinels. */
            if (target0 > NativeArchiveDataSize(binding->archive) ||
                target1 > NativeArchiveDataSize(binding->archive))
                return NULL;
        }
        saw_entry = true;
        count = i + 1;
    }
    if (count == 0 || count > SIZE_MAX / sizeof(*table)) return NULL;
    table = calloc(count, sizeof(*table));
    if (table == NULL) {
        if (error != NULL) {
            error->status = NATIVE_ARCHIVE_NO_MEMORY;
            error->offset = offset;
            error->message = "cannot allocate SIS root";
        }
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        uint32_t target0 = 0;
        uint32_t target1 = 0;
        bool present0 = false;
        bool present1 = false;
        if (NativeArchiveReference(binding->archive, offset + (uint32_t) (i * 8u),
                                   &target0, &present0, error) !=
                NATIVE_ARCHIVE_OK ||
            NativeArchiveReference(binding->archive,
                                   offset + (uint32_t) (i * 8u + 4u), &target1,
                                   &present1, error) != NATIVE_ARCHIVE_OK) {
            free(table);
            return NULL;
        }
        table[i].kerning = present0 && target0 < NativeArchiveDataSize(binding->archive)
                                ? (TextKerning*) (binding->archive->data + target0)
                                : NULL;
        table[i].textures = present1 && target1 < NativeArchiveDataSize(binding->archive)
                                ? (TextGlyphTexture*) (binding->archive->data + target1)
                                : NULL;
    }
    root = calloc(1, sizeof(*root));
    if (root == NULL) {
        free(table);
        return NULL;
    }
    root->table = table;
    root->count = count;
    root->next = binding->sis_roots;
    binding->sis_roots = root;
    return table;
}

static struct Fighter_804D653C_t* native_rumble_root(
    NativeArchiveBinding* binding, uint32_t offset, NativeArchiveError* error)
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
        if (status != NATIVE_ARCHIVE_OK || !present) break;
        if (!NativeArchiveDataRange(binding->archive, target, 1)) return NULL;
        count = i + 1;
    }
    if (count == 0) return NULL;
    table = calloc(count, sizeof(*table));
    if (table == NULL) return NULL;
    for (i = 0; i < count; ++i) {
        uint32_t target = 0;
        bool present = false;
        const uint8_t* bytes = binding->archive->data + offset + i * 8u;
        if (NativeArchiveReference(binding->archive,
                                   offset + (uint32_t) (i * 8u), &target,
                                   &present, error) != NATIVE_ARCHIVE_OK) {
            free(table);
            return NULL;
        }
        table[i].unk = present ? (void*) (binding->archive->data + target) : NULL;
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
    if (model == NULL) return NULL;
    if (!native_scene_reference(binding, offset, &target, &present, error))
        return NULL;
    if (present && NativeArchiveJoint(binding->graph, target, &model->joint,
                                      error) != NATIVE_ARCHIVE_OK)
        return NULL;
    /* Dynamic model descriptors store a terminated array of animation roots.
     * The array itself is a DAT object, so widen each entry separately. */
    if (!native_scene_reference(binding, offset + 4, &target, &present, error))
        return NULL;
    if (present) {
        size_t count = 0;
        size_t i;
        HSD_AnimJoint** animations;
        for (i = 0; i < 256; ++i) {
            uint32_t animation_offset;
            bool animation_present;
            if (!native_scene_reference(binding, target + (uint32_t) (i * 4u),
                                        &animation_offset, &animation_present,
                                        error))
                return NULL;
            if (!animation_present) break;
            ++count;
        }
        if (count == 0) return NULL;
        animations = native_scene_alloc(binding, (count + 1) * sizeof(*animations));
        if (animations == NULL) return NULL;
        for (i = 0; i < count; ++i) {
            uint32_t animation_offset;
            bool animation_present;
            if (!native_scene_reference(binding, target + (uint32_t) (i * 4u),
                                        &animation_offset, &animation_present,
                                        error) || !animation_present ||
                NativeArchiveAnimation(binding->graph, animation_offset,
                                       &animations[i], error) !=
                    NATIVE_ARCHIVE_OK)
                return NULL;
        }
        model->anims = animations;
    }
    return model;
}

static DynamicModelDesc** native_scene_models(NativeArchiveBinding* binding,
                                              uint32_t offset, size_t* count,
                                              NativeArchiveError* error)
{
    size_t i;
    DynamicModelDesc** models;
    *count = 0;
    for (i = 0; i < 256; ++i) {
        uint32_t target;
        bool present;
        if (!native_scene_reference(binding, offset + (uint32_t) (i * 4u),
                                    &target, &present, error))
            return NULL;
        if (!present) break;
        ++*count;
    }
    if (*count == 0) return NULL;
    models = native_scene_alloc(binding, (*count + 1) * sizeof(*models));
    if (models == NULL) return NULL;
    for (i = 0; i < *count; ++i) {
        uint32_t target;
        bool present;
        if (!native_scene_reference(binding, offset + (uint32_t) (i * 4u),
                                    &target, &present, error) || !present)
            return NULL;
        models[i] = native_scene_model(binding, target, error);
        if (models[i] == NULL) return NULL;
    }
    return models;
}

static struct SceneCameraDesc* native_scene_cameras(
    NativeArchiveBinding* binding, uint32_t offset, NativeArchiveError* error)
{
    size_t i;
    struct SceneCameraDesc* cameras;
    for (i = 0; i < 32; ++i) {
        uint32_t target;
        bool present;
        if (!native_scene_reference(binding, offset + (uint32_t) (i * 8u),
                                    &target, &present, error))
            return NULL;
        /* A camera entry is present when its descriptor is present.  Its
         * animation pointer is optional and is commonly null. */
        if (!present) break;
    }
    if (i == 0) return NULL;
    cameras = native_scene_alloc(binding, (i + 1) * sizeof(*cameras));
    if (cameras == NULL) return NULL;
    for (size_t j = 0; j < i; ++j) {
        uint32_t target;
        bool present;
        if (!native_scene_reference(binding, offset + (uint32_t) (j * 8u),
                                    &target, &present, error) || !present)
            return NULL;
        if (NativeArchiveCObj(binding->graph, target, &cameras[j].desc,
                              error) != NATIVE_ARCHIVE_OK)
            return NULL;
        if (!native_scene_reference(binding,
                                    offset + (uint32_t) (j * 8u + 4u), &target,
                                    &present, error))
            return NULL;
        /* Camera animation conversion is a separate schema.  Keep null
         * animation arrays valid while the descriptor remains usable. */
        if (present) {
            if (error != NULL) {
                error->status = NATIVE_ARCHIVE_UNSUPPORTED;
                error->offset = 32u + offset + (uint32_t) (j * 8u + 4u);
                error->message = "camera animation schema is not implemented";
            }
            return NULL;
        }
    }
    return cameras;
}

static SceneDesc* native_scene_root(NativeArchiveBinding* binding,
                                    uint32_t offset,
                                    NativeArchiveError* error)
{
    SceneDesc* scene = native_scene_alloc(binding, sizeof(*scene));
    uint32_t models_offset, cameras_offset;
    bool models_present, cameras_present;
    size_t model_count;
    if (scene == NULL || !native_scene_reference(binding, offset, &models_offset,
                                                 &models_present, error) ||
        !native_scene_reference(binding, offset + 4, &cameras_offset,
                                &cameras_present, error))
        return NULL;
    if (models_present) {
        scene->models = native_scene_models(binding, models_offset, &model_count,
                                             error);
        if (scene->models == NULL) return NULL;
    }
    if (cameras_present) {
        scene->cameras = native_scene_cameras(binding, cameras_offset, error);
        if (scene->cameras == NULL) return NULL;
    }
    return scene;
}

void* HSD_ArchiveNativePublicAddress(HSD_Archive* archive, const char* symbol)
{
    NativeArchiveBinding* binding = native_binding(archive);
    NativeArchiveError error = { NATIVE_ARCHIVE_OK, 0, "ok" };
    uint32_t offset;
    void* root = NULL;

    if (binding == NULL || symbol == NULL ||
        NativeArchiveFind(binding->archive, symbol, &offset, &error) !=
            NATIVE_ARCHIVE_OK) {
        if (binding != NULL) {
            OSReport("native lookup miss %s public_count=%zu\n", symbol,
                     NativeArchivePublicCount(binding->archive));
            for (size_t i = 0; i < NativeArchivePublicCount(binding->archive) && i < 5; i++) {
                NativeArchiveSymbol item;
                if (NativeArchivePublic(binding->archive, i, &item, NULL) == NATIVE_ARCHIVE_OK)
                    OSReport("  public[%zu]=%s off=%u\n", i, item.name, item.offset);
            }
        }
        return NULL;
    }
    if (strncmp(symbol, "SIS_", 4) == 0) {
        return native_sis_root(binding, offset, &error);
    }
    if (strcmp(symbol, "lbRumbleData") == 0) {
        return native_rumble_root(binding, offset, &error);
    }
    if (strcmp(symbol, "MemCardIconData") == 0 &&
        NativeArchiveDataRange(binding->archive, offset, 1)) {
        return (void*) (binding->archive->data + offset);
    }
    if (strcmp(symbol, "ScNtcCommon_scene_data") == 0) {
        root = native_scene_root(binding, offset, &error);
        if (root != NULL) return root;
    }
    if (native_name_ends_with(symbol, "_animjoint") ||
        native_name_ends_with(symbol, "_animation")) {
        if (NativeArchiveAnimation(binding->graph, offset,
                                   (HSD_AnimJoint**) &root, &error) ==
            NATIVE_ARCHIVE_OK) return root;
    } else if (native_name_ends_with(symbol, "_camera") ||
               native_name_ends_with(symbol, "_cobjdesc")) {
        if (NativeArchiveCObj(binding->graph, offset,
                              (HSD_CObjDesc**) &root, &error) ==
            NATIVE_ARCHIVE_OK) return root;
    } else if (native_name_ends_with(symbol, "_joint")) {
        if (NativeArchiveJoint(binding->graph, offset, (HSD_Joint**) &root,
                               &error) == NATIVE_ARCHIVE_OK)
            return root;
    } else if (native_name_ends_with(symbol, "_wobj") ||
               native_name_ends_with(symbol, "_light")) {
        if (NativeArchiveWObj(binding->graph, offset, (HSD_WObjDesc**) &root,
                              &error) == NATIVE_ARCHIVE_OK)
            return root;
    }
    /* Byte-only DAT roots are safe to expose through the old API. */
    if (binding->archive->reloc_count == 0 &&
        NativeArchiveDataRange(binding->archive, offset, 1))
        return (void*) (binding->archive->data + offset);
    native_archive_error(symbol, &error);
    return NULL;
}

void HSD_ArchiveNativeRelease(HSD_Archive* archive)
{
    NativeArchiveBinding** cursor = &native_archive_bindings;
    NativeArchiveBinding* binding;
    NativeSisRoot* sis;
    NativeSceneAllocation* allocation;
    while (*cursor != NULL && (*cursor)->legacy != archive) cursor = &(*cursor)->next;
    binding = *cursor;
    if (binding == NULL) return;
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
    HSD_ArchiveNativeRelease(archive);
    lbHeap_80015CA8(0, (u8*) archive - 0x20);
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
