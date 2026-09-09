#include "items_special.h"

#include <string.h>

#include "archive_internal.h"
#include "items_fighter_special.h"
#include "items_internal.h"
#include <melee/it/itCommonItems.h>
#include <melee/it/kinds/itkinoko.h>
#include <melee/it/kinds/types.h>
#include <melee/it/types.h>

#define TRY_READ(call)                                                        \
    do {                                                                      \
        NativeArchiveStatus status = (call);                                  \
        if (status != NATIVE_ARCHIVE_OK) {                                    \
            return status;                                                    \
        }                                                                     \
    } while (0)

static NativeArchiveStatus bounds(NativeItemArchive* items, uint32_t offset,
                                  size_t size, NativeArchiveError* error)
{
    if (NativeItemArchiveSpan(items, offset) < size) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 32U + offset,
                                 "item special attributes are truncated");
    }
    return NATIVE_ARCHIVE_OK;
}

/* These ranges contain scalar words only. A relocation is a schema mismatch,
 * even when its current numeric value could also be a valid scalar. */
static NativeArchiveStatus words(NativeItemArchive* items, uint32_t offset,
                                 void* output, size_t size,
                                 NativeArchiveError* error)
{
    const NativeArchive* source = NativeItemArchiveSource(items);
    uint8_t* bytes = output;
    size_t i;
    TRY_READ(NativeArchiveRead(source, offset, output, size, error));
    for (i = 0; i < source->reloc_count; ++i) {
        uint32_t field = source->relocations[i];
        if (field >= offset && (size_t) field - offset < size) {
            return NativeArchiveFail(error, NATIVE_ARCHIVE_TYPE_CONFLICT,
                                     32U + field,
                                     "reference found in item scalar fields");
        }
    }
    for (i = 0; i < size; i += 4) {
        size_t word = ((size_t) offset + i) / 4;
        if (source->external_fields != NULL &&
            (source->external_fields[word / 8] & (1U << (word % 8))) != 0)
        {
            return NativeArchiveFail(
                error, NATIVE_ARCHIVE_TYPE_CONFLICT, 32U + offset + i,
                "external reference found in item scalar fields");
        }
        uint32_t value = NativeArchiveBE32(bytes + i);
        memcpy(bytes + i, &value, sizeof(value));
    }
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus raw(NativeItemArchive* items, uint32_t offset,
                               void* output, size_t size,
                               NativeArchiveError* error)
{
    return NativeArchiveRead(NativeItemArchiveSource(items), offset, output,
                             size, error);
}

static NativeArchiveStatus halves(NativeItemArchive* items, uint32_t offset,
                                  void* output, size_t size,
                                  NativeArchiveError* error)
{
    uint8_t* bytes = output;
    size_t i;
    TRY_READ(raw(items, offset, output, size, error));
    for (i = 0; i < size; i += 2) {
        uint16_t value = (uint16_t) ((bytes[i] << 8) | bytes[i + 1]);
        memcpy(bytes + i, &value, sizeof(value));
    }
    return NATIVE_ARCHIVE_OK;
}

typedef enum SpecialReference {
    SPECIAL_JOINT,
    SPECIAL_ANIMATION,
    SPECIAL_MATERIAL_ANIMATION,
    SPECIAL_SHAPE_ANIMATION,
    SPECIAL_MONSTER_COMMON,
} SpecialReference;

static NativeArchiveStatus reference(NativeItemArchive* items, uint32_t field,
                                     SpecialReference schema, void* output,
                                     NativeArchiveError* error)
{
    uint32_t target;
    bool present;
    void* value = NULL;
    NativeArchiveGraph* graph = NativeItemArchiveGraph(items);
    TRY_READ(NativeArchiveReference(NativeItemArchiveSource(items), field,
                                    &target, &present, error));
    if (present) {
        switch (schema) {
        case SPECIAL_JOINT: {
            HSD_Joint* joint;
            TRY_READ(NativeArchiveJoint(graph, target, &joint, error));
            value = joint;
            break;
        }
        case SPECIAL_ANIMATION: {
            HSD_AnimJoint* animation;
            TRY_READ(NativeArchiveAnimation(graph, target, &animation, error));
            value = animation;
            break;
        }
        case SPECIAL_MATERIAL_ANIMATION: {
            HSD_MatAnimJoint* animation;
            TRY_READ(
                NativeArchiveMatAnimJoint(graph, target, &animation, error));
            value = animation;
            break;
        }
        case SPECIAL_SHAPE_ANIMATION: {
            HSD_ShapeAnimJoint* animation;
            TRY_READ(
                NativeArchiveShapeAnimJoint(graph, target, &animation, error));
            value = animation;
            break;
        }
        case SPECIAL_MONSTER_COMMON:
            /* Zako helpers use the five scalar words shared by monsters. */
            TRY_READ(bounds(items, target, 0x14, error));
            value = NativeItemArchiveAllocate(items, 0x14, error);
            if (value == NULL) {
                return NATIVE_ARCHIVE_NO_MEMORY;
            }
            TRY_READ(words(items, target, value, 0x14, error));
            break;
        }
    }
    memcpy(output, &value, sizeof(value));
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus scalar(NativeItemArchive* items, uint32_t offset,
                                  size_t wire_size, size_t host_size,
                                  void** output, NativeArchiveError* error)
{
    void* value;
    TRY_READ(bounds(items, offset, wire_size, error));
    value = NativeItemArchiveAllocate(items, host_size, error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    TRY_READ(words(items, offset, value, wire_size, error));
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus foods(NativeItemArchive* items, uint32_t offset,
                                 void** output, NativeArchiveError* error)
{
    uint32_t count;
    size_t i;
    itFoodsAttributes* value;
    TRY_READ(bounds(items, offset, 4, error));
    TRY_READ(words(items, offset, &count, sizeof(count), error));
    if (count == 0 || count > (NativeItemArchiveSpan(items, offset) - 4) / 16)
    {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 32U + offset,
                                 "food count exceeds special attributes");
    }
    value =
        NativeItemArchiveAllocate(items, (count + 1) * sizeof(*value), error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    for (i = 0; i < count; ++i) {
        uint32_t entry = offset + (uint32_t) i * 16;
        TRY_READ(words(items, entry, &value[i].x0, 4, error));
        TRY_READ(
            reference(items, entry + 4, SPECIAL_JOINT, &value[i].x4, error));
        TRY_READ(words(items, entry + 8, &value[i].x8, 8, error));
    }
    /* The last record's Y offset follows its X offset in the next word. */
    TRY_READ(words(items, offset + count * 16, &value[count].x0, 4, error));
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus mushroom(NativeItemArchive* items, uint32_t offset,
                                    void** output, NativeArchiveError* error)
{
    KinokoAttrs* value;
    size_t i;
    TRY_READ(bounds(items, offset, 16, error));
    value = NativeItemArchiveAllocate(items, sizeof(*value), error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    TRY_READ(words(items, offset, value, 8, error));
    for (i = 0; i < 2; ++i) {
        TRY_READ(reference(items, offset + 8 + (uint32_t) i * 4,
                           SPECIAL_ANIMATION, &value->animations[i], error));
    }
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus warpStar(NativeItemArchive* items, uint32_t offset,
                                    void** output, NativeArchiveError* error)
{
    uint32_t count;
    size_t i;
    itWstarAttributes* value;
    TRY_READ(bounds(items, offset, 0x28, error));
    TRY_READ(words(items, offset + 0x24, &count, 4, error));
    /* The game builds a seven-entry list and excludes its previous choice. */
    if (count < 2 || count > 7) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID,
                                 32U + offset + 0x24,
                                 "warp star needs two to seven animations");
    }
    if (count > (NativeItemArchiveSpan(items, offset) - 0x28) / 8) {
        return NativeArchiveFail(
            error, NATIVE_ARCHIVE_BOUNDS, 32U + offset + 0x24,
            "warp star animation count exceeds attributes");
    }
    value =
        NativeItemArchiveAllocate(items,
                                  offsetof(itWstarAttributes, x28_entries) +
                                      count * sizeof(itWstarAttrEntry),
                                  error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    TRY_READ(words(items, offset, value, 0x28, error));
    for (i = 0; i < count; ++i) {
        uint32_t entry = offset + 0x28 + (uint32_t) i * 8;
        TRY_READ(reference(items, entry, SPECIAL_ANIMATION,
                           &value->x28_entries[i].x0_anim_joint, error));
        TRY_READ(
            words(items, entry + 4, &value->x28_entries[i].x4_sfx, 4, error));
    }
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus unown(NativeItemArchive* items, uint32_t offset,
                                 void** output, NativeArchiveError* error)
{
    itUnknownAttributes* value;
    size_t i;
    TRY_READ(bounds(items, offset, 0x8c, error));
    value = NativeItemArchiveAllocate(items, sizeof(*value), error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    TRY_READ(words(items, offset, value, 0x24, error));
    for (i = 0; i < 26; ++i) {
        TRY_READ(reference(items, offset + 0x24 + (uint32_t) i * 4,
                           SPECIAL_JOINT, &value->x24[i], error));
    }
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus eventEgg(NativeItemArchive* items, uint32_t offset,
                                    void** output, NativeArchiveError* error)
{
    itEvYoshiEgg_DatAttrs* value;
    uint32_t target;
    bool present;
    TRY_READ(bounds(items, offset, 8, error));
    TRY_READ(NativeArchiveReference(NativeItemArchiveSource(items), offset + 4,
                                    &target, &present, error));
    if (present) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_UNSUPPORTED,
                                 32U + offset + 4,
                                 "event egg reference has no verified schema");
    }
    value = NativeItemArchiveAllocate(items, sizeof(*value), error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    TRY_READ(words(items, offset, &value->x0, 4, error));
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus monsterWords(NativeItemArchive* items,
                                        uint32_t offset, size_t wire_size,
                                        void** output,
                                        NativeArchiveError* error)
{
    uint8_t* value;
    TRY_READ(bounds(items, offset, wire_size, error));
    value = NativeItemArchiveAllocate(items, wire_size + sizeof(void*), error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    TRY_READ(reference(items, offset, SPECIAL_MONSTER_COMMON, value, error));
    TRY_READ(
        words(items, offset + 4, value + sizeof(void*), wire_size - 4, error));
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus leadead(NativeItemArchive* items, uint32_t offset,
                                   void** output, NativeArchiveError* error)
{
    itLeadeadAttributes* value;
    TRY_READ(bounds(items, offset, 0x20, error));
    value = NativeItemArchiveAllocate(items, sizeof(*value), error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    TRY_READ(
        reference(items, offset, SPECIAL_MONSTER_COMMON, &value->x0, error));
    TRY_READ(words(items, offset + 4, &value->x4, 0x14, error));
    TRY_READ(halves(items, offset + 0x18, &value->x18, 6, error));
    TRY_READ(raw(items, offset + 0x1e, &value->x1E, 1, error));
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus octarock(NativeItemArchive* items, uint32_t offset,
                                    void** output, NativeArchiveError* error)
{
    itOctarockAttributes* value;
    TRY_READ(bounds(items, offset, 0x20, error));
    value = NativeItemArchiveAllocate(items, sizeof(*value), error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    TRY_READ(
        reference(items, offset, SPECIAL_MONSTER_COMMON, &value->x0, error));
    TRY_READ(words(items, offset + 4, &value->x4, 0x18, error));
    TRY_READ(halves(items, offset + 0x1c, &value->x1C, 2, error));
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus ottosea(NativeItemArchive* items, uint32_t offset,
                                   void** output, NativeArchiveError* error)
{
    itOldottoseaAttributes* value;
    TRY_READ(bounds(items, offset, 0x2c, error));
    value = NativeItemArchiveAllocate(items, sizeof(*value), error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    TRY_READ(
        reference(items, offset, SPECIAL_MONSTER_COMMON, &value->x0, error));
    TRY_READ(words(items, offset + 4, &value->x4, 0xc, error));
    TRY_READ(raw(items, offset + 0x10, &value->x10, 1, error));
    TRY_READ(words(items, offset + 0x14, &value->x14, 0x14, error));
    TRY_READ(raw(items, offset + 0x28, &value->x28, 1, error));
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus whitebear(NativeItemArchive* items, uint32_t offset,
                                     void** output, NativeArchiveError* error)
{
    itWhiteBeaAttributes* value;
    TRY_READ(bounds(items, offset, 0x18, error));
    value = NativeItemArchiveAllocate(items, sizeof(*value), error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    TRY_READ(
        reference(items, offset, SPECIAL_MONSTER_COMMON, &value->x0, error));
    TRY_READ(words(items, offset + 4, &value->x4, 4, error));
    TRY_READ(halves(items, offset + 8, &value->x8, 8, error));
    TRY_READ(words(items, offset + 0x10, &value->x10, 4, error));
    TRY_READ(halves(items, offset + 0x14, &value->x14, 2, error));
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus likelike(NativeItemArchive* items, uint32_t offset,
                                    void** output, NativeArchiveError* error)
{
    itLikelikeAttributes* value;
    TRY_READ(bounds(items, offset, 0x88, error));
    value = NativeItemArchiveAllocate(items, sizeof(*value), error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    TRY_READ(
        reference(items, offset, SPECIAL_MONSTER_COMMON, &value->x0, error));
    TRY_READ(words(items, offset + 4, &value->x4, 0x38, error));
    TRY_READ(raw(items, offset + 0x3c, &value->x3C, 3, error));
    TRY_READ(words(items, offset + 0x40, value->x40, 0x48, error));
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus coinTiers(NativeItemArchive* items, uint32_t offset,
                                     void** output, NativeArchiveError* error)
{
    it_2E5A_Attrs* value;
    size_t i;
    TRY_READ(bounds(items, offset, 0xc0, error));
    value = NativeItemArchiveAllocate(items, sizeof(*value), error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    TRY_READ(words(items, offset, value, 0x3c, error));
    for (i = 0; i < 3; ++i) {
        uint32_t entry = offset + 0x3c + (uint32_t) i * 0x2c;
        it_2E5A_TierEntry* tier = &value->tiers[i];
        ItemStateDesc* state;
        TRY_READ(reference(items, entry, SPECIAL_JOINT, &tier->joint, error));
        TRY_READ(reference(items, entry + 4, SPECIAL_ANIMATION,
                           &tier->anim_joint, error));
        TRY_READ(reference(items, entry + 8, SPECIAL_MATERIAL_ANIMATION,
                           &tier->matanim_joint, error));
        TRY_READ(reference(items, entry + 0xc, SPECIAL_SHAPE_ANIMATION,
                           &tier->shape_anim_joint, error));
        TRY_READ(words(items, entry + 0x10, &tier->xD84_value, 0x1c, error));
        state = NativeItemArchiveAllocate(items, sizeof(*state), error);
        if (state == NULL) {
            return NATIVE_ARCHIVE_NO_MEMORY;
        }
        state->x0_anim_joint = tier->anim_joint;
        state->x4_matanim_joint = tier->matanim_joint;
        state->x8_parameters = tier->shape_anim_joint;
        tier->native_state = state;
    }
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

NativeArchiveStatus NativeItemSpecialRead(NativeItemArchive* items, int kind,
                                          uint32_t offset, void** output,
                                          NativeArchiveError* error)
{
    /* Wire sizes follow each kind's consumers. Several C declarations also
     * contain runtime-only tails, so sizeof is not a serialized size. */
    static const uint16_t common_sizes[] = {
        0x08, 0x20, 0x38, 0x08, 0x30, 0x54, 0x28, 0x20, 0x1c, 0x18, 0x18,
        0x10, 0x30, 0x10, 0x40, 0x58, 0x10, 0x14, 0,    0x18, 0x28, 0x40,
        0x10, 0x10, 0x04, 0x18, 0,    0,    0x0c, 0,    0x04, 0x04, 0x08,
        0x04, 0xb4, 0x0c, 0x20, 0x1c, 0x80, 0x14, 0x08, 0x04, 0,
    };
    static const uint16_t pokemon_sizes[] = {
        0x0c, 0x14, 0x1c, 0x20, 0x10, 0x30, 0x10, 0x10, 0x10, 0x28, 0x24, 0,
        0x08, 0x08, 0x08, 0x10, 0x10, 0x44, 0x20, 0x04, 0x1c, 0x20, 0x10, 0x10,
        0x5c, 0x1c, 0,    0x0c, 0x18, 0x0c, 0x04, 0x04, 0x0c, 0x0c, 0x08, 0x08,
        0x08, 0x08, 0,    0x10, 0x10, 0x10, 0x04, 0x04, 0x08, 0x18, 0x04,
    };
    size_t size = 0;
    if (items == NULL || output == NULL) {
        return NativeArchiveFail(
            error, NATIVE_ARCHIVE_INVALID, 32U + offset,
            "item special attributes need a context and output");
    }
    *output = NULL;
    switch (kind) {
    case It_Kind_Foods:
        return foods(items, offset, output, error);
    case It_Kind_Kinoko:
    case It_Kind_DKinoko:
        return mushroom(items, offset, output, error);
    case It_Kind_WStar:
        return warpStar(items, offset, output, error);
    case It_Kind_EvYoshiEgg:
        return eventEgg(items, offset, output, error);
    case It_PKind_Unknown:
    case It_Kind_Unknown_Swarm:
        return unown(items, offset, output, error);
    case It_Kind_Sword: {
        itSword_UnkArticle1* value;
        TRY_READ(bounds(items, offset, 0x30, error));
        value = NativeItemArchiveAllocate(items, sizeof(*value), error);
        if (value == NULL) {
            return NATIVE_ARCHIVE_NO_MEMORY;
        }
        TRY_READ(words(items, offset, value, 0x24, error));
        TRY_READ(raw(items, offset + 0x24, &value->x1C.x8, 9, error));
        *output = value;
        return NATIVE_ARCHIVE_OK;
    }
    case It_Kind_Kuriboh:
    case It_Kind_Old_Kuri:
    case It_Kind_Mato:
        return monsterWords(items, offset, 0x14, output, error);
    case It_Kind_Leadead:
        return leadead(items, offset, output, error);
    case It_Kind_Octarock:
        return octarock(items, offset, output, error);
    case It_Kind_Ottosea:
    case It_Kind_Old_Otto:
        return ottosea(items, offset, output, error);
    case It_Kind_Whitebea:
        return whitebear(items, offset, output, error);
    case It_Kind_Nokonoko:
        return monsterWords(items, offset, 0x14, output, error);
    case It_Kind_Patapata:
        return monsterWords(items, offset, 0x40, output, error);
    case It_Kind_Kyasarin:
        return monsterWords(items, offset, 0x4c, output, error);
    case It_Kind_Kyasarin_Egg:
        return monsterWords(items, offset, 0x14, output, error);
    case It_Kind_Arwing_Laser:
        return monsterWords(items, offset, 0xc, output, error);
    case It_Kind_Heiho:
        return monsterWords(items, offset, 0x1c, output, error);
    case It_Kind_Likelike:
        return likelike(items, offset, output, error);
    case It_Kind_Klap:
        return monsterWords(items, offset, 4, output, error);
    case It_Kind_Unk4:
        return coinTiers(items, offset, output, error);
    case It_Kind_Coin:
        size = 0x4c;
        break;
    case It_Kind_ZGShell:
    case It_Kind_ZRShell:
        return monsterWords(items, offset, 0x48, output, error);
    case It_Kind_GreatFox_Laser:
        size = sizeof(itGreatFoxLaser_Attrs);
        break;
    case It_Kind_WhispyApple:
    case It_Kind_WhispyHealApple:
        return monsterWords(items, offset, 0x1c, output, error);
    case It_Kind_Tincle: {
        itTincleAttributes* value;
        TRY_READ(bounds(items, offset, 0x58, error));
        value = NativeItemArchiveAllocate(items, sizeof(*value), error);
        if (value == NULL) {
            return NATIVE_ARCHIVE_NO_MEMORY;
        }
        TRY_READ(reference(items, offset, SPECIAL_MONSTER_COMMON,
                           &value->common, error));
        TRY_READ(words(items, offset + 4, &value->x4, 0x50, error));
        TRY_READ(raw(items, offset + 0x54, &value->x54, 2, error));
        *output = value;
        return NATIVE_ARCHIVE_OK;
    }
    case It_Kind_Tools:
        /* Flatzone has five tools, each with seven scalar motion fields. */
        size = 0x10 + 5 * 0x1c;
        break;
    default:
        if (kind >= It_Kind_Mario_Fire && kind <= It_Kind_Kirby_YoshiEggLay) {
            return NativeItemFighterSpecialRead(items, kind, offset, output,
                                                error);
        } else if (kind >= It_Kind_Capsule && kind <= It_Kind_EvYoshiEgg) {
            size = common_sizes[kind];
        } else if (kind >= It_PKind_Start && kind <= It_Kind_Pokemon_Unk) {
            size = pokemon_sizes[kind - It_PKind_Start];
        }
        break;
    }
    if (size == 0) {
        return NativeArchiveFail(
            error, NATIVE_ARCHIVE_UNSUPPORTED, 32U + offset,
            "item kind has no verified special-attribute schema");
    }
    return scalar(items, offset, size, size, output, error);
}
