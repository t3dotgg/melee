#include <string.h>

#include "lb_0195.h"
#include "lbarchive.h"
#include "lbdvd.static.h"
#include "lbfile.h"
#include "lbheap.h"
#include "types.h"
#include <dolphin/dvd.h>
#include <melee/db/db.h>
#include <melee/ef/efasync.h>
#include <melee/gm/gmcameramode.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/stage.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/debug.h>
#ifdef MELEE_NATIVE
#include <sysdolphin/baselib/archive.h>
#endif

enum {
    PRELOAD_STATE_UNUSED = 0,
    PRELOAD_STATE_QUEUED = 1,
    PRELOAD_STATE_READING = 2,
    PRELOAD_STATE_LOADED = 3,
    PRELOAD_STATE_READY = 4,
};

/* 0189EC */ static void lbDvd_800189EC(int entry_num);

void lbDvd_SetupVsPreloadCache(void)
{
    lbDvd_80018C6C();
    lbDvd_80018254();
    lbDvd_80017700(4);
}

#ifdef MUST_MATCH
#pragma push
#pragma dont_inline on
#endif
void lbDvd_800174E8(int index)
{
    PreloadEntry* entry = &preloadCache.entries[index];
    if (entry->archive != NULL) {
#ifdef MELEE_NATIVE
        HSD_ArchiveNativeRelease((HSD_Archive*) entry->archive->addr);
#endif
        lbHeap_80015CA8(entry->heap, entry->archive->addr);
    }
    if (entry->raw_data != NULL) {
        lbHeap_80015CA8(entry->heap, entry->raw_data->addr);
    }
    *entry = lbDvd_803BA68C;
}
#ifdef MUST_MATCH
#pragma pop
#endif

bool lbDvd_80017598(int heap)
{
    PreloadEntry* entry;
    bool enabled;
    int i;
    bool no_change;

    no_change = false;
    enabled = OSDisableInterrupts();

    if (preloadCache.persistent_heap == heap) {
        no_change = true;
    } else {
        for (i = 0; i < (signed) ARRAY_SIZE(preloadCache.entries); i++) {
            entry = &preloadCache.entries[i];
            if (entry->state != PRELOAD_STATE_UNUSED && entry->heap == heap) {
                if (entry->state == PRELOAD_STATE_READING) {
                    no_change = true;
                } else {
                    lbDvd_800174E8(i);
                }
            }
        }
    }
    OSRestoreInterrupts(enabled);
    return no_change;
}

static bool lbDvd_80017644(int heap)
{
    PreloadEntry* entry;
    int enabled;
    int i;
    bool no_change;

    no_change = false;
    enabled = OSDisableInterrupts();
    if (preloadCache.persistent_heap == heap) {
        no_change = true;
    } else {
        for (i = 0; i < (signed) ARRAY_SIZE(preloadCache.entries); i++) {
            entry = &preloadCache.entries[i];
            if ((entry->state == PRELOAD_STATE_LOADED ||
                 entry->state == PRELOAD_STATE_READY) &&
                entry->heap == heap && entry->load_score < 0 &&
                entry->load_state == 2)
            {
                lbDvd_800174E8(i);
            }
        }
    }
    OSRestoreInterrupts(enabled);
    return no_change;
}

void lbDvd_80017700(int heap)
{
    while (lbDvd_80017644(heap)) {
        lb_800195D0();
    }
}

static inline int sameHeap(int entry_heap, s32 requested_heap)
{
    // Keep the integer result. A direct comparison changes the matching code.
    int result = 0;
    if (entry_heap == requested_heap) {
        result = 1;
    }
    return result;
}

