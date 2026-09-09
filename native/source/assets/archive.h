#ifndef MELEE_NATIVE_ASSETS_ARCHIVE_H
#define MELEE_NATIVE_ASSETS_ARCHIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Offsets in this API name bytes in the serialized DAT data block. */
typedef struct NativeArchive NativeArchive;
typedef struct NativeArchiveGraph NativeArchiveGraph;

typedef enum NativeArchiveStatus {
    NATIVE_ARCHIVE_OK,
    NATIVE_ARCHIVE_INVALID,
    NATIVE_ARCHIVE_BOUNDS,
    NATIVE_ARCHIVE_NO_MEMORY,
    NATIVE_ARCHIVE_NOT_FOUND,
    NATIVE_ARCHIVE_UNSUPPORTED,
    NATIVE_ARCHIVE_TYPE_CONFLICT,
} NativeArchiveStatus;

typedef struct NativeArchiveError {
    NativeArchiveStatus status;
    size_t offset;
    const char* message;
} NativeArchiveError;

typedef struct NativeArchiveSymbol {
    const char* name;
    uint32_t offset;
} NativeArchiveSymbol;

/* The archive owns a copy of the file. The caller can release input on return.
 * Errors contain a static message and a file byte offset, when applicable. */
NativeArchiveStatus NativeArchiveOpen(const void* input, size_t size,
                                     NativeArchive** output,
                                     NativeArchiveError* error);
void NativeArchiveClose(NativeArchive* archive);
size_t NativeArchiveDataSize(const NativeArchive* archive);
size_t NativeArchivePublicCount(const NativeArchive* archive);
size_t NativeArchiveExternalCount(const NativeArchive* archive);
NativeArchiveStatus NativeArchivePublic(const NativeArchive* archive,
                                       size_t index,
                                       NativeArchiveSymbol* output,
                                       NativeArchiveError* error);
NativeArchiveStatus NativeArchiveExternal(const NativeArchive* archive,
                                         size_t index,
                                         NativeArchiveSymbol* output,
                                         NativeArchiveError* error);
NativeArchiveStatus NativeArchiveFind(const NativeArchive* archive,
                                     const char* name, uint32_t* offset,
                                     NativeArchiveError* error);

/* The relocation table distinguishes a reference to offset zero from null.
 * External references fail with UNSUPPORTED until typed binding is available. */
NativeArchiveStatus NativeArchiveReference(const NativeArchive* archive,
                                          uint32_t field_offset,
                                          uint32_t* target, bool* present,
                                          NativeArchiveError* error);
NativeArchiveStatus NativeArchiveRead(const NativeArchive* archive,
                                     uint32_t offset, void* output, size_t size,
                                     NativeArchiveError* error);

/* Converted objects own their strings, matrices and byte streams. The archive
 * must outlive the graph. Host pointers remain valid until GraphClose.
 * Supported schemas are explicit. Unsupported branches fail without returning
 * a partial root. A failed conversion leaves the graph unusable except Close. */
NativeArchiveStatus NativeArchiveGraphOpen(const NativeArchive* archive,
                                          NativeArchiveGraph** output,
                                          NativeArchiveError* error);
void NativeArchiveGraphClose(NativeArchiveGraph* graph);

struct HSD_Joint;
struct HSD_MatAnimJoint;
struct HSD_ShapeAnimJoint;
struct HSD_AnimJoint;
struct HSD_AObjDesc;
struct HSD_WObjDesc;
struct HSD_CameraAnim;
union HSD_CObjDesc;
struct FigaTree;
NativeArchiveStatus NativeArchiveJoint(NativeArchiveGraph* graph,
                                      uint32_t offset,
                                      struct HSD_Joint** output,
                                      NativeArchiveError* error);
NativeArchiveStatus NativeArchiveJointByName(NativeArchiveGraph* graph,
                                             const char* name,
                                             struct HSD_Joint** output,
                                             NativeArchiveError* error);
NativeArchiveStatus NativeArchiveMatAnimJoint(
    NativeArchiveGraph* graph, uint32_t offset,
    struct HSD_MatAnimJoint** output, NativeArchiveError* error);
NativeArchiveStatus NativeArchiveMatAnimJointByName(
    NativeArchiveGraph* graph, const char* name,
    struct HSD_MatAnimJoint** output, NativeArchiveError* error);
NativeArchiveStatus NativeArchiveShapeAnimJoint(
    NativeArchiveGraph* graph, uint32_t offset,
    struct HSD_ShapeAnimJoint** output, NativeArchiveError* error);
NativeArchiveStatus NativeArchiveShapeAnimJointByName(
    NativeArchiveGraph* graph, const char* name,
    struct HSD_ShapeAnimJoint** output, NativeArchiveError* error);
NativeArchiveStatus NativeArchiveAnimation(NativeArchiveGraph* graph,
                                          uint32_t offset,
                                          struct HSD_AnimJoint** output,
                                          NativeArchiveError* error);
NativeArchiveStatus NativeArchiveAnimationByName(
    NativeArchiveGraph* graph, const char* name, struct HSD_AnimJoint** output,
    NativeArchiveError* error);
NativeArchiveStatus NativeArchiveAObj(NativeArchiveGraph* graph,
                                     uint32_t offset,
                                     struct HSD_AObjDesc** output,
                                     NativeArchiveError* error);
NativeArchiveStatus NativeArchiveAObjByName(NativeArchiveGraph* graph,
                                            const char* name,
                                            struct HSD_AObjDesc** output,
                                            NativeArchiveError* error);
NativeArchiveStatus NativeArchiveWObj(NativeArchiveGraph* graph,
                                     uint32_t offset,
                                     struct HSD_WObjDesc** output,
                                     NativeArchiveError* error);
NativeArchiveStatus NativeArchiveWObjByName(NativeArchiveGraph* graph,
                                            const char* name,
                                            struct HSD_WObjDesc** output,
                                            NativeArchiveError* error);
NativeArchiveStatus NativeArchiveCObj(NativeArchiveGraph* graph,
                                      uint32_t offset,
                                      union HSD_CObjDesc** output,
                                      NativeArchiveError* error);
NativeArchiveStatus NativeArchiveCObjByName(NativeArchiveGraph* graph,
                                            const char* name,
                                            union HSD_CObjDesc** output,
                                            NativeArchiveError* error);
NativeArchiveStatus NativeArchiveCameraAnimation(
    NativeArchiveGraph* graph, uint32_t offset, struct HSD_CameraAnim** output,
    NativeArchiveError* error);
NativeArchiveStatus NativeArchiveFigaTree(NativeArchiveGraph* graph,
                                         uint32_t offset,
                                         struct FigaTree** output,
                                         NativeArchiveError* error);
NativeArchiveStatus NativeArchiveFigaTreeByName(
    NativeArchiveGraph* graph, const char* name, struct FigaTree** output,
    NativeArchiveError* error);

#endif
