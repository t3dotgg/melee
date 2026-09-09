#include <assert.h>
#include <stdio.h>

#undef __assert

#include "../../../src/melee/ft/ftdata.c"
#include <dolphin/ar.h>
#include <melee/lb/lbanim.h>

static void motion_word(u8* data, size_t offset, u32 value)
{
    data[offset] = value >> 24;
    data[offset + 1] = value >> 16;
    data[offset + 2] = value >> 8;
    data[offset + 3] = value;
}

static void test_motion_bundle(void)
{
    enum {
        DATA_SIZE = 60,
        FILE_SIZE = 32 + DATA_SIZE + 8 + 8 + 7
    };
    u8 bundle[0x100 + FILE_SIZE] = { 0 };
    for (int i = 0; i < 2; ++i) {
        u8* file = bundle + i * 0x100;
        u8* data = file + 32;
        motion_word(file, 0, FILE_SIZE);
        motion_word(file, 4, DATA_SIZE);
        motion_word(file, 8, 2);
        motion_word(file, 12, 1);
        motion_word(data, 0, 1);
        motion_word(data, 4, 0x20000000);
        motion_word(data, 8, i == 0 ? 0x42480000 : 0x42F00000);
        motion_word(data, 12, 20);
        motion_word(data, 16, 24);
        data[20] = 1;
        data[21] = 2;
        data[22] = 0xFF;
        data[24 + 4] = 3;
        data[36 + 4] = 4;
        data[48 + 4] = 5;
        motion_word(file, 32 + DATA_SIZE, 12);
        motion_word(file, 32 + DATA_SIZE + 4, 16);
        memcpy(file + 32 + DATA_SIZE + 16, "motion", 7);
    }
    struct Fighter_WaitAnimData motion = { 0 };
    motion.x0 = "motion";
    motion.x8 = FILE_SIZE;
    FigaTree* first =
        ftDataNativeReadMotion(FTKIND_MARIO, bundle, sizeof(bundle), &motion);
    assert(first->frames == 50 && first->tracks[2].obj_type == 5);
    assert(ftDataNativeReadMotion(FTKIND_MARIO, bundle, sizeof(bundle),
                                  &motion) == first);
    motion.x4 = 0x100;
    FigaTree* second =
        ftDataNativeReadMotion(FTKIND_MARIO, bundle, sizeof(bundle), &motion);
    assert(second != first && second->frames == 120);
    ftDataNativeClearMotions();
    assert(ftData_native_motions[FTKIND_MARIO] == NULL);

    ARInit(NULL, 0);
    u32 address = ARAlloc(sizeof(bundle));
    assert(address != 0);
    ARStartDMA(ARAM_DIR_MRAM_TO_ARAM, (uintptr_t) bundle, address,
               sizeof(bundle));
    FigaTree* aram = ftDataNativeReadMotion(
        FTKIND_MARIO, (void*) (uintptr_t) address, sizeof(bundle), &motion);
    assert(aram->frames == 120 && aram->tracks[2].obj_type == 5);
    ftDataNativeClearMotions();
    ARReset();
}

static void test_real_bundle(const char* path)
{
    FILE* file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size > 0 && fseek(file, 0, SEEK_SET) == 0);
    u8* bytes = malloc(size);
    assert(bytes != NULL && fread(bytes, 1, size, file) == (size_t) size);
    fclose(file);
    size_t count = 0;
    for (size_t offset = 0; offset + 32 <= (size_t) size;) {
        NativeArchive* archive;
        NativeArchiveError error;
        NativeArchiveSymbol symbol;
        u32 length = (u32) bytes[offset] << 24 |
                     (u32) bytes[offset + 1] << 16 |
                     (u32) bytes[offset + 2] << 8 | bytes[offset + 3];
        assert(length >= 32 && length <= (size_t) size - offset);
        assert(NativeArchiveOpen(bytes + offset, length, &archive, &error) ==
               NATIVE_ARCHIVE_OK);
        assert(NativeArchivePublic(archive, 0, &symbol, &error) ==
               NATIVE_ARCHIVE_OK);
        struct Fighter_WaitAnimData motion = { 0 };
        motion.x0 = (char*) symbol.name;
        motion.x4 = offset;
        motion.x8 = length;
        FigaTree* tree =
            ftDataNativeReadMotion(FTKIND_MARIO, bytes, size, &motion);
        assert(tree != NULL && isfinite(tree->frames) && tree->frames >= 0);
        NativeArchiveClose(archive);
        offset += (length + 31u) & ~31u;
        ++count;
    }
    assert(count > 0);
    printf("%s: %zu motion archives OK\n", path, count);
    ftDataNativeClearMotions();
    free(bytes);
}

int main(int argc, char** argv)
{
    test_motion_bundle();
    for (int i = 1; i < argc; ++i) {
        test_real_bundle(argv[i]);
    }
    return 0;
}
