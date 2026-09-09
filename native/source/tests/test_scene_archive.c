#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Dolphin defines a function with the same name as the system macro. */
#undef __assert

/* Exercise the private scene converters without the platform launcher. */
#include <melee/lb/lbarchive.c>
#include <sysdolphin/baselib/aobj.h>

static void word(u8* data, size_t offset, u32 value)
{
    data[offset] = value >> 24;
    data[offset + 1] = value >> 16;
    data[offset + 2] = value >> 8;
    data[offset + 3] = value;
}

static size_t fixture(u8* file, bool unsupported_fog)
{
    enum {
        DATA_SIZE = 272
    };
    const char names[] = "ScTest_scene_data\0ScTest_scene_modelset";
    const u32 references[][2] = {
        { 0, 264 },   { 4, 16 },    { 12, 24 },   { 16, 40 },   { 24, 104 },
        { 28, 124 },  { 124, 132 }, { 132, 148 }, { 164, 184 }, { 248, 184 },
        { 264, 164 }, { 268, 248 }, { 136, 148 },
    };
    size_t count = sizeof(references) / sizeof(*references) - !unsupported_fog;
    u8* data = file + 32;
    memset(file, 0, 512);
    word(data, 32, 0x1200ac01);
    word(data, 44, 1); /* Perspective camera. */
    word(data, 104, 2);
    word(data, 112, 0x45bb8000); /* Fog starts at 6000. */
    word(data, 116, 0x461c4000); /* Fog ends at 10000. */
    word(data, 140, 0x1200ac01);
    word(data, 152, 0x41200000); /* Fog animation lasts 10 frames. */
    for (size_t i = 0; i < count; ++i) {
        word(data, references[i][0], references[i][1]);
        word(file, 32 + DATA_SIZE + i * 4, references[i][0]);
    }
    size_t symbols = 32 + DATA_SIZE + count * 4;
    word(file, symbols, 0);
    word(file, symbols + 4, 0);
    word(file, symbols + 8, 264);
    word(file, symbols + 12, sizeof("ScTest_scene_data"));
    memcpy(file + symbols + 16, names, sizeof(names));
    size_t size = symbols + 16 + sizeof(names);
    word(file, 0, size);
    word(file, 4, DATA_SIZE);
    word(file, 8, count);
    word(file, 12, 2);
    return size;
}

static void open_binding(NativeArchiveBinding* binding, const void* bytes,
                         size_t size)
{
    NativeArchiveError error;
    memset(binding, 0, sizeof(*binding));
    assert(NativeArchiveOpen(bytes, size, &binding->archive, &error) ==
           NATIVE_ARCHIVE_OK);
    NativeArchiveNullExternals(binding->archive);
    assert(NativeArchiveGraphOpen(binding->archive, &binding->graph, &error) ==
           NATIVE_ARCHIVE_OK);
}

static void close_binding(NativeArchiveBinding* binding)
{
    NativeSceneAllocation* allocation = binding->scene_allocations;
    while (allocation != NULL) {
        NativeSceneAllocation* next = allocation->next;
        free(allocation->pointer);
        free(allocation);
        allocation = next;
    }
    NativeArchiveGraphClose(binding->graph);
    NativeArchiveClose(binding->archive);
}

static void test_scene_records(void)
{
    u8 bytes[512];
    NativeArchiveBinding binding;
    NativeArchiveError error = { 0 };
    size_t size = fixture(bytes, false);
    open_binding(&binding, bytes, size);
    SceneDesc* scene = native_scene_root(&binding, 0, &error);
    assert(scene != NULL);
    assert(scene->cameras != NULL && scene->cameras->desc != NULL);
    assert(scene->cameras->anims == NULL);
    assert(scene->fogs != NULL && scene->fogs->desc != NULL);
    assert(scene->fogs->anims[0]->aobjdesc->end_frame == 10.0f);
    assert(scene->fogs->anims[1] == NULL);
    assert(scene->models[0]->joint == scene->models[1]->joint);
    assert(scene->models[2] == NULL);
    size_t count;
    DynamicModelDesc** models =
        native_scene_models(&binding, 264, 0, &count, &error);
    assert(models != NULL && count == 2 && models[2] == NULL);
    assert(models[0]->joint == scene->models[0]->joint);
    close_binding(&binding);

    size = fixture(bytes, true);
    open_binding(&binding, bytes, size);
    assert(native_scene_root(&binding, 0, &error) == NULL);
    assert(error.status == NATIVE_ARCHIVE_UNSUPPORTED && error.offset == 168);
    close_binding(&binding);
}

static size_t test_real_scene(const char* path)
{
    FILE* file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size > 0 && fseek(file, 0, SEEK_SET) == 0);
    void* bytes = malloc(size);
    assert(bytes != NULL && fread(bytes, 1, size, file) == (size_t) size);
    fclose(file);
    NativeArchiveBinding binding;
    NativeArchiveError error = { 0 };
    open_binding(&binding, bytes, size);
    free(bytes);
    size_t roots = 0;
    for (size_t i = 0; i < NativeArchivePublicCount(binding.archive); ++i) {
        NativeArchiveSymbol symbol;
        assert(NativeArchivePublic(binding.archive, i, &symbol, &error) ==
               NATIVE_ARCHIVE_OK);
        void* root;
        if (native_name_ends_with(symbol.name, "_scene_data")) {
            root = native_scene_root(&binding, symbol.offset, &error);
        } else if (native_name_ends_with(symbol.name, "_scene_modelset")) {
            size_t count;
            root =
                native_scene_models(&binding, symbol.offset, 0, &count, &error);
            assert(root == NULL || count > 0);
        } else {
            continue;
        }
        if (root == NULL) {
            fprintf(stderr, "%s %s at %zu: %s\n", path, symbol.name,
                    error.offset, error.message);
            abort();
        }
        ++roots;
    }
    close_binding(&binding);
    return roots;
}

int main(int argc, char** argv)
{
    test_scene_records();
    size_t roots = 0;
    for (int i = 1; i < argc; ++i) {
        roots += test_real_scene(argv[i]);
    }
    printf("Native scene archive tests passed, %zu real roots\n", roots);
    return 0;
}
