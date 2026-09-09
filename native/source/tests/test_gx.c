#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <dolphin/gx.h>

int main(void)
{
    uint8_t xfb[64 * 64 * 2];
    GXInit(NULL, 0);
    GXSetViewport(0, 0, 64, 64, 0, 1);
    GXSetScissor(0, 0, 64, 64);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition2f32(-1.0f, -1.0f);
    GXColor4u8(255, 0, 0, 255);
    GXPosition2f32(1.0f, -1.0f);
    GXColor4u8(0, 255, 0, 255);
    GXPosition2f32(0.0f, 1.0f);
    GXColor4u8(0, 0, 255, 255);
    GXEnd();
    GXSetDispCopySrc(0, 0, 64, 64);
    GXSetDispCopyDst(64, 64);
    memset(xfb, 0, sizeof(xfb));
    GXCopyDisp(xfb, GX_FALSE);
    /* The center of the triangle must contain a non-black RGB565 pixel. */
    assert((xfb[(32 * 64 + 32) * 2] | xfb[(32 * 64 + 32) * 2 + 1]) != 0);

    GXSetCopyClear((GXColor){ 0, 0, 0, 255 }, GX_MAX_Z24);
    GXCopyDisp(xfb, GX_TRUE);
    memset(xfb, 0xff, sizeof(xfb));
    GXCopyDisp(xfb, GX_FALSE);
    for (size_t i = 0; i < sizeof(xfb); i++) assert(xfb[i] == 0);
    return 0;
}
