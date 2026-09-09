#ifndef MELEE_NATIVE_ASSETS_FIGHTER_PARTS_H
#define MELEE_NATIVE_ASSETS_FIGHTER_PARTS_H

#include "archive.h"

typedef struct NativeFighterParts NativeFighterParts;

typedef enum NativeFighterPartsType {
    NATIVE_FIGHTER_PARTS_MODELS,
    NATIVE_FIGHTER_PARTS_ANIMATIONS,
    NATIVE_FIGHTER_PARTS_SHIELD,
    NATIVE_FIGHTER_PARTS_DYNAMICS,
    NATIVE_FIGHTER_PARTS_DESCRIPTION,
} NativeFighterPartsType;

/* The archive and graph must outlive this context. Converted records keep
 * their identity until Close. After a failed Read, only Close is valid. */
NativeArchiveStatus NativeFighterPartsOpen(const NativeArchive* archive,
                                           NativeArchiveGraph* graph,
                                           NativeFighterParts** output,
                                           NativeArchiveError* error);
void NativeFighterPartsClose(NativeFighterParts* parts);
NativeArchiveStatus NativeFighterPartsRead(NativeFighterParts* parts,
                                           NativeFighterPartsType type,
                                           uint32_t offset, void** output,
                                           NativeArchiveError* error);

struct FtPartsVisLookup;
NativeArchiveStatus
NativeFighterPartsVisibility(NativeFighterParts* parts, uint32_t offset,
                             size_t count, struct FtPartsVisLookup** output,
                             NativeArchiveError* error);

#endif
