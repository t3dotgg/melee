#include "fighter_articles.h"

#include <melee/it/forward.h>

#include <stdlib.h>
#include <string.h>

#include "archive_internal.h"
#include <melee/ft/kinds/ftSamus/types.h>
#include <melee/ft/types.h>

/* OnLoad registers article slots by item kind. The remaining slots have
 * separate consumers in ftParts, ftSamus, ftPurin and the special moves. */
enum SlotKind {
    SLOT_UNUSED = -1,
    SLOT_JOINT = -2,
    SLOT_SAMUS_BEAM = -3,
    SLOT_FOX_WORDS = -4,
    SLOT_VISIBILITY = -5,
    SLOT_PURIN_PARTS = -6,
};

typedef struct FighterSlots {
    const char* symbol;
    unsigned count;
    int kind[11];
} FighterSlots;

static const FighterSlots fighter_slots[] = {
    { "ftDataMario",
      4,
      { It_Kind_Mario_Fire, SLOT_UNUSED, It_Kind_Mario_Cape, SLOT_UNUSED } },
    { "ftDataDrmario",
      4,
      { SLOT_UNUSED, It_Kind_DrMario_Vitamin, SLOT_UNUSED,
        It_Kind_DrMario_Sheet } },
    { "ftDataFox",
      5,
      { It_Kind_Fox_Laser, It_Kind_Fox_Blaster, It_Kind_Fox_Illusion,
        SLOT_UNUSED, SLOT_FOX_WORDS } },
    { "ftDataFalco",
      5,
      { It_Kind_Falco_Laser, It_Kind_Falco_Blaster, SLOT_UNUSED,
        It_Kind_Falco_Phantasm, SLOT_UNUSED } },
    { "ftDataLink",
      7,
      { It_Kind_Link_Bomb, It_Kind_Link_Boomerang, It_Kind_Link_HShot,
        It_Kind_Link_Arrow, It_Kind_Link_Bow, SLOT_UNUSED, SLOT_JOINT } },
    { "ftDataClink",
      7,
      { It_Kind_CLink_Bomb, It_Kind_CLink_Boomerang, It_Kind_CLink_HShot,
        It_Kind_CLink_Arrow, It_Kind_CLink_Bow, It_Kind_CLink_Milk,
        SLOT_JOINT } },
    { "ftDataKirby",
      5,
      { It_Kind_Kirby_CBeam, It_Kind_Kirby_Hammer, It_Kind_Unk1, It_Kind_Unk2,
        SLOT_JOINT } },
    { "ftDataSamus",
      5,
      { It_Kind_Samus_Bomb, It_Kind_Samus_Charge, It_Kind_Samus_Missile,
        It_Kind_Samus_GBeam, SLOT_SAMUS_BEAM } },
    { "ftDataPikachu",
      3,
      { It_Kind_Pikachu_Thunder, It_Kind_Pikachu_TJolt_Ground,
        It_Kind_Pikachu_TJolt_Air } },
    { "ftDataPichu",
      3,
      { It_Kind_Pichu_Thunder, It_Kind_Pichu_TJolt_Ground,
        It_Kind_Pichu_TJolt_Air } },
    { "ftDataNess",
      11,
      { It_Kind_Ness_PKFire, It_Kind_Ness_PKFire_Flame, It_Kind_Ness_PKFlush,
        It_Kind_Ness_PKThunder, It_Kind_Ness_PKThunder1,
        It_Kind_Ness_PKThunder2, It_Kind_Ness_PKThunder3,
        It_Kind_Ness_PKThunder4, It_Kind_Ness_PKFlush_Explode,
        It_Kind_Ness_Bat, It_Kind_Ness_Yoyo } },
    { "ftDataPeach",
      5,
      { It_Kind_Peach_Explode, It_Kind_Peach_Turnip, It_Kind_Peach_Parasol,
        It_Kind_Peach_Toad, It_Kind_Peach_ToadSpore } },
    { "ftDataSeak",
      6,
      { It_Kind_Seak_NeedleThrow, It_Kind_Seak_NeedleHeld, It_Kind_Seak_Vanish,
        It_Kind_Seak_Chain, SLOT_JOINT, SLOT_JOINT } },
    { "ftDataYoshi",
      4,
      { It_Kind_Yoshi_EggThrow, It_Kind_Yoshi_Star, It_Kind_Yoshi_EggLay,
        SLOT_JOINT } },
    { "ftDataGamewatch",
      11,
      { It_Kind_GameWatch_Greenhouse, It_Kind_GameWatch_Manhole,
        It_Kind_GameWatch_Fire, It_Kind_GameWatch_Parachute,
        It_Kind_GameWatch_Turtle, It_Kind_GameWatch_Breath,
        It_Kind_GameWatch_Judge, It_Kind_GameWatch_Panic,
        It_Kind_GameWatch_Chef, It_Kind_GameWatch_Rescue, SLOT_VISIBILITY } },
    { "ftDataPurin", 2, { SLOT_UNUSED, SLOT_PURIN_PARTS } },
    { "ftDataLuigi", 1, { It_Kind_Luigi_Fire } },
    { "ftDataKoopa", 1, { It_Kind_Koopa_Flame } },
    { "ftDataGkoopa", 1, { It_Kind_Koopa_Flame } },
    { "ftDataPopo",
      3,
      { It_Kind_IceClimber_Ice, It_Kind_IceClimber_Blizzard,
        It_Kind_IceClimber_GumStrings } },
    { "ftDataNana",
      3,
      { It_Kind_IceClimber_Ice, It_Kind_IceClimber_Blizzard,
        It_Kind_IceClimber_GumStrings } },
    { "ftDataMewtwo",
      2,
      { It_Kind_Mewtwo_Disable, It_Kind_Mewtwo_ShadowBall } },
    { "ftDataZelda",
      2,
      { It_Kind_Zelda_DinFire, It_Kind_Zelda_DinFire_Explode } },
    { "ftDataCrazyhand",
      3,
      { It_Kind_CrazyHand_Laser, It_Kind_CrazyHand_Bullet,
        It_Kind_CrazyHand_Bomb } },
};

