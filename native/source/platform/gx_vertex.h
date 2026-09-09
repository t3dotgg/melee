#ifndef MELEE_NATIVE_GX_VERTEX_H
#define MELEE_NATIVE_GX_VERTEX_H

/* The game submits host values. Archive display lists are decoded before
 * they enter this structure. Matrix indices select GX matrix rows. */
typedef struct GXNativeVertex {
    f32 position[3];
    f32 normal[3];
    f32 binormal[3];
    f32 tangent[3];
    f32 texcoord[8][2];
    GXColor color[2];
    u32 position_matrix;
    u32 texture_matrix[8];
    GXBool has_position_matrix;
    GXBool has_texture_matrix[8];
} GXNativeVertex;

typedef struct GXSWVertex {
    f32 x, y, z;
    f32 s, t;
    GXColor color;
    GXColor color1;
    f32 texcoord[8][2];
    f32 tex_q[8];
    f32 clip[4];
    f32 inv_w;
    GXBool projected;
} GXSWVertex;

#endif
