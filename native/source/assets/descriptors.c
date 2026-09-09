#include "archive_internal.h"

#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/robj.h>
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/sobjlib.h>
#include <melee/lb/lbanim.h>

#include <stdlib.h>
#include <string.h>

#ifndef MELEE_NATIVE
#error "DAT descriptor conversion requires MELEE_NATIVE"
#endif

_Static_assert(sizeof(u32) == 4, "serialized words must be 32 bits");
_Static_assert(sizeof(float) == 4, "serialized floats must be 32 bits");

typedef enum Schema {
    SCHEMA_JOINT,
    SCHEMA_DOBJ,
    SCHEMA_MOBJ,
    SCHEMA_TOBJ,
    SCHEMA_POBJ,
    SCHEMA_VTXLIST,
    SCHEMA_MATERIAL,
    SCHEMA_PEDESC,
    SCHEMA_IMAGE,
    SCHEMA_TLUT,
    SCHEMA_TEXLOD,
    SCHEMA_TOBJTEV,
    SCHEMA_IMAGETBL,
    SCHEMA_TLUTTBL,
    SCHEMA_TEXANIM,
    SCHEMA_MATANIMJOINT,
    SCHEMA_MATANIM,
    SCHEMA_SHAPEANIMJOINT,
    SCHEMA_SHAPEANIMDOBJ,
    SCHEMA_SHAPEANIM,
    SCHEMA_ANIMATION,
    SCHEMA_AOBJ,
    SCHEMA_FOBJ,
    SCHEMA_WOBJ,
    SCHEMA_COBJ,
    SCHEMA_CANIM,
    SCHEMA_WOBJANIM,
    SCHEMA_ROBJANIM,
    SCHEMA_ROBJ,
    SCHEMA_IKHINT,
    SCHEMA_LIGHT,
    SCHEMA_LIGHTANIM,
    SCHEMA_LIGHTPOINT,
    SCHEMA_LIGHTSPOT,
    SCHEMA_LIGHTATTN,
    SCHEMA_FOG,
    SCHEMA_FOGADJ,
    SCHEMA_SOBJ,
    SCHEMA_VECTOR,
    SCHEMA_MATRIX,
    SCHEMA_STRING,
    SCHEMA_BYTES,
} Schema;

typedef struct Node {
    struct Node* next;
    struct Node* hash_next;
    uint32_t offset;
    Schema schema;
    size_t length;
    void* value;
} Node;

typedef struct FigaOwned {
    struct FigaOwned* next;
    uint32_t offset;
    FigaTree* tree;
} FigaOwned;

struct NativeArchiveGraph {
    const NativeArchive* archive;
    Node** buckets;
    size_t bucket_count;
    size_t node_count;
    Node* first;
    Node* last;
    Node* pending;
    NativeArchiveError failure;
    FigaOwned* owned_figa;
};

static bool read_reference(NativeArchiveGraph* graph, uint32_t field,
                           uint32_t* target, bool* present);
static void* add_node(NativeArchiveGraph* graph, uint32_t offset,
                      Schema schema, size_t length);

static NativeArchiveStatus graph_fail(NativeArchiveGraph* graph,
                                      NativeArchiveStatus status,
                                      uint32_t offset, const char* message)
{
    return NativeArchiveFail(&graph->failure, status, 32u + (size_t) offset,
                             message);
}

static float read_float(const uint8_t* bytes)
{
    uint32_t bits = NativeArchiveBE32(bytes);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static Vec3 read_vec(const uint8_t* bytes)
{
    Vec3 vector = { read_float(bytes), read_float(bytes + 4),
                    read_float(bytes + 8) };
    return vector;
}

static size_t hash_offset(uint32_t offset, size_t buckets)
{
    return ((uint64_t) offset * UINT64_C(11400714819323198485) >> 32) &
           (buckets - 1);
}

/* DAT does not store a byte count for buffers. The writer lays out every
 * object at a relocation target, so the next target is the end of a buffer.
 * This is the same extent rule used by HSDRaw and does not inspect payload
 * bytes as pointers. */
static bool next_target_length(const NativeArchive* archive, uint32_t offset,
                               size_t* length)
{
    uint32_t next = archive->data_size;
    uint32_t i;
    if (offset > archive->data_size) return false;
    for (i = 0; i < archive->reloc_count; ++i) {
        uint32_t target = NativeArchiveBE32(archive->data + archive->relocations[i]);
        if (target > offset && target < next) next = target;
    }
    for (i = 0; i < archive->public_count; ++i) {
        size_t at = archive->public_at + (size_t) i * 8;
        uint32_t target = NativeArchiveBE32(archive->file + at);
        if (target > offset && target < next) next = target;
    }
    *length = (size_t) next - offset;
    return true;
}

static bool link_tail(NativeArchiveGraph* graph, uint32_t field, void** output)
{
    uint32_t target;
    bool present;
    size_t length;
    *output = NULL;
    if (!read_reference(graph, field, &target, &present) || !present) return true;
    if (!next_target_length(graph->archive, target, &length) || length == 0) {
        graph_fail(graph, NATIVE_ARCHIVE_BOUNDS, target,
                   "buffer extent is outside archive data");
        return false;
    }
    *output = add_node(graph, target, SCHEMA_BYTES, length);
    return *output != NULL;
}

static bool vtxlist_length(NativeArchiveGraph* graph, uint32_t offset,
                           size_t* length)
{
    size_t count;
    if ((offset & 3u) || offset > graph->archive->data_size ||
        graph->archive->data_size - offset < 24) {
        graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset,
                   "vertex descriptor list is unaligned");
        return false;
    }
    for (count = 0; count < (graph->archive->data_size - offset) / 24; ++count) {
        uint32_t attr = NativeArchiveBE32(graph->archive->data + offset + count * 24);
        if (attr == GX_VA_NULL) {
            *length = (count + 1) * 24;
            return true;
        }
    }
    graph_fail(graph, NATIVE_ARCHIVE_BOUNDS, offset,
               "vertex descriptor list has no GX_VA_NULL terminator");
    return false;
}

static bool grow_index(NativeArchiveGraph* graph)
{
    size_t count = graph->bucket_count == 0 ? 64 : graph->bucket_count * 2;
    Node** buckets;
    Node* node;
    if (count < graph->bucket_count || count > SIZE_MAX / sizeof(*buckets)) {
        graph_fail(graph, NATIVE_ARCHIVE_NO_MEMORY, 0,
                   "descriptor index size overflow");
        return false;
    }
    buckets = calloc(count, sizeof(*buckets));
    if (buckets == NULL) {
        graph_fail(graph, NATIVE_ARCHIVE_NO_MEMORY, 0,
                   "cannot allocate descriptor index");
        return false;
    }
    for (node = graph->first; node != NULL; node = node->next) {
        size_t bucket = hash_offset(node->offset, count);
        node->hash_next = buckets[bucket];
        buckets[bucket] = node;
    }
    free(graph->buckets);
    graph->buckets = buckets;
    graph->bucket_count = count;
    return true;
}

static Node* find_node(NativeArchiveGraph* graph, uint32_t offset,
                       Schema schema)
{
    Node* node;
    if (graph->bucket_count == 0) return NULL;
    for (node = graph->buckets[hash_offset(offset, graph->bucket_count)];
         node != NULL; node = node->hash_next) {
        if (node->offset == offset && node->schema == schema) return node;
    }
    return NULL;
}

