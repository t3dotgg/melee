#include "native_game.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {
using namespace melee::native;

class TestGame final : public NativeGame {
public:
    void update(const NativeInput& input, double dt) override
    {
        state_.frame++;
        state_.player_x += input.stick_x * static_cast<float>(dt);
    }
    const NativeFrameState& state() const noexcept override { return state_; }
    RenderSnapshot render_snapshot() const override
    {
        RenderSnapshot snapshot;
        snapshot.simulation_frame = state_.frame;
        snapshot.objects.push_back({1, 0, {state_.player_x, 0.0F, 0.0F}});
        return snapshot;
    }

private:
    NativeFrameState state_;
};

class TestInput final : public NativeInputSource {
public:
    NativeInput poll() override { return input; }
    NativeInput input{.stick_x = 1.0F};
};

class TestRenderer final : public NativeRenderer {
public:
    void render(const NativeFrameState&) override { assert(false); }
    void render(const NativeFrameState&, const RenderSnapshot& snapshot) override
    {
        ++count;
        last = snapshot;
    }
    std::uint32_t count = 0;
    RenderSnapshot last;
};
} // namespace

int main()
{
    TestGame game;
    TestInput input;
    TestRenderer renderer;
    NativeGameLoop loop(game, input, renderer);
    const auto first = loop.advance(1.0 / 60.0);
    assert(first.simulation_steps == 1 && first.render_frames == 2);
    assert(renderer.count == 1);
    const auto second = loop.advance(1.0 / 120.0);
    assert(second.simulation_steps == 0 && renderer.count == 2);
    assert(renderer.last.objects.size() == 1);
    // Mid-tick presentation changes position without running rules again.
    assert(std::abs(renderer.last.objects[0].transform.x - (1.0F / 120.0F)) < 1e-6F);
    assert(game.state().frame == 1);
    loop.advance(1.0 / 120.0);
    assert(game.state().frame == 2);
    assert(std::abs(renderer.last.objects[0].transform.x - (1.0F / 60.0F)) < 1e-6F);
    // Catch-up runs the rules for each tick but never submits duplicate
    // presentations for already-missed display slots.
    const auto before = renderer.count;
    const auto stalled = loop.advance(0.1);
    assert(stalled.simulation_steps == 6 && stalled.render_frames == 12);
    assert(renderer.count == before + 1);
    std::cout << "native game loop interpolation tests passed\n";
}
