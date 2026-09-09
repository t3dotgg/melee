#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../assets/fighter_attributes.h"
#include <melee/ft/kinds/ftCaptain/types.h>
#include <melee/ft/kinds/ftFox/types.h>
#include <melee/ft/kinds/ftGameWatch/types.h>
#include <melee/ft/kinds/ftKirby/types.h>
#include <melee/ft/kinds/ftLink/types.h>
#include <melee/ft/kinds/ftMario/types.h>
#include <melee/ft/kinds/ftPurin/types.h>
#include <melee/ft/kinds/ftYoshi/types.h>

static void word(unsigned char* data, size_t offset, uint32_t value)
{
    data[offset] = value >> 24;
    data[offset + 1] = value >> 16;
    data[offset + 2] = value >> 8;
    data[offset + 3] = value;
}

static NativeArchive* open_archive(unsigned char* file, size_t size,
                                   size_t data_size, size_t relocations)
{
    NativeArchive* archive;
    NativeArchiveError error;
    word(file, 0, size);
    word(file, 4, data_size);
    word(file, 8, relocations);
    assert(NativeArchiveOpen(file, size, &archive, &error) ==
           NATIVE_ARCHIVE_OK);
    return archive;
}

static void test_scalars_and_reflector(void)
{
    unsigned char file[32 + 0x200] = { 0 };
    unsigned char* data = file + 32;
    NativeArchive* archive;
    NativeFighterAttributes* context;
    NativeArchiveError error;
    void* output;
    void* again;
    word(data, 0, 0x3FC00000);
    word(data, 0x14, 0x0000002A);
    word(data, 0x60, 0x00000008);
    word(data, 0x64, 0xFFFFFFFE);
    word(data, 0x70, 0xC0200000);
    word(data, 0x78, 0x40000000);
    word(data, 0x80, 0x01020304);
    word(data, 0x100 + 0xA4, 0xFFFFFFFC);
    word(data, 0x100 + 0xB0, 7);
    word(data, 0x100 + 0xD0, 0xA5112233);
    archive = open_archive(file, sizeof(file), sizeof(file) - 32, 0);
    context = NativeFighterAttributesOpen(archive, NULL);
    assert(context != NULL);
    assert(NativeFighterAttributesRead(context, "ftDataMario", 0, &output,
                                       &error) == NATIVE_ARCHIVE_OK);
    ftMario_DatAttrs* mario = output;
    assert(mario->specials.vel_x_decay == 1.5F);
    assert(mario->specials.cape_kind == 42);
    assert(mario->cape_reflection.x0_bone_id == 8);
    assert(mario->cape_reflection.x4_max_damage == -2);
    assert(mario->cape_reflection.x8_offset.z == -2.5F);
    assert(mario->cape_reflection.x18_damage_mul == 2.0F);
    assert(mario->cape_reflection.x20_behavior == 1);
    assert(NativeFighterAttributesRead(context, "ftDataDrmario", 0, &again,
                                       &error) == NATIVE_ARCHIVE_OK);
    assert(again == mario);
    assert(
        NativeFighterAttributesRead(context, "ftDataFox", 0, &again, &error) ==
        NATIVE_ARCHIVE_TYPE_CONFLICT);
    assert(again == NULL);
    assert(NativeFighterAttributesRead(context, "ftDataFox", 0x100, &output,
                                       &error) == NATIVE_ARCHIVE_OK);
    struct ftFox_DatAttrs* fox = output;
    assert(fox->xA4_FOX_REFLECTOR_GRAVITY_DELAY == -4);
    assert(fox->xB0_FOX_REFLECTOR_REFLECTION.x0_bone_id == 7);
    assert(fox->xB0_FOX_REFLECTOR_REFLECTION.x20_behavior == 0xA5);
    NativeFighterAttributesClose(context);
    NativeArchiveClose(archive);
}

