#ifndef MELEE_NATIVE_OS_H
#define MELEE_NATIVE_OS_H

#include <dolphin/types.h>

/* Host callbacks run only while the game permits interrupts. */
BOOL NativeOSInterruptsEnabled(void);

#endif