void* lbDvd_80017740(int type, int entry_num, int transient_heap, int heap,
                     size_t size, int load_state, int load_score, u8 flags,
                     int effect_index)
{
    PreloadEntry* entry;
    int free_index = -1;
    int entry_index;

    for (entry_index = 0;
         entry_index < (signed) ARRAY_SIZE(preloadCache.entries);
         entry_index++)
    {
        entry = &preloadCache.entries[entry_index];
        if (entry->state == PRELOAD_STATE_UNUSED) {
            if (free_index == -1) {
                free_index = entry_index;
            }
        } else if (entry->entry_num == entry_num &&
                   sameHeap(entry->heap, transient_heap))
        {
            if (entry->state == PRELOAD_STATE_QUEUED) {
                if (entry->load_score < 0) {
                    entry->load_score *= -1;
                    if (entry->load_score <= 0x2314) {
                        entry->load_score += 10;
                    }
                }
            } else {
                entry->load_score = 0x270F;
            }
            goto done;
        }
    }

    HSD_ASSERT(0x1C1, free_index != -1);
    entry = &preloadCache.entries[free_index];
    entry->state = PRELOAD_STATE_QUEUED;
    entry->type = type;
    entry->entry_num = entry_num;
    if (lbHeap_80015BB8(heap)) {
        HSD_ASSERTREPORT(0x1CB, 0, "%d, %d\n", heap, entry_num);
    }
    entry->heap = heap;
    entry->size = size;
    entry->archive = NULL;
    entry->raw_data = NULL;
    entry->load_state = load_state;
    entry->load_score = load_score;
    entry->unknown004 = flags;
    entry->effect_index = effect_index;

done:
    return entry;
}

void lbDvd_800178E8(int type, const char* name, int transient_heap, int heap,
                    int size, int load_state, int load_score, u8 flags,
                    int effect_index)
{
    u8 _[8];
    int entry_num = DVDConvertPathToEntrynum(lbFileGetFullName(name));
    lbDvd_80017740(type, entry_num, transient_heap, heap, size, load_state,
                   load_score, flags, effect_index);
}

void lbDvd_80017960(void)
{
    int j;
    struct GameCache* game_cache = &preloadCache.new_scene.game_cache;
    int i;
    u8 _[4];

    if (preloadCache.new_scene.game_cache.mode_kind != GM_COUNT) {
        switch (preloadCache.new_scene.game_cache.mode_kind) {
        case GM_CAMERA_MODE:
            gm_801B23F0();
            break;
        }
    }

    if (game_cache->stkind != 0x148) {
        Stage_802251B4(game_cache->stkind);
    }

    for (i = 0; i < 8; i++) {
        if (game_cache->entries[i].char_id != CHKIND_NONE) {
            Player_80031CB0(game_cache->entries[i].char_id,
                            game_cache->entries[i].color);
        }
        if (game_cache->entries[i].char_id == CKIND_KIRBY) {
            if (game_cache->entries[i].x5 == 0) {
                CharacterKind kind;
                for (kind = 0; kind < CHKIND_MAX; kind++) {
                    Player_80031D2C(kind, game_cache->entries[i].color);
                }
            } else {
                for (j = 0; j < 8; j++) {
                    if (game_cache->entries[j].char_id != CHKIND_NONE) {
                        if (game_cache->entries[j].char_id == CKIND_KIRBY &&
                            game_cache->entries[j].x5 == 0)
                        {
                            CharacterKind kind;
                            for (kind = 0; kind < CHKIND_MAX; kind++) {
                                Player_80031D2C(kind,
                                                game_cache->entries[i].color);
                            }
                        }
                        Player_80031D2C(game_cache->entries[j].char_id,
                                        game_cache->entries[i].color);
                    }
                }
            }
        }
    }
}

static void lbDvd_80017A80(u32 unused)
{
    preloadCache.persistent_heap = 6;
    lbDvd_80017CC4();
}

static inline int lbDvd_CleanupPreloadHeap(int heap, PreloadCache* cache)
{
    PreloadEntry* entry;
    s32 i;

    for (i = 0; i < (signed) ARRAY_SIZE(cache->entries); i++) {
        entry = &cache->entries[i];
        if (entry->state == PRELOAD_STATE_LOADED) {
            if (entry->heap == heap && entry->load_score < 0) {
                // This reload preserves the matching register allocation.
                entry = &cache->entries[i];
                if (entry->archive != NULL) {
#ifdef MELEE_NATIVE
                    HSD_ArchiveNativeRelease(
                        (HSD_Archive*) entry->archive->addr);
#endif
                    lbHeap_80015CA8(entry->heap, entry->archive->addr);
                }
                if (entry->raw_data != NULL) {
                    lbHeap_80015CA8(entry->heap, entry->raw_data->addr);
                }
                *entry = lbDvd_803BA68C;
            }
        }
    }
    cache->persistent_heap = heap;
    return lbHeap_80015D6C(heap, lbDvd_80017A80, heap);
}

