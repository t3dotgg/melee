#include <stdlib.h>
#include <string.h>

#include "archive_internal.h"
#include "items_internal.h"
#include "items_special.h"
#include <melee/gr/types.h>
#include <melee/it/it_3F14.h>
#include <melee/it/types.h>

typedef struct NativeItemAllocation {
    void* data;
    struct NativeItemAllocation* next;
} NativeItemAllocation;

typedef struct NativeItemRoot {
    uint32_t offset;
    int schema;
    void* data;
    struct NativeItemRoot* next;
} NativeItemRoot;

struct NativeItemArchive {
    const NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeItemAllocation* allocations;
    NativeItemRoot* roots;
    uint32_t* boundaries;
    size_t boundary_count;
    bool failed;
};

static int compare_offsets(const void* left, const void* right)
{
    uint32_t a = *(const uint32_t*) left;
    uint32_t b = *(const uint32_t*) right;
    return (a > b) - (a < b);
}

NativeItemArchive* NativeItemArchiveOpen(const NativeArchive* archive,
                                         NativeArchiveGraph* graph)
{
    NativeItemArchive* items;
    size_t count;
    if (archive == NULL || graph == NULL) {
        return NULL;
    }
    items = calloc(1, sizeof(*items));
    if (items == NULL) {
        return NULL;
    }
    items->archive = archive;
    items->graph = graph;
    count = (size_t) archive->reloc_count + archive->public_count + 1;
    items->boundaries = calloc(count, sizeof(*items->boundaries));
    if (items->boundaries == NULL) {
        free(items);
        return NULL;
    }
    for (size_t i = 0; i < archive->reloc_count; i++) {
        items->boundaries[items->boundary_count++] =
            NativeArchiveBE32(archive->data + archive->relocations[i]);
    }
    for (size_t i = 0; i < archive->public_count; i++) {
        NativeArchiveSymbol symbol;
        if (NativeArchivePublic(archive, i, &symbol, NULL) !=
            NATIVE_ARCHIVE_OK)
        {
            NativeItemArchiveClose(items);
            return NULL;
        }
        items->boundaries[items->boundary_count++] = symbol.offset;
    }
    items->boundaries[items->boundary_count++] = archive->data_size;
    qsort(items->boundaries, items->boundary_count, sizeof(*items->boundaries),
          compare_offsets);
    return items;
}

void NativeItemArchiveClose(NativeItemArchive* items)
{
    NativeItemAllocation* allocation;
    if (items == NULL) {
        return;
    }
    while ((allocation = items->allocations) != NULL) {
        items->allocations = allocation->next;
        free(allocation->data);
        free(allocation);
    }
    free(items->boundaries);
    free(items);
}

const NativeArchive* NativeItemArchiveSource(NativeItemArchive* items)
{
    return items->archive;
}

NativeArchiveGraph* NativeItemArchiveGraph(NativeItemArchive* items)
{
    return items->graph;
}

void* NativeItemArchiveAllocate(NativeItemArchive* items, size_t size,
                                NativeArchiveError* error)
{
    NativeItemAllocation* allocation = calloc(1, sizeof(*allocation));
    if (allocation != NULL) {
        allocation->data = calloc(1, size == 0 ? 1 : size);
        if (allocation->data != NULL) {
            allocation->next = items->allocations;
            items->allocations = allocation;
            return allocation->data;
        }
        free(allocation);
    }
    NativeArchiveFail(error, NATIVE_ARCHIVE_NO_MEMORY, 0,
                      "cannot allocate native item data");
    return NULL;
}

size_t NativeItemArchiveSpan(NativeItemArchive* items, uint32_t offset)
{
    size_t low = 0;
    size_t high = items->boundary_count;
    if (offset >= items->archive->data_size) {
        return 0;
    }
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (items->boundaries[mid] <= offset) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low < items->boundary_count ? items->boundaries[low] - offset : 0;
}

