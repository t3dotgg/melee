#ifndef MELEE_NATIVE_ASSETS_EFFECTS_H
#define MELEE_NATIVE_ASSETS_EFFECTS_H

#include "archive.h"

typedef struct NativeEffectArchive NativeEffectArchive;

NativeEffectArchive* NativeEffectArchiveOpen(const NativeArchive* archive,
                                             NativeArchiveGraph* graph);
void NativeEffectArchiveClose(NativeEffectArchive* effects);
/* Effect roots start with two raw particle-bank pointers. Widened
 * EF_EffectDesc records follow inline where EF_DAT_Entry.data begins. */
NativeArchiveStatus NativeEffectArchiveRead(NativeEffectArchive* effects,
                                            const char* symbol,
                                            uint32_t offset, void** output,
                                            NativeArchiveError* error);

#endif
