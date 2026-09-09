#include <stdio.h>
#include <stdlib.h>

#include <melee/it/it_3F14.h>
#include <melee/it/itspawn.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/memory.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

void* HSD_MemAlloc(ssize_t size)
{
    void* result = malloc(size);
    CHECK(result != NULL);
    return result;
}

int main(void)
{
    s32 counts[It_Kind_L_Gun_Ray] = { 0 };
    counts[It_Kind_BombHei] = 2;
    counts[It_Kind_Dosei] = 3;
    it_804A0E30.x0 = 123;
    it_804A0E30.x4.size = 9;
    it_8026CA4C(&it_804A0E50, counts, 3, It_Kind_BombHei, 0.5f);
    it_8026CD50(counts, 3, 0.5f);
    CHECK(it_804A0E50.size == 2 && it_804A0E50.x8 == 3);
    CHECK(it_804A0E50.x4[0] == It_Kind_BombHei);
    CHECK(it_804A0E50.x4[1] == It_Kind_Dosei);
    CHECK(it_804A0E50.xC[0] == 0 && it_804A0E50.xC[1] == 1);
    CHECK(it_804A0E30.x0 == 123 && it_804A0E30.x4.size == 9);
    free(it_804A0E50.x4);
    free(it_804A0E50.xC);
    puts("item spawn test passed");
    return 0;
}
