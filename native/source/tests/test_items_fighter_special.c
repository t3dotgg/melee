#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../assets/items.h"
#include "../assets/items_fighter_special.h"
#include <melee/it/itCharItems.h>
#include <melee/it/itCommonItems.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/aobj.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            abort();                                                          \
        }                                                                     \
    } while (0)

typedef struct TestArchive {
    NativeArchive* source;
    NativeArchiveGraph* graph;
    NativeItemArchive* items;
} TestArchive;

static void word(uint8_t* data, size_t offset, uint32_t value)
{
    data[offset] = (uint8_t) (value >> 24);
    data[offset + 1] = (uint8_t) (value >> 16);
    data[offset + 2] = (uint8_t) (value >> 8);
    data[offset + 3] = (uint8_t) value;
}

static TestArchive openArchive(const uint8_t* data, size_t size,
                               const uint32_t* relocations, size_t count)
{
    size_t file_size = 32 + size + count * 4;
    uint8_t* file = calloc(1, file_size);
    TestArchive archive;
    NativeArchiveError error;
    size_t i;
    CHECK(file != NULL);
    word(file, 0, (uint32_t) file_size);
    word(file, 4, (uint32_t) size);
    word(file, 8, (uint32_t) count);
    memcpy(file + 32, data, size);
    for (i = 0; i < count; ++i) {
        word(file, 32 + size + i * 4, relocations[i]);
    }
    CHECK(NativeArchiveOpen(file, file_size, &archive.source, &error) ==
          NATIVE_ARCHIVE_OK);
    free(file);
    CHECK(NativeArchiveGraphOpen(archive.source, &archive.graph, &error) ==
          NATIVE_ARCHIVE_OK);
    archive.items = NativeItemArchiveOpen(archive.source, archive.graph);
    CHECK(archive.items != NULL);
    return archive;
}

static void closeArchive(TestArchive* archive)
{
    NativeItemArchiveClose(archive->items);
    NativeArchiveGraphClose(archive->graph);
    NativeArchiveClose(archive->source);
}

static void testShortScalar(void)
{
    uint8_t data[0x10] = { 0 };
    NativeArchiveError error;
    TestArchive archive;
    void* value;
    word(data, 0, 0x3F800000);
    word(data, 0xC, 0x40000000);
    archive = openArchive(data, sizeof(data), NULL, 0);
    CHECK(NativeItemFighterSpecialRead(archive.items, It_Kind_Kirby_LuigiFire,
                                       0, &value,
                                       &error) == NATIVE_ARCHIVE_OK);
    CHECK(((itUnkAttributes*) value)->x0_float == 1.0F);
    CHECK(((itUnkAttributes*) value)->xC == 2.0F);
    CHECK(((itUnkAttributes*) value)->x10 == 0.0F);
    CHECK(NativeItemFighterSpecialRead(archive.items, It_Kind_Mario_Fire, 0,
                                       &value,
                                       &error) == NATIVE_ARCHIVE_BOUNDS);
    CHECK(value == NULL);
    closeArchive(&archive);
}

static void testArrowReferences(void)
{
    uint8_t data[0x80] = { 0 };
    const uint32_t relocations[] = { 0x24, 0x28 };
    NativeArchiveError error;
    TestArchive archive;
    itLinkArrowAttributes* arrow;
    void* value;
    word(data, 0, 0x42C80000);
    word(data, 0x24, 0x40);
    word(data, 0x28, 0x40);
    word(data, 0x40 + 0x20, 0x3F800000);
    archive = openArchive(data, sizeof(data), relocations, 2);
    CHECK(NativeItemFighterSpecialRead(archive.items, It_Kind_Kirby_LinkArrow,
                                       0, &value,
                                       &error) == NATIVE_ARCHIVE_OK);
    arrow = value;
    CHECK(arrow->x0 == 100.0F);
    CHECK(arrow->x24 != NULL && arrow->x24 == arrow->x28);
    CHECK(arrow->x24->scale.x == 1.0F);
    CHECK(arrow->x2C == 0.0F);
    closeArchive(&archive);
}

