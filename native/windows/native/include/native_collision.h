#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace melee::native {

// World-space axis-aligned box. Bounds use a half-open convention for
// intersection: boxes that only touch at an edge do not produce a hit.
struct NativeAabb {
    float min_x = 0.0F;
    float min_y = 0.0F;
    float max_x = 0.0F;
    float max_y = 0.0F;

    [[nodiscard]] float width() const noexcept { return max_x - min_x; }
    [[nodiscard]] float height() const noexcept { return max_y - min_y; }
    [[nodiscard]] float center_x() const noexcept { return (min_x + max_x) * 0.5F; }
    [[nodiscard]] float center_y() const noexcept { return (min_y + max_y) * 0.5F; }
    [[nodiscard]] bool valid() const noexcept;
};

struct NativeStageBounds {
    NativeAabb bounds{-120.0F, 0.0F, 120.0F, 100.0F};
};

struct NativeHitbox {
    std::uint32_t id = 0;
    NativeAabb bounds;
    std::uint16_t damage = 0;
    std::uint8_t priority = 0;
    bool active = true;
};

// All state is value-owned. There are no guest addresses, callbacks, or
// pointers in the collision representation.
struct NativeFighterCollisionState {
    std::uint32_t id = 0;
    NativeAabb hurtbox;
    std::vector<NativeHitbox> hitboxes;
    std::uint16_t damage = 0;
    bool active = true;
};

struct NativeCollisionEvent {
    std::uint64_t simulation_frame = 0;
    std::uint32_t attacker_id = 0;
    std::uint32_t target_id = 0;
    std::uint32_t hitbox_id = 0;
    std::uint16_t damage = 0;
    NativeAabb overlap;
};

struct NativeCollisionResult {
    std::vector<NativeCollisionEvent> events;
};

// Resolves stage clamping and hitbox/hurtbox overlaps for one simulation tick.
// Input order does not affect results: fighters and hitboxes are ordered by
// their stable numeric IDs, with source order used only to break duplicate-ID
// ties. Target damage is saturated at UINT16_MAX.
NativeCollisionResult resolve_native_collisions(
    const NativeStageBounds& stage, std::span<NativeFighterCollisionState> fighters,
    std::uint64_t simulation_frame = 0);

} // namespace melee::native
