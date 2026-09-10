#include "native_fighter.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace melee::native {

void NativeFighter::update(const FighterInput& input, double dt_seconds)
{
    if (!(std::isfinite(dt_seconds) && dt_seconds > 0.0 && dt_seconds <= 0.1))
        throw std::invalid_argument("fighter dt must be finite and in (0, 0.1]");
    ++state_.frame;

    constexpr float speed = 6.0F;
    constexpr float gravity = -24.0F;
    constexpr float jump_speed = 10.0F;
    state_.velocity_x = std::clamp(input.stick_x, -1.0F, 1.0F) * speed;

    if (state_.grounded && input.jump_pressed) {
        state_.grounded = false;
        state_.velocity_y = jump_speed;
        state_.action = FighterAction::Jump;
        action_frames_ = 0;
    } else if (state_.grounded && input.attack_pressed) {
        state_.action = FighterAction::Attack;
        action_frames_ = 10;
    } else if (state_.grounded && input.special_pressed) {
        state_.action = FighterAction::Special;
        action_frames_ = 16;
    }

    state_.x += state_.velocity_x * static_cast<float>(dt_seconds);
    if (!state_.grounded) {
        state_.velocity_y += gravity * static_cast<float>(dt_seconds);
        state_.y += state_.velocity_y * static_cast<float>(dt_seconds);
        if (state_.y <= 0.0F) {
            state_.y = 0.0F;
            state_.velocity_y = 0.0F;
            state_.grounded = true;
            state_.action = FighterAction::Idle;
        }
    }
    if (state_.grounded && action_frames_ != 0) {
        --action_frames_;
        if (action_frames_ == 0) state_.action = FighterAction::Idle;
    } else if (state_.grounded && state_.action == FighterAction::Idle &&
               std::abs(state_.velocity_x) > 0.001F) {
        state_.action = FighterAction::Run;
    } else if (state_.grounded && std::abs(state_.velocity_x) <= 0.001F &&
               action_frames_ == 0) {
        state_.action = FighterAction::Idle;
    }
}

} // namespace melee::native
