#include "items_fighter_special.h"

#include <string.h>

#include "archive_internal.h"
#include "items_internal.h"
#include <melee/it/itCharItems.h>
#include <melee/it/itCommonItems.h>
#include <melee/it/itYoyo.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/pobj.h>

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
                                 "fighter item attributes are truncated");
    }
    return NATIVE_ARCHIVE_OK;
}

/* A relocation in scalar or byte fields is a schema mismatch. */
static NativeArchiveStatus raw(NativeItemArchive* items, uint32_t offset,
                               void* output, size_t size,
                               NativeArchiveError* error)
{
    const NativeArchive* source = NativeItemArchiveSource(items);
    size_t i;
    if (!NativeArchiveDataRange(source, offset, size)) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 32U + offset,
                                 "fighter item field exceeds archive data");
    }
    for (i = 0; i < source->reloc_count; ++i) {
        uint32_t field = source->relocations[i];
        if ((size_t) field < (size_t) offset + size &&
            (size_t) offset < (size_t) field + 4)
        {
            return NativeArchiveFail(
                error, NATIVE_ARCHIVE_TYPE_CONFLICT, 32U + field,
                "reference found in fighter item scalars");
        }
    }
    if (source->external_fields != NULL) {
        for (i = offset / 4; i * 4 < (size_t) offset + size; ++i) {
            if ((source->external_fields[i / 8] & (1U << (i % 8))) != 0) {
                return NativeArchiveFail(error, NATIVE_ARCHIVE_TYPE_CONFLICT,
                                         32U + i * 4,
                                         "external reference in item scalars");
            }
        }
    }
    return NativeArchiveRead(source, offset, output, size, error);
}

static NativeArchiveStatus words(NativeItemArchive* items, uint32_t offset,
                                 void* output, size_t size,
                                 NativeArchiveError* error)
{
    uint8_t* bytes = output;
    size_t i;
    TRY_READ(raw(items, offset, output, size, error));
    for (i = 0; i < size; i += 4) {
        uint32_t value = NativeArchiveBE32(bytes + i);
        memcpy(bytes + i, &value, 4);
    }
    return NATIVE_ARCHIVE_OK;
}

typedef enum FighterReference {
    FIGHTER_JOINT,
    FIGHTER_ANIMATION,
    FIGHTER_MATERIAL,
    FIGHTER_SHAPE,
    FIGHTER_ANIMATION_CHILD,
    FIGHTER_MATERIAL_CHILD,
    FIGHTER_SHAPE_CHILD,
} FighterReference;

typedef struct FighterField {
    uint16_t wire;
    size_t host;
    FighterReference schema;
} FighterField;