static bool range(NativeItemArchive* items, uint32_t offset, size_t size,
                  NativeArchiveError* error)
{
    if (NativeArchiveDataRange(items->archive, offset, size)) {
        return true;
    }
    NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 32u + offset,
                      "item data exceeds archive bounds");
    return false;
}

static uint32_t word(NativeItemArchive* items, uint32_t offset)
{
    return NativeArchiveBE32(items->archive->data + offset);
}

static bool reference(NativeItemArchive* items, uint32_t offset,
                      uint32_t* target, bool* present,
                      NativeArchiveError* error)
{
    return NativeArchiveReference(items->archive, offset, target, present,
                                  error) == NATIVE_ARCHIVE_OK;
}

static void words(NativeItemArchive* items, uint32_t offset, void* output,
                  size_t size)
{
    for (size_t i = 0; i < size; i += 4) {
        uint32_t value = word(items, offset + (uint32_t) i);
        memcpy((uint8_t*) output + i, &value, 4);
    }
}

static void* cached(NativeItemArchive* items, uint32_t offset, int schema)
{
    for (NativeItemRoot* root = items->roots; root != NULL; root = root->next)
    {
        if (root->offset == offset && root->schema == schema) {
            return root->data;
        }
    }
    return NULL;
}

static bool remember(NativeItemArchive* items, uint32_t offset, int schema,
                     void* data, NativeArchiveError* error)
{
    NativeItemRoot* root =
        NativeItemArchiveAllocate(items, sizeof(*root), error);
    if (root == NULL) {
        return false;
    }
    root->offset = offset;
    root->schema = schema;
    root->data = data;
    root->next = items->roots;
    items->roots = root;
    return true;
}

static void* script(NativeItemArchive* items, uint32_t offset,
                    NativeArchiveError* error)
{
    if (!range(items, offset, 4, error)) {
        return NULL;
    }
    /* Command engines read BE words and resolve branches through the owning
     * archive. Scripts cannot be widened as ordinary pointer structures. */
    return (void*) (items->archive->data + offset);
}

static ItemAttr* attributes(NativeItemArchive* items, uint32_t offset,
                            NativeArchiveError* error)
{
    const uint8_t* bytes;
    ItemAttr* result;
    if (!range(items, offset, 0x84, error)) {
        return NULL;
    }
    result = NativeItemArchiveAllocate(items, sizeof(*result), error);
    if (result == NULL) {
        return NULL;
    }
    bytes = items->archive->data + offset;
    result->x0_is_heavy = bytes[0] >> 7;
    result->x0_78 = (bytes[0] >> 3) & 15;
    result->x0_hold_kind = bytes[0] & 7;
    result->x1_1 = bytes[1] >> 6;
    result->x1_3 = (bytes[1] >> 5) & 1;
    result->x1_4 = (bytes[1] >> 4) & 1;
    result->x1_5 = (bytes[1] >> 3) & 1;
    result->x1_67_cam_kind = (bytes[1] >> 1) & 3;
    result->x1_8 = bytes[1] & 1;
    result->x3 = bytes[2];
    /* All fields after the flag bytes are four-byte scalars. */
    _Static_assert(offsetof(ItemAttr, x4_throw_speed_mul) == 4,
                   "item scalar attributes must start at byte four");
    _Static_assert(sizeof(ItemAttr) == 0x84,
                   "item attributes contain no pointers");
    words(items, offset + 4, &result->x4_throw_speed_mul, 0x80);
    return result;
}

static ItemModelDesc* model(NativeItemArchive* items, uint32_t offset,
                            NativeArchiveError* error)
{
    uint32_t target;
    bool present;
    ItemModelDesc* result;
    if (!range(items, offset, 16, error)) {
        return NULL;
    }
    result = NativeItemArchiveAllocate(items, sizeof(*result), error);
    if (result == NULL || !reference(items, offset, &target, &present, error))
    {
        return NULL;
    }
    if (present && NativeArchiveJoint(items->graph, target, &result->x0_joint,
                                      error) != NATIVE_ARCHIVE_OK)
    {
        return NULL;
    }
    result->x4_bone_count = word(items, offset + 4);
    result->x8_bone_attach_id = word(items, offset + 8);
    result->xC_bit_field = items->archive->data[offset + 12];
    return result;
}

