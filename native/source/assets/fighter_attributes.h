#ifndef MELEE_NATIVE_ASSETS_FIGHTER_ATTRIBUTES_H
#define MELEE_NATIVE_ASSETS_FIGHTER_ATTRIBUTES_H

#include "archive.h"

typedef struct NativeFighterAttributes NativeFighterAttributes;

/* Offsets refer to ftData.ext_attr, not to the ftData root. The context
 * owns each converted block and preserves its address on repeated reads. */
NativeFighterAttributes*
NativeFighterAttributesOpen(const NativeArchive* archive,
                            NativeArchiveGraph* graph);
void NativeFighterAttributesClose(NativeFighterAttributes* attributes);
NativeArchiveStatus
NativeFighterAttributesRead(NativeFighterAttributes* attributes,
                            const char* symbol, uint32_t offset, void** output,
                            NativeArchiveError* error);

#endif
