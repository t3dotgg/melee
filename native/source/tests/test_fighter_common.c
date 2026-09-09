#include <assert.h>
#undef __assert
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../assets/fighter_common.h"
#include <melee/ft/fighter.h>

static void word(u8* bytes, size_t offset, u32 value)
{
    bytes[offset] = value >> 24;
    bytes[offset + 1] = value >> 16;
    bytes[offset + 2] = value >> 8;
    bytes[offset + 3] = value;
}

typedef struct Fixture {
    u8 bytes[4096];
    size_t data_size;
    u32 relocations[64];
    size_t count;
} Fixture;

static void ref(Fixture* fixture, u32 field, u32 target)
{
    word(fixture->bytes + 32, field, target);
    fixture->relocations[fixture->count++] = field;
}

static NativeArchive* open_fixture(Fixture* fixture)
{
    size_t size = 32 + fixture->data_size + fixture->count * 4;
    NativeArchive* archive;
    NativeArchiveError error;
    word(fixture->bytes, 0, size);
    word(fixture->bytes, 4, fixture->data_size);
    word(fixture->bytes, 8, fixture->count);
    for (size_t i = 0; i < fixture->count; ++i) {
        word(fixture->bytes, 32 + fixture->data_size + i * 4,
             fixture->relocations[i]);
    }
    assert(NativeArchiveOpen(fixture->bytes, size, &archive, &error) ==
           NATIVE_ARCHIVE_OK);
    return archive;
}

