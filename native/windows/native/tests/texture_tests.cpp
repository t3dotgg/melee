#include "native_texture.h"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

int main()
{
    using namespace melee::native;
    std::vector<std::byte> rgba(64);
    for (int i = 0; i < 16; ++i) { rgba[i * 2] = std::byte{255}; rgba[i * 2 + 1] = std::byte{10}; rgba[32 + i * 2] = std::byte{20}; rgba[33 + i * 2] = std::byte{30}; }
    const auto image = decode_gx_texture(GxTextureFormat::RGBA8, 4, 4, rgba);
    assert(image.pixels.size() == 64 && image.pixels[0] == 10 && image.pixels[1] == 20 && image.pixels[2] == 30 && image.pixels[3] == 255);
    bool rejected = false;
    try { (void)decode_gx_texture(GxTextureFormat::RGB565, 4, 4, std::span<const std::byte>(rgba.data(), 2)); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::vector<std::byte> cmpr(32);
    // Four DXT1 sub-blocks whose first endpoint is opaque red; all indices 0.
    for (std::size_t sub = 0; sub < 4; ++sub) {
        cmpr[sub * 8] = std::byte{0xF8};
        cmpr[sub * 8 + 1] = std::byte{0x00};
    }
    const auto compressed = decode_gx_texture(GxTextureFormat::CMPR, 8, 8, cmpr);
    assert(compressed.pixels[0] == 255 && compressed.pixels[1] == 0 && compressed.pixels[2] == 0);
    assert(compressed.pixels[(7U * 8U + 7U) * 4U] == 255);
    std::vector<std::byte> c4(32);
    c4[0] = std::byte{0x12}; // first two texels use palette entries 1 and 2
    const NativePaletteEntry palette[] = {
        {0, 0, 0, 255}, {10, 20, 30, 255}, {40, 50, 60, 128},
    };
    const auto indexed = decode_gx_indexed_texture(GxTextureFormat::C4, 8, 8, c4, palette);
    assert(indexed.pixels[0] == 10 && indexed.pixels[1] == 20 && indexed.pixels[2] == 30);
    assert(indexed.pixels[4] == 40 && indexed.pixels[5] == 50 && indexed.pixels[6] == 60 && indexed.pixels[7] == 128);
    rejected = false;
    try { (void)decode_gx_indexed_texture(GxTextureFormat::C4, 8, 8, c4,
                                          std::span<const NativePaletteEntry>(palette, 1)); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::cout << "native GX texture tests passed\n";
}
