// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdlib>
#include <string_view>

namespace MeleeRender {
    // Allow the same executable to measure the original Metal queue and submit
    // order.
    inline bool LowLatency()
    {
        static const bool enabled = [] {
            const char* value = std::getenv("MELEE_METAL_LOW_LATENCY");
            return !value || std::string_view(value) != "0";
        }();
        return enabled;
    }
} // namespace MeleeRender