static void test_mixed_layouts(void)
{
    unsigned char file[32 + 0x1000] = { 0 };
    unsigned char* data = file + 32;
    NativeArchive* archive;
    NativeFighterAttributes* context;
    NativeArchiveError error;
    void* output;
    word(data, 0x34, 0xFFFE1234);
    word(data, 0x38, 0x3F800000);
    word(data, 0x420, 0x01123456);
    word(data, 0x500 + 4, 0x11223344);
    word(data, 0x500 + 0x14, 0x55667788);
    word(data, 0x500 + 0x18, 0x3FC00000);
    word(data, 0x600 + 0x6C, 0x01020304);
    word(data, 0x600 + 0x70, 0x05060708);
    word(data, 0x600 + 0x74, 0x09112233);
    word(data, 0x600 + 0x78, 0xFFFFFFFE);
    word(data, 0x600 + 0x94, 7);
    word(data, 0x600 + 0x98, 0xFFFFFFFD);
    word(data, 0x600 + 0x9C, 63);
    word(data, 0x600 + 0xA0, 88);
    word(data, 0x600 + 0xA4, 42);
    word(data, 0x600 + 0xBC, 79);
    word(data, 0x600 + 0xD8, 0xC0200000);
    word(data, 0x800 + 0xE8, 0x3E800000);
    word(data, 0x800 + 0xEC, 0x3E4CCCCD);
    word(data, 0x800 + 0xF0, 0xC0200000);
    word(data, 0x800 + 0xF4, 0x3FC00000);
    word(data, 0xA00 + 0x1C, 0xC0000000);
    word(data, 0xA00 + 0xEC, 0x3FC00000);
    word(data, 0xA00 + 0x118, 0xC0200000);
    archive = open_archive(file, sizeof(file), sizeof(file) - 32, 0);
    context = NativeFighterAttributesOpen(archive, NULL);
    assert(NativeFighterAttributesRead(context, "ftDataKirby", 0, &output,
                                       &error) == NATIVE_ARCHIVE_OK);
    struct ftKb_DatAttrs* kirby = output;
    assert(kirby->jumpaerial_unk == -2);
    assert(kirby->specialn_x_offset_inhaled == 1.0F);
    assert(kirby->specialn_zd_reflectdesc.x20_behavior == 1);
    assert(NativeFighterAttributesRead(context, "ftDataGamewatch", 0x500,
                                       &output, &error) == NATIVE_ARCHIVE_OK);
    ftGameWatchAttributes* gamewatch = output;
    assert(gamewatch->x4_GAMEWATCH_COLOR[0].r == 0x11);
    assert(gamewatch->x4_GAMEWATCH_COLOR[0].a == 0x44);
    assert(gamewatch->x14_GAMEWATCH_OUTLINE.g == 0x66);
    assert(gamewatch->x18_GAMEWATCH_CHEF_LOOPFRAME == 1.5F);
    assert(NativeFighterAttributesRead(context, "ftDataLink", 0x600, &output,
                                       &error) == NATIVE_ARCHIVE_OK);
    struct ftLk_DatAttrs* link = output;
    assert(link->x64.x8 == 1 && link->x64.xF == 8 && link->x64.x10 == 9);
    assert(link->x64.x14 == -2);
    assert(link->x94 == 7 && link->x98 == -3);
    assert(link->x9C == 63 && link->xA0 == 88);
    assert(link->xA4 == 42 && link->xD8 == -2.5F);
    /* ftCo_0D8E.c reads the same block through a contiguous s32 view. */
    s32 catch_fields[11];
    memcpy(catch_fields, (unsigned char*) link + 0x94, sizeof(catch_fields));
    assert(catch_fields[0] == 7 && catch_fields[1] == -3);
    assert(catch_fields[2] == 63 && catch_fields[3] == 88);
    assert(catch_fields[10] == 79);
    assert(NativeFighterAttributesRead(context, "ftDataPurin", 0x800, &output,
                                       &error) == NATIVE_ARCHIVE_OK);
    ftPurinAttributes* purin = output;
    assert((uintptr_t) purin->xE8 == 0x3E800000);
    assert((uintptr_t) purin->xEC == 0x3E4CCCCD);
    assert(purin->xF0 == -2.5F && purin->xF4 == 1.5F);
    assert(NativeFighterAttributesRead(context, "ftDataYoshi", 0xA00, &output,
                                       &error) == NATIVE_ARCHIVE_OK);
    assert(((ftYoshiAttributes*) output)->x1C == -2.0F);
    assert(((struct ftYs_DatAttrs*) output)->xEC == 1.5F);
    assert(((struct ftYs_DatAttrs*) output)->speciallw_star_offset.x == -2.5F);
    NativeFighterAttributesClose(context);
    NativeArchiveClose(archive);
}

