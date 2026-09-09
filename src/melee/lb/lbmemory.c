#include "lbmemory.h"

#include <Runtime/platform.h>

#include <string.h>

#include <dolphin/ar.h>
#include <dolphin/os/OSAlarm.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/devcom.h>

struct MemEntry {
    /* 0x00 */ struct MemEntry* x0_next;
    /* 0x04 */ void* x4_lo;
    /* 0x08 */ void* x8_hi;
};

struct LBMgr {
    OSAlarm alarm;   // 0x00
    u8* src;         // 0x28
    u8* dst;         // 0x2C
    size_t size;     // 0x30
    size_t offset;   // 0x34
    intptr_t cb_arg; // 0x38
    HSD_DevComCallback cb;
};

struct Allocator {
    void* a_arenaLo;
    void* a_arenaHi;
    struct MemEntry x8_mem[0x83];
    Handle* free_mem;
    s32 x630_num_allocs;
    s32 x634_max_num_allocs;
    Handle x638_heap[6];
    Handle* free_heap;
    Handle* x69C;
    struct LBMgr x6A0_mgr;
    u32 x6E0;
    void* x6E4;
    void (*x6E8)(u32);
    u8 x6EC[0x6F0 - 0x6EC];
};

/* 015320 */ static void lbMemory_80015320(int request_id,
                                           intptr_t callback_arg, void* buffer,
                                           bool cancelflag);

struct Allocator lbMemory_804318B0;
#define _p(x) (lbMemory_804318B0.x)
ASSERT_SIZE(struct MemEntry, 0xC);
ASSERT_SIZE(lbMemory_804318B0, 0x6F0);

#define PUSH_HANDLE(list, handle)                                             \
    do {                                                                      \
        handle->x0_next = *list;                                              \
        *list = handle;                                                       \
    } while (0)
#define POP_HANDLE(list, handle)                                              \
    do {                                                                      \
        handle = *list;                                                       \
        *list = handle->x0_next;                                              \
    } while (0)

