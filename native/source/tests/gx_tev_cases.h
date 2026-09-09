#ifndef MELEE_NATIVE_GX_TEV_CASES_H
#define MELEE_NATIVE_GX_TEV_CASES_H

#include <dolphin/gx.h>

static void gx_tev_test_setup(void)
{
    GXInit(NULL, 0);
    GXSetViewport(0, 0, 8, 8, 0, 1);
    GXSetScissor(0, 0, 8, 8);
    GXSetCullMode(GX_CULL_NONE);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetNumTexGens(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumTevStages(1);
    GXSetDispCopySrc(0, 0, 8, 8);
    GXSetDispCopyDst(8, 8);
}

static u16 gx_tev_test_pixel(GXColor color)
{
    u8 xfb[8 * 8 * 2] = { 0 };
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition2f32(-1.0F, -1.0F);
    GXColor4u8(color.r, color.g, color.b, color.a);
    GXTexCoord2f32(0.0F, 0.0F);
    GXPosition2f32(1.0F, -1.0F);
    GXColor4u8(color.r, color.g, color.b, color.a);
    GXTexCoord2f32(0.0F, 0.0F);
    GXPosition2f32(1.0F, 1.0F);
    GXColor4u8(color.r, color.g, color.b, color.a);
    GXTexCoord2f32(0.0F, 0.0F);
    GXPosition2f32(-1.0F, 1.0F);
    GXColor4u8(color.r, color.g, color.b, color.a);
    GXTexCoord2f32(0.0F, 0.0F);
    GXEnd();
    GXCopyDisp(xfb, GX_FALSE);
    return (u16) ((xfb[(4 * 8 + 4) * 2] << 8) | xfb[(4 * 8 + 4) * 2 + 1]);
}

static void test_gx_tev_cases(void)
{
    const GXColor white = { 255, 255, 255, 255 };
    gx_tev_test_setup();

    /* HSD stores intermediate colors and alpha in different registers.
     * The final stage can write any register and still supply the pixel. */
    GXSetNumTevStages(2);
    GXSetTevColor(GX_TEVREG2, (GXColor) { 0, 0, 64, 0 });
    GXSetTevKColor(GX_KCOLOR0, (GXColor) { 255, 0, 0, 64 });
    GXSetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
    GXSetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO,
                    GX_CC_KONST);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
                    GX_CA_KONST);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_TRUE, GX_TEVREG0);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_TRUE, GX_TEVREG1);
    GXSetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_1_2);
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_C0, GX_CC_KONST, GX_CC_C2);
    GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_TRUE, GX_TEVREG2);
    GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
                    GX_CA_A1);
    assert(gx_tev_test_pixel(white) == 0x8008);
    /* Turn the independent alpha result into RGB so the XFB can check it. */
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO,
                    GX_CC_A1);
    assert(gx_tev_test_pixel(white) == 0x4208);

    /* Color and alpha read the old register before the stage writes it. */
    gx_tev_test_setup();
    GXSetTevColor(GX_TEVREG0, (GXColor) { 0, 0, 0, 64 });
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO,
                    GX_CC_A0);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
                    GX_CA_KONST);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_TRUE, GX_TEVREG0);
    assert(gx_tev_test_pixel(white) == 0x4208);

    /* Negative intermediate values survive an unclamped stage. */
    gx_tev_test_setup();
    GXSetNumTevStages(2);
    GXSetTevColorS10(GX_TEVREG0, (GXColorS10) { -64, -64, -64, -64 });
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO,
                    GX_CC_C0);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_FALSE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ONE, GX_CC_ZERO, GX_CC_ZERO,
                    GX_CC_CPREV);
    assert(gx_tev_test_pixel(white) == 0xbdf7);
    /* A/B/C read only the low byte. D retains all signed bits. */
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_CPREV, GX_CC_ZERO, GX_CC_ZERO,
                    GX_CC_ZERO);
    assert(gx_tev_test_pixel(white) == 0xc618);

    /* Scale must apply before rounding. Bias applies to the signed D input. */
    gx_tev_test_setup();
    GXSetTevColor(GX_TEVREG0, (GXColor) { 7, 7, 7, 0 });
    GXSetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_1_2);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_C0, GX_CC_KONST,
                    GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2,
                    GX_TRUE, GX_TEVPREV);
    assert(gx_tev_test_pixel(white) == 0x0020);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_SUB, GX_TB_ADDHALF, GX_CS_SCALE_4,
                    GX_TRUE, GX_TEVPREV);
    assert(gx_tev_test_pixel(white) == 0xffff);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ADDHALF, GX_CS_DIVIDE_2,
                    GX_TRUE, GX_TEVPREV);
    assert(gx_tev_test_pixel(white) == 0x4208);

    /* Packed comparisons prioritize green over red, then blue over green.
     * RGB8 compares each component separately. */
    gx_tev_test_setup();
    GXSetTevColor(GX_TEVREG0, (GXColor) { 255, 0, 1, 0 });
    GXSetTevColor(GX_TEVREG1, (GXColor) { 0, 1, 0, 0 });
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_C1, GX_CC_HALF, GX_CC_ZERO);
    const GXTevOp comparisons[] = {
        GX_TEV_COMP_R8_GT,   GX_TEV_COMP_GR16_GT, GX_TEV_COMP_BGR24_GT,
        GX_TEV_COMP_RGB8_GT, GX_TEV_COMP_R8_EQ,   GX_TEV_COMP_RGB8_EQ,
    };
    const u16 expected[] = { 0x8410, 0x0000, 0x8410, 0x8010, 0x0000, 0x0000 };
    for (unsigned i = 0; i < sizeof comparisons / sizeof comparisons[0]; i++) {
        GXSetTevColorOp(GX_TEVSTAGE0, comparisons[i], GX_TB_ZERO,
                        GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        assert(gx_tev_test_pixel(white) == expected[i]);
    }
    GXSetTevColor(GX_TEVREG1, (GXColor) { 255, 1, 1, 0 });
    assert(gx_tev_test_pixel(white) == 0x8010);

    /* Alpha comparisons use alpha operands for A8 and color operands for
     * packed modes. A later stage makes the alpha result visible in RGB. */
    GXSetNumTevStages(2);
    GXSetTevColor(GX_TEVREG0, (GXColor) { 1, 0, 0, 200 });
    GXSetTevColor(GX_TEVREG1, (GXColor) { 2, 0, 0, 100 });
    GXSetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_1_2);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_A0, GX_CA_A1, GX_CA_KONST, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_COMP_A8_GT, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO,
                    GX_CC_APREV);
    assert(gx_tev_test_pixel(white) == 0x8410);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_1,
                    GX_TRUE, GX_TEVPREV);
    assert(gx_tev_test_pixel(white) == 0x0000);

    /* Konst component selectors address a channel in any of four colors. */
    gx_tev_test_setup();
    GXSetTevKColor(GX_KCOLOR3, (GXColor) { 12, 73, 150, 210 });
    GXSetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K3_G);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO,
                    GX_CC_KONST);
    assert(gx_tev_test_pixel(white) == 0x4a49);
    GXSetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_7_8);
    assert(gx_tev_test_pixel(white) == 0xdefb);

    /* Texture and raster swaps are independent. GX_BLEND blends toward one. */
    gx_tev_test_setup();
    u8 texels[32] = { 0 };
    for (unsigned i = 0; i < sizeof texels; i += 2) {
        texels[i] = 0xf8;
    }
    GXTexObj texture;
    GXInitTexObj(&texture, texels, 4, 4, GX_TF_RGB565, GX_CLAMP, GX_CLAMP,
                 GX_FALSE);
    GXLoadTexObj(&texture, GX_TEXMAP0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GXSetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_BLUE, GX_CH_GREEN, GX_CH_RED,
                          GX_CH_ALPHA);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP1);
    assert(gx_tev_test_pixel(white) == 0x001f);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP1, GX_TEV_SWAP0);
    assert(gx_tev_test_pixel((GXColor) { 255, 0, 0, 255 }) == 0x001f);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetTevOp(GX_TEVSTAGE0, GX_BLEND);
    assert(gx_tev_test_pixel((GXColor) { 0, 255, 0, 255 }) == 0xffe0);
    GXLoadTexObj(&texture, GX_TEXMAP3);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP3, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    assert(gx_tev_test_pixel(white) == 0xf800);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0,
                  (GXTexMapID) (GX_TEXMAP3 | GX_TEX_DISABLE), GX_COLOR0A0);
    assert(gx_tev_test_pixel(white) == 0xffff);
}

#endif
