#include "gx_metal.h"

#include <stdio.h>
#include <string.h>

#include "gx_metal_shader.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

/* The game submits from one thread. Render and blit encoders share one queue,
 * so a CPU EFB read or write is also a barrier for all earlier GX draws. */
@interface NativeGXMetalCachedTexture : NSObject
@property(nonatomic, strong) id<MTLTexture> texture;
@property(nonatomic) uint64_t last_use;
@property(nonatomic) size_t bytes;
@end
@implementation NativeGXMetalCachedTexture
@end

static id<MTLDevice> gx_device;
static id<MTLCommandQueue> gx_queue;
static id<MTLCommandBuffer> gx_commands;
static id<MTLRenderCommandEncoder> gx_encoder;
static id<MTLLibrary> gx_library;
static id<MTLFunction> gx_vertex_function, gx_fragment_function,
    gx_early_fragment_function;
static id<MTLTexture> gx_color_target, gx_depth_target, gx_white_texture;
static id<MTLBuffer> gx_transfer_buffer, gx_stream_buffer;
static id<MTLDepthStencilState> gx_depth_states[16];
static id<MTLSamplerState> gx_samplers[36];
static NSMutableDictionary<NSNumber*, id<MTLRenderPipelineState>>*
    gx_pipelines;
static NSMutableDictionary<NSNumber*, NativeGXMetalCachedTexture*>*
    gx_texture_cache;
static NSMutableArray<id<MTLBuffer>>* gx_stream_buffers;
static uint32_t gx_width, gx_height;
static size_t gx_row_bytes, gx_image_bytes, gx_stream_offset, gx_stream_bytes,
    gx_texture_bytes;
static uint64_t gx_texture_clock;
static int gx_failed;

static const size_t gx_texture_limit = 128 * 1024 * 1024;
static const size_t gx_stream_block = 4 * 1024 * 1024;
static const size_t gx_stream_limit = 64 * 1024 * 1024;

static int gx_metal_error(const char* operation, NSError* error)
{
    fprintf(stderr, "[native-gx-metal] %s: %s\n", operation,
            error != nil ? error.localizedDescription.UTF8String : "failed");
    gx_failed = 1;
    return 0;
}

static void gx_metal_end_encoder(void)
{
    [gx_encoder endEncoding];
    gx_encoder = nil;
}

static int gx_metal_finish(void)
{
    gx_metal_end_encoder();
    if (gx_commands != nil) {
        [gx_commands commit];
        [gx_commands waitUntilCompleted];
        if (gx_commands.status == MTLCommandBufferStatusError) {
            gx_metal_error("GPU command buffer", gx_commands.error);
        }
        gx_commands = nil;
    }
    gx_stream_buffer = nil;
    [gx_stream_buffers removeAllObjects];
    gx_stream_offset = gx_stream_bytes = 0;
    return !gx_failed;
}

static int gx_metal_begin_commands(void)
{
    if (gx_failed || gx_device == nil) {
        return 0;
    }
    if (gx_commands == nil) {
        gx_commands = [gx_queue commandBuffer];
        gx_commands.label = @"Native GX EFB";
    }
    return gx_commands != nil;
}

static int gx_metal_begin_render(void)
{
    if (!gx_metal_begin_commands()) {
        return 0;
    }
    if (gx_encoder != nil) {
        return 1;
    }
    MTLRenderPassDescriptor* pass =
        [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = gx_color_target;
    pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.depthAttachment.texture = gx_depth_target;
    pass.depthAttachment.loadAction = MTLLoadActionLoad;
    pass.depthAttachment.storeAction = MTLStoreActionStore;
    gx_encoder = [gx_commands renderCommandEncoderWithDescriptor:pass];
    if (gx_encoder == nil) {
        return gx_metal_error("render encoder", nil);
    }
    [gx_encoder setViewport:(MTLViewport) { 0, 0, gx_width, gx_height, 0, 1 }];
    /* GX front faces have positive winding after its screen Y flip. */
    [gx_encoder setFrontFacingWinding:MTLWindingClockwise];
    [gx_encoder setDepthClipMode:MTLDepthClipModeClip];
    return 1;
}

static id<MTLTexture> gx_metal_new_target(MTLPixelFormat format,
                                          const char* label)
{
    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                           width:gx_width
                                                          height:gx_height
                                                       mipmapped:NO];
    desc.storageMode = MTLStorageModePrivate;
    desc.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> texture = [gx_device newTextureWithDescriptor:desc];
    texture.label = [NSString stringWithUTF8String:label];
    return texture;
}

