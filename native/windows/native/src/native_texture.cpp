#include "native_texture.h"

#include <algorithm>
#include <stdexcept>

namespace melee::native {
namespace {
std::uint8_t at(std::span<const std::byte> s, std::size_t p) { return std::to_integer<std::uint8_t>(s[p]); }
std::uint16_t be16(std::span<const std::byte> s, std::size_t p) { return static_cast<std::uint16_t>((at(s, p) << 8) | at(s, p + 1)); }
std::uint8_t expand4(std::uint8_t v) { return static_cast<std::uint8_t>((v << 4) | v); }
void pixel(NativeRgba8& out, std::uint16_t x, std::uint16_t y, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    if (x >= out.width || y >= out.height) return;
    const std::size_t p = (static_cast<std::size_t>(y) * out.width + x) * 4;
    out.pixels[p] = r; out.pixels[p + 1] = g; out.pixels[p + 2] = b; out.pixels[p + 3] = a;
}
} // namespace

NativeRgba8 decode_gx_texture(GxTextureFormat format, std::uint16_t width,
                              std::uint16_t height, std::span<const std::byte> source)
{
    if (width == 0 || height == 0) throw std::invalid_argument("texture dimensions must be nonzero");
    const std::size_t block_w = (format == GxTextureFormat::I4 || format == GxTextureFormat::I8 || format == GxTextureFormat::IA4) ? 8 : 4;
    const std::size_t block_h = (format == GxTextureFormat::I4) ? 8 : 4;
    std::size_t bytes_per_block = 0;
    switch (format) {
    case GxTextureFormat::I4: bytes_per_block = 32; break;
    case GxTextureFormat::I8: case GxTextureFormat::IA4: bytes_per_block = 32; break;
    case GxTextureFormat::IA8: case GxTextureFormat::RGB565: case GxTextureFormat::RGB5A3: bytes_per_block = 32; break;
    case GxTextureFormat::RGBA8: bytes_per_block = 64; break;
    }
    const std::size_t blocks_x = (width + block_w - 1) / block_w;
    const std::size_t blocks_y = (height + block_h - 1) / block_h;
    if (blocks_x > (static_cast<std::size_t>(-1) / blocks_y) || blocks_x * blocks_y > static_cast<std::size_t>(-1) / bytes_per_block)
        throw std::invalid_argument("texture size overflows");
    if (source.size() < blocks_x * blocks_y * bytes_per_block) throw std::invalid_argument("texture data is truncated");
    NativeRgba8 out{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
    std::size_t cursor = 0;
    for (std::size_t by = 0; by < blocks_y; ++by) for (std::size_t bx = 0; bx < blocks_x; ++bx) {
        const std::size_t ox = bx * block_w, oy = by * block_h;
        for (std::size_t y = 0; y < block_h; ++y) for (std::size_t x = 0; x < block_w; ++x) {
            std::uint8_t r = 0, g = 0, b = 0, a = 255;
            if (format == GxTextureFormat::RGBA8) {
                const std::size_t i = y * 4 + x;
                a = at(source, cursor + i * 2); r = at(source, cursor + i * 2 + 1);
                g = at(source, cursor + 32 + i * 2); b = at(source, cursor + 32 + i * 2 + 1);
            } else if (format == GxTextureFormat::RGB565 || format == GxTextureFormat::RGB5A3) {
                const auto value = be16(source, cursor + (y * 4 + x) * 2);
                if (format == GxTextureFormat::RGB565) {
                    r = static_cast<std::uint8_t>(((value >> 11) & 0x1F) * 255 / 31);
                    g = static_cast<std::uint8_t>(((value >> 5) & 0x3F) * 255 / 63);
                    b = static_cast<std::uint8_t>((value & 0x1F) * 255 / 31);
                } else if ((value & 0x8000) != 0) {
                    r = static_cast<std::uint8_t>(((value >> 10) & 0x1F) * 255 / 31);
                    g = static_cast<std::uint8_t>(((value >> 5) & 0x1F) * 255 / 31);
                    b = static_cast<std::uint8_t>((value & 0x1F) * 255 / 31);
                } else {
                    a = static_cast<std::uint8_t>(((value >> 12) & 7) * 255 / 7);
                    r = static_cast<std::uint8_t>(((value >> 8) & 0xF) * 255 / 15);
                    g = static_cast<std::uint8_t>(((value >> 4) & 0xF) * 255 / 15);
                    b = static_cast<std::uint8_t>((value & 0xF) * 255 / 15);
                }
            } else {
                const std::size_t i = y * block_w + x;
                if (format == GxTextureFormat::I4) { const auto v = at(source, cursor + i / 2); const auto n = static_cast<std::uint8_t>((i & 1) ? v & 0xF : v >> 4); r = g = b = expand4(n); }
                else if (format == GxTextureFormat::I8) r = g = b = at(source, cursor + i);
                else if (format == GxTextureFormat::IA4) { const auto v = at(source, cursor + i); a = expand4(v >> 4); r = g = b = expand4(v & 0xF); }
                else { const auto v = be16(source, cursor + i * 2); a = at(source, cursor + i * 2); r = g = b = at(source, cursor + i * 2 + 1); (void)v; }
            }
            pixel(out, static_cast<std::uint16_t>(ox + x), static_cast<std::uint16_t>(oy + y), r, g, b, a);
        }
        cursor += bytes_per_block;
    }
    return out;
}
} // namespace melee::native
