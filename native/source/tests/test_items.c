#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../assets/items.h"
#include <melee/gr/types.h>
#include <melee/it/it_3F14.h>
#include <melee/it/types.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

typedef struct Fixture {
    uint8_t file[8192];
    uint32_t relocations[64];
    size_t relocation_count;
    size_t size;
} Fixture;

static void be32(uint8_t* output, uint32_t value)
{
    output[0] = value >> 24;
    output[1] = value >> 16;
    output[2] = value >> 8;
    output[3] = value;
}

static void word(Fixture* fixture, uint32_t offset, uint32_t value)
{
    CHECK(offset < 4096);
    be32(fixture->file + 32 + offset, value);
}

static void reference(Fixture* fixture, uint32_t field, uint32_t target)
{
    CHECK(fixture->relocation_count < 64);
    fixture->relocations[fixture->relocation_count++] = field;
    word(fixture, field, target);
}

static Fixture fixture_new(void)
{
    Fixture fixture = { 0 };
    /* Public root, common settings, and the three item categories. */
    reference(&fixture, 0x100, 0x200);
    reference(&fixture, 0x104, 0x400);
    reference(&fixture, 0x108, 0x800);
    reference(&fixture, 0x10c, 0xa00);
    reference(&fixture, 0x110, 0xb00);
    reference(&fixture, 0x114, 0xb40);
    word(&fixture, 0x200, 40);
    word(&fixture, 0x230, 1400);
    fixture.file[32 + 0x248] = 0x44;
    word(&fixture, 0x24c, 0x3f000000);
    word(&fixture, 0x35c, 0x3f800000);
    reference(&fixture, 0x400, 0x620);
    reference(&fixture, 0x404, 0x620);
    reference(&fixture, 0x620, 0x640);
    reference(&fixture, 0x624, 0xe00);
    reference(&fixture, 0x628, 0x6d0);
    reference(&fixture, 0x62c, 0x600);
    reference(&fixture, 0x630, 0x730);
    reference(&fixture, 0x634, 0x750);
    fixture.file[32 + 0x640] = 0xad;
    fixture.file[32 + 0x641] = 0xb7;
    fixture.file[32 + 0x642] = 0x31;
    word(&fixture, 0x644, 0x3fc00000);
    word(&fixture, 0x660, 0x40800000);
    word(&fixture, 0x6a0, 0x3f800000);
    word(&fixture, 0x6b8, 0x10203);
    word(&fixture, 0x6d0, 1);
    reference(&fixture, 0x6d4, 0x700);
    word(&fixture, 0x700, 3);
    word(&fixture, 0x704, 0x3f800000);
    word(&fixture, 0x71c, 0x40000000);
    word(&fixture, 0x734, 5);
    word(&fixture, 0x738, 3);
    fixture.file[32 + 0x73c] = 0x80;
    word(&fixture, 0x750, 1);
    reference(&fixture, 0x754, 0x770);
    word(&fixture, 0x758, 1);
    reference(&fixture, 0x75c, 0x7d0);
    word(&fixture, 0x770, 7);
    reference(&fixture, 0x774, 0x790);
    word(&fixture, 0x778, 1);
    word(&fixture, 0x77c, 0x3f800000);
    word(&fixture, 0x790, 0x3fc00000);
    word(&fixture, 0x7d0, 7);
    word(&fixture, 0x7d4, 0x40000000);
    word(&fixture, 0x7e0, 0x40800000);
    reference(&fixture, 0x60c, 0xf00);
    reference(&fixture, 0x61c, 0xf04);
    word(&fixture, 0xf00, 0x0400003c);
    word(&fixture, 0xf04, 0x18000000);
    word(&fixture, 0xb00, 5);
    word(&fixture, 0xb04, 0x40a00000);
    reference(&fixture, 0xb48, 0xf08);
    fixture.file[32 + 0xb4c] = 30;
    fixture.file[32 + 0xb4d] = 4;
    word(&fixture, 0xf08, 0x48000000);
    word(&fixture, 0xe00, 0x3f000000);
    word(&fixture, 0xe04, 0x3c23d70a);
    /* The stage table preserves its reserved first script slot. */
    reference(&fixture, 0xd04, 0xf00);
    reference(&fixture, 0xd40, 0xd60);
    word(&fixture, 0xd60, It_Kind_Capsule);
    reference(&fixture, 0xd64, 0x620);
    return fixture;
}

