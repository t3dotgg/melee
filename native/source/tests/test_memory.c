#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sysdolphin/baselib/class.h>
#include <sysdolphin/baselib/id.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/list.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/object.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            abort();                                                          \
        }                                                                     \
    } while (0)

static void* allocations[1024];
static size_t allocation_count;

// These heap hooks run the real HSD allocators without the console OS.
void* HSD_MemAlloc(ssize_t size)
{
    void* memory;
    CHECK(size > 0);
    CHECK(allocation_count < ARRAY_SIZE(allocations));
    CHECK(posix_memalign(&memory, 32, size) == 0);
    memset(memory, 0xA5, size);
    allocations[allocation_count++] = memory;
    return memory;
}

OSHeapHandle HSD_GetHeap(void)
{
    return 0;
}

long OSCheckHeap(int heap)
{
    (void) heap;
    return LONG_MAX;
}

void OSReport(char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

void __assert(char* file, u32 line, char* condition)
{
    fprintf(stderr, "%s:%u: %s\n", file, line, condition);
    abort();
}

static void test_object_pool(void)
{
    static HSD_ObjAllocData pool;
    unsigned char* arena = HSD_MemAlloc(128);
    void* objects[3];
    size_t i;

    CHECK((uintptr_t) arena > UINT32_MAX);
    HSD_ObjSetHeap(31, arena + 1);
    HSD_ObjAllocInit(&pool, 3, 4);
    CHECK(pool.size >= sizeof(void*));
    CHECK(pool.align + 1 >= _Alignof(void*));
    CHECK(HSD_ObjAllocAddFree(&pool, 0) == 0);
    CHECK(HSD_ObjAllocAddFree(&pool, 4) == 3);
    for (i = 0; i < ARRAY_SIZE(objects); i++) {
        objects[i] = HSD_ObjAlloc(&pool);
        CHECK(objects[i] == arena + 8 * (i + 1));
        CHECK((uintptr_t) objects[i] % _Alignof(void*) == 0);
    }
    CHECK(HSD_ObjAlloc(&pool) == NULL);
    HSD_ObjFree(&pool, objects[1]);
    CHECK(HSD_ObjAlloc(&pool) == objects[1]);
    CHECK(pool.used == 3 && pool.free == 0 && pool.peak == 3);
    for (i = 0; i < ARRAY_SIZE(objects); i++) {
        HSD_ObjFree(&pool, objects[i]);
    }
    HSD_ObjSetHeap(SIZE_MAX, NULL);
}

static void test_ids(void)
{
    int first = 1;
    int second = 2;
    uintptr_t first_id = (uintptr_t) &first;
    uintptr_t second_id = first_id ^ ((uintptr_t) 1 << 32);
    s32 success;

    CHECK(first_id > UINT32_MAX);
    CHECK((u32) first_id == (u32) second_id);
    HSD_IDInitAllocData();
    HSD_IDSetup();
    HSD_IDInsertToTable(NULL, first_id, &first);
    HSD_IDInsertToTable(NULL, second_id, &second);
    CHECK(HSD_IDGetData(first_id, &success) == &first && success == 1);
    CHECK(HSD_IDGetData(second_id, &success) == &second && success == 1);
    HSD_IDRemoveByIDFromTable(NULL, first_id);
    CHECK(HSD_IDGetData(first_id, &success) == NULL && success == 0);
    CHECK(HSD_IDGetData(second_id, NULL) == &second);
    HSD_IDRemoveByIDFromTable(NULL, second_id);
}

static void test_class_growth(void)
{
    const s32 sizes[] = { 8, 4096, 8192 };
    void* pieces[ARRAY_SIZE(sizes)];
    size_t i;
    HSD_Obj* object;

    // Poisoned allocations expose a partially cleared pointer table on resize.
    for (i = 0; i < ARRAY_SIZE(sizes); i++) {
        pieces[i] = hsdAllocMemPiece(sizes[i]);
        CHECK(pieces[i] != NULL);
        CHECK((uintptr_t) pieces[i] > UINT32_MAX);
        memset(pieces[i], 0x5A, sizes[i]);
    }
    for (i = 0; i < ARRAY_SIZE(sizes); i++) {
        hsdFreeMemPiece(pieces[i], sizes[i]);
        CHECK(hsdAllocMemPiece(sizes[i]) == pieces[i]);
        hsdFreeMemPiece(pieces[i], sizes[i]);
    }
    object = hsdNew(&hsdObj);
    CHECK(object != NULL && object->parent.class_info == &hsdObj);
    CHECK(hsdObj.head.nb_exist == 1);
    hsdDelete(object);
    CHECK(hsdObj.head.nb_exist == 0);
}

static void test_lists(void)
{
    int value = 7;
    HSD_SList* list;
    HSD_ListInitAllocData();
    list = HSD_SListAllocAndPrepend(NULL, &value);
    CHECK(list->data == &value);
    CHECK(HSD_SListRemove(list) == NULL);
}

int main(void)
{
    size_t i;
    CHECK(sizeof(void*) == 8 && sizeof(u32) == 4);
    test_object_pool();
    test_ids();
    test_class_growth();
    test_lists();
    for (i = 0; i < allocation_count; i++) {
        free(allocations[i]);
    }
    puts("Native memory tests passed.");
    return 0;
}
