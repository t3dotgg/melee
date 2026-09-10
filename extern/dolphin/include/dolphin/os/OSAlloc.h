#ifndef _DOLPHIN_OSALLOC_H_
#define _DOLPHIN_OSALLOC_H_

#include <dolphin/types.h>

typedef int OSHeapHandle;

extern volatile OSHeapHandle __OSCurrHeap;

#ifdef MELEE_NATIVE
void* OSAllocFromHeap(int heap, size_t size);
#else
void * OSAllocFromHeap(int heap, u32 size);
#endif
void * OSAllocFixed(void * rstart, void * rend);
void OSFreeToHeap(int heap, void * ptr);
int OSSetCurrentHeap(int heap);
void * OSInitAlloc(void * arenaStart, void * arenaEnd, int maxHeaps);
int OSCreateHeap(void * start, void * end);
void OSDestroyHeap(int heap);
void OSAddToHeap(int heap, void * start, void * end);
#ifdef MELEE_NATIVE
intptr_t OSCheckHeap(int heap);
size_t OSReferentSize(void* ptr);
#else
s32 OSCheckHeap(int heap);
u32 OSReferentSize(void * ptr);
#endif
void OSDumpHeap(int heap);
#ifdef MELEE_NATIVE
void OSVisitAllocated(void (*visitor)(void*, size_t));
#else
void OSVisitAllocated(void (* visitor)(void *, u32));
#endif

#define OSAlloc(size) OSAllocFromHeap(__OSCurrHeap, (size))
#define OSFree(ptr)   OSFreeToHeap(__OSCurrHeap, (ptr))

#endif
