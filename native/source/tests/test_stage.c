#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../assets/stage.h"
#include <melee/gr/types.h>
#include <melee/mp/types.h>

static void word(unsigned char* data, size_t offset, uint32_t value)
{
    data[offset] = value >> 24;
    data[offset + 1] = value >> 16;
    data[offset + 2] = value >> 8;
    data[offset + 3] = value;
}

static void test_null_external_initialization(void)
{
    unsigned char file[55] = { 0 };
    NativeArchive* archive;
    NativeArchiveError error;
    uint32_t target;
    bool present;
    unsigned char original[4];
    word(file, 0, sizeof(file));
    word(file, 4, 8);
    word(file, 16, 1);
    word(file, 32, 4);
    word(file, 36, UINT32_MAX);
    memcpy(file + 48, "shared", 7);
    assert(NativeArchiveOpen(file, sizeof(file), &archive, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(NativeArchiveReference(archive, 0, &target, &present, &error) ==
           NATIVE_ARCHIVE_UNSUPPORTED);
    NativeArchiveNullExternals(archive);
    assert(NativeArchiveReference(archive, 0, &target, &present, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(!present && target == 0);
    assert(NativeArchiveReference(archive, 4, &target, &present, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(!present);
    assert(NativeArchiveRead(archive, 0, original, sizeof(original), &error) ==
           NATIVE_ARCHIVE_OK);
    assert(original[3] == 4);
    NativeArchiveClose(archive);
}

static void test_stage_layouts(void)
{
    enum {
        DATA_SIZE = 512,
        RELOCS = 4,
        FILE_SIZE = 32 + DATA_SIZE + RELOCS * 4
    };
    unsigned char file[FILE_SIZE] = { 0 };
    unsigned char* data = file + 32;
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeStageArchive* stage;
    NativeArchiveError error;
    void* result;
    void* again;
    word(file, 0, FILE_SIZE);
    word(file, 4, DATA_SIZE);
    word(file, 8, RELOCS);
    word(data, 0, 0x3F800000);
    word(data, 4, 0x00800000);
    word(data, 0x4C, 1);
    word(data, 0x68, 0x0001FFFF);
    word(data, 0xB0, 220);
    word(data, 0xB4, 1);
    data[0xB8] = 11;
    data[0xB9] = 22;
    data[0xBA] = 33;
    data[0xBB] = 44;
    word(data, 220, St_Kind_Battle);
    word(data, 220 + 0x18, 0x0002FFFE);
    word(data, 320, 364);
    word(data, 324, 2);
    word(data, 328, 380);
    word(data, 332, 1);
    word(data, 336, 1);
    word(data, 356, 396);
    word(data, 360, 1);
    word(data, 364, 0xC1200000);
    word(data, 372, 0x41200000);
    word(data, 380, 1);
    word(data, 384, 0xFFFFFFFF);
    word(data, 396, 1);
    word(data, 396 + 20, 0xC1200000);
    word(data, 396 + 28, 0x41200000);
    word(data, 396 + 36, 2);
    const uint32_t relocations[] = { 0xB0, 320, 328, 356 };
    for (size_t i = 0; i < RELOCS; ++i) {
        word(file, 32 + DATA_SIZE + i * 4, relocations[i]);
    }
    assert(NativeArchiveOpen(file, sizeof(file), &archive, &error) ==
           NATIVE_ARCHIVE_OK);
    NativeArchiveNullExternals(archive);
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    stage = NativeStageArchiveOpen(archive, graph);
    assert(stage != NULL);
    assert(NativeStageArchiveRead(stage, "grGroundParam", 0, &result,
                                  &error) == NATIVE_ARCHIVE_OK);
    GroundParam* ground = result;
    assert(ground->y == 1 && ground->x4 == 128 && ground->x4C_fixed_cam);
    assert(ground->x68 == 1 && ground->x6A[0] == -1);
    assert(ground->stage_param_count == 1 &&
           ground->stage_params[0].stkind == St_Kind_Battle);
    assert(ground->stage_params[0].x18 == 2 &&
           ground->stage_params[0].x1A[0] == -2);
    assert(ground->xB8.r == 11 && ground->xB8.a == 44);
    assert(NativeStageArchiveRead(stage, "grGroundParam", 0, &again, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(again == ground);
    assert(NativeStageArchiveRead(stage, "coll_data", 320, &result, &error) ==
           NATIVE_ARCHIVE_OK);
    MapCollData* collision = result;
    assert(collision->vert_count == 2 && collision->line_count == 1);
    assert(collision->verts[0].x == -10 && collision->verts[1].x == 10);
    assert(collision->lines[0].v1_idx == 1 &&
           collision->lines[0].prev_id0 == -1);
    assert(collision->floor_count == 1 && collision->joints[0].vtx_count == 2);
    assert(NativeStageArchiveRead(stage, "map_head", 436, &result, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(((UnkStageDat*) result)->unkC == 0);
    NativeStageArchiveClose(stage);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);

    word(data, 0xB4, 0x7FFFFFFF);
    assert(NativeArchiveOpen(file, sizeof(file), &archive, &error) ==
           NATIVE_ARCHIVE_OK);
    NativeArchiveNullExternals(archive);
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    stage = NativeStageArchiveOpen(archive, graph);
    assert(NativeStageArchiveRead(stage, "grGroundParam", 0, &result,
                                  &error) == NATIVE_ARCHIVE_BOUNDS);
    assert(result == NULL);
    NativeStageArchiveClose(stage);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_zebes_parameters(void)
{
    enum {
        DATA_SIZE = 1024,
        RELOCS = 2,
        FILE_SIZE = 32 + DATA_SIZE + 8
    };
    unsigned char file[FILE_SIZE] = { 0 };
    unsigned char* data = file + 32;
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeArchiveError error;
    void* result;
    word(file, 0, FILE_SIZE);
    word(file, 4, DATA_SIZE);
    word(file, 8, RELOCS);
    word(data, 0xB0, 220);
    word(data, 0xB4, 1);
    word(data, 220, St_Kind_Zebes);
    word(data, 320, 0x3F800000);
    word(data, 320 + 0x2C, 800);
    word(data, 320 + 0x30, 0x40000000);
    word(data, 320 + 0x9C, 0x40400000);
    word(data, 320 + 0xA0, 0xFFFE0014);
    word(data, 320 + 0xA4, 0x00280003);
    word(data, 320 + 0x18C, 0x0032FFFF);
    word(data, 800, HitCapsule_Enabled);
    word(data, 804, 14);
    word(data, 808, 90);
    word(data, 832, 8);
    word(file, 32 + DATA_SIZE, 0xB0);
    word(file, 32 + DATA_SIZE + 4, 320 + 0x2C);
    assert(NativeArchiveOpen(file, sizeof(file), &archive, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    NativeStageArchive* stage = NativeStageArchiveOpen(archive, graph);
    assert(NativeStageArchiveRead(stage, "grGroundParam", 0, &result,
                                  &error) == NATIVE_ARCHIVE_OK);
    assert(NativeStageArchiveRead(stage, "yakumono_param", 320, &result,
                                  &error) == NATIVE_ARCHIVE_OK);
    grZe_YakumonoParam* parameters = result;
    assert(parameters->x00 == 1 && parameters->x30 == 2 &&
           parameters->x9C == 3);
    assert(parameters->x2C != NULL && parameters->x2C->damage == 14 &&
           parameters->x2C->kb_angle == 90 && parameters->x2C->sfx_kind == 8);
    assert(parameters->xA0_entries[0].x0_base == -2 &&
           parameters->xA0_entries[0].x2_delay_min == 20 &&
           parameters->xA0_entries[0].x4_delay_max == 40 &&
           parameters->xA0_entries[0].x6_level == 3);
    assert(parameters->xA0_entries[29].x4_delay_max == 50 &&
           parameters->xA0_entries[29].x6_level == -1);
    NativeStageArchiveClose(stage);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static uint16_t native_half(const void* data, size_t offset)
{
    uint16_t value;
    memcpy(&value, (const unsigned char*) data + offset, sizeof(value));
    return value;
}

static uint32_t native_word(const void* data, size_t offset)
{
    uint32_t value;
    memcpy(&value, (const unsigned char*) data + offset, sizeof(value));
    return value;
}

static void test_greatbay_parameters(void)
{
    enum {
        DATA_SIZE = 512,
        FILE_SIZE = 32 + DATA_SIZE + 4,
    };
    unsigned char file[FILE_SIZE] = { 0 };
    unsigned char* data = file + 32;
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeArchiveError error;
    void* result;
    word(file, 0, FILE_SIZE);
    word(file, 4, DATA_SIZE);
    word(file, 8, 1);
    word(data, 0xB0, 220);
    word(data, 0xB4, 1);
    word(data, 220, St_Kind_GreatBay);

    word(data, 320, 0xFFFE0003);
    word(data, 320 + 0x04, 0x3F800000);
    word(data, 320 + 0x40, 0x41200000);
    word(data, 320 + 0x44, 0xFFFD0004);
    word(data, 320 + 0x4C, 0x40000000);
    word(data, 320 + 0x6C, 0x40A00000);
    word(data, 320 + 0x70, 0xFFFC0005);
    word(data, 320 + 0x78, 0x3F000000);
    word(data, 320 + 0x7C, 0x000A0014);
    word(data, 320 + 0xA0, 0xFFFB0006);

    word(file, 32 + DATA_SIZE, 0xB0);
    assert(NativeArchiveOpen(file, sizeof(file), &archive, &error) ==
           NATIVE_ARCHIVE_OK);
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    NativeStageArchive* stage = NativeStageArchiveOpen(archive, graph);
    assert(NativeStageArchiveRead(stage, "grGroundParam", 0, &result,
                                  &error) == NATIVE_ARCHIVE_OK);
    assert(NativeStageArchiveRead(stage, "yakumono_param", 320, &result,
                                  &error) == NATIVE_ARCHIVE_OK);

    assert((int16_t) native_half(result, 0) == -2);
    assert((int16_t) native_half(result, 2) == 3);
    assert(native_word(result, 0x04) == 0x3F800000);
    assert(native_word(result, 0x40) == 0x41200000);
    assert((int16_t) native_half(result, 0x44) == -3);
    assert((int16_t) native_half(result, 0x46) == 4);
    assert(native_word(result, 0x4C) == 0x40000000);
    assert(native_word(result, 0x6C) == 0x40A00000);
    assert((int16_t) native_half(result, 0x70) == -4);
    assert((int16_t) native_half(result, 0x72) == 5);
    assert(native_word(result, 0x78) == 0x3F000000);
    assert((int16_t) native_half(result, 0x7C) == 10);
    assert((int16_t) native_half(result, 0x7E) == 20);
    assert((int16_t) native_half(result, 0xA0) == -5);
    assert((int16_t) native_half(result, 0xA2) == 6);

    NativeStageArchiveClose(stage);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

/* Optional local DAT paths exercise full stage graphs without committing game
 * data. */
static void test_real_stage(const char* path)
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
    NativeArchiveNullExternals(archive);
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    NativeStageArchive* stage = NativeStageArchiveOpen(archive, graph);
    const char* symbols[] = { "grGroundParam", "coll_data", "map_head",
                              "map_plit", "yakumono_param" };
    for (size_t i = 0; i < sizeof(symbols) / sizeof(*symbols); ++i) {
        uint32_t offset;
        void* result;
        assert(NativeArchiveFind(archive, symbols[i], &offset, &error) ==
               NATIVE_ARCHIVE_OK);
        if (NativeStageArchiveRead(stage, symbols[i], offset, &result,
                                   &error) != NATIVE_ARCHIVE_OK)
        {
            fprintf(stderr, "%s %s at %zu: %s\n", path, symbols[i],
                    error.offset, error.message);
            exit(1);
        }
        if (i == 0) {
            GroundParam* ground = result;
            assert(isfinite(ground->y) && ground->y > 0 &&
                   ground->stage_param_count > 0);
        } else if (i == 1) {
            MapCollData* collision = result;
            assert(collision->vert_count > 0 && collision->line_count > 0 &&
                   collision->joint_count > 0);
            for (int j = 0; j < collision->line_count; ++j) {
                assert(collision->lines[j].v0_idx < collision->vert_count);
                assert(collision->lines[j].v1_idx < collision->vert_count);
            }
        } else if (i == 2) {
            UnkStageDat* map = result;
            assert(map->unkC > 0 && map->unk8[0].unk0 != NULL);
        }
    }
    printf("Validated native stage %s\n", path);
    NativeStageArchiveClose(stage);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
    free(bytes);
}

int main(int argc, char** argv)
{
    test_null_external_initialization();
    test_stage_layouts();
    test_zebes_parameters();
    test_greatbay_parameters();
    for (int i = 1; i < argc; ++i) {
        test_real_stage(argv[i]);
    }
    puts("Native stage archive tests passed");
    return 0;
}