static void testGrappleAnimationChild(void)
{
    uint8_t data[0xD8] = { 0 };
    const uint32_t relocations[] = { 0x74, 0xB0 };
    NativeArchiveError error;
    TestArchive archive;
    itSamusGrappleAttributes* grapple;
    HSD_AnimJoint* root;
    void* value;
    word(data, 0x74, 0xB0);
    word(data, 0xB0, 0xC4);
    word(data, 0xC4 + 0x10, 0x1234);
    archive = openArchive(data, sizeof(data), relocations, 2);
    CHECK(NativeItemFighterSpecialRead(archive.items, It_Kind_Samus_GBeam, 0,
                                       &value, &error) == NATIVE_ARCHIVE_OK);
    grapple = value;
    CHECK(NativeArchiveAnimation(archive.graph, 0xB0, &root, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(grapple->x74 == &root->child);
    CHECK(*grapple->x74 != NULL && (*grapple->x74)->flags == 0x1234);
    CHECK(grapple->x78 == NULL);
    closeArchive(&archive);
}

static void testGamewatchVisibility(void)
{
    uint8_t data[0x148] = { 0 };
    const uint32_t relocations[] = { 0, 0x24, 0x2C };
    NativeArchiveError error;
    TestArchive archive;
    it_266F_ItemVars* visibility;
    void* value;
    size_t i;
    word(data, 0, 0x20);
    word(data, 0x20, 0x01020000);
    word(data, 0x24, 0x40);
    word(data, 0x28, 0x00030000);
    word(data, 0x2C, 0x144);
    for (i = 0; i < 258; ++i) {
        data[0x40 + i] = (uint8_t) i;
    }
    data[0x144] = 7;
    data[0x145] = 3;
    data[0x146] = 9;
    archive = openArchive(data, sizeof(data), relocations, 3);
    CHECK(NativeItemFighterSpecialRead(archive.items,
                                       It_Kind_GameWatch_Parachute, 0, &value,
                                       &error) == NATIVE_ARCHIVE_OK);
    visibility = *(void**) value;
    CHECK(visibility->x0 == 258 && visibility->x8 == 3);
    CHECK(visibility->x4[255] == 255 && visibility->x4[257] == 1);
    CHECK(visibility->xC[0] == 7 && visibility->xC[2] == 9);
    closeArchive(&archive);

    word(data, 0x20, 0xFFFF0000);
    archive = openArchive(data, sizeof(data), relocations, 3);
    CHECK(NativeItemFighterSpecialRead(archive.items,
                                       It_Kind_GameWatch_Parachute, 0, &value,
                                       &error) == NATIVE_ARCHIVE_BOUNDS);
    CHECK(value == NULL);
    closeArchive(&archive);
}

static void testChefEntries(void)
{
    uint8_t data[0x90] = { 0 };
    const uint32_t relocations[] = { 0 };
    NativeArchiveError error;
    TestArchive archive;
    itGamewatchchefAttributes* chef;
    void* value;
    word(data, 0, 0x80);
    word(data, 4, 0x3F800000);
    word(data, 0x70, 0x41100000);
    archive = openArchive(data, sizeof(data), relocations, 1);
    CHECK(NativeItemFighterSpecialRead(archive.items, It_Kind_GameWatch_Chef,
                                       0, &value,
                                       &error) == NATIVE_ARCHIVE_OK);
    chef = value;
    CHECK(chef->x0 != NULL && chef->x4 == 1.0F);
    CHECK(chef->entries[4].x10 == 9.0F);
    closeArchive(&archive);
}

static void testTurnipCount(void)
{
    uint8_t data[0x18] = { 0 };
    NativeArchiveError error;
    TestArchive archive;
    itPeachTurnipAttributes* turnip;
    void* value;
    word(data, 4, 2);
    word(data, 8, 35);
    word(data, 0x14, 12);
    archive = openArchive(data, sizeof(data), NULL, 0);
    CHECK(NativeItemFighterSpecialRead(archive.items, It_Kind_Peach_Turnip, 0,
                                       &value, &error) == NATIVE_ARCHIVE_OK);
    turnip = value;
    CHECK(turnip->x4_length == 2 && turnip->x8[0].x0_odds == 35);
    CHECK(turnip->x8[1].x4_damage == 12);
    closeArchive(&archive);
    word(data, 4, 3);
    archive = openArchive(data, sizeof(data), NULL, 0);
    CHECK(NativeItemFighterSpecialRead(archive.items, It_Kind_Peach_Turnip, 0,
                                       &value,
                                       &error) == NATIVE_ARCHIVE_BOUNDS);
    CHECK(value == NULL);
    closeArchive(&archive);
}

static void testScalarRejectsReference(void)
{
    uint8_t data[0x14] = { 0 };
    const uint32_t relocation = 4;
    NativeArchiveError error;
    TestArchive archive = openArchive(data, sizeof(data), &relocation, 1);
    void* value;
    CHECK(NativeItemFighterSpecialRead(archive.items, It_Kind_Mario_Fire, 0,
                                       &value, &error) ==
          NATIVE_ARCHIVE_TYPE_CONFLICT);
    CHECK(value == NULL && error.offset == 36);
    closeArchive(&archive);
}

int main(void)
{
    testShortScalar();
    testArrowReferences();
    testGrappleAnimationChild();
    testGamewatchVisibility();
    testChefEntries();
    testTurnipCount();
    testScalarRejectsReference();
    puts("fighter item special attributes: 7 tests passed");
    return 0;
}
