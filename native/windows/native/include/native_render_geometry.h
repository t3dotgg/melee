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
