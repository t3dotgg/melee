#include "lbheap.h"

#include <stddef.h> // offsetof

#include "lbmemory.h"
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/memory.h>

struct Heap {
    /* 0x00 */ s32 id;
    /* 0x04 */ Handle* handle;
    /// The heap's base *address*. `s32` would sign-extend when it is cast
    /// back to a pointer; the same four bytes on GameCube.
    /* 0x08 */ uintptr_t start;
    /* 0x0C */ size_t size;
    /* 0x10 */ s32 type;
    /* 0x14 */ s32 transient;
    /* 0x18 */ LbHeapStatus status;
};
ASSERT_SIZE(struct Heap, 0x1C);

struct lbHeap_HeapState {
    /* 0x00 */ void* arena_lo;    /* inferred */
    /* 0x04 */ void* arena_hi;    /* inferred */
    /* 0x08 */ uintptr_t aram_lo; /* inferred */
    /* 0x0C */ uintptr_t aram_hi; /* inferred */
    /* 0x10 */ struct Heap heap_array[6];
}; /* size = 0xB8 */
ASSERT_SIZE(struct lbHeap_HeapState, 0xB8);

/* 431FA0 */ static struct lbHeap_HeapState lbHeap_80431FA0;

struct lbHeap_HeapDesc {
    u32 idx;
    u32 type;
    u32 prev_idx;
    u32 size;
};

struct lbHeap_HeapOffsetView {
    u8 pad[0x10];
    struct Heap heap;
};

struct lbHeap_HeapDesc lbHeap_803BA380[5] = {
    { 2, 1, 6, 0x800 },    { 3, 1, 2, 0x4F8800 }, { 4, 2, 6, 0x64B400 },
    { 5, 4, 6, 0x96C800 }, { 6, 0, 0, 0 },
};

#define lbHeap_GetHeapOffsetView(offset)                                      \
    ((struct lbHeap_HeapOffsetView*) ((uintptr_t) (&lbHeap_80431FA0) +        \
                                      (offset)))

/// Offset within lbHeap_80431FA0 of the view overlaying heap_array[idx].
/// 0x38 for idx 2 on PowerPC.
#define lbHeap_HeapViewOffset(idx)                                            \
    (offsetof(struct lbHeap_HeapState, heap_array[idx]) -                     \
     offsetof(struct lbHeap_HeapOffsetView, heap))

static inline void lbHeap_ResetHeap(struct Heap* heap)
{
    heap->id = -1;
    heap->handle = (Handle*) -1;
    heap->start = 0;
    heap->size = 0;
    heap->type = 1;
    heap->transient = 1;
    heap->status = LbHeapStatus_Destroy;
}

static inline void
lbHeap_DestroyOffsetViewIfCreated(struct lbHeap_HeapOffsetView* view)
{
    struct Heap* heap = &view->heap;

    if (view->heap.status == LbHeapStatus_Create) {
        if (heap->type == 0) {
            OSDestroyHeap(heap->id);
            heap->id = -1;
        } else {
            lbMemory_80014EEC(heap->handle);
            heap->handle = (Handle*) -1;
        }
        heap->status = LbHeapStatus_Destroy;
    }
}

static inline void
lbHeap_CreateOffsetViewIfDestroyed(struct lbHeap_HeapOffsetView* view)
{
    struct Heap* heap = &view->heap;

    if (view->heap.status == LbHeapStatus_Destroy) {
        if (heap->type == 0) {
            heap->id = OSCreateHeap((void*) heap->start,
                                    (void*) (heap->start + heap->size));
        } else {
            heap->handle = lbMemory_80014E24(
                (void*) heap->start, (void*) (heap->start + heap->size));
        }
        heap->status = LbHeapStatus_Create;
    }
}

void lbHeap_800158D0(int heap_index, int transient)
{
    lbHeap_80431FA0.heap_array[heap_index].transient = transient;
}

int lbHeap_800158E8(int heap_index)
{
    return lbHeap_80431FA0.heap_array[heap_index].transient;
}

