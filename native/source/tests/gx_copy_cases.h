#ifndef MELEE_NATIVE_GX_COPY_CASES_H
#define MELEE_NATIVE_GX_COPY_CASES_H

static void test_gx_copy_cases(void)
{
    u8 texture[256];
    u8 display[16 * 8 * 2];
    u32 color, depth;
    GXInit(NULL, 0);
    GXPokeAlphaRead(GX_READ_NONE);
    GXPokeARGB(2, 3, 0x80402010);
    GXPokeZ(2, 3, 0x123456);
    GXPeekARGB(2, 3, &color);
    GXPeekZ(2, 3, &depth);
    assert(color == 0x80402010);
    assert(depth == 0x123456);

    /* Distinct channels test byte order, luma conversion, and depth lanes. */
    const struct {
        GXTexFmt format;
        u8 first, second;
    } cases[] = {
        { GX_TF_I4, 0x33, 0x33 },     { GX_TF_I8, 0x32, 0x32 },
        { GX_TF_IA4, 0x83, 0x83 },    { GX_TF_IA8, 0x80, 0x32 },
        { GX_TF_RGB565, 0x41, 0x02 }, { GX_TF_RGB5A3, 0x44, 0x21 },
        { GX_TF_RGBA8, 0x80, 0x40 },  { GX_CTF_R4, 0x44, 0x44 },
        { GX_CTF_RA4, 0x84, 0x84 },   { GX_CTF_RA8, 0x80, 0x40 },
        { GX_CTF_A8, 0x80, 0x80 },    { GX_CTF_R8, 0x40, 0x40 },
        { GX_CTF_G8, 0x20, 0x20 },    { GX_CTF_B8, 0x10, 0x10 },
        { GX_CTF_RG8, 0x20, 0x40 },   { GX_CTF_GB8, 0x10, 0x20 },
        { GX_TF_Z8, 0x12, 0x12 },     { GX_TF_Z16, 0x34, 0x12 },
        { GX_TF_Z24X8, 0xFF, 0x12 },  { GX_CTF_Z4, 0x11, 0x11 },
        { GX_CTF_Z8M, 0x34, 0x34 },   { GX_CTF_Z8L, 0x56, 0x56 },
        { GX_CTF_Z16L, 0x56, 0x34 },
    };
    GXSetTexCopySrc(2, 3, 1, 1);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        memset(texture, 0xCC, sizeof texture);
        GXSetTexCopyDst(1, 1, cases[i].format, GX_FALSE);
        GXCopyTex(texture, GX_FALSE);
        assert(texture[0] == cases[i].first);
        assert(texture[1] == cases[i].second);
        size_t bytes =
            cases[i].format == GX_TF_RGBA8 || cases[i].format == GX_TF_Z24X8
                ? 64
                : 32;
        assert(texture[bytes] == 0xCC);
    }

    /* RGBA8 stores AR and GB planes in each 4x4 tile. The right and lower
     * tiles must stay distinct. Partial edge tiles must stay inside
     * allocation. */
    GXPokeARGB(0, 0, 0xFF112233);
    GXPokeARGB(4, 0, 0xFF445566);
    GXPokeARGB(0, 4, 0xFF778899);
    GXSetTexCopySrc(0, 0, 5, 5);
    GXSetTexCopyDst(5, 5, GX_TF_RGBA8, GX_FALSE);
    GXCopyTex(texture, GX_FALSE);
    assert(texture[0] == 0xFF && texture[1] == 0x11);
    assert(texture[32] == 0x22 && texture[33] == 0x33);
    assert(texture[64 + 1] == 0x44 && texture[64 + 32] == 0x55);
    assert(texture[128 + 1] == 0x77 && texture[128 + 33] == 0x99);
    GXSetTexCopyDst(1, 1, GX_TF_RGB5A3, GX_FALSE);
    GXCopyTex(texture, GX_FALSE);
    assert(texture[0] == 0x88 && texture[1] == 0x86);

    /* Refraction halves each dimension and averages a 2x2 group. It must not
     * build a full mip chain or read only the first pixel of each group. */
    GXPokeARGB(8, 8, 0xFF000000);
    GXPokeARGB(9, 8, 0xFFFF0000);
    GXPokeARGB(8, 9, 0xFF00FF00);
    GXPokeARGB(9, 9, 0xFF0000FF);
    GXSetTexCopySrc(8, 8, 2, 2);
    GXSetTexCopyDst(1, 1, GX_TF_RGB565, GX_TRUE);
    memset(texture, 0xCC, sizeof texture);
    GXCopyTex(texture, GX_FALSE);
    assert(texture[0] == 0x39 && texture[1] == 0xE7);
    assert(texture[32] == 0xCC);

    /* Display and texture copy rectangles are independent. A copy clears
     * after reading, only inside its source rectangle, and obeys write masks.
     */
    GXSetDispCopySrc(2, 3, 1, 1);
    GXSetDispCopyDst(16, 1);
    GXSetCopyClear((GXColor) { 0, 255, 0, 0x11 }, 0x654321);
    memset(display, 0, sizeof display);
    GXCopyDisp(display, GX_TRUE);
    assert(display[0] == 0x41 && display[1] == 0x02);
    GXPeekARGB(2, 3, &color);
    GXPeekZ(2, 3, &depth);
    assert(color == 0x1100FF00 && depth == 0x654321);
    GXPeekARGB(0, 0, &color);
    assert(color == 0xFF112233);
    GXSetColorUpdate(GX_FALSE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetZMode(GX_TRUE, GX_ALWAYS, GX_FALSE);
    GXSetCopyClear((GXColor) { 255, 0, 0, 255 }, GX_MAX_Z24);
    GXCopyDisp(NULL, GX_TRUE);
    GXPeekARGB(2, 3, &color);
    GXPeekZ(2, 3, &depth);
    assert(color == 0x1100FF00 && depth == 0x654321);
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_TRUE);
    GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);

    /* Display scale is based on source height and includes the final row. */
    for (unsigned x = 0; x < 16; x++) {
        GXPokeARGB(x, 10, 0xFFFF0000);
        GXPokeARGB(x, 11, 0xFF00FF00);
    }
    GXSetDispCopySrc(0, 10, 16, 2);
    assert(GXSetDispCopyYScale(2.0F) == 4);
    GXSetDispCopyDst(16, 4);
    memset(display, 0, sizeof display);
    GXCopyDisp(display, GX_FALSE);
    assert(display[0] == 0xF8 && display[32] == 0xF8);
    assert(display[64] == 0x07 && display[65] == 0xE0);
    assert(display[96] == 0x07 && display[97] == 0xE0);

    GXPokeAlphaMode(GX_GREATER, 128);
    GXPokeARGB(2, 3, 0x80FFFFFF);
    GXPeekARGB(2, 3, &color);
    assert(color == 0x1100FF00);
    GXPokeAlphaMode(GX_ALWAYS, 0);
    GXPokeColorUpdate(GX_FALSE);
    GXPokeDstAlpha(GX_TRUE, 0x77);
    GXPokeARGB(2, 3, 0xFFFFFFFF);
    GXPeekARGB(2, 3, &color);
    assert(color == 0x7700FF00);
    GXPokeZMode(GX_TRUE, GX_LESS, GX_TRUE);
    GXPokeZ(2, 3, GX_MAX_Z24);
    GXPeekZ(2, 3, &depth);
    assert(depth == 0x654321);
    GXPokeZ(2, 3, 0);
    GXPeekZ(2, 3, &depth);
    assert(depth == 0);
    GXInit(NULL, 0);
}

#endif
