#ifndef MELEE_NATIVE_PLATFORM_PAD_H
#define MELEE_NATIVE_PLATFORM_PAD_H

#include <dolphin/pad.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL NativePADSetStatus(s32 chan, const PADStatus* status);
BOOL NativePADSetConnected(s32 chan, BOOL connected);
const PADStatus* NativePADGetStatus(s32 chan);

/* Feed a keyboard event into controller 0. The display backend calls this
 * while it pumps Cocoa events. Tests and other front ends can use the same
 * function without depending on Cocoa. */
void NativePADHandleKeyCode(u16 key_code, BOOL pressed, BOOL repeat);
void NativePADResetKeyboard(void);

#ifdef __cplusplus
}
#endif

#endif