void lbDvd_CachePreloadedFile(s32 index)
{
    int compaction_pending;
    PreloadEntry* entry;
    entry = &preloadCache.entries[index];

    compaction_pending = lbDvd_CleanupPreloadHeap(entry->heap, &preloadCache);

    if (compaction_pending == 0) {
        preloadCache.persistent_heap = 6;
    }

    if (compaction_pending == 0) {
        if (entry->size == 0) {
            entry->size = lbFile_8001634C(entry->entry_num);
        }
        entry->raw_data =
            lbHeap_80015BD0(entry->heap, OSRoundUp32B(entry->size));
        if (entry->type == 2 || entry->type == 3 || entry->type == 4) {
            entry->archive = lbHeap_80015BD0(entry->heap, sizeof(HSD_Archive));
        }
        if (entry->raw_data == NULL) {
            lbDvd_80017E64(0, index, 0, 1);
        } else if (entry->type == 0) {
            lbDvd_80017E64(0, index, 0, 0);
        } else {
            entry->state = PRELOAD_STATE_READING;
            entry->load_score = 9999;
            lbFile_800164A4(entry->entry_num,
                            (uintptr_t) entry->raw_data->addr, &entry->size, 2,
                            lbDvd_80017E64, (void*) (intptr_t) index);
        }
    }
}

void lbDvd_80017CC4(void)
{
    PreloadEntry* entry;
    s32 next_read_index;
    s32 active_read_index;
    int max_load_score;
    int entry_index;

    active_read_index = -1;
    max_load_score = 0;
    next_read_index = -1;

    if (preloadCache.persistent_heap == 6) {
        for (entry_index = 0;
             entry_index < (signed) ARRAY_SIZE(preloadCache.entries);
             entry_index++)
        {
            entry = &preloadCache.entries[entry_index];
            switch (preloadCache.entries[entry_index].state) {
            case PRELOAD_STATE_UNUSED:
            case PRELOAD_STATE_LOADED:
                break;
            case PRELOAD_STATE_READING:
                active_read_index = entry_index;
                break;
            case PRELOAD_STATE_QUEUED:
                if (entry->load_score > max_load_score) {
                    max_load_score = entry->load_score;
                    next_read_index = entry_index;
                }
                break;
            }
        }

        if (active_read_index == -1 && next_read_index != -1) {
            lbDvd_CachePreloadedFile(next_read_index);
        }
    }
}

void lbDvd_80017E64(int request_id, intptr_t index, void* buffer,
                    bool cancelflag)
{
    PreloadEntry* entry = &preloadCache.entries[index];
    if (cancelflag != 0) {
        HSD_ASSERT(827, 0);
    } else {
        entry->state = PRELOAD_STATE_LOADED;
    }
    lbDvd_80017CC4();
}

void* lbDvd_GetPreloadedArchive(ssize_t entry_num)
{
    s8 type;
    ssize_t entry_index;
    s32 interrupts_enabled;
    PreloadEntry* entry;

    interrupts_enabled = OSDisableInterrupts();

    for (entry_index = 0;
         entry_index < (signed) ARRAY_SIZE(preloadCache.entries);
         entry_index++)
    {
        entry = &preloadCache.entries[entry_index];
        if (entry->state != PRELOAD_STATE_UNUSED && entry->load_score > 0 &&
            entry->entry_num == entry_num)
        {
            break;
        }
    }

    if (entry_index == (signed) ARRAY_SIZE(preloadCache.entries)) {
        OSRestoreInterrupts(interrupts_enabled);
        return 0;
    }

    if (entry->state == PRELOAD_STATE_QUEUED) {
        entry->load_score = 8980;
    }

    OSRestoreInterrupts(interrupts_enabled);
    lbDvd_800189EC(entry_num);
    if (entry->load_state == 1) {
        PreloadEntry* entry = &preloadCache.entries[entry_index];
        type = entry->type;

        switch (type) {
        case 2:
            lbArchive_InitializeDAT((HSD_Archive*) entry->archive->addr,
                                    (u8*) entry->raw_data->addr, entry->size);
            break;

        case 3:
            efAsync_OnLoad((HSD_Archive*) entry->archive->addr,
                           (u8*) entry->raw_data->addr, entry->size,
                           entry->effect_index);
            break;

        case 4:
            grDatFiles_801C5FC0((HSD_Archive*) entry->archive->addr,
                                entry->raw_data->addr, entry->size);
            break;

        default:
            HSD_ASSERT(864, 0);
            break;
        }

        entry->load_state = 2;
    }
    if (entry->archive != NULL) {
        return (HSD_Archive*) entry->archive->addr;
    }
    return entry->raw_data->addr;
}