void NativeGXMetalShutdown(void)
{
    @autoreleasepool {
        gx_metal_finish();
        gx_transfer_buffer = gx_stream_buffer = nil;
        gx_color_target = gx_depth_target = gx_white_texture = nil;
        gx_vertex_function = gx_fragment_function =
            gx_early_fragment_function = nil;
        gx_library = nil;
        for (unsigned i = 0; i < 16; ++i) {
            gx_depth_states[i] = nil;
        }
        for (unsigned i = 0; i < 36; ++i) {
            gx_samplers[i] = nil;
        }
        gx_texture_cache = nil;
        gx_pipelines = nil;
        gx_stream_buffers = nil;
        gx_queue = nil;
        gx_device = nil;
        gx_width = gx_height = 0;
        gx_row_bytes = gx_image_bytes = gx_texture_bytes = 0;
        gx_texture_clock = 0;
        gx_failed = 0;
    }
}

int NativeGXMetalInit(uint32_t width, uint32_t height)
{
    @autoreleasepool {
        NativeGXMetalShutdown();
#if !defined(__aarch64__)
        return 0;
#endif
        if (width == 0 || height == 0 || width > 16384 || height > 16384) {
            return 0;
        }
        gx_device = MTLCreateSystemDefaultDevice();
        if (gx_device == nil) {
            return 0;
        }
        gx_width = width;
        gx_height = height;
        gx_queue = [gx_device newCommandQueue];
        gx_row_bytes = ((size_t) width * 4 + 255) & ~(size_t) 255;
        gx_image_bytes = gx_row_bytes * height;
        gx_transfer_buffer =
            [gx_device newBufferWithLength:gx_image_bytes * 2
                                   options:MTLResourceStorageModeShared];
        gx_color_target =
            gx_metal_new_target(MTLPixelFormatRGBA8Unorm, "GX color");
        gx_depth_target =
            gx_metal_new_target(MTLPixelFormatDepth32Float, "GX depth");
        gx_pipelines = [NSMutableDictionary dictionary];
        gx_texture_cache = [NSMutableDictionary dictionary];
        gx_stream_buffers = [NSMutableArray array];
        NSError* error = nil;
        MTLCompileOptions* options = [MTLCompileOptions new];
        options.fastMathEnabled = NO;
        gx_library = [gx_device
            newLibraryWithSource:
                [NSString stringWithUTF8String:native_gx_metal_shader]
                         options:options
                           error:&error];
        if (gx_library == nil) {
            gx_metal_error("compile GX shaders", error);
            NativeGXMetalShutdown();
            return 0;
        }
        gx_vertex_function =
            [gx_library newFunctionWithName:@"native_gx_vertex"];
        gx_fragment_function =
            [gx_library newFunctionWithName:@"native_gx_fragment"];
        gx_early_fragment_function =
            [gx_library newFunctionWithName:@"native_gx_fragment_early"];
        MTLTextureDescriptor* white_desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:1
                                        height:1
                                     mipmapped:NO];
        white_desc.storageMode = MTLStorageModeShared;
        white_desc.usage = MTLTextureUsageShaderRead;
        gx_white_texture = [gx_device newTextureWithDescriptor:white_desc];
        const uint8_t white[4] = { 255, 255, 255, 255 };
        [gx_white_texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                            mipmapLevel:0
                              withBytes:white
                            bytesPerRow:4];
        if (gx_queue == nil || gx_transfer_buffer == nil ||
            gx_color_target == nil || gx_depth_target == nil ||
            gx_white_texture == nil || gx_vertex_function == nil ||
            gx_fragment_function == nil)
        {
            NativeGXMetalShutdown();
            return 0;
        }
        /* Initialize both attachments before the first load action. */
        if (!gx_metal_begin_commands()) {
            NativeGXMetalShutdown();
            return 0;
        }
        MTLRenderPassDescriptor* pass =
            [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = gx_color_target;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
        pass.depthAttachment.texture = gx_depth_target;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionStore;
        pass.depthAttachment.clearDepth = 1;
        gx_encoder = [gx_commands renderCommandEncoderWithDescriptor:pass];
        int success = gx_encoder != nil && gx_metal_finish();
        if (!success) {
            NativeGXMetalShutdown();
        }
        return success;
    }
}