typedef struct ArticleAllocation {
    void* data;
    struct ArticleAllocation* next;
} ArticleAllocation;

struct NativeFighterArticles {
    const NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeItemArchive* items;
    NativeFighterParts* parts;
    ArticleAllocation* allocations;
    const FighterSlots* schema;
    uint32_t offset;
    void** result;
    bool failed;
};

NativeFighterArticles* NativeFighterArticlesOpen(const NativeArchive* archive,
                                                 NativeArchiveGraph* graph,
                                                 NativeItemArchive* items,
                                                 NativeFighterParts* parts)
{
    NativeFighterArticles* articles;
    if (archive == NULL || graph == NULL || items == NULL || parts == NULL) {
        return NULL;
    }
    articles = calloc(1, sizeof(*articles));
    if (articles != NULL) {
        articles->archive = archive;
        articles->graph = graph;
        articles->items = items;
        articles->parts = parts;
    }
    return articles;
}

void NativeFighterArticlesClose(NativeFighterArticles* articles)
{
    ArticleAllocation* allocation;
    if (articles == NULL) {
        return;
    }
    while ((allocation = articles->allocations) != NULL) {
        articles->allocations = allocation->next;
        free(allocation->data);
        free(allocation);
    }
    free(articles);
}

static void* allocate(NativeFighterArticles* articles, size_t size,
                      NativeArchiveError* error)
{
    ArticleAllocation* allocation = calloc(1, sizeof(*allocation));
    if (allocation != NULL) {
        allocation->data = calloc(1, size == 0 ? 1 : size);
        if (allocation->data != NULL) {
            allocation->next = articles->allocations;
            articles->allocations = allocation;
            return allocation->data;
        }
        free(allocation);
    }
    NativeArchiveFail(error, NATIVE_ARCHIVE_NO_MEMORY, 0,
                      "cannot allocate fighter article data");
    return NULL;
}

static bool range(NativeFighterArticles* articles, uint32_t offset,
                  size_t size, NativeArchiveError* error)
{
    if (NativeArchiveDataRange(articles->archive, offset, size)) {
        return true;
    }
    NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 32u + offset,
                      "fighter article data exceeds archive bounds");
    return false;
}

static uint32_t word(NativeFighterArticles* articles, uint32_t offset)
{
    return NativeArchiveBE32(articles->archive->data + offset);
}

