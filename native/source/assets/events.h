#ifndef MELEE_NATIVE_ASSETS_EVENTS_H
#define MELEE_NATIVE_ASSETS_EVENTS_H

#include "archive.h"

typedef struct NativeEventArchive NativeEventArchive;

/* The archive must outlive this context. Converted event tables remain valid
 * until Close. The public table has the 51 levels used by gmevent.c. */
NativeEventArchive* NativeEventArchiveOpen(const NativeArchive* archive);
void NativeEventArchiveClose(NativeEventArchive* events);
NativeArchiveStatus NativeEventArchiveRead(NativeEventArchive* events,
                                           const char* symbol, uint32_t offset,
                                           void** output,
                                           NativeArchiveError* error);

#endif