static NativeArchiveStatus reference(NativeItemArchive* items, uint32_t field,
                                     FighterReference schema, void* output,
                                     NativeArchiveError* error)
{
    NativeArchiveGraph* graph = NativeItemArchiveGraph(items);
    uint32_t target;
    bool present;
    void* value = NULL;
    TRY_READ(NativeArchiveReference(NativeItemArchiveSource(items), field,
                                    &target, &present, error));
    if (present) {
        switch (schema) {
        case FIGHTER_JOINT: {
            HSD_Joint* joint;
            TRY_READ(NativeArchiveJoint(graph, target, &joint, error));
            value = joint;
            break;
        }
        case FIGHTER_ANIMATION:
        case FIGHTER_ANIMATION_CHILD: {
            HSD_AnimJoint* animation;
            TRY_READ(NativeArchiveAnimation(graph, target, &animation, error));
            value = schema == FIGHTER_ANIMATION ? (void*) animation
                                                : (void*) &animation->child;
            break;
        }
        case FIGHTER_MATERIAL:
        case FIGHTER_MATERIAL_CHILD: {
            HSD_MatAnimJoint* animation;
            TRY_READ(
                NativeArchiveMatAnimJoint(graph, target, &animation, error));
            value = schema == FIGHTER_MATERIAL ? (void*) animation
                                               : (void*) &animation->child;
            break;
        }
        case FIGHTER_SHAPE:
        case FIGHTER_SHAPE_CHILD: {
            HSD_ShapeAnimJoint* animation;
            TRY_READ(
                NativeArchiveShapeAnimJoint(graph, target, &animation, error));
            value = schema == FIGHTER_SHAPE ? (void*) animation
                                            : (void*) &animation->child;
            break;
        }
        }
    }
    memcpy(output, &value, sizeof(value));
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus mapped(NativeItemArchive* items, uint32_t offset,
                                  size_t wire_size, size_t host_size,
                                  const FighterField* fields, size_t count,
                                  void** output, NativeArchiveError* error)
{
    uint8_t* value;
    size_t wire = 0;
    size_t host = 0;
    size_t i;
    TRY_READ(bounds(items, offset, wire_size, error));
    value = NativeItemArchiveAllocate(items, host_size, error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    for (i = 0; i < count; ++i) {
        TRY_READ(words(items, offset + (uint32_t) wire, value + host,
                       fields[i].wire - wire, error));
        TRY_READ(reference(items, offset + fields[i].wire, fields[i].schema,
                           value + fields[i].host, error));
        wire = fields[i].wire + 4;
        host = fields[i].host + sizeof(void*);
    }
    TRY_READ(words(items, offset + (uint32_t) wire, value + host,
                   wire_size - wire, error));
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

#define FIELD(type, field, wire, schema)                                      \
    { wire, offsetof(type, field), FIGHTER_##schema }
#define MAP(type, size, fields)                                               \
    mapped(items, offset, size, sizeof(type), fields,                         \
           sizeof(fields) / sizeof(fields[0]), output, error)

/* These offsets are the attribute declarations in itCharItems.h and itYoyo.h.
 * Unused fields after the last consumer are not read from the archive. */
static const FighterField arrow_fields[] = {
    FIELD(itLinkArrowAttributes, x24, 0x24, JOINT),
    FIELD(itLinkArrowAttributes, x28, 0x28, JOINT),
};
static const FighterField hookshot_fields[] = {
    FIELD(itLinkHookshotAttributes, x54, 0x54, JOINT),
    FIELD(itLinkHookshotAttributes, x58, 0x58, JOINT),
    FIELD(itLinkHookshotAttributes, x5C, 0x5C, JOINT),
};
static const FighterField boomerang_fields[] = {
    FIELD(itLinkBoomerangAttributes, x44, 0x44, JOINT),
    FIELD(itLinkBoomerangAttributes, x48, 0x48, JOINT),
    FIELD(itLinkBoomerangAttributes, x4C_anim.anim, 0x4C, ANIMATION),
    FIELD(itLinkBoomerangAttributes, x4C_anim.matanim, 0x50, MATERIAL),
    FIELD(itLinkBoomerangAttributes, x4C_anim.shapeanim, 0x54, SHAPE),
    FIELD(itLinkBoomerangAttributes, x58_anim.anim, 0x58, ANIMATION),
    FIELD(itLinkBoomerangAttributes, x58_anim.matanim, 0x5C, MATERIAL),
    FIELD(itLinkBoomerangAttributes, x58_anim.shapeanim, 0x60, SHAPE),
};
static const FighterField chain_fields[] = {
    FIELD(itSeakChain_Attrs, x64_joint, 0x64, JOINT),
    FIELD(itSeakChain_Attrs, x68_joint, 0x68, JOINT),
};
static const FighterField string_fields[] = {
    FIELD(itClimbersStringAttributes, x24_joint, 0x24, JOINT),
    FIELD(itClimbersStringAttributes, x28_joint, 0x28, JOINT),
};
static const FighterField yoyo_fields[] = {
    FIELD(itYoyoAttributes, x50_string_joint, 0x50, JOINT),
    FIELD(itYoyoAttributes, x54_yoyo_joint, 0x54, JOINT),
    FIELD(itYoyoAttributes, x58_yoyo_matanim, 0x58, MATERIAL),
};
/* itsamusgrapple.c dereferences the first field of each animation root to
 * animate the corresponding model's child. Keep that extra indirection. */
static const FighterField grapple_fields[] = {
    FIELD(itSamusGrappleAttributes, x64, 0x64, JOINT),
    FIELD(itSamusGrappleAttributes, x68, 0x68, JOINT),
    FIELD(itSamusGrappleAttributes, x6C, 0x6C, JOINT),
    FIELD(itSamusGrappleAttributes, x70, 0x70, JOINT),
    FIELD(itSamusGrappleAttributes, x74, 0x74, ANIMATION_CHILD),
    FIELD(itSamusGrappleAttributes, x78, 0x78, MATERIAL_CHILD),
    FIELD(itSamusGrappleAttributes, x7C, 0x7C, SHAPE_CHILD),
    FIELD(itSamusGrappleAttributes, x80, 0x80, ANIMATION_CHILD),
    FIELD(itSamusGrappleAttributes, x84, 0x84, MATERIAL_CHILD),
    FIELD(itSamusGrappleAttributes, x88, 0x88, SHAPE_CHILD),
    FIELD(itSamusGrappleAttributes, x8C, 0x8C, ANIMATION_CHILD),
    FIELD(itSamusGrappleAttributes, x90, 0x90, MATERIAL_CHILD),
    FIELD(itSamusGrappleAttributes, x94, 0x94, SHAPE_CHILD),
    FIELD(itSamusGrappleAttributes, x98, 0x98, ANIMATION_CHILD),
    FIELD(itSamusGrappleAttributes, x9C, 0x9C, MATERIAL_CHILD),
    FIELD(itSamusGrappleAttributes, xA0, 0xA0, SHAPE_CHILD),
    FIELD(itSamusGrappleAttributes, xA4, 0xA4, ANIMATION_CHILD),
    FIELD(itSamusGrappleAttributes, xA8, 0xA8, MATERIAL_CHILD),
    FIELD(itSamusGrappleAttributes, xAC, 0xAC, SHAPE_CHILD),
};

static NativeArchiveStatus byteArray(NativeItemArchive* items, uint32_t field,
                                     size_t count, u8** output,
                                     NativeArchiveError* error)
{
    uint32_t target;
    bool present;
    *output = NULL;
    TRY_READ(NativeArchiveReference(NativeItemArchiveSource(items), field,
                                    &target, &present, error));
    if (!present) {
        if (count != 0) {
            return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID,
                                     32U + field,
                                     "item bone count has no index array");
        }
        return NATIVE_ARCHIVE_OK;
    }
    TRY_READ(bounds(items, target, count, error));
    *output = NativeItemArchiveAllocate(items, count, error);
    if (*output == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    return raw(items, target, *output, count, error);
}

/* itdraw.c reads this block through it_266F_ItemVars. The counts are u16 and
 * the joint indices are bytes. Reversing each word corrupts both fields. */
static NativeArchiveStatus visibility(NativeItemArchive* items, uint32_t field,
                                      void** output, NativeArchiveError* error)
{
    uint32_t target;
    bool present;
    uint8_t counts[4];
    it_266F_ItemVars* value;
    *output = NULL;
    TRY_READ(NativeArchiveReference(NativeItemArchiveSource(items), field,
                                    &target, &present, error));
    if (!present) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32U + field,
                                 "Game & Watch item has no visibility data");
    }
    TRY_READ(bounds(items, target, 0x10, error));
    value = NativeItemArchiveAllocate(items, sizeof(*value), error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    TRY_READ(raw(items, target, counts, 4, error));
    value->x0 = (uint16_t) ((counts[0] << 8) | counts[1]);
    TRY_READ(raw(items, target + 8, counts, 4, error));
    value->x8 = (uint16_t) ((counts[0] << 8) | counts[1]);
    TRY_READ(byteArray(items, target + 4, value->x0, &value->x4, error));
    TRY_READ(byteArray(items, target + 12, value->x8, &value->xC, error));
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus gamewatch(NativeItemArchive* items, uint32_t offset,
                                     bool chef, void** output,
                                     NativeArchiveError* error)
{
    itGamewatchchefAttributes* value;
    /* Both fighter Chef routines select one of five projectile types. */
    size_t wire_size = chef ? 0x10 + 5 * 0x14 : 4;
    size_t host_size = chef ? offsetof(itGamewatchchefAttributes, entries) +
                                  5 * sizeof(itGamewatchchefAttrEntry)
                            : sizeof(void*);
    TRY_READ(bounds(items, offset, wire_size, error));
    value = NativeItemArchiveAllocate(items, host_size, error);
    if (value == NULL) {
        return NATIVE_ARCHIVE_NO_MEMORY;
    }
    TRY_READ(visibility(items, offset, &value->x0, error));
    if (chef) {
        TRY_READ(words(items, offset + 4, &value->x4, wire_size - 4, error));
    }
    *output = value;
    return NATIVE_ARCHIVE_OK;
}

static NativeArchiveStatus turnip(NativeItemArchive* items, uint32_t offset,
                                  void** output, NativeArchiveError* error)
{
    uint32_t count;
    TRY_READ(bounds(items, offset, 8, error));
    TRY_READ(words(items, offset + 4, &count, 4, error));
    if (count == 0 || count > (NativeItemArchiveSpan(items, offset) - 8) / 8) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 36U + offset,
                                 "turnip type count exceeds attribute data");
    }
    return mapped(items, offset, 8 + count * 8, 8 + count * 8, NULL, 0, output,
                  error);
}

NativeArchiveStatus NativeItemFighterSpecialRead(NativeItemArchive* items,
                                                 int kind, uint32_t offset,
                                                 void** output,
                                                 NativeArchiveError* error)
{
    /* Kirby's copied items use the same item functions and archive layout. */
    static const uint8_t copies[] = {
        It_Kind_Mario_Fire,
        It_Kind_DrMario_Vitamin,
        It_Kind_Luigi_Fire,
        It_Kind_IceClimber_Ice,
        It_Kind_Peach_Toad,
        It_Kind_Peach_ToadSpore,
        It_Kind_Fox_Laser,
        It_Kind_Falco_Laser,
        It_Kind_Fox_Blaster,
        It_Kind_Falco_Blaster,
        It_Kind_Link_Arrow,
        It_Kind_CLink_Arrow,
        It_Kind_Link_Bow,
        It_Kind_CLink_Bow,
        It_Kind_Mewtwo_ShadowBall,
        It_Kind_Ness_PKFlush,
        It_Kind_Ness_PKFlush_Explode,
        It_Kind_Pikachu_TJolt_Ground,
        It_Kind_Pikachu_TJolt_Air,
        It_Kind_Pichu_TJolt_Ground,
        It_Kind_Pichu_TJolt_Air,
        It_Kind_Samus_Charge,
        It_Kind_Seak_NeedleThrow,
        It_Kind_Seak_NeedleHeld,
        It_Kind_Koopa_Flame,
        It_Kind_GameWatch_Chef,
        It_Kind_GameWatch_Parachute,
        It_Kind_Yoshi_EggLay,
    };
    /* Sizes follow each kind's attribute consumers. Some shared declarations
     * contain unused tails absent from the serialized object. */
    static const uint16_t scalar_sizes[It_Kind_Kirby_YoshiEggLay + 1] = {
        [It_Kind_Mario_Fire] = 0x14,
        [It_Kind_DrMario_Vitamin] = 0x14,
        [It_Kind_Kirby_CBeam] = 0x10,
        [It_Kind_Unk1] = 4,
        [It_Kind_Fox_Laser] = 0x28,
        [It_Kind_Falco_Laser] = 0x28,
        [It_Kind_Fox_Illusion] = 8,
        [It_Kind_Falco_Phantasm] = 8,
        [It_Kind_Link_Bomb] = 0x34,
        [It_Kind_CLink_Bomb] = 0x34,
        [It_Kind_Ness_PKFire] = 8,
        [It_Kind_Ness_PKFire_Flame] = 0xC,
        [It_Kind_Ness_PKFlush] = 0x2C,
        [It_Kind_Ness_PKThunder] = 0x14,
        [It_Kind_Ness_PKFlush_Explode] = 0x14,
        [It_Kind_Seak_NeedleThrow] = 0xC,
        [It_Kind_Pikachu_Thunder] = 0xC,
        [It_Kind_Pichu_Thunder] = 0xC,
        [It_Kind_Yoshi_EggThrow] = 8,
        [It_Kind_Yoshi_Star] = 8,
        [It_Kind_Pikachu_TJolt_Ground] = 0x10,
        [It_Kind_Pichu_TJolt_Ground] = 0x10,
        [It_Kind_Samus_Bomb] = 0x10,
        [It_Kind_Samus_Charge] = 0x20,
        [It_Kind_Samus_Missile] = 0x38,
        [It_Kind_Koopa_Flame] = 0x18,
        [It_Kind_Luigi_Fire] = 0x10,
        [It_Kind_IceClimber_Blizzard] = 0x14,
        [It_Kind_Zelda_DinFire] = 0x30,
        [It_Kind_Zelda_DinFire_Explode] = 0x14,
        [It_Kind_Mewtwo_Disable] = 8,
        [It_Kind_Peach_ToadSpore] = 0x10,
        [It_Kind_Mewtwo_ShadowBall] = 0x30,
        [It_Kind_MasterHand_Laser] = 8,
        [It_Kind_MasterHand_Bullet] = 4,
        [It_Kind_CrazyHand_Laser] = 8,
        [It_Kind_CrazyHand_Bullet] = 4,
        [It_Kind_CrazyHand_Bomb] = 0xC,
    };
    size_t size;
    size_t host_size;
    _Static_assert(sizeof(copies) ==
                       It_Kind_Kirby_YoshiEggLay - It_Kind_Kirby_MarioFire + 1,
                   "Kirby item kind map must cover all copies");
    if (items == NULL || output == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32U + offset,
                                 "fighter item needs a context and output");
    }
    *output = NULL;
    if (kind < It_Kind_Mario_Fire || kind > It_Kind_Kirby_YoshiEggLay) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_UNSUPPORTED,
                                 32U + offset,
                                 "item kind is not a fighter item");
    }
    if (kind >= It_Kind_Kirby_MarioFire) {
        kind = copies[kind - It_Kind_Kirby_MarioFire];
    }
    switch (kind) {
    case It_Kind_Link_Arrow:
    case It_Kind_CLink_Arrow:
        return MAP(itLinkArrowAttributes, 0x2C, arrow_fields);
    case It_Kind_Link_HShot:
    case It_Kind_CLink_HShot:
        return MAP(itLinkHookshotAttributes, 0x60, hookshot_fields);
    case It_Kind_Link_Boomerang:
    case It_Kind_CLink_Boomerang:
        return MAP(itLinkBoomerangAttributes, 0x64, boomerang_fields);
    case It_Kind_Samus_GBeam:
        return MAP(itSamusGrappleAttributes, 0xB0, grapple_fields);
    case It_Kind_Ness_Yoyo:
        return MAP(itYoyoAttributes, 0x5C, yoyo_fields);
    case It_Kind_Seak_Chain: {
        itSeakChain_Attrs* value;
        void* converted;
        TRY_READ(mapped(items, offset, 0x6C, sizeof(*value), chain_fields,
                        sizeof(chain_fields) / sizeof(chain_fields[0]),
                        &converted, error));
        value = converted;
        TRY_READ(raw(items, offset + 8, value->pad_8, 8, error));
        TRY_READ(raw(items, offset + 0x4C, value->pad_4C, 8, error));
        *output = value;
        return NATIVE_ARCHIVE_OK;
    }
    case It_Kind_IceClimber_GumStrings: {
        itClimbersStringAttributes* value;
        void* converted;
        TRY_READ(mapped(items, offset, 0x2C, sizeof(*value), string_fields,
                        sizeof(string_fields) / sizeof(string_fields[0]),
                        &converted, error));
        value = converted;
        TRY_READ(raw(items, offset + 0x10, value->pad_10, 4, error));
        *output = value;
        return NATIVE_ARCHIVE_OK;
    }
    case It_Kind_IceClimber_Ice: {
        itClimbersIceAttributes* value;
        void* converted;
        TRY_READ(mapped(items, offset, 0x34, sizeof(*value), NULL, 0,
                        &converted, error));
        value = converted;
        TRY_READ(raw(items, offset + 0x20, value->pad_20, 4, error));
        *output = value;
        return NATIVE_ARCHIVE_OK;
    }
    case It_Kind_Peach_Turnip:
        return turnip(items, offset, output, error);
    case It_Kind_GameWatch_Greenhouse:
    case It_Kind_GameWatch_Manhole:
    case It_Kind_GameWatch_Fire:
    case It_Kind_GameWatch_Parachute:
    case It_Kind_GameWatch_Turtle:
    case It_Kind_GameWatch_Breath:
    case It_Kind_GameWatch_Judge:
    case It_Kind_GameWatch_Panic:
    case It_Kind_GameWatch_Rescue:
        return gamewatch(items, offset, false, output, error);
    case It_Kind_GameWatch_Chef:
        return gamewatch(items, offset, true, output, error);
    case It_Kind_Fox_Blaster:
    case It_Kind_Falco_Blaster:
    case It_Kind_Link_Bow:
    case It_Kind_CLink_Bow:
    case It_Kind_Ness_PKThunder1:
    case It_Kind_Ness_PKThunder2:
    case It_Kind_Ness_PKThunder3:
    case It_Kind_Ness_PKThunder4:
    case It_Kind_Seak_NeedleHeld:
    case It_Kind_Mario_Cape:
    case It_Kind_DrMario_Sheet:
    case It_Kind_Pikachu_TJolt_Air:
    case It_Kind_Pichu_TJolt_Air:
    case It_Kind_Ness_Bat:
    case It_Kind_Peach_Parasol:
    case It_Kind_Peach_Toad:
    case It_Kind_CLink_Milk: {
        /* These consumers use only the common article fields. Preserve the
         * unused special bytes, but reject an unhandled pointer layout. */
        void* value;
        size = NativeItemArchiveSpan(items, offset);
        if (size == 0) {
            return NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS,
                                     32U + offset,
                                     "empty item attribute data");
        }
        value = NativeItemArchiveAllocate(items, size, error);
        if (value == NULL) {
            return NATIVE_ARCHIVE_NO_MEMORY;
        }
        TRY_READ(raw(items, offset, value, size, error));
        *output = value;
        return NATIVE_ARCHIVE_OK;
    }
    default:
        break;
    }
    size = scalar_sizes[kind];
    if (size == 0) {
        return NativeArchiveFail(
            error, NATIVE_ARCHIVE_UNSUPPORTED, 32U + offset,
            "fighter item has no special-attribute schema");
    }
    host_size = size;
    switch (kind) {
    case It_Kind_Link_Bomb:
    case It_Kind_CLink_Bomb:
        host_size = sizeof(itLinkBombAttributes);
        break;
    case It_Kind_Samus_Bomb:
        host_size = sizeof(itSamusBombAttributes);
        break;
    case It_Kind_Samus_Missile:
        host_size = sizeof(itSamusMissileAttributes);
        break;
    case It_Kind_Ness_PKFire:
        host_size = sizeof(itNessPKFirepillarAttributes);
        break;
    case It_Kind_Luigi_Fire:
    case It_Kind_Unk1:
        host_size = sizeof(itUnkAttributes);
        break;
    }
    return mapped(items, offset, size, host_size, NULL, 0, output, error);
}
