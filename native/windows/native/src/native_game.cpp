#include "native_game.h"

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
    constexpr auto frame_time = std::chrono::microseconds(16667);
    auto next_frame = clock::now();
    std::uint64_t frames = 0;
    while (max_frames == 0 || frames < max_frames) {
        const auto frame_start = clock::now();
        game.update(input.poll(), 1.0 / 60.0);
        renderer.render(game.state());
        ++frames;
        next_frame += frame_time;
        std::this_thread::sleep_until(next_frame);
        if (clock::now() - frame_start > frame_time * 4) {
            next_frame = clock::now();
        }
    }
    return 0;
}

} // namespace melee::native