static inline Handle* new_handle(void* arenaLo, void* arenaHi)
{
    Handle* h;
    HSD_ASSERT(0x7B, _p(free_heap));

#ifdef MELEE_NATIVE
    HSD_ASSERT(0x80, (uintptr_t) arenaLo <= (uintptr_t) arenaHi);
    if ((uintptr_t) arenaLo < ARGetSize()) {
#else
    if (((uintptr_t) arenaLo < 0x80000000U) &&
        ((uintptr_t) arenaHi < 0x80000000U))
    {
#endif
        HSD_ASSERT(0x80, (uintptr_t) arenaLo >= (uintptr_t) _p(a_arenaLo) &&
                             (uintptr_t) arenaHi <= (uintptr_t) _p(a_arenaHi));
    }

    POP_HANDLE(&_p(free_heap), h);
    h->x0_next = NULL;
    h->x4_lo = arenaLo;
    h->x8_hi = arenaHi;
    h->xC_prev = NULL;
    return h;
}

Handle* lbMemory_80014E24(void* arenaLo, void* arenaHi)
{
    return new_handle(arenaLo, arenaHi);
}

void lbMemory_80014EEC(Handle* handle)
{
    Handle* allocation;
    Handle* next_allocation;
    HSD_ASSERT(149, handle);
    for (allocation = handle->xC_prev; allocation != NULL;) {
        next_allocation = allocation->x0_next;
        PUSH_HANDLE(&_p(free_mem), allocation);
        allocation = next_allocation;
        _p(x630_num_allocs) -= 1;
    }
    PUSH_HANDLE(&_p(free_heap), handle);
}

size_t lbMemory_80014F7C(Handle* heap)
{
    uintptr_t gap_end;
    uintptr_t gap_start = (uintptr_t) heap->x4_lo;
    /* Only x0_next is read through this view of the list head. */
    Handle* allocation = (Handle*) &heap->xC_prev;
    size_t free_bytes = 0;

    while (1) {
        allocation = allocation->x0_next;
        gap_end = (uintptr_t) ((allocation != NULL) ? allocation->x4_lo
                                                    : heap->x8_hi);
        free_bytes += gap_end - gap_start;
        if (allocation == NULL) {
            break;
        }
        gap_start =
            (uintptr_t) allocation->x4_lo + (uintptr_t) allocation->x8_hi;
    }
    return free_bytes;
}

Handle* lbMemory_80014FC8(Handle* heap, size_t size)
{
    void* allocation_start;
    Handle* memp_kouho;
    void* gap_end;
    size_t best_leftover;
    size_t leftover;
    size_t gap_size;
    void* gap_start;
    Handle* previous;

#ifdef MELEE_NATIVE
    best_leftover = SIZE_MAX;
    HSD_ASSERT(0xCC, size > 0 && size <= SIZE_MAX - 31);
#else
    best_leftover = 0x40000000U;
#endif
    HSD_ASSERT(0xCC, _p(free_mem));
    size = OSRoundUp32B(size);
    gap_start = heap->x4_lo;
    /* Treat the head link as the predecessor of the first allocation. */
    previous = (Handle*) &heap->xC_prev;
    memp_kouho = NULL;

    /* Best fit. Equal leftovers select the later gap in address order. */
    while (1) {
        gap_end = (previous->x0_next != NULL) ? previous->x0_next->x4_lo
                                              : heap->x8_hi;
        gap_size = (uintptr_t) gap_end - (uintptr_t) gap_start;
        if (gap_size >= size) {
            leftover = gap_size;
            leftover = leftover - size;
            if (leftover <= best_leftover) {
                best_leftover = leftover;
                allocation_start = gap_start;
                memp_kouho = previous;
            }
        }
        if (previous->x0_next == NULL) {
            break;
        }
        previous = previous->x0_next;
        gap_start = (void*) ((uintptr_t) previous->x4_lo +
                             (uintptr_t) previous->x8_hi);
    }
    HSD_ASSERT(0xE9, memp_kouho);
    {
        Handle* result;
        POP_HANDLE(&_p(free_mem), result);

        result->x8_hi = (void*) size;
        result->x4_lo = allocation_start;
        result->x0_next = memp_kouho->x0_next;
        memp_kouho->x0_next = result;

        _p(x630_num_allocs) += 1;
        if (_p(x630_num_allocs) > _p(x634_max_num_allocs)) {
            _p(x634_max_num_allocs) = _p(x630_num_allocs);
        }
        return result;
    }
}
void lbMemFreeToHeap(Handle* heap, void* address)
{
    Handle* allocation = heap->xC_prev;
    Handle** allocation_link = &heap->xC_prev;

    while (allocation != NULL) {
        if (allocation->x4_lo == address) {
            *allocation_link = allocation->x0_next;
            PUSH_HANDLE(&_p(free_mem), allocation);
            _p(x630_num_allocs) -= 1;
            return;
        }
        allocation_link = &allocation->x0_next;
        allocation = allocation->x0_next;
    }
#ifdef MELEE_NATIVE
    OSReport("[LbMem] Error: lbMemFreeToHeap %p.\n", address);
#else
    OSReport("[LbMem] Error: lbMemFreeToHeap %x.\n", address);
#endif
    HSD_ASSERT(283, 0);
}

static void fn_80015184(OSAlarm* alarm, OSContext* context)
{
    struct LBMgr* p;
    size_t remaining_bytes;
    size_t copied_bytes;
    size_t chunk_size;

    p = &_p(x6A0_mgr);
    HSD_ASSERT(0x127, p->size);
    copied_bytes = p->offset;
    remaining_bytes = p->size - copied_bytes;
    chunk_size = remaining_bytes;
    if (remaining_bytes > 0x19000U) {
        chunk_size = 0x19000;
    }
#ifdef MELEE_NATIVE
    memmove(p->dst + copied_bytes, p->src + copied_bytes, chunk_size);
#else
    memcpy(p->dst + copied_bytes, p->src + copied_bytes, chunk_size);
#endif
    p->offset += chunk_size;
    if (p->offset == p->size) {
        p->size = 0U;
        p->cb(0, p->cb_arg, 0, 0);
        return;
    }
    OSCreateAlarm(&p->alarm);
    OSSetAlarm(&p->alarm, OSMillisecondsToTicks(3), fn_80015184);
}

u32 lbMemory_8001529C(Handle* heap, void (*callback)(u32), u32 callback_arg)
{
    void* allocation_start;
    Handle* allocation;
    void** compact_end;

    _p(x6E8) = callback;
    _p(x6E0) = callback_arg;
    _p(x6E4) = heap->x4_lo;

    compact_end = &_p(x6E4);

    /* Skip the packed prefix. The callback chain moves the remaining nodes. */
    for (allocation = heap->xC_prev; allocation != NULL;
         allocation = allocation->x0_next)
    {
        allocation_start = allocation->x4_lo;
        if (allocation_start != *compact_end) {
            lbMemory_80015320(0, (intptr_t) allocation, NULL, false);
            return 1;
        }
        *compact_end = (void*) ((uintptr_t) allocation_start +
                                (uintptr_t) allocation->x8_hi);
    }
    return 0;
}

static void start_ram_copy(uintptr_t source, uintptr_t destination,
                           size_t size, Handle* next_allocation)
{
    struct LBMgr* p = &_p(x6A0_mgr);
    int interrupts_enabled = OSDisableInterrupts();

    HSD_ASSERT(0x14F, !p->size);
    p->src = (u8*) source;
    p->dst = (u8*) destination;
    p->size = size;
    p->offset = 0;
    p->cb_arg = (intptr_t) next_allocation;
    p->cb = lbMemory_80015320;
    OSRestoreInterrupts(interrupts_enabled);
    OSCreateAlarm(&p->alarm);
    OSSetAlarm(&p->alarm, OSMillisecondsToTicks(3), fn_80015184);
}

static void lbMemory_80015320(int request_id, intptr_t callback_arg,
                              void* buffer, bool cancelflag)
{
    void* null_or_source;
    Handle* handle = (Handle*) callback_arg;
    void** compact_end;
    void* source;
    uintptr_t destination;
    void* copy_source;
    void* allocation_start;

    compact_end = &_p(x6E4);
    destination = (uintptr_t) _p(x6E4);
    null_or_source = NULL;

    HSD_ASSERT(0x188, !cancelflag);

    if (handle != null_or_source) {
        allocation_start = handle->x4_lo;
        if ((source = allocation_start) != (void*) destination) {
            null_or_source = source;
            /* Store the new address before the asynchronous copy starts. */
            handle->x4_lo = (void*) destination;
            *compact_end = (void*) ((uintptr_t) handle->x4_lo +
                                    (uintptr_t) handle->x8_hi);
            copy_source = null_or_source;

#ifdef MELEE_NATIVE
            if ((uintptr_t) handle->x4_lo < ARGetSize()) {
#else
            if ((uintptr_t) handle->x4_lo < 0x80000000U) {
#endif
                HSD_DevComRequest(0, (uintptr_t) copy_source, destination,
                                  OSRoundUp32B(handle->x8_hi), 0x1B, 1,
                                  lbMemory_80015320, handle->x0_next);
                return;
            } else {
                start_ram_copy((uintptr_t) copy_source, destination,
                               OSRoundUp32B(handle->x8_hi), handle->x0_next);
                return;
            }
        }

        *compact_end =
            (void*) ((uintptr_t) source + (uintptr_t) handle->x8_hi);
        lbMemory_80015320(0, (intptr_t) handle->x0_next, null_or_source,
                          false);
        return;
    }

    _p(x6E8)(_p(x6E0));
}

void lbMemory_800154BC(uintptr_t* arenaLo, uintptr_t* arenaHi)
{
    *arenaLo = (uintptr_t) _p(a_arenaLo);
    *arenaHi = (uintptr_t) _p(a_arenaHi);
}

Handle* lbMemory_800154D4(void* arenaLo, void* arenaHi)
{
    _p(x69C) = new_handle(arenaLo, arenaHi);
    return _p(x69C);
}

void lbMemory_800155A4(void)
{
    Handle* handle = _p(x69C);
    Handle* allocation;

    Handle** free_list;

    HSD_ASSERT(149, handle);
    free_list = &_p(free_mem);
    for (allocation = handle->xC_prev; allocation != NULL;) {
        Handle* next_allocation = allocation->x0_next;
        PUSH_HANDLE(free_list, allocation);
        allocation = next_allocation;
        _p(x630_num_allocs) -= 1;
    }
    PUSH_HANDLE(&_p(free_heap), handle);
    _p(x69C) = NULL;
}

#ifdef MUST_MATCH
#pragma push
#pragma dont_inline on
#endif
void lbMemory_8001564C(void)
{
    u32 size[3];
    int i;
    u8* base = (u8*) &lbMemory_804318B0;

    _p(a_arenaLo) = (void*) (uintptr_t) ARAlloc(0x20);
    ARFree(&size[2]);
    _p(a_arenaHi) =
        (void*) (uintptr_t) ((ARGetSize() > 0x01000000U) ? 0x01000000U
                                                         : ARGetSize());

    _p(free_mem) = (Handle*) &_p(x8_mem)[0];
    for (i = 0; i < 0x82; i++) {
        _p(x8_mem)[i].x0_next = &_p(x8_mem)[i + 1];
    }
    _p(x8_mem)[i].x0_next = NULL;

    _p(x634_max_num_allocs) = 0;
    _p(x630_num_allocs) = 0;
#ifdef MELEE_NATIVE
    _p(free_heap) = &_p(x638_heap)[0];
    for (i = 0; i < ARRAY_SIZE(_p(x638_heap)) - 1; i++) {
        _p(x638_heap)[i].x0_next = &_p(x638_heap)[i + 1];
    }
    _p(x638_heap)[i].x0_next = NULL;
#else
    // The chain below walks _p(x638_heap)[0..5], one Handle (0x10) apart.
    // Writing it through the array instead does not match.
    _p(free_heap) = &_p(x638_heap)[0];
    *(void**) (base + 0x638) = base + 0x648;
    *(void**) (base + 0x648) = base + 0x658;
    *(void**) (base + 0x658) = base + 0x668;
    *(void**) (base + 0x668) = base + 0x678;
    *(void**) (base + 0x678) = base + 0x688;
    *(void**) (base + 0x688) = NULL;
#endif
    _p(x69C) = NULL;
    {
        void* hi = _p(a_arenaHi);
        void* lo = _p(a_arenaLo);
        _p(x69C) = lbMemory_80014E24(lo, hi);
    }
    _p(x6A0_mgr).size = 0; // base + 0x6D0 on PowerPC
}
#ifdef MUST_MATCH
#pragma pop
#endif
