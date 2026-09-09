#include "../assets/archive.h"

#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/robj.h>
#include <sysdolphin/baselib/wobj.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);     \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

enum { HEADER_SIZE = 32, FIXTURE_CAPACITY = 4096, TABLE_CAPACITY = 64 };

typedef struct FixtureSymbol {
    uint32_t offset;
    uint32_t name;
} FixtureSymbol;

typedef struct Fixture {
    unsigned char bytes[FIXTURE_CAPACITY];
    uint32_t data_size;
    uint32_t relocations[TABLE_CAPACITY];
    FixtureSymbol publics[TABLE_CAPACITY];
    FixtureSymbol externals[TABLE_CAPACITY];
    char strings[512];
    size_t relocation_count;
    size_t public_count;
    size_t external_count;
    size_t string_size;
    size_t relocation_start;
    size_t public_start;
    size_t external_start;
    size_t string_start;
    size_t size;
} Fixture;

static void be32(unsigned char* output, uint32_t value)
{
    output[0] = (unsigned char) (value >> 24);
    output[1] = (unsigned char) (value >> 16);
    output[2] = (unsigned char) (value >> 8);
    output[3] = (unsigned char) value;
}

static Fixture fixture_new(uint32_t data_size)
{
    Fixture fixture = { 0 };
    CHECK(data_size <= FIXTURE_CAPACITY - HEADER_SIZE);
    fixture.data_size = data_size;
    return fixture;
}

static void word(Fixture* fixture, uint32_t offset, uint32_t value)
{
    CHECK(offset <= fixture->data_size && fixture->data_size - offset >= 4);
    be32(fixture->bytes + HEADER_SIZE + offset, value);
}

static void reference(Fixture* fixture, uint32_t field, uint32_t target)
{
    CHECK(fixture->relocation_count < TABLE_CAPACITY);
    word(fixture, field, target);
    fixture->relocations[fixture->relocation_count++] = field;
}

static uint32_t symbol_name(Fixture* fixture, const char* name)
{
    size_t length = strlen(name) + 1;
    uint32_t offset = (uint32_t) fixture->string_size;
    CHECK(length <= sizeof(fixture->strings) - fixture->string_size);
    memcpy(fixture->strings + fixture->string_size, name, length);
    fixture->string_size += length;
    return offset;
}

static void public_symbol(Fixture* fixture, uint32_t offset, const char* name)
{
    CHECK(fixture->public_count < TABLE_CAPACITY);
    fixture->publics[fixture->public_count++] =
        (FixtureSymbol) { offset, symbol_name(fixture, name) };
}

static void external_symbol(Fixture* fixture, uint32_t field, const char* name)
{
    CHECK(fixture->external_count < TABLE_CAPACITY);
    fixture->externals[fixture->external_count++] =
        (FixtureSymbol) { field, symbol_name(fixture, name) };
}

/* Keep disk offsets explicit. No host descriptor layout enters the fixture. */
static void fixture_finish(Fixture* fixture)
{
    size_t cursor = HEADER_SIZE + fixture->data_size;
    fixture->relocation_start = cursor;
    for (size_t i = 0; i < fixture->relocation_count; i++, cursor += 4) {
        CHECK(cursor + 4 <= sizeof(fixture->bytes));
        be32(fixture->bytes + cursor, fixture->relocations[i]);
    }
    fixture->public_start = cursor;
    for (size_t i = 0; i < fixture->public_count; i++, cursor += 8) {
        CHECK(cursor + 8 <= sizeof(fixture->bytes));
        be32(fixture->bytes + cursor, fixture->publics[i].offset);
        be32(fixture->bytes + cursor + 4, fixture->publics[i].name);
    }
    fixture->external_start = cursor;
    for (size_t i = 0; i < fixture->external_count; i++, cursor += 8) {
        CHECK(cursor + 8 <= sizeof(fixture->bytes));
        be32(fixture->bytes + cursor, fixture->externals[i].offset);
        be32(fixture->bytes + cursor + 4, fixture->externals[i].name);
    }
    fixture->string_start = cursor;
    CHECK(fixture->string_size <= sizeof(fixture->bytes) - cursor);
    memcpy(fixture->bytes + cursor, fixture->strings, fixture->string_size);
    fixture->size = cursor + fixture->string_size;
    be32(fixture->bytes, (uint32_t) fixture->size);
    be32(fixture->bytes + 4, fixture->data_size);
    be32(fixture->bytes + 8, (uint32_t) fixture->relocation_count);
    be32(fixture->bytes + 12, (uint32_t) fixture->public_count);
    be32(fixture->bytes + 16, (uint32_t) fixture->external_count);
}

static NativeArchive* open_fixture(Fixture* fixture)
{
    NativeArchive* archive = NULL;
    NativeArchiveError error = { 0 };
    fixture_finish(fixture);
    NativeArchiveStatus status = NativeArchiveOpen(
        fixture->bytes, fixture->size, &archive, &error);
    if (status != NATIVE_ARCHIVE_OK) {
        fprintf(stderr, "Open failed at %zu: %s\n", error.offset,
                error.message ? error.message : "no error message");
    }
    CHECK(status == NATIVE_ARCHIVE_OK);
    CHECK(archive != NULL);
    return archive;
}

static NativeArchiveGraph* open_graph(NativeArchive* archive)
{
    NativeArchiveGraph* graph = NULL;
    NativeArchiveError error = { 0 };
    CHECK(NativeArchiveGraphOpen(archive, &graph, &error) == NATIVE_ARCHIVE_OK);
    CHECK(graph != NULL);
    return graph;
}

static NativeArchiveError reject_file_at(Fixture* fixture, size_t size,
                                         const char* test, int line)
{
    NativeArchive* archive = (NativeArchive*) (uintptr_t) 1;
    NativeArchiveError error = { 0 };
    NativeArchiveStatus status =
        NativeArchiveOpen(fixture->bytes, size, &archive, &error);
    if (status != NATIVE_ARCHIVE_INVALID && status != NATIVE_ARCHIVE_BOUNDS) {
        fprintf(stderr, "%s:%d: Expected malformed file, got status %d at %zu: %s\n",
                test, line, status, error.offset,
                error.message ? error.message : "no error message");
    }
    CHECK(status == NATIVE_ARCHIVE_INVALID || status == NATIVE_ARCHIVE_BOUNDS);
    CHECK(error.status == status);
    CHECK(error.message != NULL);
    CHECK(archive == NULL);
    return error;
}

#define reject_file(fixture, size) reject_file_at(fixture, size, __func__, __LINE__)

static void check_host_pointer(const Fixture* fixture, const void* pointer)
{
    uintptr_t address = (uintptr_t) pointer;
    uintptr_t input = (uintptr_t) fixture->bytes;
    CHECK(pointer != NULL);
    CHECK(address < input || address >= input + sizeof(fixture->bytes));
    CHECK(address > UINT32_MAX);
}

