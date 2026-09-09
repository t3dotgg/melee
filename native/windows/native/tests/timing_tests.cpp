#include "native_timing.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace melee::native;
    NativeTimingScheduler scheduler;
    std::uint64_t callbacks = 0;
    auto tick = [&](double dt, std::uint64_t frame) {
        assert(std::abs(dt - 1.0 / 60.0) < 1e-12);
        assert(frame == ++callbacks);
    };

    auto result = scheduler.advance(1.0, tick);
    assert(result.simulation_steps == 8); // catch-up cap
    assert(result.dropped_simulation_steps == 7);
    assert(result.render_frames == 30); // elapsed is clamped to 0.25 s
    assert(callbacks == 8);

    scheduler.reset();
    callbacks = 0;
    result = scheduler.advance(0.5, tick);
    assert(result.simulation_steps == 8);
    assert(result.dropped_simulation_steps == 7);
    assert(result.render_frames == 30);

    scheduler.reset();
    result = scheduler.advance(1.0 / 120.0);
    assert(result.simulation_steps == 0);
    assert(result.render_frames == 1);
    assert(result.interpolation_alpha > 0.49 && result.interpolation_alpha < 0.51);

    result = scheduler.advance(1.0 / 120.0);
    assert(result.simulation_steps == 1);
    assert(result.render_frames == 1);
    assert(result.simulation_frame == 1 && result.render_frame == 2);

    bool threw = false;
    try {
        scheduler.advance(-0.01);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    threw = false;
    try {
        NativeTimingScheduler invalid(TimingConfig {.simulation_hz = 0.0});
        (void)invalid;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    std::cout << "native timing tests passed\n";
}
