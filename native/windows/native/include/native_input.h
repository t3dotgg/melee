#pragma once

#include <cstdint>

namespace melee::native {

// Logical controls exposed to native gameplay. Face-button positions follow
// Smash on Switch: A attacks/confirms, B uses special/backs out, and X/Y jump.
enum class PadButton : std::uint8_t {
    Attack,
    Special,
    Jump,
    Shield,
    Grab,
    Start,
    Back,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Count,
};

constexpr std::uint32_t pad_button_bit(PadButton button)
{
    return std::uint32_t{1} << static_cast<unsigned>(button);
}

struct PadState {
    float stick_x = 0.0F;
    float stick_y = 0.0F;
    float cstick_x = 0.0F;
    float cstick_y = 0.0F;
    float left_trigger = 0.0F;
    float right_trigger = 0.0F;
    std::uint32_t held_mask = 0;
    std::uint32_t pressed_mask = 0;
    std::uint32_t released_mask = 0;

    [[nodiscard]] bool held(PadButton button) const noexcept
    {
        return (held_mask & pad_button_bit(button)) != 0;
    }
    [[nodiscard]] bool pressed(PadButton button) const noexcept
    {
        return (pressed_mask & pad_button_bit(button)) != 0;
    }
    [[nodiscard]] bool released(PadButton button) const noexcept
    {
        return (released_mask & pad_button_bit(button)) != 0;
    }
};

// XInput-compatible raw state. Keeping this definition independent of
// Windows.h lets gameplay and tests compile on any host platform.
struct XInputState {
    std::uint16_t buttons = 0;
    std::uint8_t left_trigger = 0;
    std::uint8_t right_trigger = 0;
    std::int16_t thumb_lx = 0;
    std::int16_t thumb_ly = 0;
    std::int16_t thumb_rx = 0;
    std::int16_t thumb_ry = 0;
};

namespace xinput_button {
constexpr std::uint16_t dpad_up = 0x0001;
constexpr std::uint16_t dpad_down = 0x0002;
constexpr std::uint16_t dpad_left = 0x0004;
constexpr std::uint16_t dpad_right = 0x0008;
constexpr std::uint16_t start = 0x0010;
constexpr std::uint16_t back = 0x0020;
constexpr std::uint16_t left_stick = 0x0040;
constexpr std::uint16_t right_stick = 0x0080;
constexpr std::uint16_t left_shoulder = 0x0100;
constexpr std::uint16_t right_shoulder = 0x0200;
constexpr std::uint16_t a = 0x1000;
constexpr std::uint16_t b = 0x2000;
constexpr std::uint16_t x = 0x4000;
constexpr std::uint16_t y = 0x8000;
} // namespace xinput_button

class XboxPadMapper {
public:
    explicit XboxPadMapper(float trigger_threshold = 0.5F) : trigger_threshold_(trigger_threshold) {}

    // Converts one XInput sample and computes pressed/released edges against
    // the previous sample. The first sample reports all held buttons as pressed.
    [[nodiscard]] PadState update(const XInputState& sample) noexcept;
    void reset() noexcept { previous_ = 0; initialized_ = false; }

private:
    static float normalize_axis(std::int16_t value) noexcept;
    float trigger_threshold_;
    std::uint32_t previous_ = 0;
    bool initialized_ = false;
};

} // namespace melee::native
