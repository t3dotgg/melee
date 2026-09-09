#pragma once

#include "native_game_memory.h"
#include "native_fighter.h"
#include "native_match.h"
#include "native_render.h"
#include "native_scene.h"

#include <cstdint>

namespace melee::native {

struct NativeInput {
    float stick_x = 0.0F;
    float stick_y = 0.0F;
    bool attack = false;
    bool special = false;
    bool jump = false;
    bool start = false;
};

struct NativeFrameState {
    std::uint64_t frame = 0;
    float player_x = 0.0F;
    float player_y = 0.0F;
};

class NativeGame {
public:
    virtual ~NativeGame() = default;
    virtual void update(const NativeInput& input, double dt_seconds) = 0;
    virtual const NativeFrameState& state() const noexcept = 0;

    // The renderer consumes an owned snapshot so simulation state never leaks
    // pointers or guest-memory assumptions across the thread boundary.  A
    // default one-object snapshot keeps small host games source compatible.
    virtual RenderSnapshot render_snapshot() const;
};

// A tiny deterministic implementation used by the shell and tests. Real
// fighters, collision, archives, and rendering can replace this interface one
// subsystem at a time without reintroducing guest memory assumptions.
class NativeDemoGame final : public NativeGame {
public:
    explicit NativeDemoGame(NativeGameMemory& memory);
    void update(const NativeInput& input, double dt_seconds) override;
    const NativeFrameState& state() const noexcept override { return state_; }
    RenderSnapshot render_snapshot() const override;

private:
    NativeGameMemory& memory_;
    NativeFighter fighter_;
    NativeScene scene_;
    NativeObjectId fighter_object_ = 0;
    NativeFrameState state_;
};

// A NativeGame adapter around the two-fighter training rules.  It gives the
// executable a real game path while retaining NativeDemoGame for deterministic
// API tests and compatibility with existing hosts.  Player two is idle until
// a multi-pad input source is added; its state and collision events are still
// simulated and rendered through the same snapshot contract.
class NativeTrainingGame final : public NativeGame {
public:
    NativeTrainingGame() = default;
    void update(const NativeInput& input, double dt_seconds) override;
    const NativeFrameState& state() const noexcept override { return state_; }
    RenderSnapshot render_snapshot() const override { return match_.snapshot(); }
    const NativeTrainingMatch& match() const noexcept { return match_; }

private:
    NativeTrainingMatch match_;
    NativeFrameState state_;
};

class NativeInputSource {
public:
    virtual ~NativeInputSource() = default;
    virtual NativeInput poll() = 0;
};

class NativeRenderer {
public:
    virtual ~NativeRenderer() = default;
    virtual void render(const NativeFrameState& state) = 0;
    virtual void render(const NativeFrameState& state,
                        const RenderSnapshot& snapshot)
    {
        (void)snapshot;
        render(state);
    }
};

int run_native_loop(NativeGame& game, NativeInputSource& input,
                   NativeRenderer& renderer, std::uint64_t max_frames = 0);

} // namespace melee::native
