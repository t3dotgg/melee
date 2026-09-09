#ifndef MELEE_NATIVE_GX_COPY_H
#define MELEE_NATIVE_GX_COPY_H

/* Included by gx.c after the EFB storage and pixel update flags. Texture
 * copies retain GX tile layout so the same bytes work with the native texture
 * sampler. */
typedef struct GXSWCopyRect {
    u16 left, top, width, height;
} GXSWCopyRect;

static GXSWCopyRect gx_copy_disp_src, gx_copy_tex_src;
static u16 gx_copy_disp_width, gx_copy_disp_height;
static u16 gx_copy_tex_width, gx_copy_tex_height;
static u16 gx_copy_yscale;
static GXTexFmt gx_copy_tex_format;
static GXBool gx_copy_half_scale;
static GXColor gx_copy_clear;
static u32 gx_copy_clear_z;
static GXFBClamp gx_copy_clamp;
static GXCopyMode gx_copy_field;
static u8 gx_copy_filter[3];
static u8 gx_copy_gamma[256];
static GXCompare gx_copy_poke_alpha_func, gx_copy_poke_z_func;
static u8 gx_copy_poke_alpha_ref, gx_copy_poke_dst_alpha;
static GXAlphaReadMode gx_copy_poke_alpha_read;
static GXBool gx_copy_poke_rgb_update, gx_copy_poke_alpha_update;
static GXBool gx_copy_poke_z_compare, gx_copy_poke_z_update;
static GXBool gx_copy_poke_dst_enable;
static GXBlendMode gx_copy_poke_blend;
static GXBlendFactor gx_copy_poke_src_factor, gx_copy_poke_dst_factor;
static GXLogicOp gx_copy_poke_logic;

static void gx_copy_reset(void)
{
    gx_copy_disp_src = gx_copy_tex_src =
        (GXSWCopyRect) { 0, 0, gx_efb_width, gx_efb_height };
    gx_copy_disp_width = gx_copy_tex_width = gx_efb_width;
    gx_copy_disp_height = gx_copy_tex_height = gx_efb_height;
    gx_copy_yscale = 256;
    gx_copy_tex_format = GX_TF_RGBA8;
    gx_copy_half_scale = GX_FALSE;
    gx_copy_clear = (GXColor) { 0, 0, 0, 255 };
    gx_copy_clear_z = GX_MAX_Z24;
    gx_copy_clamp = GX_CLAMP_TOP | GX_CLAMP_BOTTOM;
    gx_copy_field = GX_COPY_PROGRESSIVE;
    gx_copy_filter[0] = gx_copy_filter[2] = 0;
    gx_copy_filter[1] = 64;
    for (unsigned i = 0; i < 256; i++) {
        gx_copy_gamma[i] = i;
    }
    gx_copy_poke_alpha_func = gx_copy_poke_z_func = GX_ALWAYS;
    gx_copy_poke_alpha_ref = gx_copy_poke_dst_alpha = 0;
    gx_copy_poke_alpha_read = GX_READ_FF;
    gx_copy_poke_rgb_update = gx_copy_poke_alpha_update = GX_TRUE;
    gx_copy_poke_z_compare = gx_copy_poke_z_update = GX_TRUE;
    gx_copy_poke_dst_enable = GX_FALSE;
    gx_copy_poke_blend = GX_BM_NONE;
    gx_copy_poke_src_factor = GX_BL_ZERO;
    gx_copy_poke_dst_factor = GX_BL_ONE;
    gx_copy_poke_logic = GX_LO_SET;
}

static u8 gx_copy_byte(int value)
{
    return value < 0 ? 0 : value > 255 ? 255 : value;
}

static u16 gx_copy_rgb565(GXColor color)
{
    return ((u16) (color.r >> 3) << 11) | ((u16) (color.g >> 2) << 5) |
           (color.b >> 3);
}

static void gx_copy_be16(u8* dest, u16 value)
{
    dest[0] = value >> 8;
    dest[1] = value;
}

static u32 gx_copy_depth24(size_t index)
{
    if (gx_depth == NULL || !isfinite(gx_depth[index])) {
        return GX_MAX_Z24;
    }
    double depth = gx_depth[index];
    if (depth <= 0.0) {
        return 0;
    }
    if (depth >= 1.0) {
        return GX_MAX_Z24;
    }
    return (u32) (depth * GX_MAX_Z24 + 0.5);
}

