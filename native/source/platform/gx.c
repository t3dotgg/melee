#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gx_vertex.h"
#include <dolphin/gx.h>

_Static_assert(sizeof(((GXTexObj*) 0)->dummy[0]) == sizeof(void*),
               "native GX texture state must keep host pointers");
static GXFifoObj gx_fifo;
static GXFifoObj *gx_cpu_fifo, *gx_gp_fifo;
static GXDrawDoneCallback gx_done_cb;
static GXDrawSyncCallback gx_sync_cb;
static u16 gx_draw_token;
static f32 gx_projection[7];
static f32 gx_viewport[6] = { 0, 0, 640, 480, 0, 1 };
static u32 gx_scissor[4];

/* Keep GX submission state separate from the host framebuffer. */
#define GX_SW_MAX_VERTICES 65535u
static GXColor* gx_efb;
static f32* gx_depth;
static u32 gx_efb_width = 640, gx_efb_height = 480;
static GXPrimitive gx_primitive;
static u16 gx_expected_vertices;
static u32 gx_vertex_count;
static GXVtxFmt gx_vertex_format;
static GXSWVertex gx_vertices[GX_SW_MAX_VERTICES];
static GXNativeVertex gx_current_vertex;
static GXBool gx_color_update, gx_alpha_update, gx_z_update;
static GXBool gx_in_begin;
static GXBool gx_skip_raster;
static u8 gx_stream_vertex[512];
static size_t gx_stream_used, gx_stream_vertex_size;

static GXBool gx_trace_enabled;
static u64 gx_trace_draws, gx_trace_vertices, gx_trace_fragments,
    gx_trace_copies;
static void gx_trace_frame(void);

#ifdef MELEE_NATIVE_METAL
static void gx_metal_reset(void);
static void gx_metal_sync(void);
static void gx_metal_cpu_dirty(void);
static void gx_metal_invalidate_textures(void);
static bool gx_metal_try_draw(void);
#else
static void gx_metal_reset(void) {}
static void gx_metal_sync(void) {}
static void gx_metal_cpu_dirty(void) {}
static void gx_metal_invalidate_textures(void) {}
#endif

static GXBool gx_should_skip_raster(void)
{
    const char* value = getenv("MELEE_SKIP_RENDER");
    return value != NULL && value[0] != '\0' && value[0] != '0';
}

/* GX display lists contain the vertex stream in the GameCube wire format.
 * The original GX FIFO reads all multi-byte values as big endian, while the
 * native host uses little endian values. Keep the descriptor and attribute
 * array state here so GXCallDisplayList can consume those streams directly. */
typedef struct GXSWVtxAttrState {
    GXAttrType desc;
    GXCompCnt cnt;
    GXCompType type;
    u8 frac;
    const u8* array;
    u8 stride;
} GXSWVtxAttrState;
static GXSWVtxAttrState gx_vtx_state[GX_MAX_VTXFMT][GX_VA_MAX_ATTR];
static GXVtxFmt gx_active_vtxfmt;

typedef struct GXSWTexture {
    const u8* data;
    u16 width;
    u16 height;
    GXTexFmt format;
    GXTexWrapMode wrap_s;
    GXTexWrapMode wrap_t;
    GXTexFilter min_filter;
    GXTexFilter mag_filter;
    u32 tlut;
    size_t base_size;
    GXBool loaded;
} GXSWTexture;

typedef struct GXSWTlut {
    const u8* data;
    GXTlutFmt format;
    u16 entries;
    GXBool loaded;
} GXSWTlut;

static GXSWTexture gx_textures[GX_MAX_TEXMAP];
static GXSWTlut gx_tluts[20];
/* GX_VA_NBT shares the normal slot in the FIFO. The enum gives it a value at
 * the end of the attribute list, but its data still follows position. */
static GXAttr gx_state_attr(GXAttr attr)
{
    return attr == GX_VA_NBT ? GX_VA_NRM : attr;
}

static void gx_ensure_efb(void)
{
    if (gx_efb != NULL) {
        return;
    }
    size_t count = (size_t) gx_efb_width * gx_efb_height;
    gx_efb = calloc(count, sizeof(*gx_efb));
    gx_depth = malloc(count * sizeof(*gx_depth));
    if (gx_efb == NULL || gx_depth == NULL) {
        abort();
    }
    for (size_t i = 0; i < count; i++) {
        gx_depth[i] = 1.0f;
    }
}

#include "gx_transform.h"

static u16 gx_tex_be16(const u8* data)
{
    return (u16) (((u16) data[0] << 8) | data[1]);
}

static GXColor gx_tex_rgb565(u16 value)
{
    GXColor color = { 0, 0, 0, 255 };
    color.r = (u8) ((((value >> 11) & 0x1f) * 255 + 15) / 31);
    color.g = (u8) ((((value >> 5) & 0x3f) * 255 + 31) / 63);
    color.b = (u8) (((value & 0x1f) * 255 + 15) / 31);
    return color;
}

static GXColor gx_tex_rgb5a3(u16 value)
{
    GXColor color;
    if ((value & 0x8000) != 0) {
        color.r = (u8) ((((value >> 10) & 0x1f) * 255 + 15) / 31);
        color.g = (u8) ((((value >> 5) & 0x1f) * 255 + 15) / 31);
        color.b = (u8) (((value & 0x1f) * 255 + 15) / 31);
        color.a = 255;
    } else {
        color.a = (u8) ((((value >> 12) & 7) * 255 + 3) / 7);
        color.r = (u8) ((((value >> 8) & 0xf) * 255 + 7) / 15);
        color.g = (u8) ((((value >> 4) & 0xf) * 255 + 7) / 15);
        color.b = (u8) (((value & 0xf) * 255 + 7) / 15);
    }
    return color;
}

static size_t gx_texture_size(u16 width, u16 height, GXTexFmt format)
{
    u32 tile_width, tile_height, tile_bytes;
    switch (format & 0xf) {
    case GX_TF_I4:
    case GX_TF_C4:
    case GX_TF_CMPR:
        tile_width = tile_height = 8;
        tile_bytes = 32;
        break;
    case GX_TF_I8:
    case GX_TF_IA4:
    case GX_TF_C8:
        tile_width = 8;
        tile_height = 4;
        tile_bytes = 32;
        break;
    case GX_TF_IA8:
    case GX_TF_RGB565:
    case GX_TF_RGB5A3:
    case GX_TF_C14X2:
        tile_width = tile_height = 4;
        tile_bytes = 32;
        break;
    case GX_TF_RGBA8:
        tile_width = tile_height = 4;
        tile_bytes = 64;
        break;
    default:
        return 0;
    }
    return (size_t) ((width + tile_width - 1) / tile_width) *
           ((height + tile_height - 1) / tile_height) * tile_bytes;
}

static u32 gx_wrap_tex_coord(f32 coordinate, u16 size, GXTexWrapMode mode)
{
    s32 value;
    s32 period;
    s32 remainder;
    if (size == 0 || !isfinite(coordinate)) {
        return 0;
    }
    if (mode != GX_CLAMP) {
        coordinate = fmodf(coordinate, mode == GX_MIRROR ? 2.0f : 1.0f);
    } else {
        coordinate = fminf(1.0f, fmaxf(0.0f, coordinate));
    }
    value = (s32) floorf(coordinate * (f32) size);
    if (mode == GX_CLAMP) {
        if (value < 0) {
            value = 0;
        }
        if (value >= (s32) size) {
            value = (s32) size - 1;
        }
        return (u32) value;
    }
    period = mode == GX_MIRROR ? (s32) size * 2 : (s32) size;
    if (period <= 0) {
        return 0;
    }
    remainder = value % period;
    if (remainder < 0) {
        remainder += period;
    }
    if (mode == GX_MIRROR && remainder >= (s32) size) {
        remainder = period - remainder - 1;
    }
    return (u32) remainder;
}

static GXColor gx_tex_palette(const GXSWTexture* texture, u32 index)
{
    GXColor color = { 255, 255, 255, 255 };
    u32 slot = texture->tlut & 0x1f;
    const GXSWTlut* tlut;
    if (slot >= sizeof gx_tluts / sizeof gx_tluts[0] ||
        !gx_tluts[slot].loaded || gx_tluts[slot].data == NULL ||
        index >= gx_tluts[slot].entries)
    {
        return color;
    }
    tlut = &gx_tluts[slot];
    switch (tlut->format) {
    case GX_TL_IA8:
        color.a = tlut->data[index * 2];
        color.r = color.g = color.b = tlut->data[index * 2 + 1];
        break;
    case GX_TL_RGB565:
        color = gx_tex_rgb565(gx_tex_be16(tlut->data + index * 2));
        break;
    case GX_TL_RGB5A3:
        color = gx_tex_rgb5a3(gx_tex_be16(tlut->data + index * 2));
        break;
    default:
        break;
    }
    return color;
}

