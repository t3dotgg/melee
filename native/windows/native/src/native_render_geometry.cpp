#include "native_render_geometry.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace melee::native {
namespace {

std::uint32_t proxy_color(std::uint32_t material)
{
    // A small integer hash gives stable, high-contrast colors for proxies
    // without coupling this transitional path to a material database.
    std::uint32_t value = material * 0x9e3779b9U + 0x7f4a7c15U;
    value ^= value >> 16;
    const auto channel = [](std::uint32_t bits) {
        return static_cast<std::uint32_t>(64U + (bits & 0x7fU));
    };
    const std::uint32_t red = channel(value);
    const std::uint32_t green = channel(value >> 8);
    const std::uint32_t blue = channel(value >> 16);
    return red | (green << 8) | (blue << 16) | 0xff000000U;
}

bool finite_transform(const RenderTransform& transform)
{
    return std::isfinite(transform.x) && std::isfinite(transform.y) &&
           std::isfinite(transform.z);
}

} // namespace

bool plan_geometry_upload(const NativeRenderGeometry& geometry,
                          NativeGeometryUploadPlan& plan,
                          std::uint32_t byte_limit) noexcept
{
    plan = {};
    if (geometry.vertices.empty() && geometry.indices.empty() && geometry.draws.empty())
        return true;
    if (geometry.vertices.empty() || geometry.indices.empty() || geometry.draws.empty() ||
        geometry.draws.size() > native_geometry_draw_limit ||
        geometry.vertices.size() > byte_limit / sizeof(NativeRenderVertex) ||
        geometry.indices.size() > byte_limit / sizeof(std::uint32_t))
        return false;
    const std::uint64_t vertex_bytes = geometry.vertices.size() * sizeof(NativeRenderVertex);
    const std::uint64_t index_offset = (vertex_bytes + 15U) & ~std::uint64_t(15U);
    const std::uint64_t index_bytes = geometry.indices.size() * sizeof(std::uint32_t);
    if (index_offset + index_bytes > byte_limit) return false;
    for (const auto& vertex : geometry.vertices)
        if (!finite_transform(vertex.position)) return false;
    for (const auto index : geometry.indices)
        if (index >= geometry.vertices.size()) return false;
    for (const auto& draw : geometry.draws) {
        if (draw.index_count == 0 || draw.index_count % 3 != 0 ||
            draw.first_index > geometry.indices.size() ||
            draw.index_count > geometry.indices.size() - draw.first_index)
            return false;
    }
    plan = {static_cast<std::uint32_t>(vertex_bytes), static_cast<std::uint32_t>(index_bytes),
            static_cast<std::uint32_t>(index_offset),
            static_cast<std::uint32_t>(index_offset + index_bytes),
            static_cast<std::uint32_t>(geometry.draws.size())};
    return true;
}

NativeRenderGeometry build_proxy_geometry(std::span<const RenderObject> objects,
                                           float half_width,
                                           float half_height)
{
    if (!(std::isfinite(half_width) && std::isfinite(half_height) && half_width > 0.0F &&
          half_height > 0.0F)) {
        throw std::invalid_argument("proxy extents must be finite and positive");
    }
    constexpr std::size_t vertices_per_object = 4;
    constexpr std::size_t indices_per_object = 6;
    if (objects.size() > std::numeric_limits<std::uint32_t>::max() / vertices_per_object ||
        objects.size() > std::numeric_limits<std::uint32_t>::max() / indices_per_object) {
        throw std::length_error("proxy geometry contains too many objects");
    }

    NativeRenderGeometry geometry;
    geometry.vertices.reserve(objects.size() * vertices_per_object);
    geometry.indices.reserve(objects.size() * indices_per_object);
    geometry.draws.reserve(objects.size());
    for (const RenderObject& object : objects) {
        if (!finite_transform(object.transform))
            throw std::invalid_argument("proxy transform must be finite");
        const std::uint32_t first_vertex = static_cast<std::uint32_t>(geometry.vertices.size());
        const std::uint32_t first_index = static_cast<std::uint32_t>(geometry.indices.size());
        const std::uint32_t color = proxy_color(object.material);
        const float left = object.transform.x - half_width;
        const float right = object.transform.x + half_width;
        const float bottom = object.transform.y - half_height;
        const float top = object.transform.y + half_height;
        geometry.vertices.push_back({{left, bottom, object.transform.z}, color});
        geometry.vertices.push_back({{right, bottom, object.transform.z}, color});
        geometry.vertices.push_back({{right, top, object.transform.z}, color});
        geometry.vertices.push_back({{left, top, object.transform.z}, color});
        geometry.indices.insert(geometry.indices.end(),
                               {first_vertex, first_vertex + 1, first_vertex + 2,
                                first_vertex, first_vertex + 2, first_vertex + 3});
        geometry.draws.push_back(
            {object.id, object.material, first_index, static_cast<std::uint32_t>(indices_per_object)});
    }
    return geometry;
}

NativeRenderGeometry RenderCommandBuffer::build_proxy_geometry(float half_width,
                                                               float half_height) const
{
    return melee::native::build_proxy_geometry(commands(), half_width, half_height);
}

} // namespace melee::native
