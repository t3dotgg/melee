#ifndef MELEE_NATIVE_GX_TEXTURE_CASES_H
#define MELEE_NATIVE_GX_TEXTURE_CASES_H

#include <assert.h>
#include <string.h>

#include <dolphin/gx.h>

static void gx_texture_test_setup(void)
{
    GXInit(NULL, 0);
    GXSetViewport(0, 0, 64, 64, 0, 1);
    GXSetScissor(0, 0, 64, 64);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GXPokeAlphaRead(GX_READ_NONE);
}

static u32 gx_texture_test_sample(f32 s, f32 t)
{
    GXBegin(GX_POINTS, GX_VTXFMT0, 1);
    GXPosition2f32(0, 0);
    GXTexCoord2f32(s, t);
    u32 pixel;
    GXPeekARGB(32, 32, &pixel);
    return pixel;
}

static void test_gx_texture_cases(void)
{
    GXTexObj texture;
    u8 data[64] = { 0 };
    gx_texture_test_setup();
    data[0] = 0xf1;
    GXInitTexObj(&texture, data, 8, 8, GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXLoadTexObj(&texture, GX_TEXMAP0);
    assert(gx_texture_test_sample(0, 0) == 0xffffffff);
    assert(gx_texture_test_sample(0.125f, 0) == 0x11111111);

    data[0] = 0x53;
    GXInitTexObj(&texture, data, 8, 4, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXLoadTexObj(&texture, GX_TEXMAP0);
    assert(gx_texture_test_sample(0, 0) == 0x53535353);
    data[0] = 0xa3;
    GXInitTexObj(&texture, data, 8, 4, GX_TF_IA4, GX_CLAMP, GX_CLAMP,
                 GX_FALSE);
    GXLoadTexObj(&texture, GX_TEXMAP0);
    assert(gx_texture_test_sample(0, 0) == 0xaa333333);
    data[0] = 0x80;
    data[1] = 0x20;
    GXInitTexObj(&texture, data, 4, 4, GX_TF_IA8, GX_CLAMP, GX_CLAMP,
                 GX_FALSE);
    GXLoadTexObj(&texture, GX_TEXMAP0);
    assert(gx_texture_test_sample(0, 0) == 0x80202020);

    /* Wrap fields must remain independent when the object has mipmaps. */
    data[0] = 0xff;
    data[1] = 0;
    data[2] = 0;
    data[3] = 0;
    GXInitTexObj(&texture, data, 4, 4, GX_TF_I8, GX_CLAMP, GX_REPEAT, GX_TRUE);
    GXLoadTexObj(&texture, GX_TEXMAP0);
    assert(gx_texture_test_sample(-1, 0) == 0xffffffff);
    assert(gx_texture_test_sample(2, 0) == 0);

    /* Linear filtering samples around texel centers. */
    GXInitTexObjLOD(&texture, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE,
                    GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&texture, GX_TEXMAP0);
    assert(gx_texture_test_sample(0.25f, 0.125f) == 0x80808080);

    /* CMPR keeps the average endpoint RGB when selector 3 is transparent. */
    memset(data, 0, sizeof data);
    data[1] = 0x1f;
    data[2] = 0xf8;
    data[4] = 0x1b;
    GXInitTexObj(&texture, data, 8, 8, GX_TF_CMPR, GX_CLAMP, GX_CLAMP,
                 GX_FALSE);
    GXLoadTexObj(&texture, GX_TEXMAP0);
    assert(gx_texture_test_sample(0, 0) == 0xff0000ff);
    assert(gx_texture_test_sample(0.125f, 0) == 0xffff0000);
    assert(gx_texture_test_sample(0.25f, 0) == 0xff7f007f);
    assert(gx_texture_test_sample(0.375f, 0) == 0x007f007f);

    /* C14X2 discards the two unused high bits before palette lookup. */
    u8 palette[4] = { 0, 0, 0xf8, 0 };
    GXTlutObj tlut;
    GXInitTlutObj(&tlut, palette, GX_TL_RGB565, 2);
    GXLoadTlut(&tlut, 0);
    data[0] = 0xc0;
    data[1] = 1;
    GXInitTexObjCI(&texture, data, 4, 4, GX_TF_C14X2, GX_CLAMP, GX_CLAMP,
                   GX_FALSE, 0);
    GXLoadTexObj(&texture, GX_TEXMAP0);
    assert(gx_texture_test_sample(0, 0) == 0xffff0000);

    /* Vertex RGBA4 is a big endian RGBA word. */
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA4, 0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXBegin(GX_POINTS, GX_VTXFMT0, 1);
    GXPosition2f32(0, 0);
    GXColor1u16(0xf123);
    u32 pixel;
    GXPeekARGB(32, 32, &pixel);
    assert(pixel == 0x33ff1122);
}

#endif