static GXColor gx_texture_nearest(const GXSWTexture* texture, f32 s, f32 t)
{
    GXColor color = { 255, 255, 255, 255 };
    u32 x, y, tiles_w, tile_x, tile_y, in_x, in_y;
    size_t tile, offset;
    const u8* data;
    if (!texture->loaded || texture->data == NULL || texture->width == 0 ||
        texture->height == 0 || texture->base_size == 0)
    {
        return color;
    }
    x = gx_wrap_tex_coord(s, texture->width, texture->wrap_s);
    y = gx_wrap_tex_coord(t, texture->height, texture->wrap_t);
    switch (texture->format & 0xf) {
    case GX_TF_I4:
    case GX_TF_C4:
    case GX_TF_CMPR:
        tiles_w = (texture->width + 7) / 8;
        tile_x = x / 8;
        tile_y = y / 8;
        in_x = x & 7;
        in_y = y & 7;
        tile = ((size_t) tile_y * tiles_w + tile_x) * 32;
        break;
    case GX_TF_I8:
    case GX_TF_IA4:
    case GX_TF_C8:
        tiles_w = (texture->width + 7) / 8;
        tile_x = x / 8;
        tile_y = y / 4;
        in_x = x & 7;
        in_y = y & 3;
        tile = ((size_t) tile_y * tiles_w + tile_x) * 32;
        break;
    case GX_TF_IA8:
    case GX_TF_RGB565:
    case GX_TF_RGB5A3:
    case GX_TF_C14X2:
        tiles_w = (texture->width + 3) / 4;
        tile_x = x / 4;
        tile_y = y / 4;
        in_x = x & 3;
        in_y = y & 3;
        tile = ((size_t) tile_y * tiles_w + tile_x) * 32;
        break;
    case GX_TF_RGBA8:
        tiles_w = (texture->width + 3) / 4;
        tile_x = x / 4;
        tile_y = y / 4;
        in_x = x & 3;
        in_y = y & 3;
        tile = ((size_t) tile_y * tiles_w + tile_x) * 64;
        break;
    default:
        return color;
    }
    if (tile >= texture->base_size) {
        return color;
    }
    data = texture->data + tile;
    switch (texture->format & 0xf) {
    case GX_TF_I4:
        offset = (size_t) in_y * 4 + in_x / 2;
        if (tile + offset >= texture->base_size) {
            return color;
        }
        color.a = color.r = color.g = color.b =
            (u8) ((data[offset] >> (in_x & 1 ? 0 : 4) & 0xf) * 17);
        break;
    case GX_TF_I8:
        offset = (size_t) in_y * 8 + in_x;
        if (tile + offset >= texture->base_size) {
            return color;
        }
        color.a = color.r = color.g = color.b = data[offset];
        break;
    case GX_TF_IA4:
        offset = (size_t) in_y * 8 + in_x;
        if (tile + offset >= texture->base_size) {
            return color;
        }
        color.a = (u8) ((data[offset] >> 4) * 17);
        color.r = color.g = color.b = (u8) ((data[offset] & 0xf) * 17);
        break;
    case GX_TF_IA8:
        offset = ((size_t) in_y * 4 + in_x) * 2;
        if (tile + offset + 1 >= texture->base_size) {
            return color;
        }
        color.a = data[offset];
        color.r = color.g = color.b = data[offset + 1];
        break;
    case GX_TF_RGB565:
        offset = ((size_t) in_y * 4 + in_x) * 2;
        if (tile + offset + 1 >= texture->base_size) {
            return color;
        }
        color = gx_tex_rgb565(gx_tex_be16(data + offset));
        break;
    case GX_TF_RGB5A3:
        offset = ((size_t) in_y * 4 + in_x) * 2;
        if (tile + offset + 1 >= texture->base_size) {
            return color;
        }
        color = gx_tex_rgb5a3(gx_tex_be16(data + offset));
        break;
    case GX_TF_RGBA8:
        offset = (size_t) in_y * 8 + in_x * 2;
        if (tile + offset + 1 >= texture->base_size ||
            tile + 32 + offset + 1 >= texture->base_size)
        {
            return color;
        }
        color.a = data[offset];
        color.r = data[offset + 1];
        color.g = data[32 + offset];
        color.b = data[32 + offset + 1];
        break;
    case GX_TF_C4:
        offset = (size_t) in_y * 4 + in_x / 2;
        if (tile + offset >= texture->base_size) {
            return color;
        }
        return gx_tex_palette(texture,
                              (data[offset] >> (in_x & 1 ? 0 : 4)) & 0xf);
    case GX_TF_C8:
        offset = (size_t) in_y * 8 + in_x;
        if (tile + offset >= texture->base_size) {
            return color;
        }
        return gx_tex_palette(texture, data[offset]);
    case GX_TF_C14X2:
        offset = ((size_t) in_y * 4 + in_x) * 2;
        if (tile + offset + 1 >= texture->base_size) {
            return color;
        }
        return gx_tex_palette(texture, gx_tex_be16(data + offset) & 0x3fff);
    case GX_TF_CMPR: {
        u32 sub = (in_y / 4) * 2 + in_x / 4;
        u32 local_x = in_x & 3, local_y = in_y & 3;
        const u8* block = data + sub * 8;
        GXColor palette[4];
        u16 first, second;
        u8 selector;
        if (tile + sub * 8 + 7 >= texture->base_size) {
            return color;
        }
        first = gx_tex_be16(block);
        second = gx_tex_be16(block + 2);
        palette[0] = gx_tex_rgb565(first);
        palette[1] = gx_tex_rgb565(second);
        if (first > second) {
            palette[2].r = (u8) ((5 * palette[0].r + 3 * palette[1].r) / 8);
            palette[2].g = (u8) ((5 * palette[0].g + 3 * palette[1].g) / 8);
            palette[2].b = (u8) ((5 * palette[0].b + 3 * palette[1].b) / 8);
            palette[2].a = 255;
            palette[3].r = (u8) ((3 * palette[0].r + 5 * palette[1].r) / 8);
            palette[3].g = (u8) ((3 * palette[0].g + 5 * palette[1].g) / 8);
            palette[3].b = (u8) ((3 * palette[0].b + 5 * palette[1].b) / 8);
            palette[3].a = 255;
        } else {
            palette[2].r = (u8) ((palette[0].r + palette[1].r) / 2);
            palette[2].g = (u8) ((palette[0].g + palette[1].g) / 2);
            palette[2].b = (u8) ((palette[0].b + palette[1].b) / 2);
            palette[2].a = 255;
            palette[3] = (GXColor){ 0, 0, 0, 0 };
        }
        selector = block[4 + local_y] >> (6 - local_x * 2);
        return palette[selector & 3];
    }
    default:
        break;
    }
    return color;
}

static GXColor gx_texture_sample(const GXSWTexture* texture, f32 s, f32 t)
{
    if (texture->mag_filter != GX_LINEAR || !texture->width ||
        !texture->height || !isfinite(s) || !isfinite(t))
    {
        return gx_texture_nearest(texture, s, t);
    }
    if (texture->wrap_s == GX_CLAMP) {
        s = fminf(1, fmaxf(0, s));
    } else {
        s = fmodf(s, texture->wrap_s == GX_MIRROR ? 2 : 1);
    }
    if (texture->wrap_t == GX_CLAMP) {
        t = fminf(1, fmaxf(0, t));
    } else {
        t = fmodf(t, texture->wrap_t == GX_MIRROR ? 2 : 1);
    }
    f32 x = s * texture->width - 0.5f, y = t * texture->height - 0.5f;
    f32 left = floorf(x), top = floorf(y);
    f32 fx = x - left, fy = y - top;
    GXColor colors[4] = {
        gx_texture_nearest(texture, (left + 0.5f) / texture->width,
                           (top + 0.5f) / texture->height),
        gx_texture_nearest(texture, (left + 1.5f) / texture->width,
                           (top + 0.5f) / texture->height),
        gx_texture_nearest(texture, (left + 0.5f) / texture->width,
                           (top + 1.5f) / texture->height),
        gx_texture_nearest(texture, (left + 1.5f) / texture->width,
                           (top + 1.5f) / texture->height),
    };
    GXColor out;
    for (u32 i = 0; i < 4; i++) {
        f32 upper =
            ((u8*) &colors[0])[i] * (1 - fx) + ((u8*) &colors[1])[i] * fx;
        f32 lower =
            ((u8*) &colors[2])[i] * (1 - fx) + ((u8*) &colors[3])[i] * fx;
        ((u8*) &out)[i] = (u8) lroundf(upper * (1 - fy) + lower * fy);
    }
    return out;
}