void lbHeap_80015900(void)
{
    uintptr_t reserved_end;
    struct lbHeap_HeapOffsetView* destroy_view;
    s32 reserve_index;
    struct Heap* reserved_heap;
    struct lbHeap_HeapOffsetView* create_view;
    s32 view_offset;
    s32 create_index;
    uintptr_t arena_lo;
    uintptr_t aram_lo;
    uintptr_t aram_hi;
    uintptr_t arena_hi;
    struct Heap* main_heap;
    s32 destroy_index;
    struct Heap* aram_heap;
    uintptr_t view_address;

    /* Slots 0 and 1 are rebuilt after retained heaps reserve their ranges. */
    destroy_index = 2;
    view_address = (uintptr_t) &lbHeap_80431FA0.heap_array[destroy_index] -
                   offsetof(struct lbHeap_HeapOffsetView, heap);
    view_offset = lbHeap_HeapViewOffset(2);
    for (; destroy_index < 6; destroy_index++,
                              view_address += sizeof(struct Heap),
                              view_offset += sizeof(struct Heap))
    {
        if (((struct lbHeap_HeapOffsetView*) view_address)->heap.transient ==
            1)
        {
            destroy_view = lbHeap_GetHeapOffsetView(view_offset);
            lbHeap_DestroyOffsetViewIfCreated(destroy_view);
        }
    }

    arena_lo = (uintptr_t) lbHeap_80431FA0.arena_lo;
    arena_hi = (uintptr_t) lbHeap_80431FA0.arena_hi;
    aram_lo = lbHeap_80431FA0.aram_lo;
    aram_hi = lbHeap_80431FA0.aram_hi;

    /* Keep retained heaps outside the ranges available to HSD and ARAM. */
    for (reserve_index = 2; reserve_index < 6; reserve_index++) {
        reserved_heap = &lbHeap_80431FA0.heap_array[reserve_index];
        if (lbHeap_80431FA0.heap_array[reserve_index].transient == 0) {
            switch (reserved_heap->type) {
            case 1:
                reserved_end = reserved_heap->start + reserved_heap->size;
                if (arena_lo < reserved_end) {
                    arena_lo = reserved_end;
                }
                break;

            case 2:
                if (arena_hi > reserved_heap->start) {
                    arena_hi = reserved_heap->start;
                }
                break;

            case 4:
                reserved_end = reserved_heap->start + reserved_heap->size;
                if (aram_lo < reserved_end) {
                    aram_lo = reserved_end;
                }
                break;
            }
        }
    }

    main_heap = &lbHeap_80431FA0.heap_array[0];
    main_heap->id = HSD_CreateMainHeap((void*) arena_lo, (void*) arena_hi);
    main_heap->start = arena_lo;
    aram_heap = &lbHeap_80431FA0.heap_array[1];
    main_heap->size = arena_hi - arena_lo;
    main_heap->status = LbHeapStatus_Create;
    main_heap->type = 0;

    lbMemory_800155A4();

    aram_heap->handle = lbMemory_800154D4((void*) aram_lo, (void*) aram_hi);
    aram_heap->start = aram_lo;
    aram_heap->size = aram_hi - aram_lo;
    aram_heap->status = LbHeapStatus_Create;
    aram_heap->type = 3;

    for (create_index = 2, view_offset = lbHeap_HeapViewOffset(2);
         create_index < 6; create_index++, view_offset += sizeof(struct Heap))
    {
        if (lbHeap_80431FA0.heap_array[create_index].transient == 0) {
            create_view = lbHeap_GetHeapOffsetView(view_offset);
            lbHeap_CreateOffsetViewIfDestroyed(create_view);
        }
    }
}

LbHeapStatus lbHeap_80015BB8(int heap_index)
{
    return lbHeap_80431FA0.heap_array[heap_index].status;
}

void* lbHeap_80015BD0(int heap_index, size_t size)
{
    Handle* result;
    int interrupts_enabled = OSDisableInterrupts();
    struct Heap* heap = &lbHeap_80431FA0.heap_array[heap_index];

    if (heap->status == LbHeapStatus_Create) {
        if (heap->type == 0) {
            int previous_heap_id = HSD_GetHeap();
            HSD_SetHeap(heap->id);
            result = HSD_MemAlloc(size);
            HSD_SetHeap(previous_heap_id);
        } else {
            result = lbMemory_80014FC8(heap->handle, size);
            if (heap->type == 3) {
                result = result->x4_lo;
            }
        }
    } else {
        result = NULL;
    }
    OSRestoreInterrupts(interrupts_enabled);
    return result;
}

void lbHeap_80015CA8(int heap_index, void* address)
{
    int interrupts_enabled = OSDisableInterrupts();
    struct Heap* p = &lbHeap_80431FA0.heap_array[heap_index];

    HSD_ASSERT(0x143, p->status == LbHeapStatus_Create);
    if (p->type == 0) {
        int previous_heap_id = HSD_GetHeap();
        HSD_SetHeap(p->id);
        HSD_Free(address);
        HSD_SetHeap(previous_heap_id);
    } else {
        lbMemFreeToHeap(p->handle, address);
    }
    OSRestoreInterrupts(interrupts_enabled);
}