static ItHurtBoneList* hurtbones(NativeItemArchive* items, uint32_t offset,
                                 NativeArchiveError* error)
{
    uint32_t target;
    bool present;
    ItHurtBoneList* result;
    if (!range(items, offset, 8, error)) {
        return NULL;
    }
    result = NativeItemArchiveAllocate(items, sizeof(*result), error);
    if (result == NULL ||
        !reference(items, offset + 4, &target, &present, error))
    {
        return NULL;
    }
    result->count = word(items, offset);
    if (result->count < 0 || result->count > 2 || (result->count && !present))
    {
        NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                          "invalid item hurtbone count");
        return NULL;
    }
    if (result->count != 0) {
        size_t size = (size_t) result->count * sizeof(*result->descs);
        _Static_assert(sizeof(ItHurtBoneDesc) == 32, "hurtbone scalar layout");
        if (!range(items, target, size, error)) {
            return NULL;
        }
        result->descs = NativeItemArchiveAllocate(items, size, error);
        if (result->descs == NULL) {
            return NULL;
        }
        words(items, target, result->descs, size);
    }
    return result;
}

static ItemStateArray* states(NativeItemArchive* items, uint32_t offset,
                              NativeArchiveError* error)
{
    size_t span = NativeItemArchiveSpan(items, offset);
    size_t count = span / 16;
    ItemStateArray* result;
    if (span == 0 || span % 16 != 0) {
        NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                          "item state array is not a complete record array");
        return NULL;
    }
    result = NativeItemArchiveAllocate(items, sizeof(*result), error);
    if (result == NULL) {
        return NULL;
    }
    result->x0_itemStateDesc = NativeItemArchiveAllocate(
        items, count * sizeof(*result->x0_itemStateDesc), error);
    if (result->x0_itemStateDesc == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        struct ItemStateDesc* state = &result->x0_itemStateDesc[i];
        for (unsigned j = 0; j < 4; j++) {
            uint32_t target;
            bool present;
            if (!reference(items, offset + (uint32_t) i * 16 + j * 4, &target,
                           &present, error))
            {
                return NULL;
            }
            if (!present) {
                continue;
            }
            NativeArchiveStatus status = NATIVE_ARCHIVE_OK;
            switch (j) {
            case 0:
                status = NativeArchiveAnimation(items->graph, target,
                                                &state->x0_anim_joint, error);
                break;
            case 1:
                status = NativeArchiveMatAnimJoint(
                    items->graph, target, &state->x4_matanim_joint, error);
                break;
            case 2:
                status = NativeArchiveShapeAnimJoint(
                    items->graph, target, &state->x8_parameters, error);
                break;
            case 3:
                state->xC_script = script(items, target, error);
                if (state->xC_script == NULL) {
                    return NULL;
                }
                break;
            }
            if (status != NATIVE_ARCHIVE_OK) {
                return NULL;
            }
        }
    }
    return result;
}

