#ifndef MELEE_NATIVE_GX_RASTER_H
#define MELEE_NATIVE_GX_RASTER_H

#include "gx_tev.h"

static GXBool gx_z_compare;
static GXCompare gx_z_func;
static GXBool gx_z_before_texture;
static GXCompare gx_alpha_func[2];
static u8 gx_alpha_ref[2];
static GXAlphaOp gx_alpha_op;
static GXBlendMode gx_blend_mode;
static GXBlendFactor gx_blend_src, gx_blend_dst;
static GXLogicOp gx_logic_op;
static GXBool gx_dst_alpha_enabled;
static u8 gx_dst_alpha;
static GXCullMode gx_cull_mode;
static f32 gx_line_width, gx_point_size;

static void gx_raster_reset(void)
{
    gx_color_update = gx_alpha_update = gx_z_update = GX_TRUE;
    gx_z_compare = GX_TRUE;
    gx_z_func = GX_LEQUAL;
    gx_z_before_texture = GX_FALSE;
    gx_alpha_func[0] = gx_alpha_func[1] = GX_ALWAYS;
    gx_alpha_ref[0] = gx_alpha_ref[1] = 0;
    gx_alpha_op = GX_AOP_AND;
    gx_blend_mode = GX_BM_NONE;
    gx_blend_src = GX_BL_ONE;
    gx_blend_dst = GX_BL_ZERO;
    gx_logic_op = GX_LO_COPY;
    gx_dst_alpha_enabled = GX_FALSE;
    gx_dst_alpha = 0;
    gx_cull_mode = GX_CULL_NONE;
    gx_line_width = gx_point_size = 1.0f;
}

static bool gx_compare_value(f32 source, f32 dest, GXCompare func)
{
    switch (func) {
    case GX_NEVER:
        return false;
    case GX_LESS:
        return source < dest;
    case GX_EQUAL:
        return source == dest;
    case GX_LEQUAL:
        return source <= dest;
    case GX_GREATER:
        return source > dest;
    case GX_NEQUAL:
        return source != dest;
    case GX_GEQUAL:
        return source >= dest;
    case GX_ALWAYS:
        return true;
    default:
        return false;
    }
}

static bool gx_alpha_pass(u8 alpha)
{
    bool a = gx_compare_value(alpha, gx_alpha_ref[0], gx_alpha_func[0]);
    bool b = gx_compare_value(alpha, gx_alpha_ref[1], gx_alpha_func[1]);
    switch (gx_alpha_op) {
    case GX_AOP_AND:
        return a && b;
    case GX_AOP_OR:
        return a || b;
    case GX_AOP_XOR:
        return a != b;
    case GX_AOP_XNOR:
        return a == b;
    default:
        return false;
    }
}