static void test_attributes(void)
{
    Fixture fixture = { .data_size = 0x818 + 23 * 4 };
    u8* data = fixture.bytes + 32;
    /* Offset zero is a real reference. Numeric legacy fields are not. */
    ref(&fixture, 0x818, 0);
    word(data, 0, 0x3F000000);
    word(data, 0x1E0, 0x3F800000);
    word(data, 0x274, 4);
    word(data, 0x278, 0x40000000);
    word(data, 0x3A4, 0x40400000);
    word(data, 0x500, 60);
    word(data, 0x510, 0x40800000);
    word(data, 0x5C4, 100);
    word(data, 0x5C8, 120);
    word(data, 0x6DC, 0x11223344);
    word(data, 0x6EC, 0x55667788);
    word(data, 0x7D8, 0x99AABBCC);
    word(data, 0x808, 0x40A00000);
    word(data, 0x814, 180);
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph;
    NativeArchiveError error;
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    NativeFighterCommonArchive* common =
        NativeFighterCommonArchiveOpen(archive, graph);
    void* root;
    assert(NativeFighterCommonArchiveRead(common, "unrelated", 0, &root,
                                          &error) == NATIVE_ARCHIVE_NOT_FOUND);
    assert(root == NULL);
    assert(NativeFighterCommonArchiveRead(common, "ftLoadCommonData", 0x818,
                                          &root, &error) == NATIVE_ARCHIVE_OK);
    ftCommonData* attributes = ((void**) root)[0];
    assert(attributes->horizontal_stick_deadzone == 0.5f);
    assert(attributes->x1E0 == 1 && attributes->x278 == 2);
    assert((uintptr_t) attributes->x274 == 4);
    assert(attributes->grab_timer_decrement == 3 && attributes->x510 == 4);
    assert((uintptr_t) attributes->x500 == 60);
    assert((uintptr_t) attributes->x5C4 == 100 && attributes->x5C8 == 120);
    assert(attributes->x6DC_colorsByPlayer[0].r == 0x11);
    assert(attributes->x6DC_colorsByPlayer[0].a == 0x44);
    assert(attributes->x6EC[0] == 0x55 && attributes->x6EC[3] == 0x88);
    assert(attributes->x7D8.r == 0x99 && attributes->x7D8.a == 0xCC);
    assert(attributes->x808.x == 5 && attributes->x814 == 180);
    void* again;
    assert(NativeFighterCommonArchiveRead(common, "ftLoadCommonData", 0x818,
                                          &again,
                                          &error) == NATIVE_ARCHIVE_OK);
    assert(again == root);
    u8 unchanged[0x818];
    assert(NativeArchiveRead(archive, 0, unchanged, sizeof(unchanged),
                             &error) == NATIVE_ARCHIVE_OK);
    assert(memcmp(unchanged, data, sizeof(unchanged)) == 0);
    NativeFighterCommonArchiveClose(common);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_tables(bool bad_count)
{
    Fixture fixture = { .data_size = 156 };
    u8* data = fixture.bytes + 32;
    ref(&fixture, 4 * 4, 92);
    ref(&fixture, 92, 100);
    ref(&fixture, 96, 100);
    ref(&fixture, 100, 112);
    ref(&fixture, 104, 116);
    word(data, 108, 4);
    word(data, 112, 0x0001FF03);
    word(data, 116, 0x03010002);
    ref(&fixture, 6 * 4, 120);
    ref(&fixture, 128, 136);
    word(data, 132, 0x1E010000);
    word(data, 136, 0x08010203);
    ref(&fixture, 9 * 4, 140);
    ref(&fixture, 10 * 4, 140);
    ref(&fixture, 140, 148);
    word(data, 144, bad_count ? 2 : 1);
    word(data, 148, 0xBF800000);
    word(data, 152, 0x40000000);
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph;
    NativeArchiveError error;
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    NativeFighterCommonArchive* common =
        NativeFighterCommonArchiveOpen(archive, graph);
    void* root;
    NativeArchiveStatus status = NativeFighterCommonArchiveRead(
        common, "ftLoadCommonData", 0, &root, &error);
    if (bad_count) {
        assert(status == NATIVE_ARCHIVE_INVALID && root == NULL);
        assert(NativeFighterCommonArchiveRead(common, "ftLoadCommonData", 0,
                                              &root, &error) == status);
        assert(root == NULL);
    } else {
        assert(status == NATIVE_ARCHIVE_OK);
        void** targets = root;
        FighterPartsTable** parts = targets[4];
        assert(parts[0] == parts[1]);
        assert(parts[0]->parts_num == 4 && parts[0]->joint_to_part[2] == 255);
        assert(parts[0]->part_to_joint[0] == 3);
        struct Fighter_804D653C_t* colors = targets[6];
        assert(colors[0].unk == NULL && colors[1].unk4 == 30);
        assert(colors[1].unk5 == 1);
        const u8* commands = colors[1].unk;
        assert(commands[0] == 8 && commands[3] == 3);
        struct Fighter_DamageFallSamples* samples = targets[9];
        assert(targets[9] == targets[10]);
        assert(samples[0].count == 1 && samples[0].samples[0].x == -1);
        assert(samples[0].samples[0].y == 2);
    }
    NativeFighterCommonArchiveClose(common);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_cpu(void)
{
    Fixture fixture = { .data_size = 220 };
    u8* data = fixture.bytes + 32;
    ref(&fixture, 22 * 4, 92);
    ref(&fixture, 92, 132);
    ref(&fixture, 96, 140);
    ref(&fixture, 100, 140);
    ref(&fixture, 124, 192);
    ref(&fixture, 128, 196);
    ref(&fixture, 132, 188);
    ref(&fixture, 136, 188);
    ref(&fixture, 140, 148);
    ref(&fixture, 144, 148);
    word(data, 148, 3);
    word(data, 152, 0xFFFFFFFF);
    word(data, 156, 0x3F800000);
    word(data, 184, 0);
    word(data, 188, 0xC07F7F7F);
    word(data, 192, 0x41200000);
    word(data, 216, 0x41A00000);
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph;
    NativeArchiveError error;
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    NativeFighterCommonArchive* common =
        NativeFighterCommonArchiveOpen(archive, graph);
    void* root;
    assert(NativeFighterCommonArchiveRead(common, "ftLoadCommonData", 0, &root,
                                          &error) == NATIVE_ARCHIVE_OK);
    struct Fighter_804D64FC_t* cpu = ((void**) root)[22];
    assert(cpu->cmdscripts[0] == cpu->cmdscripts[1]);
    assert(cpu->cmdscripts[0][0] == 0xC0 && cpu->cmdscripts[0][1] == 0x7F);
    assert(cpu->x4 == cpu->x8 && cpu->x4[0] == cpu->x4[1]);
    const s32* attack = cpu->x4[0];
    assert(attack[0] == 3 && attack[1] == -1 && attack[9] == 0);
    assert(cpu->x20[0] == 10 && ((float*) cpu->x24)[5] == 20);
    NativeFighterCommonArchiveClose(common);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

/* Optional local PlCo.dat input checks the complete graph. */
static void test_real_archive(const char* path)
{
    FILE* file = fopen(path, "rb");
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
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
    assert(NativeArchiveGraphOpen(archive, &graph, &error) ==
           NATIVE_ARCHIVE_OK);
    NativeFighterCommonArchive* common =
        NativeFighterCommonArchiveOpen(archive, graph);
    uint32_t offset;
    void* root;
    assert(NativeArchiveFind(archive, "ftLoadCommonData", &offset, &error) ==
           NATIVE_ARCHIVE_OK);
    NativeArchiveStatus status = NativeFighterCommonArchiveRead(
        common, "ftLoadCommonData", offset, &root, &error);
    if (status != NATIVE_ARCHIVE_OK) {
        fprintf(stderr, "%s at %zu: %s\n", path, error.offset, error.message);
        exit(1);
    }
    void** targets = root;
    for (size_t i = 0; i < 23; ++i) {
        assert(targets[i] != NULL);
    }
    const ftCommonData* attributes = targets[0];
    assert(attributes->horizontal_stick_deadzone > 0 &&
           attributes->horizontal_stick_deadzone < 1);
    FighterPartsTable** parts = targets[4];
    assert(parts[0]->parts_num > 0 && parts[0]->joint_to_part != NULL);
    assert(((struct Fighter_DamageFallSamples*) targets[9])[0].count > 0);
    struct Fighter_804D64FC_t* cpu = targets[22];
    assert(cpu->cmdscripts[0x3D] != NULL && cpu->x4[0] != NULL);
    NativeFighterCommonArchiveClose(common);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
    free(bytes);
}

int main(int argc, char** argv)
{
    test_attributes();
    test_tables(false);
    test_tables(true);
    test_cpu();
    for (int i = 1; i < argc; ++i) {
        test_real_archive(argv[i]);
    }
    puts("Native fighter common archive tests passed");
    return 0;
}