int NativeGXMetalSupports(const NativeGXMetalState* state)
{
    if (gx_device == nil || gx_failed || state == NULL ||
        state->stage_count > 16 || state->z_func > 7 ||
        state->alpha_func[0] > 7 || state->alpha_func[1] > 7 ||
        state->alpha_op > 3 || state->blend_mode > 3 || state->blend_src > 7 ||
        state->blend_dst > 7 || state->cull_mode > 3)
    {
        return 0;
    }
    /* Metal has no fixed-function logic ops. COPY and NOOP need no emulation.
     */
    if (state->blend_mode == 2 && state->logic_op != 3 && state->logic_op != 5)
    {
        return 0;
    }
    if (state->z_before_texture && state->z_compare && state->z_update &&
        gx_early_fragment_function == nil)
    {
        return 0;
    }
    return 1;
}

static MTLBlendFactor gx_metal_blend_factor(uint32_t factor, int source,
                                            int alpha)
{
    switch (factor) {
    case 0:
        return MTLBlendFactorZero;
    case 1:
        return MTLBlendFactorOne;
    case 2:
        return source ? (alpha ? MTLBlendFactorDestinationAlpha
                               : MTLBlendFactorDestinationColor)
                      : (alpha ? MTLBlendFactorSource1Alpha
                               : MTLBlendFactorSourceColor);
    case 3:
        return source ? (alpha ? MTLBlendFactorOneMinusDestinationAlpha
                               : MTLBlendFactorOneMinusDestinationColor)
                      : (alpha ? MTLBlendFactorOneMinusSource1Alpha
                               : MTLBlendFactorOneMinusSourceColor);
    case 4:
        return MTLBlendFactorSource1Alpha;
    case 5:
        return MTLBlendFactorOneMinusSource1Alpha;
    case 6:
        return MTLBlendFactorDestinationAlpha;
    case 7:
        return MTLBlendFactorOneMinusDestinationAlpha;
    }
    return MTLBlendFactorZero;
}

static id<MTLRenderPipelineState>
gx_metal_pipeline(const NativeGXMetalState* state)
{
    uint32_t early =
        state->z_before_texture && state->z_compare && state->z_update;
    uint32_t noop = state->blend_mode == 2 && state->logic_op == 5;
    uint32_t key =
        state->blend_mode | (state->blend_src << 2) | (state->blend_dst << 5) |
        (!!state->color_update << 8) | (!!state->alpha_update << 9) |
        (!!state->dst_alpha_enabled << 10) | (early << 11) | (noop << 12);
    id<MTLRenderPipelineState> pipeline = gx_pipelines[@(key)];
    if (pipeline != nil) {
        return pipeline;
    }
    MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
    desc.label = @"Native GX TEV";
    desc.vertexFunction = gx_vertex_function;
    desc.fragmentFunction =
        early ? gx_early_fragment_function : gx_fragment_function;
    desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    MTLRenderPipelineColorAttachmentDescriptor* color =
        desc.colorAttachments[0];
    color.pixelFormat = MTLPixelFormatRGBA8Unorm;
    color.writeMask = MTLColorWriteMaskNone;
    if (state->color_update && !noop) {
        color.writeMask |= MTLColorWriteMaskRed | MTLColorWriteMaskGreen |
                           MTLColorWriteMaskBlue;
    }
    if (state->alpha_update && (!noop || state->dst_alpha_enabled)) {
        color.writeMask |= MTLColorWriteMaskAlpha;
    }
    color.blendingEnabled = state->blend_mode == 1 || state->blend_mode == 3;
    if (state->blend_mode == 1) {
        color.sourceRGBBlendFactor =
            gx_metal_blend_factor(state->blend_src, 1, 0);
        color.destinationRGBBlendFactor =
            gx_metal_blend_factor(state->blend_dst, 0, 0);
        color.sourceAlphaBlendFactor =
            gx_metal_blend_factor(state->blend_src, 1, 1);
        color.destinationAlphaBlendFactor =
            gx_metal_blend_factor(state->blend_dst, 0, 1);
    } else if (state->blend_mode == 3) {
        color.rgbBlendOperation = color.alphaBlendOperation =
            MTLBlendOperationReverseSubtract;
        color.sourceRGBBlendFactor = color.sourceAlphaBlendFactor =
            MTLBlendFactorOne;
        color.destinationRGBBlendFactor = color.destinationAlphaBlendFactor =
            MTLBlendFactorOne;
    }
    if (state->dst_alpha_enabled) {
        color.alphaBlendOperation = MTLBlendOperationAdd;
        color.sourceAlphaBlendFactor = MTLBlendFactorOne;
        color.destinationAlphaBlendFactor = MTLBlendFactorZero;
    }
    NSError* error = nil;
    pipeline = [gx_device newRenderPipelineStateWithDescriptor:desc
                                                         error:&error];
    if (pipeline == nil) {
        gx_metal_error("GX pipeline", error);
        return nil;
    }
    if (gx_pipelines.count >= 256) {
        [gx_pipelines removeAllObjects];
    }
    gx_pipelines[@(key)] = pipeline;
    return pipeline;
}

