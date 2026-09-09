#include "native_collision.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace melee::native;
    NativeCollisionWorld world(StageBounds{-10.0F, 10.0F, 0.0F, 5.0F});
    float x = 20.0F, y = -2.0F;
    world.constrain(x, y, 1.0F, 2.0F);
    assert(x == 9.0F && y == 0.0F);

    const std::vector<Hitbox> boxes{
        {2, 1.0F, 1.0F, 2.0F, 2.0F, 7},
        {1, 2.0F, 1.0F, 2.0F, 2.0F, 5},
        {3, 20.0F, 20.0F, 1.0F, 1.0F, 9},
    };
    const auto events = world.resolve(boxes);
    assert(events.size() == 2);
    assert(events[0].attacker == 1 && events[0].target == 2 && events[0].damage == 5);
    assert(events[1].attacker == 2 && events[1].target == 1 && events[1].damage == 7);
    std::cout << "native collision tests passed\n";
}
