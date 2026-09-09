#pragma once

#include "native_collision.h"
#include "native_fighter.h"
#include "native_render.h"

#include <cstdint>

namespace melee::native {

// Small training-mode simulation that composes the native rules, collision,
// and render layers. It is intentionally data-driven at the boundary so the
// complete roster and stage data can replace it without guest memory.
class NativeTrainingMatch final {
public:
    NativeTrainingMatch();
    void update(const FighterInput& player_one, const FighterInput& player_two,
                double dt_seconds);
    const NativeFighter& player_one() const noexcept { return player_one_; }
    const NativeFighter& player_two() const noexcept { return player_two_; }
    const NativeCollisionResult& last_collisions() const noexcept { return collisions_; }
    RenderSnapshot snapshot() const;

private:
    NativeFighter player_one_;
    NativeFighter player_two_;
    NativeStageBounds stage_;
    NativeCollisionResult collisions_;
    std::uint64_t frame_ = 0;
};

} // namespace melee::native
