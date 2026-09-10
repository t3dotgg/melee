#ifndef MELEE_NATIVE_ASSETS_FIGHTERS_H
#define MELEE_NATIVE_ASSETS_FIGHTERS_H

#include "archive.h"

struct NativeItemArchive;
typedef struct NativeFighterArchive NativeFighterArchive;

NativeFighterArchive*
NativeFighterArchiveOpen(const NativeArchive* archive,
                         NativeArchiveGraph* graph,
                         struct NativeItemArchive* items);
void NativeFighterArchiveClose(NativeFighterArchive* fighter);
NativeArchiveStatus NativeFighterArchiveRead(NativeFighterArchive* fighter,
                                             const char* symbol,
                                             uint32_t offset, void** output,
                                             NativeArchiveError* error);

#endif
