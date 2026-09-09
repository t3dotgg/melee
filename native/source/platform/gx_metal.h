#ifndef MELEE_NATIVE_GX_METAL_H
#define MELEE_NATIVE_GX_METAL_H

#include <stdint.h>

/* These values contain native GX draw state. They do not contain addresses
 * in a console memory map. All shader-facing fields have a fixed width. */
typedef struct NativeGXMetalVertex {
    float position[4];
    float color[2][4];
    float texcoord[8][4];
} NativeGXMetalVertex;

typedef struct NativeGXMetalCombiner {
    uint32_t input[4];
    uint32_t op, bias, scale, clamp, output;
} NativeGXMetalCombiner;

typedef struct NativeGXMetalStage {
    NativeGXMetalCombiner color, alpha;
    uint32_t map, coord, raster, raster_swap, texture_swap, kcolor, kalpha;
} NativeGXMetalStage;

typedef struct NativeGXMetalState {
    NativeGXMetalStage stage[16];
    int32_t registers[4][4];
    uint32_t konst[4][4];
    uint32_t swap[4][4];
    uint32_t stage_count;
    uint32_t z_compare, z_func, z_update, z_before_texture;
    uint32_t alpha_func[2], alpha_ref[2], alpha_op;
    uint32_t blend_mode, blend_src, blend_dst, logic_op;
    uint32_t color_update, alpha_update, dst_alpha_enabled, dst_alpha;
    uint32_t cull_mode;
    uint32_t scissor[4];
    uint32_t fog_type, fog_ortho, fog_range_enabled, fog_center;
    float fog_start, fog_end, fog_near, fog_far;
    float viewport_near, viewport_far;
    float fog_color[4];
    float fog_range[10];
} NativeGXMetalState;

typedef struct NativeGXMetalTexture {
    const uint8_t* rgba;
    uint32_t width, height;
    uint32_t wrap_s, wrap_t, min_filter, mag_filter;
    uint64_t serial;
} NativeGXMetalTexture;

/* The software GX path remains available to sanitizer tests and for state
 * that the Metal backend does not yet support. */
int NativeGXMetalInit(uint32_t width, uint32_t height);
void NativeGXMetalShutdown(void);
int NativeGXMetalSupports(const NativeGXMetalState* state);
int NativeGXMetalDraw(const NativeGXMetalVertex* vertices, uint32_t count,
                      const NativeGXMetalState* state,
                      const NativeGXMetalTexture textures[8]);
int NativeGXMetalReadback(uint8_t* rgba, float* depth);
int NativeGXMetalUpload(const uint8_t* rgba, const float* depth);

#endif
