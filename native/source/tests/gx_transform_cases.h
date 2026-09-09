#ifndef MELEE_NATIVE_TEST_GX_TRANSFORM_CASES_H
#define MELEE_NATIVE_TEST_GX_TRANSFORM_CASES_H

#include <math.h>

static void test_gx_transform_setup(void)
{
    GXInit(NULL, 0);
    GXSetViewport(0, 0, 64, 64, 0, 1);
    GXSetScissor(0, 0, 64, 64);
    GXSetDispCopySrc(0, 0, 64, 64);
    GXSetDispCopyDst(64, 64);
    f32 projection[4][4] = {
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, 1, -0.5f },
        { 0, 0, 0, 1 },
    };
    GXSetProjection(projection, GX_ORTHOGRAPHIC);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, 0, GX_DF_NONE,
                  GX_AF_NONE);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetCullMode(GX_CULL_NONE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
    GXSetCopyClear((GXColor) { 0, 0, 0, 255 }, GX_MAX_Z24);
    GXCopyDisp(NULL, GX_TRUE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
}

static u16 test_gx_transform_pixel(const u8* xfb, u32 x, u32 y)
{
    size_t offset = (y * 64 + x) * 2;
    return (u16) ((xfb[offset] << 8) | xfb[offset + 1]);
}

static void test_gx_transform_triangle(u8 matrix, GXColor color, GXBool normal)
{
    const f32 positions[3][2] = {
        { -0.3f, -0.4f },
        { 0.3f, -0.4f },
        { 0.0f, 0.4f },
    };
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    for (u32 i = 0; i < 3; i++) {
        GXMatrixIndex1u8(matrix);
        GXPosition3f32(positions[i][0], positions[i][1], 0.0f);
        if (normal) {
            GXNormal3f32(0.0f, 0.0f, 1.0f);
        } else {
            GXColor4u8(color.r, color.g, color.b, color.a);
        }
    }
    GXEnd();
}

static void test_gx_transform_cases(void)
{
    u8 xfb[64 * 64 * 2];
    f32 left[3][4] = {
        { 1, 0, 0, -0.5f },
        { 0, 1, 0, 0 },
        { 0, 0, 1, 0 },
    };
    f32 right[3][4] = {
        { 1, 0, 0, 0.5f },
        { 0, 1, 0, 0 },
        { 0, 0, 1, 0 },
    };

    /* Loading a second bone matrix must keep the first matrix available.
     * Both draws use the same position values and select different rows. */
    test_gx_transform_setup();
    GXLoadPosMtxImm(left, GX_PNMTX0);
    GXLoadPosMtxImm(right, GX_PNMTX1);
    GXSetCurrentMtx(GX_PNMTX0);
    GXSetVtxDesc(GX_VA_PNMTXIDX, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    test_gx_transform_triangle(GX_PNMTX0, (GXColor) { 255, 0, 0, 255 },
                               GX_FALSE);
    test_gx_transform_triangle(GX_PNMTX1, (GXColor) { 0, 255, 0, 255 },
                               GX_FALSE);
    GXCopyDisp(xfb, GX_FALSE);
    assert(test_gx_transform_pixel(xfb, 16, 32) == 0xf800);
    assert(test_gx_transform_pixel(xfb, 48, 32) == 0x07e0);

    /* Position and normal matrices use the same per-vertex row index.
     * A reversed normal must receive ambient light without the red light. */
    test_gx_transform_setup();
    GXLoadPosMtxImm(left, GX_PNMTX0);
    GXLoadPosMtxImm(right, GX_PNMTX1);
    GXLoadNrmMtxImm(left, GX_PNMTX0);
    f32 reversed[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 } };
    GXLoadNrmMtxImm3x3(reversed, GX_PNMTX1);
    GXLightObj light = { 0 };
    GXInitLightPos(&light, 0.0f, 0.0f, 100000.0f);
    GXInitLightDir(&light, 0.0f, 0.0f, -1.0f);
    GXInitLightColor(&light, (GXColor) { 255, 0, 0, 255 });
    GXInitLightAttn(&light, 1, 0, 0, 1, 0, 0);
    GXLoadLightObjImm(&light, GX_LIGHT0);
    GXSetChanMatColor(GX_COLOR0A0, (GXColor) { 255, 255, 255, 255 });
    GXSetChanAmbColor(GX_COLOR0A0, (GXColor) { 0, 64, 0, 255 });
    GXSetChanCtrl(GX_COLOR0A0, GX_TRUE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT0,
                  GX_DF_CLAMP, GX_AF_NONE);
    GXSetVtxDesc(GX_VA_PNMTXIDX, GX_DIRECT);
    GXSetVtxDesc(GX_VA_NRM, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
    test_gx_transform_triangle(GX_PNMTX0, (GXColor) { 0 }, GX_TRUE);
    test_gx_transform_triangle(GX_PNMTX1, (GXColor) { 0 }, GX_TRUE);
    GXCopyDisp(xfb, GX_FALSE);
    assert((test_gx_transform_pixel(xfb, 16, 32) & 0xf800) == 0xf800);
    assert((test_gx_transform_pixel(xfb, 48, 32) & 0xf800) == 0);
    assert((test_gx_transform_pixel(xfb, 48, 32) & 0x07e0) != 0);

    /* Route the second input coordinate through a post matrix to the second
     * generated coordinate. The left texture half is red, the right blue. */
    test_gx_transform_setup();
    u8 __attribute__((aligned(32))) pixels[32];
    for (u32 i = 0; i < 16; i++) {
        u16 color = i % 4 < 2 ? 0xf800 : 0x001f;
        pixels[i * 2] = (u8) (color >> 8);
        pixels[i * 2 + 1] = (u8) color;
    }
    GXTexObj texture;
    GXInitTexObj(&texture, pixels, 4, 4, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, 0);
    GXLoadTexObj(&texture, GX_TEXMAP0);
    f32 post[3][4] = {
        { 1, 0, 0, 0.5f },
        { 0, 1, 0, 0 },
        { 0, 0, 1, 0 },
    };
    GXLoadTexMtxImm(post, GX_PTTEXMTX0, GX_MTX3x4);
    GXSetNumTexGens(2);
    GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY,
                      GX_FALSE, GX_PTTEXMTX0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD1, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX1, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    const f32 positions[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (u32 i = 0; i < 4; i++) {
        GXPosition3f32(positions[i][0], positions[i][1], 0.0f);
        GXTexCoord2f32(0.0f, 0.5f);
        GXTexCoord2f32(0.25f, 0.5f);
    }
    GXEnd();
    GXCopyDisp(xfb, GX_FALSE);
    assert(test_gx_transform_pixel(xfb, 32, 32) == 0x001f);

    /* These getters read the SDK object representation, including the
     * reversed direction stored by GXInitLightDir. */
    f32 x, y, z;
    GXGetLightDir(&light, &x, &y, &z);
    assert(x == 0.0f && y == 0.0f && z == -1.0f);
    GXInitLightDistAttn(&light, 4.0f, 0.5f, GX_DA_MEDIUM);
    GXGetLightAttnK(&light, &x, &y, &z);
    assert(x == 1.0f && y == 0.125f && z == 0.03125f);
    GXInitLightSpot(&light, 60.0f, GX_SP_COS);
    GXGetLightAttnA(&light, &x, &y, &z);
    assert(fabsf(x + 1.0f) < 0.0001f);
    assert(fabsf(y - 2.0f) < 0.0001f && z == 0.0f);
    GXInitLightDistAttn(&light, 0.0f, 0.5f, GX_DA_STEEP);
    GXGetLightAttnK(&light, &x, &y, &z);
    assert(x == 1.0f && y == 0.0f && z == 0.0f);
}

#endif