#include "gx_copy.h"
#include "gx_raster.h"
#include "gx_tev.h"

#ifdef MELEE_NATIVE_METAL
#include "gx_metal_bridge.h"
#endif

static void gx_rasterize(void)
{
    gx_trace_draws++;
    gx_trace_vertices += gx_vertex_count;
    if (gx_skip_raster || gx_vertex_count == 0) {
        return;
    }
#ifdef MELEE_NATIVE_METAL
    if (gx_metal_try_draw()) {
        return;
    }
#endif
    gx_metal_sync();
    gx_metal_cpu_dirty();
    if (gx_primitive == GX_POINTS) {
        for (u32 i = 0; i < gx_vertex_count; i++) {
            GXSWVertex* v = &gx_vertices[i];
            if (!gx_finite_vertex(v)) {
                continue;
            }
            bool clipped = false;
            if (v->projected) {
                for (u32 plane = 0; plane < 6; plane++) {
                    if (gx_clip_distance(v, plane) < 0) {
                        clipped = true;
                    }
                }
            }
            if (clipped || v->x < -gx_point_size || v->y < -gx_point_size ||
                v->x > gx_efb_width + gx_point_size ||
                v->y > gx_efb_height + gx_point_size)
            {
                continue;
            }
            s32 radius = (s32) floorf(gx_point_size * 0.5f);
            s32 x = (s32) lroundf(v->x), y = (s32) lroundf(v->y);
            for (s32 py = y - radius; py <= y + radius; py++) {
                for (s32 px = x - radius; px <= x + radius; px++) {
                    gx_plot(px, py, v);
                }
            }
        }
    } else if (gx_primitive == GX_LINES || gx_primitive == GX_LINESTRIP) {
        u32 step = gx_primitive == GX_LINES ? 2 : 1;
        for (u32 i = 0; i + 1 < gx_vertex_count; i += step) {
            gx_line(&gx_vertices[i], &gx_vertices[i + 1]);
        }
    } else if (gx_primitive == GX_QUADS) {
        for (u32 i = 0; i + 3 < gx_vertex_count; i += 4) {
            gx_triangle(&gx_vertices[i], &gx_vertices[i + 1],
                        &gx_vertices[i + 2]);
            gx_triangle(&gx_vertices[i], &gx_vertices[i + 2],
                        &gx_vertices[i + 3]);
        }
    } else {
        u32 step = gx_primitive == GX_TRIANGLES ? 3 : 1;
        for (u32 i = 0; i + 2 < gx_vertex_count; i += step) {
            if (gx_primitive == GX_TRIANGLEFAN) {
                gx_triangle(&gx_vertices[0], &gx_vertices[i + 1],
                            &gx_vertices[i + 2]);
            } else if (gx_primitive == GX_TRIANGLESTRIP && (i & 1)) {
                gx_triangle(&gx_vertices[i + 1], &gx_vertices[i],
                            &gx_vertices[i + 2]);
            } else {
                gx_triangle(&gx_vertices[i], &gx_vertices[i + 1],
                            &gx_vertices[i + 2]);
            }
        }
    }
}

static void gx_trace_frame(void)
{
    if (!gx_trace_enabled) {
        return;
    }
    gx_trace_copies++;
    if (gx_trace_copies > 5 && gx_trace_copies % 60 != 0) {
        return;
    }
    u32 colored = 0;
    for (size_t i = 0; i < (size_t) gx_efb_width * gx_efb_height; i++) {
        if (gx_efb[i].r || gx_efb[i].g || gx_efb[i].b) {
            colored++;
        }
    }
    fprintf(stderr,
            "[native-gx] copy=%llu draws=%llu vertices=%llu fragments=%llu "
            "colored=%u\n",
            (unsigned long long) gx_trace_copies,
            (unsigned long long) gx_trace_draws,
            (unsigned long long) gx_trace_vertices,
            (unsigned long long) gx_trace_fragments, colored);
}

static bool gx_dl_read_u8(const u8** cursor, const u8* end, u8* value)
{
    if (*cursor >= end) {
        return false;
    }
    *value = *(*cursor)++;
    return true;
}

static bool gx_dl_read_be16(const u8** cursor, const u8* end, u16* value)
{
    if ((size_t) (end - *cursor) < 2) {
        return false;
    }
    *value = (u16) (((u16) (*cursor)[0] << 8) | (*cursor)[1]);
    *cursor += 2;
    return true;
}

static bool gx_dl_read_be32(const u8** cursor, const u8* end, u32* value)
{
    if ((size_t) (end - *cursor) < 4) {
        return false;
    }
    *value = ((u32) (*cursor)[0] << 24) | ((u32) (*cursor)[1] << 16) |
             ((u32) (*cursor)[2] << 8) | (*cursor)[3];
    *cursor += 4;
    return true;
}

static bool gx_dl_read_bytes(const u8** cursor, const u8* end, size_t size,
                             const u8** value)
{
    if (size > (size_t) (end - *cursor)) {
        return false;
    }
    *value = *cursor;
    *cursor += size;
    return true;
}

static size_t gx_component_size(GXCompType type, GXAttr attr)
{
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        switch (type) {
        case GX_RGB565:
        case GX_RGBA4:
            return 2;
        case GX_RGB8:
        case GX_RGBA6:
            return 3;
        case GX_RGBX8:
        case GX_RGBA8:
            return 4;
        default:
            return 0;
        }
    }
    switch (type) {
    case GX_U8:
    case GX_S8:
        return 1;
    case GX_U16:
    case GX_S16:
        return 2;
    case GX_F32:
        return 4;
    default:
        return 0;
    }
}

static u32 gx_dl_be16_at(const u8* bytes)
{
    return ((u32) bytes[0] << 8) | bytes[1];
}

static u32 gx_dl_be24_at(const u8* bytes)
{
    return ((u32) bytes[0] << 16) | ((u32) bytes[1] << 8) | bytes[2];
}

static u32 gx_dl_be32_at(const u8* bytes)
{
    return ((u32) bytes[0] << 24) | ((u32) bytes[1] << 16) |
           ((u32) bytes[2] << 8) | bytes[3];
}

static f32 gx_dl_scalar(const u8* bytes, GXCompType type, u8 frac)
{
    u32 raw;
    switch (type) {
    case GX_U8:
        return (f32) bytes[0] / (f32) (1u << (frac < 32 ? frac : 31));
    case GX_S8:
        return (f32) (s8) bytes[0] / (f32) (1u << (frac < 32 ? frac : 31));
    case GX_U16:
        return (f32) gx_dl_be16_at(bytes) /
               (f32) (1u << (frac < 32 ? frac : 31));
    case GX_S16:
        return (f32) (s16) gx_dl_be16_at(bytes) /
               (f32) (1u << (frac < 32 ? frac : 31));
    case GX_F32:
        raw = gx_dl_be32_at(bytes);
        {
            f32 value;
            memcpy(&value, &raw, sizeof value);
            return value;
        }
    default:
        return 0.0f;
    }
}

static u8 gx_expand_bits(u32 value, u32 bits)
{
    u32 max = (1u << bits) - 1u;
    return (u8) ((value * 255u + max / 2u) / max);
}

static GXColor gx_dl_color(const u8* bytes, GXCompType type)
{
    GXColor color = { 255, 255, 255, 255 };
    u32 packed;
    switch (type) {
    case GX_RGB565:
        packed = gx_dl_be16_at(bytes);
        color.r = gx_expand_bits(packed >> 11, 5);
        color.g = gx_expand_bits((packed >> 5) & 0x3f, 6);
        color.b = gx_expand_bits(packed & 0x1f, 5);
        break;
    case GX_RGB8:
        color.r = bytes[0];
        color.g = bytes[1];
        color.b = bytes[2];
        break;
    case GX_RGBX8:
        color.r = bytes[0];
        color.g = bytes[1];
        color.b = bytes[2];
        break;
    case GX_RGBA4:
        packed = gx_dl_be16_at(bytes);
        color.r = gx_expand_bits(packed >> 12, 4);
        color.g = gx_expand_bits((packed >> 8) & 0xf, 4);
        color.b = gx_expand_bits((packed >> 4) & 0xf, 4);
        color.a = gx_expand_bits(packed & 0xf, 4);
        break;
    case GX_RGBA6:
        packed = gx_dl_be24_at(bytes);
        color.r = gx_expand_bits(packed >> 18, 6);
        color.g = gx_expand_bits((packed >> 12) & 0x3f, 6);
        color.b = gx_expand_bits((packed >> 6) & 0x3f, 6);
        color.a = gx_expand_bits(packed & 0x3f, 6);
        break;
    case GX_RGBA8:
        color.r = bytes[0];
        color.g = bytes[1];
        color.b = bytes[2];
        color.a = bytes[3];
        break;
    default:
        break;
    }
    return color;
}

