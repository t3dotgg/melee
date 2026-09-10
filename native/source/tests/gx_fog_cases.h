#ifndef MELEE_NATIVE_GX_FOG_CASES_H
#define MELEE_NATIVE_GX_FOG_CASES_H

#include <assert.h>

#include <dolphin/gx.h>

static void gx_fog_test_setup(GXProjectionType type)
{
    f32 projection[4][4] = {
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, -0.125F, -1.125F },
        { 0, 0, 0, 1 },
    };
    if (type == GX_PERSPECTIVE) {
        projection[3][2] = -1.0F;
        projection[3][3] = 0.0F;
    }
    GXInit(NULL, 0);
    GXSetProjection(projection, type);
    GXSetViewport(0, 0, 128, 8, 0, 1);
    GXSetScissor(0, 0, 128, 8);
    GXSetCullMode(GX_CULL_NONE);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL,
                  GX_DF_NONE, GX_AF_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumTevStages(1);
    GXSetDispCopySrc(0, 0, 128, 8);
    GXSetDispCopyDst(128, 8);
}

static u16 gx_fog_test_pixel(GXProjectionType type, f32 distance,
                             GXColor color, unsigned x)
{
    u8 xfb[128 * 8 * 2] = { 0 };
    f32 size = type == GX_PERSPECTIVE ? distance : 1.0F;
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(-size, -size, -distance);
    GXColor4u8(color.r, color.g, color.b, color.a);
    GXPosition3f32(size, -size, -distance);
    GXColor4u8(color.r, color.g, color.b, color.a);
    GXPosition3f32(size, size, -distance);
    GXColor4u8(color.r, color.g, color.b, color.a);
    GXPosition3f32(-size, size, -distance);
    GXColor4u8(color.r, color.g, color.b, color.a);
    GXEnd();
    GXCopyDisp(xfb, GX_FALSE);
    return (u16) ((xfb[(4 * 128 + x) * 2] << 8) | xfb[(4 * 128 + x) * 2 + 1]);
}

static void test_gx_fog_cases(void)
{
    const GXColor white = { 255, 255, 255, 255 };
    const GXColor blue = { 0, 0, 255, 0 };
    const GXFogType curves[] = {
        GX_FOG_LIN, GX_FOG_EXP, GX_FOG_EXP2, GX_FOG_REVEXP, GX_FOG_REVEXP2,
    };
    const u16 midpoints[] = { 0x7bff, 0x087f, 0x39ff, 0xef7f, 0xbdff };
    /* The same eye distance must give the same fog under both projections. */
    for (GXProjectionType projection = GX_PERSPECTIVE;
         projection <= GX_ORTHOGRAPHIC; projection++)
    {
        gx_fog_test_setup(projection);
        GXSetFog(GX_FOG_LIN, 3, 7, 1, 9, blue);
        assert(gx_fog_test_pixel(projection, 2, white, 64) == 0xffff);
        assert(gx_fog_test_pixel(projection, 8, white, 64) == 0x001f);
        for (unsigned i = 0; i < sizeof curves / sizeof curves[0]; i++) {
            GXSetFog(curves[i], 3, 7, 1, 9, blue);
            assert(gx_fog_test_pixel(projection, 5, white, 64) ==
                   midpoints[i]);
        }
        GXSetFog(GX_FOG_LIN, 3, 7, 1, 9, blue);
        GXSetViewport(0, 0, 128, 8, 0.2F, 0.8F);
        assert(gx_fog_test_pixel(projection, 5, white, 64) == 0x7bff);
        GXSetViewport(0, 0, 128, 8, 1, 0);
        assert(gx_fog_test_pixel(projection, 5, white, 64) == 0x7bff);
        GXSetFog(GX_FOG_NONE, 3, 7, 1, 9, blue);
        assert(gx_fog_test_pixel(projection, 5, white, 64) == 0xffff);
        GXSetFog(GX_FOG_LIN, 3, 3, 1, 9, blue);
        assert(gx_fog_test_pixel(projection, 5, white, 64) == 0xffff);
    }

    /* Range table values come from the SDK projection formula. */
    f32 matrix[4][4] = {
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, -0.125F, -1.125F },
        { 0, 0, -1, 0 },
    };
    GXFogAdjTable table;
    GXInitFogAdjTable(&table, 128, matrix);
    assert(table.r[0] == 286);
    assert(table.r[1] == 362);
    assert(table.r[9] == 1305);
    GXInitFogAdjTable(&table, 0, matrix);
    for (unsigned i = 0; i < 10; i++) {
        assert(table.r[i] == 256);
    }

    /* Range samples are copied. Adjustment increases fog away from center. */
    gx_fog_test_setup(GX_PERSPECTIVE);
    GXSetFog(GX_FOG_LIN, 3, 7, 1, 9, blue);
    for (unsigned i = 0; i < 10; i++) {
        table.r[i] = 512;
    }
    GXSetFogRangeAdj(GX_TRUE, 64, &table);
    for (unsigned i = 0; i < 10; i++) {
        table.r[i] = 256;
    }
    assert(gx_fog_test_pixel(GX_PERSPECTIVE, 5, white, 64) == 0x7bff);
    assert(gx_fog_test_pixel(GX_PERSPECTIVE, 5, white, 96) == 0x001f);
    GXSetFogRangeAdj(GX_FALSE, 64, NULL);
    assert(gx_fog_test_pixel(GX_PERSPECTIVE, 5, white, 96) == 0x7bff);

    /* Fog color alpha must not replace the TEV alpha used by blending. */
    gx_fog_test_setup(GX_ORTHOGRAPHIC);
    GXSetCopyClear((GXColor) { 0, 0, 0, 255 }, GX_MAX_Z24);
    GXCopyDisp(NULL, GX_TRUE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_COPY);
    GXSetFog(GX_FOG_LIN, 3, 7, 1, 9, (GXColor) { 255, 0, 0, 0 });
    assert(gx_fog_test_pixel(GX_ORTHOGRAPHIC, 8, (GXColor) { 0, 0, 0, 64 },
                             64) == 0x4000);
}

#endif
