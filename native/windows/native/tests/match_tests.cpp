#include "native_game.h"
#include "native_match.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace melee::native;
    NativeTrainingMatch match;
    assert(match.snapshot().objects.size() == 2);
    for (int i = 0; i < 60; ++i)
        match.update({.attack_pressed = i == 0}, {}, 1.0 / 60.0);
    assert(match.snapshot().simulation_frame == 60);
    assert(match.player_one().state().action == FighterAction::Idle);
    assert(match.last_collisions().events.empty()); // fighters start outside reach

    // The executable-facing adapter feeds the same training rules through the
    // NativeGame loop and exports both fighters as an owned render snapshot.
    NativeTrainingGame game;
    NativeInput input;
    input.stick_x = 1.0F;
    input.stick_x2 = -1.0F;
    game.update(input, 1.0 / 60.0);
    assert(game.state().frame == 1);
    assert(game.state().player_x < -2.8F);
    const auto snapshot = game.render_snapshot();
    assert(snapshot.simulation_frame == 1);
    assert(snapshot.objects.size() == 2);
    assert(snapshot.objects[0].id == 1 && snapshot.objects[1].id == 2);
    assert(snapshot.objects[0].transform.x < snapshot.objects[1].transform.x);
    assert(snapshot.objects[0].transform.x > -3.0F);
    assert(snapshot.objects[1].transform.x < 3.0F);
    std::cout << "native training match tests passed\n";
}
