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
    std::cout << "native GX texture tests passed\n";
}