struct lbDvd_803B72C0_t {
    u8 x0;
    char* x4;
    int x8;
};

static inline void inline1_inner(struct lbDvd_803B72C0_t* data)
{
    const char* filename = data->x4;
    int effect_index = data->x8;
    u8 type = data->x0;
    int entry_num = DVDConvertPathToEntrynum(lbFileGetFullName(filename));
    lbDvd_80017740(type, entry_num, 2, 2, 0, 1, 9, 0x80, effect_index);
}

static inline void inline1(void)
{
    struct lbDvd_803B72C0_t spA0 = { 2, "LbRb.dat" };
    if (preloadCache.new_scene.is_heap_persistent[0]) {
        inline1_inner(&spA0);
    }
}

static inline void inline2_inner(struct lbDvd_803B72C0_t* data)
{
    int effect_index;
    int entry_num;
    u8 type;
    const char* filename;

    filename = data->x4;
    effect_index = data->x8;
    type = data->x0;
    entry_num = DVDConvertPathToEntrynum(lbFileGetFullName(filename));
    lbDvd_80017740(type, entry_num, 3, 3, 0, 1, 8, 0x40, effect_index);
}

static inline void inline2(void)
{
    int i;
    struct lbDvd_803B72C0_t sp28[4] = {
        { 3, "EfMnData.dat", 0x1F },
        { 3, "EfCoData.dat" },
        { 2, "ItCo." },
        { 2, "IfAll" },
    };
    if (preloadCache.new_scene.is_heap_persistent[1]) {
        for (i = 0; i < ARRAY_SIZE(sp28); i++) {
            inline2_inner(&sp28[i]);
        }
    }
}

HSD_Archive* lbDvd_8001819C(const char* basename)
{
    HSD_Archive* archive;
    char* filename = lbFileGetFullName(basename);
    archive = lbDvd_GetPreloadedArchive(DVDConvertPathToEntrynum(filename));
    if (DbLevel != DbLKind_Master && preloadCache.preloaded && archive == NULL)
    {
        HSD_ASSERTREPORT(948, 0, "[LbDvd] %s is not PRELOADed.\n", filename);
    }
    return archive;
}

PreloadedGameModeState* lbDvd_GetPreloadCacheScene(void)
{
    return &preloadCache.scene;
}

void lbDvd_8001823C(void)
{
    lbDvd_GetPreloadCacheScene()->mode_scene_changes =
        preloadCache.new_scene.mode_scene_changes + 1;
}

static inline void inline_preload_entries(bool* enabled)
{
    PreloadEntry* entry;
    int i;

    *enabled = OSDisableInterrupts();
    for (i = 0; i < (signed) ARRAY_SIZE(preloadCache.entries); i++) {
        entry = &preloadCache.entries[i];
        if (entry->state != PRELOAD_STATE_UNUSED) {
            if (entry->load_score > 0) {
                entry->load_score *= -1;
            }
        }
    }
}

static inline void inline_pad(void)
{
    u8 pad[0x10];
    (void) pad;
}

static inline void inline_cleanup_entries(void)
{
    PreloadEntry* cleanup_entry;
    int j = 0;

    for (; j < (signed) ARRAY_SIZE(preloadCache.entries); j++) {
        cleanup_entry = &preloadCache.entries[j];
        if (cleanup_entry->load_score < 0) {
            if (cleanup_entry->state == PRELOAD_STATE_QUEUED) {
                if (preloadCache.entries[j].archive != NULL) {
#ifdef MELEE_NATIVE
                    HSD_ArchiveNativeRelease(
                        (HSD_Archive*) cleanup_entry->archive->addr);
#endif
                    lbHeap_80015CA8(cleanup_entry->heap,
                                    cleanup_entry->archive->addr);
                }
                if (cleanup_entry->raw_data != NULL) {
                    lbHeap_80015CA8(cleanup_entry->heap,
                                    cleanup_entry->raw_data->addr);
                }
                *cleanup_entry = lbDvd_803BA68C;
            } else if (cleanup_entry->state == PRELOAD_STATE_READY) {
                cleanup_entry->state = PRELOAD_STATE_LOADED;
            }
        }
    }
}

