#include "events.h"

#include <melee/it/forward.h>

#include <stdlib.h>
#include <string.h>

#include "archive_internal.h"
#include <melee/gm/types.h>

/* gm_801BEBC0 and gm_801BEBF8 scan all 51 event levels. */
enum {
    EVENT_LEVELS = 51
};

typedef enum EventType {
    EVENT_LEVEL,
    EVENT_INIT,
    EVENT_PLAYER,
    EVENT_STAGE,
    EVENT_BONUS,
    EVENT_EXTRA,
} EventType;

typedef struct EventAllocation {
    uint32_t offset;
    EventType type;
    void* data;
    struct EventAllocation* next;
} EventAllocation;

struct NativeEventArchive {
    const NativeArchive* archive;
    EventAllocation* allocations;
    struct gm_804D6900_t** levels;
    uint32_t root_offset;
    NativeArchiveError* error;
    bool failed;
};

static void* fail(NativeEventArchive* events, uint32_t offset,
                  const char* message)
{
    NativeArchiveFail(events->error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                      message);
    return NULL;
}

static bool range(NativeEventArchive* events, uint32_t offset, size_t size)
{
    if (NativeArchiveDataRange(events->archive, offset, size)) {
        return true;
    }
    NativeArchiveFail(events->error, NATIVE_ARCHIVE_BOUNDS, 32u + offset,
                      "event record exceeds archive data");
    return false;
}

static void* cached(NativeEventArchive* events, uint32_t offset,
                    EventType type)
{
    EventAllocation* allocation;
    for (allocation = events->allocations; allocation != NULL;
         allocation = allocation->next)
    {
        if (allocation->offset == offset && allocation->type == type) {
            return allocation->data;
        }
    }
    return NULL;
}

static void* allocate(NativeEventArchive* events, uint32_t offset,
                      EventType type, size_t size)
{
    EventAllocation* allocation = malloc(sizeof(*allocation));
    void* data = calloc(1, size);
    if (allocation == NULL || data == NULL) {
        free(allocation);
        free(data);
        NativeArchiveFail(events->error, NATIVE_ARCHIVE_NO_MEMORY,
                          32u + offset, "cannot allocate event record");
        return NULL;
    }
    allocation->offset = offset;
    allocation->type = type;
    allocation->data = data;
    allocation->next = events->allocations;
    events->allocations = allocation;
    return data;
}

static u16 read16(const u8* data)
{
    return (u16) ((u16) data[0] << 8 | data[1]);
}

