#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../assets/fighter_parts.h"
#include <melee/ft/types.h>
/* macOS also defines __assert as a macro. */
#undef __assert
#include <sysdolphin/baselib/jobj.h>

static void word(unsigned char* data, size_t offset, uint32_t value)
{
    data[offset] = value >> 24;
    data[offset + 1] = value >> 16;
    data[offset + 2] = value >> 8;
    data[offset + 3] = value;
}

static void test_layouts(void)
{
    enum {
        DATA_SIZE = 640,
        RELOCS = 16,
        FILE_SIZE = 32 + DATA_SIZE + RELOCS * 4
    };
    unsigned char file[FILE_SIZE] = { 0 };
    unsigned char* data = file + 32;
    const uint32_t relocs[] = { 4,   12, 24,      28,       40,       44,
                                52,  60, 128 + 4, 128 + 12, 128 + 16, 180,
                                256, 84, 88,      456 };
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeFighterParts* parts;
    NativeArchiveError error;
    void* result;
    void* again;
    word(file, 0, FILE_SIZE);
    word(file, 4, DATA_SIZE);
    word(file, 8, RELOCS);
    word(data, 0, 1);
    word(data, 4, 24);
    word(data, 8, 2);
    word(data, 12, 40);
    data[16] = 10;
    data[20] = 14;
    word(data, 24, 48);
    word(data, 28, 48);
    word(data, 40, 68);
    word(data, 44, 68);
    word(data, 48, 1);
    word(data, 52, 56);
    word(data, 56, 3);
    word(data, 60, 64);
    data[64] = 2;
    data[65] = 5;
    data[66] = 7;
    word(data, 68, 0x0002FFFE);
    word(data, 80, 1);
    word(data, 84, 24);
    word(data, 88, 448);
    word(data, 452, 8);
    word(data, 456, 512);
    word(data, 516, 8);
    word(data, 128, 1);
    word(data, 132, 176);
    word(data, 136, 1);
    word(data, 140, 200);
    word(data, 144, 256);
    word(data, 176, 17);
    word(data, 180, 320);
    word(data, 184, 2);
    word(data, 188, 0x3F800000);
    word(data, 192, 0x40000000);
    word(data, 196, 0x40400000);
    word(data, 200, 7);
    word(data, 204, 0xC1200000);
    word(data, 216, 0x40800000);
    word(data, 256, 316);
    word(data, 316, 0xFFFFFFFF);
    word(data, 320, 0x3F000000);
    word(data, 380, 0x3F400000);
    for (size_t i = 0; i < RELOCS; ++i) {
        word(file, 32 + DATA_SIZE + 4 * i, relocs[i]);
    }
    assert(NativeArchiveOpen(file, sizeof(file), &archive, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(NativeFighterPartsOpen(archive, graph, &parts, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(NativeFighterPartsRead(parts, NATIVE_FIGHTER_PARTS_MODELS, 0,
                                  &result, &error) == NATIVE_ARCHIVE_OK);
    struct ftData_x8* models = result;
    assert(models->x0.model_num == 1 && models->x8.x8 == 2);
    assert(models->x10 == 10 && models->x14 == 14);
    assert(models->x0.vis_table[0][0] == models->x0.vis_table[0][1]);
    FtPartsVisLookup* lookup = models->x0.vis_table[0][0];
    assert(lookup[0].x4[0].x0 == 3 && lookup[0].x4[0].x4[2] == 7);
    assert(models->x8.xC[0] == models->x8.xC[1]);
    assert(models->x8.xC[0][0] == 2 && models->x8.xC[0][1] == 65534);
    assert(NativeFighterPartsRead(parts, NATIVE_FIGHTER_PARTS_MODELS, 0,
                                  &again, &error) == NATIVE_ARCHIVE_OK);
    assert(again == result);
    assert(NativeFighterPartsRead(parts, NATIVE_FIGHTER_PARTS_DESCRIPTION, 80,
                                  &result, &error) == NATIVE_ARCHIVE_OK);
    assert(((FtPartsDesc*) result)->vis_table == models->x0.vis_table);
    FtPartsVisLookup* shared;
    assert(NativeFighterPartsVisibility(parts, 48, 1, &shared, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(shared == lookup);
    assert(NativeFighterPartsRead(parts, NATIVE_FIGHTER_PARTS_SHIELD, 88,
                                  &result, &error) == NATIVE_ARCHIVE_OK);
    HSD_Joint* shield = (void*) ((struct ftData_x20*) result)->x0;
    assert(shield != NULL && shield->child != NULL &&
           shield->child->flags == 8);
    assert(NativeFighterPartsRead(parts, NATIVE_FIGHTER_PARTS_DYNAMICS, 128,
                                  &result, &error) == NATIVE_ARCHIVE_OK);
    ftDynamics* dyn = result;
    assert(dyn->dynamicsNum == 1 && dyn->x4 == 1);
    assert(dyn->ftDynamicBones->array[0].bone_id == 17);
    DynamicsDesc* desc = &dyn->ftDynamicBones->array[0].dyn_desc;
    assert(desc->count == 2 && desc->pos.z == 3);
    assert(desc->data->desc.lb_unk1.array[0].unk_0 == 0.5F);
    assert(desc->data->desc.lb_unk1.array[1].unk_0 == 0.75F);
    assert(dyn->x8[0].x4.x == -10 && dyn->x8[0].x10 == 4);
    assert(dyn->x10[0][0] == -1);
    NativeFighterPartsClose(parts);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);

    word(data, 56, DATA_SIZE);
    assert(NativeArchiveOpen(file, sizeof(file), &archive, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(NativeFighterPartsOpen(archive, graph, &parts, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(NativeFighterPartsRead(parts, NATIVE_FIGHTER_PARTS_MODELS, 0,
                                  &result, &error) == NATIVE_ARCHIVE_BOUNDS);
    assert(result == NULL);
    NativeFighterPartsClose(parts);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

/* Optional local DAT files test the complete model, animation, shield and
 * dynamics graph. Original game data stays outside the test source. */
static void test_real_fighter(const char* path)
{
    FILE* file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size > 0 && fseek(file, 0, SEEK_SET) == 0);
    unsigned char* bytes = malloc(size);
    assert(bytes != NULL && fread(bytes, 1, size, file) == (size_t) size);
    fclose(file);
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeFighterParts* parts;
    NativeArchiveError error;
    assert(NativeArchiveOpen(bytes, size, &archive, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(NativeFighterPartsOpen(archive, graph, &parts, &error) ==
           NATIVE_ARCHIVE_OK);
    bool found = false;
    for (size_t i = 0; i < NativeArchivePublicCount(archive); ++i) {
        NativeArchiveSymbol symbol;
        assert(NativeArchivePublic(archive, i, &symbol, &error) ==
               NATIVE_ARCHIVE_OK);
        if (strncmp(symbol.name, "ftData", 6) != 0) {
            continue;
        }
        found = true;
        const uint32_t fields[] = { 8, 28, 32, 44 };
        for (size_t j = 0; j < 4; ++j) {
            uint32_t offset;
            bool present;
            void* result;
            assert(NativeArchiveReference(archive, symbol.offset + fields[j],
                                          &offset, &present,
                                          &error) == NATIVE_ARCHIVE_OK);
            if (!present) {
                continue;
            }
            if (NativeFighterPartsRead(parts, (NativeFighterPartsType) j,
                                       offset, &result,
                                       &error) != NATIVE_ARCHIVE_OK)
            {
                fprintf(stderr, "%s field 0x%x at 0x%zx: %s\n", path,
                        fields[j], error.offset, error.message);
                exit(1);
            }
        }
    }
    assert(found);
    NativeFighterPartsClose(parts);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
    free(bytes);
}

int main(int argc, char** argv)
{
    test_layouts();
    for (int i = 1; i < argc; ++i) {
        test_real_fighter(argv[i]);
    }
    puts("fighter parts archive tests passed");
    return 0;
}
