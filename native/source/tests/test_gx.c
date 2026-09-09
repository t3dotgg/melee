#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <dolphin/gx.h>

static void put_be32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t) (value >> 24);
    dst[1] = (uint8_t) (value >> 16);
    dst[2] = (uint8_t) (value >> 8);
    dst[3] = (uint8_t) value;
}

static void put_be_float(uint8_t *dst, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    put_be32(dst, bits);
}

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

    /* GXCallDisplayList receives the exact big endian FIFO stream emitted by
     * GXBegin and the direct vertex writers. The native decoder must honor
     * the command's format bits and convert its payload before rasterizing. */
    GXInit(NULL, 0);
    GXSetViewport(0, 0, 64, 64, 0, 1);
    GXSetScissor(0, 0, 64, 64);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    uint8_t __attribute__((aligned(32))) list[64] = { 0 };
    list[0] = GX_TRIANGLES | GX_VTXFMT0;
    list[1] = 0;
    list[2] = 3;
    const float positions[3][3] = {
        { -1.0f, -1.0f, 0.0f }, { 1.0f, -1.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
    };
    const uint8_t colors[3][4] = {
        { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 },
    };
    size_t offset = 3;
    for (size_t vertex = 0; vertex < 3; vertex++) {
        for (size_t component = 0; component < 3; component++) {
            put_be_float(list + offset, positions[vertex][component]);
            offset += 4;
        }
        memcpy(list + offset, colors[vertex], 4);
        offset += 4;
    }
    GXCallDisplayList(list, sizeof list);
    GXSetDispCopySrc(0, 0, 64, 64);
    GXSetDispCopyDst(64, 64);
    memset(xfb, 0, sizeof xfb);
    GXCopyDisp(xfb, GX_FALSE);
    assert((xfb[(32 * 64 + 32) * 2] | xfb[(32 * 64 + 32) * 2 + 1]) != 0);
    return 0;
}
