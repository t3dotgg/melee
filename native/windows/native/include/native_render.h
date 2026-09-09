#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace melee::native {

struct RenderTransform {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct RenderObject {
    std::uint32_t id = 0;
    std::uint32_t material = 0;
    RenderTransform transform;
};

// Immutable per-tick data handed from simulation to a renderer. Objects are
// ordered explicitly so a backend does not depend on pointer/list layout.
struct RenderSnapshot {
    std::uint64_t simulation_frame = 0;
    std::vector<RenderObject> objects;
};

RenderSnapshot interpolate(const RenderSnapshot& previous,
                           const RenderSnapshot& current, float alpha);

class RenderCommandBuffer final {
public:
    void clear() noexcept { commands_.clear(); }
    void append(RenderObject object) { commands_.push_back(object); }
    std::span<const RenderObject> commands() const noexcept { return commands_; }

private:
    std::vector<RenderObject> commands_;
};

} // namespace melee::native
