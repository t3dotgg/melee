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
    std::cout << "native training match tests passed\n";
}