void lbDvd_80018254(void)
{
    bool enabled;

    if (memcmp(&preloadCache.new_scene, &preloadCache.scene,
               sizeof(PreloadedGameModeState)) == 0)
    {
        return;
    }

    preloadCache.new_scene = preloadCache.scene;
    inline_preload_entries(&enabled);

    switch (preloadCache.persistent_heaps) {
    case 0:
        break;
    case 1:
        inline1();
        break;
    case 2:
        inline1();
        inline2();
        break;
    case 3:
        inline1();
        inline2();
        lbDvd_80017960();
        break;
    }

    inline_pad();
    inline_cleanup_entries();

    lbDvd_80017CC4();
    OSRestoreInterrupts(enabled);
}

static inline void findHeapDependencies(PreloadEntry* entry,
                                        bool* has_pending_entry,
                                        bool* has_stale_entry)
{
    int i;
    PreloadEntry* other;
    *has_stale_entry = false;
    *has_pending_entry = false;
    for (i = 0; i < (signed) ARRAY_SIZE(preloadCache.entries); i++) {
        other = &preloadCache.entries[i];
        if (other->state == PRELOAD_STATE_QUEUED &&
            other->heap == entry->heap && other->load_score > 0)
        {
            *has_pending_entry = true;
        }
        if ((other->state == PRELOAD_STATE_READING ||
             other->state == PRELOAD_STATE_LOADED) &&
            other->heap == entry->heap && other->load_score < 0)
        {
            *has_stale_entry = true;
        }
    }
}

int lbDvd_800187F4(int entry_num)
{
    int result = 0;
    PreloadEntry* entry;
    int entry_index;

    bool interrupts_enabled = OSDisableInterrupts();
    bool has_pending_entry;
    bool has_stale_entry;

    int persistent_heap = preloadCache.persistent_heap;

    for (entry_index = 0;
         entry_index < (signed) ARRAY_SIZE(preloadCache.entries);
         entry_index++)
    {
        entry = &preloadCache.entries[entry_index];
        if (entry->state == PRELOAD_STATE_UNUSED) {
            continue;
        }

        if (entry->load_score <= 0) {
            continue;
        }

        if (entry->entry_num != entry_num) {
            continue;
        }

        if (entry->heap == persistent_heap) {
            result = 1;
            break;
        }

        switch (entry->state) {
        case PRELOAD_STATE_QUEUED:
        case PRELOAD_STATE_READING:
            result = 1;
            break;
        case PRELOAD_STATE_LOADED:
            findHeapDependencies(entry, &has_pending_entry, &has_stale_entry);
            if (has_pending_entry && has_stale_entry) {
                result = 1;
                break;
            }
            entry->state = PRELOAD_STATE_READY;
            entry->load_score = 0x270F;
        case PRELOAD_STATE_READY:
            result = 2;
            break;
        default:
            continue;
        }
        break;
    }

    OSRestoreInterrupts(interrupts_enabled);
    return result;
}

void lbDvd_800189EC(int entry_num)
{
    while (lbDvd_800187F4(entry_num) == 1) {
        lb_800195D0();
    }
}

int lbDvd_80018A2C(u8 flags)
{
    PreloadEntry* other;
    int j;
    bool has_pending_entry;
    bool has_stale_entry;
    int result = 0;
    PreloadEntry* entry;
    bool interrupts_enabled = OSDisableInterrupts();
    int i;

    for (i = 0; i < 0x50; i++) {
        entry = &preloadCache.entries[i];
        if (entry->state == PRELOAD_STATE_UNUSED) {
            continue;
        }
        if (entry->load_score <= 0) {
            continue;
        }
        if (!(entry->unknown004 & flags)) {
            continue;
        }

        if (entry->heap == preloadCache.persistent_heap) {
            result = 1;
            break;
        }

        switch (entry->state) {
        case PRELOAD_STATE_QUEUED:
        case PRELOAD_STATE_READING:
            result = 1;
            break;
        case PRELOAD_STATE_LOADED:
            has_stale_entry = false;
            has_pending_entry = false;
            for (j = 0; j < (signed) ARRAY_SIZE(preloadCache.entries); j++) {
                other = &preloadCache.entries[j];
                if (other->state == PRELOAD_STATE_QUEUED &&
                    other->heap == entry->heap && other->load_score > 0)
                {
                    has_pending_entry = true;
                }
                if ((other->state == PRELOAD_STATE_READING ||
                     other->state == PRELOAD_STATE_LOADED) &&
                    other->heap == entry->heap && other->load_score < 0)
                {
                    has_stale_entry = true;
                }
            }
            if (has_pending_entry && has_stale_entry) {
                result = 1;
                break;
            }
            entry->state = PRELOAD_STATE_READY;
            entry->load_score = 0x270F;
        case PRELOAD_STATE_READY:
            result = 2;
            continue;
        default:
            continue;
        }
        break;
    }

    OSRestoreInterrupts(interrupts_enabled);
    return result;
}

