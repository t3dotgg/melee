#include "fighters.h"

#include <stdlib.h>
#include <string.h>

#include "archive_internal.h"
#include "fighter_articles.h"
#include "fighter_attributes.h"
#include "fighter_common.h"
#include "fighter_parts.h"
#include <melee/ft/types.h>

typedef struct FighterAllocation {
    uint32_t offset;
    unsigned type;
    size_t size;
    void* data;
    struct FighterAllocation* next;
} FighterAllocation;

struct NativeFighterArchive {
    const NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeFighterParts* parts;
    NativeFighterAttributes* attributes;
    NativeFighterCommonArchive* common;
    NativeFighterArticles* articles;
    FighterAllocation* allocations;
    NativeArchiveError* error;
    NativeArchiveError failure;
};

enum {
    FIGHTER_ROOT,
    FIGHTER_BYTES,
    FIGHTER_WORDS,
    FIGHTER_ATTRIBUTES,
    FIGHTER_MOTIONS,
    FIGHTER_HURTBOXES,
    FIGHTER_SFX,
    FIGHTER_SFX_ARRAY,
    FIGHTER_COLLISION,
    FIGHTER_IK,
};

static void* fail(NativeFighterArchive* fighter, uint32_t offset,
                  const char* message)
{
    NativeArchiveFail(fighter->error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                      message);
    return NULL;
}

static bool range(NativeFighterArchive* fighter, uint32_t offset, size_t size)
{
    if (NativeArchiveDataRange(fighter->archive, offset, size)) {
        return true;
    }
    NativeArchiveFail(fighter->error, NATIVE_ARCHIVE_BOUNDS, 32u + offset,
                      "fighter record exceeds archive data");
    return false;
}

static void* cached(NativeFighterArchive* fighter, uint32_t offset,
                    unsigned type)
{
    FighterAllocation* allocation;
    for (allocation = fighter->allocations; allocation != NULL;
         allocation = allocation->next)
    {
        if (allocation->offset == offset && allocation->type == type) {
            return allocation->data;
        }
    }
    return NULL;
}

static void* allocate(NativeFighterArchive* fighter, uint32_t offset,
                      unsigned type, size_t size)
{
    FighterAllocation* allocation = calloc(1, sizeof(*allocation));
    void* data = calloc(1, size == 0 ? 1 : size);
    if (data == NULL || allocation == NULL) {
        free(data);
        free(allocation);
        NativeArchiveFail(fighter->error, NATIVE_ARCHIVE_NO_MEMORY, 0,
                          "cannot allocate fighter record");
        return NULL;
    }
    allocation->offset = offset;
    allocation->type = type;
    allocation->size = size;
    allocation->data = data;
    allocation->next = fighter->allocations;
    fighter->allocations = allocation;
    return data;
}

static uint32_t word(NativeFighterArchive* fighter, uint32_t offset)
{
    return NativeArchiveBE32(fighter->archive->data + offset);
}

static bool reference(NativeFighterArchive* fighter, uint32_t field,
                      uint32_t* target, bool* present)
{
    if (range(fighter, field, 4) &&
        fighter->archive->external_fields != NULL &&
        (fighter->archive->external_fields[field / 32] &
         (1u << ((field / 4) % 8))))
    {
        *target = 0;
        *present = false;
        return true;
    }
    return NativeArchiveReference(fighter->archive, field, target, present,
                                  fighter->error) == NATIVE_ARCHIVE_OK;
}

