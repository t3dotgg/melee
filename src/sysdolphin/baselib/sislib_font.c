#include "sislib_font.h"

TextGlyphTexture HSD_SisLib_FontAtlas[] ATTRIBUTE_ALIGN(32) = {
#ifdef MELEE_NATIVE
    // The retail font is supplied by the host UI. Keep the fixed table shape
    // so native text code can safely query its entry count.
    [0 ... 286] = { { 0 } },
#else
#include <sysdolphin/baselib/sislib_font.inc>
#endif
};
