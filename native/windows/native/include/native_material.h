#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace melee::native {

struct NativeColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
    friend constexpr bool operator==(const NativeColor&, const NativeColor&) = default;
};

// Values in this structure use the numeric values from Dolphin's GXEnum.h.
// They remain raw so a backend can apply the exact TEV semantics once its
// shader translator is available; this layer does not invent replacements.
struct NativeTevStage {
    std::array<std::uint8_t, 4> color_inputs{};
    std::array<std::uint8_t, 4> alpha_inputs{};
    std::uint8_t color_op = 0;
    std::uint8_t color_bias = 0;
    std::uint8_t color_scale = 0;
    bool color_clamp = true;
    std::uint8_t color_output = 0;
    std::uint8_t alpha_op = 0;
    std::uint8_t alpha_bias = 0;
    std::uint8_t alpha_scale = 0;
    bool alpha_clamp = true;
    std::uint8_t alpha_output = 0;
    std::uint8_t konst_color_selector = 0;
    std::uint8_t konst_alpha_selector = 0;
    std::uint8_t tex_coord = 0xff;
    std::uint16_t tex_map = 0xff;
    std::uint8_t channel = 0xff;
};

struct NativeMaterial {
    NativeColor ambient;
    NativeColor diffuse{255, 255, 255, 255};
    NativeColor specular;
    float alpha = 1.0F;
    float shininess = 0.0F;
    // HSD render-mode flags are retained verbatim until blend/depth mapping
    // is implemented. Their bit meanings are intentionally not guessed here.
    std::uint32_t render_mode = 0;
    std::array<NativeColor, 4> konst_colors{};
    std::vector<NativeTevStage> tev_stages;
};

struct NativePalette {
    std::vector<NativeColor> entries;
};

// Validate fields whose ranges are specified by Dolphin GXEnum.h. Unknown
// render-mode bits are preserved and accepted. On failure, error receives a
// stable diagnostic when non-null.
bool validate_material(const NativeMaterial& material, std::string* error = nullptr) noexcept;

// Decode only the unambiguous RGBA8 palette representation. Other GameCube
// palette encodings must be decoded by a format-aware asset importer instead
// of being silently interpreted as colors.
bool decode_rgba8_palette(std::span<const std::byte> bytes,
                          NativePalette& palette,
                          std::string* error = nullptr);

} // namespace melee::native