static void* add_node(NativeArchiveGraph* graph, uint32_t offset,
                      Schema schema, size_t length)
{
    const NativeArchive* archive = graph->archive;
    size_t disk_size;
    size_t host_size;
    size_t bucket;
    Node* node;

    if (graph->failure.status != NATIVE_ARCHIVE_OK) {
        return NULL;
    }
    if (graph->bucket_count == 0 && !grow_index(graph)) {
        return NULL;
    }
    bucket = hash_offset(offset, graph->bucket_count);
    for (node = graph->buckets[bucket]; node != NULL; node = node->hash_next) {
        if (node->offset == offset) {
            if (node->schema != schema ||
                (schema == SCHEMA_BYTES && node->length != length) ||
                ((schema == SCHEMA_IMAGETBL || schema == SCHEMA_TLUTTBL) &&
                 node->length / 4 != length)) {
                graph_fail(graph, NATIVE_ARCHIVE_TYPE_CONFLICT, offset,
                           "archive offset has conflicting descriptor types");
                return NULL;
            }
            return node->value;
        }
    }
    switch (schema) {
    case SCHEMA_JOINT:
        disk_size = 64;
        host_size = sizeof(HSD_Joint);
        break;
    case SCHEMA_DOBJ:
        disk_size = 16;
        host_size = sizeof(HSD_DObjDesc);
        break;
    case SCHEMA_MOBJ:
        disk_size = 24;
        host_size = sizeof(HSD_MObjDesc);
        break;
    case SCHEMA_TOBJ:
        disk_size = 92;
        host_size = sizeof(HSD_TObjDesc);
        break;
    case SCHEMA_POBJ:
        disk_size = 24;
        host_size = sizeof(HSD_PObjDesc);
        break;
    case SCHEMA_VTXLIST:
        if (length == 0 || length % 24 != 0) {
            graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset,
                       "vertex descriptor list has invalid size");
            return NULL;
        }
        disk_size = length;
        host_size = (length / 24) * sizeof(HSD_VtxDescList);
        break;
    case SCHEMA_MATERIAL:
        disk_size = 20;
        host_size = sizeof(HSD_Material);
        break;
    case SCHEMA_PEDESC:
        disk_size = 12;
        host_size = sizeof(HSD_PEDesc);
        break;
    case SCHEMA_IMAGE:
        disk_size = 24;
        host_size = sizeof(HSD_ImageDesc);
        break;
    case SCHEMA_TLUT:
        disk_size = 16;
        host_size = sizeof(HSD_TlutDesc);
        break;
    case SCHEMA_TEXLOD:
        disk_size = 16;
        host_size = sizeof(HSD_TexLODDesc);
        break;
    case SCHEMA_TOBJTEV:
        disk_size = 32;
        host_size = sizeof(HSD_TObjTevDesc);
        break;
    case SCHEMA_IMAGETBL:
    case SCHEMA_TLUTTBL:
        if (length > SIZE_MAX / 4 || length > (SIZE_MAX / sizeof(void*)) - 1) {
            graph_fail(graph, NATIVE_ARCHIVE_BOUNDS, offset,
                       "texture animation table size overflows");
            return NULL;
        }
        disk_size = length * 4;
        host_size = (length + 1) * sizeof(void*);
        break;
    case SCHEMA_TEXANIM:
        disk_size = 24;
        host_size = sizeof(HSD_TexAnim);
        break;
    case SCHEMA_MATANIMJOINT:
        disk_size = 12;
        host_size = sizeof(HSD_MatAnimJoint);
        break;
    case SCHEMA_MATANIM:
        disk_size = 16;
        host_size = sizeof(HSD_MatAnim);
        break;
    case SCHEMA_SHAPEANIMJOINT:
        disk_size = 12;
        host_size = sizeof(HSD_ShapeAnimJoint);
        break;
    case SCHEMA_SHAPEANIMDOBJ:
        disk_size = 8;
        host_size = sizeof(HSD_ShapeAnimDObj);
        break;
    case SCHEMA_SHAPEANIM:
        disk_size = 8;
        host_size = sizeof(HSD_ShapeAnim);
        break;
    case SCHEMA_ANIMATION:
        disk_size = 20;
        host_size = sizeof(HSD_AnimJoint);
        break;
    case SCHEMA_AOBJ:
        disk_size = 16;
        host_size = sizeof(HSD_AObjDesc);
        break;
    case SCHEMA_FOBJ:
        disk_size = 20;
        host_size = sizeof(HSD_FObjDesc);
        break;
    case SCHEMA_WOBJ:
        disk_size = 20;
        host_size = sizeof(HSD_WObjDesc);
        break;
    case SCHEMA_COBJ:
        /* HSD_CObjDesc is a 0x40-byte GameCube union. Its pointer fields are
         * decoded below into the wider native union members. */
        disk_size = 64;
        host_size = sizeof(HSD_CObjDesc);
        break;
    case SCHEMA_CANIM:
        disk_size = 12;
        host_size = sizeof(HSD_CameraAnim);
        break;
    case SCHEMA_WOBJANIM:
        disk_size = 8;
        host_size = sizeof(HSD_WObjAnim);
        break;
    case SCHEMA_ROBJ:
        disk_size = 12;
        host_size = sizeof(HSD_RObjDesc);
        break;
    case SCHEMA_IKHINT:
        disk_size = 8;
        host_size = sizeof(HSD_IKHintDesc);
        break;
    case SCHEMA_ROBJANIM:
        disk_size = 8;
        host_size = sizeof(HSD_RObjAnimJoint);
        break;
    case SCHEMA_LIGHT:
        disk_size = 28;
        host_size = sizeof(HSD_LightDesc);
        break;
    case SCHEMA_LIGHTANIM:
        disk_size = 16;
        host_size = sizeof(HSD_LightAnim);
        break;
    case SCHEMA_LIGHTPOINT:
        disk_size = 12;
        host_size = sizeof(HSD_LightPointDesc);
        break;
    case SCHEMA_LIGHTSPOT:
        disk_size = 20;
        host_size = sizeof(HSD_LightSpotDesc);
        break;
    case SCHEMA_LIGHTATTN:
        disk_size = 24;
        host_size = sizeof(HSD_LightAttn);
        break;
    case SCHEMA_FOG:
        disk_size = 20;
        host_size = sizeof(HSD_FogDesc);
        break;
    case SCHEMA_FOGADJ:
        disk_size = 68;
        host_size = sizeof(HSD_FogAdjDesc);
        break;
    case SCHEMA_SOBJ:
        disk_size = 8;
        host_size = sizeof(HSD_SObjDesc);
        break;
    case SCHEMA_VECTOR:
        disk_size = 12;
        host_size = sizeof(Vec3);
        break;
    case SCHEMA_MATRIX:
        disk_size = 48;
        host_size = sizeof(Mtx);
        break;
    case SCHEMA_STRING: {
        const uint8_t* end;
        if (offset >= archive->data_size ||
            (end = memchr(archive->data + offset, 0,
                          archive->data_size - offset)) == NULL) {
            graph_fail(graph, NATIVE_ARCHIVE_BOUNDS, offset,
                       "descriptor string is not terminated in data");
            return NULL;
        }
        disk_size = (size_t) (end - (archive->data + offset)) + 1;
        host_size = disk_size;
        break;
    }
    case SCHEMA_BYTES:
        disk_size = length;
        host_size = length == 0 ? 1 : length;
        break;
    default:
        graph_fail(graph, NATIVE_ARCHIVE_UNSUPPORTED, offset,
                   "descriptor schema is not implemented");
        return NULL;
    }
    if (offset > archive->data_size ||
        disk_size > (size_t) archive->data_size - offset) {
        graph_fail(graph, NATIVE_ARCHIVE_BOUNDS, offset,
                   "descriptor extends beyond archive data");
        return NULL;
    }
    if (schema != SCHEMA_BYTES && schema != SCHEMA_STRING && (offset & 3u)) {
        graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset,
                   "descriptor offset is unaligned");
        return NULL;
    }
    if (graph->node_count >= graph->bucket_count - graph->bucket_count / 4 &&
        !grow_index(graph)) {
        return NULL;
    }
    node = calloc(1, sizeof(*node));
    if (node == NULL) {
        graph_fail(graph, NATIVE_ARCHIVE_NO_MEMORY, offset,
                   "cannot allocate descriptor record");
        return NULL;
    }
    node->value = calloc(1, host_size);
    if (node->value == NULL) {
        free(node);
        graph_fail(graph, NATIVE_ARCHIVE_NO_MEMORY, offset,
                   "cannot allocate host descriptor");
        return NULL;
    }
    node->offset = offset;
    node->schema = schema;
    node->length = disk_size;
    bucket = hash_offset(offset, graph->bucket_count);
    node->hash_next = graph->buckets[bucket];
    graph->buckets[bucket] = node;
    if (graph->last != NULL) {
        graph->last->next = node;
    } else {
        graph->first = node;
    }
    graph->last = node;
    if (graph->pending == NULL) {
        graph->pending = node;
    }
    ++graph->node_count;
    return node->value;
}

