#ifndef MELEE_NATIVE_DISPLAY_H
#define MELEE_NATIVE_DISPLAY_H

#include <stdint.h>

/*
 * Present one GameCube RGB565 XFB on the host display. The copy is synchronous,
 * so callers may reuse the XFB after this function returns.
 *
 * Set MELEE_HEADLESS=1 to skip Cocoa window creation and event handling. In
 * headless mode, MELEE_FRAME_OUTPUT can save one frame as a binary PPM. Use
 * "/path/frame.ppm@120" or "120:/path/frame.ppm" to capture retrace 120.
 * A path without a retrace captures the first presented frame.
 */
void NativeDisplayPresent(const void* xfb, uint16_t width, uint16_t height,
                          uint16_t stride_pixels);
void NativeDisplayPumpEvents(void);
void NativeDisplaySetRetraceCount(uint32_t retrace_count);

#endif