static GXColor gx_copy_read(const GXSWCopyRect* rect, int x, int y,
                            GXBool depth)
{
    if (gx_efb == NULL || gx_efb_width == 0 || gx_efb_height == 0) {
        return (GXColor) { 0, 0, 0, 255 };
    }
    if (y < rect->top && (gx_copy_clamp & GX_CLAMP_TOP)) {
        y = rect->top;
    }
    int bottom = rect->top + rect->height - 1;
    if (y > bottom && (gx_copy_clamp & GX_CLAMP_BOTTOM)) {
        y = bottom;
    }
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if ((u32) x >= gx_efb_width) {
        x = gx_efb_width - 1;
    }
    if ((u32) y >= gx_efb_height) {
        y = gx_efb_height - 1;
    }
    size_t index = (size_t) y * gx_efb_width + x;
    if (depth) {
        u32 z = gx_copy_depth24(index);
        return (GXColor) { z >> 16, z >> 8, z, 255 };
    }
    return gx_efb[index];
}

static GXColor gx_copy_filtered(const GXSWCopyRect* rect, int x, int y,
                                GXBool depth)
{
    GXColor center = gx_copy_read(rect, x, y, depth);
    if (depth || (gx_copy_filter[0] == 0 && gx_copy_filter[1] == 64 &&
                  gx_copy_filter[2] == 0))
    {
        return center;
    }
    GXColor above = gx_copy_read(rect, x, y - 1, GX_FALSE);
    GXColor below = gx_copy_read(rect, x, y + 1, GX_FALSE);
    unsigned a = gx_copy_filter[0], b = gx_copy_filter[1];
    unsigned c = gx_copy_filter[2];
    return (GXColor) {
        gx_copy_byte((above.r * a + center.r * b + below.r * c) >> 6),
        gx_copy_byte((above.g * a + center.g * b + below.g * c) >> 6),
        gx_copy_byte((above.b * a + center.b * b + below.b * c) >> 6),
        center.a,
    };
}

static void gx_copy_clear_rect(const GXSWCopyRect* rect)
{
    u32 right = (u32) rect->left + rect->width;
    u32 bottom = (u32) rect->top + rect->height;
    if (right > gx_efb_width) {
        right = gx_efb_width;
    }
    if (bottom > gx_efb_height) {
        bottom = gx_efb_height;
    }
    if (gx_efb == NULL) {
        return;
    }
    for (u32 y = rect->top; y < bottom; y++) {
        for (u32 x = rect->left; x < right; x++) {
            size_t index = (size_t) y * gx_efb_width + x;
            if (gx_color_update) {
                gx_efb[index].r = gx_copy_clear.r;
                gx_efb[index].g = gx_copy_clear.g;
                gx_efb[index].b = gx_copy_clear.b;
            }
            if (gx_alpha_update) {
                gx_efb[index].a = gx_copy_clear.a;
            }
            if (gx_z_update && gx_depth != NULL) {
                gx_depth[index] =
                    (f32) ((double) gx_copy_clear_z / GX_MAX_Z24);
            }
        }
    }
}

void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht)
{
    gx_copy_disp_src = (GXSWCopyRect) { left, top, wd, ht };
}

void GXSetDispCopyDst(u16 wd, u16 ht)
{
    gx_copy_disp_width = wd;
    gx_copy_disp_height = ht;
}

u32 GXSetDispCopyYScale(f32 vscale)
{
    if (!isfinite(vscale) || vscale < 1.0F) {
        vscale = 1.0F;
    }
    if (vscale > 256.0F) {
        vscale = 256.0F;
    }
    gx_copy_yscale = (u16) (256.0F / vscale);
    return (u32) gx_copy_disp_src.height * 256 / gx_copy_yscale;
}

void GXSetDispCopyFrame2Field(GXCopyMode mode)
{
    gx_copy_field = mode;
}

void GXSetCopyClamp(GXFBClamp clamp)
{
    gx_copy_clamp = clamp;
}

void GXSetCopyClear(GXColor clear_clr, u32 clear_z)
{
    gx_copy_clear = clear_clr;
    gx_copy_clear_z = clear_z & GX_MAX_Z24;
}

