#pragma once

#include <cstdint>
#include <functional>

namespace melee::native {

// Host-clock policy for a native game loop. Simulation remains deterministic at
// a fixed rate while rendering may run independently (for example, 120 Hz on a
// 120/240 Hz display).
struct TimingConfig {
    double simulation_hz = 60.0;
    double render_hz = 120.0;
    std::uint32_t max_simulation_steps_per_advance = 8;
    double max_elapsed_seconds = 0.25;
};

struct TimingStepResult {
    std::uint32_t simulation_steps = 0;
    std::uint32_t render_frames = 0;
    std::uint32_t dropped_simulation_steps = 0;
    double interpolation_alpha = 0.0;
    std::uint64_t simulation_frame = 0;
    std::uint64_t render_frame = 0;
};

class NativeTimingScheduler final {
public:
    using SimulationCallback = std::function<void(double, std::uint64_t)>;

    explicit NativeTimingScheduler(TimingConfig config = {});

    // Advances both clocks by elapsed wall time. The callback is invoked once
    // for each fixed simulation tick with (dt_seconds, frame_number).
    // Negative elapsed values are rejected; large values are clamped according
    // to TimingConfig to prevent an unbounded catch-up spiral.
    TimingStepResult advance(double elapsed_seconds,
                             const SimulationCallback& simulation = {});

    void reset() noexcept;
    const TimingConfig& config() const noexcept { return config_; }
    double simulation_dt() const noexcept { return simulation_dt_; }
    double render_dt() const noexcept { return render_dt_; }
    double simulation_time() const noexcept { return simulation_time_; }
    double render_time() const noexcept { return render_time_; }

private:
    TimingConfig config_;
    double simulation_dt_;
    double render_dt_;
    double simulation_accumulator_ = 0.0;
    double render_accumulator_ = 0.0;
    double simulation_time_ = 0.0;
    double render_time_ = 0.0;
    std::uint64_t simulation_frame_ = 0;
    std::uint64_t render_frame_ = 0;
};

} // namespace melee::native
