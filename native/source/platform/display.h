#ifndef MELEE_NATIVE_DISPLAY_H
#define MELEE_NATIVE_DISPLAY_H

#include <stdint.h>

/* Present one GameCube RGB565 XFB on the host display. The copy is synchronous,
 * so callers may reuse the XFB after this function returns. */
void NativeDisplayPresent(const void* xfb, uint16_t width, uint16_t height,
                          uint16_t stride_pixels);
void NativeDisplayPumpEvents(void);

#endif
