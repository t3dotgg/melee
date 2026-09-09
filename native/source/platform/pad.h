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

/* Configure a deterministic controller timeline for headless validation.
 * The syntax is a semicolon separated list of frame=buttons entries, for
 * example "0=START;1=NONE;60=A;61=NONE". The latest entry at or before the
 * current VI retrace remains active. */
BOOL NativePADSetScript(const char* script);
void NativePADSetTrace(BOOL enabled);
void NativePADAdvanceFrame(u32 frame);

#ifdef __cplusplus
}
#endif

#endif
