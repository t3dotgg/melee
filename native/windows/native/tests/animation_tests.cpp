#include "native_animation.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    using namespace melee::native;
    NativeAnimationTrack track({{10, 10.0F}, {0, 0.0F}, {20, 20.0F}});
    assert(track.duration() == 20);
    assert(track.sample(-1.0F) == 0.0F);
    assert(std::abs(track.sample(5.0F) - 5.0F) < 1e-6F);
    assert(track.sample(99.0F) == 20.0F);
    assert(track.sample(std::numeric_limits<float>::quiet_NaN()) == 0.0F);
    std::cout << "native animation tests passed\n";
}
