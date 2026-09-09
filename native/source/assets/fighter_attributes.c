#include "fighter_attributes.h"

#include <stdlib.h>
#include <string.h>

#include "archive_internal.h"
#include <melee/ft/kinds/ftCaptain/types.h>
#include <melee/ft/kinds/ftCrazyHand/types.h>
#include <melee/ft/kinds/ftDonkey/types.h>
#include <melee/ft/kinds/ftFox/types.h>
#include <melee/ft/kinds/ftGameWatch/types.h>
#include <melee/ft/kinds/ftKirby/types.h>
#include <melee/ft/kinds/ftKoopa/types.h>
#include <melee/ft/kinds/ftLink/types.h>
#include <melee/ft/kinds/ftLuigi/types.h>
#include <melee/ft/kinds/ftMario/types.h>
#include <melee/ft/kinds/ftMars/types.h>
#include <melee/ft/kinds/ftMasterHand/types.h>
#include <melee/ft/kinds/ftMewtwo/types.h>
#include <melee/ft/kinds/ftNess/types.h>
#include <melee/ft/kinds/ftPeach/types.h>
#include <melee/ft/kinds/ftPikachu/types.h>
#include <melee/ft/kinds/ftPopo/types.h>
#include <melee/ft/kinds/ftPurin/types.h>
#include <melee/ft/kinds/ftSamus/types.h>
#include <melee/ft/kinds/ftSeak/types.h>
#include <melee/ft/kinds/ftYoshi/types.h>
#include <melee/ft/kinds/ftZelda/types.h>

typedef enum {
    ATTR_WORDS,
    ATTR_REFLECT,
    ATTR_SWORD,
    ATTR_COLORS,
    ATTR_KIRBY,
    ATTR_LINK,
    ATTR_PURIN,
} AttributeLayout;

typedef struct {
    const char* name;
    size_t disk_size;
    size_t host_size;
    AttributeLayout layout;
    size_t special_offset;
} AttributeSchema;

/* These schemas follow each fighter's types.h and LoadSpecialAttrs caller.
 * Clone fighters copy the same attribute type as the original fighter.
 * AbsorbDesc and ftCollisionBox contain only 32-bit scalars. ReflectDesc,
 * SwordAttrs, GXColor and Kirby's jump attributes have smaller fields. */