static bool read_reference(NativeArchiveGraph* graph, uint32_t field,
                           uint32_t* target, bool* present)
{
    return NativeArchiveReference(graph->archive, field, target, present,
                                  &graph->failure) == NATIVE_ARCHIVE_OK;
}

static void* link_node(NativeArchiveGraph* graph, uint32_t field,
                       Schema schema, size_t length)
{
    uint32_t target;
    bool present;
    if (graph->failure.status != NATIVE_ARCHIVE_OK ||
        !read_reference(graph, field, &target, &present) || !present) {
        return NULL;
    }
    return add_node(graph, target, schema, length);
}

static bool unsupported_link(NativeArchiveGraph* graph, uint32_t field,
                              const char* message)
{
    uint32_t target;
    bool present;
    if (!read_reference(graph, field, &target, &present)) {
        return false;
    }
    if (present) {
        graph_fail(graph, NATIVE_ARCHIVE_UNSUPPORTED, field, message);
        return false;
    }
    return true;
}

static size_t fraction_size(uint8_t fraction)
{
    if (fraction == HSD_A_FRAC_FLOAT) {
        return 4;
    }
    if ((fraction & 31u) == 31u) {
        return 0;
    }
    switch (fraction & 0xe0u) {
    case HSD_A_FRAC_S16:
    case HSD_A_FRAC_U16:
        return 2;
    case HSD_A_FRAC_S8:
    case HSD_A_FRAC_U8:
        return 1;
    default:
        return 0;
    }
}

/* fobj.c consumes an opcode, a packed sample count, then values and waits.
 * Its values are explicitly little endian and must stay in byte form. */
static bool validate_stream(NativeArchiveGraph* graph, uint32_t offset,
                             size_t length, uint8_t value, uint8_t slope)
{
    const uint8_t* bytes = graph->archive->data + offset;
    size_t cursor = 0;
    size_t value_size = fraction_size(value);
    size_t slope_size = fraction_size(slope);
    if (value_size == 0 || slope_size == 0) {
        graph_fail(graph, NATIVE_ARCHIVE_UNSUPPORTED, offset,
                   "unsupported animation value format");
        return false;
    }
    while (cursor < length) {
        uint8_t byte = bytes[cursor++];
        unsigned opcode = byte & 15u;
        uint32_t count = ((byte >> 4) & 7u) + 1;
        unsigned shift = 3;
        size_t sample_size;
        while (byte & 128u) {
            if (cursor == length || shift >= 16) {
                goto invalid;
            }
            byte = bytes[cursor++];
            count += (uint32_t) (byte & 127u) << shift;
            shift += 7;
        }
        if (count > UINT16_MAX || opcode < HSD_A_OP_CON ||
            opcode > HSD_A_OP_KEY) {
            goto invalid;
        }
        sample_size = opcode == HSD_A_OP_SLP ? slope_size : value_size;
        if (opcode == HSD_A_OP_SPL) {
            sample_size += slope_size;
        }
        while (count-- != 0) {
            if (sample_size > length - cursor) {
                goto invalid;
            }
            cursor += sample_size;
            if (opcode != HSD_A_OP_SLP && cursor < length) {
                uint32_t wait = 0;
                shift = 0;
                do {
                    if (cursor == length || shift >= 21) {
                        goto invalid;
                    }
                    byte = bytes[cursor++];
                    wait |= (uint32_t) (byte & 127u) << shift;
                    shift += 7;
                } while (byte & 128u);
                if (wait > UINT16_MAX) {
                    goto invalid;
                }
            }
        }
    }
    return true;

invalid:
    graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset + (uint32_t) cursor,
               "invalid or truncated animation byte stream");
    return false;
}

