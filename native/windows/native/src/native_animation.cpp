#include "native_animation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace melee::native {

NativeAnimationTrack::NativeAnimationTrack(std::vector<AnimationKeyframe> keys)
    : keys_(std::move(keys))
{
    std::stable_sort(keys_.begin(), keys_.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.frame < rhs.frame;
    });
}

float NativeAnimationTrack::sample(float frame) const noexcept
{
    if (keys_.empty() || !std::isfinite(frame)) return 0.0F;
    if (frame <= static_cast<float>(keys_.front().frame)) return keys_.front().value;
    if (frame >= static_cast<float>(keys_.back().frame)) return keys_.back().value;
    const auto upper = std::upper_bound(keys_.begin(), keys_.end(), frame,
        [](float value, const AnimationKeyframe& key) { return value < static_cast<float>(key.frame); });
    const auto& after = *upper;
    const auto& before = *(upper - 1);
    const float span = static_cast<float>(after.frame - before.frame);
    const float alpha = span == 0.0F ? 0.0F : (frame - before.frame) / span;
    return before.value + (after.value - before.value) * alpha;
}

std::uint32_t NativeAnimationTrack::duration() const noexcept
{
    return keys_.empty() ? 0 : keys_.back().frame;
}

} // namespace melee::native
