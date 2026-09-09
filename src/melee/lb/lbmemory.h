#ifndef MELEE_LB_MEMORY_H
#define MELEE_LB_MEMORY_H

#include <Runtime/platform.h>

typedef struct Handle {
    /* 0x00 */ struct Handle* x0_next;
    /* 0x04 */ void* x4_lo;
    // Arena high bound for heap handles; allocation size for child handles.
    /* 0x08 */ void* x8_hi;
    // First allocation for heap handles. Child descriptors have no such field.
    /* 0x0C */ struct Handle* xC_prev;
} Handle;

/* 014E24 */ Handle* lbMemory_80014E24(void* lo, void* hi);
/* 014EEC */ void lbMemory_80014EEC(Handle* handle);
/* 014F7C */ size_t lbMemory_80014F7C(Handle* heap);
// Return an allocation descriptor. Its x4_lo field holds the data address.
/* 014FC8 */ Handle* lbMemory_80014FC8(Handle* heap, size_t size);
/* 0150F0 */ void lbMemFreeToHeap(Handle* heap, void* address);
// Compact live allocations. Return 0 without calling back if already packed.
/* 01529C */ u32 lbMemory_8001529C(Handle* heap, void (*callback)(u32),
                                   u32 callback_arg);
/* 0154BC */ void lbMemory_800154BC(uintptr_t* arenaLo, uintptr_t* arenaHi);
/* 0154D4 */ Handle* lbMemory_800154D4(void* lo, void* hi);
/* 0155A4 */ void lbMemory_800155A4(void);
/* 01564C */ void lbMemory_8001564C(void);

#endif
