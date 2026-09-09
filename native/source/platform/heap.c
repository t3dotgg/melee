#include <dolphin/os.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct NativeBlock {
    struct NativeBlock* next;
    struct NativeBlock* prev;
    size_t size;
    int allocated;
} NativeBlock;

typedef struct NativeHeap {
    uintptr_t start;
    uintptr_t end;
    NativeBlock* first;
} NativeHeap;

_Static_assert(sizeof(NativeBlock) == 32, "Heap headers must retain alignment");

static NativeHeap* heaps;
static int heap_count;
static void* arena_lo;
static void* arena_hi;
/* OSSetArenaLo moves the allocation cursor. Heap bounds must continue to use
 * the full arena, because the game creates heaps after moving that cursor. */
static void* arena_base_lo;
static void* arena_base_hi;
volatile OSHeapHandle __OSCurrHeap = -1;

static bool round_up_uintptr(uintptr_t value, size_t align, uintptr_t* result)
{
    uintptr_t mask;

    if (align == 0 || (align & (align - 1)) != 0) {
        return false;
    }
    mask = (uintptr_t) align - 1;
    if (value > UINTPTR_MAX - mask) {
        return false;
    }
    *result = (value + mask) & ~mask;
    return true;
}

static NativeHeap* get_heap(int handle)
{
    if (handle < 0 || handle >= heap_count || heaps[handle].first == NULL) {
        OSPanic(__FILE__, __LINE__, "Invalid native heap %d", handle);
    }
    return &heaps[handle];
}

static NativeBlock* find_block(NativeHeap* heap, void* pointer)
{
    NativeBlock* block;
    for (block = heap->first; block != NULL; block = block->next) {
        if ((void*) (block + 1) == pointer && block->allocated) {
            return block;
        }
    }
    OSPanic(__FILE__, __LINE__, "Pointer does not belong to this native heap");
}

void* OSInitAlloc(void* start, void* end, int max_heaps)
{
    NativeHeap* next;
    uintptr_t low;
    uintptr_t high;
    if (max_heaps <= 0 || (uintptr_t) start >= (uintptr_t) end ||
        !round_up_uintptr((uintptr_t) start, 32, &low)) {
        return NULL;
    }
    high = (uintptr_t) end & ~(uintptr_t) 31;
    if (low >= high) {
        return NULL;
    }
    next = calloc((size_t) max_heaps, sizeof(*next));
    if (next == NULL) {
        return NULL;
    }
    free(heaps);
    heaps = next;
    heap_count = max_heaps;
    __OSCurrHeap = -1;
    arena_lo = (void*) low;
    arena_hi = (void*) high;
    arena_base_lo = (void*) low;
    arena_base_hi = (void*) high;
    return (void*) low;
}

int OSCreateHeap(void* start, void* end)
{
    uintptr_t low;
    uintptr_t high = OSRoundDown32B(end);
    int handle = -1;
    int i;
    if ((uintptr_t) start < (uintptr_t) arena_base_lo ||
        (uintptr_t) end > (uintptr_t) arena_base_hi ||
        (uintptr_t) start > UINTPTR_MAX - 31) {
        return -1;
    }
    low = OSRoundUp32B(start);
    if (low >= high || high - low < sizeof(NativeBlock) + 32 ||
        high - low > INTPTR_MAX) {
        return -1;
    }
    for (i = 0; i < heap_count; i++) {
        if (heaps[i].first == NULL) {
            if (handle == -1) {
                handle = i;
            }
        } else if (low < heaps[i].end && high > heaps[i].start) {
            return -1;
        }
    }
    if (handle >= 0) {
        NativeBlock* block = (NativeBlock*) low;
        *block = (NativeBlock) { .size = high - low - sizeof(*block) };
        heaps[handle] = (NativeHeap) { low, high, block };
    }
    return handle;
}

void OSDestroyHeap(int handle)
{
    NativeHeap* heap = get_heap(handle);
    // Scene changes discard the whole arena, including remaining allocations.
    memset(heap, 0, sizeof(*heap));
    if (__OSCurrHeap == handle) {
        __OSCurrHeap = -1;
    }
}

int OSSetCurrentHeap(int handle)
{
    int previous = __OSCurrHeap;
    get_heap(handle);
    __OSCurrHeap = handle;
    return previous;
}

