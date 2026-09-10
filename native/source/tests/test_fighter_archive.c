#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../assets/archive_internal.h"
#include "../assets/fighters.h"
#include "../assets/items.h"
#include <melee/ft/types.h>

static void word(u8* data, size_t offset, u32 value)
{
    data[offset] = value >> 24;
    data[offset + 1] = value >> 16;
    data[offset + 2] = value >> 8;
    data[offset + 3] = value;
}

static void test_animation_flag_layout(void)
{
#ifdef MELEE_NATIVE
    Fighter fighter = { 0 };
    const u32 flags = 0xA52A40A2;

    fighter.x594_s32 = (s32) flags;
    assert(fighter.x597_bits == 34);
    assert(fighter.x594_bits == (flags >> 9 & 0x1FFF));
    assert(fighter.x594_b0 == 1);
    assert(fighter.x594_b1_loop == 0);
    assert(fighter.x594_b2 == 1);
    assert(fighter.x594_b3 == 0);
    assert(fighter.x594_b4 == 0);
    assert(fighter.x594_b5 == 1);
    assert(fighter.x594_b6 == 0);
    assert(fighter.x594_b7 == 1);
    assert(fighter.x596_x7 == (flags >> 6 & 7));
#endif
}

static void test_layout(void)
{
    enum {
        DATA_SIZE = 0x400,
        RELOC_COUNT = 15,
        FILE_SIZE = 32 + DATA_SIZE + RELOC_COUNT * 4
    };
    const u32 relocations[] = { 0,     12,    16,    48,    76,
                                88,    0x204, 0x210, 0x21C, 0x228,
                                0x264, 0x320, 0x33C, 0x340, 0x364 };
    u8 file[FILE_SIZE] = { 0 };
    u8* data = file + 32;
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeArchiveError error;
    NativeItemArchive* items;
    NativeFighterArchive* fighter;
    void* root;
    void* again;
    word(file, 0, sizeof(file));
    word(file, 4, DATA_SIZE);
    word(file, 8, RELOC_COUNT);
    word(data, 0, 0x80);
    word(data, 12, 0x204);
    word(data, 16, 0x234);
    word(data, 48, 0x260);
    word(data, 76, 0x320);
    word(data, 88, 0x300);
    word(data, 0x80, 0x3F800000);
    data[0x200] = 0x5A;
    word(data, 0x204, 0x238);
    word(data, 0x208, 0x12340);
    word(data, 0x20C, 0x400);
    word(data, 0x210, 0x250);
    word(data, 0x214, 0x40000001);
    word(data, 0x21C, 0x238);
    word(data, 0x220, 0x20000);
    word(data, 0x224, 0x800);
    word(data, 0x228, 0x250);
    data[0x234] = 6;
    memcpy(data + 0x238, "motion", 7);
    word(data, 0x250, 0x04000007);
    word(data, 0x260, 2);
    word(data, 0x264, 0x268);
    word(data, 0x268, 9);
    word(data, 0x268 + 36, 0x40000000);
    word(data, 0x290, 12);
    data[0x300] = 5;
    data[0x301] = 8;
    word(data, 0x304, 0x3F800000);
    data[0x308] = 9;
    data[0x309] = 12;
    word(data, 0x30C, 0x40000000);
    word(data, 0x318, 0x40800000);
    word(data, 0x320, 0x360);
    word(data, 0x324, 0x123456);
    word(data, 0x33C, 0x360);
    word(data, 0x340, 0x360);
    word(data, 0x354, 0x12);
    word(data, 0x360, 2);
    word(data, 0x364, 0x370);
    word(data, 0x370, 0x12345678);
    word(data, 0x374, 0x87654321);
    for (size_t i = 0; i < RELOC_COUNT; ++i) {
        word(file, 32 + DATA_SIZE + i * 4, relocations[i]);
    }
    assert(NativeArchiveOpen(file, sizeof(file), &archive, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    items = NativeItemArchiveOpen(archive, graph);
    fighter = NativeFighterArchiveOpen(archive, graph, items);
    assert(fighter != NULL);
    assert(NativeFighterArchiveRead(fighter, "ftDataMario", 0, &root,
                                    &error) == NATIVE_ARCHIVE_OK);
    ftData* result = root;
    assert(result->x0->walk_accel_mul == 1.0F);
    assert(result->x0->weight_independent_throws_mask == 0x5A);
    assert(result->xC[0].x4 == 0x12340 && result->xC[1].x8 == 0x800);
    assert(result->xC[0].x0 == result->xC[1].x0);
    assert(strcmp(result->xC[0].x0, "motion") == 0);
    assert(result->xC[0].xC == result->xC[1].xC);
    assert((const u8*) result->xC[0].xC == archive->data + 0x250);
    assert(((const u8*) result->xC[0].xC)[3] == 7);
    assert(result->x10[0][0] == 6);
    assert(result->x30->count == 2 && result->x30->inits[1].bone_idx == 12);
    assert(result->x30->inits[0].scale == 2.0F);
    assert(result->x4C_sfx->smash == result->x4C_sfx->x1C);
    assert(result->x4C_sfx->smash == result->x4C_sfx->x20);
    assert((u32) result->x4C_sfx->smash->sfx_ids[1] == 0x87654321);
    assert(result->x4C_sfx->x4 == 0x123456 && result->x4C_sfx->x34 == 0x12);
    assert(result->x58->x0 == 5 && result->x58->x1 == 8);
    assert(result->x58->x4 == 1.0F && result->x58->x18 == 4.0F);
    /* Both source types are views of the same motion table entry. */
    assert(sizeof(struct Fighter_WaitAnimData) ==
           sizeof(struct ftData_80085FD4_ret));
    assert(offsetof(struct Fighter_WaitAnimData, x14) ==
           offsetof(struct ftData_80085FD4_ret, x14));
    struct ftData_80085FD4_ret* alternate = (void*) &result->xC[0];
    assert(alternate->x4 == 0x12340 && alternate->x8 == 0x400);
    assert(alternate->x10_b1 && !alternate->x10_b0);
    assert(NativeFighterArchiveRead(fighter, "ftDataMario", 0, &again,
                                    &error) == NATIVE_ARCHIVE_OK);
    assert(root == again);
    NativeFighterArchiveClose(fighter);
    NativeItemArchiveClose(items);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);

    word(data, 0x260, 0x7FFFFFFF);
    assert(NativeArchiveOpen(file, sizeof(file), &archive, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    items = NativeItemArchiveOpen(archive, graph);
    fighter = NativeFighterArchiveOpen(archive, graph, items);
    assert(NativeFighterArchiveRead(fighter, "ftDataMario", 0, &root,
                                    &error) != NATIVE_ARCHIVE_OK);
    assert(root == NULL);
    assert(NativeFighterArchiveRead(fighter, "ftDataMario", 0, &root,
                                    &error) != NATIVE_ARCHIVE_OK);
    assert(root == NULL);
    NativeFighterArchiveClose(fighter);
    NativeItemArchiveClose(items);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_real_archive(const char* path)
{
    FILE* file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size > 0 && fseek(file, 0, SEEK_SET) == 0);
    void* bytes = malloc(size);
    assert(bytes != NULL && fread(bytes, 1, size, file) == (size_t) size);
    fclose(file);
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeArchiveError error;
    assert(NativeArchiveOpen(bytes, size, &archive, &error) ==
           NATIVE_ARCHIVE_OK);
    free(bytes);
    NativeArchiveNullExternals(archive);
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    NativeItemArchive* items = NativeItemArchiveOpen(archive, graph);
    NativeFighterArchive* fighter =
        NativeFighterArchiveOpen(archive, graph, items);
    assert(fighter != NULL);
    for (size_t i = 0; i < NativeArchivePublicCount(archive); ++i) {
        NativeArchiveSymbol symbol;
        void* output;
        assert(NativeArchivePublic(archive, i, &symbol, &error) ==
               NATIVE_ARCHIVE_OK);
        if (NativeFighterArchiveRead(fighter, symbol.name, symbol.offset,
                                     &output, &error) != NATIVE_ARCHIVE_OK)
        {
            fprintf(stderr, "%s: %s at %zu: %s\n", path, symbol.name,
                    error.offset, error.message);
            exit(1);
        }
        printf("%s: %s OK\n", path, symbol.name);
    }
    NativeFighterArchiveClose(fighter);
    NativeItemArchiveClose(items);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

int main(int argc, char** argv)
{
    test_animation_flag_layout();
    test_layout();
    for (int i = 1; i < argc; ++i) {
        test_real_archive(argv[i]);
    }
    return 0;
}
