#include "native_input.h"

#include <algorithm>
#include <cmath>

namespace melee::native {

float XboxPadMapper::normalize_axis(std::int16_t value) noexcept
{
    constexpr float deadzone = 7849.0F;
    constexpr float range = 32767.0F - deadzone;
    const float input = static_cast<float>(value);
    const float magnitude = std::fabs(input);
    if (magnitude <= deadzone) {
        return 0.0F;
    }
    const float scaled = std::min((magnitude - deadzone) / range, 1.0F);
    return std::copysign(scaled, input);
}

PadState XboxPadMapper::update(const XInputState& sample) noexcept
{
    PadState output;
    output.stick_x = normalize_axis(sample.thumb_lx);
    output.stick_y = normalize_axis(sample.thumb_ly);
    output.cstick_x = normalize_axis(sample.thumb_rx);
    output.cstick_y = normalize_axis(sample.thumb_ry);
    output.left_trigger = static_cast<float>(sample.left_trigger) / 255.0F;
    output.right_trigger = static_cast<float>(sample.right_trigger) / 255.0F;

    auto set = [&](PadButton logical, bool down) {
        if (down) {
            output.held_mask |= pad_button_bit(logical);
        }
    };
    const auto buttons = sample.buttons;
    // Xbox face positions intentionally use the Switch Smash actions.
    set(PadButton::Attack, (buttons & xinput_button::a) != 0);
    set(PadButton::Special, (buttons & xinput_button::b) != 0);
    set(PadButton::Jump, (buttons & (xinput_button::x | xinput_button::y)) != 0);
    set(PadButton::Shield,
        (buttons & (xinput_button::left_shoulder | xinput_button::right_shoulder)) != 0 ||
            output.left_trigger >= trigger_threshold_ || output.right_trigger >= trigger_threshold_);
    set(PadButton::Grab, (buttons & (xinput_button::left_stick | xinput_button::right_stick)) != 0);
    set(PadButton::Start, (buttons & xinput_button::start) != 0);
    set(PadButton::Back, (buttons & xinput_button::back) != 0);
    set(PadButton::DPadUp, (buttons & xinput_button::dpad_up) != 0);
    set(PadButton::DPadDown, (buttons & xinput_button::dpad_down) != 0);
    set(PadButton::DPadLeft, (buttons & xinput_button::dpad_left) != 0);
    set(PadButton::DPadRight, (buttons & xinput_button::dpad_right) != 0);

    output.pressed_mask = output.held_mask & (initialized_ ? ~previous_ : ~std::uint32_t{0});
    output.released_mask = previous_ & ~output.held_mask;
    previous_ = output.held_mask;
    initialized_ = true;
    return output;
}

} // namespace melee::native