void GXSetCopyFilter(GXBool aa, const u8 sample_pattern[12][2], GXBool vf,
                     const u8 vfilter[7])
{
    /* The EFB has one sample per pixel. The three center taps therefore read
     * the same pixel. Subpixel AA positions require a multisample EFB. */
    (void) aa;
    (void) sample_pattern;
    gx_copy_filter[0] = gx_copy_filter[2] = 0;
    gx_copy_filter[1] = 64;
    if (vf && vfilter != NULL) {
        gx_copy_filter[0] = (vfilter[0] & 63) + (vfilter[1] & 63);
        gx_copy_filter[1] =
            (vfilter[2] & 63) + (vfilter[3] & 63) + (vfilter[4] & 63);
        gx_copy_filter[2] = (vfilter[5] & 63) + (vfilter[6] & 63);
    }
}

void GXSetDispCopyGamma(GXGamma gamma)
{
    f32 exponent = gamma == GX_GM_1_7   ? 1.0F / 1.7F
                   : gamma == GX_GM_2_2 ? 1.0F / 2.2F
                                        : 1.0F;
    for (unsigned i = 0; i < 256; i++) {
        gx_copy_gamma[i] = gx_copy_byte(
            (int) (255.0F * powf((f32) i / 255.0F, exponent) + 0.5F));
    }
}

void GXCopyDisp(void* dest, GXBool clear)
{
    gx_ensure_efb();
    gx_trace_frame();
    unsigned width = gx_copy_disp_src.width;
    if (width > gx_copy_disp_width) {
        width = gx_copy_disp_width;
    }
    unsigned height = (unsigned) gx_copy_disp_src.height * 256 /
                      (gx_copy_yscale != 0 ? gx_copy_yscale : 256);
    unsigned step = gx_copy_field == GX_COPY_PROGRESSIVE ? 1 : 2;
    unsigned first = gx_copy_field == GX_COPY_INTLC_ODD ? 1 : 0;
    if (height > gx_copy_disp_height) {
        height = gx_copy_disp_height;
    }
    if (dest != NULL && gx_copy_disp_src.height != 0) {
        u8* output = dest;
        for (unsigned y = first; y < height; y += step) {
            int source_y = gx_copy_disp_src.top + y * gx_copy_yscale / 256;
            for (unsigned x = 0; x < width; x++) {
                GXColor color = gx_copy_filtered(&gx_copy_disp_src,
                                                 gx_copy_disp_src.left + x,
                                                 source_y, GX_FALSE);
                color.r = gx_copy_gamma[color.r];
                color.g = gx_copy_gamma[color.g];
                color.b = gx_copy_gamma[color.b];
                /* display.m consumes RGB565 bytes, not the console's YUYV. */
                gx_copy_be16(output +
                                 ((size_t) y * gx_copy_disp_width + x) * 2,
                             gx_copy_rgb565(color));
            }
        }
    }
    if (clear) {
        gx_copy_clear_rect(&gx_copy_disp_src);
    }
}

void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht)
{
    gx_copy_tex_src = (GXSWCopyRect) { left, top, wd, ht };
}

void GXSetTexCopyDst(u16 wd, u16 ht, GXTexFmt fmt, GXBool mipmap)
{
    gx_copy_tex_width = wd;
    gx_copy_tex_height = ht;
    gx_copy_tex_format = fmt;
    /* GX calls this mipmap, but one copy makes just one half-size level. */
    gx_copy_half_scale = mipmap;
}

static GXBool gx_copy_tile_size(GXTexFmt format, unsigned* width,
                                unsigned* height, unsigned* bytes)
{
    *bytes = 32;
    switch (format) {
    case GX_TF_I4:
    case GX_CTF_R4:
    case GX_CTF_Z4:
        *width = *height = 8;
        break;
    case GX_TF_I8:
    case GX_TF_IA4:
    case GX_CTF_RA4:
    case GX_CTF_A8:
    case GX_CTF_R8:
    case GX_CTF_G8:
    case GX_CTF_B8:
    case GX_TF_Z8:
    case GX_CTF_Z8M:
    case GX_CTF_Z8L:
        *width = 8;
        *height = 4;
        break;
    case GX_TF_RGBA8:
    case GX_TF_Z24X8:
        *bytes = 64;
        *width = *height = 4;
        break;
    case GX_TF_IA8:
    case GX_TF_RGB565:
    case GX_TF_RGB5A3:
    case GX_CTF_RA8:
    case GX_CTF_RG8:
    case GX_CTF_GB8:
    case GX_TF_Z16:
    case GX_CTF_Z16L:
        *width = *height = 4;
        break;
    default:
        /* CMPR, indexed formats, and YUVA copies are not implemented. */
        return GX_FALSE;
    }
    return GX_TRUE;
}