static const u8* gx_dl_array_element(const GXSWVtxAttrState* state, u32 index,
                                     size_t size)
{
    uintptr_t base;
    size_t offset;
    if (state->array == NULL || state->stride == 0 || size == 0) {
        return NULL;
    }
    if (index > SIZE_MAX / state->stride) {
        return NULL;
    }
    offset = (size_t) index * state->stride;
    base = (uintptr_t) state->array;
    if (offset > UINTPTR_MAX - base) {
        return NULL;
    }
    return (const u8*) (base + offset);
}

static size_t gx_attribute_components(GXAttr attr,
                                      const GXSWVtxAttrState* state)
{
    if (attr == GX_VA_POS) {
        return state->cnt == GX_POS_XYZ ? 3 : 2;
    }
    if (attr == GX_VA_NRM) {
        return state->cnt == GX_NRM_XYZ ? 3 : 9;
    }
    if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
        return state->cnt == GX_TEX_ST ? 2 : 1;
    }
    return 1;
}

static bool gx_dl_attr(const u8** cursor, const u8* end, GXAttr attr,
                       const GXSWVtxAttrState* state, f32 value[9],
                       GXColor* color)
{
    if (state->desc == GX_NONE) {
        return true;
    }
    size_t component_size = gx_component_size(state->type, attr);
    size_t count = gx_attribute_components(attr, state);
    if (component_size == 0) {
        return false;
    }
    bool packed_color = attr == GX_VA_CLR0 || attr == GX_VA_CLR1;
    u32 index_count = attr == GX_VA_NRM && state->cnt == GX_NRM_NBT3 &&
                              state->desc != GX_DIRECT
                          ? 3
                          : 1;
    for (u32 part = 0; part < index_count; part++) {
        const u8* bytes;
        size_t part_count = index_count == 3 ? 3 : count;
        size_t size =
            packed_color ? component_size : component_size * part_count;
        if (state->desc == GX_DIRECT) {
            if (!gx_dl_read_bytes(cursor, end, size, &bytes)) {
                return false;
            }
        } else {
            u16 index = 0;
            if (state->desc == GX_INDEX16) {
                if (!gx_dl_read_be16(cursor, end, &index)) {
                    return false;
                }
            } else {
                u8 small;
                if (!gx_dl_read_u8(cursor, end, &small)) {
                    return false;
                }
                index = small;
            }
            bytes = gx_dl_array_element(state, index, size);
            if (bytes == NULL) {
                return false;
            }
            if (index_count == 3) {
                bytes += part * 3 * component_size;
            }
        }
        if (packed_color && color) {
            *color = gx_dl_color(bytes, state->type);
        } else if (value) {
            u8 frac = state->frac;
            if (attr == GX_VA_NRM) {
                if (state->type == GX_S8) {
                    frac = 6;
                } else if (state->type == GX_S16) {
                    frac = 14;
                }
            }
            for (size_t i = 0; i < part_count; i++) {
                value[part * 3 + i] = gx_dl_scalar(bytes + i * component_size,
                                                   state->type, frac);
            }
        }
    }
    return true;
}

static bool gx_dl_vertex(const u8** cursor, const u8* end, GXVtxFmt format)
{
    GXSWVtxAttrState* states = gx_vtx_state[format];
    GXNativeVertex v = gx_current_vertex;
    v.has_position_matrix = GX_FALSE;
    memset(v.has_texture_matrix, 0, sizeof v.has_texture_matrix);
    for (GXAttr attr = GX_VA_PNMTXIDX; attr <= GX_VA_TEX7; attr++) {
        GXSWVtxAttrState* state = &states[attr];
        if (state->desc == GX_NONE) {
            continue;
        }
        if (attr <= GX_VA_TEX7MTXIDX) {
            u8 index;
            if (!gx_dl_read_u8(cursor, end, &index)) {
                return false;
            }
            if (attr == GX_VA_PNMTXIDX) {
                v.position_matrix = index;
                v.has_position_matrix = GX_TRUE;
            } else {
                u32 i = attr - GX_VA_TEX0MTXIDX;
                v.texture_matrix[i] = index;
                v.has_texture_matrix[i] = GX_TRUE;
            }
            continue;
        }
        f32 values[9] = { 0 };
        GXColor color;
        if (!gx_dl_attr(cursor, end, attr, state, values, &color)) {
            return false;
        }
        if (attr == GX_VA_POS) {
            memcpy(v.position, values, sizeof v.position);
        } else if (attr == GX_VA_NRM) {
            memcpy(v.normal, values, sizeof v.normal);
            if (state->cnt != GX_NRM_XYZ) {
                memcpy(v.binormal, values + 3, sizeof v.binormal);
                memcpy(v.tangent, values + 6, sizeof v.tangent);
            }
        } else if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
            v.color[attr - GX_VA_CLR0] = color;
        } else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
            memcpy(v.texcoord[attr - GX_VA_TEX0], values,
                   sizeof v.texcoord[0]);
        }
    }
    if (gx_vertex_count >= GX_SW_MAX_VERTICES) {
        return false;
    }
    gx_current_vertex = v;
    gx_vertices[gx_vertex_count++] = gx_transform_vertex(&v);
    return true;
}

static size_t gx_vertex_stream_size(GXVtxFmt format)
{
    size_t size = 0;
    for (GXAttr attr = GX_VA_PNMTXIDX; attr <= GX_VA_TEX7; attr++) {
        const GXSWVtxAttrState* state = &gx_vtx_state[format][attr];
        if (state->desc == GX_NONE) {
            continue;
        }
        if (attr <= GX_VA_TEX7MTXIDX) {
            size++;
        } else if (state->desc == GX_DIRECT) {
            size += gx_component_size(state->type, attr) *
                    gx_attribute_components(attr, state);
        } else {
            size_t indices =
                attr == GX_VA_NRM && state->cnt == GX_NRM_NBT3 ? 3 : 1;
            size += indices * (state->desc == GX_INDEX16 ? 2 : 1);
        }
    }
    return size;
}

static void gx_stream_write(u32 value, size_t size)
{
    if (!gx_in_begin || gx_stream_vertex_size == 0 ||
        gx_stream_vertex_size > sizeof gx_stream_vertex)
    {
        return;
    }
    for (size_t i = 0; i < size; i++) {
        gx_stream_vertex[gx_stream_used++] =
            (u8) (value >> ((size - i - 1) * 8));
        if (gx_stream_used == gx_stream_vertex_size) {
            const u8* cursor = gx_stream_vertex;
            if (!gx_dl_vertex(&cursor, gx_stream_vertex + gx_stream_used,
                              gx_vertex_format))
            {
                gx_in_begin = GX_FALSE;
                return;
            }
            gx_stream_used = 0;
            if (gx_vertex_count == gx_expected_vertices) {
                gx_rasterize();
                gx_in_begin = GX_FALSE;
                return;
            }
        }
    }
}

static bool gx_dl_is_primitive(u8 command)
{
    u8 primitive = command & 0xf8;
    return primitive == GX_QUADS || primitive == GX_TRIANGLES ||
           primitive == GX_TRIANGLESTRIP || primitive == GX_TRIANGLEFAN ||
           primitive == GX_LINES || primitive == GX_LINESTRIP ||
           primitive == GX_POINTS;
}