static void fixture_finish(Fixture* fixture)
{
    const struct {
        uint32_t offset;
        const char* name;
    } publics[] = {
        { 0x100, "itPublicData" },   { 0xb50, "color_end" },
        { 0xe08, "attributes_end" }, { 0xd00, "ALDYakuAll" },
        { 0xd40, "itemdata" },
    };
    size_t cursor = 32 + 4096;
    for (size_t i = 0; i < fixture->relocation_count; i++, cursor += 4) {
        be32(fixture->file + cursor, fixture->relocations[i]);
    }
    size_t public_at = cursor;
    size_t strings = public_at + sizeof(publics) / sizeof(publics[0]) * 8;
    cursor = strings;
    for (size_t i = 0; i < sizeof(publics) / sizeof(publics[0]); i++) {
        be32(fixture->file + public_at + i * 8, publics[i].offset);
        be32(fixture->file + public_at + i * 8 + 4,
             (uint32_t) (cursor - strings));
        size_t size = strlen(publics[i].name) + 1;
        memcpy(fixture->file + cursor, publics[i].name, size);
        cursor += size;
    }
    fixture->size = cursor;
    be32(fixture->file, (uint32_t) cursor);
    be32(fixture->file + 4, 4096);
    be32(fixture->file + 8, (uint32_t) fixture->relocation_count);
    be32(fixture->file + 12, sizeof(publics) / sizeof(publics[0]));
}