static f32 read_float(const u8* data)
{
    u32 bits = NativeArchiveBE32(data);
    f32 result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static bool reference(NativeEventArchive* events, uint32_t field,
                      uint32_t* target, bool* present)
{
    return NativeArchiveReference(events->archive, field, target, present,
                                  events->error) == NATIVE_ARCHIVE_OK;
}

static struct gm_801BAB40_src* player(NativeEventArchive* events,
                                      uint32_t offset)
{
    struct gm_801BAB40_src* result = cached(events, offset, EVENT_PLAYER);
    const u8* data;
    if (result != NULL) {
        return result;
    }
    if (!range(events, offset, 0x1C)) {
        return NULL;
    }
    result = allocate(events, offset, EVENT_PLAYER, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    data = events->archive->data + offset;
    memcpy(result, data, 12);
    result->x12 = read16(data + 0x0C);
    result->hp = read16(data + 0x0E);
    result->x18 = read_float(data + 0x10);
    result->x1C = read_float(data + 0x14);
    result->x20 = read_float(data + 0x18);
    return result;
}

static struct gm_evinit* init(NativeEventArchive* events, uint32_t offset)
{
    struct gm_evinit* result = cached(events, offset, EVENT_INIT);
    const u8* data;
    if (result != NULL) {
        return result;
    }
    if (!range(events, offset, 0x28)) {
        return NULL;
    }
    result = allocate(events, offset, EVENT_INIT, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    data = events->archive->data + offset;
    result->x0_0 = data[0] >> 5;
    result->x0_3 = (data[0] >> 2) & 7;
    result->x0_6 = (data[0] >> 1) & 1;
    result->x0_7 = data[0] & 1;
    result->x1_0 = data[1] >> 7;
    result->x1_1 = (data[1] >> 6) & 1;
    result->x1_2 = (data[1] >> 5) & 1;
    result->x1_3 = (data[1] >> 4) & 1;
    result->x1_4 = (data[1] >> 3) & 1;
    result->x1_5 = data[1] & 7;
    result->is_teams = data[2];
    result->item_freq = (s8) data[3];
    result->sd_penalty = (s8) data[4];
    result->unk5 = data[5];
    result->stkind = read16(data + 6);
    result->time_limit = NativeArchiveBE32(data + 8);
    memcpy(result->padC, data + 0x0C, sizeof(result->padC));
    result->x10 = (u64) NativeArchiveBE32(data + 0x10) << 32 |
                  NativeArchiveBE32(data + 0x14);
    result->x18 = (s32) NativeArchiveBE32(data + 0x18);
    result->x1C = read_float(data + 0x1C);
    result->game_speed = read_float(data + 0x20);
    result->unk24 = read_float(data + 0x24);
    return result;
}

static struct gm_evbonus* bonus(NativeEventArchive* events, uint32_t offset)
{
    struct gm_evbonus* result = cached(events, offset, EVENT_BONUS);
    const u8* data;
    if (result != NULL) {
        return result;
    }
    if (!range(events, offset, 0x18)) {
        return NULL;
    }
    result = allocate(events, offset, EVENT_BONUS, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    data = events->archive->data + offset;
    memcpy(result, data, 8);
    result->x8 = read_float(data + 8);
    result->xC = read_float(data + 0x0C);
    result->x10 = read_float(data + 0x10);
    memcpy(&result->flags, data + 0x14, 4);
    return result;
}

static struct gm_evstage_table* stages(NativeEventArchive* events,
                                       uint32_t offset)
{
    struct gm_evstage_table* result = cached(events, offset, EVENT_STAGE);
    const u8* data;
    if (result != NULL) {
        return result;
    }
    if (!range(events, offset, 0x28)) {
        return NULL;
    }
    data = events->archive->data + offset;
    if (data[0] == 0 || data[0] > GM_MAX_PLAYERS) {
        return fail(events, offset, "event round count exceeds stage entries");
    }
    result = allocate(events, offset, EVENT_STAGE, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    result->count = data[0];
    result->pad1 = data[1];
    for (size_t i = 0; i < 7; ++i) {
        result->stage[i] = read16(data + 2 + i * 2);
    }
    /* Event 36 uses extra opponent slots beyond its two-round count. */
    for (size_t i = 0; i < GM_MAX_PLAYERS; ++i) {
        uint32_t target;
        bool present;
        if (!reference(events, offset + 0x10 + i * 4, &target, &present)) {
            return NULL;
        }
        if (present) {
            result->entries[i] = player(events, target);
            if (result->entries[i] == NULL) {
                return NULL;
            }
        } else if (i < result->count) {
            return fail(events, offset + 0x10 + i * 4,
                        "event round has no opponent");
        }
    }
    return result;
}

static struct gm_804D6900_x4_t* extra(NativeEventArchive* events,
                                      uint32_t offset, unsigned index)
{
    struct gm_804D6900_x4_t* result = cached(events, offset, EVENT_EXTRA);
    uint32_t target;
    bool present;
    const u8* data;
    size_t size = index == 12 ? 12 : (index == 36 ? 48 : 8);
    if (result != NULL) {
        return result;
    }
    if (!range(events, offset, size)) {
        return NULL;
    }
    if (index == 13 || index == 25 || index == 46) {
        size_t bytes = index == 46 ? 28 : 16;
        u8* list = allocate(events, offset, EVENT_EXTRA, bytes);
        if (list == NULL) {
            return NULL;
        }
        memcpy(list, events->archive->data + offset, bytes);
        return (struct gm_804D6900_x4_t*) list;
    }
    if (index == 36) {
        u32* list = allocate(events, offset, EVENT_EXTRA, 12 * sizeof(*list));
        if (list == NULL) {
            return NULL;
        }
        for (size_t i = 0; i < 12; ++i) {
            list[i] =
                NativeArchiveBE32(events->archive->data + offset + i * 4);
        }
        return (struct gm_804D6900_x4_t*) list;
    }
    result = allocate(events, offset, EVENT_EXTRA, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    data = events->archive->data + offset;
    result->x0 = (s32) NativeArchiveBE32(data);
    if (index == 43) {
        if (!reference(events, offset + 4, &target, &present) || !present) {
            return fail(events, offset + 4, "event extra player is missing");
        }
        result->x4 = (intptr_t) player(events, target);
        if (result->x4 == 0) {
            return NULL;
        }
    } else {
        result->x4 = (intptr_t) (s32) NativeArchiveBE32(data + 4);
    }
    return result;
}

static struct gm_804D6900_t* level(NativeEventArchive* events, uint32_t offset,
                                   unsigned index)
{
    struct gm_804D6900_t* result = cached(events, offset, EVENT_LEVEL);
    const u8* data;
    uint32_t target;
    bool present;
    if (result != NULL) {
        return result;
    }
    if (!range(events, offset, 0x2C)) {
        return NULL;
    }
    result = allocate(events, offset, EVENT_LEVEL, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    data = events->archive->data + offset;
    result->kind = data[0];
    result->flags = data[1];
#define EVENT_REF(field, member, converter)                                   \
    do {                                                                      \
        if (!reference(events, offset + (field), &target, &present)) {        \
            return NULL;                                                      \
        }                                                                     \
        if (present) {                                                        \
            result->member = converter;                                       \
            if (result->member == NULL) {                                     \
                return NULL;                                                  \
            }                                                                 \
        }                                                                     \
    } while (0)
    EVENT_REF(4, x4, extra(events, target, index));
    EVENT_REF(8, evinit, init(events, target));
    EVENT_REF(0x0C, evbonus, bonus(events, target));
    EVENT_REF(0x10, evstage_table, stages(events, target));
    for (size_t i = 0; i < GM_MAX_PLAYERS; ++i) {
        if (!reference(events, offset + 0x14 + i * 4, &target, &present)) {
            return NULL;
        }
        if (present) {
            result->player_init[i] = player(events, target);
            if (result->player_init[i] == NULL) {
                return NULL;
            }
        }
    }
#undef EVENT_REF
    return result;
}

NativeEventArchive* NativeEventArchiveOpen(const NativeArchive* archive)
{
    NativeEventArchive* events;
    if (archive == NULL) {
        return NULL;
    }
    events = calloc(1, sizeof(*events));
    if (events == NULL) {
        return NULL;
    }
    events->archive = archive;
    return events;
}

void NativeEventArchiveClose(NativeEventArchive* events)
{
    if (events == NULL) {
        return;
    }
    while (events->allocations != NULL) {
        EventAllocation* allocation = events->allocations;
        events->allocations = allocation->next;
        free(allocation->data);
        free(allocation);
    }
    free(events->levels);
    free(events);
}

NativeArchiveStatus NativeEventArchiveRead(NativeEventArchive* events,
                                           const char* symbol, uint32_t offset,
                                           void** output,
                                           NativeArchiveError* error)
{
    uint32_t target;
    bool present;
    if (output != NULL) {
        *output = NULL;
    }
    if (events == NULL || symbol == NULL || output == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 32u + offset,
                                 "invalid event archive request");
    }
    if (strcmp(symbol, "sqEventInitDataLevelTbl") != 0) {
        return NATIVE_ARCHIVE_NOT_FOUND;
    }
    if (events->levels != NULL) {
        *output = events->levels;
        return NATIVE_ARCHIVE_OK;
    }
    /* The symbol offset points directly at the pointer table. */
    target = offset;
    if (!range(events, target, EVENT_LEVELS * 4)) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_BOUNDS, 32u + target,
                                 "event level table is truncated");
    }
    events->levels = calloc(EVENT_LEVELS + 1, sizeof(*events->levels));
    if (events->levels == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_NO_MEMORY, 32u + target,
                                 "cannot allocate event level table");
    }
    for (size_t i = 0; i < EVENT_LEVELS; ++i) {
        if (!reference(events, target + i * 4, &offset, &present) || !present)
        {
            return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID,
                                     32u + target + i * 4,
                                     "event level table entry is missing");
        }
        events->levels[i] = level(events, offset, (unsigned) i);
        if (events->levels[i] == NULL) {
            return NATIVE_ARCHIVE_INVALID;
        }
    }
    *output = events->levels;
    return NATIVE_ARCHIVE_OK;
}