/* Record starts bound only schemas whose element width is known below. */
static size_t span(NativeFighterArchive* fighter, uint32_t offset)
{
    uint32_t end = fighter->archive->data_size;
    if (offset >= end) {
        range(fighter, offset, 1);
        return 0;
    }
    for (size_t i = 0; i < fighter->archive->reloc_count; ++i) {
        uint32_t target = word(fighter, fighter->archive->relocations[i]);
        if (target > offset && target < end) {
            end = target;
        }
    }
    for (size_t i = 0; i < fighter->archive->public_count; ++i) {
        NativeArchiveSymbol symbol;
        if (NativeArchivePublic(fighter->archive, i, &symbol,
                                fighter->error) != NATIVE_ARCHIVE_OK)
        {
            return 0;
        }
        if (symbol.offset > offset && symbol.offset < end) {
            end = symbol.offset;
        }
    }
    return end - offset;
}

static void swap_words(void* output, const u8* input, size_t size)
{
    for (size_t i = 0; i < size; i += 4) {
        u32 value = NativeArchiveBE32(input + i);
        memcpy((u8*) output + i, &value, 4);
    }
}

static void* words(NativeFighterArchive* fighter, uint32_t offset, size_t size)
{
    void* output = cached(fighter, offset, FIGHTER_WORDS);
    if (output != NULL) {
        return output;
    }
    if ((size & 3u) != 0 || !range(fighter, offset, size)) {
        return fail(fighter, offset, "fighter scalar array has invalid size");
    }
    output = allocate(fighter, offset, FIGHTER_WORDS, size);
    if (output != NULL) {
        swap_words(output, fighter->archive->data + offset, size);
    }
    return output;
}

static void* bytes(NativeFighterArchive* fighter, uint32_t offset, size_t size)
{
    void* output = cached(fighter, offset, FIGHTER_BYTES);
    if (output != NULL) {
        return output;
    }
    if (!range(fighter, offset, size)) {
        return NULL;
    }
    output = allocate(fighter, offset, FIGHTER_BYTES, size);
    if (output != NULL) {
        memcpy(output, fighter->archive->data + offset, size);
    }
    return output;
}

static char* string(NativeFighterArchive* fighter, uint32_t offset)
{
    const u8* end;
    if (!range(fighter, offset, 1)) {
        return NULL;
    }
    end = memchr(fighter->archive->data + offset, 0,
                 fighter->archive->data_size - offset);
    if (end == NULL) {
        return fail(fighter, offset, "unterminated fighter animation name");
    }
    return bytes(fighter, offset, end - (fighter->archive->data + offset) + 1);
}

static void* motions(NativeFighterArchive* fighter, uint32_t offset)
{
    struct Fighter_WaitAnimData* output =
        cached(fighter, offset, FIGHTER_MOTIONS);
    size_t size = span(fighter, offset);
    if (output != NULL) {
        return output;
    }
    if (size == 0 || size % 24 != 0 || size / 24 > SIZE_MAX / sizeof(*output))
    {
        return fail(fighter, offset, "invalid fighter animation table size");
    }
    output = allocate(fighter, offset, FIGHTER_MOTIONS,
                      size / 24 * sizeof(*output));
    if (output == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < size / 24; ++i) {
        uint32_t field = offset + i * 24;
        uint32_t target;
        bool present;
        if (!reference(fighter, field, &target, &present)) {
            return NULL;
        }
        if (present && (output[i].x0 = string(fighter, target)) == NULL) {
            return NULL;
        }
        output[i].x4 = word(fighter, field + 4);
        output[i].x8 = word(fighter, field + 8);
        if (!reference(fighter, field + 12, &target, &present)) {
            return NULL;
        }
        /* Branch commands resolve relocation fields in the owning archive.
         * Keep the original BE bytes and their address for that resolver. */
        if (present) {
            output[i].xC = (void*) (fighter->archive->data + target);
        }
        output[i].x10_animCurrFlags = word(fighter, field + 16);
        if (word(fighter, field + 20) != 0) {
            return fail(fighter, field + 20,
                        "fighter animation has a serialized runtime pointer");
        }
    }
    return output;
}

