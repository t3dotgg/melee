#include "native_game.h"
#include "native_timing.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace melee::native {

NativeDemoGame::NativeDemoGame(NativeGameMemory& memory) : memory_(memory)
{
    fighter_object_ = scene_.create_object(0, RenderObject{1, 0, {}},
        [this](NativeScene& scene, NativeObjectId id, double) {
            const auto& fighter = fighter_.state();
            state_.player_x = fighter.x;
            state_.player_y = fighter.y;
            scene.set_render_object(id, RenderObject{1, 0, {fighter.x, fighter.y, 0.0F}});
        });
}

void NativeDemoGame::update(const NativeInput& input, double dt_seconds)
{
    fighter_.update(FighterInput{input.stick_x, input.attack, input.special, input.jump}, dt_seconds);
    state_.frame = fighter_.state().frame;
    scene_.update(dt_seconds, state_.frame);
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
