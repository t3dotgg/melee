#include "native_game.h"
#include "native_timing.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace melee::native {

RenderSnapshot NativeGame::render_snapshot() const
{
    const auto& value = state();
    RenderSnapshot snapshot;
    snapshot.simulation_frame = value.frame;
    snapshot.objects.push_back({1, 0, {value.player_x, value.player_y, 0.0F}});
    return snapshot;
}

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

RenderSnapshot NativeDemoGame::render_snapshot() const
{
    return scene_.extract_snapshot(state_.frame);
}

NativeTrainingGame::NativeTrainingGame()
    : audio_(AudioMixerConfig{}, make_platform_audio_backend())
{
}

void NativeTrainingGame::update(const NativeInput& input, double dt_seconds)
{
    const FighterInput player_one{input.stick_x, input.attack, input.special, input.jump};
    const FighterInput player_two{input.stick_x2, input.attack2, input.special2, input.jump2};
    match_.update(player_one, player_two, dt_seconds);
    if (input.attack || input.special) {
        (void)audio_.play(AudioVoiceRequest{input.attack ? 1U : 2U, 0.08, 0.18F, 0.0F,
                                            1.0F, input.attack ? 4U : 3U});
    }
    audio_.advance(dt_seconds);
    const auto snapshot = match_.snapshot();
    state_.frame = snapshot.simulation_frame;
    const auto& first = match_.player_one().state();
    state_.player_x = first.x;
    state_.player_y = first.y;
}

NativeGameLoop::NativeGameLoop(NativeGame& game, NativeInputSource& input,
                               NativeRenderer& renderer)
    : game_(game), input_(input), renderer_(renderer),
      previous_snapshot_(game.render_snapshot()), current_snapshot_(previous_snapshot_)
{
}

TimingStepResult NativeGameLoop::advance(double elapsed_seconds)
{
    const auto result = scheduler_.advance(elapsed_seconds, [this](double dt, std::uint64_t) {
        previous_snapshot_ = current_snapshot_;
        game_.update(input_.poll(), dt);
        current_snapshot_ = game_.render_snapshot();
    });
    if (result.render_frames != 0 && renderer_.running()) {
        // Present at the current wall-clock position, one tick behind rules.
        // If the host stalled, earlier display slots have already passed;
        // replaying them would queue stale frames and make positions regress.
        renderer_.render(game_.state(), interpolate(previous_snapshot_, current_snapshot_,
                            static_cast<float>(result.interpolation_alpha)));
        ++presented_frames_;
    }
    return result;
}

int run_native_loop(NativeGame& game, NativeInputSource& input,
                   NativeRenderer& renderer, std::uint64_t max_frames)
{
    using clock = std::chrono::steady_clock;
    NativeGameLoop loop(game, input, renderer);
    auto previous = clock::now();
    auto next_wake = previous;
    std::uint64_t simulation_frames = 0;
    while ((max_frames == 0 || simulation_frames < max_frames) && renderer.running()) {
        const auto now = clock::now();
        const double elapsed = std::chrono::duration<double>(now - previous).count();
        previous = now;
        const auto result = loop.advance(elapsed);
        simulation_frames += result.simulation_steps;
        next_wake += std::chrono::microseconds(8333);
        std::this_thread::sleep_until(next_wake);
        if (clock::now() - next_wake > std::chrono::milliseconds(100))
            next_wake = clock::now();
    }
    return 0;
}

} // namespace melee::native
