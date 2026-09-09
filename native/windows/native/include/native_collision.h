#pragma once

#include <cstdint>
#include <vector>

namespace melee::native {

struct StageBounds {
    float left = -20.0F;
    float right = 20.0F;
    float floor = 0.0F;
    float ceiling = 18.0F;
};

struct Hitbox {
    std::uint64_t owner = 0;
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    std::uint16_t damage = 0;
};

struct CollisionEvent {
    std::uint64_t attacker = 0;
    std::uint64_t target = 0;
    std::uint16_t damage = 0;
};

class NativeCollisionWorld final {
public:
    explicit NativeCollisionWorld(StageBounds bounds = {}) : bounds_(bounds) {}

    void constrain(float& x, float& y, float half_width, float height) const noexcept;
    std::vector<CollisionEvent> resolve(const std::vector<Hitbox>& hitboxes) const;
    const StageBounds& bounds() const noexcept { return bounds_; }

private:
    StageBounds bounds_;
};

} // namespace melee::native
