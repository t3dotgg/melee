#ifndef MELEE_NATIVE_GX_FOG_H
#define MELEE_NATIVE_GX_FOG_H

typedef struct GXSWFog {
    GXFogType type;
    f32 start, end, near, far;
    GXColor color;
    GXBool range_enabled;
    u16 center;
    GXFogAdjTable range;
} GXSWFog;

static GXSWFog gx_fog;

static void gx_fog_reset(void)
{
    memset(&gx_fog, 0, sizeof gx_fog);
}

void GXSetFog(GXFogType type, f32 start, f32 end, f32 near, f32 far,
              GXColor color)
{
    gx_fog.type = type;
    gx_fog.start = start;
    gx_fog.end = end;
    gx_fog.near = near;
    gx_fog.far = far;
    gx_fog.color = color;
    if (!isfinite(start) || !isfinite(end) || !isfinite(near) ||
        !isfinite(far) || end == start || far <= near || far < 0.0F)
    {
        gx_fog.type = GX_FOG_NONE;
    }
}

void GXInitFogAdjTable(GXFogAdjTable* table, u16 width, f32 projection[4][4])
{
    if (table == NULL) {
        return;
    }
    for (unsigned i = 0; i < 10; i++) {
        table->r[i] = 256;
    }
    if (projection == NULL || width == 0) {
        return;
    }

    /* GXPixel.c builds ten distance ratios at 32-pixel intervals. */
    f32 near, side;
    if (projection[3][3] == 0.0F) {
        near = projection[2][3] / (projection[2][2] - 1.0F);
        side = near * (1.0F + projection[0][2]) / projection[0][0];
    } else {
        near = (1.0F + projection[2][3]) / projection[2][2];
        side = (1.0F - projection[0][3]) / projection[0][0];
    }
    if (!isfinite(near) || !isfinite(side) || near == 0.0F) {
        return;
    }
    f32 inverse_width = 2.0F / width;
    for (unsigned i = 0; i < 10; i++) {
        f32 position = (f32) ((i + 1) * 32) * inverse_width * side;
        f32 value = 256.0F * sqrtf(1.0F + position * position / (near * near));
        if (isfinite(value) && value >= 0.0F && value < 4294967296.0F) {
            table->r[i] = (u16) ((u32) value & 0xfff);
        }
    }
}

void GXSetFogRangeAdj(GXBool enabled, u16 center, GXFogAdjTable* table)
{
    gx_fog.range_enabled = enabled && table != NULL;
    /* The SDK adds a hardware origin of 342 in a ten-bit register. */
    gx_fog.center = (u16) ((center + 342) & 1023);
    if (gx_fog.range_enabled) {
        for (unsigned i = 0; i < 10; i++) {
            gx_fog.range.r[i] = table->r[i] & 0xfff;
        }
    }
}

static f32 gx_fog_range_factor(s32 x)
{
    if (!gx_fog.range_enabled) {
        return 1.0F;
    }
    f32 distance = fabsf((f32) x - ((s32) gx_fog.center - 342));
    f32 index = distance / 32.0F;
    if (index >= 10.0F) {
        return gx_fog.range.r[9] / 256.0F;
    }
    unsigned upper = (unsigned) index;
    f32 lower_value = upper == 0 ? 256.0F : gx_fog.range.r[upper - 1];
    f32 upper_value = gx_fog.range.r[upper];
    /* Linear interpolation approximates the hardware table between samples. */
    return (lower_value + (upper_value - lower_value) * (index - upper)) /
           256.0F;
}

static GXColor gx_fog_apply(GXColor color, f32 depth, s32 x)
{
    unsigned type = (unsigned) gx_fog.type & 7;
    if (type == GX_FOG_NONE || !isfinite(depth)) {
        return color;
    }
    f32 viewport_range = gx_viewport[5] - gx_viewport[4];
    if (!isfinite(viewport_range) || viewport_range == 0.0F) {
        return color;
    }
    f32 normalized = (depth - gx_viewport[4]) / viewport_range;
    if (!isfinite(normalized)) {
        return color;
    }
    normalized = fminf(1.0F, fmaxf(0.0F, normalized));

    bool orthographic =
        ((unsigned) gx_fog.type & 8) != 0 ||
        (gx_transform_has_projection && gx_projection[0] == GX_ORTHOGRAPHIC);
    double distance, start;
    if (orthographic) {
        distance = normalized * ((double) gx_fog.far - gx_fog.near);
        start = (double) gx_fog.start - gx_fog.near;
    } else {
        double denominator =
            gx_fog.far - normalized * ((double) gx_fog.far - gx_fog.near);
        if (denominator == 0.0) {
            return color;
        }
        distance = (double) gx_fog.near * gx_fog.far / denominator;
        start = gx_fog.start;
    }
    double amount = (distance * gx_fog_range_factor(x) - start) /
                    ((double) gx_fog.end - gx_fog.start);
    amount = fmin(1.0, fmax(0.0, amount));
    switch (type) {
    case GX_FOG_LIN:
        break;
    case GX_FOG_EXP:
        amount = 1.0 - exp2(-8.0 * amount);
        break;
    case GX_FOG_EXP2:
        amount = 1.0 - exp2(-8.0 * amount * amount);
        break;
    case GX_FOG_REVEXP:
        amount = exp2(-8.0 * (1.0 - amount));
        break;
    case GX_FOG_REVEXP2:
        amount = 1.0 - amount;
        amount = exp2(-8.0 * amount * amount);
        break;
    default:
        return color;
    }

    /* Fog uses an eight-bit fraction with a separate endpoint of 256.
     * It changes RGB after TEV and preserves alpha for the blend stage. */
    unsigned factor = (unsigned) floor(amount * 256.0 + 0.5);
    unsigned inverse = 256 - factor;
    color.r = (u8) ((color.r * inverse + gx_fog.color.r * factor) >> 8);
    color.g = (u8) ((color.g * inverse + gx_fog.color.g * factor) >> 8);
    color.b = (u8) ((color.b * inverse + gx_fog.color.b * factor) >> 8);
    return color;
}

#endif