static bool convert_node(NativeArchiveGraph* graph, Node* node)
{
    uint32_t offset = node->offset;
    const uint8_t* bytes = graph->archive->data + offset;
    switch (node->schema) {
    case SCHEMA_JOINT: {
        HSD_Joint* joint = node->value;
        if ((NativeArchiveBE32(bytes + 4) & (JOBJ_PTCL | JOBJ_SPLINE)) != 0) {
            if (!unsupported_link(graph, offset + 16,
                                  "joint spline and particle schemas are not "
                                  "implemented")) {
                return false;
            }
        }
        joint->flags = NativeArchiveBE32(bytes + 4);
        joint->rotation = read_vec(bytes + 20);
        joint->scale = read_vec(bytes + 32);
        joint->position = read_vec(bytes + 44);
        joint->class_name = link_node(graph, offset, SCHEMA_STRING, 0);
        joint->child = link_node(graph, offset + 8, SCHEMA_JOINT, 0);
        joint->next = link_node(graph, offset + 12, SCHEMA_JOINT, 0);
        if ((joint->flags & (JOBJ_PTCL | JOBJ_SPLINE)) == 0) {
            joint->u.dobjdesc = link_node(graph, offset + 16, SCHEMA_DOBJ, 0);
        }
        joint->mtx = link_node(graph, offset + 56, SCHEMA_MATRIX, 0);
        joint->robjdesc = link_node(graph, offset + 60, SCHEMA_ROBJ, 0);
        break;
    }
    case SCHEMA_DOBJ: {
        HSD_DObjDesc* dobj = node->value;
        dobj->class_name = link_node(graph, offset, SCHEMA_STRING, 0);
        dobj->next = link_node(graph, offset + 4, SCHEMA_DOBJ, 0);
        dobj->mobjdesc = link_node(graph, offset + 8, SCHEMA_MOBJ, 0);
        dobj->pobjdesc = link_node(graph, offset + 12, SCHEMA_POBJ, 0);
        break;
    }
    case SCHEMA_POBJ: {
        HSD_PObjDesc* pobj = node->value;
        uint32_t flags = ((uint32_t) bytes[12] << 8) | bytes[13];
        uint32_t display_size = ((uint32_t) bytes[14] << 8 | bytes[15]) * 32u;
        uint32_t target;
        bool present;
        size_t vtx_length;
        pobj->class_name = link_node(graph, offset, SCHEMA_STRING, 0);
        pobj->next = link_node(graph, offset + 4, SCHEMA_POBJ, 0);
        if (!read_reference(graph, offset + 8, &target, &present)) return false;
        if (present) {
            if (!vtxlist_length(graph, target, &vtx_length)) return false;
            pobj->verts = add_node(graph, target, SCHEMA_VTXLIST, vtx_length);
        }
        pobj->flags = (u16) flags;
        pobj->n_display = (u16) ((bytes[14] << 8) | bytes[15]);
        if (display_size != 0) {
            if (!read_reference(graph, offset + 16, &target, &present)) return false;
            if (!present) {
                graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset + 16,
                           "polygon display list is null but nonempty");
                return false;
            }
            pobj->display = add_node(graph, target, SCHEMA_BYTES, display_size);
            if (pobj->display == NULL) return false;
        }
        switch (flags & 0x3000u) {
        case POBJ_SKIN:
            pobj->u.joint = link_node(graph, offset + 20, SCHEMA_JOINT, 0);
            break;
        case POBJ_SHAPEANIM:
            /* Shape animation data has no host-safe representation yet. Keep
             * the polygon descriptor usable for scene roots that only need
             * their joints and cameras. */
            break;
        case POBJ_ENVELOPE:
            /* Envelope weights are optional for the native scene bootstrap.
             * Leave the union empty until the envelope schema is available. */
            break;
        default:
            graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset + 12,
                       "polygon descriptor has invalid type flags");
            return false;
        }
        break;
    }
    case SCHEMA_VTXLIST: {
        HSD_VtxDescList* list = node->value;
        size_t i;
        for (i = 0; i < node->length / 24; ++i) {
            const uint8_t* item = bytes + i * 24;
            list[i].attr = NativeArchiveBE32(item);
            list[i].attr_type = NativeArchiveBE32(item + 4);
            list[i].comp_cnt = NativeArchiveBE32(item + 8);
            list[i].comp_type = NativeArchiveBE32(item + 12);
            list[i].frac = item[16];
            list[i].stride = (u16) ((item[18] << 8) | item[19]);
            if (!link_tail(graph, offset + (uint32_t) (i * 24) + 20,
                           &list[i].vertex)) return false;
        }
        break;
    }
    case SCHEMA_MOBJ: {
        HSD_MObjDesc* mobj = node->value;
        bool present;
        mobj->class_name = link_node(graph, offset, SCHEMA_STRING, 0);
        mobj->rendermode = NativeArchiveBE32(bytes + 4);
        mobj->texdesc = link_node(graph, offset + 8, SCHEMA_TOBJ, 0);
        mobj->mat = link_node(graph, offset + 12, SCHEMA_MATERIAL, 0);
        if (mobj->mat == NULL) {
            graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset + 12,
                       "material descriptor is required by MObjLoad");
            return false;
        }
        if (!read_reference(graph, offset + 16, &(uint32_t) { 0 }, &present)) {
            return false;
        }
        if (present) {
            graph_fail(graph, NATIVE_ARCHIVE_UNSUPPORTED, offset + 16,
                       "MObj render descriptor is not implemented");
            return false;
        }
        mobj->renderdesc = NULL;
        mobj->pedesc = link_node(graph, offset + 20, SCHEMA_PEDESC, 0);
        break;
    }
    case SCHEMA_TOBJ: {
        HSD_TObjDesc* tobj = node->value;
        tobj->class_name = link_node(graph, offset, SCHEMA_STRING, 0);
        tobj->next = link_node(graph, offset + 4, SCHEMA_TOBJ, 0);
        tobj->id = NativeArchiveBE32(bytes + 8);
        tobj->src = NativeArchiveBE32(bytes + 12);
        tobj->rotate = read_vec(bytes + 16);
        tobj->scale = read_vec(bytes + 28);
        tobj->translate = read_vec(bytes + 40);
        tobj->wrap_s = NativeArchiveBE32(bytes + 52);
        tobj->wrap_t = NativeArchiveBE32(bytes + 56);
        tobj->repeat_s = bytes[60];
        tobj->repeat_t = bytes[61];
        tobj->blend_flags = NativeArchiveBE32(bytes + 64);
        tobj->blending = read_float(bytes + 68);
        tobj->magFilt = NativeArchiveBE32(bytes + 72);
        tobj->imagedesc = link_node(graph, offset + 76, SCHEMA_IMAGE, 0);
        tobj->tlutdesc = link_node(graph, offset + 80, SCHEMA_TLUT, 0);
        tobj->lod = link_node(graph, offset + 84, SCHEMA_TEXLOD, 0);
        tobj->tev = link_node(graph, offset + 88, SCHEMA_TOBJTEV, 0);
        break;
    }
    case SCHEMA_MATERIAL: {
        HSD_Material* material = node->value;
        memcpy(&material->ambient, bytes, 4);
        memcpy(&material->diffuse, bytes + 4, 4);
        memcpy(&material->specular, bytes + 8, 4);
        material->alpha = read_float(bytes + 12);
        material->shininess = read_float(bytes + 16);
        break;
    }
    case SCHEMA_PEDESC: {
        HSD_PEDesc* pe = node->value;
        memcpy(pe, bytes, 12);
        break;
    }
    case SCHEMA_IMAGE: {
        HSD_ImageDesc* image = node->value;
        image->width = (u16) ((bytes[4] << 8) | bytes[5]);
        image->height = (u16) ((bytes[6] << 8) | bytes[7]);
        image->format = NativeArchiveBE32(bytes + 8);
        image->mipmap = NativeArchiveBE32(bytes + 12);
        image->minLOD = read_float(bytes + 16);
        image->maxLOD = read_float(bytes + 20);
        if (!link_tail(graph, offset, &image->image_ptr)) return false;
        break;
    }
    case SCHEMA_TLUT: {
        HSD_TlutDesc* tlut = node->value;
        void* data = NULL;
        tlut->fmt = NativeArchiveBE32(bytes + 4);
        tlut->tlut_name = NativeArchiveBE32(bytes + 8);
        tlut->n_entries = (u16) ((bytes[12] << 8) | bytes[13]);
        if (tlut->n_entries != 0) {
            data = link_node(graph, offset, SCHEMA_BYTES,
                             (size_t) tlut->n_entries * 2);
            if (data == NULL && graph->failure.status != NATIVE_ARCHIVE_OK) return false;
        } else if (!link_tail(graph, offset, &data)) {
            return false;
        }
        tlut->lut = data;
        break;
    }
    case SCHEMA_TEXLOD: {
        HSD_TexLODDesc* lod = node->value;
        lod->minFilt = NativeArchiveBE32(bytes);
        lod->LODBias = read_float(bytes + 4);
        lod->bias_clamp = bytes[8];
        lod->edgeLODEnable = bytes[9];
        lod->max_anisotropy = NativeArchiveBE32(bytes + 12);
        break;
    }
    case SCHEMA_TOBJTEV: {
        HSD_TObjTevDesc* tev = node->value;
        memcpy(tev, bytes, 28);
        tev->active = NativeArchiveBE32(bytes + 28);
        break;
    }
    case SCHEMA_MATANIMJOINT: {
        HSD_MatAnimJoint* animation = node->value;
        animation->child = link_node(graph, offset, SCHEMA_MATANIMJOINT, 0);
        animation->next = link_node(graph, offset + 4,
                                    SCHEMA_MATANIMJOINT, 0);
        animation->matanim = link_node(graph, offset + 8, SCHEMA_MATANIM, 0);
        break;
    }
    case SCHEMA_MATANIM: {
        HSD_MatAnim* animation = node->value;
        animation->next = link_node(graph, offset, SCHEMA_MATANIM, 0);
        animation->aobjdesc = link_node(graph, offset + 4, SCHEMA_AOBJ, 0);
        animation->texanim = link_node(graph, offset + 8, SCHEMA_TEXANIM, 0);
        if (!unsupported_link(graph, offset + 12,
                              "material render animation schema is not implemented")) {
            return false;
        }
        break;
    }
    case SCHEMA_SHAPEANIMJOINT: {
        HSD_ShapeAnimJoint* animation = node->value;
        animation->child = link_node(graph, offset,
                                     SCHEMA_SHAPEANIMJOINT, 0);
        animation->next = link_node(graph, offset + 4,
                                    SCHEMA_SHAPEANIMJOINT, 0);
        animation->shapeanimdobj = link_node(graph, offset + 8,
                                             SCHEMA_SHAPEANIMDOBJ, 0);
        break;
    }
    case SCHEMA_SHAPEANIMDOBJ: {
        HSD_ShapeAnimDObj* animation = node->value;
        animation->next = link_node(graph, offset, SCHEMA_SHAPEANIMDOBJ, 0);
        animation->shapeanim = link_node(graph, offset + 4,
                                          SCHEMA_SHAPEANIM, 0);
        break;
    }
    case SCHEMA_SHAPEANIM: {
        HSD_ShapeAnim* animation = node->value;
        animation->next = link_node(graph, offset, SCHEMA_SHAPEANIM, 0);
        animation->aobjdesc = link_node(graph, offset + 4, SCHEMA_AOBJ, 0);
        break;
    }
    case SCHEMA_TEXANIM: {
        HSD_TexAnim* animation = node->value;
        uint32_t table_offset;
        bool table_present;
        uint16_t image_count = (uint16_t) ((bytes[20] << 8) | bytes[21]);
        uint16_t tlut_count = (uint16_t) ((bytes[22] << 8) | bytes[23]);
        animation->id = NativeArchiveBE32(bytes + 4);
        animation->aobjdesc = link_node(graph, offset + 8, SCHEMA_AOBJ, 0);
        animation->n_imagetbl = image_count;
        animation->n_tluttbl = tlut_count;
        if (!read_reference(graph, offset + 12, &table_offset, &table_present)) return false;
        if (image_count == 0 && table_present) {
            graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset + 12,
                       "texture animation image table is present with zero count");
            return false;
        }
        animation->imagetbl = table_present
            ? add_node(graph, table_offset, SCHEMA_IMAGETBL, image_count) : NULL;
        if (!read_reference(graph, offset + 16, &table_offset, &table_present)) return false;
        if (tlut_count == 0 && table_present) {
            graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset + 16,
                       "texture animation TLUT table is present with zero count");
            return false;
        }
        animation->tluttbl = table_present
            ? add_node(graph, table_offset, SCHEMA_TLUTTBL, tlut_count) : NULL;
        animation->next = link_node(graph, offset, SCHEMA_TEXANIM, 0);
        break;
    }
    case SCHEMA_IMAGETBL:
    case SCHEMA_TLUTTBL: {
        size_t count = node->length / 4;
        void** table = node->value;
        size_t i;
        for (i = 0; i < count; ++i) {
            Schema target_schema = node->schema == SCHEMA_IMAGETBL ? SCHEMA_IMAGE : SCHEMA_TLUT;
            table[i] = link_node(graph, offset + (uint32_t) (i * 4),
                                 target_schema, 0);
        }
        table[count] = NULL;
        break;
    }
    case SCHEMA_ANIMATION: {
        HSD_AnimJoint* animation = node->value;
        animation->flags = NativeArchiveBE32(bytes + 16);
        animation->child = link_node(graph, offset, SCHEMA_ANIMATION, 0);
        animation->next = link_node(graph, offset + 4, SCHEMA_ANIMATION, 0);
        animation->aobjdesc = link_node(graph, offset + 8, SCHEMA_AOBJ, 0);
        animation->robj_anim = link_node(graph, offset + 12, SCHEMA_ROBJANIM, 0);
        break;
    }
    case SCHEMA_AOBJ: {
        HSD_AObjDesc* aobj = node->value;
        HSD_Joint* joint;
        aobj->flags = NativeArchiveBE32(bytes);
        aobj->end_frame = read_float(bytes + 4);
        aobj->fobjdesc = link_node(graph, offset + 8, SCHEMA_FOBJ, 0);
        joint = link_node(graph, offset + 12, SCHEMA_JOINT, 0);
        if (joint != NULL && sizeof(aobj->obj_id) < sizeof(uintptr_t)) {
            graph_fail(graph, NATIVE_ARCHIVE_UNSUPPORTED, offset + 12,
                       "native animation object IDs must hold a host pointer");
            return false;
        }
        aobj->obj_id = (uintptr_t) joint;
        break;
    }
    case SCHEMA_FOBJ: {
        HSD_FObjDesc* fobj = node->value;
        uint32_t target;
        bool present;
        fobj->length = NativeArchiveBE32(bytes + 4);
        fobj->startframe = read_float(bytes + 8);
        fobj->type = bytes[12];
        fobj->frac_value = bytes[13];
        fobj->frac_slope = bytes[14];
        fobj->dummy0 = bytes[15];
        fobj->next = link_node(graph, offset, SCHEMA_FOBJ, 0);
        if (graph->failure.status != NATIVE_ARCHIVE_OK ||
            !read_reference(graph, offset + 16, &target, &present)) {
            return false;
        }
        if (!present) {
            if (fobj->length != 0) {
                graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset + 16,
                           "animation stream is null but its length is not zero");
                return false;
            }
        } else {
            fobj->ad = add_node(graph, target, SCHEMA_BYTES, fobj->length);
            if (fobj->ad == NULL ||
                !validate_stream(graph, target, fobj->length,
                                 fobj->frac_value, fobj->frac_slope)) {
                return false;
            }
        }
        break;
    }
    case SCHEMA_WOBJ: {
        HSD_WObjDesc* wobj = node->value;
        wobj->class_name = link_node(graph, offset, SCHEMA_STRING, 0);
        wobj->pos = read_vec(bytes + 4);
        wobj->robjdesc = link_node(graph, offset + 16, SCHEMA_ROBJ, 0);
        break;
    }
    case SCHEMA_COBJ: {
        HSD_CObjDesc* desc = node->value;
        HSD_CameraDescCommon* common = &desc->common;
        uint16_t projection_type =
            (uint16_t) (((uint16_t) bytes[6] << 8) | bytes[7]);

        if (projection_type != PROJ_PERSPECTIVE &&
            projection_type != PROJ_FRUSTUM && projection_type != PROJ_ORTHO) {
            graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset + 6,
                       "camera descriptor has an invalid projection type");
            return false;
        }
        common->class_name = link_node(graph, offset, SCHEMA_STRING, 0);
        common->flags = (uint16_t) (((uint16_t) bytes[4] << 8) | bytes[5]);
        common->projection_type = projection_type;
        common->viewport.xmin = (s16) (((uint16_t) bytes[8] << 8) | bytes[9]);
        common->viewport.xmax =
            (s16) (((uint16_t) bytes[10] << 8) | bytes[11]);
        common->viewport.ymin =
            (s16) (((uint16_t) bytes[12] << 8) | bytes[13]);
        common->viewport.ymax =
            (s16) (((uint16_t) bytes[14] << 8) | bytes[15]);
        common->scissor.left =
            (uint16_t) (((uint16_t) bytes[16] << 8) | bytes[17]);
        common->scissor.right =
            (uint16_t) (((uint16_t) bytes[18] << 8) | bytes[19]);
        common->scissor.top =
            (uint16_t) (((uint16_t) bytes[20] << 8) | bytes[21]);
        common->scissor.bottom =
            (uint16_t) (((uint16_t) bytes[22] << 8) | bytes[23]);
        common->eyepos = link_node(graph, offset + 24, SCHEMA_WOBJ, 0);
        common->interest = link_node(graph, offset + 28, SCHEMA_WOBJ, 0);
        common->roll = read_float(bytes + 32);
        common->up_vector = link_node(graph, offset + 36, SCHEMA_VECTOR, 0);
        common->nnear = read_float(bytes + 40);
        common->ffar = read_float(bytes + 44);
        if (projection_type == PROJ_PERSPECTIVE) {
            desc->perspective.fov = read_float(bytes + 48);
            desc->perspective.aspect = read_float(bytes + 52);
        } else {
            desc->frustum.top = read_float(bytes + 48);
            desc->frustum.bottom = read_float(bytes + 52);
            desc->frustum.left = read_float(bytes + 56);
            desc->frustum.right = read_float(bytes + 60);
        }
        break;
    }
    case SCHEMA_CANIM: {
        HSD_CameraAnim* animation = node->value;
        animation->aobjdesc = link_node(graph, offset, SCHEMA_AOBJ, 0);
        animation->eye_anim = link_node(graph, offset + 4, SCHEMA_WOBJANIM, 0);
        animation->interest_anim =
            link_node(graph, offset + 8, SCHEMA_WOBJANIM, 0);
        break;
    }
    case SCHEMA_WOBJANIM: {
        HSD_WObjAnim* animation = node->value;
        animation->aobjdesc = link_node(graph, offset, SCHEMA_AOBJ, 0);
        animation->robjanim = link_node(graph, offset + 4, SCHEMA_ROBJANIM, 0);
        break;
    }
    case SCHEMA_ROBJ: {
        HSD_RObjDesc* constraint = node->value;
        constraint->next = link_node(graph, offset, SCHEMA_ROBJ, 0);
        constraint->flags = NativeArchiveBE32(bytes + 4);
        switch (constraint->flags & ROBJ_TYPE_MASK) {
        case REFTYPE_JOBJ:
            constraint->u.joint = link_node(graph, offset + 8, SCHEMA_JOINT, 0);
            if (constraint->u.joint == NULL) {
                graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset + 8,
                           "joint constraint requires a joint");
                return false;
            }
            break;
        case REFTYPE_LIMIT:
            constraint->u.limit = read_float(bytes + 8);
            break;
        case REFTYPE_IKHINT:
            constraint->u.ik_hint = link_node(graph, offset + 8, SCHEMA_IKHINT, 0);
            if (constraint->u.ik_hint == NULL) {
                graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset + 8,
                           "IK constraint requires a hint descriptor");
                return false;
            }
            break;
        case REFTYPE_EXP:
            graph_fail(graph, NATIVE_ARCHIVE_UNSUPPORTED, offset + 8,
                       "PowerPC constraint functions require native bindings");
            return false;
        case REFTYPE_BYTECODE:
            graph_fail(graph, NATIVE_ARCHIVE_UNSUPPORTED, offset + 8,
                       "constraint bytecode schema is not implemented");
            return false;
        default:
            graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset + 4,
                       "constraint has an invalid reference type");
            return false;
        }
        break;
    }
    case SCHEMA_IKHINT: {
        HSD_IKHintDesc* hint = node->value;
        hint->bone_length = read_float(bytes);
        hint->rotate_x = read_float(bytes + 4);
        break;
    }
    case SCHEMA_ROBJANIM: {
        HSD_RObjAnimJoint* animation = node->value;
        animation->next = link_node(graph, offset, SCHEMA_ROBJANIM, 0);
        animation->aobjdesc = link_node(graph, offset + 4, SCHEMA_AOBJ, 0);
        break;
    }
    case SCHEMA_LIGHT: {
        HSD_LightDesc* light = node->value;
        uint16_t flags = (uint16_t) (((uint16_t) bytes[8] << 8) | bytes[9]);
        uint16_t attnflags =
            (uint16_t) (((uint16_t) bytes[10] << 8) | bytes[11]);
        light->class_name = link_node(graph, offset, SCHEMA_STRING, 0);
        light->next = link_node(graph, offset + 4, SCHEMA_LIGHT, 0);
        light->flags = flags;
        light->attnflags = attnflags;
        memcpy(&light->color, bytes + 12, sizeof(light->color));
        light->position = link_node(graph, offset + 16, SCHEMA_WOBJ, 0);
        light->interest = link_node(graph, offset + 20, SCHEMA_WOBJ, 0);
        switch (flags & LOBJ_TYPE_MASK) {
        case LOBJ_POINT:
            light->u.p = link_node(graph, offset + 24,
                                   attnflags & LOBJ_LIGHT_ATTN
                                       ? SCHEMA_LIGHTATTN
                                       : SCHEMA_LIGHTPOINT,
                                   0);
            break;
        case LOBJ_SPOT:
            light->u.p = link_node(graph, offset + 24,
                                   attnflags != 0 ? SCHEMA_LIGHTATTN
                                                  : SCHEMA_LIGHTSPOT,
                                   0);
            break;
        case LOBJ_AMBIENT:
        case LOBJ_INFINITE:
            /* These light types do not read the union during LObjLoad. Keep
             * validating the relocation while leaving the host union empty. */
            {
                uint32_t target;
                bool present;
                if (!read_reference(graph, offset + 24, &target, &present))
                    return false;
            }
            break;
        }
        break;
    }
    case SCHEMA_LIGHTANIM: {
        HSD_LightAnim* animation = node->value;
        animation->next = link_node(graph, offset, SCHEMA_LIGHTANIM, 0);
        animation->aobjdesc = link_node(graph, offset + 4, SCHEMA_AOBJ, 0);
        animation->position_anim =
            link_node(graph, offset + 8, SCHEMA_WOBJANIM, 0);
        animation->interest_anim =
            link_node(graph, offset + 12, SCHEMA_WOBJANIM, 0);
        break;
    }
    case SCHEMA_LIGHTPOINT: {
        HSD_LightPointDesc* point = node->value;
        point->ref_br = read_float(bytes);
        point->ref_dist = read_float(bytes + 4);
        point->dist_func = NativeArchiveBE32(bytes + 8);
        break;
    }
    case SCHEMA_LIGHTSPOT: {
        HSD_LightSpotDesc* spot = node->value;
        spot->cutoff = read_float(bytes);
        spot->spot_func = NativeArchiveBE32(bytes + 4);
        spot->ref_br = read_float(bytes + 8);
        spot->ref_dist = read_float(bytes + 12);
        spot->dist_func = NativeArchiveBE32(bytes + 16);
        break;
    }
    case SCHEMA_LIGHTATTN: {
        HSD_LightAttn* attenuation = node->value;
        attenuation->a0 = read_float(bytes);
        attenuation->a1 = read_float(bytes + 4);
        attenuation->a2 = read_float(bytes + 8);
        attenuation->k0 = read_float(bytes + 12);
        attenuation->k1 = read_float(bytes + 16);
        attenuation->k2 = read_float(bytes + 20);
        break;
    }
    case SCHEMA_FOG: {
        HSD_FogDesc* fog = node->value;
        fog->type = NativeArchiveBE32(bytes);
        fog->fogadjdesc = link_node(graph, offset + 4, SCHEMA_FOGADJ, 0);
        fog->start = read_float(bytes + 8);
        fog->end = read_float(bytes + 12);
        memcpy(&fog->color, bytes + 16, sizeof(fog->color));
        break;
    }
    case SCHEMA_FOGADJ: {
        HSD_FogAdjDesc* adjustment = node->value;
        adjustment->center = (u16) ((bytes[0] << 8) | bytes[1]);
        adjustment->width = (u16) ((bytes[2] << 8) | bytes[3]);
        for (size_t row = 0; row < 4; ++row) {
            for (size_t column = 0; column < 4; ++column) {
                adjustment->mtx[row][column] =
                    read_float(bytes + 4 + (row * 4 + column) * 4);
            }
        }
        break;
    }
    case SCHEMA_SOBJ: {
        HSD_SObjDesc* descriptor = node->value;
        descriptor->image = link_node(graph, offset, SCHEMA_IMAGE, 0);
        descriptor->tlut = link_node(graph, offset + 4, SCHEMA_TLUT, 0);
        break;
    }
    case SCHEMA_VECTOR: {
        Vec3* vector = node->value;
        *vector = read_vec(bytes);
        break;
    }
    case SCHEMA_MATRIX: {
        float* matrix = node->value;
        size_t i;
        for (i = 0; i < 12; ++i) {
            matrix[i] = read_float(bytes + i * 4);
        }
        break;
    }
    case SCHEMA_STRING:
    case SCHEMA_BYTES:
        memcpy(node->value, bytes, node->length);
        break;
    }
    return graph->failure.status == NATIVE_ARCHIVE_OK;
}

