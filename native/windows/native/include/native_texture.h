#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace melee::native {

enum class GxTextureFormat : std::uint8_t { I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8 };

struct NativeRgba8 {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<std::uint8_t> pixels; // four bytes per pixel, row-major RGBA
};

// Decodes GameCube GX tiled texture blocks into a host row-major RGBA8 image.
// The importer validates dimensions and source size before reading any block.
NativeRgba8 decode_gx_texture(GxTextureFormat format, std::uint16_t width,
                              std::uint16_t height, std::span<const std::byte> source);

} // namespace melee::native
