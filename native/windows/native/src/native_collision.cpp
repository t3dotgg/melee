#include "native_collision.h"

#include <algorithm>

namespace melee::native {

void NativeCollisionWorld::constrain(float& x, float& y, float half_width,
                                     float height) const noexcept
{
    x = std::clamp(x, bounds_.left + half_width, bounds_.right - half_width);
    y = std::clamp(y, bounds_.floor, bounds_.ceiling - height);
}

std::vector<CollisionEvent> NativeCollisionWorld::resolve(
    const std::vector<Hitbox>& hitboxes) const
{
    std::vector<CollisionEvent> events;
    for (std::size_t i = 0; i < hitboxes.size(); ++i) {
        const auto& a = hitboxes[i];
        if (a.owner == 0 || a.width <= 0.0F || a.height <= 0.0F || a.damage == 0)
            continue;
        for (std::size_t j = 0; j < hitboxes.size(); ++j) {
            const auto& b = hitboxes[j];
            if (i == j || a.owner == b.owner || b.owner == 0 ||
                b.width <= 0.0F || b.height <= 0.0F)
                continue;
            const bool overlap_x = a.x < b.x + b.width && b.x < a.x + a.width;
            const bool overlap_y = a.y < b.y + b.height && b.y < a.y + a.height;
            if (overlap_x && overlap_y)
                events.push_back({a.owner, b.owner, a.damage});
        }
    }
    std::sort(events.begin(), events.end(), [](const CollisionEvent& lhs,
                                               const CollisionEvent& rhs) {
        if (lhs.attacker != rhs.attacker) return lhs.attacker < rhs.attacker;
        return lhs.target < rhs.target;
    });
    events.erase(std::unique(events.begin(), events.end(), [](const CollisionEvent& lhs,
                                                               const CollisionEvent& rhs) {
        return lhs.attacker == rhs.attacker && lhs.target == rhs.target;
    }), events.end());
    return events;
}

} // namespace melee::native