NativeArchiveStatus NativeArchiveGraphOpen(const NativeArchive* archive,
                                          NativeArchiveGraph** output,
                                          NativeArchiveError* error)
{
    NativeArchiveGraph* graph;
    if (output == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "graph output is null");
    }
    *output = NULL;
    if (archive == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "archive is null");
    }
    graph = calloc(1, sizeof(*graph));
    if (graph == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_NO_MEMORY, 0,
                                 "cannot allocate descriptor graph");
    }
    graph->archive = archive;
    *output = graph;
    return NativeArchiveFail(error, NATIVE_ARCHIVE_OK, 0, "ok");
}

void NativeArchiveGraphClose(NativeArchiveGraph* graph)
{
    if (graph != NULL) {
        Node* node = graph->first;
        while (node != NULL) {
            Node* next = node->next;
            free(node->value);
            free(node);
            node = next;
        }
        FigaOwned* figa = graph->owned_figa;
        while (figa != NULL) {
            FigaOwned* next = figa->next;
            free(figa->tree->nodes);
            free(figa->tree->tracks);
            free(figa->tree);
            free(figa);
            figa = next;
        }
        free(graph->buckets);
        free(graph);
    }
}

static NativeArchiveStatus convert_root(NativeArchiveGraph* graph,
                                        uint32_t offset, Schema schema,
                                        void** output,
                                        NativeArchiveError* error)
{
    void* root;
    *output = NULL;
    if (graph == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "descriptor graph is null");
    }
    root = add_node(graph, offset, schema, 0);
    if (root != NULL) {
        /* A graph may decode several roots. Queue this root when an earlier
         * conversion drained the pending list. */
        if (graph->pending == NULL) {
            graph->pending = find_node(graph, offset, schema);
        }
        while (graph->pending != NULL) {
            Node* node = graph->pending;
            if (!convert_node(graph, node)) {
                break;
            }
            graph->pending = node->next;
        }
    }
    if (graph->failure.status != NATIVE_ARCHIVE_OK) {
        if (error != NULL) {
            *error = graph->failure;
        }
        return graph->failure.status;
    }
    *output = root;
    return NativeArchiveFail(error, NATIVE_ARCHIVE_OK, 0, "ok");
}


