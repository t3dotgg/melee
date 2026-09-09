#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../assets/effects.h"
#include <melee/ef/types.h>
#include <sysdolphin/baselib/aobj.h>
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

static void test_inline_effects(void)
{
    enum {
        DATA_SIZE = 384,
        RELOCS = 8,
        FILE_SIZE = 32 + DATA_SIZE + 4 * RELOCS
    };
    unsigned char file[FILE_SIZE] = { 0 };
    unsigned char* data = file + 32;
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeArchiveError error;
    NativeEffectArchive* effects;
    void* result;
    void* again;
    word(file, 0, FILE_SIZE);
    word(file, 4, DATA_SIZE);
    word(file, 8, RELOCS);
    word(data, 0, 320);
    word(data, 4, 352);
    word(data, 8, 0x3FC00000); /* lifetime is not a pointer */
    word(data, 12, 80);
    word(data, 16, 160);
    word(data, 20, 192);
    word(data, 24, 224);
    word(data, 28, 0x41D80000);
    word(data, 32, 80); /* shared model, different effect lifetime */
    word(data, 80 + 4, 0x20000000);
    word(data, 80 + 32, 0x3F800000);
    word(data, 80 + 36, 0x3F800000);
    word(data, 80 + 40, 0x3F800000);
    word(data, 160 + 16, 0x12345678);
    word(data, 160 + 8, 256);
    word(data, 256 + 4, 0x42280000);
    memcpy(data + 320, "particle bank", 12);
    memcpy(data + 352, "texture bank", 12);
    const uint32_t fields[RELOCS] = { 0, 4, 12, 16, 20, 24, 32, 168 };
    for (size_t i = 0; i < RELOCS; ++i) {
        word(file, 32 + DATA_SIZE + i * 4, fields[i]);
    }
    CHECK(NativeArchiveOpen(file, sizeof(file), &archive, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(NativeArchiveGraphOpen(archive, &graph, &error) ==
          NATIVE_ARCHIVE_OK);
    effects = NativeEffectArchiveOpen(archive, graph);
    CHECK(effects != NULL);
    CHECK(NativeEffectArchiveRead(effects, "MnSelectChrDataTable", 0, &result,
                                  &error) == NATIVE_ARCHIVE_NOT_FOUND);
    CHECK(result == NULL);
    CHECK(NativeEffectArchiveRead(effects, "effCommonDataTable", 0, &result,
                                  &error) == NATIVE_ARCHIVE_OK);
    EF_DAT_Entry* prefix = result;
    CHECK(memcmp(prefix->ef_DAT_file, "particle bank", 12) == 0);
    CHECK(memcmp(prefix->effDataTable_name, "texture bank", 12) == 0);
    /* Match the actual efAsync_LoadSync -> efLib_Create pointer operation. */
    EF_EffectDesc* entries = (EF_EffectDesc*) &prefix->data;
    CHECK(entries[0].lifetime == 1.5F);
    CHECK(entries[1].lifetime == 27.0F);
    CHECK(entries[0].model_desc.joint == entries[1].model_desc.joint);
    CHECK(entries[0].model_desc.joint->flags == 0x20000000);
    CHECK(entries[0].model_desc.joint->scale.x == 1.0F);
    CHECK(entries[0].model_desc.animjoint->flags == 0x12345678);
    CHECK(entries[0].model_desc.animjoint->aobjdesc->end_frame == 42.0F);
    CHECK(entries[0].model_desc.matanim_joint != NULL);
    CHECK(entries[0].model_desc.shapeanim_joint != NULL);
    CHECK(entries[1].model_desc.animjoint == NULL);
    CHECK(entries[2].lifetime == 0 && entries[2].model_desc.joint == NULL);
    CHECK(NativeEffectArchiveRead(effects, "effCommonDataTable", 0, &again,
                                  &error) == NATIVE_ARCHIVE_OK);
    CHECK(result == again);
    NativeEffectArchiveClose(effects);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);

    /* Reject nonzero bytes in the incomplete record before the next object. */
    data[79] = 1;
    CHECK(NativeArchiveOpen(file, sizeof(file), &archive, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(NativeArchiveGraphOpen(archive, &graph, &error) ==
          NATIVE_ARCHIVE_OK);
    effects = NativeEffectArchiveOpen(archive, graph);
    CHECK(NativeEffectArchiveRead(effects, "effCommonDataTable", 0, &result,
                                  &error) == NATIVE_ARCHIVE_INVALID);
    CHECK(result == NULL && error.offset == 32 + 79);
    CHECK(NativeEffectArchiveRead(effects, "effCommonDataTable", DATA_SIZE - 4,
                                  &result, &error) == NATIVE_ARCHIVE_BOUNDS);
    CHECK(result == NULL);
    NativeEffectArchiveClose(effects);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);

    data[79] = 0;
    /* A model field must have a relocation, even when its value is in range.
     */
    word(data, 52, 80);
    CHECK(NativeArchiveOpen(file, sizeof(file), &archive, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(NativeArchiveGraphOpen(archive, &graph, &error) ==
          NATIVE_ARCHIVE_OK);
    effects = NativeEffectArchiveOpen(archive, graph);
    CHECK(NativeEffectArchiveRead(effects, "effCommonDataTable", 0, &result,
                                  &error) == NATIVE_ARCHIVE_INVALID);
    CHECK(result == NULL && error.offset == 32 + 52);
    NativeEffectArchiveClose(effects);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

/* Local DAT paths exercise full effect graphs without committing game data. */
static void test_real_effects(const char* path)
{
    FILE* file = fopen(path, "rb");
    CHECK(file != NULL);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    CHECK(size > 0 && fseek(file, 0, SEEK_SET) == 0);
    void* bytes = malloc(size);
    CHECK(bytes != NULL && fread(bytes, 1, size, file) == (size_t) size);
    fclose(file);
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeArchiveError error;
    CHECK(NativeArchiveOpen(bytes, size, &archive, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(NativeArchiveGraphOpen(archive, &graph, &error) ==
          NATIVE_ARCHIVE_OK);
    NativeEffectArchive* effects = NativeEffectArchiveOpen(archive, graph);
    CHECK(effects != NULL);
    size_t converted = 0;
    for (size_t i = 0; i < NativeArchivePublicCount(archive); ++i) {
        NativeArchiveSymbol symbol;
        void* root;
        CHECK(NativeArchivePublic(archive, i, &symbol, &error) ==
              NATIVE_ARCHIVE_OK);
        NativeArchiveStatus status = NativeEffectArchiveRead(
            effects, symbol.name, symbol.offset, &root, &error);
        if (status == NATIVE_ARCHIVE_NOT_FOUND) {
            continue;
        }
        if (status != NATIVE_ARCHIVE_OK) {
            fprintf(stderr, "%s %s at %zu: %s\n", path, symbol.name,
                    error.offset, error.message);
            exit(1);
        }
        EF_DAT_Entry* table = root;
        EF_EffectDesc* entries = (EF_EffectDesc*) &table->data;
        CHECK(isfinite(entries[0].lifetime));
        ++converted;
    }
    CHECK(converted > 0);
    printf("Validated native effects %s\n", path);
    NativeEffectArchiveClose(effects);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
    free(bytes);
}

int main(int argc, char** argv)
{
    test_inline_effects();
    for (int i = 1; i < argc; ++i) {
        test_real_effects(argv[i]);
    }
    puts("Native effect archive tests passed");
    return 0;
}
