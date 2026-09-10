#include "native_timing.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace melee::native {

namespace {
constexpr double kEpsilon = 1e-12;
}

NativeTimingScheduler::NativeTimingScheduler(TimingConfig config)
    : config_(config), simulation_dt_(1.0 / config.simulation_hz),
      render_dt_(1.0 / config.render_hz)
{
    if (!(config_.simulation_hz > 0.0) || !std::isfinite(config_.simulation_hz))
        throw std::invalid_argument("simulation_hz must be finite and positive");
    if (!(config_.render_hz > 0.0) || !std::isfinite(config_.render_hz))
        throw std::invalid_argument("render_hz must be finite and positive");
    if (config_.max_simulation_steps_per_advance == 0)
        throw std::invalid_argument("max_simulation_steps_per_advance must be nonzero");
    if (!(config_.max_elapsed_seconds > 0.0) || !std::isfinite(config_.max_elapsed_seconds))
        throw std::invalid_argument("max_elapsed_seconds must be finite and positive");
}

TimingStepResult NativeTimingScheduler::advance(double elapsed_seconds,
                                                const SimulationCallback& simulation)
{
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0)
        throw std::invalid_argument("elapsed_seconds must be finite and nonnegative");

    const double elapsed = std::min(elapsed_seconds, config_.max_elapsed_seconds);
    simulation_accumulator_ += elapsed;
    render_accumulator_ += elapsed;

    TimingStepResult result;
    while (simulation_accumulator_ + kEpsilon >= simulation_dt_ &&
           result.simulation_steps < config_.max_simulation_steps_per_advance) {
        simulation_accumulator_ -= simulation_dt_;
        if (simulation_accumulator_ < 0.0)
            simulation_accumulator_ = 0.0;
        ++simulation_frame_;
        simulation_time_ += simulation_dt_;
        ++result.simulation_steps;
        if (simulation)
            simulation(simulation_dt_, simulation_frame_);
    }

    if (simulation_accumulator_ + kEpsilon >= simulation_dt_) {
        const auto dropped = static_cast<std::uint32_t>(
            std::floor((simulation_accumulator_ + kEpsilon) / simulation_dt_));
        result.dropped_simulation_steps = dropped;
        simulation_accumulator_ -= static_cast<double>(dropped) * simulation_dt_;
    }

    while (render_accumulator_ + kEpsilon >= render_dt_) {
        render_accumulator_ -= render_dt_;
        if (render_accumulator_ < 0.0)
            render_accumulator_ = 0.0;
        ++render_frame_;
        render_time_ += render_dt_;
        ++result.render_frames;
    }

    result.interpolation_alpha = simulation_accumulator_ / simulation_dt_;
    if (result.interpolation_alpha < 0.0)
        result.interpolation_alpha = 0.0;
    if (result.interpolation_alpha > 1.0)
        result.interpolation_alpha = 1.0;
    result.simulation_frame = simulation_frame_;
    result.render_frame = render_frame_;
    return result;
}

void NativeTimingScheduler::reset() noexcept
{
    simulation_accumulator_ = 0.0;
    render_accumulator_ = 0.0;
    simulation_time_ = 0.0;
    render_time_ = 0.0;
    simulation_frame_ = 0;
    render_frame_ = 0;
}

} // namespace melee::native
