#ifndef MELEE_NATIVE_ASSETS_STAGE_H
#define MELEE_NATIVE_ASSETS_STAGE_H

#include "archive.h"

typedef struct NativeStageArchive NativeStageArchive;

NativeStageArchive* NativeStageArchiveOpen(const NativeArchive* archive,
                                           NativeArchiveGraph* graph);
void NativeStageArchiveClose(NativeStageArchive* stage);
NativeArchiveStatus NativeStageArchiveRead(NativeStageArchive* stage,
                                           const char* symbol, uint32_t offset,
                                           void** output,
                                           NativeArchiveError* error);

#endif