static void* common_attributes(NativeFighterArchive* fighter, uint32_t offset)
{
    ftCo_DatAttrs* output = cached(fighter, offset, FIGHTER_ATTRIBUTES);
    if (output != NULL) {
        return output;
    }
    if (!range(fighter, offset, 0x184)) {
        return NULL;
    }
    output = allocate(fighter, offset, FIGHTER_ATTRIBUTES, sizeof(*output));
    if (output != NULL) {
        swap_words(output, fighter->archive->data + offset, 0x180);
        output->weight_independent_throws_mask =
            fighter->archive->data[offset + 0x180];
    }
    return output;
}

static void* hurtboxes(NativeFighterArchive* fighter, uint32_t offset)
{
    struct ftData_x30* output = cached(fighter, offset, FIGHTER_HURTBOXES);
    uint32_t target;
    bool present;
    if (output != NULL) {
        return output;
    }
    if (!range(fighter, offset, 8)) {
        return NULL;
    }
    output = allocate(fighter, offset, FIGHTER_HURTBOXES, sizeof(*output));
    if (output == NULL) {
        return NULL;
    }
    output->count = word(fighter, offset);
    if (output->count < 0 ||
        !reference(fighter, offset + 4, &target, &present))
    {
        return fail(fighter, offset, "invalid fighter hurtbox count");
    }
    if (output->count != 0) {
        if (!present || (size_t) output->count > SIZE_MAX / 40) {
            return fail(fighter, offset, "fighter hurtbox array is missing");
        }
        output->inits = words(fighter, target, (size_t) output->count * 40);
        if (output->inits == NULL) {
            return NULL;
        }
    }
    return output;
}

static void* sfx_array(NativeFighterArchive* fighter, uint32_t offset)
{
    FtSFXArr* output = cached(fighter, offset, FIGHTER_SFX_ARRAY);
    uint32_t target;
    bool present;
    if (output != NULL) {
        return output;
    }
    if (!range(fighter, offset, 8)) {
        return NULL;
    }
    output = allocate(fighter, offset, FIGHTER_SFX_ARRAY, sizeof(*output));
    if (output == NULL) {
        return NULL;
    }
    output->num = word(fighter, offset);
    if (output->num < 0 || !reference(fighter, offset + 4, &target, &present))
    {
        return fail(fighter, offset, "invalid fighter sound count");
    }
    if (output->num != 0) {
        if (!present || (size_t) output->num > SIZE_MAX / 4) {
            return fail(fighter, offset, "fighter sound array is missing");
        }
        output->sfx_ids = words(fighter, target, (size_t) output->num * 4);
        if (output->sfx_ids == NULL) {
            return NULL;
        }
    }
    return output;
}

static void* sfx(NativeFighterArchive* fighter, uint32_t offset)
{
    FtSFX* output = cached(fighter, offset, FIGHTER_SFX);
    uint32_t target;
    bool present;
    if (output != NULL) {
        return output;
    }
    if (!range(fighter, offset, 0x38)) {
        return NULL;
    }
    output = allocate(fighter, offset, FIGHTER_SFX, sizeof(*output));
    if (output == NULL) {
        return NULL;
    }
#define ARRAY(field, at)                                                      \
    if (!reference(fighter, offset + (at), &target, &present)) {              \
        return NULL;                                                          \
    }                                                                         \
    if (present && (output->field = sfx_array(fighter, target)) == NULL) {    \
        return NULL;                                                          \
    }
    ARRAY(smash, 0);
    ARRAY(x1C, 0x1C);
    ARRAY(x20, 0x20);
#undef ARRAY
    swap_words(&output->x4, fighter->archive->data + offset + 4, 0x18);
    swap_words(&output->x24, fighter->archive->data + offset + 0x24, 0x14);
    return output;
}