static ItemDynamics* dynamics(NativeItemArchive* items, uint32_t offset,
                              NativeArchiveError* error)
{
    ItemDynamics* result;
    uint32_t target;
    bool present;
    if (!range(items, offset, 16, error)) {
        return NULL;
    }
    result = NativeItemArchiveAllocate(items, sizeof(*result), error);
    if (result == NULL) {
        return NULL;
    }
    result->count = word(items, offset);
    result->collision_count = word(items, offset + 8);
    if (result->count < 0 || result->count > 24 ||
        result->collision_count < 0 || result->collision_count > 2)
    {
        NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                          "invalid item dynamics count");
        return NULL;
    }
    if (!reference(items, offset + 4, &target, &present, error)) {
        return NULL;
    }
    if (result->count != 0) {
        if (!present ||
            !range(items, target, (size_t) result->count * 24, error))
        {
            return NULL;
        }
        result->dyn_descs = NativeItemArchiveAllocate(
            items, (size_t) result->count * sizeof(*result->dyn_descs), error);
        if (result->dyn_descs == NULL) {
            return NULL;
        }
        for (int i = 0; i < result->count; i++) {
            uint32_t at = target + (uint32_t) i * 24;
            uint32_t data;
            bool has_data;
            BoneDynamicsDesc* desc = &result->dyn_descs[i];
            desc->bone_id = word(items, at);
            desc->dyn_desc.count = word(items, at + 8);
            words(items, at + 12, &desc->dyn_desc.pos, 12);
            if (!reference(items, at + 4, &data, &has_data, error)) {
                return NULL;
            }
            size_t size = (size_t) desc->dyn_desc.count * 0x3c;
            if (size != 0) {
                if (!has_data || !range(items, data, size, error)) {
                    return NULL;
                }
                /* lb_80011710 reads a packed array of scalar source records,
                 * distinct from its linked runtime DynamicsData objects. */
                desc->dyn_desc.data =
                    NativeItemArchiveAllocate(items, size, error);
                if (desc->dyn_desc.data == NULL) {
                    return NULL;
                }
                words(items, data, desc->dyn_desc.data, size);
            }
        }
    }
    if (!reference(items, offset + 12, &target, &present, error)) {
        return NULL;
    }
    if (result->collision_count != 0) {
        size_t size = (size_t) result->collision_count *
                      sizeof(*result->collision_descs);
        _Static_assert(sizeof(struct ItCollDynamicsDesc) == 20,
                       "collision dynamics scalar layout");
        if (!present || !range(items, target, size, error)) {
            return NULL;
        }
        result->collision_descs =
            NativeItemArchiveAllocate(items, size, error);
        if (result->collision_descs == NULL) {
            return NULL;
        }
        words(items, target, result->collision_descs, size);
    }
    return result;
}

NativeArchiveStatus NativeItemArchiveArticle(NativeItemArchive* items,
                                             int kind, uint32_t offset,
                                             Article** output,
                                             NativeArchiveError* error)
{
    Article* article;
    if (output != NULL) {
        *output = NULL;
    }
    if (output == NULL || items == NULL || items->failed) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                                 "invalid item archive context");
    }
    article = cached(items, offset, 0);
    if (article != NULL) {
        *output = article;
        return NATIVE_ARCHIVE_OK;
    }
    if (!range(items, offset, 24, error)) {
        goto failed;
    }
    article = NativeItemArchiveAllocate(items, sizeof(*article), error);
    if (article == NULL) {
        goto failed;
    }
    for (unsigned i = 0; i < 6; i++) {
        uint32_t target;
        bool present;
        void* value = NULL;
        if (!reference(items, offset + i * 4, &target, &present, error)) {
            goto failed;
        }
        if (!present) {
            continue;
        }
        switch (i) {
        case 0:
            value = article->x0_common_attr = attributes(items, target, error);
            break;
        case 1:
            if (NativeItemSpecialRead(items, kind, target, &value, error) !=
                NATIVE_ARCHIVE_OK)
            {
                goto failed;
            }
            article->x4_specialAttributes = value;
            break;
        case 2:
            value = article->x8_hurtbones = hurtbones(items, target, error);
            break;
        case 3:
            value = article->xC_itemStates = states(items, target, error);
            break;
        case 4:
            value = article->x10_modelDesc = model(items, target, error);
            break;
        case 5:
            value = article->x14_dynamics = dynamics(items, target, error);
            break;
        }
        if (value == NULL) {
            goto failed;
        }
    }
    if (!remember(items, offset, 0, article, error)) {
        goto failed;
    }
    *output = article;
    return NATIVE_ARCHIVE_OK;