NativeArchiveStatus NativeArchiveFigaTree(NativeArchiveGraph* graph,
                                         uint32_t offset,
                                         struct FigaTree** output,
                                         NativeArchiveError* error)
{
    const uint8_t* bytes;
    uint32_t nodes_offset, tracks_offset;
    bool present_nodes, present_tracks;
    size_t node_count = 0, track_count = 0, i;
    int max_track = -1;
    FigaTree* tree;
    FigaTrack* tracks;
    s8* nodes;
    if (output == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "figatree output is null");
    }
    *output = NULL;
    if (graph == NULL || graph->archive == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "descriptor graph is null");
    }
    bytes = graph->archive->data;
    if (!NativeArchiveDataRange(graph->archive, offset, 16) || (offset & 3u)) {
        return graph_fail(graph, NATIVE_ARCHIVE_BOUNDS, offset,
                          "figatree descriptor is outside archive data");
    }
    if (!read_reference(graph, offset + 8, &nodes_offset, &present_nodes) ||
        !read_reference(graph, offset + 12, &tracks_offset, &present_tracks)) {
        if (error != NULL) *error = graph->failure;
        return graph->failure.status;
    }
    if (!present_nodes || !present_tracks) {
        return graph_fail(graph, NATIVE_ARCHIVE_INVALID, offset,
                          "figatree requires node and track arrays");
    }
    if (!NativeArchiveDataRange(graph->archive, nodes_offset, 1) ||
        !NativeArchiveDataRange(graph->archive, tracks_offset, 12)) {
        return graph_fail(graph, NATIVE_ARCHIVE_BOUNDS, offset,
                          "figatree array is outside archive data");
    }
    while (nodes_offset + node_count < graph->archive->data_size &&
           node_count < 65536) {
        int value = (int8_t) bytes[nodes_offset + node_count];
        ++node_count;
        if (value == -1) break;
        if (value < 0) {
            return graph_fail(graph, NATIVE_ARCHIVE_INVALID,
                              nodes_offset + (uint32_t) node_count - 1,
                              "figatree node index is invalid");
        }
        if (value > max_track) max_track = value;
    }
    if (node_count == 0 || node_count > 65536 ||
        (int8_t) bytes[nodes_offset + node_count - 1] != -1) {
        return graph_fail(graph, NATIVE_ARCHIVE_BOUNDS, nodes_offset,
                          "figatree node list is not terminated");
    }
    track_count = max_track < 0 ? 0 : (size_t) max_track + 1;
    if (track_count > (graph->archive->data_size - tracks_offset) / 12) {
        return graph_fail(graph, NATIVE_ARCHIVE_BOUNDS, tracks_offset,
                          "figatree track list is truncated");
    }
    if (track_count > UINT32_MAX / 12 ||
        !NativeArchiveDataRange(graph->archive, tracks_offset, track_count * 12)) {
        return graph_fail(graph, NATIVE_ARCHIVE_BOUNDS, tracks_offset,
                          "figatree track list is outside archive data");
    }
    tree = calloc(1, sizeof(*tree));
    nodes = malloc(node_count);
    tracks = track_count == 0 ? NULL : calloc(track_count, sizeof(*tracks));
    if (tree == NULL || nodes == NULL || (track_count != 0 && tracks == NULL)) {
        free(tree); free(nodes); free(tracks);
        return graph_fail(graph, NATIVE_ARCHIVE_NO_MEMORY, offset,
                          "cannot allocate figatree");
    }
    memcpy(nodes, bytes + nodes_offset, node_count);
    for (i = 0; i < track_count; ++i) {
        const uint8_t* track = bytes + tracks_offset + i * 12;
        uint32_t ad_offset;
        bool present_ad;
        tracks[i].length = (u16) ((track[0] << 8) | track[1]);
        tracks[i].startframe = (u16) ((track[2] << 8) | track[3]);
        tracks[i].obj_type = track[4];
        tracks[i].frac_value = track[5];
        tracks[i].frac_slope = track[6];
        if (!read_reference(graph, tracks_offset + (uint32_t) (i * 12) + 8,
                            &ad_offset, &present_ad)) {
            free(tree); free(nodes); free(tracks);
            if (error != NULL) *error = graph->failure;
            return graph->failure.status;
        }
        if (present_ad) {
            if (!NativeArchiveDataRange(graph->archive, ad_offset, tracks[i].length)) {
                free(tree); free(nodes); free(tracks);
                return graph_fail(graph, NATIVE_ARCHIVE_BOUNDS, ad_offset,
                                  "figatree track stream is truncated");
            }
            tracks[i].ad_head = add_node(graph, ad_offset, SCHEMA_BYTES,
                                          tracks[i].length);
            if (tracks[i].ad_head == NULL) {
                free(tree); free(nodes); free(tracks);
                if (error != NULL) *error = graph->failure;
                return graph->failure.status;
            }
        } else if (tracks[i].length != 0) {
            free(tree); free(nodes); free(tracks);
            return graph_fail(graph, NATIVE_ARCHIVE_INVALID,
                              tracks_offset + (uint32_t) (i * 12) + 8,
                              "figatree track stream is null but nonempty");
        }
    }
    tree->type = (int) NativeArchiveBE32(bytes + offset);
    tree->flags = NativeArchiveBE32(bytes + offset + 4);
    tree->frames = read_float(bytes + offset + 8);
    tree->nodes = nodes;
    tree->tracks = tracks;
    {
        FigaOwned* existing;
        FigaOwned* owned = calloc(1, sizeof(*owned));
        if (owned == NULL) {
            free(tree->nodes); free(tree->tracks); free(tree);
            return graph_fail(graph, NATIVE_ARCHIVE_NO_MEMORY, offset,
                              "cannot allocate figatree owner");
        }
        for (existing = graph->owned_figa; existing != NULL;
             existing = existing->next) {
            if (existing->offset == offset) {
                free(owned); free(tree->nodes); free(tree->tracks); free(tree);
                *output = existing->tree;
                return NativeArchiveFail(error, NATIVE_ARCHIVE_OK, 0, "ok");
            }
        }
        owned->offset = offset;
        owned->tree = tree;
        owned->next = graph->owned_figa;
        graph->owned_figa = owned;
    }
    *output = tree;
    return NativeArchiveFail(error, NATIVE_ARCHIVE_OK, 0, "ok");
}