void lbDvd_80018C2C(u8 flags)
{
    while (lbDvd_80018A2C(flags) == 1) {
        lb_800195D0();
    }
}

void lbDvd_80018C6C(void)
{
    switch (preloadCache.persistent_heaps) {
    case 1:
        lbDvd_GetPreloadCacheScene()->is_heap_persistent[0] = true;
        break;
    case 2:
        lbDvd_GetPreloadCacheScene()->is_heap_persistent[0] = true;
        lbDvd_GetPreloadCacheScene()->is_heap_persistent[1] = true;
        break;
    case 3:
        lbDvd_GetPreloadCacheScene()->is_heap_persistent[0] = true;
        lbDvd_GetPreloadCacheScene()->is_heap_persistent[1] = true;
        lbDvd_GetPreloadCacheScene()->game_cache =
            preload_cache_scene.game_cache;
    case 0:
        break;
    }
}

static s32 lbDvd_804D37F4[2] = { 4, 5 };

static inline void inline0(void)
{
    int i;
    int tmp;
    for (i = 0; i < ARRAY_SIZE(lbDvd_804D37F4); i++) {
        if (lbHeap_800158E8(lbDvd_804D37F4[i]) == 1) {
            tmp = lbDvd_804D37F4[i];
            while (lbDvd_80017598(tmp) != 0) {
                lb_800195D0();
            }
        }
    }
}

void lbDvd_80018CF4(int arg0)
{
    unsigned int i;

    if (preloadCache.persistent_heaps != arg0) {
        lbDvd_GetPreloadCacheScene()->mode_scene_changes =
            preloadCache.new_scene.mode_scene_changes + 1;
    }
    preloadCache.persistent_heaps = arg0;
    lbHeap_800158D0(2, 1);
    lbHeap_800158D0(3, 1);

    for (i = 0; i < 2; i++) {
        lbHeap_800158D0(lbDvd_804D37F4[i], 1);
    }
    switch (preloadCache.persistent_heaps) {
    case lbDvdPreload_0:
        lbDvd_GetPreloadCacheScene()->is_heap_persistent[0] =
            preload_cache_scene.is_heap_persistent[0];
        lbDvd_GetPreloadCacheScene()->is_heap_persistent[1] =
            preload_cache_scene.is_heap_persistent[1];
        lbDvd_GetPreloadCacheScene()->game_cache =
            preload_cache_scene.game_cache;
        break;
    case lbDvdPreload_1:
        lbHeap_800158D0(2, 0);
        lbDvd_GetPreloadCacheScene()->is_heap_persistent[1] =
            preload_cache_scene.is_heap_persistent[1];
        lbDvd_GetPreloadCacheScene()->game_cache =
            preload_cache_scene.game_cache;
        break;
    case lbDvdPreload_2:
        lbHeap_800158D0(2, 0);
        lbHeap_800158D0(3, 0);
        lbDvd_GetPreloadCacheScene()->game_cache =
            preload_cache_scene.game_cache;
        break;
    case lbDvdPreload_3:
        lbHeap_800158D0(2, 0);
        lbHeap_800158D0(3, 0);
        for (i = 0; i < ARRAY_SIZE(lbDvd_804D37F4); i++) {
            lbHeap_800158D0(lbDvd_804D37F4[i], 0);
        }
        break;
    }
    if (lbHeap_800158E8(2) == 1) {
        while (lbDvd_80017598(2) != 0) {
            lb_800195D0();
        }
    }
    if (lbHeap_800158E8(3) == 1) {
        while (lbDvd_80017598(3) != 0) {
            lb_800195D0();
        }
    }
    inline0();
    lbHeap_80015900();
}

void lbDvd_80018F58(bool value)
{
    preloadCache.preloaded = value;
}

void lbDvd_80018F68(void)
{
    int i;
    preloadCache.persistent_heaps = 0;
    *lbDvd_GetPreloadCacheScene() = preload_cache_scene;
    preloadCache.new_scene = preload_cache_scene;
    for (i = 0; i < (signed) ARRAY_SIZE(preloadCache.entries); i++) {
        preloadCache.entries[i] = lbDvd_803BA68C;
    }
    preloadCache.persistent_heap = 6;
    preloadCache.preloaded = 0;
}