static void test_rejected_data(void)
{
    unsigned char file[32 + 0x8C + 4] = { 0 };
    NativeArchive* archive;
    NativeFighterAttributes* context;
    NativeArchiveError error;
    void* output;
    word(file, 32 + 0x8C, 0);
    archive = open_archive(file, sizeof(file), 0x8C, 1);
    context = NativeFighterAttributesOpen(archive, NULL);
    assert(NativeFighterAttributesRead(context, "ftDataCaptain", 0, &output,
                                       &error) == NATIVE_ARCHIVE_UNSUPPORTED);
    assert(output == NULL && error.offset == 32);
    assert(NativeFighterAttributesRead(context, "ftDataCaptain", 4, &output,
                                       &error) == NATIVE_ARCHIVE_BOUNDS);
    assert(output == NULL);
    assert(NativeFighterAttributesRead(context, "ftDataCaptain", 1, &output,
                                       &error) == NATIVE_ARCHIVE_BOUNDS);
    assert(NativeFighterAttributesRead(context, "ftDataUnknown", 0, &output,
                                       &error) == NATIVE_ARCHIVE_NOT_FOUND);
    assert(NativeFighterAttributesRead(context, "map_head", 0, &output,
                                       &error) == NATIVE_ARCHIVE_NOT_FOUND);
    assert(NativeFighterAttributesRead(context, "ftDataCaptain", 0, NULL,
                                       &error) == NATIVE_ARCHIVE_INVALID);
    NativeFighterAttributesClose(context);
    NativeArchiveClose(archive);
}

/* Optional local DAT arguments test every ftData root against its real
 * ext_attr reference. Disc data remains outside the test source. */
static void test_archive_file(const char* path)
{
    FILE* file = fopen(path, "rb");
    long size;
    void* bytes;
    NativeArchive* archive;
    NativeFighterAttributes* context;
    NativeArchiveError error;
    size_t converted = 0;
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size > 0 && fseek(file, 0, SEEK_SET) == 0);
    bytes = malloc(size);
    assert(bytes != NULL && fread(bytes, 1, size, file) == (size_t) size);
    fclose(file);
    assert(NativeArchiveOpen(bytes, size, &archive, &error) ==
           NATIVE_ARCHIVE_OK);
    free(bytes);
    context = NativeFighterAttributesOpen(archive, NULL);
    for (size_t i = 0; i < NativeArchivePublicCount(archive); ++i) {
        NativeArchiveSymbol symbol;
        uint32_t offset;
        bool present;
        void* output;
        NativeArchiveStatus status;
        assert(NativeArchivePublic(archive, i, &symbol, &error) ==
               NATIVE_ARCHIVE_OK);
        if (strncmp(symbol.name, "ftData", 6) != 0) {
            continue;
        }
        assert(NativeArchiveReference(archive, symbol.offset + 4, &offset,
                                      &present, &error) == NATIVE_ARCHIVE_OK);
        assert(present);
        status = NativeFighterAttributesRead(context, symbol.name, offset,
                                             &output, &error);
        if (status != NATIVE_ARCHIVE_OK) {
            fprintf(stderr, "%s: %s: %d at %zu: %s\n", path, symbol.name,
                    status, error.offset, error.message);
        }
        assert(status == NATIVE_ARCHIVE_OK && output != NULL);
        ++converted;
    }
    NativeFighterAttributesClose(context);
    NativeArchiveClose(archive);
    printf("%s: %zu fighter attribute blocks\n", path, converted);
}

int main(int argc, char** argv)
{
    test_scalars_and_reflector();
    test_mixed_layouts();
    test_rejected_data();
    for (int i = 1; i < argc; ++i) {
        test_archive_file(argv[i]);
    }
    puts("fighter attribute conversion passed");
    return 0;
}
