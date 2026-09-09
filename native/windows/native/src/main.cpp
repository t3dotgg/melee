#include "native_game.h"
#include "native_input.h"
#include "native_render_geometry.h"
#include "native_swapchain.h"
#include "native_windows_input.h"

#include <charconv>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {
class ConsoleRenderer final : public melee::native::NativeRenderer {
public:
    void render(const melee::native::NativeFrameState& state) override
    {
        if (state.frame == 1) {
            std::cout << "Melee native Windows shell (experimental)\n";
        }
    }
};

class XInputSource final : public melee::native::NativeInputSource {
public:
    melee::native::NativeInput poll() override
    {
        melee::native::XInputState raw{};
        if (!device_.read(0, raw)) return {};
        const auto state = mapper_.update(raw);
        return {state.stick_x, state.stick_y,
                state.held(melee::native::PadButton::Attack),
                state.held(melee::native::PadButton::Special),
                state.held(melee::native::PadButton::Jump),
                state.held(melee::native::PadButton::Start)};
    }

private:
    melee::native::WindowsXInputDevice device_;
    melee::native::XboxPadMapper mapper_;
};

class D3D12Renderer final : public melee::native::NativeRenderer {
public:
    explicit D3D12Renderer(melee::native::NativeWin32SwapChain& chain) : chain_(chain) {}

    void render(const melee::native::NativeFrameState& state) override
    {
        chain_.pump_messages();
        // A visible deterministic frame proves the native command path is
        // connected to simulation state. The material/mesh renderer can use
        // the same swap-chain resources as it replaces this clear pass.
        melee::native::RenderSnapshot snapshot;
        snapshot.simulation_frame = state.frame;
        snapshot.objects.push_back({1, 0, {state.player_x, state.player_y, 0.0F}});
        const auto geometry = melee::native::build_proxy_geometry(snapshot);
        const float red = 0.04F + std::min(0.5F, std::abs(state.player_x) * 0.04F);
        const float green = 0.10F + std::min(0.5F, std::max(0.0F, state.player_y) * 0.06F);
        chain_.clear_and_present(red, green, geometry.empty() ? 0.12F : 0.20F, 1.0F);
    }

private:
    melee::native::NativeWin32SwapChain& chain_;
};
} // namespace

int main(int argc, char** argv)
{
    melee::native::NativeGameMemory memory(1024 * 1024);
    melee::native::NativeDemoGame game(memory);
    XInputSource input;
    melee::native::NativeWin32SwapChain chain;
    const bool gpu = chain.initialize();
    if (gpu) {
        chain.show();
    }
    ConsoleRenderer console;
    D3D12Renderer native_renderer(chain);
    std::uint64_t max_frames = 0;
    if (argc == 3 && std::string_view(argv[1]) == "--frames") {
        const auto* first = argv[2];
        const auto* last = first + std::char_traits<char>::length(first);
        const auto parsed = std::from_chars(first, last, max_frames);
        if (parsed.ec != std::errc{} || parsed.ptr != last) {
            std::cerr << "usage: melee_native_shell [--frames N]\n";
            return 2;
        }
    } else if (argc != 1) {
        std::cerr << "usage: melee_native_shell [--frames N]\n";
        return 2;
    }
    const int result = melee::native::run_native_loop(game, input,
        gpu ? static_cast<melee::native::NativeRenderer&>(native_renderer)
            : static_cast<melee::native::NativeRenderer&>(console), max_frames);
    chain.shutdown();
    return result;
}