static bool gx_dl_skip_state_command(u8 command, const u8** cursor,
                                     const u8* end)
{
    size_t payload_size;
    switch (command) {
    case GX_LOAD_CP_REG:
        payload_size = 5; /* register byte and one 32-bit value */
        break;
    case GX_LOAD_XF_REG: {
        u32 header;
        if (!gx_dl_read_be32(cursor, end, &header)) {
            return false;
        }
        payload_size = ((size_t) (header >> 16) + 1) * 4;
        break;
    }
    case GX_LOAD_INDX_A:
    case GX_LOAD_INDX_B:
    case GX_LOAD_INDX_C:
    case GX_LOAD_INDX_D:
    case GX_LOAD_BP_REG:
        payload_size = 4;
        break;
    default:
        return false;
    }
    if (payload_size > (size_t) (end - *cursor)) {
        return false;
    }
    *cursor += payload_size;
    return true;
}
GXRenderModeObj GXNtsc480Int = {
    .viTVmode = 0,
    .fbWidth = 640,
    .efbHeight = 480,
    .xfbHeight = 480,
    .viXOrigin = 40,
    .viWidth = 640,
    .viHeight = 480,
    .xFBmode = 1,
    .sample_pattern = { { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 } },
    .vfilter = { 0, 0, 21, 22, 21, 0, 0 },
};
GXRenderModeObj GXNtsc480IntDf = {
    .viTVmode = 0,
    .fbWidth = 640,
    .efbHeight = 480,
    .xfbHeight = 480,
    .viXOrigin = 40,
    .viWidth = 640,
    .viHeight = 480,
    .xFBmode = 1,
    .sample_pattern = { { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 } },
    .vfilter = { 8, 8, 10, 12, 10, 8, 8 },
};
GXRenderModeObj GXNtsc480Prog = {
    .viTVmode = 2,
    .fbWidth = 640,
    .efbHeight = 480,
    .xfbHeight = 480,
    .viXOrigin = 40,
    .viWidth = 640,
    .viHeight = 480,
    .xFBmode = 0,
    .sample_pattern = { { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 },
                        { 6, 6 } },
    .vfilter = { 0, 0, 21, 22, 21, 0, 0 },
};
GXFifoObj* GXInit(void* buffer, u32 size)
{
    (void) buffer;
    (void) size;
    memset(&gx_fifo, 0, sizeof gx_fifo);
    gx_cpu_fifo = gx_gp_fifo = &gx_fifo;
    free(gx_efb);
    free(gx_depth);
    gx_efb = NULL;
    gx_depth = NULL;
    gx_ensure_efb();
    gx_skip_raster = gx_should_skip_raster();
    const char* trace = getenv("MELEE_GX_TRACE");
    gx_trace_enabled = trace != NULL && trace[0] != '0' && trace[0] != '\0';
    gx_trace_draws = gx_trace_vertices = gx_trace_fragments = gx_trace_copies =
        0;
    memset(gx_projection, 0, sizeof gx_projection);
    memset(gx_scissor, 0, sizeof gx_scissor);
    memset(gx_vtx_state, 0, sizeof gx_vtx_state);
    memset(gx_textures, 0, sizeof gx_textures);
    memset(gx_tluts, 0, sizeof gx_tluts);
    memset(&gx_current_vertex, 0, sizeof gx_current_vertex);
    gx_current_vertex.color[0] = gx_current_vertex.color[1] =
        (GXColor){ 255, 255, 255, 255 };
    gx_current_vertex.normal[2] = 1;
    gx_current_vertex.binormal[1] = 1;
    gx_current_vertex.tangent[0] = 1;
    for (u32 format = 0; format < GX_MAX_VTXFMT; format++) {
        for (u32 attr = 0; attr < GX_VA_MAX_ATTR; attr++) {
            gx_vtx_state[format][attr].cnt = GX_POS_XYZ;
            gx_vtx_state[format][attr].type = GX_F32;
        }
    }
    gx_in_begin = GX_FALSE;
    gx_active_vtxfmt = GX_VTXFMT0;
    gx_transform_reset();
    gx_fog_reset();
    gx_tev_reset();
    gx_raster_reset();
    gx_copy_reset();
    gx_metal_reset();
    return &gx_fifo;
}
GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback c)
{
    GXDrawDoneCallback o = gx_done_cb;
    gx_done_cb = c;
    return o;
}
GXDrawSyncCallback GXSetDrawSyncCallback(GXDrawSyncCallback c)
{
    GXDrawSyncCallback o = gx_sync_cb;
    gx_sync_cb = c;
    return o;
}
void GXSetDrawSync(u16 t)
{
    gx_draw_token = t;
    if (gx_sync_cb) {
        gx_sync_cb(t);
    }
}
u16 GXReadDrawSync(void)
{
    return gx_draw_token;
}
void GXSetDrawDone(void)
{
    if (gx_done_cb) {
        gx_done_cb();
    }
}
void GXWaitDrawDone(void) {}
void GXDrawDone(void)
{
    GXSetDrawDone();
}
void GXBegin(GXPrimitive primitive, GXVtxFmt format, u16 count)
{
    if (gx_in_begin) {
        GXEnd();
    }
    if (format >= GX_MAX_VTXFMT) {
        return;
    }
    gx_ensure_efb();
    gx_primitive = primitive;
    gx_vertex_format = gx_active_vtxfmt = format;
    gx_expected_vertices = count;
    gx_vertex_count = 0;
    gx_stream_used = 0;
    gx_stream_vertex_size = gx_vertex_stream_size(format);
    gx_in_begin = count != 0;
}
void GXEnd(void)
{
    if (!gx_in_begin) {
        return;
    }
    gx_rasterize();
    gx_in_begin = GX_FALSE;
}

/* All SDK vertex functions write the same byte stream. Using one decoder
 * also handles particle code that submits raw GXParam values and shape
 * animation code that submits matrix indices through GXTexCoord1u8. */
static void gx_stream_float(f32 value)
{
    u32 bits;
    memcpy(&bits, &value, sizeof bits);
    gx_stream_write(bits, 4);
}
// clang-format off: these macros intentionally generate adjacent GX ABI symbols.
#define GX_WRITE1(name, type, bytes)                                          \
    void name##1##type(type x)                                                \
    {                                                                         \
        gx_stream_write((u32) x, bytes);                                      \
    }
#define GX_WRITE2(name, type, bytes)                                          \
    void name##2##type(type x, type y)                                        \
    {                                                                         \
        gx_stream_write((u32) x, bytes);                                      \
        gx_stream_write((u32) y, bytes);                                      \
    }
#define GX_WRITE3(name, type, bytes)                                          \
    void name##3##type(type x, type y, type z)                                \
    {                                                                         \
        gx_stream_write((u32) x, bytes);                                      \
        gx_stream_write((u32) y, bytes);                                      \
        gx_stream_write((u32) z, bytes);                                      \
    }
#define GX_FLOAT1(name)                                                       \
    void name##1f32(f32 x)                                                    \
    {                                                                         \
        gx_stream_float(x);                                                   \
    }
#define GX_FLOAT2(name)                                                       \
    void name##2f32(f32 x, f32 y)                                             \
    {                                                                         \
        gx_stream_float(x);                                                   \
        gx_stream_float(y);                                                   \
    }
#define GX_FLOAT3(name)                                                       \
    void name##3f32(f32 x, f32 y, f32 z)                                      \
    {                                                                         \
        gx_stream_float(x);                                                   \
        gx_stream_float(y);                                                   \
        gx_stream_float(z);                                                   \
    }
GX_WRITE1(GXParam, u8, 1)
GX_WRITE1(GXParam, u16, 2)
GX_WRITE1(GXParam, u32, 4)
GX_WRITE1(GXParam, s8, 1)
GX_WRITE1(GXParam, s16, 2)
GX_WRITE1(GXParam, s32, 4)
GX_FLOAT1(GXParam)
    GX_FLOAT3(GXParam) void GXParam4f32(f32 x, f32 y, f32 z, f32 w)
{
    GXParam3f32(x, y, z);
    gx_stream_float(w);
}
GX_FLOAT2(GXPosition)
GX_FLOAT3(GXPosition)
GX_WRITE2(GXPosition, u8, 1)
GX_WRITE3(GXPosition, u8, 1)
GX_WRITE2(GXPosition, s8, 1)
GX_WRITE3(GXPosition, s8, 1)
GX_WRITE2(GXPosition, u16, 2) GX_WRITE3(GXPosition, u16, 2)
    GX_WRITE2(GXPosition, s16, 2) GX_WRITE3(GXPosition, s16, 2)
        GX_FLOAT3(GXNormal) GX_WRITE3(GXNormal, s8, 1)
            GX_WRITE3(GXNormal, s16, 2) GX_WRITE1(GXColor, u16, 2)
                GX_WRITE1(GXColor, u32, 4)
                    GX_WRITE3(GXColor, u8, 1) void GXColor4u8(u8 r, u8 g, u8 b,
                                                              u8 a)
{
    GXColor3u8(r, g, b);
    gx_stream_write(a, 1);
}
GX_FLOAT1(GXTexCoord)
GX_FLOAT2(GXTexCoord)
GX_WRITE1(GXTexCoord, u8, 1)
GX_WRITE2(GXTexCoord, u8, 1)
GX_WRITE1(GXTexCoord, s8, 1)
GX_WRITE2(GXTexCoord, s8, 1)
GX_WRITE1(GXTexCoord, u16, 2) GX_WRITE2(GXTexCoord, u16, 2)
    GX_WRITE1(GXTexCoord, s16, 2) GX_WRITE2(GXTexCoord, s16, 2)
        GX_WRITE1(GXMatrixIndex, u8, 1)
