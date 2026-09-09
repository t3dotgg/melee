#ifndef MELEE_NATIVE_GX_RASTER_CASES_H
#define MELEE_NATIVE_GX_RASTER_CASES_H

#include <dolphin/gx.h>

static void gx_test_raster_setup(void)
{
    GXInit(NULL, 0);
    GXSetViewport(0, 0, 64, 64, 0, 1);
    GXSetScissor(0, 0, 64, 64);
    GXSetDispCopySrc(0, 0, 64, 64);
    GXSetDispCopyDst(64, 64);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
}

static void gx_test_raster_triangle(f32 z, GXColor color)
{
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(-1, -1, z);
    GXColor4u8(color.r, color.g, color.b, color.a);
    GXPosition3f32(1, -1, z);
    GXColor4u8(color.r, color.g, color.b, color.a);
    GXPosition3f32(0, 1, z);
    GXColor4u8(color.r, color.g, color.b, color.a);
    /* Particle draws finish at the declared vertex count without GXEnd. */
}

static u32 gx_test_raster_pixel(u16 x, u16 y)
{
    u32 value = 0;
    GXPeekARGB(x, y, &value);
    return value;
}

static void test_gx_raster_cases(void)
{
    GXColor red = { 255, 0, 0, 255 }, blue = { 0, 0, 255, 255 };
    gx_test_raster_setup();
    GXSetZMode(GX_TRUE, GX_LESS, GX_TRUE);
    gx_test_raster_triangle(0.2f, red);
    gx_test_raster_triangle(0.8f, blue);
    assert((gx_test_raster_pixel(32, 32) & 0xffffff) == 0xff0000);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    gx_test_raster_triangle(0.8f, blue);
    assert((gx_test_raster_pixel(32, 32) & 0xffffff) == 0x0000ff);

    gx_test_raster_setup();
    GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);
    gx_test_raster_triangle(0.2f, (GXColor) { 255, 0, 0, 0 });
    gx_test_raster_triangle(0.8f, blue);
    assert((gx_test_raster_pixel(32, 32) & 0xffffff) == 0x0000ff);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_COPY);
    gx_test_raster_triangle(0.2f, (GXColor) { 255, 0, 0, 128 });
    u32 pixel = gx_test_raster_pixel(32, 32);
    assert(((pixel >> 16) & 255) == 128);
    assert((pixel & 255) == 127);

    gx_test_raster_setup();
    /* The SDK swaps front/back culling before the hardware register write.
     * This triangle has negative screen-space winding. */
    GXSetCullMode(GX_CULL_FRONT);
    gx_test_raster_triangle(0.5f, red);
    assert((gx_test_raster_pixel(32, 32) & 0xffffff) == 0xff0000);
    GXSetCullMode(GX_CULL_BACK);
    gx_test_raster_triangle(0.4f, blue);
    assert((gx_test_raster_pixel(32, 32) & 0xffffff) == 0xff0000);

    /* Two quads must use disjoint groups of four vertices. Their common
     * diagonal must not be blended twice. */
    gx_test_raster_setup();
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_COPY);
    GXBegin(GX_QUADS, GX_VTXFMT0, 8);
    const f32 points[8][2] = {
        { -1, -1 }, { 0, -1 }, { 0, 1 }, { -1, 1 },
        { 0, -1 },  { 1, -1 }, { 1, 1 }, { 0, 1 },
    };
    for (u32 i = 0; i < 8; i++) {
        GXPosition3f32(points[i][0], points[i][1], 0.5f);
        GXColor4u8(i < 4 ? 255 : 0, 0, i < 4 ? 0 : 255, 128);
    }
    assert((gx_test_raster_pixel(16, 32) & 0xffffff) == 0x800000);
    assert((gx_test_raster_pixel(48, 32) & 0xffffff) == 0x000080);

    /* Raw FIFO values are used by particles. They must use the same vertex
     * descriptor as the named GXPosition and GXColor calls. */
    gx_test_raster_setup();
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    const f32 triangle[3][2] = { { -1, -1 }, { 1, -1 }, { 0, 1 } };
    for (u32 i = 0; i < 3; i++) {
        GXParam1f32(triangle[i][0]);
        GXParam1f32(triangle[i][1]);
        GXParam1f32(0.5f);
        GXParam1u32(0x00ff00ff);
    }
    assert((gx_test_raster_pixel(32, 32) & 0xffffff) == 0x00ff00);

    gx_test_raster_setup();
    f32 projection[4][4] = {
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, -0.1f, -1.1f },
        { 0, 0, -1, 0 },
    };
    GXSetProjection(projection, GX_PERSPECTIVE);
    gx_test_raster_triangle(2, red);
    assert((gx_test_raster_pixel(32, 32) & 0xffffff) == 0);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(-1, -1, -2);
    GXColor4u8(255, 0, 0, 255);
    GXPosition3f32(1, -1, -2);
    GXColor4u8(255, 0, 0, 255);
    GXPosition3f32(0, 1, 1);
    GXColor4u8(255, 0, 0, 255);
    GXEnd();
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(NAN, 0, -2);
    GXColor4u8(0, 0, 255, 255);
    GXPosition3f32(1, -1, -2);
    GXColor4u8(0, 0, 255, 255);
    GXPosition3f32(0, 1, -2);
    GXColor4u8(0, 0, 255, 255);
    GXEnd();
}

#endif
