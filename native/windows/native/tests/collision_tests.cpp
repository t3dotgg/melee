#include "native_collision.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace melee::native;

namespace {
NativeFighterCollisionState fighter(std::uint32_t id, NativeAabb hurtbox)
{
    NativeFighterCollisionState state;
    state.id = id;
    state.hurtbox = hurtbox;
    return state;
}
}

int main()
{
    const NativeStageBounds stage{{0.0F, 0.0F, 10.0F, 10.0F}};
    auto edge = fighter(7, {-2.0F, 1.0F, 0.5F, 3.0F});
    edge.hitboxes.push_back({4, {-2.0F, 1.0F, 1.0F, 2.0F}, 3, 0, true});
    const auto edge_result = resolve_native_collisions(stage, std::span(&edge, 1), 12);
    assert(edge.hurtbox.min_x == 0.0F);
    assert(edge.hitboxes[0].bounds.min_x == 0.0F);
    assert(edge_result.events.empty());

    auto attacker = fighter(20, {3.0F, 2.0F, 4.0F, 4.0F});
    attacker.hitboxes = {{9, {4.0F, 2.5F, 6.0F, 3.5F}, 40000, 1, true},
                         {2, {3.5F, 2.0F, 5.0F, 3.0F}, 30000, 0, true}};
    auto target = fighter(10, {4.5F, 2.0F, 5.5F, 4.0F});
    target.damage = 60000;
    std::vector<NativeFighterCollisionState> reversed{attacker, target};
    const auto result = resolve_native_collisions(stage, reversed, 99);
    assert(result.events.size() == 2);
    // Priority 0 hitbox is resolved before priority 1, regardless of vector order.
    assert(result.events[0].hitbox_id == 2 && result.events[0].attacker_id == 20);
    assert(result.events[0].target_id == 10 && result.events[0].simulation_frame == 99);
    assert(result.events[1].hitbox_id == 9);
    assert(reversed[1].damage == UINT16_MAX);
    assert(result.events[0].overlap.valid());

    auto first = fighter(1, {0.0F, 1.0F, 1.0F, 2.0F});
    first.hitboxes.push_back({3, {0.5F, 1.0F, 2.0F, 2.0F}, 1, 0, true});
    auto second = fighter(2, {0.5F, 1.0F, 1.5F, 2.0F});
    second.hitboxes.push_back({1, {0.5F, 1.0F, 2.0F, 2.0F}, 1, 0, true});
    std::vector<NativeFighterCollisionState> ordered{second, first};
    const auto ordered_result = resolve_native_collisions(stage, ordered);
    assert(ordered_result.events.size() == 2);
    assert(ordered_result.events[0].attacker_id == 1);
    assert(ordered_result.events[1].attacker_id == 2);

    bool rejected = false;
    try {
        const NativeStageBounds invalid{{1.0F, 0.0F, 1.0F, 1.0F}};
        (void)resolve_native_collisions(invalid, std::span<NativeFighterCollisionState>{});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    std::cout << "native collision tests passed\n";
}