failed:
    items->failed = true;
    return error != NULL && error->status != NATIVE_ARCHIVE_OK
               ? error->status
               : NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                                   "invalid item article");
}

static Article** articles(NativeItemArchive* items, uint32_t offset,
                          int first_kind, int count, NativeArchiveError* error)
{
    Article** table;
    if (!range(items, offset, (size_t) count * 4, error)) {
        return NULL;
    }
    table = NativeItemArchiveAllocate(items, (size_t) count * sizeof(*table),
                                      error);
    if (table == NULL) {
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        uint32_t target;
        bool present;
        if (!reference(items, offset + (uint32_t) i * 4, &target, &present,
                       error))
        {
            return NULL;
        }
        if (present &&
            NativeItemArchiveArticle(items, first_kind + i, target, &table[i],
                                     error) != NATIVE_ARCHIVE_OK)
        {
            return NULL;
        }
    }
    return table;
}

static Fighter_804D653C_t* color_scripts(NativeItemArchive* items,
                                         uint32_t offset,
                                         NativeArchiveError* error)
{
    size_t span = NativeItemArchiveSpan(items, offset);
    size_t count = span / 8;
    Fighter_804D653C_t* table;
    if (span == 0 || span % 8 != 0) {
        NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                          "invalid item color script table");
        return NULL;
    }
    table = NativeItemArchiveAllocate(items, count * sizeof(*table), error);
    if (table == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        uint32_t at = offset + (uint32_t) i * 8;
        uint32_t target;
        bool present;
        if (!reference(items, at, &target, &present, error)) {
            return NULL;
        }
        if (present) {
            table[i].unk = script(items, target, error);
            if (table[i].unk == NULL) {
                return NULL;
            }
        }
        table[i].unk4 = items->archive->data[at + 4];
        table[i].unk5 = items->archive->data[at + 5];
    }
    return table;
}

static it_804D6D20_t* public_data(NativeItemArchive* items, uint32_t offset,
                                  NativeArchiveError* error)
{
    it_804D6D20_t* root;
    _Static_assert(sizeof(ItemCommonData) == 0x160,
                   "common item data contains no pointers");
    if (!range(items, offset, 24, error)) {
        return NULL;
    }
    root = NativeItemArchiveAllocate(items, sizeof(*root), error);
    if (root == NULL) {
        return NULL;
    }
    for (unsigned i = 0; i < 6; i++) {
        uint32_t target;
        bool present;
        void* value = NULL;
        if (!reference(items, offset + i * 4, &target, &present, error)) {
            return NULL;
        }
        if (!present) {
            continue;
        }
        switch (i) {
        case 0:
            if (!range(items, target, 0x160, error)) {
                return NULL;
            }
            root->x0 =
                NativeItemArchiveAllocate(items, sizeof(*root->x0), error);
            if (root->x0 == NULL) {
                return NULL;
            }
            words(items, target, root->x0, 0x160);
            root->x0->x48_byte = items->archive->data[target + 0x48];
            value = root->x0;
            break;
        case 1:
            value = root->x4 = articles(items, target, It_Kind_Capsule,
                                        It_Kind_Kuriboh, error);
            break;
        case 2:
            value = root->x8 =
                articles(items, target, It_Kind_Kuriboh,
                         It_PKind_Start - It_Kind_Kuriboh, error);
            break;
        case 3:
            value = root->xC =
                articles(items, target, It_PKind_Start,
                         It_Kind_Old_Kuri - It_PKind_Start, error);
            break;
        case 4:
            if (!range(items, target, 28, error)) {
                return NULL;
            }
            value = root->x10 =
                NativeItemArchiveAllocate(items, sizeof(*root->x10), error);
            if (value != NULL) {
                words(items, target, value, 28);
            }
            break;
        case 5:
            value = root->x14 = color_scripts(items, target, error);
            break;
        }
        if (value == NULL) {
            return NULL;
        }
    }
    return root;
}