static id<MTLDepthStencilState> gx_metal_depth(const NativeGXMetalState* state)
{
    uint32_t func = state->z_compare ? state->z_func : 7;
    uint32_t write = state->z_compare && state->z_update;
    unsigned key = func | (write << 3);
    if (gx_depth_states[key] == nil) {
        MTLDepthStencilDescriptor* desc = [MTLDepthStencilDescriptor new];
        desc.depthCompareFunction = (MTLCompareFunction) func;
        desc.depthWriteEnabled = write;
        gx_depth_states[key] =
            [gx_device newDepthStencilStateWithDescriptor:desc];
    }
    return gx_depth_states[key];
}

static id<MTLSamplerState>
gx_metal_sampler(const NativeGXMetalTexture* texture)
{
    uint32_t wrap_s = texture->wrap_s <= 2 ? texture->wrap_s : 0;
    uint32_t wrap_t = texture->wrap_t <= 2 ? texture->wrap_t : 0;
    uint32_t min_filter = texture->min_filter & 1;
    uint32_t mag_filter = !!texture->mag_filter;
    unsigned key = (wrap_s * 3 + wrap_t) * 4 + min_filter * 2 + mag_filter;
    if (gx_samplers[key] == nil) {
        static const MTLSamplerAddressMode wrap[] = {
            MTLSamplerAddressModeClampToEdge,
            MTLSamplerAddressModeRepeat,
            MTLSamplerAddressModeMirrorRepeat,
        };
        MTLSamplerDescriptor* desc = [MTLSamplerDescriptor new];
        desc.sAddressMode = wrap[wrap_s];
        desc.tAddressMode = wrap[wrap_t];
        desc.minFilter = min_filter ? MTLSamplerMinMagFilterLinear
                                    : MTLSamplerMinMagFilterNearest;
        desc.magFilter = mag_filter ? MTLSamplerMinMagFilterLinear
                                    : MTLSamplerMinMagFilterNearest;
        desc.mipFilter = MTLSamplerMipFilterNotMipmapped;
        gx_samplers[key] = [gx_device newSamplerStateWithDescriptor:desc];
    }
    return gx_samplers[key];
}

