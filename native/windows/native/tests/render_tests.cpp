#include "native_render.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace melee::native;
    RenderSnapshot previous;
    previous.objects.push_back({7, 3, {0.0F, 2.0F, 4.0F}});
    RenderSnapshot current;
    current.simulation_frame = 2;
    current.objects.push_back({7, 3, {10.0F, 4.0F, 8.0F}});
    current.objects.push_back({9, 4, {1.0F, 1.0F, 1.0F}});
    const auto middle = interpolate(previous, current, 0.5F);
    assert(std::abs(middle.objects[0].transform.x - 5.0F) < 1e-6F);
    assert(std::abs(middle.objects[0].transform.y - 3.0F) < 1e-6F);
    assert(middle.objects[1].transform.x == 1.0F); // newly spawned objects are current state

    bool rejected = false;
    try { (void)interpolate(previous, current, 1.1F); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);

    RenderCommandBuffer commands;
    commands.append(current.objects[1]);
    commands.append(current.objects[0]);
    assert(commands.commands().size() == 2);
    assert(commands.commands()[0].id == 9 && commands.commands()[1].id == 7);
    commands.clear();
    assert(commands.commands().empty());
    std::cout << "native render tests passed\n";
}
