#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <dolphin/gx.h>

_Static_assert(sizeof(((GXTexObj *) 0)->dummy[0]) == sizeof(void *),
               "native GX texture state must keep host pointers");
static GXFifoObj gx_fifo; static GXFifoObj *gx_cpu_fifo, *gx_gp_fifo;
static GXDrawDoneCallback gx_done_cb; static GXDrawSyncCallback gx_sync_cb; static u16 gx_draw_token;
static f32 gx_projection[7]; static f32 gx_viewport[6]={0,0,640,480,0,1}; static u32 gx_scissor[4]; static u16 gx_copy_height;

/* A small software EFB keeps the native build useful before a Metal backend
 * exists.  GX's display copy format is RGB565, stored in big endian order in
 * the XFB.  The game mostly uses direct vertices, so this tracks that stream
 * and rasterizes the common triangle primitives. */
#define GX_SW_MAX_WIDTH 1024u
#define GX_SW_MAX_HEIGHT 1024u
typedef struct GXSWVertex {
    f32 x, y, z;
    GXColor color;
} GXSWVertex;
static u16 *gx_efb;
static u32 gx_efb_width = 640, gx_efb_height = 480;
static GXPrimitive gx_primitive;
static u16 gx_expected_vertices, gx_vertex_count;
static GXVtxFmt gx_vertex_format;
static GXSWVertex gx_vertices[32];
static GXSWVertex gx_pending_vertex;
static GXBool gx_pending_position;
static GXColor gx_current_color = { 255, 255, 255, 255 };
static GXColor gx_copy_clear_color;
static GXBool gx_color_update = GX_TRUE;
static GXBool gx_in_begin;
static f32 gx_pos_mtx[3][4] = {
    { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 },
};
static GXBool gx_have_pos_mtx, gx_have_projection;
static u16 gx_copy_left, gx_copy_top, gx_copy_width, gx_copy_src_height;
static u16 gx_copy_dst_width;

static void gx_ensure_efb(void)
{
    if (gx_efb != NULL) return;
    gx_efb = calloc((size_t) gx_efb_width * gx_efb_height, sizeof(*gx_efb));
}

static u16 gx_pack_rgb565(GXColor c)
{
    return (u16) (((u16) (c.r >> 3) << 11) | ((u16) (c.g >> 2) << 5) |
                  (u16) (c.b >> 3));
}

static GXSWVertex gx_transform_vertex(f32 x, f32 y, f32 z)
{
    GXSWVertex v = { x, y, z, gx_current_color };
    if (gx_have_pos_mtx) {
        f32 tx = gx_pos_mtx[0][3] + gx_pos_mtx[0][0] * x +
                 gx_pos_mtx[0][1] * y + gx_pos_mtx[0][2] * z;
        f32 ty = gx_pos_mtx[1][3] + gx_pos_mtx[1][0] * x +
                 gx_pos_mtx[1][1] * y + gx_pos_mtx[1][2] * z;
        f32 tz = gx_pos_mtx[2][3] + gx_pos_mtx[2][0] * x +
                 gx_pos_mtx[2][1] * y + gx_pos_mtx[2][2] * z;
        v.x = tx; v.y = ty; v.z = tz;
    }
    if (gx_have_projection) {
        f32 xc, yc, zc, w;
        if (gx_projection[0] == GX_ORTHOGRAPHIC) {
            xc = gx_projection[2] + v.x * gx_projection[1];
            yc = gx_projection[4] + v.y * gx_projection[3];
            zc = gx_projection[6] + v.z * gx_projection[5];
            w = 1.0f;
        } else {
            xc = gx_projection[2] + v.x * gx_projection[1];
            yc = gx_projection[4] + v.y * gx_projection[3];
            zc = gx_projection[6] + v.z * gx_projection[5];
            w = v.z == 0.0f ? 1.0f : 1.0f / -v.z;
        }
        v.x = gx_viewport[0] + gx_viewport[2] * (0.5f + 0.5f * w * xc);
        v.y = gx_viewport[1] + gx_viewport[3] * (0.5f - 0.5f * w * yc);
        v.z = gx_viewport[4] + (gx_viewport[5] - gx_viewport[4]) * w * zc;
    } else if (v.x >= -1.01f && v.x <= 1.01f && v.y >= -1.01f && v.y <= 1.01f) {
        v.x = gx_viewport[0] + (v.x + 1.0f) * gx_viewport[2] * 0.5f;
        v.y = gx_viewport[1] + (1.0f - v.y) * gx_viewport[3] * 0.5f;
    }
    return v;
}

static void gx_plot(s32 x, s32 y, GXColor color)
{
    u32 left = gx_scissor[2] != 0 ? gx_scissor[0] : 0;
    u32 top = gx_scissor[3] != 0 ? gx_scissor[1] : 0;
    u32 right = gx_scissor[2] != 0 ? left + gx_scissor[2] : gx_efb_width;
    u32 bottom = gx_scissor[3] != 0 ? top + gx_scissor[3] : gx_efb_height;
    if (x < (s32) left || y < (s32) top || x >= (s32) right ||
        y >= (s32) bottom || x < 0 || y < 0 || (u32) x >= gx_efb_width ||
        (u32) y >= gx_efb_height || !gx_color_update) return;
    gx_efb[(size_t) y * gx_efb_width + (u32) x] = gx_pack_rgb565(color);
}

