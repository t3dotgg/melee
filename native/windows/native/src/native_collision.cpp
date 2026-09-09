#include "native_collision.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace melee::native {
namespace {

void validate_box(const NativeAabb& box, const char* label)
{
    if (!box.valid() || !std::isfinite(box.min_x) || !std::isfinite(box.min_y) ||
        !std::isfinite(box.max_x) || !std::isfinite(box.max_y))
        throw std::invalid_argument(std::string(label) + " must be finite with positive extents");
}

void translate(NativeAabb& box, float dx, float dy) noexcept
{
    box.min_x += dx;
    box.max_x += dx;
    box.min_y += dy;
    box.max_y += dy;
}

NativeAabb intersection(const NativeAabb& lhs, const NativeAabb& rhs) noexcept
{
    return {std::max(lhs.min_x, rhs.min_x), std::max(lhs.min_y, rhs.min_y),
            std::min(lhs.max_x, rhs.max_x), std::min(lhs.max_y, rhs.max_y)};
}

bool overlaps(const NativeAabb& lhs, const NativeAabb& rhs) noexcept
{
    return lhs.min_x < rhs.max_x && rhs.min_x < lhs.max_x && lhs.min_y < rhs.max_y &&
           rhs.min_y < lhs.max_y;
}

void clamp_fighter(const NativeStageBounds& stage, NativeFighterCollisionState& fighter)
{
    const NativeAabb& limit = stage.bounds;
    const NativeAabb& body = fighter.hurtbox;
    const float body_width = body.width();
    const float body_height = body.height();
    float dx = 0.0F;
    float dy = 0.0F;
    if (body_width >= limit.width())
        dx = limit.center_x() - body.center_x();
    else if (body.min_x < limit.min_x)
        dx = limit.min_x - body.min_x;
    else if (body.max_x > limit.max_x)
        dx = limit.max_x - body.max_x;
    if (body_height >= limit.height())
        dy = limit.center_y() - body.center_y();
    else if (body.min_y < limit.min_y)
        dy = limit.min_y - body.min_y;
    else if (body.max_y > limit.max_y)
        dy = limit.max_y - body.max_y;
    if (dx == 0.0F && dy == 0.0F)
        return;
    translate(fighter.hurtbox, dx, dy);
    for (NativeHitbox& hitbox : fighter.hitboxes) translate(hitbox.bounds, dx, dy);
}

} // namespace

bool NativeAabb::valid() const noexcept
{
    return max_x > min_x && max_y > min_y;
}

NativeCollisionResult resolve_native_collisions(const NativeStageBounds& stage,
                                                std::span<NativeFighterCollisionState> fighters,
                                                std::uint64_t simulation_frame)
{
    validate_box(stage.bounds, "stage bounds");
    for (const NativeFighterCollisionState& fighter : fighters) {
        validate_box(fighter.hurtbox, "fighter hurtbox");
        for (const NativeHitbox& hitbox : fighter.hitboxes)
            if (hitbox.active) validate_box(hitbox.bounds, "active hitbox");
    }

    for (NativeFighterCollisionState& fighter : fighters)
        if (fighter.active) clamp_fighter(stage, fighter);

    std::vector<std::size_t> order(fighters.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&fighters](std::size_t lhs, std::size_t rhs) {
        return fighters[lhs].id < fighters[rhs].id;
    });

    NativeCollisionResult result;
    for (const std::size_t attacker_index : order) {
        const NativeFighterCollisionState& attacker = fighters[attacker_index];
        if (!attacker.active)
            continue;
        std::vector<std::size_t> hitbox_order(attacker.hitboxes.size());
        for (std::size_t i = 0; i < hitbox_order.size(); ++i) hitbox_order[i] = i;
        std::stable_sort(hitbox_order.begin(), hitbox_order.end(), [&attacker](std::size_t lhs,
                                                                                 std::size_t rhs) {
            if (attacker.hitboxes[lhs].priority != attacker.hitboxes[rhs].priority)
                return attacker.hitboxes[lhs].priority < attacker.hitboxes[rhs].priority;
            return attacker.hitboxes[lhs].id < attacker.hitboxes[rhs].id;
        });
        for (const std::size_t hitbox_index : hitbox_order) {
            const NativeHitbox& hitbox = attacker.hitboxes[hitbox_index];
            if (!hitbox.active || hitbox.damage == 0)
                continue;
            for (const std::size_t target_index : order) {
                NativeFighterCollisionState& target = fighters[target_index];
                if (!target.active || target.id == attacker.id || !overlaps(hitbox.bounds, target.hurtbox))
                    continue;
                const NativeAabb overlap = intersection(hitbox.bounds, target.hurtbox);
                result.events.push_back(
                    {simulation_frame, attacker.id, target.id, hitbox.id, hitbox.damage, overlap});
                const unsigned int total = static_cast<unsigned int>(target.damage) + hitbox.damage;
                target.damage = static_cast<std::uint16_t>(
                    std::min(total, static_cast<unsigned int>(std::numeric_limits<std::uint16_t>::max())));
            }
        }
    }
    return result;
}

} // namespace melee::native

