#ifndef MELEE_NATIVE_GX_TRANSFORM_H
#define MELEE_NATIVE_GX_TRANSFORM_H

/* GX position and texture IDs name rows in the same matrix memory. Normal
 * matrices use three floats per row. Post matrices have separate memory. */
static f32 gx_transform_rows[64][4];
static f32 gx_transform_normal_rows[32][3];
static f32 gx_transform_post_rows[64][4];
static u32 gx_transform_current_matrix;
static GXBool gx_transform_has_projection;
static u8 gx_transform_num_texgens, gx_transform_num_chans;

typedef struct GXNativeTexGen {
    GXTexGenType type;
    GXTexGenSrc source;
    u32 matrix, post_matrix;
    GXBool normalize;
} GXNativeTexGen;

typedef struct GXNativeLight {
    GXColor color;
    f32 cosine[3], distance[3], position[3], direction[3];
} GXNativeLight;

typedef struct GXNativeChannel {
    GXBool enabled;
    GXColorSrc ambient_source, material_source;
    u32 light_mask;
    GXDiffuseFn diffuse;
    GXAttnFn attenuation;
} GXNativeChannel;

static GXNativeTexGen gx_transform_texgens[8];
static GXNativeLight gx_transform_lights[8];
static GXNativeChannel gx_transform_channels[4];
static GXColor gx_transform_ambient[2], gx_transform_material[2];