static bool reference(NativeFighterArticles* articles, uint32_t offset,
                      uint32_t* target, bool* present,
                      NativeArchiveError* error)
{
    return NativeArchiveReference(articles->archive, offset, target, present,
                                  error) == NATIVE_ARCHIVE_OK;
}

/* DAT has no length field for these arrays. All their references start at an
 * object boundary, including the last array before the public root. */
static size_t span(NativeFighterArticles* articles, uint32_t offset)
{
    const NativeArchive* archive = articles->archive;
    uint32_t end = archive->data_size;
    if (offset >= end) {
        return 0;
    }
    for (size_t i = 0; i < archive->reloc_count; i++) {
        uint32_t target = word(articles, archive->relocations[i]);
        if (target > offset && target < end) {
            end = target;
        }
    }
    for (size_t i = 0; i < archive->public_count; i++) {
        NativeArchiveSymbol symbol;
        if (NativeArchivePublic(archive, i, &symbol, NULL) ==
                NATIVE_ARCHIVE_OK &&
            symbol.offset > offset && symbol.offset < end)
        {
            end = symbol.offset;
        }
    }
    return end - offset;
}

static void* purin_parts(NativeFighterArticles* articles, uint32_t offset,
                         NativeArchiveError* error)
{
    /* ftPr_Init_8013C360 skips one pointer slot before reading FtPartsDesc. */
    struct PurinParts {
        void* reserved;
        FtPartsDesc parts;
    };
    struct PurinParts* result;
    void* parts;
    if (!range(articles, offset, 12, error)) {
        return NULL;
    }
    if (word(articles, offset) != 0) {
        NativeArchiveFail(error, NATIVE_ARCHIVE_UNSUPPORTED, 32u + offset,
                          "unsupported Purin part prefix");
        return NULL;
    }
    result = allocate(articles, sizeof(*result), error);
    if (result == NULL) {
        return NULL;
    }
    if (NativeFighterPartsRead(articles->parts,
                               NATIVE_FIGHTER_PARTS_DESCRIPTION, offset + 4,
                               &parts, error) != NATIVE_ARCHIVE_OK)
    {
        return NULL;
    }
    result->parts = *(FtPartsDesc*) parts;
    return result;
}

static void* samus_beam(NativeFighterArticles* articles, uint32_t offset,
                        NativeArchiveError* error)
{
    struct UNK_SAMUS_S1* beam;
    if (!range(articles, offset, 16, error)) {
        return NULL;
    }
    beam = allocate(articles, sizeof(*beam), error);
    if (beam == NULL) {
        return NULL;
    }
    for (unsigned i = 0; i < 4; i++) {
        uint32_t target;
        bool present;
        if (!reference(articles, offset + i * 4, &target, &present, error)) {
            return NULL;
        }
        if (!present) {
            continue;
        }
        switch (i) {
        case 0:
            if (NativeArchiveJoint(articles->graph, target, &beam->x0_joint,
                                   error) != NATIVE_ARCHIVE_OK)
            {
                return NULL;
            }
            break;
        case 1:
            /* The four entries are indexed by ThrowF through ThrowLw. */
            if (!range(articles, target, 16, error)) {
                return NULL;
            }
            beam->x4_anim_joints =
                allocate(articles, 4 * sizeof(*beam->x4_anim_joints), error);
            if (beam->x4_anim_joints == NULL) {
                return NULL;
            }
            for (unsigned j = 0; j < 4; j++) {
                uint32_t animation;
                if (!reference(articles, target + j * 4, &animation, &present,
                               error))
                {
                    return NULL;
                }
                if (present &&
                    NativeArchiveAnimation(articles->graph, animation,
                                           &beam->x4_anim_joints[j],
                                           error) != NATIVE_ARCHIVE_OK)
                {
                    return NULL;
                }
            }
            break;
        case 2:
            if (NativeArchiveAnimation(articles->graph, target,
                                       &beam->x8_anim_joint,
                                       error) != NATIVE_ARCHIVE_OK)
            {
                return NULL;
            }
            break;
        case 3:
            if (NativeArchiveMatAnimJoint(articles->graph, target,
                                          &beam->xC_matanim_joint,
                                          error) != NATIVE_ARCHIVE_OK)
            {
                return NULL;
            }
            break;
        }
    }
    return beam;
}

