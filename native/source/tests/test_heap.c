#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dolphin/os.h>
#include <sysdolphin/baselib/random.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            abort();                                                          \
        }                                                                     \
    } while (0)

static size_t visited;
static size_t visited_bytes;

static void visit(void* pointer, size_t size)
{
    CHECK(pointer != NULL && size >= 32);
    visited++;
    visited_bytes += size;
}

int main(void)
{
    u8* arena;
    u8* outside;
    void* slots[128] = { 0 };
    size_t lengths[128] = { 0 };
    uint32_t random = 7;
    int heap;
    int other;
    size_t initial;
    size_t i;
    const u32 random_values[] = { 41, 51235, 6334, 59268, 51937 };

    CHECK(sizeof(void*) == 8 && sizeof(u32) == 4 && sizeof(s32) == 4);
    CHECK(posix_memalign((void**) &arena, 32, 65536) == 0);
    CHECK(posix_memalign((void**) &outside, 32, 1024) == 0);
    CHECK((uintptr_t) arena > UINT32_MAX);
    CHECK(OSInitAlloc(arena, arena + 65536, 4) == arena);
    CHECK(OSGetArenaLo() == arena && OSGetArenaHi() == arena + 65536);
    CHECK(OSCreateHeap(outside, outside + 1024) == -1);
    heap = OSCreateHeap(arena, arena + 32768);
    other = OSCreateHeap(arena + 32768, arena + 65536);
    CHECK(heap >= 0 && other >= 0 && heap != other);
    CHECK(OSCreateHeap(arena + 32, arena + 1024) == -1);
    CHECK(OSSetCurrentHeap(heap) == -1);
    initial = (size_t) OSCheckHeap(heap);
    CHECK(initial == 32736);
    CHECK(OSAllocFromHeap(heap, SIZE_MAX) == NULL);
    CHECK(OSAllocFromHeap(heap, (size_t) 1 << 32) == NULL);
    CHECK(OSAllocFromHeap(heap, 0) == NULL);

    // Interleaved allocation and release must preserve every live payload.
    for (i = 0; i < 6000; i++) {
        size_t index;
        size_t j;
        random = random * 1664525U + 1013904223U;
        index = (random >> 16) % ARRAY_SIZE(slots);
        if (slots[index] != NULL) {
            for (j = 0; j < lengths[index]; j++) {
                CHECK(((u8*) slots[index])[j] == (u8) index);
            }
            OSFreeToHeap(heap, slots[index]);
            slots[index] = NULL;
        } else {
            lengths[index] = 1 + random % 192;
            slots[index] = OSAllocFromHeap(heap, lengths[index]);
            CHECK(slots[index] != NULL);
            CHECK((uintptr_t) slots[index] % 32 == 0);
            CHECK(OSReferentSize(slots[index]) >= lengths[index]);
            memset(slots[index], (u8) index, lengths[index]);
        }
        CHECK(OSCheckHeap(heap) >= 0);
    }
    OSVisitAllocated(visit);
    CHECK(visited > 0 && visited_bytes < initial);
    for (i = 0; i < ARRAY_SIZE(slots); i++) {
        if (slots[i] != NULL) {
            OSFreeToHeap(heap, slots[i]);
        }
    }
    CHECK(OSCheckHeap(heap) == (intptr_t) initial);
    CHECK(OSCheckHeap(other) == (intptr_t) initial);
    slots[0] = OSAllocFromHeap(heap, initial);
    CHECK(slots[0] != NULL && OSAllocFromHeap(heap, 1) == NULL);
    OSFreeToHeap(heap, slots[0]);
    OSDestroyHeap(heap);
    CHECK(OSCreateHeap(arena, arena + 32768) == heap);
    OSDestroyHeap(heap);
    OSDestroyHeap(other);

    OSSetArenaLo(arena + 1);
    OSSetArenaHi(arena + 65535);
    CHECK(OSAllocFromArenaLo(16, 32) == arena + 32);
    CHECK(OSGetArenaLo() == arena + 64);
    CHECK(OSAllocFromArenaHi(32, 32) == arena + 65472);
    OSSetArenaLo(arena + 1);
    OSSetArenaHi(arena + 65535);
    CHECK(OSAllocFromArenaHi(1, 32) == arena + 65472);
    OSSetArenaLo(arena + 1);
    OSSetArenaHi(arena + 65535);
    CHECK(OSAllocFromArenaLo(1, 64) == arena + 64);
    CHECK(OSGetArenaLo() == arena + 128);
    CHECK(OSAllocFromArenaLo(SIZE_MAX, 32) == NULL);
    CHECK(OSAllocFromArenaHi(1, 3) == NULL);

    *seed_ptr = 1;
    for (i = 0; i < ARRAY_SIZE(random_values); i++) {
        CHECK((u32) HSD_Rand() == random_values[i]);
    }
    free(outside);
    free(arena);
    puts("Native heap and game integer tests passed.");
    return 0;
}
