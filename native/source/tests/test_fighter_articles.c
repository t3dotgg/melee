#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../assets/fighter_articles.h"
#include <melee/ft/kinds/ftSamus/types.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/jobj.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

static void word(unsigned char* data, size_t offset, uint32_t value)
{
    data[offset] = value >> 24;
    data[offset + 1] = value >> 16;
    data[offset + 2] = value >> 8;
    data[offset + 3] = value;
}

typedef struct Fixture {
    unsigned char file[1024];
    uint32_t relocations[32];
    size_t count;
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeItemArchive* items;
    NativeFighterParts* parts;
    NativeFighterArticles* articles;
} Fixture;

static void ref(Fixture* fixture, uint32_t field, uint32_t target)
{
    CHECK(fixture->count < 32);
    word(fixture->file + 32, field, target);
    fixture->relocations[fixture->count++] = field;
}

static void open_fixture(Fixture* fixture)
{
    NativeArchiveError error;
    size_t size = 32 + 512 + fixture->count * 4;
    word(fixture->file, 0, size);
    word(fixture->file, 4, 512);
    word(fixture->file, 8, fixture->count);
    for (size_t i = 0; i < fixture->count; i++) {
        word(fixture->file, 32 + 512 + i * 4, fixture->relocations[i]);
    }
    CHECK(NativeArchiveOpen(fixture->file, size, &fixture->archive, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(NativeArchiveGraphOpen(fixture->archive, &fixture->graph, &error) ==
          NATIVE_ARCHIVE_OK);
    fixture->items = NativeItemArchiveOpen(fixture->archive, fixture->graph);
    CHECK(fixture->items != NULL);
    CHECK(NativeFighterPartsOpen(fixture->archive, fixture->graph,
                                 &fixture->parts,
                                 &error) == NATIVE_ARCHIVE_OK);
    fixture->articles = NativeFighterArticlesOpen(
        fixture->archive, fixture->graph, fixture->items, fixture->parts);
    CHECK(fixture->articles != NULL);
}

static void close_fixture(Fixture* fixture)
{
    NativeFighterArticlesClose(fixture->articles);
    NativeFighterPartsClose(fixture->parts);
    NativeItemArchiveClose(fixture->items);
    NativeArchiveGraphClose(fixture->graph);
    NativeArchiveClose(fixture->archive);
}

static void test_slot_layouts(void)
{
    Fixture fixture = { 0 };
    NativeArchiveError error;
    void** result;
    void** again;
    /* Fox's fifth slot is six scalar words, including a relocated zero target.
     */
    ref(&fixture, 400 + 16, 0);
    word(fixture.file + 32, 0, 3);
    word(fixture.file + 32, 4, 70);
    word(fixture.file + 32, 16, UINT32_MAX);
    ref(&fixture, 400, 32);
    open_fixture(&fixture);
    CHECK(NativeFighterArticlesRead(fixture.articles, "ftDataFox", 400,
                                    &result, &error) == NATIVE_ARCHIVE_OK);
    CHECK(result[0] != NULL && result[1] == NULL);
    CHECK(((Article*) result[0])->x10_modelDesc == NULL);
    CHECK(((uint32_t*) result[4])[0] == 3);
    CHECK(((uint32_t*) result[4])[1] == 70);
    CHECK(((uint32_t*) result[4])[4] == UINT32_MAX);
    CHECK(NativeFighterArticlesRead(fixture.articles, "ftDataFox", 400, &again,
                                    &error) == NATIVE_ARCHIVE_OK);
    CHECK(again == result);
    CHECK(NativeFighterArticlesRead(fixture.articles, "ftDataMario", 400,
                                    &again,
                                    &error) == NATIVE_ARCHIVE_TYPE_CONFLICT);
    CHECK(again == NULL);
    close_fixture(&fixture);

    memset(&fixture, 0, sizeof(fixture));
    ref(&fixture, 400 + 16, 32);
    word(fixture.file + 32, 32 + 0x20, 0x40000000);
    open_fixture(&fixture);
    CHECK(NativeFighterArticlesRead(fixture.articles, "ftDataKirby", 400,
                                    &result, &error) == NATIVE_ARCHIVE_OK);
    CHECK(((HSD_Joint*) result[4])->scale.x == 2);
    close_fixture(&fixture);

    memset(&fixture, 0, sizeof(fixture));
    ref(&fixture, 400 + 16, 32);
    ref(&fixture, 32, 64);
    ref(&fixture, 36, 128);
    ref(&fixture, 128 + 12, 160);
    open_fixture(&fixture);
    CHECK(NativeFighterArticlesRead(fixture.articles, "ftDataSamus", 400,
                                    &result, &error) == NATIVE_ARCHIVE_OK);
    struct UNK_SAMUS_S1* beam = result[4];
    CHECK(beam->x0_joint != NULL);
    CHECK(beam->x4_anim_joints[0] == NULL);
    CHECK(beam->x4_anim_joints[3] != NULL);
    close_fixture(&fixture);
}

static void test_rejected_slots(void)
{
    Fixture fixture = { 0 };
    NativeArchiveError error;
    void** result = (void**) 1;
    ref(&fixture, 400 + 4, 32);
    open_fixture(&fixture);
    CHECK(NativeFighterArticlesRead(fixture.articles, "ftDataMario", 400,
                                    &result,
                                    &error) == NATIVE_ARCHIVE_UNSUPPORTED);
    CHECK(result == NULL);
    CHECK(NativeFighterArticlesRead(fixture.articles, "ftDataMario", 400,
                                    &result,
                                    &error) == NATIVE_ARCHIVE_INVALID);
    close_fixture(&fixture);

    memset(&fixture, 0, sizeof(fixture));
    open_fixture(&fixture);
    CHECK(NativeFighterArticlesRead(fixture.articles, "ftDataUnknown", 400,
                                    &result,
                                    &error) == NATIVE_ARCHIVE_UNSUPPORTED);
    CHECK(result == NULL);
    CHECK(NativeFighterArticlesRead(fixture.articles, "ftDataSamus", 500,
                                    &result, &error) == NATIVE_ARCHIVE_BOUNDS);
    CHECK(result == NULL);
    close_fixture(&fixture);

    memset(&fixture, 0, sizeof(fixture));
    ref(&fixture, 400 + 16, 32);
    ref(&fixture, 32, 64);
    open_fixture(&fixture);
    CHECK(NativeFighterArticlesRead(fixture.articles, "ftDataFox", 400,
                                    &result,
                                    &error) == NATIVE_ARCHIVE_UNSUPPORTED);
    CHECK(result == NULL);
    close_fixture(&fixture);
}

/* Local DAT arguments check complete article and descriptor graphs. */
static bool test_real_fighter(const char* path)
{
    FILE* file = fopen(path, "rb");
    CHECK(file != NULL);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    CHECK(size > 0 && fseek(file, 0, SEEK_SET) == 0);
    void* bytes = malloc(size);
    CHECK(bytes != NULL && fread(bytes, 1, size, file) == (size_t) size);
    fclose(file);
    Fixture fixture = { 0 };
    NativeArchiveError error;
    bool passed = true;
    CHECK(NativeArchiveOpen(bytes, size, &fixture.archive, &error) ==
          NATIVE_ARCHIVE_OK);
    free(bytes);
    CHECK(NativeArchiveGraphOpen(fixture.archive, &fixture.graph, &error) ==
          NATIVE_ARCHIVE_OK);
    fixture.items = NativeItemArchiveOpen(fixture.archive, fixture.graph);
    CHECK(fixture.items != NULL);
    CHECK(NativeFighterPartsOpen(fixture.archive, fixture.graph,
                                 &fixture.parts, &error) == NATIVE_ARCHIVE_OK);
    fixture.articles = NativeFighterArticlesOpen(
        fixture.archive, fixture.graph, fixture.items, fixture.parts);
    CHECK(fixture.articles != NULL);
    for (size_t i = 0; i < NativeArchivePublicCount(fixture.archive); i++) {
        NativeArchiveSymbol symbol;
        CHECK(NativeArchivePublic(fixture.archive, i, &symbol, &error) ==
              NATIVE_ARCHIVE_OK);
        if (strncmp(symbol.name, "ftData", 6) != 0) {
            continue;
        }
        uint32_t target;
        bool present;
        void** result;
        CHECK(NativeArchiveReference(fixture.archive, symbol.offset + 0x48,
                                     &target, &present,
                                     &error) == NATIVE_ARCHIVE_OK);
        if (present &&
            NativeFighterArticlesRead(fixture.articles, symbol.name, target,
                                      &result, &error) != NATIVE_ARCHIVE_OK)
        {
            fprintf(stderr, "%s %s at %#zx: %s\n", path, symbol.name,
                    error.offset, error.message);
            passed = false;
        }
    }
    close_fixture(&fixture);
    return passed;
}

int main(int argc, char** argv)
{
    test_slot_layouts();
    test_rejected_slots();
    bool passed = true;
    for (int i = 1; i < argc; i++) {
        if (!test_real_fighter(argv[i])) {
            passed = false;
        }
    }
    if (!passed) {
        return 1;
    }
    puts("fighter article tests passed");
    return 0;
}