static void* collision(NativeFighterArchive* fighter, uint32_t offset)
{
    ftData_x44_t* output = cached(fighter, offset, FIGHTER_COLLISION);
    if (output != NULL) {
        return output;
    }
    if (!range(fighter, offset, 0x1C)) {
        return NULL;
    }
    output = allocate(fighter, offset, FIGHTER_COLLISION, sizeof(*output));
    if (output != NULL) {
        for (size_t i = 0; i < 12; i += 2) {
            const u8* data = fighter->archive->data + offset + i;
            u16 value = (u16) data[0] << 8 | data[1];
            memcpy((u8*) output + i, &value, 2);
        }
        swap_words(&output->unkC, fighter->archive->data + offset + 12, 16);
    }
    return output;
}

static void* ik(NativeFighterArchive* fighter, uint32_t offset)
{
    ftData_x58_t* output = cached(fighter, offset, FIGHTER_IK);
    if (output != NULL) {
        return output;
    }
    if (!range(fighter, offset, 0x1C)) {
        return NULL;
    }
    output = allocate(fighter, offset, FIGHTER_IK, sizeof(*output));
    if (output != NULL) {
        memcpy(output, fighter->archive->data + offset, 0x1C);
        swap_words(&output->x4, fighter->archive->data + offset + 4, 4);
        swap_words(&output->xC, fighter->archive->data + offset + 12, 4);
        swap_words(&output->x18, fighter->archive->data + offset + 24, 4);
    }
    return output;
}

static void* fighter_data(NativeFighterArchive* fighter, const char* symbol,
                          uint32_t offset)
{
    ftData* output = cached(fighter, offset, FIGHTER_ROOT);
    uint32_t target;
    bool present;
    if (output != NULL) {
        return output;
    }
    if (!range(fighter, offset, 0x60)) {
        return NULL;
    }
    output = allocate(fighter, offset, FIGHTER_ROOT, sizeof(*output));
    if (output == NULL) {
        return NULL;
    }
#define FIELD(field, at, expression)                                          \
    if (!reference(fighter, offset + (at), &target, &present)) {              \
        return NULL;                                                          \
    }                                                                         \
    if (present && (output->field = (expression)) == NULL) {                  \
        return NULL;                                                          \
    }
#define READER(field, at, call)                                               \
    if (!reference(fighter, offset + (at), &target, &present)) {              \
        return NULL;                                                          \
    }                                                                         \
    if (present && (call) != NATIVE_ARCHIVE_OK) {                             \
        return NULL;                                                          \
    }
    FIELD(x0, 0, common_attributes(fighter, target));
    READER(ext_attr, 4,
           NativeFighterAttributesRead(fighter->attributes, symbol, target,
                                       &output->ext_attr, fighter->error));
    READER(x8, 8,
           NativeFighterPartsRead(fighter->parts, NATIVE_FIGHTER_PARTS_MODELS,
                                  target, (void**) &output->x8,
                                  fighter->error));
    FIELD(xC, 12, motions(fighter, target));
    FIELD(x10, 16, bytes(fighter, target, span(fighter, target)));
    FIELD(x14, 20, motions(fighter, target));
    FIELD(x18, 24, bytes(fighter, target, span(fighter, target)));
    READER(x1C, 28,
           NativeFighterPartsRead(fighter->parts,
                                  NATIVE_FIGHTER_PARTS_ANIMATIONS, target,
                                  (void**) &output->x1C, fighter->error));
    READER(x20, 32,
           NativeFighterPartsRead(fighter->parts, NATIVE_FIGHTER_PARTS_SHIELD,
                                  target, (void**) &output->x20,
                                  fighter->error));
    FIELD(x24, 36, words(fighter, target, span(fighter, target)));
    FIELD(x28, 40, words(fighter, target, span(fighter, target)));
    READER(x2C, 44,
           NativeFighterPartsRead(fighter->parts,
                                  NATIVE_FIGHTER_PARTS_DYNAMICS, target,
                                  (void**) &output->x2C, fighter->error));
    FIELD(x30, 48, hurtboxes(fighter, target));
    FIELD(x34, 52, words(fighter, target, 8));
    FIELD(x38, 56, words(fighter, target, 40));
    FIELD(x3C, 60, words(fighter, target, 24));
    FIELD(x40, 64, words(fighter, target, 48));
    FIELD(x44, 68, collision(fighter, target));
    READER(x48_items, 72,
           NativeFighterArticlesRead(fighter->articles, symbol, target,
                                     &output->x48_items, fighter->error));
    FIELD(x4C_sfx, 76, sfx(fighter, target));
    FIELD(x50, 80, words(fighter, target, 8));
    FIELD(x54, 84, words(fighter, target, span(fighter, target)));
    FIELD(x58, 88, ik(fighter, target));
    READER(x5C, 92,
           NativeArchiveJoint(fighter->graph, target, &output->x5C,
                              fighter->error));
#undef FIELD
#undef READER
    return output;
}

