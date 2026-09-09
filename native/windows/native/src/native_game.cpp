#include "native_game.h"
#include "native_timing.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace melee::native {

void NativeDemoGame::update(const NativeInput& input, double dt_seconds)
{
    state_.frame++;
    state_.player_x += input.stick_x * static_cast<float>(dt_seconds) * 5.0F;
    state_.player_y = std::max(-1.0F, std::min(1.0F, state_.player_y +
        input.stick_y * static_cast<float>(dt_seconds) * 5.0F));
    memory_.write_be_u32(0, static_cast<std::uint32_t>(state_.frame));
}

int run_native_loop(NativeGame& game, NativeInputSource& input,
                   NativeRenderer& renderer, std::uint64_t max_frames)
{
    using clock = std::chrono::steady_clock;
    NativeTimingScheduler scheduler;
    auto previous = clock::now();
    auto next_wake = previous;
    std::uint64_t simulation_frames = 0;
    while (max_frames == 0 || simulation_frames < max_frames) {
        const auto now = clock::now();
        const double elapsed = std::chrono::duration<double>(now - previous).count();
        previous = now;
        const auto result = scheduler.advance(elapsed, [&](double dt, std::uint64_t) {
            game.update(input.poll(), dt);
            ++simulation_frames;
        });
        // Rendering can run twice for each 60 Hz simulation tick. The state is
        // immutable for this shell; a production renderer will interpolate
        // snapshots using result.interpolation_alpha.
        for (std::uint32_t i = 0; i < result.render_frames; ++i)
            renderer.render(game.state());
        next_wake += std::chrono::microseconds(8333);
        std::this_thread::sleep_until(next_wake);
        if (clock::now() - next_wake > std::chrono::milliseconds(100))
            next_wake = clock::now();
    }
    return 0;
}

} // namespace melee::native
