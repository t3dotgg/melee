#ifndef MELEE_NATIVE_GX_METAL_BRIDGE_H
#define MELEE_NATIVE_GX_METAL_BRIDGE_H

#include "gx_metal.h"

typedef struct GXMetalTextureCache {
    struct GXMetalTextureCache* next;
    const u8* source;
    const u8* palette;
    u32 format, palette_format;
    u16 width, height, palette_entries;
    u64 epoch, hash;
    NativeGXMetalTexture texture;
} GXMetalTextureCache;

static GXMetalTextureCache* gx_metal_texture_cache;
static u64 gx_metal_texture_epoch = 1, gx_metal_texture_serial = 1;
static size_t gx_metal_texture_bytes;
static GXBool gx_metal_enabled, gx_metal_gpu_dirty, gx_metal_host_dirty;
static NativeGXMetalVertex* gx_metal_vertices;
static size_t gx_metal_vertex_capacity;

static void gx_metal_free_texture_cache(void)
{
    while (gx_metal_texture_cache) {
        GXMetalTextureCache* next = gx_metal_texture_cache->next;
        free((void*) gx_metal_texture_cache->texture.rgba);
        free(gx_metal_texture_cache);
        gx_metal_texture_cache = next;
    }
    gx_metal_texture_bytes = 0;
}

static void gx_metal_invalidate_textures(void)
{
    gx_metal_texture_epoch++;
}

static void gx_metal_reset(void)
{
    NativeGXMetalShutdown();
    gx_metal_free_texture_cache();
    const char* software = getenv("MELEE_SOFTWARE_RENDER");
    gx_metal_enabled = !(software && software[0] && software[0] != '0') &&
                       NativeGXMetalInit(gx_efb_width, gx_efb_height);
    const char* required = getenv("MELEE_REQUIRE_METAL");
    if (!gx_metal_enabled && required && required[0] && required[0] != '0') {
        fprintf(stderr, "native GX: required Metal backend is unavailable\n");
        abort();
    }
    gx_metal_gpu_dirty = GX_FALSE;
    gx_metal_host_dirty = GX_TRUE;
    if (gx_trace_enabled) {
        fprintf(stderr, "[native-gx] renderer=%s\n",
                gx_metal_enabled ? "Metal" : "software");
    }
}

static void gx_metal_sync(void)
{
    if (!gx_metal_enabled || !gx_metal_gpu_dirty) {
        return;
    }
    if (!NativeGXMetalReadback((u8*) gx_efb, gx_depth)) {
        fprintf(stderr, "native GX: Metal framebuffer readback failed\n");
        abort();
    }
    gx_metal_gpu_dirty = GX_FALSE;
}

static void gx_metal_cpu_dirty(void)
{
    gx_metal_host_dirty = GX_TRUE;
}

