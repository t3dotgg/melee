#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace melee::native {

enum class GxTextureFormat : std::uint8_t {
    I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8, C4, C8, C14X2, CMPR
};

struct NativeRgba8 {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<std::uint8_t> pixels; // four bytes per pixel, row-major RGBA
};

struct NativePaletteEntry {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

// Decodes GameCube GX tiled texture blocks into a host row-major RGBA8 image.
// The importer validates dimensions and source size before reading any block.
NativeRgba8 decode_gx_texture(GxTextureFormat format, std::uint16_t width,
                              std::uint16_t height, std::span<const std::byte> source);

// Decode an indexed GX texture using a host RGBA palette. Palette indices are
// bounds-checked; C4/C8/C14X2 retain their tiled GameCube block layout.
NativeRgba8 decode_gx_indexed_texture(GxTextureFormat format, std::uint16_t width,
                                      std::uint16_t height,
                                      std::span<const std::byte> source,
                                      std::span<const NativePaletteEntry> palette);

} // namespace melee::native