#define GX_INDEX(name)                                                        \
    void name##1x8(u8 x)                                                      \
    {                                                                         \
        gx_stream_write(x, 1);                                                \
    }                                                                         \
    void name##1x16(u16 x)                                                    \
    {                                                                         \
        gx_stream_write(x, 2);                                                \
    }
            GX_INDEX(GXPosition) GX_INDEX(GXNormal) GX_INDEX(GXColor)
                GX_INDEX(GXTexCoord)
#undef GX_INDEX
#undef GX_WRITE1
#undef GX_WRITE2
#undef GX_WRITE3
#undef GX_FLOAT1
#undef GX_FLOAT2
#undef GX_FLOAT3
    // clang-format on

    u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, u8 mipmap,
                           u8 max_lod)
{
    u32 x_shift;
    u32 y_shift;
    u32 tile_bytes =
        (format == GX_TF_RGBA8 || format == GX_TF_Z24X8) ? 64 : 32;
    switch (format & 0xf) {
    case GX_TF_I4:
    case GX_TF_CMPR:
    case GX_TF_C4:
        x_shift = 3;
        y_shift = 3;
        break;
    case GX_TF_I8:
    case GX_TF_IA4:
    case GX_TF_C8:
    case GX_TF_Z8:
        x_shift = 3;
        y_shift = 2;
        break;
    default:
        x_shift = 2;
        y_shift = 2;
        break;
    }
    if (!mipmap) {
        return ((width + (1u << x_shift) - 1) >> x_shift) *
               ((height + (1u << y_shift) - 1) >> y_shift) * tile_bytes;
    }
    u32 size = 0;
    for (u32 level = 0; level < max_lod; level++) {
        size += ((width + (1u << x_shift) - 1) >> x_shift) *
                ((height + (1u << y_shift) - 1) >> y_shift) * tile_bytes;
        if (width == 1 && height == 1) {
            break;
        }
        width = width > 1 ? width >> 1 : 1;
        height = height > 1 ? height >> 1 : 1;
    }
    return size;
}
void GXInitTexObj(GXTexObj* o, void* d, u16 w, u16 h, GXTexFmt f,
                  GXTexWrapMode s, GXTexWrapMode t, u8 m)
{
    memset(o, 0, sizeof *o);
    o->dummy[0] = (uptr) d;
    o->dummy[1] = (uptr) w | ((uptr) h << 16);
    o->dummy[2] =
        (uptr) f | ((uptr) s << 8) | ((uptr) t << 16) | ((uptr) m << 24);
}
void GXInitTexObjCI(GXTexObj* o, void* d, u16 w, u16 h, GXTexFmt f,
                    GXTexWrapMode s, GXTexWrapMode t, u8 m, u32 tl)
{
    GXInitTexObj(o, d, w, h, f, s, t, m);
    o->dummy[3] = tl;
}
void GXInitTexObjLOD(GXTexObj* o, GXTexFilter a, GXTexFilter b, f32 c, f32 d,
                     f32 e, GXBool f, GXBool g, GXAnisotropy h)
{
    o->dummy[4] = (uptr) a | ((uptr) b << 8);
    (void) c;
    (void) d;
    (void) e;
    (void) f;
    (void) g;
    (void) h;
}
GXTexFmt GXGetTexObjFmt(const GXTexObj* o)
{
    return (GXTexFmt) (o->dummy[2] & 255);
}
u16 GXGetTexObjWidth(const GXTexObj* o)
{
    return (u16) o->dummy[1];
}
u16 GXGetTexObjHeight(const GXTexObj* o)
{
    return (u16) (o->dummy[1] >> 16);
}
void* GXGetTexObjData(const GXTexObj* o)
{
    return (void*) o->dummy[0];
}
void GXProject(f32 x, f32 y, f32 z, f32 m[3][4], f32* pm, f32* vp, f32* sx,
               f32* sy, f32* sz)
{
    if (!m || !pm || !vp || !sx || !sy || !sz) {
        return;
    }
    f32 ex = m[0][3] + m[0][0] * x + m[0][1] * y + m[0][2] * z;
    f32 ey = m[1][3] + m[1][0] * x + m[1][1] * y + m[1][2] * z;
    f32 ez = m[2][3] + m[2][0] * x + m[2][1] * y + m[2][2] * z;
    f32 xc, yc, zc, wc;
    if (pm[0] == 0.0f) {
        xc = ex * pm[1] + ez * pm[2];
        yc = ey * pm[3] + ez * pm[4];
        zc = pm[6] + ez * pm[5];
        wc = 1.0f / -ez;
    } else {
        xc = pm[2] + ex * pm[1];
        yc = pm[4] + ey * pm[3];
        zc = pm[6] + ez * pm[5];
        wc = 1.0f;
    }
    *sx = vp[2] * 0.5f + vp[0] + wc * xc * vp[2] * 0.5f;
    *sy = vp[3] * 0.5f + vp[1] - wc * yc * vp[3] * 0.5f;
    *sz = vp[5] + wc * zc * (vp[5] - vp[4]);
}

void GXSetViewport(f32 l, f32 t, f32 w, f32 h, f32 n, f32 f)
{
    gx_viewport[0] = l;
    gx_viewport[1] = t;
    gx_viewport[2] = w;
    gx_viewport[3] = h;
    gx_viewport[4] = n;
    gx_viewport[5] = f;
}
void GXSetViewportJitter(f32 l, f32 t, f32 w, f32 h, f32 n, f32 f, u32 q)
{
    (void) q;
    GXSetViewport(l, t, w, h, n, f);
}
void GXGetViewportv(f32* p)
{
    if (p) {
        memcpy(p, gx_viewport, sizeof gx_viewport);
    }
}
void GXSetScissor(u32 l, u32 t, u32 w, u32 h)
{
    gx_scissor[0] = l;
    gx_scissor[1] = t;
    gx_scissor[2] = w;
    gx_scissor[3] = h;
}
void GXClearVtxDesc(void)
{
    for (u32 format = 0; format < GX_MAX_VTXFMT; format++) {
        for (u32 attr = 0; attr < GX_VA_MAX_ATTR; attr++) {
            gx_vtx_state[format][attr].desc = GX_NONE;
        }
    }
}

void GXEnableTexOffsets(GXTexCoordID coord, u8 line_enable, u8 point_enable) {}

void GXInitTlutObj(GXTlutObj* tlut_obj, void* lut, GXTlutFmt fmt,
                   u16 n_entries)
{
    if (tlut_obj == NULL) {
        return;
    }
    memset(tlut_obj, 0, sizeof *tlut_obj);
    tlut_obj->dummy[0] = (uptr) lut;
    tlut_obj->dummy[1] = (uptr) fmt;
    tlut_obj->dummy[2] = (uptr) n_entries;
}
void GXInvalidateTexAll(void)
{
    gx_metal_invalidate_textures();
}
void GXInvalidateVtxCache(void) {}

void GXLoadTexObj(GXTexObj* obj, GXTexMapID id)
{
    if (obj == NULL || id >= GX_MAX_TEXMAP) {
        return;
    }
    gx_textures[id].data = (const u8*) obj->dummy[0];
    gx_textures[id].width = (u16) obj->dummy[1];
    gx_textures[id].height = (u16) (obj->dummy[1] >> 16);
    gx_textures[id].format = (GXTexFmt) (obj->dummy[2] & 0xff);
    gx_textures[id].wrap_s = (GXTexWrapMode) ((obj->dummy[2] >> 8) & 0xff);
    gx_textures[id].wrap_t = (GXTexWrapMode) ((obj->dummy[2] >> 16) & 0xff);
    gx_textures[id].min_filter = (GXTexFilter) (obj->dummy[4] & 0xff);
    gx_textures[id].mag_filter = (GXTexFilter) ((obj->dummy[4] >> 8) & 0xff);
    gx_textures[id].tlut = (u32) obj->dummy[3];
    gx_textures[id].base_size = gx_texture_size(
        gx_textures[id].width, gx_textures[id].height, gx_textures[id].format);
    gx_textures[id].loaded = GX_TRUE;
}
void GXLoadTlut(GXTlutObj* tlut_obj, u32 tlut_name)
{
    u32 slot = tlut_name & 0x1f;
    if (tlut_obj == NULL || slot >= sizeof gx_tluts / sizeof gx_tluts[0]) {
        return;
    }
    gx_tluts[slot].data = (const u8*) tlut_obj->dummy[0];
    gx_tluts[slot].format = (GXTlutFmt) tlut_obj->dummy[1];
    gx_tluts[slot].entries = (u16) tlut_obj->dummy[2];
    gx_tluts[slot].loaded = GX_TRUE;
}
void GXPixModeSync(void) {}

