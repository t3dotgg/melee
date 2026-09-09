#pragma once

#include <cstdint>

namespace melee::native {

enum class FighterAction : std::uint8_t { Idle, Run, Jump, Attack, Special };

struct FighterInput {
    float stick_x = 0.0F;
    bool attack_pressed = false;
    bool special_pressed = false;
    bool jump_pressed = false;
};

struct FighterState {
    std::uint64_t frame = 0;
    FighterAction action = FighterAction::Idle;
    float x = 0.0F;
    float y = 0.0F;
    float velocity_x = 0.0F;
    float velocity_y = 0.0F;
    std::uint16_t damage = 0;
    std::uint8_t stocks = 4;
    bool grounded = true;
};

// Deterministic native rules slice. It owns typed state and has no guest
// addresses, CPU register file, or callback pointers. Constants are isolated
// here so a later data-driven fighter implementation can replace them.
class NativeFighter final {
public:
    void reset() noexcept { state_ = {}; state_.stocks = 4; }
    void set_position(float x, float y) noexcept { state_.x = x; state_.y = y; }
    void update(const FighterInput& input, double dt_seconds);
    const FighterState& state() const noexcept { return state_; }

private:
    FighterState state_;
    std::uint16_t action_frames_ = 0;
};

} // namespace melee::native
