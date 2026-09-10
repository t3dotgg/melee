#include "native_match.h"

#include <vector>

namespace melee::native {

NativeTrainingMatch::NativeTrainingMatch()
{
    player_one_.set_position(-3.0F, 0.0F);
    player_two_.set_position(3.0F, 0.0F);
}

void NativeTrainingMatch::update(const FighterInput& player_one,
                                 const FighterInput& player_two, double dt_seconds)
{
    player_one_.update(player_one, dt_seconds);
    player_two_.update(player_two, dt_seconds);
    ++frame_;

    std::vector<NativeFighterCollisionState> fighters(2);
    const auto make_state = [](std::uint32_t id, const NativeFighter& fighter) {
        NativeFighterCollisionState state;
        state.id = id;
        const auto& value = fighter.state();
        state.hurtbox = {value.x - 0.6F, value.y, value.x + 0.6F, value.y + 1.8F};
        if (value.action == FighterAction::Attack)
            state.hitboxes.push_back({id * 10u + 1u,
                                      {value.x + (id == 1 ? 0.5F : -1.5F), value.y + 0.6F,
                                       value.x + (id == 1 ? 1.5F : -0.5F), value.y + 1.4F},
                                      3, 0, true});
        return state;
    };
    fighters[0] = make_state(1, player_one_);
    fighters[1] = make_state(2, player_two_);
    collisions_ = resolve_native_collisions(stage_, fighters, frame_);
}

RenderSnapshot NativeTrainingMatch::snapshot() const
{
    RenderSnapshot result;
    result.simulation_frame = frame_;
    result.objects.push_back({1, 0, {player_one_.state().x, player_one_.state().y, 0.0F}});
    result.objects.push_back({2, 0, {player_two_.state().x, player_two_.state().y, 0.0F}});
    return result;
}

} // namespace melee::native