int lbHeap_80015D6C(u32 heap_index, void (*callback)(u32), u32 callback_arg)
{
    int interrupts_enabled = OSDisableInterrupts();
    struct Heap* heap = &lbHeap_80431FA0.heap_array[heap_index];
    int compaction_started;

    if (heap_index <= 1) {
        compaction_started = 0;
    } else {
        compaction_started =
            lbMemory_8001529C(heap->handle, callback, callback_arg);
    }
    OSRestoreInterrupts(interrupts_enabled);
    return compaction_started;
}

char* lbHeap_803BA448[] = {
    "     Hsd", "    ARAM", "     Seq", "    Stay", "    AllM", "    AllA",
};

void lbHeap_80015DF8(void)
{
    ssize_t total_bytes;
    struct Heap* heap;
    int heap_index;
    int free_bytes;

    OSReport("[lbHeap] -- Report --\n");

    for (heap_index = 0; heap_index < 6; heap_index++) {
        OSReport("%s :", lbHeap_803BA448[heap_index]);
        heap = &lbHeap_80431FA0.heap_array[heap_index];
        if (heap->status == LbHeapStatus_Create) {
            if (heap->type == 0) {
                free_bytes = OSCheckHeap(heap->id);
            } else {
                free_bytes = lbMemory_80014F7C(heap->handle);
            }
            OSReport(" %5d KB + ", (heap->size - free_bytes) / 1024);
            OSReport(" %5d KB( %8d)", free_bytes / 1024, free_bytes);
        } else {
            OSReport("                         destroy");
        }
        OSReport(" / %5d KB\n", heap->size / 1024, heap->size);
    }

    total_bytes = (uintptr_t) lbHeap_80431FA0.arena_hi -
                  (uintptr_t) lbHeap_80431FA0.arena_lo;
    OSReport("MainRAM Total : %5d KB( %8d)\n", total_bytes / 1024,
             total_bytes);
    total_bytes = lbHeap_80431FA0.aram_hi - lbHeap_80431FA0.aram_lo;
    OSReport("   ARAM Total : %5d KB( %8d)\n", total_bytes / 1024,
             total_bytes);
}

void lbHeap_80015F3C(void)
{
    int heap_index;
    int previous_index;
    struct Heap* heap;
    struct Heap* previous_heap;
    struct lbHeap_HeapDesc* layout;

    HSD_GetNextArena(&lbHeap_80431FA0.arena_lo, &lbHeap_80431FA0.arena_hi);
    lbMemory_800154BC(&lbHeap_80431FA0.aram_lo, &lbHeap_80431FA0.aram_hi);

    lbHeap_ResetHeap(&lbHeap_80431FA0.heap_array[0]);
    lbHeap_ResetHeap(&lbHeap_80431FA0.heap_array[1]);
    lbHeap_ResetHeap(&lbHeap_80431FA0.heap_array[2]);
    lbHeap_ResetHeap(&lbHeap_80431FA0.heap_array[3]);
    lbHeap_ResetHeap(&lbHeap_80431FA0.heap_array[4]);
    lbHeap_ResetHeap(&lbHeap_80431FA0.heap_array[5]);

    layout = lbHeap_803BA380;
    while ((heap_index = layout->idx) != 6) {
        heap = &lbHeap_80431FA0.heap_array[heap_index];

        heap->type = layout->type;
        heap->size = layout->size;
        previous_index = layout->prev_idx;
        if (previous_index == 6) {
            switch (heap->type) {
            case 3:
                break;
            case 1:
                heap->start = (uintptr_t) lbHeap_80431FA0.arena_lo;
                break;
            case 2:
                heap->start =
                    (uintptr_t) lbHeap_80431FA0.arena_hi - heap->size;
                break;
            case 4:
                heap->start = lbHeap_80431FA0.aram_lo;
                break;
            }
        } else {
            previous_heap = &lbHeap_80431FA0.heap_array[previous_index];
            switch (heap->type) {
            case 3:
                break;
            case 1:
                heap->start = previous_heap->start + previous_heap->size;
                break;
            case 2:
                heap->start = previous_heap->start - heap->size;
                break;
            case 4:
                heap->start = previous_heap->start + previous_heap->size;
                break;
            }
        }
        layout++;
    }
}
