#include "native_render.h"

#include <algorithm>
#include <stdexcept>

namespace melee::native {

RenderSnapshot interpolate(const RenderSnapshot& previous,
                           const RenderSnapshot& current, float alpha)
{
    if (!(alpha >= 0.0F && alpha <= 1.0F))
        throw std::invalid_argument("render interpolation alpha must be in [0,1]");
    RenderSnapshot result = current;
    result.objects.resize(current.objects.size());
    for (std::size_t i = 0; i < current.objects.size(); ++i) {
        const auto& now = current.objects[i];
        auto& out = result.objects[i];
        const auto before = std::find_if(previous.objects.begin(), previous.objects.end(),
            [&now](const RenderObject& candidate) { return candidate.id == now.id; });
        if (before == previous.objects.end() || before->material != now.material)
            continue;
        out.transform.x = before->transform.x + (now.transform.x - before->transform.x) * alpha;
        out.transform.y = before->transform.y + (now.transform.y - before->transform.y) * alpha;
        out.transform.z = before->transform.z + (now.transform.z - before->transform.z) * alpha;
    }
    return result;
}

} // namespace melee::native
