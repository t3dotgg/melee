#include "hsd_3B33.h"

#ifndef MELEE_NATIVE
#include <setjmp.h> // IWYU pragma: keep
#endif
#include <string.h>

#include "hsd_3A94.h"

void hsd_803B3344(u8 byte)
{
    u8* temp_r5;

    temp_r5 = hsd_804D79A0;
    if ((uintptr_t) temp_r5 < (uintptr_t) hsd_804D79A4 + (uintptr_t) hsd_804D79A8) {
        hsd_804D79A0 = temp_r5 + 1;
        *temp_r5 = byte;
        return;
    }

#ifdef MELEE_NATIVE
    return;
#else
    longjmp(&hsd_804D2648, true);
#endif
}

void hsd_803B3398(void* src, size_t size)
{
    void* temp_r3 = hsd_804D79A0;

    if ((uintptr_t) temp_r3 < (uintptr_t) hsd_804D79A4 + (uintptr_t) hsd_804D79A8 - size) {
        memcpy(temp_r3, src, size);
        hsd_804D79A0 += size;
        return;
    }

#ifdef MELEE_NATIVE
    return;
#else
    longjmp(&hsd_804D2648, true);
#endif
}