NativeFighterArchive* NativeFighterArchiveOpen(const NativeArchive* archive,
                                               NativeArchiveGraph* graph,
                                               struct NativeItemArchive* items)
{
    NativeFighterArchive* fighter = calloc(1, sizeof(*fighter));
    NativeArchiveError error;
    if (fighter == NULL) {
        return NULL;
    }
    fighter->archive = archive;
    fighter->graph = graph;
    fighter->attributes = NativeFighterAttributesOpen(archive, graph);
    fighter->common = NativeFighterCommonArchiveOpen(archive, graph);
    if (NativeFighterPartsOpen(archive, graph, &fighter->parts, &error) !=
        NATIVE_ARCHIVE_OK)
    {
        NativeFighterArchiveClose(fighter);
        return NULL;
    }
    fighter->articles =
        NativeFighterArticlesOpen(archive, graph, items, fighter->parts);
    if (fighter->attributes == NULL || fighter->common == NULL ||
        fighter->articles == NULL)
    {
        NativeFighterArchiveClose(fighter);
        return NULL;
    }
    return fighter;
}

void NativeFighterArchiveClose(NativeFighterArchive* fighter)
{
    if (fighter == NULL) {
        return;
    }
    NativeFighterPartsClose(fighter->parts);
    NativeFighterAttributesClose(fighter->attributes);
    NativeFighterCommonArchiveClose(fighter->common);
    NativeFighterArticlesClose(fighter->articles);
    while (fighter->allocations != NULL) {
        FighterAllocation* next = fighter->allocations->next;
        free(fighter->allocations->data);
        free(fighter->allocations);
        fighter->allocations = next;
    }
    free(fighter);
}

NativeArchiveStatus NativeFighterArchiveRead(NativeFighterArchive* fighter,
                                             const char* symbol,
                                             uint32_t offset, void** output,
                                             NativeArchiveError* error)
{
    NativeArchiveError local_error;
    if (fighter == NULL || symbol == NULL || output == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "invalid fighter archive request");
    }
    *output = NULL;
    if (fighter->failure.status != NATIVE_ARCHIVE_OK) {
        if (error != NULL) {
            *error = fighter->failure;
        }
        return fighter->failure.status;
    }
    fighter->error = error == NULL ? &local_error : error;
    fighter->error->status = NATIVE_ARCHIVE_OK;
    fighter->error->offset = 0;
    fighter->error->message = NULL;
    if (strncmp(symbol, "ftData", 6) != 0) {
        return NativeFighterCommonArchiveRead(fighter->common, symbol, offset,
                                              output, fighter->error);
    }
    *output = fighter_data(fighter, symbol, offset);
    if (*output == NULL) {
        if (fighter->error->status == NATIVE_ARCHIVE_OK) {
            NativeArchiveFail(fighter->error, NATIVE_ARCHIVE_INVALID,
                              32u + offset,
                              "fighter conversion returned no data");
        }
        fighter->failure = *fighter->error;
        return fighter->error->status;
    }
    return NATIVE_ARCHIVE_OK;
}
