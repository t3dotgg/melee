#include "native_material.h"

#include <cmath>
#include <limits>

namespace melee::native {
namespace {

bool fail(std::string* error, const char* message) noexcept
{
    if (error != nullptr) *error = message;
    return false;
}

bool valid_color_arg(std::uint8_t value)
{
    // GX_CC_TEXRRR/TEXGGG/TEXBBB extend the contiguous 0..15 range to 18.
    return value <= 18;
}

bool valid_alpha_arg(std::uint8_t value) { return value <= 7; }

bool valid_tev_op(std::uint8_t value)
{
    return value == 0 || value == 1 || (value >= 8 && value <= 15);
}

bool valid_kcolor(std::uint8_t value)
{
    return value <= 7 || (value >= 12 && value <= 31);
}

bool valid_kalpha(std::uint8_t value)
{
    return value <= 7 || (value >= 16 && value <= 31);
}

bool valid_order(std::uint8_t coord, std::uint16_t map, std::uint8_t channel)
{
    const bool coord_ok = coord == 0xff || coord <= 7;
    const bool map_ok = map == 0xff || map == 0x100 || map <= 7;
    // GX_COLOR_NULL is 0xff; the defined channel values are 0..8.
    const bool channel_ok = channel == 0xff || channel <= 8;
    return coord_ok && map_ok && channel_ok;
}

} // namespace

bool validate_material(const NativeMaterial& material, std::string* error) noexcept
{
    if (!(std::isfinite(material.alpha) && material.alpha >= 0.0F && material.alpha <= 1.0F))
        return fail(error, "material alpha must be finite and in [0,1]");
    if (!(std::isfinite(material.shininess) && material.shininess >= 0.0F))
        return fail(error, "material shininess must be finite and non-negative");
    if (material.tev_stages.size() > 16)
        return fail(error, "material has more than 16 TEV stages");
    for (const NativeTevStage& stage : material.tev_stages) {
        for (const auto value : stage.color_inputs)
            if (!valid_color_arg(value)) return fail(error, "invalid TEV color input");
        for (const auto value : stage.alpha_inputs)
            if (!valid_alpha_arg(value)) return fail(error, "invalid TEV alpha input");
        if (!valid_tev_op(stage.color_op) || !valid_tev_op(stage.alpha_op))
            return fail(error, "invalid TEV operation");
        if (stage.color_bias > 2 || stage.alpha_bias > 2)
            return fail(error, "invalid TEV bias");
        if (stage.color_scale > 3 || stage.alpha_scale > 3)
            return fail(error, "invalid TEV scale");
        if (stage.color_output > 3 || stage.alpha_output > 3)
            return fail(error, "invalid TEV output register");
        if (!valid_kcolor(stage.konst_color_selector) ||
            !valid_kalpha(stage.konst_alpha_selector))
            return fail(error, "invalid TEV constant selector");
        if (!valid_order(stage.tex_coord, stage.tex_map, stage.channel))
            return fail(error, "invalid TEV texture order");
    }
    if (error != nullptr) error->clear();
    return true;
}

bool decode_rgba8_palette(std::span<const std::byte> bytes,
                          NativePalette& palette,
                          std::string* error)
{
    if (bytes.size() % 4 != 0) return fail(error, "RGBA8 palette size is not a multiple of four");
    const std::size_t count = bytes.size() / 4;
    if (count > std::numeric_limits<std::uint32_t>::max())
        return fail(error, "RGBA8 palette is too large");
    NativePalette decoded;
    decoded.entries.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(bytes.data() + i * 4);
        decoded.entries.push_back({raw[0], raw[1], raw[2], raw[3]});
    }
    palette = std::move(decoded);
    if (error != nullptr) error->clear();
    return true;
}

} // namespace melee::native
