#ifndef _id_h_
#define _id_h_

#include <Runtime/platform.h>

#include <sysdolphin/baselib/objalloc.h>

typedef struct _IDEntry {
    struct _IDEntry* next;
    uintptr_t id;
    void* data;
} IDEntry;

typedef struct _HSD_IDTable {
    struct _IDEntry* table[101];
} HSD_IDTable;

HSD_ObjAllocData* HSD_IDGetAllocData(void);
void HSD_IDInitAllocData(void);
void HSD_IDSetup(void);
/// A NULL table selects the default table for insertion, removal, and lookup.
/// Replaces the data for an existing ID without freeing the previous data.
void HSD_IDInsertToTable(HSD_IDTable* table, uintptr_t id, void* data);
/// Removes and frees the entry. The caller still owns its data.
void HSD_IDRemoveByIDFromTable(HSD_IDTable* table, uintptr_t id);
/// Sets *success to 1 for a found ID, even when its data is NULL, or 0 if
/// absent. The success output is optional.
void* HSD_IDGetDataFromTable(HSD_IDTable* table, uintptr_t id, s32* success);
/// Clears the entire default table without freeing entries. Ignores the range.
void _HSD_IDForgetMemory(void* low, void* high);

static inline void* HSD_IDGetData(uintptr_t id, s32* success)
{
    return HSD_IDGetDataFromTable(NULL, id, success);
}

#endif