static id<MTLTexture> gx_metal_texture(const NativeGXMetalTexture* texture)
{
    if (texture->rgba == NULL || texture->width == 0 || texture->height == 0) {
        return gx_white_texture;
    }
    if (texture->width > 16384 || texture->height > 16384) {
        return nil;
    }
    NativeGXMetalCachedTexture* cached = gx_texture_cache[@(texture->serial)];
    if (texture->serial != 0 && cached != nil &&
        cached.texture.width == texture->width &&
        cached.texture.height == texture->height)
    {
        cached.last_use = ++gx_texture_clock;
        return cached.texture;
    }
    MTLTextureDescriptor* desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:texture->width
                                    height:texture->height
                                 mipmapped:NO];
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> result = [gx_device newTextureWithDescriptor:desc];
    if (result == nil) {
        return nil;
    }
    [result
        replaceRegion:MTLRegionMake2D(0, 0, texture->width, texture->height)
          mipmapLevel:0
            withBytes:texture->rgba
          bytesPerRow:(size_t) texture->width * 4];
    size_t bytes = (size_t) texture->width * texture->height * 4;
    if (texture->serial == 0 || bytes > gx_texture_limit) {
        return result;
    }
    if (cached != nil) {
        gx_texture_bytes -= cached.bytes;
        [gx_texture_cache removeObjectForKey:@(texture->serial)];
    }
    while (gx_texture_bytes + bytes > gx_texture_limit) {
        NSNumber* oldest_key = nil;
        uint64_t oldest_use = UINT64_MAX;
        for (NSNumber* key in gx_texture_cache) {
            NativeGXMetalCachedTexture* entry = gx_texture_cache[key];
            if (entry.last_use < oldest_use) {
                oldest_use = entry.last_use;
                oldest_key = key;
            }
        }
        if (oldest_key == nil) {
            break;
        }
        gx_texture_bytes -= gx_texture_cache[oldest_key].bytes;
        [gx_texture_cache removeObjectForKey:oldest_key];
    }
    cached = [NativeGXMetalCachedTexture new];
    cached.texture = result;
    cached.bytes = bytes;
    cached.last_use = ++gx_texture_clock;
    gx_texture_cache[@(texture->serial)] = cached;
    gx_texture_bytes += bytes;
    return result;
}

int NativeGXMetalDraw(const NativeGXMetalVertex* vertices, uint32_t count,
                      const NativeGXMetalState* state,
                      const NativeGXMetalTexture textures[8])
{
    @autoreleasepool {
        if (!NativeGXMetalSupports(state) || vertices == NULL ||
            textures == NULL || count % 3 != 0)
        {
            return 0;
        }
        if (count == 0 || state->cull_mode == 3) {
            return 1;
        }
        uint32_t left = MIN(state->scissor[0], gx_width);
        uint32_t top = MIN(state->scissor[1], gx_height);
        uint32_t width = MIN(state->scissor[2], gx_width - left);
        uint32_t height = MIN(state->scissor[3], gx_height - top);
        if (width == 0 || height == 0) {
            return 1;
        }
        size_t vertex_bytes = (size_t) count * sizeof(*vertices);
        size_t state_offset = (vertex_bytes + 255) & ~(size_t) 255;
        size_t needed = (state_offset + sizeof(*state) + 255) & ~(size_t) 255;
        if (gx_stream_bytes + needed > gx_stream_limit && !gx_metal_finish()) {
            return 0;
        }
        id<MTLRenderPipelineState> pipeline = gx_metal_pipeline(state);
        id<MTLDepthStencilState> depth = gx_metal_depth(state);
        if (pipeline == nil || depth == nil) {
            return 0;
        }
        id<MTLTexture> metal_textures[8];
        id<MTLSamplerState> samplers[8];
        for (unsigned i = 0; i < 8; ++i) {
            metal_textures[i] = gx_metal_texture(&textures[i]);
            samplers[i] = gx_metal_sampler(&textures[i]);
            if (metal_textures[i] == nil || samplers[i] == nil) {
                return gx_metal_error("texture or sampler", nil);
            }
        }
        if (gx_stream_buffer == nil ||
            gx_stream_offset + needed > gx_stream_buffer.length)
        {
            gx_stream_buffer =
                [gx_device newBufferWithLength:MAX(needed, gx_stream_block)
                                       options:MTLResourceStorageModeShared];
            gx_stream_offset = 0;
            if (gx_stream_buffer == nil) {
                return gx_metal_error("vertex buffer", nil);
            }
            [gx_stream_buffers addObject:gx_stream_buffer];
        }
        size_t base = gx_stream_offset;
        memcpy((uint8_t*) gx_stream_buffer.contents + base, vertices,
               vertex_bytes);
        memcpy((uint8_t*) gx_stream_buffer.contents + base + state_offset,
               state, sizeof(*state));
        gx_stream_offset += needed;
        gx_stream_bytes += needed;
        if (!gx_metal_begin_render()) {
            return 0;
        }
        [gx_encoder setRenderPipelineState:pipeline];
        [gx_encoder setDepthStencilState:depth];
        [gx_encoder setCullMode:state->cull_mode == 1   ? MTLCullModeFront
                                : state->cull_mode == 2 ? MTLCullModeBack
                                                        : MTLCullModeNone];
        [gx_encoder
            setScissorRect:(MTLScissorRect) { left, top, width, height }];
        [gx_encoder setVertexBuffer:gx_stream_buffer offset:base atIndex:0];
        [gx_encoder setFragmentBuffer:gx_stream_buffer
                               offset:base + state_offset
                              atIndex:1];
        [gx_encoder setFragmentTextures:metal_textures
                              withRange:NSMakeRange(0, 8)];
        [gx_encoder setFragmentSamplerStates:samplers
                                   withRange:NSMakeRange(0, 8)];
        [gx_encoder drawPrimitives:MTLPrimitiveTypeTriangle
                       vertexStart:0
                       vertexCount:count];
        return 1;
    }
}

