#ifndef MELEE_NATIVE_PLATFORM_PAD_H
#define MELEE_NATIVE_PLATFORM_PAD_H

#include <dolphin/pad.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL NativePADSetStatus(s32 chan, const PADStatus* status);
BOOL NativePADSetConnected(s32 chan, BOOL connected);
const PADStatus* NativePADGetStatus(s32 chan);

#ifdef __cplusplus
}
#endif

#endif