static void* read_slot(NativeFighterArticles* articles, int kind,
                       uint32_t offset, NativeArchiveError* error)
{
    if (kind >= 0) {
        struct Article* result;
        if (NativeItemArchiveArticle(articles->items, kind, offset, &result,
                                     error) != NATIVE_ARCHIVE_OK)
        {
            return NULL;
        }
        return result;
    }
    switch (kind) {
    case SLOT_JOINT: {
        struct HSD_Joint* result;
        if (NativeArchiveJoint(articles->graph, offset, &result, error) !=
            NATIVE_ARCHIVE_OK)
        {
            return NULL;
        }
        return result;
    }
    case SLOT_SAMUS_BEAM:
        return samus_beam(articles, offset, error);
    case SLOT_PURIN_PARTS:
        return purin_parts(articles, offset, error);
    case SLOT_VISIBILITY: {
        size_t size = span(articles, offset);
        if (size == 0 || size % 8 != 0 || size / 8 > 11) {
            NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                              "invalid fighter part visibility array");
            return NULL;
        }
        FtPartsVisLookup* result;
        if (NativeFighterPartsVisibility(articles->parts, offset, size / 8,
                                         &result, error) != NATIVE_ARCHIVE_OK)
        {
            return NULL;
        }
        return result;
    }
    case SLOT_FOX_WORDS: {
        uint32_t* result;
        if (!range(articles, offset, 24, error)) {
            return NULL;
        }
        result = allocate(articles, 24, error);
        if (result == NULL) {
            return NULL;
        }
        for (unsigned i = 0; i < 6; i++) {
            uint32_t field = offset + i * 4;
            for (size_t j = 0; j < articles->archive->reloc_count; j++) {
                if (articles->archive->relocations[j] == field) {
                    NativeArchiveFail(
                        error, NATIVE_ARCHIVE_UNSUPPORTED, 32u + field,
                        "Fox scalar article contains a reference");
                    return NULL;
                }
            }
            result[i] = word(articles, field);
        }
        return result;
    }
    default:
        NativeArchiveFail(error, NATIVE_ARCHIVE_UNSUPPORTED, 32u + offset,
                          "unexpected data in an unused fighter article slot");
        return NULL;
    }
}

NativeArchiveStatus NativeFighterArticlesRead(NativeFighterArticles* articles,
                                              const char* symbol,
                                              uint32_t offset, void*** output,
                                              NativeArchiveError* error)
{
    const FighterSlots* schema = NULL;
    NativeArchiveError local_error = { NATIVE_ARCHIVE_OK, 0, NULL };
    if (output != NULL) {
        *output = NULL;
    }
    if (articles == NULL || symbol == NULL || output == NULL ||
        articles->failed)
    {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                                 "invalid fighter article context");
    }
    if (error == NULL) {
        error = &local_error;
    }
    *error = local_error;
    for (size_t i = 0; i < sizeof(fighter_slots) / sizeof(*fighter_slots); i++)
    {
        if (strcmp(symbol, fighter_slots[i].symbol) == 0) {
            schema = &fighter_slots[i];
            break;
        }
    }
    if (schema == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_UNSUPPORTED,
                                 32u + offset,
                                 "unsupported fighter article list");
    }
    if (articles->result != NULL) {
        if (articles->schema != schema || articles->offset != offset) {
            return NativeArchiveFail(
                error, NATIVE_ARCHIVE_TYPE_CONFLICT, 32u + offset,
                "fighter article context already has a different root");
        }
        *output = articles->result;
        return NATIVE_ARCHIVE_OK;
    }
    if (!range(articles, offset, schema->count * 4, error)) {
        goto failed;
    }
    void** result = allocate(articles, schema->count * sizeof(*result), error);
    if (result == NULL) {
        goto failed;
    }
    for (unsigned i = 0; i < schema->count; i++) {
        uint32_t target;
        bool present;
        if (!reference(articles, offset + i * 4, &target, &present, error)) {
            goto failed;
        }
        if (present) {
            result[i] = read_slot(articles, schema->kind[i], target, error);
            if (result[i] == NULL) {
                goto failed;
            }
        }
    }
    articles->schema = schema;
    articles->offset = offset;
    articles->result = result;
    *output = result;
    return NATIVE_ARCHIVE_OK;
failed:
    articles->failed = true;
    if (error->status == NATIVE_ARCHIVE_OK) {
        NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                          "invalid fighter article data");
    }
    return error->status;
}
