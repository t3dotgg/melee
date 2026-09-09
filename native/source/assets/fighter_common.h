#ifndef MELEE_NATIVE_ASSETS_FIGHTER_COMMON_H
#define MELEE_NATIVE_ASSETS_FIGHTER_COMMON_H

#include "archive.h"

typedef struct NativeFighterCommonArchive NativeFighterCommonArchive;

NativeFighterCommonArchive*
NativeFighterCommonArchiveOpen(const NativeArchive* archive,
                               NativeArchiveGraph* graph);
void NativeFighterCommonArchiveClose(NativeFighterCommonArchive* common);
NativeArchiveStatus
NativeFighterCommonArchiveRead(NativeFighterCommonArchive* common,
                               const char* symbol, uint32_t offset,
                               void** output, NativeArchiveError* error);

#endif
