#include "archive_internal.h"

#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/wobj.h>
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
    SCHEMA_ANIMATION,
    SCHEMA_AOBJ,
    SCHEMA_FOBJ,
    SCHEMA_WOBJ,
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
                (schema == SCHEMA_BYTES && node->length != length)) {
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
        if (!unsupported_link(graph, offset + 16,
                              "joint display, spline and particle schemas "
                              "are not implemented") ||
            !unsupported_link(graph, offset + 60,
                              "joint constraint schema is not implemented")) {
            return false;
        }
        joint->flags = NativeArchiveBE32(bytes + 4);
        joint->rotation = read_vec(bytes + 20);
        joint->scale = read_vec(bytes + 32);
        joint->position = read_vec(bytes + 44);
        joint->class_name = link_node(graph, offset, SCHEMA_STRING, 0);
        joint->child = link_node(graph, offset + 8, SCHEMA_JOINT, 0);
        joint->next = link_node(graph, offset + 12, SCHEMA_JOINT, 0);
        joint->mtx = link_node(graph, offset + 56, SCHEMA_MATRIX, 0);
        break;
    }
    case SCHEMA_ANIMATION: {
        HSD_AnimJoint* animation = node->value;
        if (!unsupported_link(graph, offset + 12,
                              "animation constraint schema is not implemented")) {
            return false;
        }
        animation->flags = NativeArchiveBE32(bytes + 16);
        animation->child = link_node(graph, offset, SCHEMA_ANIMATION, 0);
        animation->next = link_node(graph, offset + 4, SCHEMA_ANIMATION, 0);
        animation->aobjdesc = link_node(graph, offset + 8, SCHEMA_AOBJ, 0);
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
        if (!unsupported_link(graph, offset + 16,
                              "world object constraint schema is not implemented")) {
            return false;
        }
        wobj->class_name = link_node(graph, offset, SCHEMA_STRING, 0);
        wobj->pos = read_vec(bytes + 4);
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
ROOT_READER(NativeArchiveAnimation, HSD_AnimJoint, SCHEMA_ANIMATION)
ROOT_READER(NativeArchiveAObj, HSD_AObjDesc, SCHEMA_AOBJ)
ROOT_READER(NativeArchiveWObj, HSD_WObjDesc, SCHEMA_WOBJ)
