#ifndef MELEE_NATIVE_ASSETS_FIGHTER_ARTICLES_H
#define MELEE_NATIVE_ASSETS_FIGHTER_ARTICLES_H

#include "archive.h"
#include "fighter_parts.h"
#include "items.h"

typedef struct NativeFighterArticles NativeFighterArticles;

/* The archive, graph and item context must outlive this context. */
NativeFighterArticles* NativeFighterArticlesOpen(const NativeArchive* archive,
                                                 NativeArchiveGraph* graph,
                                                 NativeItemArchive* items,
                                                 NativeFighterParts* parts);
void NativeFighterArticlesClose(NativeFighterArticles* articles);
NativeArchiveStatus NativeFighterArticlesRead(NativeFighterArticles* articles,
                                              const char* symbol,
                                              uint32_t offset, void*** output,
                                              NativeArchiveError* error);

#endif