#define ROOT_READER(name, type, schema)                                      \
    NativeArchiveStatus name(NativeArchiveGraph* graph, uint32_t offset,      \
                             type** output, NativeArchiveError* error)       \
    {                                                                        \
        void* root = NULL;                                                   \
        NativeArchiveStatus status;                                          \
        if (output == NULL) {                                                \
            return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,        \
                                     "descriptor output is null");           \
        }                                                                    \
        status = convert_root(graph, offset, schema, &root, error);            \
        *output = root;                                                      \
        return status;                                                       \
    }

ROOT_READER(NativeArchiveJoint, HSD_Joint, SCHEMA_JOINT)
ROOT_READER(NativeArchiveMatAnimJoint, HSD_MatAnimJoint,
            SCHEMA_MATANIMJOINT)
ROOT_READER(NativeArchiveShapeAnimJoint, HSD_ShapeAnimJoint,
            SCHEMA_SHAPEANIMJOINT)
ROOT_READER(NativeArchiveAnimation, HSD_AnimJoint, SCHEMA_ANIMATION)
ROOT_READER(NativeArchiveAObj, HSD_AObjDesc, SCHEMA_AOBJ)
ROOT_READER(NativeArchiveWObj, HSD_WObjDesc, SCHEMA_WOBJ)
ROOT_READER(NativeArchiveCObj, HSD_CObjDesc, SCHEMA_COBJ)
ROOT_READER(NativeArchiveCameraAnimation, HSD_CameraAnim, SCHEMA_CANIM)
ROOT_READER(NativeArchiveLight, HSD_LightDesc, SCHEMA_LIGHT)
ROOT_READER(NativeArchiveLightAnimation, HSD_LightAnim, SCHEMA_LIGHTANIM)
ROOT_READER(NativeArchiveFog, HSD_FogDesc, SCHEMA_FOG)
ROOT_READER(NativeArchiveSObj, HSD_SObjDesc, SCHEMA_SOBJ)