static GXColor gx_copy_tex_sample(unsigned x, unsigned y)
{
    unsigned scale = gx_copy_half_scale ? 2 : 1;
    GXBool depth = (gx_copy_tex_format & _GX_TF_ZTF) != 0;
    unsigned r = 0, g = 0, b = 0, a = 0;
    for (unsigned dy = 0; dy < scale; dy++) {
        for (unsigned dx = 0; dx < scale; dx++) {
            unsigned sx = x * scale + dx, sy = y * scale + dy;
            if (sx >= gx_copy_tex_src.width) {
                sx = gx_copy_tex_src.width - 1;
            }
            if (sy >= gx_copy_tex_src.height) {
                sy = gx_copy_tex_src.height - 1;
            }
            GXColor color =
                gx_copy_filtered(&gx_copy_tex_src, gx_copy_tex_src.left + sx,
                                 gx_copy_tex_src.top + sy, depth);
            r += color.r;
            g += color.g;
            b += color.b;
            a += color.a;
        }
    }
    unsigned samples = scale * scale;
    return (GXColor) { r / samples, g / samples, b / samples, a / samples };
}

static void gx_copy_texel(u8* tile, unsigned pixel, GXColor color)
{
    unsigned value;
    /* Intensity copies use the console's limited-range luma conversion.
     * Copy-only R formats keep red directly, as required by HSD shadows. */
    unsigned intensity =
        (4096 + 66 * color.r + 129 * color.g + 25 * color.b) >> 8;
    switch (gx_copy_tex_format) {
    case GX_TF_I4:
    case GX_CTF_R4:
    case GX_CTF_Z4:
        value = gx_copy_tex_format == GX_TF_I4 ? intensity : color.r;
        if ((pixel & 1) == 0) {
            tile[pixel / 2] = value & 0xF0;
        } else {
            tile[pixel / 2] |= value >> 4;
        }
        break;
    case GX_TF_I8:
        tile[pixel] = intensity;
        break;
    case GX_CTF_R8:
    case GX_TF_Z8:
        tile[pixel] = color.r;
        break;
    case GX_CTF_G8:
    case GX_CTF_Z8M:
        tile[pixel] = color.g;
        break;
    case GX_CTF_B8:
    case GX_CTF_Z8L:
        tile[pixel] = color.b;
        break;
    case GX_CTF_A8:
        tile[pixel] = color.a;
        break;
    case GX_TF_IA4:
    case GX_CTF_RA4:
        value = gx_copy_tex_format == GX_TF_IA4 ? intensity : color.r;
        tile[pixel] = (color.a & 0xF0) | (value >> 4);
        break;
    case GX_TF_IA8:
    case GX_CTF_RA8:
        tile[pixel * 2] = color.a;
        tile[pixel * 2 + 1] =
            gx_copy_tex_format == GX_TF_IA8 ? intensity : color.r;
        break;
    case GX_TF_RGB565:
        gx_copy_be16(tile + pixel * 2, gx_copy_rgb565(color));
        break;
    case GX_TF_RGB5A3:
        value = color.a >= 224
                    ? 0x8000 | ((color.r >> 3) << 10) | ((color.g >> 3) << 5) |
                          (color.b >> 3)
                    : ((color.a >> 5) << 12) | ((color.r >> 4) << 8) |
                          ((color.g >> 4) << 4) | (color.b >> 4);
        gx_copy_be16(tile + pixel * 2, value);
        break;
    case GX_TF_RGBA8:
    case GX_TF_Z24X8:
        tile[pixel * 2] = color.a;
        tile[pixel * 2 + 1] = color.r;
        tile[32 + pixel * 2] = color.g;
        tile[33 + pixel * 2] = color.b;
        break;
    case GX_CTF_RG8:
    case GX_TF_Z16:
        tile[pixel * 2] = color.g;
        tile[pixel * 2 + 1] = color.r;
        break;
    case GX_CTF_GB8:
    case GX_CTF_Z16L:
        tile[pixel * 2] = color.b;
        tile[pixel * 2 + 1] = color.g;
        break;
    default:
        break;
    }
}