static void test_archive_symbols_and_copy(void)
{
    Fixture fixture = fixture_new(16);
    public_symbol(&fixture, 0, "root");
    external_symbol(&fixture, 4, "shared");
    word(&fixture, 4, UINT32_MAX);
    reference(&fixture, 8, 0);
    word(&fixture, 12, 0x3fc00000);
    NativeArchive* archive = open_fixture(&fixture);
    memset(fixture.bytes, 0xee, fixture.size);

    CHECK(NativeArchiveDataSize(archive) == 16);
    CHECK(NativeArchivePublicCount(archive) == 1);
    CHECK(NativeArchiveExternalCount(archive) == 1);
    NativeArchiveError error = { 0 };
    NativeArchiveSymbol symbol = { 0 };
    CHECK(NativeArchivePublic(archive, 0, &symbol, &error) == NATIVE_ARCHIVE_OK);
    CHECK(strcmp(symbol.name, "root") == 0);
    CHECK(symbol.offset == 0);
    CHECK(NativeArchiveExternal(archive, 0, &symbol, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(strcmp(symbol.name, "shared") == 0);
    CHECK(symbol.offset == 4);
    CHECK(NativeArchivePublic(archive, 1, &symbol, &error) != NATIVE_ARCHIVE_OK);
    CHECK(NativeArchiveExternal(archive, 1, &symbol, &error) !=
          NATIVE_ARCHIVE_OK);
    uint32_t offset = UINT32_MAX;
    CHECK(NativeArchiveFind(archive, "root", &offset, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(offset == 0);
    CHECK(NativeArchiveFind(archive, "missing", &offset, &error) ==
          NATIVE_ARCHIVE_NOT_FOUND);
    bool present = true;
    CHECK(NativeArchiveReference(archive, 0, &offset, &present, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(!present);
    CHECK(NativeArchiveReference(archive, 8, &offset, &present, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(present && offset == 0);
    CHECK(NativeArchiveReference(archive, 4, &offset, &present, &error) ==
          NATIVE_ARCHIVE_UNSUPPORTED);
    CHECK(error.offset == HEADER_SIZE + 4);
    unsigned char bytes[4] = { 0 };
    const unsigned char expected[] = { 0x3f, 0xc0, 0, 0 };
    CHECK(NativeArchiveRead(archive, 12, bytes, sizeof(bytes), &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(memcmp(bytes, expected, sizeof(bytes)) == 0);
    CHECK(NativeArchiveRead(archive, 13, bytes, sizeof(bytes), &error) ==
          NATIVE_ARCHIVE_BOUNDS);
    CHECK(error.offset == HEADER_SIZE + 13);
    NativeArchiveClose(archive);
}

static void test_joint_graph(void)
{
    Fixture fixture = fixture_new(192);
    public_symbol(&fixture, 0, "joint");
    reference(&fixture, 0, 128);
    reference(&fixture, 8, 64);
    reference(&fixture, 12, 64);
    reference(&fixture, 56, 144);
    reference(&fixture, 64 + 8, 0);
    memcpy(fixture.bytes + HEADER_SIZE + 128, "HSD_JObj", 9);
    word(&fixture, 4, JOBJ_SKELETON_ROOT | JOBJ_HIDDEN);
    word(&fixture, 20, 0x3fc00000); /* 1.5 */
    word(&fixture, 24, 0xc0000000); /* -2 */
    word(&fixture, 28, 0x3e000000); /* 0.125 */
    word(&fixture, 32, 0x3f800000); /* 1 */
    word(&fixture, 36, 0x40000000); /* 2 */
    word(&fixture, 40, 0x40400000); /* 3 */
    word(&fixture, 44, 0xc0800000); /* -4 */
    word(&fixture, 48, 0x40a00000); /* 5 */
    word(&fixture, 52, 0x80000000); /* -0 */
    for (uint32_t i = 0; i < 12; i++) {
        word(&fixture, 144 + i * 4, (i % 5 == 0) ? 0x3f800000 : 0);
    }
    word(&fixture, 144 + 11 * 4, 0xc0200000); /* -2.5 */
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph = open_graph(archive);
    NativeArchiveGraph* other_graph = open_graph(archive);
    NativeArchiveError error = { 0 };
    HSD_Joint* root = NULL;
    HSD_Joint* other = NULL;
    CHECK(NativeArchiveJoint(graph, 0, &root, &error) == NATIVE_ARCHIVE_OK);
    HSD_Joint* named = NULL;
    CHECK(NativeArchiveJointByName(graph, "joint", &named, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(named == root);
    CHECK(NativeArchiveJoint(other_graph, 0, &other, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(root != other);
    CHECK(root->child == root->next);
    CHECK(root->child->child == root);
    CHECK(root->child->next == NULL);
    CHECK(root->u.dobjdesc == NULL && root->robjdesc == NULL);
    CHECK(root->flags == (JOBJ_SKELETON_ROOT | JOBJ_HIDDEN));
    CHECK(root->rotation.x == 1.5f);
    CHECK(root->rotation.y == -2.0f);
    CHECK(root->rotation.z == 0.125f);
    CHECK(root->scale.x == 1.0f && root->scale.y == 2.0f &&
          root->scale.z == 3.0f);
    CHECK(root->position.x == -4.0f && root->position.y == 5.0f);
    uint32_t negative_zero;
    memcpy(&negative_zero, &root->position.z, sizeof(negative_zero));
    CHECK(negative_zero == 0x80000000);
    CHECK(strcmp(root->class_name, "HSD_JObj") == 0);
    CHECK(root->mtx[0][0] == 1.0f && root->mtx[1][1] == 1.0f &&
          root->mtx[2][2] == 1.0f && root->mtx[2][3] == -2.5f);
    CHECK(root->class_name != other->class_name);
    CHECK(root->mtx != other->mtx);
    check_host_pointer(&fixture, root);
    check_host_pointer(&fixture, root->child);
    check_host_pointer(&fixture, root->class_name);
    check_host_pointer(&fixture, root->mtx);
    root->class_name[0] = 'X';
    root->mtx[0][0] = 9.0f;
    CHECK(other->class_name[0] == 'H');
    CHECK(other->mtx[0][0] == 1.0f);
    HSD_Joint* repeated = NULL;
    CHECK(NativeArchiveJoint(graph, 0, &repeated, &error) == NATIVE_ARCHIVE_OK);
    CHECK(repeated == root);
    NativeArchiveGraphClose(other_graph);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_shape_animation_graph(void)
{
    Fixture fixture = fixture_new(40);
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeArchiveError error = { 0 };
    HSD_ShapeAnimJoint* root = NULL;

    /* Shape animation roots are three pointer words on disk. The child points
     * to another joint, which owns one shape animation DObj and one shape
     * animation record. Each record widens its serialized pointers on host. */
    reference(&fixture, 0, 12);
    reference(&fixture, 8, 24);
    reference(&fixture, 28, 32);
    public_symbol(&fixture, 0, "shapeanim_joint");
    archive = open_fixture(&fixture);
    graph = open_graph(archive);
    CHECK(NativeArchiveShapeAnimJoint(graph, 0, &root, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(root != NULL && root->child != NULL && root->next == NULL);
    CHECK(root->shapeanimdobj != NULL);
    CHECK(root->shapeanimdobj->shapeanim != NULL);
    CHECK(root->shapeanimdobj->shapeanim->aobjdesc == NULL);
    HSD_ShapeAnimJoint* named = NULL;
    CHECK(NativeArchiveShapeAnimJointByName(graph, "shapeanim_joint", &named,
                                            &error) == NATIVE_ARCHIVE_OK);
    CHECK(named == root);
    CHECK(root->child->child == NULL && root->child->next == NULL);
    check_host_pointer(&fixture, root);
    check_host_pointer(&fixture, root->child);
    check_host_pointer(&fixture, root->shapeanimdobj);
    check_host_pointer(&fixture, root->shapeanimdobj->shapeanim);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_joint_display_descriptor_graph(void)
{
    Fixture fixture = fixture_new(208);
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeArchiveError error = { 0 };
    HSD_Joint* root = NULL;

    /* Joint -> DObjDesc -> MObjDesc -> Material. Every pointer is a
     * relocation-backed 32-bit DAT offset. The host descriptors are wider. */
    reference(&fixture, 16, 64);
    reference(&fixture, 64, 160);
    reference(&fixture, 64 + 8, 80);
    reference(&fixture, 64 + 12, 128);
    reference(&fixture, 80, 170);
    reference(&fixture, 80 + 12, 104);
    reference(&fixture, 128, 180);
    reference(&fixture, 128 + 20, 0);
    memcpy(fixture.bytes + HEADER_SIZE + 160, "HSD_DObj", 9);
    memcpy(fixture.bytes + HEADER_SIZE + 170, "HSD_MObj", 9);
    memcpy(fixture.bytes + HEADER_SIZE + 180, "HSD_PObj", 9);
    word(&fixture, 80 + 4, RENDER_DIFFUSE | RENDER_SPECULAR);
    fixture.bytes[HEADER_SIZE + 104] = 1;
    fixture.bytes[HEADER_SIZE + 105] = 2;
    fixture.bytes[HEADER_SIZE + 106] = 3;
    fixture.bytes[HEADER_SIZE + 107] = 4;
    word(&fixture, 104 + 12, 0x3f000000);
    word(&fixture, 104 + 16, 0x40000000);

    archive = open_fixture(&fixture);
    graph = open_graph(archive);
    CHECK(NativeArchiveJoint(graph, 0, &root, &error) == NATIVE_ARCHIVE_OK);
    CHECK(root->u.dobjdesc != NULL);
    CHECK(strcmp(root->u.dobjdesc->class_name, "HSD_DObj") == 0);
    CHECK(root->u.dobjdesc->mobjdesc != NULL);
    CHECK(root->u.dobjdesc->mobjdesc->rendermode ==
          (RENDER_DIFFUSE | RENDER_SPECULAR));
    CHECK(root->u.dobjdesc->mobjdesc->mat != NULL);
    CHECK(root->u.dobjdesc->mobjdesc->mat->ambient.r == 1);
    CHECK(root->u.dobjdesc->mobjdesc->mat->ambient.g == 2);
    CHECK(root->u.dobjdesc->mobjdesc->mat->ambient.b == 3);
    CHECK(root->u.dobjdesc->mobjdesc->mat->alpha == 0.5f);
    CHECK(root->u.dobjdesc->pobjdesc != NULL);
    CHECK(strcmp(root->u.dobjdesc->pobjdesc->class_name, "HSD_PObj") == 0);
    CHECK(root->u.dobjdesc->pobjdesc->u.joint == root);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_light_descriptor_graph(void)
{
    Fixture fixture = fixture_new(220);
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeArchiveError error = { 0 };
    HSD_LightDesc* light = NULL;
    HSD_LightAnim* animation = NULL;

    /* Two light descriptors exercise point and raw attenuation unions. */
    reference(&fixture, 0, 152);
    reference(&fixture, 4, 28);
    reference(&fixture, 16, 56);
    reference(&fixture, 24, 76);
    reference(&fixture, 44, 88);
    reference(&fixture, 48, 108);
    reference(&fixture, 52, 128);
    word(&fixture, 8, ((uint32_t) LOBJ_POINT << 16));
    word(&fixture, 36, ((uint32_t) LOBJ_SPOT << 16) | LOBJ_LIGHT_ATTN);
    fixture.bytes[HEADER_SIZE + 12] = 255;
    fixture.bytes[HEADER_SIZE + 13] = 128;
    fixture.bytes[HEADER_SIZE + 14] = 64;
    fixture.bytes[HEADER_SIZE + 15] = 255;
    fixture.bytes[HEADER_SIZE + 40] = 32;
    fixture.bytes[HEADER_SIZE + 41] = 64;
    fixture.bytes[HEADER_SIZE + 42] = 96;
    fixture.bytes[HEADER_SIZE + 43] = 255;
    word(&fixture, 60, 0x3f800000);
    word(&fixture, 64, 0x40000000);
    word(&fixture, 68, GX_DA_MEDIUM);
    word(&fixture, 76, 0x3f000000);
    word(&fixture, 80, 0x3f800000);
    word(&fixture, 84, GX_DA_GENTLE);
    word(&fixture, 92, 0x3f800000);
    word(&fixture, 96, 0x40000000);
    word(&fixture, 100, 0x40400000);
    word(&fixture, 112, 0x3f800000);
    word(&fixture, 116, 0x40000000);
    word(&fixture, 120, 0x40400000);
    word(&fixture, 124, 0);
    word(&fixture, 128, 0x3f000000);
    word(&fixture, 132, 0x3f800000);
    word(&fixture, 136, 0x40000000);
    word(&fixture, 140, 0x40400000);
    word(&fixture, 144, 0x40800000);
    word(&fixture, 148, 0x40a00000);
    memcpy(fixture.bytes + HEADER_SIZE + 152, "HSD_LObj", 9);

    reference(&fixture, 168, 180);
    reference(&fixture, 172, 196);
    public_symbol(&fixture, 0, "light");
    public_symbol(&fixture, 164, "light_anim");
    archive = open_fixture(&fixture);
    graph = open_graph(archive);
    CHECK(NativeArchiveLight(graph, 0, &light, &error) == NATIVE_ARCHIVE_OK);
    CHECK(light != NULL && light->next != NULL);
    CHECK(light->flags == LOBJ_POINT);
    CHECK(light->color.g == 128);
    CHECK(light->position != NULL && light->position->pos.x == 1.0f);
    CHECK(light->u.point->ref_br == 0.5f);
    CHECK(light->u.point->ref_dist == 1.0f);
    CHECK(light->u.point->dist_func == GX_DA_GENTLE);
    CHECK(light->next->flags == LOBJ_SPOT);
    CHECK(light->next->u.attn->a0 == 0.5f);
    CHECK(light->next->u.attn->k2 == 5.0f);
    CHECK(NativeArchiveLightByName(graph, "light", &light, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(NativeArchiveLightAnimationByName(graph, "light_anim", &animation,
                                            &error) == NATIVE_ARCHIVE_OK);
    CHECK(animation != NULL && animation->aobjdesc != NULL);
    CHECK(animation->position_anim != NULL);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_animation_graph(void)
{
    Fixture fixture = fixture_new(128);
    const unsigned char stream[] = { 0x01, 0x00, 0x00, 0x20, 0x40, 0x01 };
    reference(&fixture, 0, 20);
    reference(&fixture, 4, 20);
    reference(&fixture, 8, 40);
    word(&fixture, 16, 0x10203040);
    reference(&fixture, 20, 0);
    reference(&fixture, 28, 40);
    word(&fixture, 40, AOBJ_LOOP);
    word(&fixture, 44, 0x42f10000); /* 120.5 */
    reference(&fixture, 48, 56);
    reference(&fixture, 56, 76);
    word(&fixture, 60, sizeof(stream));
    word(&fixture, 64, 0xc0600000); /* -3.5 */
    fixture.bytes[HEADER_SIZE + 68] = HSD_A_J_TRAX;
    fixture.bytes[HEADER_SIZE + 69] = HSD_A_FRAC_FLOAT;
    fixture.bytes[HEADER_SIZE + 70] = HSD_A_FRAC_FLOAT;
    fixture.bytes[HEADER_SIZE + 71] = 0x5a;
    reference(&fixture, 72, 96);
    reference(&fixture, 76, 56);
    word(&fixture, 80, sizeof(stream));
    reference(&fixture, 92, 96);
    memcpy(fixture.bytes + HEADER_SIZE + 96, stream, sizeof(stream));
    public_symbol(&fixture, 0, "animation");
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph = open_graph(archive);
    NativeArchiveGraph* other_graph = open_graph(archive);
    NativeArchiveError error = { 0 };
    HSD_AnimJoint* root = NULL;
    HSD_AnimJoint* other = NULL;
    CHECK(NativeArchiveAnimation(graph, 0, &root, &error) == NATIVE_ARCHIVE_OK);
    CHECK(NativeArchiveAnimation(other_graph, 0, &other, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(root->child == root->next);
    CHECK(root->child->child == root);
    CHECK(root->child->aobjdesc == root->aobjdesc);
    CHECK(root->robj_anim == NULL);
    CHECK(root->flags == 0x10203040);
    HSD_AObjDesc* aobj = root->aobjdesc;
    CHECK(aobj->flags == AOBJ_LOOP);
    CHECK(aobj->end_frame == 120.5f);
    CHECK(aobj->obj_id == 0);
    HSD_FObjDesc* fobj = aobj->fobjdesc;
    CHECK(fobj->next->next == fobj);
    CHECK(fobj->length == sizeof(stream));
    CHECK(fobj->startframe == -3.5f);
    CHECK(fobj->type == HSD_A_J_TRAX);
    CHECK(fobj->frac_value == HSD_A_FRAC_FLOAT);
    CHECK(fobj->frac_slope == HSD_A_FRAC_FLOAT);
    CHECK(fobj->dummy0 == 0x5a);
    CHECK(memcmp(fobj->ad, stream, sizeof(stream)) == 0);
    CHECK(memcmp(fobj->next->ad, stream, sizeof(stream)) == 0);
    CHECK(fobj->ad != other->aobjdesc->fobjdesc->ad);
    check_host_pointer(&fixture, root);
    check_host_pointer(&fixture, aobj);
    check_host_pointer(&fixture, fobj);
    check_host_pointer(&fixture, fobj->ad);
    fobj->ad[0] = 0;
    CHECK(other->aobjdesc->fobjdesc->ad[0] == stream[0]);
    HSD_AObjDesc* direct = NULL;
    CHECK(NativeArchiveAObj(graph, 40, &direct, &error) == NATIVE_ARCHIVE_OK);
    CHECK(direct == aobj);
    NativeArchiveGraphClose(other_graph);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_texture_animation_graph(void)
{
    Fixture fixture = fixture_new(144);
    NativeArchive* archive;
    NativeArchiveGraph* graph;
    NativeArchiveError error = { 0 };
    HSD_MatAnimJoint* root = NULL;

    /* mat joint 0, material animation 12, texture animation 28, tables 52/60,
     * image descriptor 68, TLUT descriptor 92, image bytes 108, TLUT bytes 124. */
    reference(&fixture, 8, 12);
    reference(&fixture, 12, 12);
    reference(&fixture, 20, 28);
    reference(&fixture, 28, 28);
    reference(&fixture, 40, 52);
    reference(&fixture, 44, 60);
    word(&fixture, 32, GX_TEXMAP1);
    word(&fixture, 48, 1 | (1u << 16));
    reference(&fixture, 52, 68);
    reference(&fixture, 60, 92);
    reference(&fixture, 68, 108);
    word(&fixture, 72, 0x00040004);
    word(&fixture, 76, GX_TF_RGBA8);
    word(&fixture, 80, 0);
    word(&fixture, 84, 0x3f800000);
    word(&fixture, 88, 0x3f800000);
    reference(&fixture, 92, 124);
    word(&fixture, 96, GX_TL_RGB565);
    word(&fixture, 100, 7);
    word(&fixture, 104, 0x00020000);
    fixture.bytes[HEADER_SIZE + 108] = 1;
    fixture.bytes[HEADER_SIZE + 109] = 2;
    fixture.bytes[HEADER_SIZE + 124] = 3;
    fixture.bytes[HEADER_SIZE + 125] = 4;

    archive = open_fixture(&fixture);
    graph = open_graph(archive);
    CHECK(NativeArchiveMatAnimJoint(graph, 0, &root, &error) == NATIVE_ARCHIVE_OK);
    CHECK(root != NULL && root->matanim != NULL);
    CHECK(root->matanim->texanim != NULL);
    CHECK(root->matanim->texanim->id == GX_TEXMAP1);
    CHECK(root->matanim->texanim->next == root->matanim->texanim);
    CHECK(root->matanim->texanim->n_imagetbl == 1);
    CHECK(root->matanim->texanim->n_tluttbl == 1);
    CHECK(root->matanim->texanim->imagetbl[0] != NULL);
    CHECK(root->matanim->texanim->imagetbl[0]->width == 4);
    CHECK(((uint8_t*) root->matanim->texanim->imagetbl[0]->image_ptr)[0] == 1);
    CHECK(root->matanim->texanim->tluttbl[0] != NULL);
    CHECK(root->matanim->texanim->tluttbl[0]->n_entries == 2);
    CHECK(((uint8_t*) root->matanim->texanim->tluttbl[0]->lut)[0] == 3);
    CHECK(root->matanim->texanim->imagetbl[1] == NULL);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);

    /* A non-null render animation has no native descriptor consumer yet. */
    fixture = fixture_new(32);
    reference(&fixture, 8, 12);
    reference(&fixture, 12, 12);
    reference(&fixture, 24, 20);
    archive = open_fixture(&fixture);
    graph = open_graph(archive);
    root = (HSD_MatAnimJoint*) (uintptr_t) 1;
    CHECK(NativeArchiveMatAnimJoint(graph, 0, &root, &error) ==
          NATIVE_ARCHIVE_UNSUPPORTED);
    CHECK(root == NULL);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_wobj(void)
{
    Fixture fixture = fixture_new(32);
    reference(&fixture, 0, 20);
    memcpy(fixture.bytes + HEADER_SIZE + 20, "HSD_WObj", 9);
    word(&fixture, 4, 0x3f000000); /* 0.5 */
    word(&fixture, 8, 0xc1200000); /* -10 */
    word(&fixture, 12, 0x42000000); /* 32 */
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph = open_graph(archive);
    NativeArchiveError error = { 0 };
    HSD_WObjDesc* root = NULL;
    CHECK(NativeArchiveWObj(graph, 0, &root, &error) == NATIVE_ARCHIVE_OK);
    CHECK(strcmp(root->class_name, "HSD_WObj") == 0);
    CHECK(root->pos.x == 0.5f && root->pos.y == -10.0f && root->pos.z == 32.0f);
    CHECK(root->robjdesc == NULL);
    check_host_pointer(&fixture, root);
    check_host_pointer(&fixture, root->class_name);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_cobj(void)
{
    Fixture fixture = fixture_new(128);
    /* Camera at 0, WObj at 64, up vector at 84, strings at 96 and 105. */
    public_symbol(&fixture, 0, "camera");
    reference(&fixture, 0, 96);
    reference(&fixture, 24, 64);
    reference(&fixture, 28, 64);
    reference(&fixture, 36, 84);
    reference(&fixture, 64, 105);
    word(&fixture, 4, (uint32_t) ((0x0001u << 16) | PROJ_PERSPECTIVE));
    fixture.bytes[HEADER_SIZE + 8] = 0xff;
    fixture.bytes[HEADER_SIZE + 9] = 0xf6; /* viewport xmin -10 */
    fixture.bytes[HEADER_SIZE + 10] = 0x02;
    fixture.bytes[HEADER_SIZE + 11] = 0x80; /* viewport xmax 640 */
    fixture.bytes[HEADER_SIZE + 12] = 0;
    fixture.bytes[HEADER_SIZE + 13] = 0; /* viewport ymin 0 */
    fixture.bytes[HEADER_SIZE + 14] = 0x01;
    fixture.bytes[HEADER_SIZE + 15] = 0xe0; /* viewport ymax 480 */
    fixture.bytes[HEADER_SIZE + 16] = 0;
    fixture.bytes[HEADER_SIZE + 17] = 10;
    fixture.bytes[HEADER_SIZE + 18] = 0x02;
    fixture.bytes[HEADER_SIZE + 19] = 0x86;
    fixture.bytes[HEADER_SIZE + 20] = 0;
    fixture.bytes[HEADER_SIZE + 21] = 20;
    fixture.bytes[HEADER_SIZE + 22] = 0x01;
    fixture.bytes[HEADER_SIZE + 23] = 0xf4;
    word(&fixture, 32, 0x3f000000); /* roll 0.5 */
    word(&fixture, 40, 0x3dcccccd); /* near 0.1 */
    word(&fixture, 44, 0x447a0000); /* far 1000 */
    word(&fixture, 48, 0x40490fdb); /* fov pi */
    word(&fixture, 52, 0x3faaaaab); /* aspect 1.333333 */
    word(&fixture, 64 + 4, 0x3f800000);
    word(&fixture, 64 + 8, 0x40000000);
    word(&fixture, 64 + 12, 0x40400000);
    word(&fixture, 84, 0x40a00000);
    word(&fixture, 88, 0x40c00000);
    word(&fixture, 92, 0x40e00000);
    memcpy(fixture.bytes + HEADER_SIZE + 96, "HSD_CObj", 9);
    memcpy(fixture.bytes + HEADER_SIZE + 105, "HSD_WObj", 9);

    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph = open_graph(archive);
    NativeArchiveError error = { 0 };
    HSD_CObjDesc* root = NULL;
    CHECK(NativeArchiveCObj(graph, 0, &root, &error) == NATIVE_ARCHIVE_OK);
    CHECK(root != NULL);
    CHECK(NativeArchiveCObjByName(graph, "camera", &root, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(strcmp(root->common.class_name, "HSD_CObj") == 0);
    CHECK(root->common.flags == 1);
    CHECK(root->common.projection_type == PROJ_PERSPECTIVE);
    CHECK(root->common.viewport.xmin == -10 &&
          root->common.viewport.xmax == 640 && root->common.viewport.ymin == 0 &&
          root->common.viewport.ymax == 480);
    CHECK(root->common.scissor.left == 10 && root->common.scissor.right == 646 &&
          root->common.scissor.top == 20 && root->common.scissor.bottom == 500);
    CHECK(root->common.eyepos == root->common.interest);
    CHECK(root->common.eyepos->pos.x == 1.0f &&
          root->common.eyepos->pos.y == 2.0f &&
          root->common.eyepos->pos.z == 3.0f);
    CHECK(root->common.up_vector->x == 5.0f &&
          root->common.up_vector->y == 6.0f &&
          root->common.up_vector->z == 7.0f);
    CHECK(root->perspective.fov > 3.14f && root->perspective.aspect > 1.33f);
    check_host_pointer(&fixture, root);
    check_host_pointer(&fixture, root->common.class_name);
    check_host_pointer(&fixture, root->common.eyepos);
    check_host_pointer(&fixture, root->common.up_vector);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);

    /* Unknown projection tags must fail before returning a partial descriptor. */
    fixture.bytes[HEADER_SIZE + 7] = 7;
    archive = open_fixture(&fixture);
    graph = open_graph(archive);
    root = (HSD_CObjDesc*) (uintptr_t) 1;
    CHECK(NativeArchiveCObj(graph, 0, &root, &error) == NATIVE_ARCHIVE_INVALID);
    CHECK(root == NULL && error.offset == HEADER_SIZE + 6);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_truncation_and_counts(void)
{
    Fixture fixture = fixture_new(0);
    fixture_finish(&fixture);
    for (size_t size = 0; size < HEADER_SIZE; size++) {
        reject_file(&fixture, size);
    }
    fixture = fixture_new(16);
    fixture_finish(&fixture);
    be32(fixture.bytes, HEADER_SIZE + 15);
    reject_file(&fixture, HEADER_SIZE + 15);
    for (size_t field = 8; field <= 16; field += 4) {
        fixture = fixture_new(16);
        fixture_finish(&fixture);
        be32(fixture.bytes + field, 1);
        reject_file(&fixture, fixture.size);
        be32(fixture.bytes + field, UINT32_MAX);
        reject_file(&fixture, fixture.size);
    }
    fixture = fixture_new(16);
    fixture_finish(&fixture);
    be32(fixture.bytes + 4, UINT32_MAX);
    reject_file(&fixture, fixture.size);
    fixture = fixture_new(16);
    fixture_finish(&fixture);
    reject_file(&fixture, fixture.size - 1);
    reject_file(&fixture, fixture.size + 1);
    be32(fixture.bytes, UINT32_MAX);
    reject_file(&fixture, fixture.size);
}

static void test_bad_relocations(void)
{
    const uint32_t invalid_fields[] = { 1, 13, 16, UINT32_MAX };
    for (size_t i = 0; i < sizeof(invalid_fields) / sizeof(*invalid_fields); i++) {
        Fixture fixture = fixture_new(16);
        fixture.relocations[fixture.relocation_count++] = invalid_fields[i];
        fixture_finish(&fixture);
        reject_file(&fixture, fixture.size);
    }
    Fixture fixture = fixture_new(16);
    reference(&fixture, 4, 17);
    fixture_finish(&fixture);
    NativeArchiveError error = reject_file(&fixture, fixture.size);
    CHECK(error.offset == HEADER_SIZE + 4);
    fixture = fixture_new(16);
    reference(&fixture, 4, UINT32_MAX);
    fixture_finish(&fixture);
    error = reject_file(&fixture, fixture.size);
    CHECK(error.offset == HEADER_SIZE + 4);
    fixture = fixture_new(16);
    reference(&fixture, 4, 0);
    reference(&fixture, 4, 0);
    fixture_finish(&fixture);
    reject_file(&fixture, fixture.size);
}

static void test_bad_symbols(void)
{
    Fixture fixture = fixture_new(16);
    public_symbol(&fixture, 17, "root");
    fixture_finish(&fixture);
    reject_file(&fixture, fixture.size);
    for (int external = 0; external <= 1; external++) {
        fixture = fixture_new(16);
        if (external) {
            external_symbol(&fixture, 0, "name");
            word(&fixture, 0, UINT32_MAX);
        } else {
            public_symbol(&fixture, 0, "name");
        }
        fixture_finish(&fixture);
        size_t table = external ? fixture.external_start : fixture.public_start;
        be32(fixture.bytes + table + 4, (uint32_t) fixture.string_size);
        reject_file(&fixture, fixture.size);
        be32(fixture.bytes + table + 4, UINT32_MAX);
        reject_file(&fixture, fixture.size);
        be32(fixture.bytes + table + 4, 0);
        fixture.bytes[fixture.size - 1] = 'x';
        reject_file(&fixture, fixture.size);
    }
}

static void test_external_chains(void)
{
    Fixture fixture = fixture_new(16);
    external_symbol(&fixture, 0, "external");
    word(&fixture, 0, 4);
    word(&fixture, 4, UINT32_MAX);
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveError error = { 0 };
    uint32_t target = 0;
    bool present = false;
    CHECK(NativeArchiveReference(archive, 0, &target, &present, &error) ==
          NATIVE_ARCHIVE_UNSUPPORTED);
    CHECK(NativeArchiveReference(archive, 4, &target, &present, &error) ==
          NATIVE_ARCHIVE_UNSUPPORTED);
    CHECK(error.offset == HEADER_SIZE + 4);
    NativeArchiveClose(archive);
    word(&fixture, 4, 0);
    fixture_finish(&fixture);
    reject_file(&fixture, fixture.size);
    word(&fixture, 0, 0);
    reject_file(&fixture, fixture.size);
    word(&fixture, 0, 16);
    reject_file(&fixture, fixture.size);
    word(&fixture, 0, 1);
    reject_file(&fixture, fixture.size);
    word(&fixture, 0, UINT32_MAX);
    reference(&fixture, 0, 4);
    fixture_finish(&fixture);
    reject_file(&fixture, fixture.size);
    fixture = fixture_new(16);
    external_symbol(&fixture, 16, "external");
    fixture_finish(&fixture);
    reject_file(&fixture, fixture.size);
}

static Fixture shape_fixture(u16 mode)
{
    Fixture fixture = fixture_new(252);
    reference(&fixture, 16, 64);
    reference(&fixture, 64 + 12, 80);
    word(&fixture, 80 + 12, POBJ_SHAPEANIM << 16);
    reference(&fixture, 80 + 20, 104);
    word(&fixture, 104, (u32) mode << 16 | 2);
    word(&fixture, 108, 1);
    reference(&fixture, 112, 132);
    reference(&fixture, 116, 180);
    word(&fixture, 132, GX_VA_POS);
    word(&fixture, 136, GX_INDEX16);
    word(&fixture, 140, GX_POS_XYZ);
    word(&fixture, 144, GX_F32);
    word(&fixture, 148, 12);
    reference(&fixture, 152, 216);
    word(&fixture, 156, GX_VA_NULL);
    reference(&fixture, 180, 192);
    reference(&fixture, 184, 200);
    if (mode == SHAPESET_ADDITIVE) reference(&fixture, 188, 208);
    word(&fixture, 200, 1u << 16);
    word(&fixture, 208, 2u << 16);
    word(&fixture, 216, 0x3fc00000); /* 1.5 */
    word(&fixture, 220, 0xc0000000); /* -2 */
    word(&fixture, 224, 0x40400000); /* 3 */
    return fixture;
}

static void test_shape_sets(void)
{
    const u16 modes[] = { SHAPESET_AVERAGE, SHAPESET_ADDITIVE };
    for (size_t i = 0; i < sizeof(modes) / sizeof(*modes); ++i) {
        Fixture fixture = shape_fixture(modes[i]);
        NativeArchive* archive = open_fixture(&fixture);
        NativeArchiveGraph* graph = open_graph(archive);
        NativeArchiveError error = { 0 };
        HSD_Joint* joint = NULL;
        CHECK(NativeArchiveJoint(graph, 0, &joint, &error) == NATIVE_ARCHIVE_OK);
        HSD_ShapeSetDesc* shape = joint->u.dobjdesc->pobjdesc->u.shape_set;
        CHECK(shape != NULL && shape->flags == modes[i] && shape->nb_shape == 2);
        CHECK(shape->nb_vertex_index == 1 && shape->nb_normal_index == 0);
        CHECK(shape->normal_desc == NULL && shape->normal_idx_list == NULL);
        CHECK(shape->vertex_desc->stride == 12);
        CHECK(shape->vertex_desc->comp_type == GX_F32);
        CHECK(shape->vertex_idx_list[0][1] == 0);
        CHECK(shape->vertex_idx_list[1][1] == 1);
        if (modes[i] == SHAPESET_ADDITIVE) {
            CHECK(shape->vertex_idx_list[2][1] == 2);
        }
        const unsigned char* vertex = shape->vertex_desc->vertex;
        CHECK(vertex[0] == 0x3f && vertex[1] == 0xc0);
        check_host_pointer(&fixture, shape);
        check_host_pointer(&fixture, shape->vertex_desc);
        check_host_pointer(&fixture, shape->vertex_idx_list);
        check_host_pointer(&fixture, shape->vertex_idx_list[0]);
        NativeArchiveGraphClose(graph);
        NativeArchiveClose(archive);
    }
    Fixture fixture = shape_fixture(SHAPESET_AVERAGE);
    word(&fixture, 200, 3u << 16);
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph = open_graph(archive);
    NativeArchiveError error = { 0 };
    HSD_Joint* joint = NULL;
    CHECK(NativeArchiveJoint(graph, 0, &joint, &error) == NATIVE_ARCHIVE_BOUNDS);
    CHECK(joint == NULL && error.offset == HEADER_SIZE + 200);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_envelope_graph(void)
{
    Fixture fixture = fixture_new(224);
    reference(&fixture, 8, 64);
    reference(&fixture, 16, 128);
    reference(&fixture, 128 + 12, 144);
    word(&fixture, 144 + 12, POBJ_ENVELOPE << 16);
    reference(&fixture, 144 + 20, 168);
    reference(&fixture, 168, 180);
    reference(&fixture, 172, 180);
    reference(&fixture, 180, 0);
    word(&fixture, 184, 0x3e800000); /* 0.25 */
    reference(&fixture, 188, 64);
    word(&fixture, 192, 0x3f400000); /* 0.75 */
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph = open_graph(archive);
    NativeArchiveError error = { 0 };
    HSD_Joint* joint = NULL;
    CHECK(NativeArchiveJoint(graph, 0, &joint, &error) == NATIVE_ARCHIVE_OK);
    HSD_EnvelopeDesc** table = joint->u.dobjdesc->pobjdesc->u.envelope_p;
    CHECK(table != NULL && table[0] == table[1] && table[2] == NULL);
    CHECK(table[0][0].joint == joint && table[0][0].weight == 0.25f);
    CHECK(table[0][1].joint == joint->child && table[0][1].weight == 0.75f);
    CHECK(table[0][2].joint == NULL);
    check_host_pointer(&fixture, table);
    check_host_pointer(&fixture, table[0]);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);

    /* An envelope list at the end of the data must include its terminator. */
    fixture = fixture_new(180);
    reference(&fixture, 16, 64);
    reference(&fixture, 64 + 12, 80);
    word(&fixture, 80 + 12, POBJ_ENVELOPE << 16);
    reference(&fixture, 80 + 20, 104);
    reference(&fixture, 104, 172);
    reference(&fixture, 172, 0);
    word(&fixture, 176, 0x3f800000);
    archive = open_fixture(&fixture);
    graph = open_graph(archive);
    CHECK(NativeArchiveJoint(graph, 0, &joint, &error) == NATIVE_ARCHIVE_BOUNDS);
    CHECK(joint == NULL && error.offset == HEADER_SIZE + 172);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_joint_constraints(void)
{
    Fixture fixture = fixture_new(196);
    reference(&fixture, 60, 64);
    reference(&fixture, 64, 76);
    word(&fixture, 68, REFTYPE_JOBJ | 1);
    reference(&fixture, 72, 0);
    reference(&fixture, 76, 88);
    word(&fixture, 80, REFTYPE_LIMIT | 2);
    word(&fixture, 84, 0x42b40000); /* 90 degrees */
    word(&fixture, 92, REFTYPE_IKHINT);
    reference(&fixture, 96, 100);
    word(&fixture, 100, 0x3fc00000); /* 1.5 */
    word(&fixture, 104, 0xbf800000); /* -1 */
    reference(&fixture, 108 + 16, 64);
    public_symbol(&fixture, 108, "wobj");
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph = open_graph(archive);
    NativeArchiveError error = { 0 };
    HSD_Joint* joint = NULL;
    HSD_WObjDesc* wobj = NULL;
    CHECK(NativeArchiveJoint(graph, 0, &joint, &error) == NATIVE_ARCHIVE_OK);
    CHECK(joint->robjdesc->u.joint == joint);
    CHECK(joint->robjdesc->next->u.limit == 90.0f);
    CHECK(joint->robjdesc->next->next->u.ik_hint->bone_length == 1.5f);
    CHECK(joint->robjdesc->next->next->u.ik_hint->rotate_x == -1.0f);
    CHECK(NativeArchiveWObjByName(graph, "wobj", &wobj, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(wobj->robjdesc == joint->robjdesc);
    check_host_pointer(&fixture, joint->robjdesc);
    check_host_pointer(&fixture, joint->robjdesc->next->next->u.ik_hint);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_unsupported_joint_fields(void)
{
    const uint32_t fields[] = { 60 };
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++) {
        Fixture fixture = fixture_new(128);
        reference(&fixture, fields[i], 64);
        NativeArchive* archive = open_fixture(&fixture);
        NativeArchiveGraph* graph = open_graph(archive);
        NativeArchiveError error = { 0 };
        HSD_Joint* root = (HSD_Joint*) (uintptr_t) 1;
        CHECK(NativeArchiveJoint(graph, 0, &root, &error) ==
              NATIVE_ARCHIVE_UNSUPPORTED);
        CHECK(root == NULL);
        CHECK(error.offset == HEADER_SIZE + 64 + 8);
        NativeArchiveError first = error;
        root = (HSD_Joint*) (uintptr_t) 1;
        CHECK(NativeArchiveJoint(graph, 64, &root, &error) == first.status);
        CHECK(root == NULL);
        CHECK(error.status == first.status && error.offset == first.offset);
        NativeArchiveGraphClose(graph);
        NativeArchiveClose(archive);
    }
}

static void test_unsupported_animation_and_wobj(void)
{
    Fixture fixture = fixture_new(40);
    reference(&fixture, 12, 20);
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph = open_graph(archive);
    NativeArchiveError error = { 0 };
    HSD_AnimJoint* animation = NULL;
    CHECK(NativeArchiveAnimation(graph, 0, &animation, &error) == NATIVE_ARCHIVE_OK);
    CHECK(animation != NULL && animation->robj_anim != NULL);
    CHECK(animation->robj_anim->next == NULL);
    CHECK(animation->robj_anim->aobjdesc == NULL);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
    fixture = fixture_new(40);
    reference(&fixture, 16, 20);
    archive = open_fixture(&fixture);
    graph = open_graph(archive);
    HSD_WObjDesc* wobj = (HSD_WObjDesc*) (uintptr_t) 1;
    CHECK(NativeArchiveWObj(graph, 0, &wobj, &error) ==
          NATIVE_ARCHIVE_UNSUPPORTED);
    CHECK(wobj == NULL);
    CHECK(error.offset == HEADER_SIZE + 20 + 8);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_external_graph_reference(void)
{
    Fixture fixture = fixture_new(64);
    external_symbol(&fixture, 0, "class");
    word(&fixture, 0, UINT32_MAX);
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph = open_graph(archive);
    NativeArchiveError error = { 0 };
    HSD_Joint* root = (HSD_Joint*) (uintptr_t) 1;
    CHECK(NativeArchiveJoint(graph, 0, &root, &error) ==
          NATIVE_ARCHIVE_UNSUPPORTED);
    CHECK(root == NULL);
    CHECK(error.offset == HEADER_SIZE);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_type_conflicts(void)
{
    Fixture fixture = fixture_new(64);
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph = open_graph(archive);
    NativeArchiveError error = { 0 };
    HSD_Joint* joint = NULL;
    CHECK(NativeArchiveJoint(graph, 0, &joint, &error) == NATIVE_ARCHIVE_OK);
    HSD_AnimJoint* animation = (HSD_AnimJoint*) (uintptr_t) 1;
    CHECK(NativeArchiveAnimation(graph, 0, &animation, &error) ==
          NATIVE_ARCHIVE_TYPE_CONFLICT);
    CHECK(animation == NULL);
    CHECK(error.offset == HEADER_SIZE);
    joint = (HSD_Joint*) (uintptr_t) 1;
    CHECK(NativeArchiveJoint(graph, 0, &joint, &error) ==
          NATIVE_ARCHIVE_TYPE_CONFLICT);
    CHECK(joint == NULL);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_descriptor_bounds_and_strings(void)
{
    Fixture fixture = fixture_new(63);
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph = open_graph(archive);
    NativeArchiveError error = { 0 };
    HSD_Joint* root = (HSD_Joint*) (uintptr_t) 1;
    CHECK(NativeArchiveJoint(graph, 0, &root, &error) == NATIVE_ARCHIVE_BOUNDS);
    CHECK(root == NULL);
    CHECK(error.offset == HEADER_SIZE);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
    fixture = fixture_new(68);
    reference(&fixture, 0, 64);
    memset(fixture.bytes + HEADER_SIZE + 64, 'x', 4);
    archive = open_fixture(&fixture);
    graph = open_graph(archive);
    root = (HSD_Joint*) (uintptr_t) 1;
    CHECK(NativeArchiveJoint(graph, 0, &root, &error) != NATIVE_ARCHIVE_OK);
    CHECK(root == NULL);
    CHECK(error.offset == HEADER_SIZE + 64);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
    fixture = fixture_new(68);
    reference(&fixture, 56, 64);
    archive = open_fixture(&fixture);
    graph = open_graph(archive);
    root = (HSD_Joint*) (uintptr_t) 1;
    CHECK(NativeArchiveJoint(graph, 0, &root, &error) == NATIVE_ARCHIVE_BOUNDS);
    CHECK(root == NULL);
    CHECK(error.offset == HEADER_SIZE + 64);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_missing_relocation(void)
{
    Fixture fixture = fixture_new(128);
    word(&fixture, 8, 64);
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveError error = { 0 };
    uint32_t target = 0;
    bool present = false;
    CHECK(NativeArchiveReference(archive, 8, &target, &present, &error) ==
          NATIVE_ARCHIVE_INVALID);
    CHECK(error.offset == HEADER_SIZE + 8);
    NativeArchiveGraph* graph = open_graph(archive);
    HSD_Joint* root = (HSD_Joint*) (uintptr_t) 1;
    CHECK(NativeArchiveJoint(graph, 0, &root, &error) == NATIVE_ARCHIVE_INVALID);
    CHECK(root == NULL);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_stream_bounds(void)
{
    Fixture fixture = fixture_new(40);
    reference(&fixture, 8, 16);
    word(&fixture, 20, 8);
    reference(&fixture, 32, 36);
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph = open_graph(archive);
    NativeArchiveError error = { 0 };
    HSD_AObjDesc* root = (HSD_AObjDesc*) (uintptr_t) 1;
    CHECK(NativeArchiveAObj(graph, 0, &root, &error) == NATIVE_ARCHIVE_BOUNDS);
    CHECK(root == NULL);
    CHECK(error.offset == HEADER_SIZE + 36);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_end_pointer(void)
{
    Fixture fixture = fixture_new(16);
    public_symbol(&fixture, 16, "end");
    reference(&fixture, 0, 16);
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveError error = { 0 };
    uint32_t offset = 0;
    bool present = false;
    CHECK(NativeArchiveFind(archive, "end", &offset, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(offset == 16);
    CHECK(NativeArchiveReference(archive, 0, &offset, &present, &error) ==
          NATIVE_ARCHIVE_OK);
    CHECK(present && offset == 16);
    CHECK(NativeArchiveRead(archive, offset, NULL, 0, &error) ==
          NATIVE_ARCHIVE_OK);
    unsigned char byte = 0;
    CHECK(NativeArchiveRead(archive, offset, &byte, 1, &error) ==
          NATIVE_ARCHIVE_BOUNDS);
    NativeArchiveGraph* graph = open_graph(archive);
    HSD_Joint* root = (HSD_Joint*) (uintptr_t) 1;
    CHECK(NativeArchiveJoint(graph, offset, &root, &error) ==
          NATIVE_ARCHIVE_BOUNDS);
    CHECK(root == NULL);
    CHECK(error.offset == HEADER_SIZE + 16);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_aobj_object_id(void)
{
    Fixture fixture = fixture_new(80);
    reference(&fixture, 12, 16);
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph = open_graph(archive);
    NativeArchiveError error = { 0 };
    HSD_AObjDesc* root = (HSD_AObjDesc*) (uintptr_t) 1;
    NativeArchiveStatus status = NativeArchiveAObj(graph, 0, &root, &error);
    if (sizeof(root->obj_id) < sizeof(uintptr_t)) {
        CHECK(status == NATIVE_ARCHIVE_UNSUPPORTED);
        CHECK(root == NULL);
        CHECK(error.offset == HEADER_SIZE + 12);
    } else {
        CHECK(status == NATIVE_ARCHIVE_OK);
        CHECK(root != NULL);
        HSD_Joint* joint = NULL;
        CHECK(NativeArchiveJoint(graph, 16, &joint, &error) == NATIVE_ARCHIVE_OK);
        CHECK(root->obj_id == (uintptr_t) joint);
        check_host_pointer(&fixture, joint);
    }
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

static void test_stream_validation(void)
{
    static const struct {
        unsigned char bytes[8];
        size_t size;
        unsigned char value_format;
        NativeArchiveStatus status;
    } invalid[] = {
        { { 0 }, 1, HSD_A_FRAC_FLOAT, NATIVE_ARCHIVE_INVALID },
        { { 0x01, 0, 0, 0 }, 4, HSD_A_FRAC_FLOAT, NATIVE_ARCHIVE_INVALID },
        { { 0x81 }, 1, HSD_A_FRAC_FLOAT, NATIVE_ARCHIVE_INVALID },
        { { 0x01, 0, 0, 0, 0, 0x80 }, 6, HSD_A_FRAC_FLOAT,
          NATIVE_ARCHIVE_INVALID },
        { { 0x81, 0xff, 0x7f }, 3, HSD_A_FRAC_FLOAT,
          NATIVE_ARCHIVE_INVALID },
        { { 0x01, 0, 0, 0, 0, 1 }, 6, 0xe0, NATIVE_ARCHIVE_UNSUPPORTED },
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        Fixture fixture = fixture_new(48);
        reference(&fixture, 8, 16);
        word(&fixture, 20, (uint32_t) invalid[i].size);
        fixture.bytes[HEADER_SIZE + 29] = invalid[i].value_format;
        reference(&fixture, 32, 36);
        memcpy(fixture.bytes + HEADER_SIZE + 36, invalid[i].bytes,
               invalid[i].size);
        NativeArchive* archive = open_fixture(&fixture);
        NativeArchiveGraph* graph = open_graph(archive);
        NativeArchiveError error = { 0 };
        HSD_AObjDesc* root = (HSD_AObjDesc*) (uintptr_t) 1;
        CHECK(NativeArchiveAObj(graph, 0, &root, &error) == invalid[i].status);
        CHECK(root == NULL);
        CHECK(error.offset >= HEADER_SIZE + 36);
        CHECK(error.offset <= HEADER_SIZE + 36 + invalid[i].size);
        NativeArchiveGraphClose(graph);
        NativeArchiveClose(archive);
    }
    Fixture fixture = fixture_new(36);
    reference(&fixture, 8, 16);
    NativeArchive* archive = open_fixture(&fixture);
    NativeArchiveGraph* graph = open_graph(archive);
    NativeArchiveError error = { 0 };
    HSD_AObjDesc* root = NULL;
    CHECK(NativeArchiveAObj(graph, 0, &root, &error) == NATIVE_ARCHIVE_OK);
    CHECK(root->fobjdesc->length == 0 && root->fobjdesc->ad == NULL);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
    word(&fixture, 20, 1);
    archive = open_fixture(&fixture);
    graph = open_graph(archive);
    root = (HSD_AObjDesc*) (uintptr_t) 1;
    CHECK(NativeArchiveAObj(graph, 0, &root, &error) == NATIVE_ARCHIVE_INVALID);
    CHECK(root == NULL);
    CHECK(error.offset == HEADER_SIZE + 32);
    NativeArchiveGraphClose(graph);
    NativeArchiveClose(archive);
}

int main(void)
{
    _Static_assert(sizeof(void*) == 8, "These tests require native 64-bit pointers");
    test_archive_symbols_and_copy();
    test_joint_graph();
    test_shape_animation_graph();
    test_joint_display_descriptor_graph();
    test_light_descriptor_graph();
    test_animation_graph();
    test_texture_animation_graph();
    test_wobj();
    test_cobj();
    test_truncation_and_counts();
    test_bad_relocations();
    test_bad_symbols();
    test_external_chains();
    test_shape_sets();
    test_envelope_graph();
    test_joint_constraints();
    test_unsupported_joint_fields();
    test_unsupported_animation_and_wobj();
    test_external_graph_reference();
    test_type_conflicts();
    test_descriptor_bounds_and_strings();
    test_missing_relocation();
    test_stream_bounds();
    test_end_pointer();
    test_aobj_object_id();
    test_stream_validation();
    puts("Native DAT archive tests passed");
    return 0;
}