static NativeArchiveStatus find_named_root(NativeArchiveGraph* graph,
                                           const char* name, uint32_t* offset,
                                           NativeArchiveError* error)
{
    if (graph == NULL) {
        return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,
                                 "descriptor graph is null");
    }
    return NativeArchiveFind(graph->archive, name, offset, error);
}

#define NAMED_ROOT_READER(name, type, reader)                                \
    NativeArchiveStatus name(NativeArchiveGraph* graph, const char* symbol,  \
                             type** output, NativeArchiveError* error)       \
    {                                                                         \
        uint32_t offset = 0;                                                  \
        NativeArchiveStatus status;                                           \
        if (output == NULL) {                                                 \
            return NativeArchiveFail(error, NATIVE_ARCHIVE_INVALID, 0,        \
                                     "descriptor output is null");            \
        }                                                                      \
        *output = NULL;                                                        \
        status = find_named_root(graph, symbol, &offset, error);              \
        if (status != NATIVE_ARCHIVE_OK) {                                    \
            return status;                                                     \
        }                                                                      \
        return reader(graph, offset, output, error);                          \
    }

NAMED_ROOT_READER(NativeArchiveJointByName, HSD_Joint, NativeArchiveJoint)
NAMED_ROOT_READER(NativeArchiveMatAnimJointByName, HSD_MatAnimJoint,
                  NativeArchiveMatAnimJoint)
NAMED_ROOT_READER(NativeArchiveShapeAnimJointByName, HSD_ShapeAnimJoint,
                  NativeArchiveShapeAnimJoint)
NAMED_ROOT_READER(NativeArchiveAnimationByName, HSD_AnimJoint,
                  NativeArchiveAnimation)
NAMED_ROOT_READER(NativeArchiveAObjByName, HSD_AObjDesc, NativeArchiveAObj)
NAMED_ROOT_READER(NativeArchiveWObjByName, HSD_WObjDesc, NativeArchiveWObj)
NAMED_ROOT_READER(NativeArchiveCObjByName, HSD_CObjDesc, NativeArchiveCObj)
NAMED_ROOT_READER(NativeArchiveLightByName, HSD_LightDesc,
                  NativeArchiveLight)
NAMED_ROOT_READER(NativeArchiveLightAnimationByName, HSD_LightAnim,
                  NativeArchiveLightAnimation)
NAMED_ROOT_READER(NativeArchiveFogByName, HSD_FogDesc, NativeArchiveFog)
NAMED_ROOT_READER(NativeArchiveSObjByName, HSD_SObjDesc, NativeArchiveSObj)
NAMED_ROOT_READER(NativeArchiveFigaTreeByName, FigaTree, NativeArchiveFigaTree)
