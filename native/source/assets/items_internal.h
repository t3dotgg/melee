#ifndef MELEE_NATIVE_ASSETS_ITEMS_INTERNAL_H
#define MELEE_NATIVE_ASSETS_ITEMS_INTERNAL_H

#include "items.h"

const NativeArchive* NativeItemArchiveSource(NativeItemArchive* items);
NativeArchiveGraph* NativeItemArchiveGraph(NativeItemArchive* items);
void* NativeItemArchiveAllocate(NativeItemArchive* items, size_t size,
                                NativeArchiveError* error);
/* Distance to the next referenced object or public root, within the data. */
size_t NativeItemArchiveSpan(NativeItemArchive* items, uint32_t offset);

#endif