void GXCopyTex(void* dest, GXBool clear)
{
    gx_ensure_efb();
    unsigned tile_width, tile_height, tile_bytes;
    if (dest != NULL && gx_copy_tex_width != 0 && gx_copy_tex_height != 0 &&
        gx_copy_tex_src.width != 0 && gx_copy_tex_src.height != 0 &&
        gx_copy_tile_size(gx_copy_tex_format, &tile_width, &tile_height,
                          &tile_bytes))
    {
        unsigned tiles_x = (gx_copy_tex_width + tile_width - 1) / tile_width;
        unsigned tiles_y =
            (gx_copy_tex_height + tile_height - 1) / tile_height;
        for (unsigned ty = 0; ty < tiles_y; ty++) {
            for (unsigned tx = 0; tx < tiles_x; tx++) {
                u8* tile =
                    (u8*) dest + ((size_t) ty * tiles_x + tx) * tile_bytes;
                for (unsigned y = 0; y < tile_height; y++) {
                    for (unsigned x = 0; x < tile_width; x++) {
                        GXColor color = gx_copy_tex_sample(
                            tx * tile_width + x, ty * tile_height + y);
                        gx_copy_texel(tile, y * tile_width + x, color);
                    }
                }
            }
        }
    }
    if (clear) {
        gx_copy_clear_rect(&gx_copy_tex_src);
    }
}

static GXBool gx_copy_compare(GXCompare func, u32 value, u32 ref)
{
    switch (func) {
    case GX_NEVER:
        return GX_FALSE;
    case GX_LESS:
        return value < ref;
    case GX_EQUAL:
        return value == ref;
    case GX_LEQUAL:
        return value <= ref;
    case GX_GREATER:
        return value > ref;
    case GX_NEQUAL:
        return value != ref;
    case GX_GEQUAL:
        return value >= ref;
    case GX_ALWAYS:
        return GX_TRUE;
    default:
        return GX_FALSE;
    }
}

void GXPeekARGB(u16 x, u16 y, u32* color)
{
    gx_ensure_efb();
    if (color == NULL) {
        return;
    }
    *color = 0;
    if (gx_efb == NULL || x >= gx_efb_width || y >= gx_efb_height) {
        return;
    }
    GXColor value = gx_efb[(size_t) y * gx_efb_width + x];
    if (gx_copy_poke_alpha_read == GX_READ_00) {
        value.a = 0;
    }
    if (gx_copy_poke_alpha_read == GX_READ_FF) {
        value.a = 255;
    }
    *color = ((u32) value.a << 24) | ((u32) value.r << 16) |
             ((u32) value.g << 8) | value.b;
}

void GXPeekZ(u16 x, u16 y, u32* z)
{
    gx_ensure_efb();
    if (z != NULL) {
        *z = x < gx_efb_width && y < gx_efb_height
                 ? gx_copy_depth24((size_t) y * gx_efb_width + x)
                 : GX_MAX_Z24;
    }
}

static u8 gx_copy_logic(u8 src, u8 dst)
{
    switch (gx_copy_poke_logic) {
    case GX_LO_CLEAR:
        return 0;
    case GX_LO_AND:
        return src & dst;
    case GX_LO_REVAND:
        return src & ~dst;
    case GX_LO_COPY:
        return src;
    case GX_LO_INVAND:
        return ~src & dst;
    case GX_LO_NOOP:
        return dst;
    case GX_LO_XOR:
        return src ^ dst;
    case GX_LO_OR:
        return src | dst;
    case GX_LO_NOR:
        return ~(src | dst);
    case GX_LO_EQUIV:
        return ~(src ^ dst);
    case GX_LO_INV:
        return ~dst;
    case GX_LO_REVOR:
        return src | ~dst;
    case GX_LO_INVCOPY:
        return ~src;
    case GX_LO_INVOR:
        return ~src | dst;
    case GX_LO_NAND:
        return ~(src & dst);
    case GX_LO_SET:
        return 255;
    default:
        return src;
    }
}