int NativeGXMetalReadback(uint8_t* rgba, float* depth)
{
    @autoreleasepool {
        if (!gx_metal_begin_commands()) {
            return 0;
        }
        gx_metal_end_encoder();
        id<MTLBlitCommandEncoder> blit = [gx_commands blitCommandEncoder];
        if (blit == nil) {
            return gx_metal_error("readback encoder", nil);
        }
        if (rgba != NULL) {
            [blit copyFromTexture:gx_color_target
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:MTLSizeMake(gx_width, gx_height, 1)
                                toBuffer:gx_transfer_buffer
                       destinationOffset:0
                  destinationBytesPerRow:gx_row_bytes
                destinationBytesPerImage:gx_image_bytes];
        }
        if (depth != NULL) {
            [blit copyFromTexture:gx_depth_target
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:MTLSizeMake(gx_width, gx_height, 1)
                                toBuffer:gx_transfer_buffer
                       destinationOffset:gx_image_bytes
                  destinationBytesPerRow:gx_row_bytes
                destinationBytesPerImage:gx_image_bytes];
        }
        [blit endEncoding];
        if (!gx_metal_finish()) {
            return 0;
        }
        const uint8_t* source = gx_transfer_buffer.contents;
        for (uint32_t row = 0; row < gx_height; ++row) {
            if (rgba != NULL) {
                memcpy(rgba + (size_t) row * gx_width * 4,
                       source + row * gx_row_bytes, (size_t) gx_width * 4);
            }
            if (depth != NULL) {
                memcpy(depth + (size_t) row * gx_width,
                       source + gx_image_bytes + row * gx_row_bytes,
                       (size_t) gx_width * 4);
            }
        }
        return 1;
    }
}

int NativeGXMetalUpload(const uint8_t* rgba, const float* depth)
{
    @autoreleasepool {
        /* The transfer buffer is reused only after its last GPU use completes.
         */
        if (!gx_metal_finish() || !gx_metal_begin_commands()) {
            return 0;
        }
        uint8_t* destination = gx_transfer_buffer.contents;
        for (uint32_t row = 0; row < gx_height; ++row) {
            if (rgba != NULL) {
                memcpy(destination + row * gx_row_bytes,
                       rgba + (size_t) row * gx_width * 4,
                       (size_t) gx_width * 4);
            }
            if (depth != NULL) {
                memcpy(destination + gx_image_bytes + row * gx_row_bytes,
                       depth + (size_t) row * gx_width, (size_t) gx_width * 4);
            }
        }
        id<MTLBlitCommandEncoder> blit = [gx_commands blitCommandEncoder];
        if (blit == nil) {
            return gx_metal_error("upload encoder", nil);
        }
        if (rgba != NULL) {
            [blit copyFromBuffer:gx_transfer_buffer
                       sourceOffset:0
                  sourceBytesPerRow:gx_row_bytes
                sourceBytesPerImage:gx_image_bytes
                         sourceSize:MTLSizeMake(gx_width, gx_height, 1)
                          toTexture:gx_color_target
                   destinationSlice:0
                   destinationLevel:0
                  destinationOrigin:MTLOriginMake(0, 0, 0)];
        }
        if (depth != NULL) {
            [blit copyFromBuffer:gx_transfer_buffer
                       sourceOffset:gx_image_bytes
                  sourceBytesPerRow:gx_row_bytes
                sourceBytesPerImage:gx_image_bytes
                         sourceSize:MTLSizeMake(gx_width, gx_height, 1)
                          toTexture:gx_depth_target
                   destinationSlice:0
                   destinationLevel:0
                  destinationOrigin:MTLOriginMake(0, 0, 0)];
        }
        [blit endEncoding];
        return 1;
    }
}