void* OSAllocFromHeap(int handle, size_t size)
{
    NativeHeap* heap = get_heap(handle);
    NativeBlock* block;
    if (size == 0 || size > SIZE_MAX - 31) {
        return NULL;
    }
    size = (size + 31) & ~(size_t) 31;
    for (block = heap->first; block != NULL; block = block->next) {
        if (block->allocated || block->size < size) {
            continue;
        }
        if (block->size - size >= sizeof(*block) + 32) {
            NativeBlock* rest = (NativeBlock*) ((u8*) (block + 1) + size);
            *rest = (NativeBlock) {
                .next = block->next, .prev = block,
                .size = block->size - size - sizeof(*block),
            };
            if (rest->next != NULL) {
                rest->next->prev = rest;
            }
            block->next = rest;
            block->size = size;
        }
        block->allocated = 1;
        return block + 1;
    }
    return NULL;
}

static void join_next(NativeBlock* block)
{
    NativeBlock* next = block->next;
    if (next != NULL && !next->allocated) {
        block->size += sizeof(*next) + next->size;
        block->next = next->next;
        if (block->next != NULL) {
            block->next->prev = block;
        }
    }
}

void OSFreeToHeap(int handle, void* pointer)
{
    NativeBlock* block = find_block(get_heap(handle), pointer);
    block->allocated = 0;
    join_next(block);
    if (block->prev != NULL && !block->prev->allocated) {
        join_next(block->prev);
    }
}

intptr_t OSCheckHeap(int handle)
{
    NativeHeap* heap = get_heap(handle);
    NativeBlock* block;
    NativeBlock* previous = NULL;
    uintptr_t cursor = heap->start;
    size_t free_size = 0;
    for (block = heap->first; block != NULL; block = block->next) {
        if ((uintptr_t) block != cursor || cursor > heap->end ||
            heap->end - cursor < sizeof(*block) || block->prev != previous ||
            block->size > heap->end - cursor - sizeof(*block)) {
            return -1;
        }
        if (!block->allocated) {
            free_size += block->size;
        }
        cursor += sizeof(*block) + block->size;
        previous = block;
    }
    return cursor == heap->end ? (intptr_t) free_size : -1;
}

size_t OSReferentSize(void* pointer)
{
    int i;
    uintptr_t address = (uintptr_t) pointer;
    for (i = 0; i < heap_count; i++) {
        if (heaps[i].first != NULL && address > heaps[i].start &&
            address < heaps[i].end) {
            return find_block(&heaps[i], pointer)->size;
        }
    }
    OSPanic(__FILE__, __LINE__, "Pointer does not belong to a native heap");
}

void OSVisitAllocated(void (*visitor)(void*, size_t))
{
    int i;
    // NativeBlock::size is the aligned payload size, excluding its header.
    for (i = 0; i < heap_count; i++) {
        NativeBlock* block;
        for (block = heaps[i].first; block != NULL; block = block->next) {
            if (block->allocated) {
                visitor(block + 1, block->size);
            }
        }
    }
}

void OSDumpHeap(int handle)
{
    OSReport("Native heap %d has %zu free bytes\n", handle,
             (size_t) OSCheckHeap(handle));
}

void* OSGetArenaLo(void) { return arena_lo; }
void* OSGetArenaHi(void) { return arena_hi; }
void OSSetArenaLo(void* pointer) { arena_lo = pointer; }
void OSSetArenaHi(void* pointer) { arena_hi = pointer; }

void* OSAllocFromArenaLo(size_t size, size_t align)
{
    uintptr_t low = (uintptr_t) arena_lo;
    uintptr_t high = (uintptr_t) arena_hi;
    uintptr_t next;
    if (!round_up_uintptr(low, align, &low)) {
        return NULL;
    }
    if (low > high || size > high - low) {
        return NULL;
    }
    if (!round_up_uintptr(low + size, align, &next) || next > high) {
        return NULL;
    }
    arena_lo = (void*) next;
    return (void*) low;
}

void* OSAllocFromArenaHi(size_t size, size_t align)
{
    uintptr_t low = (uintptr_t) arena_lo;
    uintptr_t high = (uintptr_t) arena_hi;
    uintptr_t mask;
    if (align == 0 || (align & (align - 1)) != 0 || low > high) {
        return NULL;
    }
    mask = (uintptr_t) align - 1;
    high &= ~mask;
    if (high < low || size > high - low) {
        return NULL;
    }
    high -= size;
    high &= ~mask;
    if (high < low) {
        return NULL;
    }
    arena_hi = (void*) high;
    return arena_hi;
}