static GXColor gx_lerp_color(const GXSWVertex *a, const GXSWVertex *b,
                             const GXSWVertex *c, f32 wa, f32 wb, f32 wc)
{
    GXColor out;
    out.r = (u8) fminf(255.0f, fmaxf(0.0f, wa*a->color.r + wb*b->color.r + wc*c->color.r));
    out.g = (u8) fminf(255.0f, fmaxf(0.0f, wa*a->color.g + wb*b->color.g + wc*c->color.g));
    out.b = (u8) fminf(255.0f, fmaxf(0.0f, wa*a->color.b + wb*b->color.b + wc*c->color.b));
    out.a = (u8) fminf(255.0f, fmaxf(0.0f, wa*a->color.a + wb*b->color.a + wc*c->color.a));
    return out;
}

static void gx_triangle(const GXSWVertex *a, const GXSWVertex *b,
                        const GXSWVertex *c)
{
    f32 area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (fabsf(area) < 0.0001f) return;
    s32 min_x = (s32) floorf(fminf(a->x, fminf(b->x, c->x)));
    s32 max_x = (s32) ceilf(fmaxf(a->x, fmaxf(b->x, c->x)));
    s32 min_y = (s32) floorf(fminf(a->y, fminf(b->y, c->y)));
    s32 max_y = (s32) ceilf(fmaxf(a->y, fmaxf(b->y, c->y)));
    for (s32 y = min_y; y <= max_y; y++) {
        for (s32 x = min_x; x <= max_x; x++) {
            f32 px = (f32) x + 0.5f, py = (f32) y + 0.5f;
            f32 wa = ((b->x-px)*(c->y-py) - (b->y-py)*(c->x-px)) / area;
            f32 wb = ((c->x-px)*(a->y-py) - (c->y-py)*(a->x-px)) / area;
            f32 wc = 1.0f - wa - wb;
            if (wa >= 0.0f && wb >= 0.0f && wc >= 0.0f)
                gx_plot(x, y, gx_lerp_color(a, b, c, wa, wb, wc));
        }
    }
}

static void gx_commit_vertex(void)
{
    if (!gx_pending_position) return;
    if (gx_vertex_count < (u16) (sizeof(gx_vertices) / sizeof(gx_vertices[0])))
        gx_vertices[gx_vertex_count++] = gx_pending_vertex;
    gx_pending_position = GX_FALSE;
}

static void gx_rasterize(void)
{
    gx_commit_vertex();
    if (gx_vertex_count < 1) return;
    if (gx_primitive == GX_POINTS) {
        for (u16 i = 0; i < gx_vertex_count; i++)
            gx_plot((s32) lroundf(gx_vertices[i].x), (s32) lroundf(gx_vertices[i].y), gx_vertices[i].color);
    } else if (gx_primitive == GX_LINES || gx_primitive == GX_LINESTRIP) {
        for (u16 i = 0; i + 1 < gx_vertex_count; i += gx_primitive == GX_LINES ? 2 : 1) {
            s32 x0 = (s32) lroundf(gx_vertices[i].x), y0 = (s32) lroundf(gx_vertices[i].y);
            s32 x1 = (s32) lroundf(gx_vertices[i+1].x), y1 = (s32) lroundf(gx_vertices[i+1].y);
            s32 n = abs(x1-x0) > abs(y1-y0) ? abs(x1-x0) : abs(y1-y0);
            for (s32 j=0; j<=n; j++) { f32 q = n ? (f32)j/n : 0; gx_plot((s32) lroundf(x0+(x1-x0)*q), (s32) lroundf(y0+(y1-y0)*q), gx_vertices[i].color); }
        }
    } else {
        u16 step = gx_primitive == GX_TRIANGLES ? 3 : 1;
        for (u16 i = 0; i + 2 < gx_vertex_count; i += step) {
            if (gx_primitive == GX_QUADS) {
                if (i + 3 >= gx_vertex_count) break;
                gx_triangle(&gx_vertices[i], &gx_vertices[i+1], &gx_vertices[i+2]);
                gx_triangle(&gx_vertices[i], &gx_vertices[i+2], &gx_vertices[i+3]);
                i += 1;
            } else if (gx_primitive == GX_TRIANGLEFAN) {
                gx_triangle(&gx_vertices[0], &gx_vertices[i+1], &gx_vertices[i+2]);
            } else if (gx_primitive == GX_TRIANGLESTRIP) {
                gx_triangle(&gx_vertices[i], &gx_vertices[i+1], &gx_vertices[i+2]);
            } else {
                gx_triangle(&gx_vertices[i], &gx_vertices[i+1], &gx_vertices[i+2]);
            }
        }
    }
}
GXRenderModeObj GXNtsc480Int = { .fbWidth = 640, .efbHeight = 480,
                                 .xfbHeight = 480, .viWidth = 640,
                                 .viHeight = 480 };
GXRenderModeObj GXNtsc480IntDf = { .fbWidth = 640, .efbHeight = 480,
                                   .xfbHeight = 480, .viWidth = 640,
                                   .viHeight = 480 };