void GXSetArray(GXAttr attr, const void* base_ptr, u8 stride)
{
    attr = gx_state_attr(attr);
    if (attr >= GX_VA_MAX_ATTR) {
        return;
    }
    for (u32 format = 0; format < GX_MAX_VTXFMT; format++) {
        gx_vtx_state[format][attr].array = (const u8*) base_ptr;
        gx_vtx_state[format][attr].stride = stride;
    }
}

void GXSetDither(GXBool dither) {}

void GXSetFieldMode(GXBool field_mode, GXBool half_aspect_ratio) {}

void GXSetIndTexCoordScale(GXIndTexStageID ind_state, GXIndTexScale scale_s,
                           GXIndTexScale scale_t)
{
}
void GXSetIndTexMtx(GXIndTexMtxID mtx_id, f32 offset[2][3], s8 scale_exp) {}
void GXSetIndTexOrder(GXIndTexStageID ind_stage, GXTexCoordID tex_coord,
                      GXTexMapID tex_map)
{
}

void GXSetMisc(GXMiscToken token, u32 val) {}

void GXSetNumIndStages(u8 nIndStages) {}

void GXSetPixelFmt(GXPixelFmt pix_fmt, GXZFmt16 z_fmt) {}

void GXSetTevClampMode(int a, int b)
{
    (void) a;
    (void) b;
}

void GXSetTevDirect(GXTevStageID tev_stage) {}
void GXSetTevIndirect(GXTevStageID tev_stage, GXIndTexStageID ind_stage,
                      GXIndTexFormat format, GXIndTexBiasSel bias_sel,
                      GXIndTexMtxID matrix_sel, GXIndTexWrap wrap_s,
                      GXIndTexWrap wrap_t, GXBool add_prev, GXBool utc_lod,
                      GXIndTexAlphaSel alpha_sel)
{
}

void GXSetVtxAttrFmt(GXVtxFmt vtxfmt, GXAttr attr, GXCompCnt cnt,
                     GXCompType type, u8 frac)
{
    attr = gx_state_attr(attr);
    if (vtxfmt >= GX_MAX_VTXFMT || attr >= GX_VA_MAX_ATTR) {
        return;
    }
    gx_vtx_state[vtxfmt][attr].cnt = cnt;
    gx_vtx_state[vtxfmt][attr].type = type;
    gx_vtx_state[vtxfmt][attr].frac = frac;
}
void GXSetVtxDesc(GXAttr attr, GXAttrType type)
{
    attr = gx_state_attr(attr);
    if (attr >= GX_VA_MAX_ATTR) {
        return;
    }
    for (u32 format = 0; format < GX_MAX_VTXFMT; format++) {
        gx_vtx_state[format][attr].desc = type;
    }
}
void GXSetVtxDescv(const GXVtxDescList* list)
{
    if (list == NULL) {
        return;
    }
    while (list->attr != GX_VA_NULL) {
        GXSetVtxDesc(list->attr, list->type);
        list++;
    }
}

void GXSetZTexture(GXZTexOp op, GXTexFmt fmt, u32 bias) {}
void GXCallDisplayList(void* list, u32 nbytes)
{
    const u8* cursor;
    const u8* end;
    if (list == NULL || nbytes == 0 || (uintptr_t) list > UINTPTR_MAX - nbytes)
    {
        return;
    }
    cursor = (const u8*) list;
    end = cursor + nbytes;
    while (cursor < end) {
        u8 command = *cursor++;
        if (gx_dl_is_primitive(command)) {
            u16 count;
            GXPrimitive primitive = (GXPrimitive) (command & 0xf8);
            GXVtxFmt format = (GXVtxFmt) (command & 7);
            if (!gx_dl_read_be16(&cursor, end, &count)) {
                return;
            }
            GXBegin(primitive, format, count);
            for (u16 vertex = 0; vertex < count; vertex++) {
                if (!gx_dl_vertex(&cursor, end, format)) {
                    GXEnd();
                    return;
                }
            }
            GXEnd();
            continue;
        }
        /* A PObj display list is normally a stream of primitive commands.
         * Handle the common padding command and stop on other commands rather
         * than guessing a payload length and reading into the next object. */
        if (command == GX_NOP) {
            continue;
        }
        if (!gx_dl_skip_state_command(command, &cursor, end)) {
            return;
        }
    }
}
void GXAbortFrame(void) {}
void GXAdjustForOverscan(GXRenderModeObj* rmin, GXRenderModeObj* rmout,
                         u16 hor, u16 ver)
{
}
void GXBeginDisplayList(void* list, u32 size) {}
void GXClearBoundingBox(void) {}
void GXClearGPMetric(void) {}
void GXClearMemMetric(void) {}
void GXClearPixMetric(void) {}
void GXClearVCacheMetric(void) {}
u32 GXCompressZ16(u32 z24, GXZFmt16 zfmt)
{
    return 0;
}
u32 GXDecompressZ16(u32 z16, GXZFmt16 zfmt)
{
    return 0;
}
void GXDisableBreakPt(void) {}
void GXDrawCube(void) {}
void GXDrawCylinder(u8 numEdges) {}
void GXDrawDodeca(void) {}
void GXDrawIcosahedron(void) {}
void GXDrawOctahedron(void) {}
void GXDrawSphere(u8 numMajor, u8 numMinor) {}
void GXDrawSphere1(u8 depth) {}
void GXDrawTorus(f32 rc, u8 numc, u8 numt) {}
void GXEnableBreakPt(void* break_pt) {}
u32 GXEndDisplayList(void)
{
    return 0;
}
void GXFlush(void) {}
u32 GXGenNormalTable(u8 depth, f32* table)
{
    return 0;
}
GXFifoObj* GXGetCPUFifo(void)
{
    return 0;
}

OSThread* GXGetCurrentGXThread(void)
{
    return 0;
}
void* GXGetFifoBase(GXFifoObj* fifo)
{
    return 0;
}
void GXGetFifoLimits(GXFifoObj* fifo, u32* hi, u32* lo) {}
void GXGetFifoPtrs(GXFifoObj* fifo, void** readPtr, void** writePtr) {}
u32 GXGetFifoSize(GXFifoObj* fifo)
{
    return 0;
}
void GXGetFifoStatus(GXFifoObj* fifo, GXBool* overhi, GXBool* underflow,
                     u32* fifoCount, GXBool* cpuWrite, GXBool* gpRead,
                     GXBool* fifowrap)
{
}
GXFifoObj* GXGetGPFifo(void)
{
    return 0;
}
void GXGetGPStatus(GXBool* overhi, GXBool* underlow, GXBool* readIdle,
                   GXBool* cmdIdle, GXBool* brkpt)
{
}

