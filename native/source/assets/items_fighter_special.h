#ifndef MELEE_NATIVE_ASSETS_ITEMS_FIGHTER_SPECIAL_H
#define MELEE_NATIVE_ASSETS_ITEMS_FIGHTER_SPECIAL_H

#include "archive.h"

typedef struct NativeItemArchive NativeItemArchive;

NativeArchiveStatus NativeItemFighterSpecialRead(NativeItemArchive* items,
                                                 int kind, uint32_t offset,
                                                 void** output,
                                                 NativeArchiveError* error);

#endif