static u8 gx_logic_channel(u8 src, u8 dst)
{
    switch (gx_logic_op) {
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

static u32 gx_blend_factor(GXBlendFactor factor, u8 other, u8 src_a, u8 dst_a)
{
    switch (factor) {
    case GX_BL_ZERO:
        return 0;
    case GX_BL_ONE:
        return 255;
    case GX_BL_SRCCLR:
        return other;
    case GX_BL_INVSRCCLR:
        return 255 - other;
    case GX_BL_SRCALPHA:
        return src_a;
    case GX_BL_INVSRCALPHA:
        return 255 - src_a;
    case GX_BL_DSTALPHA:
        return dst_a;
    case GX_BL_INVDSTALPHA:
        return 255 - dst_a;
    default:
        return 0;
    }
}

static GXColor gx_blend_pixel(GXColor source, GXColor dest)
{
    GXColor result = source;
    u8* out = (u8*) &result;
    const u8* src = (const u8*) &source;
    const u8* dst = (const u8*) &dest;
    for (u32 i = 0; i < 4; i++) {
        if (gx_blend_mode == GX_BM_BLEND) {
            u32 sf = gx_blend_factor(gx_blend_src, dst[i], source.a, dest.a);
            u32 df = gx_blend_factor(gx_blend_dst, src[i], source.a, dest.a);
            u32 value = (src[i] * sf + dst[i] * df + 127) / 255;
            out[i] = (u8) (value > 255 ? 255 : value);
        } else if (gx_blend_mode == GX_BM_SUBTRACT) {
            out[i] = dst[i] > src[i] ? dst[i] - src[i] : 0;
        } else if (gx_blend_mode == GX_BM_LOGIC) {
            out[i] = gx_logic_channel(src[i], dst[i]);
        }
    }
    if (gx_dst_alpha_enabled) {
        result.a = gx_dst_alpha;
    }
    if (!gx_color_update) {
        result.r = dest.r;
        result.g = dest.g;
        result.b = dest.b;
    }
    if (!gx_alpha_update) {
        result.a = dest.a;
    }
    return result;
}

static void gx_plot(s32 x, s32 y, const GXSWVertex* vertex)
{
    if (x < 0 || y < 0 || (u32) x >= gx_efb_width ||
        (u32) y >= gx_efb_height || !isfinite(vertex->z))
    {
        return;
    }
    if ((gx_scissor[2] && ((u32) x < gx_scissor[0] ||
                           (u32) x - gx_scissor[0] >= gx_scissor[2])) ||
        (gx_scissor[3] && ((u32) y < gx_scissor[1] ||
                           (u32) y - gx_scissor[1] >= gx_scissor[3])))
    {
        return;
    }
    size_t index = (size_t) y * gx_efb_width + (u32) x;
    f32 depth = fminf(1.0f, fmaxf(0.0f, vertex->z));
    if (gx_z_compare && !gx_compare_value(depth, gx_depth[index], gx_z_func)) {
        return;
    }
    if (gx_z_compare && gx_z_update && gx_z_before_texture) {
        gx_depth[index] = depth;
    }
    f32 coords[8][2];
    for (u32 i = 0; i < 8; i++) {
        f32 q = vertex->tex_q[i];
        f32 inverse = isfinite(q) && fabsf(q) > 1.0e-20f ? 1.0f / q : 0.0f;
        coords[i][0] = vertex->texcoord[i][0] * inverse;
        coords[i][1] = vertex->texcoord[i][1] * inverse;
    }
    GXColor color =
        gx_shade_color(vertex->color, vertex->color1, coords, depth);
    if (!gx_alpha_pass(color.a)) {
        return;
    }
    if (gx_z_compare && gx_z_update && !gx_z_before_texture) {
        gx_depth[index] = depth;
    }
    gx_efb[index] = gx_blend_pixel(color, gx_efb[index]);
}

static u8 gx_interpolate_byte(u8 a, u8 b, u8 c, f32 wa, f32 wb, f32 wc)
{
    return (u8) fminf(255, fmaxf(0, wa * a + wb * b + wc * c));
}

static GXSWVertex gx_interpolate_vertex(const GXSWVertex* a,
                                        const GXSWVertex* b,
                                        const GXSWVertex* c, f32 wa, f32 wb,
                                        f32 wc, bool perspective)
{
    GXSWVertex out = *a;
    out.x = wa * a->x + wb * b->x + wc * c->x;
    out.y = wa * a->y + wb * b->y + wc * c->y;
    out.z = wa * a->z + wb * b->z + wc * c->z;
    for (u32 i = 0; i < 4; i++) {
        out.clip[i] = wa * a->clip[i] + wb * b->clip[i] + wc * c->clip[i];
    }
    u8* colors = (u8*) &out.color;
    u8* colors1 = (u8*) &out.color1;
    for (u32 i = 0; i < 4; i++) {
        colors[i] = gx_interpolate_byte(
            ((const u8*) &a->color)[i], ((const u8*) &b->color)[i],
            ((const u8*) &c->color)[i], wa, wb, wc);
        colors1[i] = gx_interpolate_byte(
            ((const u8*) &a->color1)[i], ((const u8*) &b->color1)[i],
            ((const u8*) &c->color1)[i], wa, wb, wc);
    }
    if (perspective) {
        f32 inverse = wa * a->inv_w + wb * b->inv_w + wc * c->inv_w;
        if (fabsf(inverse) > 1.0e-20f) {
            wa = wa * a->inv_w / inverse;
            wb = wb * b->inv_w / inverse;
            wc = wc * c->inv_w / inverse;
        }
    }
    for (u32 i = 0; i < 8; i++) {
        for (u32 j = 0; j < 2; j++) {
            out.texcoord[i][j] = wa * a->texcoord[i][j] +
                                 wb * b->texcoord[i][j] +
                                 wc * c->texcoord[i][j];
        }
        out.tex_q[i] = wa * a->tex_q[i] + wb * b->tex_q[i] + wc * c->tex_q[i];
    }
    out.s = out.texcoord[0][0];
    out.t = out.texcoord[0][1];
    return out;
}

static f32 gx_clip_distance(const GXSWVertex* v, u32 plane)
{
    switch (plane) {
    case 0:
        return v->clip[3] + v->clip[0];
    case 1:
        return v->clip[3] - v->clip[0];
    case 2:
        return v->clip[3] + v->clip[1];
    case 3:
        return v->clip[3] - v->clip[1];
    case 4:
        return v->clip[3] + v->clip[2];
    default:
        return -v->clip[2];
    }
}

static bool gx_finite_vertex(const GXSWVertex* v)
{
    if (v->projected) {
        for (u32 i = 0; i < 4; i++) {
            if (!isfinite(v->clip[i])) {
                return false;
            }
        }
        return true;
    }
    return isfinite(v->x) && isfinite(v->y) && isfinite(v->z);
}

static f32 gx_edge(const GXSWVertex* a, const GXSWVertex* b, f32 x, f32 y)
{
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

static bool gx_top_left(const GXSWVertex* a, const GXSWVertex* b)
{
    return b->y < a->y || (b->y == a->y && b->x > a->x);
}

static void gx_raster_triangle(const GXSWVertex* a, const GXSWVertex* b,
                               const GXSWVertex* c)
{
    if (!gx_finite_vertex(a) || !gx_finite_vertex(b) || !gx_finite_vertex(c)) {
        return;
    }
    f32 area = gx_edge(a, b, c->x, c->y);
    if (!isfinite(area) || fabsf(area) < 1.0e-8f) {
        return;
    }
    if (gx_cull_mode == GX_CULL_ALL ||
        (gx_cull_mode == GX_CULL_BACK && area > 0) ||
        (gx_cull_mode == GX_CULL_FRONT && area < 0))
    {
        return;
    }
    if (area < 0) {
        const GXSWVertex* tmp = b;
        b = c;
        c = tmp;
        area = -area;
    }
    /* Clamp before converting to integers. Bad archive data and vertices
     * crossing the camera plane must not create unbounded pixel loops. */
    f32 left = fmaxf(0, fminf(a->x, fminf(b->x, c->x)));
    f32 right = fminf(gx_efb_width, fmaxf(a->x, fmaxf(b->x, c->x)));
    f32 top = fmaxf(0, fminf(a->y, fminf(b->y, c->y)));
    f32 bottom = fminf(gx_efb_height, fmaxf(a->y, fmaxf(b->y, c->y)));
    if (gx_scissor[2]) {
        left = fmaxf(left, gx_scissor[0]);
        right = fminf(right, (f32) gx_scissor[0] + gx_scissor[2]);
    }
    if (gx_scissor[3]) {
        top = fmaxf(top, gx_scissor[1]);
        bottom = fminf(bottom, (f32) gx_scissor[1] + gx_scissor[3]);
    }
    if (left >= right || top >= bottom) {
        return;
    }
    s32 min_x = (s32) floorf(left), max_x = (s32) ceilf(right);
    s32 min_y = (s32) floorf(top), max_y = (s32) ceilf(bottom);
    bool edge_a = gx_top_left(b, c), edge_b = gx_top_left(c, a);
    bool edge_c = gx_top_left(a, b);
    f32 inv_area = 1.0f / area;
    for (s32 y = min_y; y < max_y; y++) {
        for (s32 x = min_x; x < max_x; x++) {
            f32 px = x + 0.5f, py = y + 0.5f;
            f32 wa = gx_edge(b, c, px, py);
            f32 wb = gx_edge(c, a, px, py);
            f32 wc = gx_edge(a, b, px, py);
            if ((wa > 0 || (wa == 0 && edge_a)) &&
                (wb > 0 || (wb == 0 && edge_b)) &&
                (wc > 0 || (wc == 0 && edge_c)))
            {
                GXSWVertex pixel =
                    gx_interpolate_vertex(a, b, c, wa * inv_area,
                                          wb * inv_area, wc * inv_area, true);
                gx_plot(x, y, &pixel);
            }
        }
    }
}

static void gx_triangle(const GXSWVertex* a, const GXSWVertex* b,
                        const GXSWVertex* c)
{
    if (!gx_finite_vertex(a) || !gx_finite_vertex(b) || !gx_finite_vertex(c)) {
        return;
    }
    if (!a->projected) {
        gx_raster_triangle(a, b, c);
        return;
    }
    GXSWVertex polygon[2][12];
    polygon[0][0] = *a;
    polygon[0][1] = *b;
    polygon[0][2] = *c;
    u32 count = 3, input = 0;
    for (u32 plane = 0; plane < 6 && count >= 3; plane++) {
        u32 output = input ^ 1, next_count = 0;
        for (u32 i = 0; i < count; i++) {
            GXSWVertex* prev = &polygon[input][i == 0 ? count - 1 : i - 1];
            GXSWVertex* curr = &polygon[input][i];
            f32 pd = gx_clip_distance(prev, plane),
                cd = gx_clip_distance(curr, plane);
            if ((pd < 0) != (cd < 0)) {
                f32 t = pd / (pd - cd);
                if (next_count >= 12) {
                    return;
                }
                polygon[output][next_count++] = gx_interpolate_vertex(
                    prev, curr, curr, 1 - t, t, 0, false);
            }
            if (cd >= 0) {
                if (next_count >= 12) {
                    return;
                }
                polygon[output][next_count++] = *curr;
            }
        }
        count = next_count;
        input = output;
    }
    for (u32 i = 0; i < count; i++) {
        if (polygon[input][i].clip[3] <= 1.0e-20f) {
            return;
        }
        gx_transform_to_screen(&polygon[input][i]);
    }
    for (u32 i = 1; i + 1 < count; i++) {
        gx_raster_triangle(&polygon[input][0], &polygon[input][i],
                           &polygon[input][i + 1]);
    }
}

static void gx_line(const GXSWVertex* first, const GXSWVertex* second)
{
    if (!gx_finite_vertex(first) || !gx_finite_vertex(second)) {
        return;
    }
    GXSWVertex a = *first, b = *second;
    if (a.projected) {
        for (u32 plane = 0; plane < 6; plane++) {
            f32 ad = gx_clip_distance(&a, plane),
                bd = gx_clip_distance(&b, plane);
            if (ad < 0 && bd < 0) {
                return;
            }
            if ((ad < 0) != (bd < 0)) {
                f32 t = ad / (ad - bd);
                GXSWVertex clipped =
                    gx_interpolate_vertex(&a, &b, &b, 1 - t, t, 0, false);
                if (ad < 0) {
                    a = clipped;
                } else {
                    b = clipped;
                }
            }
        }
        if (a.clip[3] <= 0 || b.clip[3] <= 0) {
            return;
        }
        gx_transform_to_screen(&a);
        gx_transform_to_screen(&b);
    }
    f32 dx = b.x - a.x, dy = b.y - a.y;
    f32 length = fmaxf(fabsf(dx), fabsf(dy));
    if (!isfinite(length)) {
        return;
    }
    /* Clip screen coordinates as well for direct UI vertex calls. */
    f32 begin = 0, end = 1;
    f32 p[4] = { -dx, dx, -dy, dy };
    f32 q[4] = { a.x, gx_efb_width - a.x, a.y, gx_efb_height - a.y };
    for (u32 i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0) {
                return;
            }
        } else {
            f32 r = q[i] / p[i];
            if (p[i] < 0) {
                begin = fmaxf(begin, r);
            } else {
                end = fminf(end, r);
            }
        }
    }
    if (begin > end) {
        return;
    }
    u32 steps = (u32) ceilf(fminf((end - begin) * length, 2048));
    s32 radius = (s32) floorf(gx_line_width * 0.5f);
    for (u32 i = 0; i <= steps; i++) {
        f32 t = begin + (end - begin) * (steps ? (f32) i / steps : 0);
        GXSWVertex pixel =
            gx_interpolate_vertex(&a, &b, &b, 1 - t, t, 0, true);
        s32 x = (s32) lroundf(pixel.x), y = (s32) lroundf(pixel.y);
        for (s32 offset = -radius; offset <= radius; offset++) {
            gx_plot(x + (fabsf(dx) < fabsf(dy) ? offset : 0),
                    y + (fabsf(dx) < fabsf(dy) ? 0 : offset), &pixel);
        }
    }
}

void GXSetZMode(GXBool enable, GXCompare func, GXBool update)
{
    gx_z_compare = enable;
    gx_z_func = func;
    gx_z_update = update;
}
void GXSetZCompLoc(GXBool before)
{
    gx_z_before_texture = before;
}
void GXSetAlphaCompare(GXCompare a, u8 ar, GXAlphaOp op, GXCompare b, u8 br)
{
    gx_alpha_func[0] = a;
    gx_alpha_ref[0] = ar;
    gx_alpha_op = op;
    gx_alpha_func[1] = b;
    gx_alpha_ref[1] = br;
}
void GXSetAlphaUpdate(GXBool update)
{
    gx_alpha_update = update;
}
void GXSetColorUpdate(GXBool update)
{
    gx_color_update = update;
}
void GXSetBlendMode(GXBlendMode mode, GXBlendFactor src, GXBlendFactor dst,
                    GXLogicOp op)
{
    gx_blend_mode = mode;
    gx_blend_src = src;
    gx_blend_dst = dst;
    gx_logic_op = op;
}
void GXSetDstAlpha(GXBool enabled, u8 alpha)
{
    gx_dst_alpha_enabled = enabled;
    gx_dst_alpha = alpha;
}
void GXSetCullMode(GXCullMode mode)
{
    gx_cull_mode = mode;
}
void GXGetCullMode(GXCullMode* mode)
{
    if (mode) {
        *mode = gx_cull_mode;
    }
}
void GXSetLineWidth(u8 width, GXTexOffset offsets)
{
    (void) offsets;
    gx_line_width = width / 6.0f;
}
void GXSetPointSize(u8 width, GXTexOffset offsets)
{
    (void) offsets;
    gx_point_size = width / 6.0f;
}

#endif