GXRenderModeObj GXNtsc480Prog = { .viTVmode = 2, .fbWidth = 640,
                                  .efbHeight = 480, .xfbHeight = 480,
                                  .viWidth = 640, .viHeight = 480 };
GXFifoObj *GXInit(void *b,u32 s){(void)b;(void)s;memset(&gx_fifo,0,sizeof gx_fifo);gx_cpu_fifo=gx_gp_fifo=&gx_fifo;free(gx_efb);gx_efb=NULL;gx_ensure_efb();memset(gx_projection,0,sizeof gx_projection);memset(gx_scissor,0,sizeof gx_scissor);gx_have_projection=GX_FALSE;gx_have_pos_mtx=GX_FALSE;gx_copy_width=640;gx_copy_src_height=480;gx_copy_dst_width=640;gx_copy_height=480;gx_color_update=GX_TRUE;gx_copy_clear_color=(GXColor){0,0,0,255};return &gx_fifo;}
GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback c){GXDrawDoneCallback o=gx_done_cb;gx_done_cb=c;return o;}
GXDrawSyncCallback GXSetDrawSyncCallback(GXDrawSyncCallback c){GXDrawSyncCallback o=gx_sync_cb;gx_sync_cb=c;return o;}
void GXSetDrawSync(u16 t){gx_draw_token=t;if(gx_sync_cb)gx_sync_cb(t);} u16 GXReadDrawSync(void){return gx_draw_token;}
void GXSetDrawDone(void){if(gx_done_cb)gx_done_cb();} void GXWaitDrawDone(void){} void GXDrawDone(void){GXSetDrawDone();}
void GXBegin(GXPrimitive t,GXVtxFmt f,u16 n){gx_ensure_efb();gx_primitive=t;gx_vertex_format=f;gx_expected_vertices=n;gx_vertex_count=0;gx_pending_position=GX_FALSE;gx_in_begin=GX_TRUE;} void GXEnd(void){if (!gx_in_begin) return;gx_rasterize();gx_in_begin=GX_FALSE;}
#define V1(n,t) void n##1##t(t x){(void)x;}
#define V2(n,t) void n##2##t(t x,t y){(void)x;(void)y;}
#define V3(n,t) void n##3##t(t x,t y,t z){(void)x;(void)y;(void)z;}
#define V4(n,t) void n##4##t(t x,t y,t z,t w){(void)x;(void)y;(void)z;(void)w;}
V1(GXParam,f32) V1(GXParam,u8)
static void gx_position(f32 x, f32 y, f32 z) { gx_commit_vertex(); gx_pending_vertex = gx_transform_vertex(x,y,z); gx_pending_position = GX_TRUE; }
void GXPosition2f32(f32 x, f32 y) { gx_position(x,y,0.0f); }
void GXPosition3f32(f32 x, f32 y, f32 z) { gx_position(x,y,z); }
void GXPosition2u8(u8 x, u8 y) { gx_position((f32)x,(f32)y,0.0f); }
void GXPosition3u8(u8 x, u8 y, u8 z) { gx_position((f32)x,(f32)y,(f32)z); }
V3(GXNormal,f32)
V1(GXColor,u16)
void GXColor1u32(u32 x) { gx_current_color=(GXColor){(u8)(x>>24),(u8)(x>>16),(u8)(x>>8),(u8)x}; }
void GXColor3u8(u8 r,u8 g,u8 b) { gx_current_color=(GXColor){r,g,b,255}; }
void GXColor4u8(u8 r,u8 g,u8 b,u8 a) { gx_current_color=(GXColor){r,g,b,a}; }
V1(GXTexCoord,u8) V1(GXTexCoord,u16) V2(GXTexCoord,f32) V2(GXTexCoord,u8)
void GXColor1x16(u16 x){(void)x;} void GXColor1x8(u8 x){(void)x;} void GXTexCoord1x16(u16 x){(void)x;} void GXTexCoord1x8(u8 x){(void)x;}
u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, u8 mipmap,
                       u8 max_lod)
{
    u32 x_shift;
    u32 y_shift;
    u32 tile_bytes = (format == GX_TF_RGBA8 || format == GX_TF_Z24X8) ? 64 : 32;
    switch (format & 0xf) {
    case GX_TF_I4: case GX_TF_CMPR: case GX_TF_C4: x_shift = 3; y_shift = 3; break;
    case GX_TF_I8: case GX_TF_IA4: case GX_TF_C8: case GX_TF_Z8: x_shift = 3; y_shift = 2; break;
    default: x_shift = 2; y_shift = 2; break;
    }
    if (!mipmap) {
        return ((width + (1u << x_shift) - 1) >> x_shift) *
               ((height + (1u << y_shift) - 1) >> y_shift) * tile_bytes;
    }
    u32 size = 0;
    for (u32 level = 0; level < max_lod; level++) {
        size += ((width + (1u << x_shift) - 1) >> x_shift) *
                ((height + (1u << y_shift) - 1) >> y_shift) * tile_bytes;
        if (width == 1 && height == 1) break;
        width = width > 1 ? width >> 1 : 1;
        height = height > 1 ? height >> 1 : 1;
    }
    return size;
}
void GXInitTexObj(GXTexObj*o,void*d,u16 w,u16 h,GXTexFmt f,GXTexWrapMode s,GXTexWrapMode t,u8 m){memset(o,0,sizeof*o);o->dummy[0]=(uptr)d;o->dummy[1]=(uptr)w|((uptr)h<<16);o->dummy[2]=(uptr)f|((uptr)s<<8)|((uptr)t<<16)|((uptr)m<<24);}
void GXInitTexObjCI(GXTexObj*o,void*d,u16 w,u16 h,GXTexFmt f,GXTexWrapMode s,GXTexWrapMode t,u8 m,u32 tl){GXInitTexObj(o,d,w,h,f,s,t,m);o->dummy[3]=tl;}
void GXInitTexObjLOD(GXTexObj*o,GXTexFilter a,GXTexFilter b,f32 c,f32 d,f32 e,GXBool f,GXBool g,GXAnisotropy h){o->dummy[4]=(uptr)a|((uptr)b<<8);(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
GXTexFmt GXGetTexObjFmt(const GXTexObj*o){return (GXTexFmt)(o->dummy[2]&255);} u16 GXGetTexObjWidth(const GXTexObj*o){return (u16)o->dummy[1];} u16 GXGetTexObjHeight(const GXTexObj*o){return (u16)(o->dummy[1]>>16);} void*GXGetTexObjData(const GXTexObj*o){return(void*)o->dummy[0];}
void GXProject(f32 x, f32 y, f32 z, f32 m[3][4], f32 *pm, f32 *vp,
               f32 *sx, f32 *sy, f32 *sz)
{
    if (!m || !pm || !vp || !sx || !sy || !sz) return;
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
void GXSetProjection(f32 m[4][4], GXProjectionType t)
{
    gx_projection[0] = (f32)t;
    gx_projection[1] = m[0][0];
    gx_projection[2] = (t == GX_ORTHOGRAPHIC) ? m[0][3] : m[0][2];
    gx_projection[3] = m[1][1];
    gx_projection[4] = (t == GX_ORTHOGRAPHIC) ? m[1][3] : m[1][2];
    gx_projection[5] = m[2][2];
    gx_projection[6] = m[2][3];
    gx_have_projection = GX_TRUE;
}
void GXSetProjectionv(f32 *p) { if (p) { memcpy(gx_projection, p, sizeof gx_projection); gx_have_projection = GX_TRUE; } }
void GXGetProjectionv(f32 *p) { if (p) memcpy(p, gx_projection, sizeof gx_projection); }
void GXSetViewport(f32 l,f32 t,f32 w,f32 h,f32 n,f32 f){gx_viewport[0]=l;gx_viewport[1]=t;gx_viewport[2]=w;gx_viewport[3]=h;gx_viewport[4]=n;gx_viewport[5]=f;} void GXSetViewportJitter(f32 l,f32 t,f32 w,f32 h,f32 n,f32 f,u32 q){(void)q;GXSetViewport(l,t,w,h,n,f);} void GXGetViewportv(f32*p){if(p)memcpy(p,gx_viewport,sizeof gx_viewport);} void GXSetScissor(u32 l,u32 t,u32 w,u32 h){gx_scissor[0]=l;gx_scissor[1]=t;gx_scissor[2]=w;gx_scissor[3]=h;}
void GXClearVtxDesc(void) {}
void GXCopyDisp(void *dest, GXBool clear) {
    gx_ensure_efb();
    if (dest != NULL) {
        u16 width = gx_copy_width != 0 ? gx_copy_width : (u16) gx_efb_width;
        u16 height = gx_copy_src_height != 0 ? gx_copy_src_height : gx_copy_height;
        u16 dst_width = gx_copy_dst_width != 0 ? gx_copy_dst_width : width;
        if (width > gx_efb_width) width = gx_efb_width;
        if (height > gx_efb_height) height = gx_efb_height;
        u8 *out = (u8 *) dest;
        u16 left = gx_copy_left < gx_efb_width ? gx_copy_left : 0;
        u16 top = gx_copy_top < gx_efb_height ? gx_copy_top : 0;
        if (left + width > gx_efb_width) width = (u16) (gx_efb_width - left);
        if (top + height > gx_efb_height) height = (u16) (gx_efb_height - top);
        for (u16 y = 0; y < gx_copy_height && y < height; y++) {
            u16 src_y = (u16) ((u32) y * height / (gx_copy_height ? gx_copy_height : height));
            for (u16 x = 0; x < dst_width; x++) {
                u16 src_x = width == dst_width ? x : (u16) ((u32) x * width / dst_width);
                u16 pixel = gx_efb[(size_t) (top + src_y) * gx_efb_width + left + src_x];
                out[((size_t)y * dst_width + x) * 2] = (u8) (pixel >> 8);
                out[((size_t)y * dst_width + x) * 2 + 1] = (u8) pixel;
            }
        }
    }
    if (clear) {
        u16 value = gx_pack_rgb565(gx_copy_clear_color);
        for (size_t i = 0, n = (size_t)gx_efb_width * gx_efb_height; i < n; i++) gx_efb[i] = value;
    }
}
void GXCopyTex(void *dest, GXBool clear) {}
void GXEnableTexOffsets(GXTexCoordID coord, u8 line_enable, u8 point_enable) {}
void GXInitFogAdjTable(GXFogAdjTable *table, u16 width, f32 projmtx[4][4]) {}
void GXInitLightAttn(GXLightObj *lt_obj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {}
void GXInitLightColor(GXLightObj *lt_obj, GXColor color) {}
void GXInitLightDir(GXLightObj *lt_obj, f32 nx, f32 ny, f32 nz) {}
void GXInitLightDistAttn(GXLightObj *lt_obj, f32 ref_dist, f32 ref_br, GXDistAttnFn dist_func) {}
void GXInitLightPos(GXLightObj *lt_obj, f32 x, f32 y, f32 z) {}
void GXInitLightSpot(GXLightObj *lt_obj, f32 cutoff, GXSpotFn spot_func) {}
void GXInitTlutObj(GXTlutObj *tlut_obj, void *lut, GXTlutFmt fmt, u16 n_entries) {}
void GXInvalidateTexAll(void) {}
void GXInvalidateVtxCache(void) {}
void GXLoadLightObjImm(GXLightObj *lt_obj, GXLightID light) {}
void GXLoadNrmMtxImm(f32 mtx[3][4], u32 id) {}
void GXLoadPosMtxImm(f32 mtx[3][4], u32 id) { (void) id; if (mtx != NULL) { memcpy(gx_pos_mtx, mtx, sizeof gx_pos_mtx); gx_have_pos_mtx = GX_TRUE; } }
void GXLoadTexMtxImm(f32 mtx[][4], u32 id, GXTexMtxType type) {}
void GXLoadTexObj(GXTexObj *obj, GXTexMapID id) {}
void GXLoadTlut(GXTlutObj *tlut_obj, u32 tlut_name) {}
void GXPixModeSync(void) {}
void GXSetAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1) {}
void GXSetAlphaUpdate(GXBool update_enable) {}
void GXSetArray(GXAttr attr, const void *base_ptr, u8 stride) {}
void GXSetBlendMode(GXBlendMode type, GXBlendFactor src_factor, GXBlendFactor dst_factor, GXLogicOp op) {}
void GXSetChanAmbColor(GXChannelID chan, GXColor amb_color) {}
void GXSetChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc amb_src, GXColorSrc mat_src, u32 light_mask, GXDiffuseFn diff_fn, GXAttnFn attn_fn) {}
void GXSetChanMatColor(GXChannelID chan, GXColor mat_color) {}
void GXSetColorUpdate(GXBool update_enable) { gx_color_update = update_enable; }
void GXSetCopyClamp(GXFBClamp clamp) {}
void GXSetCopyClear(GXColor clear_clr, u32 clear_z) { (void) clear_z; gx_copy_clear_color = clear_clr; }
void GXSetCopyFilter(GXBool aa, const u8 sample_pattern[12][2], GXBool vf, const u8 vfilter[7]) {}
void GXSetCullMode(GXCullMode mode) {}
void GXSetCurrentMtx(u32 id) {}
void GXSetDispCopyDst(u16 wd, u16 ht) { gx_copy_dst_width = wd; gx_copy_height = ht; }
void GXSetDispCopyGamma(GXGamma gamma) {}
void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht) { gx_copy_left=left; gx_copy_top=top; gx_copy_width=wd; gx_copy_src_height=ht; }
u32 GXSetDispCopyYScale(f32 vscale) { if (vscale < 1.0f) vscale = 1.0f; return (u32)((float)gx_copy_height / vscale); }
void GXSetDither(GXBool dither) {}
void GXSetDstAlpha(GXBool enable, u8 alpha) {}
void GXSetFieldMode(GXBool field_mode, GXBool half_aspect_ratio) {}
void GXSetFog(GXFogType type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor color) {}
void GXSetFogRangeAdj(GXBool enable, u16 center, GXFogAdjTable *table) {}
void GXSetIndTexCoordScale(GXIndTexStageID ind_state, GXIndTexScale scale_s, GXIndTexScale scale_t) {}
void GXSetIndTexMtx(GXIndTexMtxID mtx_id, f32 offset[2][3], s8 scale_exp) {}
void GXSetIndTexOrder(GXIndTexStageID ind_stage, GXTexCoordID tex_coord, GXTexMapID tex_map) {}
void GXSetLineWidth(u8 width, GXTexOffset texOffsets) {}
void GXSetMisc(GXMiscToken token, u32 val) {}
void GXSetNumChans(u8 nChans) {}
void GXSetNumIndStages(u8 nIndStages) {}
void GXSetNumTevStages(u8 nStages) {}
void GXSetNumTexGens(u8 nTexGens) {}
void GXSetPixelFmt(GXPixelFmt pix_fmt, GXZFmt16 z_fmt) {}
void GXSetPointSize(u8 pointSize, GXTexOffset texOffsets) {}
void GXSetTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d) {}
void GXSetTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out_reg) {}
void GXSetTevClampMode(int a, int b) { (void)a; (void)b; }
void GXSetTevColor(GXTevRegID id, GXColor color) {}
void GXSetTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d) {}
void GXSetTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out_reg) {}
void GXSetTevColorS10(GXTevRegID id, GXColorS10 color) {}
void GXSetTevDirect(GXTevStageID tev_stage) {}
void GXSetTevIndirect(GXTevStageID tev_stage, GXIndTexStageID ind_stage, GXIndTexFormat format, GXIndTexBiasSel bias_sel, GXIndTexMtxID matrix_sel, GXIndTexWrap wrap_s, GXIndTexWrap wrap_t, GXBool add_prev, GXBool utc_lod, GXIndTexAlphaSel alpha_sel) {}
void GXSetTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel sel) {}
void GXSetTevKColor(GXTevKColorID id, GXColor color) {}
void GXSetTevKColorSel(GXTevStageID stage, GXTevKColorSel sel) {}
void GXSetTevOp(GXTevStageID id, GXTevMode mode) {}
void GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID color) {}
void GXSetTevSwapMode(GXTevStageID stage, GXTevSwapSel ras_sel, GXTevSwapSel tex_sel) {}
void GXSetTevSwapModeTable(GXTevSwapSel table, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue, GXTevColorChan alpha) {}
void GXSetTexCoordGen2(GXTexCoordID dst_coord, GXTexGenType func, GXTexGenSrc src_param, u32 mtx, GXBool normalize, u32 pt_texmtx) {}
void GXSetTexCopyDst(u16 wd, u16 ht, GXTexFmt fmt, GXBool mipmap) {}
void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht) {}
void GXSetVtxAttrFmt(GXVtxFmt vtxfmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {}
void GXSetVtxDesc(GXAttr attr, GXAttrType type) {}
void GXSetZCompLoc(GXBool before_tex) {}
void GXSetZMode(GXBool compare_enable, GXCompare func, GXBool update_enable) {}
void GXSetZTexture(GXZTexOp op, GXTexFmt fmt, u32 bias) {}
void GXCallDisplayList(void *list, u32 nbytes) { (void)list; (void)nbytes; }
void GXAbortFrame(void) {}
void GXAdjustForOverscan(GXRenderModeObj *rmin, GXRenderModeObj *rmout, u16 hor, u16 ver) {}
void GXBeginDisplayList(void *list, u32 size) {}
void GXClearBoundingBox(void) {}
void GXClearGPMetric(void) {}
void GXClearMemMetric(void) {}
void GXClearPixMetric(void) {}
void GXClearVCacheMetric(void) {}
u32 GXCompressZ16(u32 z24, GXZFmt16 zfmt) { return 0; }
u32 GXDecompressZ16(u32 z16, GXZFmt16 zfmt) { return 0; }
void GXDisableBreakPt(void) {}
void GXDrawCube(void) {}
void GXDrawCylinder(u8 numEdges) {}
void GXDrawDodeca(void) {}
void GXDrawIcosahedron(void) {}
void GXDrawOctahedron(void) {}
void GXDrawSphere(u8 numMajor, u8 numMinor) {}
void GXDrawSphere1(u8 depth) {}
void GXDrawTorus(f32 rc, u8 numc, u8 numt) {}
void GXEnableBreakPt(void *break_pt) {}
u32 GXEndDisplayList(void) { return 0; }
void GXFlush(void) {}
u32 GXGenNormalTable(u8 depth, f32 *table) { return 0; }
GXFifoObj *GXGetCPUFifo(void) { return 0; }
void GXGetCullMode(GXCullMode *mode) {}
OSThread *GXGetCurrentGXThread(void) { return 0; }
void *GXGetFifoBase(GXFifoObj *fifo) { return 0; }
void GXGetFifoLimits(GXFifoObj *fifo, u32 *hi, u32 *lo) {}
void GXGetFifoPtrs(GXFifoObj *fifo, void **readPtr, void **writePtr) {}
u32 GXGetFifoSize(GXFifoObj *fifo) { return 0; }
void GXGetFifoStatus(GXFifoObj *fifo, GXBool *overhi, GXBool *underflow, u32 *fifoCount, GXBool *cpuWrite, GXBool *gpRead, GXBool *fifowrap) {}
GXFifoObj *GXGetGPFifo(void) { return 0; }
void GXGetGPStatus(GXBool *overhi, GXBool *underlow, GXBool *readIdle, GXBool *cmdIdle, GXBool *brkpt) {}
void GXGetLightAttnA(GXLightObj *lt_obj, f32 *a0, f32 *a1, f32 *a2) {}
void GXGetLightAttnK(GXLightObj *lt_obj, f32 *k0, f32 *k1, f32 *k2) {}
void GXGetLightColor(GXLightObj *lt_obj, GXColor *color) {}
void GXGetLightDir(GXLightObj *lt_obj, f32 *nx, f32 *ny, f32 *nz) {}
void GXGetLightPos(GXLightObj *lt_obj, f32 *x, f32 *y, f32 *z) {}
void GXGetLineWidth(u8 *width, GXTexOffset *texOffsets) {}
u32 GXGetOverflowCount(void) { return 0; }
void GXGetPointSize(u8 *pointSize, GXTexOffset *texOffsets) {}
void GXGetScissor(u32 *left, u32 *top, u32 *wd, u32 *ht) {}
void GXGetTexObjAll(const GXTexObj *obj, void **image_ptr, u16 *width, u16 *height, GXTexFmt *format, GXTexWrapMode *wrap_s, GXTexWrapMode *wrap_t, u8 *mipmap) {}
GXBool GXGetTexObjBiasClamp(const GXTexObj *tex_obj) { return 0; }
GXBool GXGetTexObjEdgeLOD(const GXTexObj *tex_obj) { return 0; }
void GXGetTexObjLODAll(const GXTexObj *tex_obj, GXTexFilter *min_filt, GXTexFilter *mag_filt, f32 *min_lod, f32 *max_lod, f32 *lod_bias, u8 *bias_clamp, u8 *do_edge_lod, GXAnisotropy *max_aniso) {}
f32 GXGetTexObjLODBias(const GXTexObj *tex_obj) { return 0; }
GXTexFilter GXGetTexObjMagFilt(const GXTexObj *tex_obj) { return 0; }
GXAnisotropy GXGetTexObjMaxAniso(const GXTexObj *tex_obj) { return 0; }
f32 GXGetTexObjMaxLOD(const GXTexObj *tex_obj) { return 0; }
GXTexFilter GXGetTexObjMinFilt(const GXTexObj *tex_obj) { return 0; }
f32 GXGetTexObjMinLOD(const GXTexObj *tex_obj) { return 0; }
GXBool GXGetTexObjMipMap(const GXTexObj *to) { return 0; }
u32 GXGetTexObjTlut(const GXTexObj *tex_obj) { return 0; }
void *GXGetTexObjUserData(const GXTexObj *obj) { return 0; }
GXTexWrapMode GXGetTexObjWrapS(const GXTexObj *to) { return 0; }
GXTexWrapMode GXGetTexObjWrapT(const GXTexObj *to) { return 0; }
void GXGetTexRegionAll(const GXTexRegion *region, u8 *is_cached, u8 *is_32b_mipmap, u32 *tmem_even, u32 *size_even, u32 *tmem_odd, u32 *size_odd) {}
void GXGetTlutObjAll(const GXTlutObj *tlut_obj, void **data, GXTlutFmt *format, u16 *numEntries) {}
void *GXGetTlutObjData(const GXTlutObj *tlut_obj) { return 0; }
GXTlutFmt GXGetTlutObjFmt(const GXTlutObj *tlut_obj) { return 0; }
u16 GXGetTlutObjNumEntries(const GXTlutObj *tlut_obj) { return 0; }
void GXGetTlutRegionAll(const GXTlutRegion *region, u32 *tmem_addr, GXTlutSize *tlut_size) {}
void GXGetVtxAttrFmt(GXVtxFmt fmt, GXAttr attr, GXCompCnt *cnt, GXCompType *type, u8 *frac) {}
void GXGetVtxAttrFmtv(GXVtxFmt fmt, GXVtxAttrFmtList *vat) {}
void GXGetVtxDesc(GXAttr attr, GXAttrType *type) {}
void GXGetVtxDescv(GXVtxDescList *vcd) {}
void GXInitFifoBase(GXFifoObj *fifo, void *base, u32 size) {}
void GXInitFifoLimits(GXFifoObj *fifo, u32 hiWatermark, u32 loWatermark) {}
void GXInitFifoPtrs(GXFifoObj *fifo, void *readPtr, void *writePtr) {}
void GXInitLightAttnA(GXLightObj *lt_obj, f32 a0, f32 a1, f32 a2) {}
void GXInitLightAttnK(GXLightObj *lt_obj, f32 k0, f32 k1, f32 k2) {}
void GXInitSpecularDir(GXLightObj *lt_obj, f32 nx, f32 ny, f32 nz) {}
void GXInitSpecularDirHA(GXLightObj *lt_obj, f32 nx, f32 ny, f32 nz, f32 hx, f32 hy, f32 hz) {}
void GXInitTexCacheRegion(GXTexRegion *region, u8 is_32b_mipmap, u32 tmem_even, GXTexCacheSize size_even, u32 tmem_odd, GXTexCacheSize size_odd) {}
void GXInitTexObjData(GXTexObj *obj, void *image_ptr) {}
void GXInitTexObjTlut(GXTexObj *obj, u32 tlut_name) {}
void GXInitTexObjUserData(GXTexObj *obj, void *user_data) {}
void GXInitTexObjWrapMode(GXTexObj *obj, GXTexWrapMode s, GXTexWrapMode t) {}
void GXInitTexPreLoadRegion(GXTexRegion *region, u32 tmem_even, u32 size_even, u32 tmem_odd, u32 size_odd) {}
void GXInitTlutRegion(GXTlutRegion *region, u32 tmem_addr, GXTlutSize tlut_size) {}
void GXInitXfRasMetric(void) {}
void GXInvalidateTexRegion(GXTexRegion *region) {}
void GXLoadLightObjIndx(u32 lt_obj_indx, GXLightID light) {}
void GXLoadNrmMtxImm3x3(f32 mtx[3][3], u32 id) {}
void GXLoadNrmMtxIndx3x3(u16 mtx_indx, u32 id) {}
void GXLoadPosMtxIndx(u16 mtx_indx, u32 id) {}
void GXLoadTexMtxIndx(u16 mtx_indx, u32 id, GXTexMtxType type) {}
void GXLoadTexObjPreLoaded(GXTexObj *obj, GXTexRegion *region, GXTexMapID id) {}
void GXPeekARGB(u16 x, u16 y, u32 *color) {}
void GXPeekZ(u16 x, u16 y, u32 *z) {}
void GXPokeARGB(u16 x, u16 y, u32 color) {}
void GXPokeAlphaMode(GXCompare func, u8 threshold) {}
void GXPokeAlphaRead(GXAlphaReadMode mode) {}
void GXPokeAlphaUpdate(GXBool update_enable) {}
void GXPokeBlendMode(GXBlendMode type, GXBlendFactor src_factor, GXBlendFactor dst_factor, GXLogicOp op) {}
void GXPokeColorUpdate(GXBool update_enable) {}
void GXPokeDither(GXBool dither) {}
void GXPokeDstAlpha(GXBool enable, u8 alpha) {}
void GXPokeZ(u16 x, u16 y, u32 z) {}
void GXPokeZMode(GXBool compare_enable, GXCompare func, GXBool update_enable) {}
void GXPreLoadEntireTexture(GXTexObj *tex_obj, GXTexRegion *region) {}
void GXReadBoundingBox(u16 *left, u16 *top, u16 *right, u16 *bottom) {}
u32 GXReadClksPerVtx(void) { return 0; }
u32 GXReadGP0Metric(void) { return 0; }
u32 GXReadGP1Metric(void) { return 0; }
void GXReadGPMetric(u32 *cnt0, u32 *cnt1) {}
void GXReadMemMetric(u32 *cp_req, u32 *tc_req, u32 *cpu_rd_req, u32 *cpu_wr_req, u32 *dsp_req, u32 *io_req, u32 *vi_req, u32 *pe_req, u32 *rf_req, u32 *fi_req) {}
void GXReadPixMetric(u32 *top_pixels_in, u32 *top_pixels_out, u32 *bot_pixels_in, u32 *bot_pixels_out, u32 *clr_pixels_in, u32 *copy_clks) {}
void GXReadVCacheMetric(u32 *check, u32 *miss, u32 *stall) {}
void GXReadXfRasMetric(u32 *xf_wait_in, u32 *xf_wait_out, u32 *ras_busy, u32 *clocks) {}
volatile void *GXRedirectWriteGatherPipe(void *ptr) { return 0; }
u32 GXResetOverflowCount(void) { return 0; }
void GXResetWriteGatherPipe(void) {}
void GXRestoreWriteGatherPipe(void) {}
void GXSaveCPUFifo(GXFifoObj *fifo) {}
void GXSaveGPFifo(GXFifoObj *fifo) {}
GXBreakPtCallback GXSetBreakPtCallback(GXBreakPtCallback cb) { return 0; }
void GXSetCPUFifo(GXFifoObj *fifo) {}
void GXSetClipMode(GXClipMode mode) {}
void GXSetCoPlanar(GXBool enable) {}
OSThread *GXSetCurrentGXThread(void) { return 0; }
void GXSetDispCopyFrame2Field(GXCopyMode mode) {}
void GXSetFieldMask(GXBool odd_mask, GXBool even_mask) {}
void GXSetGPFifo(GXFifoObj *fifo) {}
void GXSetGPMetric(GXPerf0 perf0, GXPerf1 perf1) {}
void GXSetScissorBoxOffset(s32 x_off, s32 y_off) {}
void GXSetTevIndBumpST(GXTevStageID tev_stage, GXIndTexStageID ind_stage, GXIndTexMtxID matrix_sel) {}
void GXSetTevIndBumpXYZ(GXTevStageID tev_stage, GXIndTexStageID ind_stage, GXIndTexMtxID matrix_sel) {}
void GXSetTevIndRepeat(GXTevStageID tev_stage) {}
void GXSetTevIndTile(GXTevStageID tev_stage, GXIndTexStageID ind_stage, u16 tilesize_s,
    u16 tilesize_t, u16 tilespacing_s, u16 tilespacing_t, GXIndTexFormat format,
    GXIndTexMtxID matrix_sel, GXIndTexBiasSel bias_sel, GXIndTexAlphaSel alpha_sel) {}
void GXSetTevIndWarp(GXTevStageID tev_stage, GXIndTexStageID ind_stage, u8 signed_offset, u8 replace_mode, GXIndTexMtxID matrix_sel) {}
void GXSetTexCoordBias(GXTexCoordID coord, u8 s_enable, u8 t_enable) {}
void GXSetTexCoordCylWrap(GXTexCoordID coord, u8 s_enable, u8 t_enable) {}
void GXSetTexCoordScaleManually(GXTexCoordID coord, u8 enable, u16 ss, u16 ts) {}
GXTexRegionCallback GXSetTexRegionCallback(GXTexRegionCallback f) { return 0; }
GXTlutRegionCallback GXSetTlutRegionCallback(GXTlutRegionCallback f) { return 0; }
void GXSetVCacheMetric(GXVCachePerf attr) {}
GXVerifyCallback GXSetVerifyCallback(GXVerifyCallback cb) { return 0; }
void GXSetVerifyLevel(GXWarningLevel level) {}
void GXSetVtxAttrFmtv(GXVtxFmt vtxfmt, const GXVtxAttrFmtList *list) {}
void GXSetVtxDescv(const GXVtxDescList *attrPtr) {}
void GXTexModeSync(void) {}