static f32 gx_transform_dot(const f32 a[3], const f32 b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void gx_transform_normalize(f32 value[3])
{
    f32 length = sqrtf(gx_transform_dot(value, value));
    if (length > 0.0f && isfinite(length)) {
        for (u32 i = 0; i < 3; i++) {
            value[i] /= length;
        }
    }
}

static void gx_transform_matrix(const f32 rows[][4], const f32 in[3],
                                f32 out[3])
{
    for (u32 i = 0; i < 3; i++) {
        out[i] = gx_transform_dot(rows[i], in) + rows[i][3];
    }
}

static void gx_transform_reset(void)
{
    memset(gx_transform_rows, 0, sizeof gx_transform_rows);
    memset(gx_transform_normal_rows, 0, sizeof gx_transform_normal_rows);
    memset(gx_transform_post_rows, 0, sizeof gx_transform_post_rows);
    for (u32 i = 0; i < 64; i++) {
        gx_transform_rows[i][i % 3] = 1.0f;
        gx_transform_post_rows[i][i % 3] = 1.0f;
    }
    /* GX_PTIDENTITY is row 61 in post matrix memory. */
    memset(&gx_transform_post_rows[61], 0,
           3 * sizeof gx_transform_post_rows[0]);
    for (u32 i = 0; i < 3; i++) {
        gx_transform_post_rows[61 + i][i] = 1.0f;
    }
    for (u32 i = 0; i < 32; i++) {
        gx_transform_normal_rows[i][i % 3] = 1.0f;
    }
    memset(gx_transform_lights, 0, sizeof gx_transform_lights);
    memset(gx_transform_channels, 0, sizeof gx_transform_channels);
    memset(gx_transform_ambient, 0, sizeof gx_transform_ambient);
    for (u32 i = 0; i < 2; i++) {
        gx_transform_material[i] = (GXColor) { 255, 255, 255, 255 };
    }
    for (u32 i = 0; i < 4; i++) {
        gx_transform_channels[i].material_source = GX_SRC_VTX;
        gx_transform_channels[i].attenuation = GX_AF_NONE;
    }
    for (u32 i = 0; i < 8; i++) {
        gx_transform_texgens[i] = (GXNativeTexGen) {
            GX_TG_MTX2x4, (GXTexGenSrc) (GX_TG_TEX0 + i),
            GX_IDENTITY,  GX_PTIDENTITY,
            GX_FALSE,
        };
    }
    gx_transform_num_texgens = 1;
    gx_transform_num_chans = 0;
    gx_transform_current_matrix = GX_PNMTX0;
    gx_transform_has_projection = GX_FALSE;
    memset(gx_projection, 0, sizeof gx_projection);
}

void GXLoadPosMtxImm(f32 matrix[3][4], u32 id)
{
    if (matrix != NULL && id <= 61) {
        memcpy(&gx_transform_rows[id], matrix, 3 * sizeof matrix[0]);
    }
}

void GXLoadNrmMtxImm(f32 matrix[3][4], u32 id)
{
    if (matrix == NULL || id > 29) {
        return;
    }
    for (u32 i = 0; i < 3; i++) {
        memcpy(gx_transform_normal_rows[id + i], matrix[i], 3 * sizeof(f32));
    }
}

void GXLoadNrmMtxImm3x3(f32 matrix[3][3], u32 id)
{
    if (matrix != NULL && id <= 29) {
        memcpy(&gx_transform_normal_rows[id], matrix, 3 * sizeof matrix[0]);
    }
}

void GXSetCurrentMtx(u32 id)
{
    gx_transform_current_matrix = id & 63;
}

void GXLoadTexMtxImm(f32 matrix[][4], u32 id, GXTexMtxType type)
{
    u32 count = type == GX_MTX2x4 ? 2 : 3;
    if (matrix == NULL) {
        return;
    }
    if (id >= GX_PTTEXMTX0) {
        u32 row = id - GX_PTTEXMTX0;
        if (row <= 64 - count) {
            memcpy(&gx_transform_post_rows[row], matrix,
                   count * sizeof matrix[0]);
        }
    } else if (id <= 64 - count) {
        memcpy(&gx_transform_rows[id], matrix, count * sizeof matrix[0]);
    }
}

void GXSetProjection(f32 matrix[4][4], GXProjectionType type)
{
    if (matrix == NULL) {
        return;
    }
    gx_projection[0] = (f32) type;
    gx_projection[1] = matrix[0][0];
    gx_projection[2] = matrix[0][type == GX_ORTHOGRAPHIC ? 3 : 2];
    gx_projection[3] = matrix[1][1];
    gx_projection[4] = matrix[1][type == GX_ORTHOGRAPHIC ? 3 : 2];
    gx_projection[5] = matrix[2][2];
    gx_projection[6] = matrix[2][3];
    gx_transform_has_projection = GX_TRUE;
}

void GXSetProjectionv(f32* projection)
{
    if (projection != NULL) {
        memcpy(gx_projection, projection, sizeof gx_projection);
        gx_transform_has_projection = GX_TRUE;
    }
}

void GXGetProjectionv(f32* projection)
{
    if (projection != NULL) {
        memcpy(projection, gx_projection, sizeof gx_projection);
    }
}

void GXSetTexCoordGen2(GXTexCoordID destination, GXTexGenType type,
                       GXTexGenSrc source, u32 matrix, GXBool normalize,
                       u32 post_matrix)
{
    if ((u32) destination < 8) {
        gx_transform_texgens[destination] = (GXNativeTexGen) {
            type, source, matrix & 63, post_matrix, normalize,
        };
    }
}

void GXSetNumTexGens(u8 count)
{
    gx_transform_num_texgens = count < 8 ? count : 8;
}

void GXSetNumChans(u8 count)
{
    gx_transform_num_chans = count < 2 ? count : 2;
}

/* GXLightObj keeps the SDK's 64-byte object layout. Copy floats through the
 * opaque words so the host does not need a second aliased object type. */
static void gx_transform_light_write(GXLightObj* object, u32 word, f32 x,
                                     f32 y, f32 z)
{
    if (object != NULL) {
        f32 value[3] = { x, y, z };
        memcpy(&object->dummy[word], value, sizeof value);
    }
}

static void gx_transform_light_read(const GXLightObj* object, u32 word,
                                    f32 value[3])
{
    memcpy(value, &object->dummy[word], 3 * sizeof(f32));
}

void GXInitLightAttnA(GXLightObj* object, f32 a0, f32 a1, f32 a2)
{
    gx_transform_light_write(object, 4, a0, a1, a2);
}

void GXInitLightAttnK(GXLightObj* object, f32 k0, f32 k1, f32 k2)
{
    gx_transform_light_write(object, 7, k0, k1, k2);
}

void GXInitLightAttn(GXLightObj* object, f32 a0, f32 a1, f32 a2, f32 k0,
                     f32 k1, f32 k2)
{
    GXInitLightAttnA(object, a0, a1, a2);
    GXInitLightAttnK(object, k0, k1, k2);
}

void GXGetLightAttnA(GXLightObj* object, f32* a0, f32* a1, f32* a2)
{
    if (object != NULL && a0 != NULL && a1 != NULL && a2 != NULL) {
        f32 value[3];
        gx_transform_light_read(object, 4, value);
        *a0 = value[0];
        *a1 = value[1];
        *a2 = value[2];
    }
}

void GXGetLightAttnK(GXLightObj* object, f32* k0, f32* k1, f32* k2)
{
    if (object != NULL && k0 != NULL && k1 != NULL && k2 != NULL) {
        f32 value[3];
        gx_transform_light_read(object, 7, value);
        *k0 = value[0];
        *k1 = value[1];
        *k2 = value[2];
    }
}

void GXInitLightPos(GXLightObj* object, f32 x, f32 y, f32 z)
{
    gx_transform_light_write(object, 10, x, y, z);
}

void GXGetLightPos(GXLightObj* object, f32* x, f32* y, f32* z)
{
    if (object != NULL && x != NULL && y != NULL && z != NULL) {
        f32 value[3];
        gx_transform_light_read(object, 10, value);
        *x = value[0];
        *y = value[1];
        *z = value[2];
    }
}

void GXInitLightDir(GXLightObj* object, f32 x, f32 y, f32 z)
{
    gx_transform_light_write(object, 13, -x, -y, -z);
}

void GXGetLightDir(GXLightObj* object, f32* x, f32* y, f32* z)
{
    if (object != NULL && x != NULL && y != NULL && z != NULL) {
        f32 value[3];
        gx_transform_light_read(object, 13, value);
        *x = -value[0];
        *y = -value[1];
        *z = -value[2];
    }
}

void GXInitLightColor(GXLightObj* object, GXColor color)
{
    if (object != NULL) {
        object->dummy[3] = ((u32) color.r << 24) | ((u32) color.g << 16) |
                           ((u32) color.b << 8) | color.a;
    }
}

void GXGetLightColor(GXLightObj* object, GXColor* color)
{
    if (object != NULL && color != NULL) {
        u32 packed = object->dummy[3];
        *color = (GXColor) { packed >> 24, packed >> 16, packed >> 8, packed };
    }
}

void GXInitLightSpot(GXLightObj* object, f32 cutoff, GXSpotFn function)
{
    if (cutoff <= 0.0f || cutoff > 90.0f || !isfinite(cutoff)) {
        function = GX_SP_OFF;
    }
    f32 cosine = cosf(cutoff * (3.1415927f / 180.0f));
    f32 width = 1.0f - cosine;
    f32 square = width * width;
    switch (function) {
    case GX_SP_FLAT:
        GXInitLightAttnA(object, -1000.0f * cosine, 1000.0f, 0.0f);
        break;
    case GX_SP_COS:
        GXInitLightAttnA(object, -cosine / width, 1.0f / width, 0.0f);
        break;
    case GX_SP_COS2:
        GXInitLightAttnA(object, 0.0f, -cosine / width, 1.0f / width);
        break;
    case GX_SP_SHARP:
        GXInitLightAttnA(object, cosine * (cosine - 2.0f) / square,
                         2.0f / square, -1.0f / square);
        break;
    case GX_SP_RING1:
        GXInitLightAttnA(object, -4.0f * cosine / square,
                         4.0f * (1.0f + cosine) / square, -4.0f / square);
        break;
    case GX_SP_RING2:
        GXInitLightAttnA(object, 1.0f - 2.0f * cosine * cosine / square,
                         4.0f * cosine / square, -2.0f / square);
        break;
    default:
        GXInitLightAttnA(object, 1.0f, 0.0f, 0.0f);
        break;
    }
}

void GXInitLightDistAttn(GXLightObj* object, f32 distance, f32 brightness,
                         GXDistAttnFn function)
{
    if (distance <= 0.0f || brightness <= 0.0f || brightness >= 1.0f ||
        !isfinite(distance) || !isfinite(brightness))
    {
        function = GX_DA_OFF;
    }
    switch (function) {
    case GX_DA_GENTLE:
        GXInitLightAttnK(object, 1.0f,
                         (1.0f - brightness) / (brightness * distance), 0.0f);
        break;
    case GX_DA_MEDIUM: {
        f32 slope = 0.5f * (1.0f - brightness) / (brightness * distance);
        GXInitLightAttnK(object, 1.0f, slope, slope / distance);
        break;
    }
    case GX_DA_STEEP:
        GXInitLightAttnK(object, 1.0f, 0.0f,
                         (1.0f - brightness) /
                             (brightness * distance * distance));
        break;
    default:
        GXInitLightAttnK(object, 1.0f, 0.0f, 0.0f);
        break;
    }
}

void GXInitSpecularDirHA(GXLightObj* object, f32 x, f32 y, f32 z, f32 hx,
                         f32 hy, f32 hz)
{
    gx_transform_light_write(object, 13, hx, hy, hz);
    GXInitLightPos(object, -1048576.0f * x, -1048576.0f * y, -1048576.0f * z);
}

void GXInitSpecularDir(GXLightObj* object, f32 x, f32 y, f32 z)
{
    f32 half[3] = { -x, -y, 1.0f - z };
    gx_transform_normalize(half);
    GXInitSpecularDirHA(object, x, y, z, half[0], half[1], half[2]);
}

void GXLoadLightObjImm(GXLightObj* object, GXLightID light)
{
    u32 mask = (u32) light;
    if (object == NULL || mask == 0 || mask > GX_LIGHT7 ||
        (mask & (mask - 1)) != 0)
    {
        return;
    }
    u32 index = 0;
    while ((mask >>= 1) != 0) {
        index++;
    }
    GXNativeLight* output = &gx_transform_lights[index];
    GXGetLightColor(object, &output->color);
    gx_transform_light_read(object, 4, output->cosine);
    gx_transform_light_read(object, 7, output->distance);
    gx_transform_light_read(object, 10, output->position);
    gx_transform_light_read(object, 13, output->direction);
}

static void gx_transform_set_channel_color(GXColor colors[2],
                                           GXChannelID channel, GXColor color)
{
    if ((u32) channel > GX_COLOR1A1) {
        return;
    }
    GXColor* output = &colors[(u32) channel & 1];
    if (channel != GX_ALPHA0 && channel != GX_ALPHA1) {
        output->r = color.r;
        output->g = color.g;
        output->b = color.b;
    }
    if (channel != GX_COLOR0 && channel != GX_COLOR1) {
        output->a = color.a;
    }
}

void GXSetChanAmbColor(GXChannelID channel, GXColor color)
{
    gx_transform_set_channel_color(gx_transform_ambient, channel, color);
}

void GXSetChanMatColor(GXChannelID channel, GXColor color)
{
    gx_transform_set_channel_color(gx_transform_material, channel, color);
}

void GXSetChanCtrl(GXChannelID channel, GXBool enabled, GXColorSrc ambient,
                   GXColorSrc material, u32 lights, GXDiffuseFn diffuse,
                   GXAttnFn attenuation)
{
    if ((u32) channel > GX_COLOR1A1) {
        return;
    }
    GXNativeChannel control = {
        enabled,
        ambient,
        material,
        lights & 255,
        attenuation == GX_AF_SPEC ? GX_DF_NONE : diffuse,
        attenuation,
    };
    if (channel >= GX_COLOR0A0) {
        u32 index = channel - GX_COLOR0A0;
        gx_transform_channels[index] = control;
        gx_transform_channels[index + 2] = control;
    } else {
        gx_transform_channels[channel] = control;
    }
}

static f32 gx_transform_light_scale(const GXNativeLight* light,
                                    const GXNativeChannel* channel,
                                    const f32 position[3], const f32 normal[3])
{
    f32 direction[3];
    for (u32 i = 0; i < 3; i++) {
        direction[i] = light->position[i] - position[i];
    }
    f32 distance_squared = gx_transform_dot(direction, direction);
    f32 distance = sqrtf(distance_squared);
    gx_transform_normalize(direction);
    if (distance == 0.0f) {
        memcpy(direction, normal, sizeof direction);
    }
    f32 diffuse = gx_transform_dot(direction, normal);
    f32 attenuation = 1.0f;
    if (channel->attenuation != GX_AF_NONE) {
        f32 cosine;
        f32 denominator;
        if (channel->attenuation == GX_AF_SPEC) {
            cosine =
                diffuse >= 0.0f
                    ? fmaxf(0.0f, gx_transform_dot(normal, light->direction))
                    : 0.0f;
            denominator = light->distance[0] + light->distance[1] * cosine +
                          light->distance[2] * cosine * cosine;
        } else {
            cosine =
                fmaxf(0.0f, gx_transform_dot(direction, light->direction));
            denominator = light->distance[0] + light->distance[1] * distance +
                          light->distance[2] * distance_squared;
        }
        f32 numerator =
            fmaxf(0.0f, light->cosine[0] + light->cosine[1] * cosine +
                            light->cosine[2] * cosine * cosine);
        attenuation = denominator == 0.0f ? (numerator > 0.0f ? 1.0f : 0.0f)
                                          : numerator / denominator;
    }
    if (channel->diffuse == GX_DF_CLAMP) {
        attenuation *= fmaxf(0.0f, diffuse);
    } else if (channel->diffuse == GX_DF_SIGN) {
        attenuation *= diffuse;
    }
    return isfinite(attenuation) ? attenuation : 0.0f;
}

static GXColor gx_transform_light_color(u32 index, GXColor vertex,
                                        const f32 position[3],
                                        const f32 normal[3])
{
    if (index >= gx_transform_num_chans) {
        return vertex;
    }
    GXColor output;
    u8* result = (u8*) &output;
    const u8* input = (const u8*) &vertex;
    const u8* material = (const u8*) &gx_transform_material[index];
    const u8* ambient = (const u8*) &gx_transform_ambient[index];
    for (u32 part = 0; part < 2; part++) {
        const GXNativeChannel* channel =
            &gx_transform_channels[index + part * 2];
        f32 scales[8] = { 0 };
        if (channel->enabled) {
            for (u32 light = 0; light < 8; light++) {
                if ((channel->light_mask & (1u << light)) != 0) {
                    scales[light] =
                        gx_transform_light_scale(&gx_transform_lights[light],
                                                 channel, position, normal);
                }
            }
        }
        for (u32 component = part == 0 ? 0 : 3;
             component < (part == 0 ? 3 : 4); component++)
        {
            u8 base = channel->material_source == GX_SRC_VTX
                          ? input[component]
                          : material[component];
            if (!channel->enabled) {
                result[component] = base;
                continue;
            }
            f32 illumination = channel->ambient_source == GX_SRC_VTX
                                   ? input[component]
                                   : ambient[component];
            for (u32 light = 0; light < 8; light++) {
                const u8* color =
                    (const u8*) &gx_transform_lights[light].color;
                illumination += scales[light] * color[component];
            }
            u32 level = (u32) fminf(255.0f, fmaxf(0.0f, illumination));
            result[component] = (u8) ((base * (level + (level >> 7))) >> 8);
        }
    }
    return output;
}

static void gx_transform_to_screen(GXSWVertex* vertex)
{
    vertex->inv_w = vertex->clip[3] != 0.0f ? 1.0f / vertex->clip[3] : 0.0f;
    vertex->x = gx_viewport[0] + gx_viewport[2] * 0.5f *
                                     (1.0f + vertex->clip[0] * vertex->inv_w);
    vertex->y = gx_viewport[1] + gx_viewport[3] * 0.5f *
                                     (1.0f - vertex->clip[1] * vertex->inv_w);
    vertex->z = gx_viewport[5] + vertex->clip[2] * vertex->inv_w *
                                     (gx_viewport[5] - gx_viewport[4]);
}

static GXSWVertex gx_transform_vertex(const GXNativeVertex* input)
{
    GXSWVertex output = { 0 };
    f32 position[3], normals[3][3];
    u32 matrix = input->has_position_matrix ? input->position_matrix & 63
                                            : gx_transform_current_matrix;
    if (matrix > 61) {
        matrix = GX_PNMTX0;
    }
    gx_transform_matrix(&gx_transform_rows[matrix], input->position, position);
    u32 normal_matrix = matrix & 31;
    if (normal_matrix > 29) {
        normal_matrix = GX_PNMTX0;
    }
    const f32* original_normals[3] = {
        input->normal,
        input->binormal,
        input->tangent,
    };
    for (u32 normal = 0; normal < 3; normal++) {
        for (u32 row = 0; row < 3; row++) {
            normals[normal][row] =
                gx_transform_dot(gx_transform_normal_rows[normal_matrix + row],
                                 original_normals[normal]);
        }
    }
    gx_transform_normalize(normals[0]);
    output.color =
        gx_transform_light_color(0, input->color[0], position, normals[0]);
    output.color1 =
        gx_transform_light_color(1, input->color[1], position, normals[0]);
    f32 generated[8][3];
    for (u32 i = 0; i < 8; i++) {
        generated[i][0] = input->texcoord[i][0];
        generated[i][1] = input->texcoord[i][1];
        generated[i][2] = 1.0f;
        if (i >= gx_transform_num_texgens) {
            continue;
        }
        const GXNativeTexGen* generator = &gx_transform_texgens[i];
        if (generator->type == GX_TG_SRTG) {
            GXColor color = generator->source == GX_TG_COLOR1 ? output.color1
                                                              : output.color;
            generated[i][0] = color.r / 255.0f;
            generated[i][1] = color.g / 255.0f;
        } else if (generator->type >= GX_TG_BUMP0 &&
                   generator->type <= GX_TG_BUMP7)
        {
            u32 source = (u32) generator->source - GX_TG_TEXCOORD0;
            if (source < i) {
                u32 light = generator->type - GX_TG_BUMP0;
                f32 direction[3];
                for (u32 row = 0; row < 3; row++) {
                    direction[row] = gx_transform_lights[light].position[row] -
                                     position[row];
                }
                gx_transform_normalize(direction);
                generated[i][0] = generated[source][0] +
                                  gx_transform_dot(direction, normals[1]);
                generated[i][1] = generated[source][1] +
                                  gx_transform_dot(direction, normals[2]);
                generated[i][2] = generated[source][2];
            }
        } else {
            f32 source[3] = { 0.0f, 0.0f, 1.0f };
            if (generator->source == GX_TG_POS) {
                memcpy(source, input->position, sizeof source);
            } else if (generator->source >= GX_TG_NRM &&
                       generator->source <= GX_TG_TANGENT)
            {
                memcpy(source, original_normals[generator->source - GX_TG_NRM],
                       sizeof source);
            } else if (generator->source >= GX_TG_TEX0 &&
                       generator->source <= GX_TG_TEX7)
            {
                u32 coord = generator->source - GX_TG_TEX0;
                source[0] = input->texcoord[coord][0];
                source[1] = input->texcoord[coord][1];
            }
            u32 texture_matrix = input->has_texture_matrix[i]
                                     ? input->texture_matrix[i] & 63
                                     : generator->matrix;
            if (texture_matrix > 61) {
                texture_matrix = GX_IDENTITY;
            }
            gx_transform_matrix(&gx_transform_rows[texture_matrix], source,
                                generated[i]);
            if (generator->type == GX_TG_MTX2x4) {
                generated[i][2] = 1.0f;
            }
            if (generator->normalize) {
                gx_transform_normalize(generated[i]);
            }
            u32 post = (generator->post_matrix - GX_PTTEXMTX0) & 63;
            if (post <= 61) {
                f32 temporary[3];
                memcpy(temporary, generated[i], sizeof temporary);
                gx_transform_matrix(&gx_transform_post_rows[post], temporary,
                                    generated[i]);
            }
        }
    }
    for (u32 i = 0; i < 8; i++) {
        output.texcoord[i][0] = generated[i][0];
        output.texcoord[i][1] = generated[i][1];
        output.tex_q[i] = generated[i][2];
    }
    output.s = output.texcoord[0][0];
    output.t = output.texcoord[0][1];
    output.projected = gx_transform_has_projection;
    output.clip[3] = 1.0f;
    if (output.projected) {
        GXBool perspective = gx_projection[0] == GX_PERSPECTIVE;
        f32 translation = perspective ? position[2] : 1.0f;
        output.clip[0] =
            position[0] * gx_projection[1] + translation * gx_projection[2];
        output.clip[1] =
            position[1] * gx_projection[3] + translation * gx_projection[4];
        output.clip[2] = position[2] * gx_projection[5] + gx_projection[6];
        output.clip[3] = perspective ? -position[2] : 1.0f;
        gx_transform_to_screen(&output);
    } else {
        memcpy(output.clip, position, sizeof position);
        output.inv_w = 1.0f;
        output.x = position[0];
        output.y = position[1];
        output.z = position[2];
        if (fabsf(position[0]) <= 1.01f && fabsf(position[1]) <= 1.01f) {
            output.x =
                gx_viewport[0] + (position[0] + 1.0f) * gx_viewport[2] * 0.5f;
            output.y =
                gx_viewport[1] + (1.0f - position[1]) * gx_viewport[3] * 0.5f;
        }
    }
    return output;
}

#endif
