#include "objalloc.h"

#include <string.h>

#include "initialize.h"
#include "memory.h"
#include <dolphin/os/OSAlloc.h>

static objheap obj_heap = { 0, 0, -1, -1 };

static HSD_ObjAllocData* alloc_datas;

static inline size_t getHeapFreeSize(void)
{
    if (obj_heap.top != 0) {
        return obj_heap.remain;
    }
    return OSCheckHeap(HSD_GetHeap());
}

void HSD_ObjSetHeap(size_t size, void* ptr)
{
    obj_heap.curr = (uintptr_t) ptr;
    obj_heap.top = (uintptr_t) ptr;
    obj_heap.remain = size;
    obj_heap.size = size;
}

s32 HSD_ObjAllocAddFree(HSD_ObjAllocData* data, u32 num)
{
    uintptr_t aligned_start;
    uintptr_t pool_end;
    size_t pool_size;
    u8* pool_start;

    u8 _[4];

    HSD_ASSERT(0xEE, data);
#ifdef MELEE_NATIVE
    if (num == 0 || num > S32_MAX || data->size == 0 ||
        num > SIZE_MAX / data->size) {
        return 0;
    }
#endif
    pool_size = data->size * num;
    if (obj_heap.top != 0) {
#ifdef MELEE_NATIVE
        if (obj_heap.size > UINTPTR_MAX - obj_heap.top ||
            data->align > UINTPTR_MAX - obj_heap.curr) {
            return 0;
        }
#endif
        pool_end = obj_heap.top + obj_heap.size;
        aligned_start = (obj_heap.curr + data->align) & ~data->align;
        pool_start = (u8*) aligned_start;
        if (aligned_start > pool_end) {
            return 0;
        }
        if (pool_end - (uintptr_t) pool_start < pool_size) {
            pool_size = pool_end - (uintptr_t) pool_start -
                        (pool_end - (uintptr_t) pool_start) % data->size;
        }
        num = pool_size / data->size;
        if (num == 0) {
            return 0;
        }
        obj_heap.curr = (uintptr_t) pool_start + pool_size;
        obj_heap.remain = pool_end - obj_heap.curr;
    } else {
#ifdef MELEE_NATIVE
        if (pool_size > PTRDIFF_MAX) {
            return 0;
        }
#endif
        pool_start = HSD_MemAlloc(pool_size);
        if (pool_start == NULL) {
            return 0;
        }
        obj_heap.remain -= pool_size;
    }

    {
        int index;
        for (index = 0; (unsigned) index < num - 1; index++) {
            ((HSD_ObjAllocLink*) (pool_start + data->size * index))->next =
                (HSD_ObjAllocLink*) (pool_start + data->size * (index + 1));
        }
        ((HSD_ObjAllocLink*) (pool_start + data->size * index))->next =
            data->freehead;
    }

    data->freehead = (HSD_ObjAllocLink*) pool_start;
    data->free += num;
    return num;
}

void* HSD_ObjAlloc(HSD_ObjAllocData* data)
{
    HSD_ObjAllocLink* object;
    size_t free_heap_bytes;

    if (data->num_limit_flag && data->used >= data->num_limit) {
        return NULL;
    }
    if (data->heap_limit_flag) {
        if (data->heap_limit_num == (unsigned) -1) {
            free_heap_bytes = getHeapFreeSize();
            if (free_heap_bytes <= data->heap_limit_size) {
                /* Permit reuse of this pool, but stop it from growing. */
                data->heap_limit_num = data->used + data->free;
            }
        } else {
            free_heap_bytes = getHeapFreeSize();
            if (free_heap_bytes > data->heap_limit_size) {
                data->heap_limit_num = -1;
            }
        }
        if (data->used >= data->heap_limit_num) {
            return NULL;
        }
    }
    if (data->free == 0) {
        HSD_ObjAllocAddFree(data, 1);
        if (data->free == 0) {
            return NULL;
        }
    }
    object = data->freehead;
    data->freehead = object->next;
    data->used += 1;
    data->free -= 1;
    if (data->used > data->peak) {
        data->peak = data->used;
    }
    return object;
}

void HSD_ObjFree(HSD_ObjAllocData* data, void* obj)
{
    HSD_ObjAllocLink* link = obj;
    link->next = data->freehead;
    data->freehead = link;
    data->free += 1;
    data->used -= 1;
}

static inline void removeAll(HSD_ObjAllocData* data)
{
    HSD_ObjAllocData** data_link = &alloc_datas;
    while (*data_link != NULL) {
        if (*data_link == data) {
            *data_link = (*data_link)->next;
        } else {
            data_link = &(*data_link)->next;
        }
    }
}

void HSD_ObjAllocInit(HSD_ObjAllocData* data, size_t size, u32 align)
{
    HSD_ASSERT(0x185, data);
#ifdef MELEE_NATIVE
    // Free objects store a pointer in their first bytes.
    HSD_ASSERT(0x185, align != 0 && (align & (align - 1)) == 0);
    if (align < _Alignof(HSD_ObjAllocLink)) {
        align = _Alignof(HSD_ObjAllocLink);
    }
    if (size < sizeof(HSD_ObjAllocLink)) {
        size = sizeof(HSD_ObjAllocLink);
    }
    HSD_ASSERT(0x185, size <= SIZE_MAX - (align - 1));
#endif
    if (data != NULL) {
        removeAll(data);
    } else {
        alloc_datas = NULL;
    }
    memset(data, 0, sizeof(HSD_ObjAllocData));
    data->num_limit = -1;
    data->heap_limit_size = 0;
    data->heap_limit_num = -1;
    data->align = align - 1;
    data->size = (size + data->align) & ~data->align;
    data->next = alloc_datas;
    alloc_datas = data;
}

void _HSD_ObjAllocForgetMemory(void* low, void* high)
{
    alloc_datas = NULL;
}
