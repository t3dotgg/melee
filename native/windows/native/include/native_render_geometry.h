#pragma once

#include "native_render.h"

#include <cstdint>
#include <span>
#include <vector>

namespace melee::native {

// Host-native geometry emitted for a RenderSnapshot.  The representation is
// deliberately independent of a graphics API: a backend can upload the
// vertices and indices to D3D12, Vulkan, or a software rasterizer without
// knowing about GameCube GX state or guest pointers.
struct NativeRenderVertex {
    RenderTransform position;
    // Packed RGBA8 color.  Keeping a color on every vertex gives the proxy a
    // visible result even before material/shader translation is implemented.
    std::uint32_t color = 0xffffffffU;
};

struct NativeRenderDraw {
    std::uint32_t object_id = 0;
    std::uint32_t material = 0;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
};

struct NativeRenderGeometry {
    std::vector<NativeRenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<NativeRenderDraw> draws;

    bool empty() const noexcept { return draws.empty(); }
};

// One contiguous vertex/index upload, with the index range aligned to 16
// bytes. Limits bound both GPU allocation and draw-list bookkeeping.
inline constexpr std::uint32_t native_geometry_upload_limit = 32U * 1024U * 1024U;
inline constexpr std::uint32_t native_geometry_draw_limit = 65536U;

struct NativeGeometryUploadPlan {
    std::uint32_t vertex_bytes = 0;
    std::uint32_t index_bytes = 0;
    std::uint32_t index_offset = 0;
    std::uint32_t total_bytes = 0;
    std::uint32_t draw_count = 0;
};

// Validate finite vertices, in-range indices, and nonempty triangle-list
// draws before a backend allocates or writes storage. Empty geometry is valid;
// every failure clears the output plan. No graphics API or allocation is used.
bool plan_geometry_upload(const NativeRenderGeometry& geometry,
                          NativeGeometryUploadPlan& plan,
                          std::uint32_t byte_limit = native_geometry_upload_limit) noexcept;

// Build a deterministic colored quad for each render object.  A quad is a
// temporary fighter/stage proxy: the simulation supplies only a transform,
// while a future DAT/material importer can replace this function's geometry
// without changing the renderer handoff contract.
NativeRenderGeometry build_proxy_geometry(std::span<const RenderObject> objects,
                                           float half_width = 0.5F,
                                           float half_height = 1.0F);

inline NativeRenderGeometry build_proxy_geometry(const RenderSnapshot& snapshot,
                                                 float half_width = 0.5F,
                                                 float half_height = 1.0F)
{
    return build_proxy_geometry(std::span<const RenderObject>(snapshot.objects),
                                half_width, half_height);
}

} // namespace melee::native
