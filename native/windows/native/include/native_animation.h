#pragma once

#include <cstdint>
#include <vector>

namespace melee::native {

struct AnimationKeyframe {
    std::uint32_t frame = 0;
    float value = 0.0F;
};

class NativeAnimationTrack final {
public:
    explicit NativeAnimationTrack(std::vector<AnimationKeyframe> keys = {});
    float sample(float frame) const noexcept;
    std::uint32_t duration() const noexcept;
    const std::vector<AnimationKeyframe>& keys() const noexcept { return keys_; }

private:
    std::vector<AnimationKeyframe> keys_;
};

} // namespace melee::native