static unsigned gx_copy_blend_factor(GXBlendFactor factor, u8 color,
                                     u8 src_alpha, u8 dst_alpha)
{
    switch (factor) {
    case GX_BL_ZERO:
        return 0;
    case GX_BL_ONE:
        return 255;
    case GX_BL_SRCCLR:
        return color;
    case GX_BL_INVSRCCLR:
        return 255 - color;
    case GX_BL_SRCALPHA:
        return src_alpha;
    case GX_BL_INVSRCALPHA:
        return 255 - src_alpha;
    case GX_BL_DSTALPHA:
        return dst_alpha;
    case GX_BL_INVDSTALPHA:
        return 255 - dst_alpha;
    default:
        return 0;
    }
}

static u8 gx_copy_blend_channel(u8 src, u8 dst, u8 src_alpha, u8 dst_alpha)
{
    if (gx_copy_poke_blend == GX_BM_LOGIC) {
        return gx_copy_logic(src, dst);
    }
    if (gx_copy_poke_blend == GX_BM_SUBTRACT) {
        return gx_copy_byte(dst - src);
    }
    if (gx_copy_poke_blend != GX_BM_BLEND) {
        return src;
    }
    unsigned sf = gx_copy_blend_factor(gx_copy_poke_src_factor, dst, src_alpha,
                                       dst_alpha);
    unsigned df = gx_copy_blend_factor(gx_copy_poke_dst_factor, src, src_alpha,
                                       dst_alpha);
    return gx_copy_byte((src * sf + dst * df + 127) / 255);
}

void GXPokeARGB(u16 x, u16 y, u32 color)
{
    gx_ensure_efb();
    if (gx_efb == NULL || x >= gx_efb_width || y >= gx_efb_height) {
        return;
    }
    GXColor src = { color >> 16, color >> 8, color, color >> 24 };
    if (!gx_copy_compare(gx_copy_poke_alpha_func, src.a,
                         gx_copy_poke_alpha_ref))
    {
        return;
    }
    GXColor* dst = &gx_efb[(size_t) y * gx_efb_width + x];
    if (gx_copy_poke_rgb_update) {
        dst->r = gx_copy_blend_channel(src.r, dst->r, src.a, dst->a);
        dst->g = gx_copy_blend_channel(src.g, dst->g, src.a, dst->a);
        dst->b = gx_copy_blend_channel(src.b, dst->b, src.a, dst->a);
    }
    if (gx_copy_poke_alpha_update) {
        dst->a = gx_copy_poke_dst_enable ? gx_copy_poke_dst_alpha : src.a;
    }
}

void GXPokeZ(u16 x, u16 y, u32 z)
{
    gx_ensure_efb();
    if (gx_depth == NULL || x >= gx_efb_width || y >= gx_efb_height ||
        !gx_copy_poke_z_update)
    {
        return;
    }
    size_t index = (size_t) y * gx_efb_width + x;
    z &= GX_MAX_Z24;
    if (!gx_copy_poke_z_compare ||
        gx_copy_compare(gx_copy_poke_z_func, z, gx_copy_depth24(index)))
    {
        gx_depth[index] = (f32) ((double) z / GX_MAX_Z24);
    }
}

void GXPokeAlphaMode(GXCompare func, u8 threshold)
{
    gx_copy_poke_alpha_func = func;
    gx_copy_poke_alpha_ref = threshold;
}

void GXPokeAlphaRead(GXAlphaReadMode mode)
{
    gx_copy_poke_alpha_read = mode;
}

void GXPokeAlphaUpdate(GXBool update_enable)
{
    gx_copy_poke_alpha_update = update_enable;
}

void GXPokeBlendMode(GXBlendMode type, GXBlendFactor src_factor,
                     GXBlendFactor dst_factor, GXLogicOp op)
{
    gx_copy_poke_blend = type;
    gx_copy_poke_src_factor = src_factor;
    gx_copy_poke_dst_factor = dst_factor;
    gx_copy_poke_logic = op;
}

void GXPokeColorUpdate(GXBool update_enable)
{
    gx_copy_poke_rgb_update = update_enable;
}

void GXPokeDither(GXBool dither)
{
    /* Dither has no effect on the native eight-bit color channels. */
    (void) dither;
}

void GXPokeDstAlpha(GXBool enable, u8 alpha)
{
    gx_copy_poke_dst_enable = enable;
    gx_copy_poke_dst_alpha = alpha;
}

void GXPokeZMode(GXBool compare_enable, GXCompare func, GXBool update_enable)
{
    gx_copy_poke_z_compare = compare_enable;
    gx_copy_poke_z_func = func;
    gx_copy_poke_z_update = update_enable;
}

#endif