static void** script_table(NativeItemArchive* items, uint32_t offset,
                           NativeArchiveError* error)
{
    size_t count;
    void** table;
    /* Slot zero is reserved. The final NULL follows the actual scripts. */
    for (count = 0;; count++) {
        uint32_t target;
        bool present;
        if (count > (NativeArchiveDataSize(items->archive) - offset) / 4 ||
            !reference(items, offset + (uint32_t) count * 4, &target, &present,
                       error))
        {
            return NULL;
        }
        if (count > 0 && !present) {
            break;
        }
    }
    table =
        NativeItemArchiveAllocate(items, (count + 1) * sizeof(*table), error);
    if (table == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        uint32_t target;
        bool present;
        if (!reference(items, offset + (uint32_t) i * 4, &target, &present,
                       error))
        {
            return NULL;
        }
        if (present) {
            table[i] = script(items, target, error);
            if (table[i] == NULL) {
                return NULL;
            }
        }
    }
    return table;
}

static struct GroundItemData** ground_items(NativeItemArchive* items,
                                            uint32_t offset,
                                            NativeArchiveError* error)
{
    size_t count;
    struct GroundItemData** table;
    for (count = 0;; count++) {
        uint32_t target;
        bool present;
        if (count > (NativeArchiveDataSize(items->archive) - offset) / 4 ||
            !reference(items, offset + (uint32_t) count * 4, &target, &present,
                       error))
        {
            return NULL;
        }
        if (!present) {
            break;
        }
    }
    table =
        NativeItemArchiveAllocate(items, (count + 1) * sizeof(*table), error);
    if (table == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        uint32_t target;
        uint32_t article;
        bool present;
        if (!reference(items, offset + (uint32_t) i * 4, &target, &present,
                       error) ||
            !range(items, target, 8, error))
        {
            return NULL;
        }
        table[i] = NativeItemArchiveAllocate(items, sizeof(*table[i]), error);
        if (table[i] == NULL) {
            return NULL;
        }
        table[i]->unk0 = word(items, target);
        if (!reference(items, target + 4, &article, &present, error)) {
            return NULL;
        }
        if (present && NativeItemArchiveArticle(items, table[i]->unk0, article,
                                                &table[i]->unk4,
                                                error) != NATIVE_ARCHIVE_OK)
        {
            return NULL;
        }
    }
    return table;
}

NativeArchiveStatus NativeItemArchiveRead(NativeItemArchive* items,
                                          const char* symbol, uint32_t offset,
                                          void** output,
                                          NativeArchiveError* error)
{
    int schema;
    void* root;
    if (output != NULL) {
        *output = NULL;
    }
    if (output == NULL || symbol == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                                 "invalid item archive context");
    }
    if (strcmp(symbol, "itPublicData") == 0) {
        schema = 1;
    } else if (strcmp(symbol, "itemdata") == 0) {
        schema = 2;
    } else if (strcmp(symbol, "ALDYakuAll") == 0) {
        schema = 3;
    } else {
        return NATIVE_ARCHIVE_NOT_FOUND;
    }
    if (items == NULL || items->failed) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                                 "invalid item archive context");
    }
    root = cached(items, offset, schema);
    if (root != NULL) {
        *output = root;
        return NATIVE_ARCHIVE_OK;
    }
    if (!range(items, offset, 4, error)) {
        goto failed;
    }
    switch (schema) {
    case 1:
        root = public_data(items, offset, error);
        break;
    case 2:
        root = ground_items(items, offset, error);
        break;
    case 3:
        root = script_table(items, offset, error);
        break;
    }
    if (root == NULL || !remember(items, offset, schema, root, error)) {
        goto failed;
    }
    *output = root;
    return NATIVE_ARCHIVE_OK;
failed:
    items->failed = true;
    return error != NULL && error->status != NATIVE_ARCHIVE_OK
               ? error->status
               : NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                                   "invalid item archive root");
}