static void test_public_data(void)
{
    Fixture fixture = fixture_new();
    NativeArchive* archive = NULL;
    NativeArchiveGraph* graph = NULL;
    NativeArchiveError error = { 0 };
    fixture_finish(&fixture);
    CHECK(NativeArchiveOpen(fixture.file, fixture.size, &archive, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(NativeArchiveGraphOpen(archive, &graph, &error) ==
          NATIVE_ARCHIVE_OK);
    NativeItemArchive* items = NativeItemArchiveOpen(archive, graph);
    CHECK(items != NULL);
    void* output = NULL;
    NativeArchiveStatus status =
        NativeItemArchiveRead(items, "itPublicData", 0x100, &output, &error);
    if (status != NATIVE_ARCHIVE_OK) {
        fprintf(stderr, "item decode %d at %zu: %s\n", status, error.offset,
                error.message);
    }
    CHECK(status == NATIVE_ARCHIVE_OK);
    it_804D6D20_t* root = output;
    CHECK((uintptr_t) root > UINT32_MAX);
    CHECK(root->x0->x0 == 40 && root->x0->x30_lifetime == 1400);
    CHECK(root->x0->x48_byte == 0x44 && root->x0->x4C_float == 0.5f);
    CHECK(root->x0->x15C == 1.0f);
    CHECK(root->x4[0] == root->x4[1]);
    CHECK(root->x8[It_PKind_Random - It_Kind_Kuriboh] == NULL);
    CHECK(root->xC[It_Kind_Pokemon_Unk - It_PKind_Start] == NULL);
    Article* article = root->x4[0];
    ItemAttr* attr = article->x0_common_attr;
    CHECK(attr->x0_is_heavy == 1 && attr->x0_78 == 5 &&
          attr->x0_hold_kind == 5);
    CHECK(attr->x1_1 == 2 && attr->x1_3 == 1 && attr->x1_4 == 1);
    CHECK(attr->x1_5 == 0 && attr->x1_67_cam_kind == 3 && attr->x1_8 == 1);
    CHECK(attr->x3 == 0x31 && attr->x4_throw_speed_mul == 1.5f);
    CHECK(attr->x60_scale == 1.0f && attr->destroy_sfx == 0x10203);
    CHECK(article->x8_hurtbones->count == 1);
    CHECK(article->x8_hurtbones->descs[0].bone_id == 3);
    CHECK(article->x8_hurtbones->descs[0].a_offset.x == 1.0f);
    CHECK(article->x8_hurtbones->descs[0].scale == 2.0f);
    CHECK(article->x10_modelDesc->x4_bone_count == 5);
    CHECK(article->x10_modelDesc->xC_bit_field == 0x80);
    CHECK(article->x14_dynamics->count == 1);
    CHECK(article->x14_dynamics->dyn_descs[0].bone_id == 7);
    CHECK(article->x14_dynamics->dyn_descs[0].dyn_desc.pos.x == 1.0f);
    CHECK(article->x14_dynamics->dyn_descs[0]
              .dyn_desc.data->desc.lb_unk1.array[0]
              .unk_0 == 1.5f);
    CHECK(article->x14_dynamics->collision_count == 1);
    CHECK(article->x14_dynamics->collision_descs[0].bone_id == 7);
    CHECK(article->x14_dynamics->collision_descs[0].offset.x == 2.0f);
    CHECK(article->x14_dynamics->collision_descs[0].size == 4.0f);
    uint8_t* command = article->xC_itemStates->x0_itemStateDesc[0].xC_script;
    CHECK(command[0] == 4 && command[3] == 60);
    CHECK(root->x10->x0 == 5 && root->x10->x4 == 5.0f);
    CHECK(root->x14[0].unk == NULL);
    CHECK(root->x14[1].unk4 == 30 && root->x14[1].unk5 == 4);
    CHECK(*(uint8_t*) root->x14[1].unk == 0x48);
    CHECK(NativeItemArchiveRead(items, "ALDYakuAll", 0xd00, &output, &error) ==
          NATIVE_ARCHIVE_OK);
    void** scripts = output;
    CHECK(scripts[0] == NULL && scripts[1] == command && scripts[2] == NULL);
    CHECK(NativeItemArchiveRead(items, "itemdata", 0xd40, &output, &error) ==
          NATIVE_ARCHIVE_OK);
    struct GroundItemData** stage = output;
    CHECK(stage[0]->unk0 == It_Kind_Capsule && stage[0]->unk4 == article &&
          stage[1] == NULL);
    CHECK(NativeItemArchiveRead(items, "other", 0, &output, &error) ==
          NATIVE_ARCHIVE_NOT_FOUND);
    CHECK(output == NULL);
    CHECK(NativeItemArchiveRead(items, "itPublicData", 0x100, &output,
                                &error) == NATIVE_ARCHIVE_OK);
    CHECK(output == root);
    NativeItemArchiveClose(items);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_invalid_hurtbone_count(void)
{
    Fixture fixture = fixture_new();
    NativeArchive* archive = NULL;
    NativeArchiveGraph* graph = NULL;
    NativeArchiveError error = { 0 };
    word(&fixture, 0x6d0, 3);
    fixture_finish(&fixture);
    CHECK(NativeArchiveOpen(fixture.file, fixture.size, &archive, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(NativeArchiveGraphOpen(archive, &graph, &error) ==
          NATIVE_ARCHIVE_OK);
    NativeItemArchive* items = NativeItemArchiveOpen(archive, graph);
    void* output = (void*) (uintptr_t) 1;
    CHECK(NativeItemArchiveRead(items, "itPublicData", 0x100, &output,
                                &error) == NATIVE_ARCHIVE_INVALID);
    CHECK(output == NULL && error.offset == 32 + 0x6d0);
    NativeItemArchiveClose(items);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_variable_state_count(void)
{
    Fixture fixture = fixture_new();
    NativeArchive* archive = NULL;
    NativeArchiveGraph* graph = NULL;
    NativeArchiveError error = { 0 };
    /* Scope beam has ten states in the game data. The declared PPC array has
     * eight slots, so the native representation must use the actual extent. */
    word(&fixture, 0x62c, 0xc00);
    reference(&fixture, 0xc9c, 0xf04);
    fixture_finish(&fixture);
    CHECK(NativeArchiveOpen(fixture.file, fixture.size, &archive, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(NativeArchiveGraphOpen(archive, &graph, &error) ==
          NATIVE_ARCHIVE_OK);
    NativeItemArchive* items = NativeItemArchiveOpen(archive, graph);
    void* output = NULL;
    CHECK(NativeItemArchiveRead(items, "itPublicData", 0x100, &output,
                                &error) == NATIVE_ARCHIVE_OK);
    it_804D6D20_t* root = output;
    uint8_t* command =
        root->x4[0]->xC_itemStates->x0_itemStateDesc[9].xC_script;
    CHECK(command != NULL && command[0] == 0x18);
    NativeItemArchiveClose(items);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_local_archive(const char* path)
{
    FILE* file = fopen(path, "rb");
    CHECK(file != NULL);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    CHECK(size > 0 && fseek(file, 0, SEEK_SET) == 0);
    void* bytes = malloc((size_t) size);
    CHECK(bytes != NULL &&
          fread(bytes, 1, (size_t) size, file) == (size_t) size);
    fclose(file);
    NativeArchive* archive = NULL;
    NativeArchiveGraph* graph = NULL;
    NativeArchiveError error = { 0 };
    CHECK(NativeArchiveOpen(bytes, (size_t) size, &archive, &error) ==
          NATIVE_ARCHIVE_OK);
    free(bytes);
    CHECK(NativeArchiveGraphOpen(archive, &graph, &error) ==
          NATIVE_ARCHIVE_OK);
    NativeItemArchive* items = NativeItemArchiveOpen(archive, graph);
    CHECK(items != NULL);
    for (size_t i = 0; i < NativeArchivePublicCount(archive); i++) {
        NativeArchiveSymbol symbol;
        CHECK(NativeArchivePublic(archive, i, &symbol, &error) ==
              NATIVE_ARCHIVE_OK);
        void* output = NULL;
        NativeArchiveStatus status = NativeItemArchiveRead(
            items, symbol.name, symbol.offset, &output, &error);
        if (status == NATIVE_ARCHIVE_NOT_FOUND) {
            continue;
        }
        if (status != NATIVE_ARCHIVE_OK) {
            fprintf(stderr, "%s decode %d at 0x%zx: %s\n", symbol.name, status,
                    error.offset, error.message);
        }
        CHECK(status == NATIVE_ARCHIVE_OK && output != NULL);
        printf("Decoded %s\n", symbol.name);
    }
    NativeItemArchiveClose(items);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

int main(int argc, char** argv)
{
    test_public_data();
    test_invalid_hurtbone_count();
    test_variable_state_count();
    for (int i = 1; i < argc; i++) {
        test_local_archive(argv[i]);
    }
    puts("item archive tests passed");
    return 0;
}