static u64 gx_metal_hash(const u8* data, size_t size, u64 hash)
{
    for (size_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static NativeGXMetalTexture gx_metal_texture(const GXSWTexture* source)
{
    NativeGXMetalTexture result = { 0 };
    if (!source->loaded || !source->data || !source->base_size) {
        return result;
    }
    const GXSWTlut* palette =
        source->tlut < 20 ? &gx_tluts[source->tlut] : NULL;
    bool indexed = source->format == GX_TF_C4 || source->format == GX_TF_C8 ||
                   source->format == GX_TF_C14X2;
    const u8* palette_data =
        indexed && palette && palette->loaded ? palette->data : NULL;
    u32 palette_format = palette_data ? palette->format : 0;
    u16 palette_entries = palette_data ? palette->entries : 0;
    GXMetalTextureCache* entry = gx_metal_texture_cache;
    while (entry) {
        if (entry->source == source->data && entry->palette == palette_data &&
            entry->format == source->format && entry->width == source->width &&
            entry->height == source->height &&
            entry->palette_format == palette_format &&
            entry->palette_entries == palette_entries)
        {
            break;
        }
        entry = entry->next;
    }
    if (!entry) {
        size_t bytes = (size_t) source->width * source->height * 4;
        entry = calloc(1, sizeof(*entry));
        if (!entry) {
            return result;
        }
        u8* pixels = malloc(bytes);
        if (!pixels) {
            free(entry);
            return result;
        }
        entry->source = source->data;
        entry->palette = palette_data;
        entry->format = source->format;
        entry->palette_format = palette_format;
        entry->palette_entries = palette_entries;
        entry->width = source->width;
        entry->height = source->height;
        entry->texture.rgba = pixels;
        entry->texture.width = source->width;
        entry->texture.height = source->height;
        entry->next = gx_metal_texture_cache;
        gx_metal_texture_cache = entry;
        gx_metal_texture_bytes += bytes;
    }
    if (entry->epoch != gx_metal_texture_epoch) {
        u64 hash = gx_metal_hash(source->data, source->base_size,
                                 1469598103934665603ULL);
        if (palette_data) {
            hash = gx_metal_hash(palette_data, (size_t) palette_entries * 2,
                                 hash);
        }
        if (entry->texture.serial == 0 || entry->hash != hash) {
            GXColor* pixels = (GXColor*) entry->texture.rgba;
            for (u32 y = 0; y < source->height; y++) {
                for (u32 x = 0; x < source->width; x++) {
                    pixels[(size_t) y * source->width + x] =
                        gx_texture_nearest(source, (x + 0.5f) / source->width,
                                           (y + 0.5f) / source->height);
                }
            }
            entry->hash = hash;
            entry->texture.serial = gx_metal_texture_serial++;
        }
        entry->epoch = gx_metal_texture_epoch;
    }
    result = entry->texture;
    result.wrap_s = source->wrap_s;
    result.wrap_t = source->wrap_t;
    result.min_filter = source->min_filter;
    result.mag_filter = source->mag_filter;
    return result;
}

static NativeGXMetalCombiner gx_metal_combiner(const GXSWTevCombiner* input)
{
    NativeGXMetalCombiner out;
    for (u32 i = 0; i < 4; i++) {
        out.input[i] = input->input[i];
    }
    out.op = input->op;
    out.bias = input->bias;
    out.scale = input->scale;
    out.clamp = input->clamp;
    out.output = input->output;
    return out;
}

static NativeGXMetalState gx_metal_state(void)
{
    NativeGXMetalState state = { 0 };
    state.stage_count = gx_num_tev_stages;
    for (u32 i = 0; i < gx_num_tev_stages; i++) {
        const GXSWTevStage* source = &gx_tev_stages[i];
        NativeGXMetalStage* target = &state.stage[i];
        target->color = gx_metal_combiner(&source->color);
        target->alpha = gx_metal_combiner(&source->alpha);
        target->map = source->map;
        target->coord = source->coord;
        target->raster = source->raster;
        target->kcolor = source->kcolor;
        target->kalpha = source->kalpha;
        target->raster_swap = source->raster_swap;
        target->texture_swap = source->texture_swap;
    }
    for (u32 i = 0; i < 4; i++) {
        for (u32 j = 0; j < 4; j++) {
            state.registers[i][j] = gx_tev_registers[i][j];
            state.konst[i][j] = ((u8*) &gx_tev_kcolors[i])[j];
            state.swap[i][j] = gx_tev_swaps[i][j];
        }
    }
    state.z_compare = gx_z_compare;
    state.z_func = gx_z_func;
    state.z_update = gx_z_update;
    state.z_before_texture = gx_z_before_texture;
    for (u32 i = 0; i < 2; i++) {
        state.alpha_func[i] = gx_alpha_func[i];
        state.alpha_ref[i] = gx_alpha_ref[i];
    }
    state.alpha_op = gx_alpha_op;
    state.blend_mode = gx_blend_mode;
    state.blend_src = gx_blend_src;
    state.blend_dst = gx_blend_dst;
    state.logic_op = gx_logic_op;
    state.color_update = gx_color_update;
    state.alpha_update = gx_alpha_update;
    state.dst_alpha_enabled = gx_dst_alpha_enabled;
    state.dst_alpha = gx_dst_alpha;
    state.cull_mode = gx_cull_mode;
    state.scissor[0] = gx_scissor[2] ? gx_scissor[0] : 0;
    state.scissor[1] = gx_scissor[3] ? gx_scissor[1] : 0;
    state.scissor[2] = gx_scissor[2] ? gx_scissor[2] : gx_efb_width;
    state.scissor[3] = gx_scissor[3] ? gx_scissor[3] : gx_efb_height;
    state.fog_type = gx_fog.type;
    state.fog_ortho =
        (gx_fog.type & 8) ||
        (gx_transform_has_projection && gx_projection[0] == GX_ORTHOGRAPHIC);
    state.fog_range_enabled = gx_fog.range_enabled;
    state.fog_center = gx_fog.center;
    state.fog_start = gx_fog.start;
    state.fog_end = gx_fog.end;
    state.fog_near = gx_fog.near;
    state.fog_far = gx_fog.far;
    state.viewport_near = gx_viewport[4];
    state.viewport_far = gx_viewport[5];
    for (u32 i = 0; i < 4; i++) {
        state.fog_color[i] = ((u8*) &gx_fog.color)[i];
    }
    for (u32 i = 0; i < 10; i++) {
        state.fog_range[i] = gx_fog.range.r[i];
    }
    return state;
}

static NativeGXMetalVertex gx_metal_vertex(const GXSWVertex* source)
{
    NativeGXMetalVertex v = { 0 };
    if (source->projected) {
        f32 w = source->clip[3];
        v.position[0] = (2 * gx_viewport[0] / gx_efb_width +
                         gx_viewport[2] / gx_efb_width - 1) *
                            w +
                        source->clip[0] * gx_viewport[2] / gx_efb_width;
        v.position[1] = (1 - 2 * gx_viewport[1] / gx_efb_height -
                         gx_viewport[3] / gx_efb_height) *
                            w +
                        source->clip[1] * gx_viewport[3] / gx_efb_height;
        v.position[2] = gx_viewport[5] * w +
                        source->clip[2] * (gx_viewport[5] - gx_viewport[4]);
        v.position[3] = w;
    } else {
        v.position[0] = 2 * source->x / gx_efb_width - 1;
        v.position[1] = 1 - 2 * source->y / gx_efb_height;
        v.position[2] = source->z;
        v.position[3] = 1;
    }
    for (u32 i = 0; i < 4; i++) {
        v.color[0][i] = ((const u8*) &source->color)[i];
        v.color[1][i] = ((const u8*) &source->color1)[i];
    }
    for (u32 i = 0; i < 8; i++) {
        v.texcoord[i][0] = source->texcoord[i][0];
        v.texcoord[i][1] = source->texcoord[i][1];
        v.texcoord[i][2] = source->tex_q[i];
    }
    return v;
}

static bool gx_metal_try_draw(void)
{
    if (!gx_metal_enabled || gx_vertex_count < 3 ||
        gx_primitive == GX_POINTS || gx_primitive == GX_LINES ||
        gx_primitive == GX_LINESTRIP)
    {
        return false;
    }
    NativeGXMetalState state = gx_metal_state();
    if (!NativeGXMetalSupports(&state)) {
        return false;
    }
    size_t capacity = (size_t) gx_vertex_count * 3;
    if (capacity > gx_metal_vertex_capacity) {
        void* resized =
            realloc(gx_metal_vertices, capacity * sizeof(*gx_metal_vertices));
        if (!resized) {
            return false;
        }
        gx_metal_vertices = resized;
        gx_metal_vertex_capacity = capacity;
    }
    u32 count = 0;
    u32 step = gx_primitive == GX_TRIANGLES ? 3
               : gx_primitive == GX_QUADS   ? 4
                                            : 1;
    for (u32 i = 0; i + 2 < gx_vertex_count; i += step) {
        u32 indices[6] = { i, i + 1, i + 2, i, i + 2, i + 3 };
        u32 vertices = 3;
        if (gx_primitive == GX_QUADS) {
            if (i + 3 >= gx_vertex_count) {
                break;
            }
            vertices = 6;
        }
        if (gx_primitive == GX_TRIANGLEFAN) {
            indices[0] = 0;
        }
        if (gx_primitive == GX_TRIANGLESTRIP && (i & 1)) {
            indices[0] = i + 1;
            indices[1] = i;
        }
        for (u32 j = 0; j < vertices; j++) {
            gx_metal_vertices[count++] =
                gx_metal_vertex(&gx_vertices[indices[j]]);
        }
    }
    if (count == 0) {
        return true;
    }
    if (gx_metal_texture_bytes > 128u * 1024u * 1024u) {
        gx_metal_free_texture_cache();
    }
    NativeGXMetalTexture textures[8] = { 0 };
    u32 maps = 0;
    for (u32 i = 0; i < gx_num_tev_stages; i++) {
        if ((u32) gx_tev_stages[i].map < 8) {
            maps |= 1u << gx_tev_stages[i].map;
        }
    }
    for (u32 i = 0; i < 8; i++) {
        if (maps & (1u << i)) {
            textures[i] = gx_metal_texture(&gx_textures[i]);
        }
    }
    if (gx_metal_host_dirty) {
        if (!NativeGXMetalUpload((u8*) gx_efb, gx_depth)) {
            return false;
        }
        gx_metal_host_dirty = GX_FALSE;
    }
    if (!NativeGXMetalDraw(gx_metal_vertices, count, &state, textures)) {
        return false;
    }
    gx_metal_gpu_dirty = GX_TRUE;
    return true;
}

#endif