#define CHECK_SIZE(type, size)                                                \
    _Static_assert(sizeof(type) == size, #type " attribute size changed")
CHECK_SIZE(ftMario_DatAttrs, 0x84);
CHECK_SIZE(struct ftFox_DatAttrs, 0xD4);
CHECK_SIZE(struct ftCaptain_DatAttrs, 0x8C);
CHECK_SIZE(ftDonkeyAttributes, 0x74);
CHECK_SIZE(ftKoopaAttributes, 0xA0);
CHECK_SIZE(ftLuigiAttributes, 0x98);
CHECK_SIZE(ftSs_DatAttrs, 0xD4);
CHECK_SIZE(ftPe_DatAttrs, 0xC0);
CHECK_SIZE(ftPikachuAttributes, 0xF8);
CHECK_SIZE(ftIceClimberAttributes, 0x15C);
CHECK_SIZE(ftSeakAttributes, 0x74);
CHECK_SIZE(ftYoshiAttributes, 0x138);
CHECK_SIZE(ftZelda_DatAttrs, 0xA8);
CHECK_SIZE(MarsAttributes, 0x98);
CHECK_SIZE(ftMewtwoAttributes, 0x88);
CHECK_SIZE(ftNessAttributes, 0xDC);
CHECK_SIZE(ftGameWatchAttributes, 0x94);
CHECK_SIZE(struct ftKb_DatAttrs, 0x424);
CHECK_SIZE(struct ftMasterHand_SpecialAttrs, 0x17C);
CHECK_SIZE(ftCrazyHand_DatAttrs, 0x144);
_Static_assert(offsetof(struct ftYs_DatAttrs, xEC) == 0xEC,
               "Yoshi attribute views must use the same offsets");
#undef CHECK_SIZE

#define WORDS(name, type) { name, sizeof(type), sizeof(type), ATTR_WORDS, 0 }
#define REFLECT(name, type, member)                                           \
    { name, sizeof(type), sizeof(type), ATTR_REFLECT, offsetof(type, member) }
#define SPECIAL(name, type, layout, offset)                                   \
    { name, sizeof(type), sizeof(type), layout, offset }
static const AttributeSchema schemas[] = {
    REFLECT("Mario", ftMario_DatAttrs, cape_reflection),
    REFLECT("Drmario", ftMario_DatAttrs, cape_reflection),
    REFLECT("Fox", struct ftFox_DatAttrs, xB0_FOX_REFLECTOR_REFLECTION),
    REFLECT("Falco", struct ftFox_DatAttrs, xB0_FOX_REFLECTOR_REFLECTION),
    WORDS("Captain", struct ftCaptain_DatAttrs),
    WORDS("Ganon", struct ftCaptain_DatAttrs),
    WORDS("Donkey", ftDonkeyAttributes),
    WORDS("Koopa", ftKoopaAttributes),
    WORDS("Gkoopa", ftKoopaAttributes),
    WORDS("Luigi", ftLuigiAttributes),
    WORDS("Samus", ftSs_DatAttrs),
    WORDS("Peach", ftPe_DatAttrs),
    WORDS("Pikachu", ftPikachuAttributes),
    WORDS("Pichu", ftPikachuAttributes),
    WORDS("Popo", ftIceClimberAttributes),
    WORDS("Nana", ftIceClimberAttributes),
    WORDS("Seak", ftSeakAttributes),
    WORDS("Yoshi", ftYoshiAttributes),
    REFLECT("Zelda", ftZelda_DatAttrs, x84),
    REFLECT("Mewtwo", ftMewtwoAttributes, x1C_MEWTWO_CONFUSION_REFLECTION),
    REFLECT("Ness", ftNessAttributes, xB8_BASEBALL_BAT),
    SPECIAL("Mars", MarsAttributes, ATTR_SWORD, offsetof(MarsAttributes, x78)),
    SPECIAL("Emblem", MarsAttributes, ATTR_SWORD,
            offsetof(MarsAttributes, x78)),
    SPECIAL("Gamewatch", ftGameWatchAttributes, ATTR_COLORS, 0),
    SPECIAL("Kirby", struct ftKb_DatAttrs, ATTR_KIRBY, 0),
    WORDS("Masterhand", struct ftMasterHand_SpecialAttrs),
    WORDS("Crazyhand", ftCrazyHand_DatAttrs),
    WORDS("Boy", s32),
    WORDS("Girl", s32),
    /* ftsandbag.c declares its attributes as two u32 values. */
    WORDS("Sandbag", u32[2]),
    { "Link", 0xDC, sizeof(struct ftLk_DatAttrs), ATTR_LINK, 0 },
    { "Clink", 0xDC, sizeof(struct ftLk_DatAttrs), ATTR_LINK, 0 },
    { "Purin", 0x100, sizeof(ftPurinAttributes), ATTR_PURIN, 0 },
};
#undef WORDS
#undef REFLECT
#undef SPECIAL

typedef struct AttributeBlock {
    const AttributeSchema* schema;
    uint32_t offset;
    void* data;
    struct AttributeBlock* next;
} AttributeBlock;

struct NativeFighterAttributes {
    const NativeArchive* archive;
    AttributeBlock* blocks;
};

static void copy_words(void* output, const uint8_t* input, size_t size)
{
    uint8_t* bytes = output;
    for (size_t i = 0; i < size; i += 4) {
        uint32_t value = NativeArchiveBE32(input + i);
        memcpy(bytes + i, &value, 4);
    }
}

static void copy_reflect(ReflectDesc* output, const uint8_t* input)
{
    copy_words(output, input, 0x20);
    output->x20_behavior = input[0x20];
}

static void copy_sword(struct SwordAttrs* output, const uint8_t* input)
{
    copy_words(output, input, 8);
    memcpy(&output->x8, input + 8, 12);
    copy_words(&output->x14, input + 0x14, 12);
}

static void convert(const AttributeSchema* schema, const uint8_t* input,
                    void* output)
{
    if (schema->layout == ATTR_LINK) {
        struct ftLk_DatAttrs* link = output;
        copy_words(link, input, 0x94);
        copy_sword(&link->x64, input + 0x64);
        /* These unused UNK_T fields have integer data, no relocation, and
         * no dereference in game code. Preserve their numeric bits while
         * retaining the declared host layout. */
        link->x94 = (void*) (uintptr_t) NativeArchiveBE32(input + 0x94);
        copy_words(&link->x98, input + 0x98, 4);
        link->x9C = (void*) (uintptr_t) NativeArchiveBE32(input + 0x9C);
        link->xA0 = (void*) (uintptr_t) NativeArchiveBE32(input + 0xA0);
        copy_words(&link->xA4, input + 0xA4, 0x1C);
        memcpy(link->xC0_filler, input + 0xC0, 4);
        copy_words(&link->xC4, input + 0xC4, 0x18);
        return;
    }
    if (schema->layout == ATTR_PURIN) {
        ftPurinAttributes* purin = output;
        copy_words(purin, input, 0xE8);
        purin->xE8 = (void*) (uintptr_t) NativeArchiveBE32(input + 0xE8);
        purin->xEC = (void*) (uintptr_t) NativeArchiveBE32(input + 0xEC);
        copy_words(&purin->xF0, input + 0xF0, 8);
        memcpy(purin->_F8, input + 0xF8, 8);
        return;
    }
    copy_words(output, input, schema->disk_size);
    switch (schema->layout) {
    case ATTR_REFLECT:
        copy_reflect(
            (ReflectDesc*) ((uint8_t*) output + schema->special_offset),
            input + schema->special_offset);
        break;
    case ATTR_SWORD:
        copy_sword(
            (struct SwordAttrs*) ((uint8_t*) output + schema->special_offset),
            input + schema->special_offset);
        break;
    case ATTR_COLORS: {
        ftGameWatchAttributes* gamewatch = output;
        memcpy(gamewatch->x4_GAMEWATCH_COLOR, input + 4, 0x10);
        memcpy(&gamewatch->x14_GAMEWATCH_OUTLINE, input + 0x14, 4);
        break;
    }
    case ATTR_KIRBY: {
        struct ftKb_DatAttrs* kirby = output;
        kirby->jumpaerial_unk =
            (s16) ((uint16_t) input[0x34] << 8 | input[0x35]);
        copy_reflect(&kirby->specialn_zd_reflectdesc, input + 0x400);
        break;
    }
    default:
        break;
    }
}

NativeFighterAttributes*
NativeFighterAttributesOpen(const NativeArchive* archive,
                            NativeArchiveGraph* graph)
{
    NativeFighterAttributes* attributes;
    (void) graph;
    if (archive == NULL) {
        return NULL;
    }
    attributes = calloc(1, sizeof(*attributes));
    if (attributes != NULL) {
        attributes->archive = archive;
    }
    return attributes;
}

void NativeFighterAttributesClose(NativeFighterAttributes* attributes)
{
    if (attributes == NULL) {
        return;
    }
    while (attributes->blocks != NULL) {
        AttributeBlock* block = attributes->blocks;
        attributes->blocks = block->next;
        free(block->data);
        free(block);
    }
    free(attributes);
}

NativeArchiveStatus
NativeFighterAttributesRead(NativeFighterAttributes* attributes,
                            const char* symbol, uint32_t offset, void** output,
                            NativeArchiveError* error)
{
    const AttributeSchema* schema = NULL;
    AttributeBlock* block;
    const NativeArchive* archive;
    if (output != NULL) {
        *output = NULL;
    }
    if (attributes == NULL || symbol == NULL || output == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                                 "invalid fighter attribute request");
    }
    if (strncmp(symbol, "ftData", 6) != 0) {
        return NATIVE_ARCHIVE_NOT_FOUND;
    }
    for (size_t i = 0; i < sizeof(schemas) / sizeof(*schemas); ++i) {
        if (strcmp(symbol + 6, schemas[i].name) == 0) {
            schema = &schemas[i];
            break;
        }
    }
    if (schema == NULL) {
        return NATIVE_ARCHIVE_NOT_FOUND;
    }
    for (block = attributes->blocks; block != NULL; block = block->next) {
        if (block->offset == offset) {
            if (block->schema != schema &&
                (block->schema->disk_size != schema->disk_size ||
                 block->schema->host_size != schema->host_size ||
                 block->schema->layout != schema->layout ||
                 block->schema->special_offset != schema->special_offset))
            {
                return NativeArchiveFail(
                    error, NATIVE_ARCHIVE_TYPE_CONFLICT, 32u + offset,
                    "conflicting fighter attribute types");
            }
            *output = block->data;
            return NATIVE_ARCHIVE_OK;
        }
    }
    archive = attributes->archive;
    if ((offset & 3u) != 0 ||
        !NativeArchiveDataRange(archive, offset, schema->disk_size))
    {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 32u + offset,
                                 "fighter attributes exceed archive data");
    }
    /* Special attributes contain no serialized references in these schemas.
     * Reject a different layout instead of treating a pointer as a number. */
    for (uint32_t i = 0; i < archive->reloc_count; ++i) {
        uint32_t field = archive->relocations[i];
        if (field >= offset && field - offset < schema->disk_size) {
            return NativeArchiveFail(
                error, NATIVE_ARCHIVE_UNSUPPORTED, 32u + field,
                "fighter attribute pointer has no schema");
        }
    }
    for (size_t i = 0; i < schema->disk_size; i += 4) {
        size_t field = offset + i;
        if (archive->external_fields != NULL &&
            (archive->external_fields[field / 32] &
             (1u << ((field / 4) % 8))) != 0)
        {
            return NativeArchiveFail(
                error, NATIVE_ARCHIVE_UNSUPPORTED, 32u + field,
                "fighter attribute external has no schema");
        }
    }
    block = calloc(1, sizeof(*block));
    if (block != NULL) {
        block->data = calloc(1, schema->host_size);
    }
    if (block == NULL || block->data == NULL) {
        free(block);
        return NativeArchiveFail(error, NATIVE_ARCHIVE_NO_MEMORY, 32u + offset,
                                 "cannot allocate fighter attributes");
    }
    convert(schema, archive->data + offset, block->data);
    block->schema = schema;
    block->offset = offset;
    block->next = attributes->blocks;
    attributes->blocks = block;
    *output = block->data;
    return NATIVE_ARCHIVE_OK;
}
