#ifndef MELEE_NATIVE_ASSETS_ITEMS_H
#define MELEE_NATIVE_ASSETS_ITEMS_H

#include "archive.h"

typedef struct NativeItemArchive NativeItemArchive;
struct Article;

/* The archive and descriptor graph must outlive this context. Decoded item
 * objects remain valid until Close. Script pointers retain their BE bytes. */
NativeItemArchive* NativeItemArchiveOpen(const NativeArchive* archive,
                                         NativeArchiveGraph* graph);
void NativeItemArchiveClose(NativeItemArchive* items);
NativeArchiveStatus NativeItemArchiveRead(NativeItemArchive* items,
                                          const char* symbol, uint32_t offset,
                                          void** output,
                                          NativeArchiveError* error);
NativeArchiveStatus NativeItemArchiveArticle(NativeItemArchive* items,
                                             int kind, uint32_t offset,
                                             struct Article** output,
                                             NativeArchiveError* error);

#endif