void GXGetLineWidth(u8* width, GXTexOffset* texOffsets) {}
u32 GXGetOverflowCount(void)
{
    return 0;
}
void GXGetPointSize(u8* pointSize, GXTexOffset* texOffsets) {}
void GXGetScissor(u32* left, u32* top, u32* wd, u32* ht) {}
void GXGetTexObjAll(const GXTexObj* obj, void** image_ptr, u16* width,
                    u16* height, GXTexFmt* format, GXTexWrapMode* wrap_s,
                    GXTexWrapMode* wrap_t, u8* mipmap)
{
}
GXBool GXGetTexObjBiasClamp(const GXTexObj* tex_obj)
{
    return 0;
}
GXBool GXGetTexObjEdgeLOD(const GXTexObj* tex_obj)
{
    return 0;
}
void GXGetTexObjLODAll(const GXTexObj* tex_obj, GXTexFilter* min_filt,
                       GXTexFilter* mag_filt, f32* min_lod, f32* max_lod,
                       f32* lod_bias, u8* bias_clamp, u8* do_edge_lod,
                       GXAnisotropy* max_aniso)
{
}
f32 GXGetTexObjLODBias(const GXTexObj* tex_obj)
{
    return 0;
}
GXTexFilter GXGetTexObjMagFilt(const GXTexObj* tex_obj)
{
    return 0;
}
GXAnisotropy GXGetTexObjMaxAniso(const GXTexObj* tex_obj)
{
    return 0;
}
f32 GXGetTexObjMaxLOD(const GXTexObj* tex_obj)
{
    return 0;
}
GXTexFilter GXGetTexObjMinFilt(const GXTexObj* tex_obj)
{
    return 0;
}
f32 GXGetTexObjMinLOD(const GXTexObj* tex_obj)
{
    return 0;
}
GXBool GXGetTexObjMipMap(const GXTexObj* to)
{
    return 0;
}
u32 GXGetTexObjTlut(const GXTexObj* tex_obj)
{
    return 0;
}
void* GXGetTexObjUserData(const GXTexObj* obj)
{
    return 0;
}
GXTexWrapMode GXGetTexObjWrapS(const GXTexObj* to)
{
    return 0;
}
GXTexWrapMode GXGetTexObjWrapT(const GXTexObj* to)
{
    return 0;
}
void GXGetTexRegionAll(const GXTexRegion* region, u8* is_cached,
                       u8* is_32b_mipmap, u32* tmem_even, u32* size_even,
                       u32* tmem_odd, u32* size_odd)
{
}
void GXGetTlutObjAll(const GXTlutObj* tlut_obj, void** data, GXTlutFmt* format,
                     u16* numEntries)
{
}
void* GXGetTlutObjData(const GXTlutObj* tlut_obj)
{
    return 0;
}
GXTlutFmt GXGetTlutObjFmt(const GXTlutObj* tlut_obj)
{
    return 0;
}
u16 GXGetTlutObjNumEntries(const GXTlutObj* tlut_obj)
{
    return 0;
}
void GXGetTlutRegionAll(const GXTlutRegion* region, u32* tmem_addr,
                        GXTlutSize* tlut_size)
{
}
void GXGetVtxAttrFmt(GXVtxFmt fmt, GXAttr attr, GXCompCnt* cnt,
                     GXCompType* type, u8* frac)
{
}
void GXGetVtxAttrFmtv(GXVtxFmt fmt, GXVtxAttrFmtList* vat) {}
void GXGetVtxDesc(GXAttr attr, GXAttrType* type) {}
void GXGetVtxDescv(GXVtxDescList* vcd) {}
void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size) {}
void GXInitFifoLimits(GXFifoObj* fifo, u32 hiWatermark, u32 loWatermark) {}
void GXInitFifoPtrs(GXFifoObj* fifo, void* readPtr, void* writePtr) {}

void GXInitTexCacheRegion(GXTexRegion* region, u8 is_32b_mipmap, u32 tmem_even,
                          GXTexCacheSize size_even, u32 tmem_odd,
                          GXTexCacheSize size_odd)
{
}
void GXInitTexObjData(GXTexObj* obj, void* image_ptr)
{
    if (obj != NULL) {
        obj->dummy[0] = (uptr) image_ptr;
    }
}
void GXInitTexObjTlut(GXTexObj* obj, u32 tlut_name)
{
    if (obj != NULL) {
        obj->dummy[3] = (uptr) tlut_name;
    }
}
void GXInitTexObjUserData(GXTexObj* obj, void* user_data) {}
void GXInitTexObjWrapMode(GXTexObj* obj, GXTexWrapMode s, GXTexWrapMode t)
{
    if (obj != NULL) {
        obj->dummy[2] &= ~((uptr) 0xff << 8 | (uptr) 0xff << 16);
        obj->dummy[2] |= (uptr) s << 8 | (uptr) t << 16;
    }
}
void GXInitTexPreLoadRegion(GXTexRegion* region, u32 tmem_even, u32 size_even,
                            u32 tmem_odd, u32 size_odd)
{
}
void GXInitTlutRegion(GXTlutRegion* region, u32 tmem_addr,
                      GXTlutSize tlut_size)
{
}
void GXInitXfRasMetric(void) {}
void GXInvalidateTexRegion(GXTexRegion* region) {}
void GXLoadLightObjIndx(u32 lt_obj_indx, GXLightID light) {}

void GXLoadNrmMtxIndx3x3(u16 mtx_indx, u32 id) {}
void GXLoadPosMtxIndx(u16 mtx_indx, u32 id) {}
void GXLoadTexMtxIndx(u16 mtx_indx, u32 id, GXTexMtxType type) {}
void GXLoadTexObjPreLoaded(GXTexObj* obj, GXTexRegion* region, GXTexMapID id)
{
    (void) region;
    GXLoadTexObj(obj, id);
}

void GXPreLoadEntireTexture(GXTexObj* tex_obj, GXTexRegion* region) {}
void GXReadBoundingBox(u16* left, u16* top, u16* right, u16* bottom) {}
u32 GXReadClksPerVtx(void)
{
    return 0;
}
u32 GXReadGP0Metric(void)
{
    return 0;
}
u32 GXReadGP1Metric(void)
{
    return 0;
}
void GXReadGPMetric(u32* cnt0, u32* cnt1) {}
void GXReadMemMetric(u32* cp_req, u32* tc_req, u32* cpu_rd_req,
                     u32* cpu_wr_req, u32* dsp_req, u32* io_req, u32* vi_req,
                     u32* pe_req, u32* rf_req, u32* fi_req)
{
}
void GXReadPixMetric(u32* top_pixels_in, u32* top_pixels_out,
                     u32* bot_pixels_in, u32* bot_pixels_out,
                     u32* clr_pixels_in, u32* copy_clks)
{
}
void GXReadVCacheMetric(u32* check, u32* miss, u32* stall) {}
void GXReadXfRasMetric(u32* xf_wait_in, u32* xf_wait_out, u32* ras_busy,
                       u32* clocks)
{
}
volatile void* GXRedirectWriteGatherPipe(void* ptr)
{
    return 0;
}
u32 GXResetOverflowCount(void)
{
    return 0;
}
void GXResetWriteGatherPipe(void) {}
void GXRestoreWriteGatherPipe(void) {}
void GXSaveCPUFifo(GXFifoObj* fifo) {}
void GXSaveGPFifo(GXFifoObj* fifo) {}
GXBreakPtCallback GXSetBreakPtCallback(GXBreakPtCallback cb)
{
    return 0;
}
void GXSetCPUFifo(GXFifoObj* fifo) {}
void GXSetClipMode(GXClipMode mode) {}
void GXSetCoPlanar(GXBool enable) {}
OSThread* GXSetCurrentGXThread(void)
{
    return 0;
}

void GXSetFieldMask(GXBool odd_mask, GXBool even_mask) {}
void GXSetGPFifo(GXFifoObj* fifo) {}
void GXSetGPMetric(GXPerf0 perf0, GXPerf1 perf1) {}
void GXSetScissorBoxOffset(s32 x_off, s32 y_off) {}
void GXSetTevIndBumpST(GXTevStageID tev_stage, GXIndTexStageID ind_stage,
                       GXIndTexMtxID matrix_sel)
{
}
void GXSetTevIndBumpXYZ(GXTevStageID tev_stage, GXIndTexStageID ind_stage,
                        GXIndTexMtxID matrix_sel)
{
}
void GXSetTevIndRepeat(GXTevStageID tev_stage) {}
void GXSetTevIndTile(GXTevStageID tev_stage, GXIndTexStageID ind_stage,
                     u16 tilesize_s, u16 tilesize_t, u16 tilespacing_s,
                     u16 tilespacing_t, GXIndTexFormat format,
                     GXIndTexMtxID matrix_sel, GXIndTexBiasSel bias_sel,
                     GXIndTexAlphaSel alpha_sel)
{
}
void GXSetTevIndWarp(GXTevStageID tev_stage, GXIndTexStageID ind_stage,
                     u8 signed_offset, u8 replace_mode,
                     GXIndTexMtxID matrix_sel)
{
}
void GXSetTexCoordBias(GXTexCoordID coord, u8 s_enable, u8 t_enable) {}
void GXSetTexCoordCylWrap(GXTexCoordID coord, u8 s_enable, u8 t_enable) {}
void GXSetTexCoordScaleManually(GXTexCoordID coord, u8 enable, u16 ss, u16 ts)
{
}
GXTexRegionCallback GXSetTexRegionCallback(GXTexRegionCallback f)
{
    return 0;
}
GXTlutRegionCallback GXSetTlutRegionCallback(GXTlutRegionCallback f)
{
    return 0;
}
void GXSetVCacheMetric(GXVCachePerf attr) {}
GXVerifyCallback GXSetVerifyCallback(GXVerifyCallback cb)
{
    return 0;
}
void GXSetVerifyLevel(GXWarningLevel level) {}
void GXSetVtxAttrFmtv(GXVtxFmt vtxfmt, const GXVtxAttrFmtList* list)
{
    if (list == NULL) {
        return;
    }
    while (list->attr != GX_VA_NULL) {
        GXSetVtxAttrFmt(vtxfmt, list->attr, list->cnt, list->type, list->frac);
        list++;
    }
}
void GXTexModeSync(void) {}
