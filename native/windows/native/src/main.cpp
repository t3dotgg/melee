#include "native_game.h"
#include "native_input.h"
#include "native_render_geometry.h"
#include "native_swapchain.h"
#include "native_windows_input.h"

#include <charconv>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
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
        const auto convert = [this](std::uint32_t index, melee::native::XboxPadMapper& mapper) {
            melee::native::XInputState raw{};
            if (!device_.read(index, raw)) return melee::native::PadState{};
            return mapper.update(raw);
        };
        const auto first = convert(0, mapper_);
        const auto second = convert(1, mapper_two_);
        // Gameplay actions are edge-triggered at the 60 Hz boundary. Sticks
        // remain level-triggered, matching the controller queue semantics.
        return {first.stick_x, first.stick_y,
                first.pressed(melee::native::PadButton::Attack),
                first.pressed(melee::native::PadButton::Special),
                first.pressed(melee::native::PadButton::Jump),
                first.pressed(melee::native::PadButton::Start),
                second.stick_x, second.stick_y,
                second.pressed(melee::native::PadButton::Attack),
                second.pressed(melee::native::PadButton::Special),
                second.pressed(melee::native::PadButton::Jump),
                second.pressed(melee::native::PadButton::Start)};
    }

private:
    melee::native::WindowsXInputDevice device_;
    melee::native::XboxPadMapper mapper_;
    melee::native::XboxPadMapper mapper_two_;
};

class D3D12Renderer final : public melee::native::NativeRenderer {
public:
    explicit D3D12Renderer(melee::native::NativeWin32SwapChain& chain) : chain_(chain) {}
    bool running() const noexcept override { return !chain_.close_requested(); }
    void set_capture_path(std::filesystem::path path) { capture_path_ = std::move(path); }

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
        chain_.prepare_geometry(geometry);
        const float red = 0.04F + std::min(0.5F, std::abs(state.player_x) * 0.04F);
        const float green = 0.10F + std::min(0.5F, std::max(0.0F, state.player_y) * 0.06F);
        chain_.clear_and_present(red, green, geometry.empty() ? 0.12F : 0.20F, 1.0F,
                                 !capture_path_.empty());
        maybe_capture();
    }

    void render(const melee::native::NativeFrameState& state,
                const melee::native::RenderSnapshot& snapshot) override
    {
        chain_.pump_messages();
        const auto geometry = melee::native::build_proxy_geometry(snapshot);
        chain_.prepare_geometry(geometry);
        const float red = 0.04F + std::min(0.5F, std::abs(state.player_x) * 0.04F);
        const float green = 0.10F + std::min(0.5F, std::max(0.0F, state.player_y) * 0.06F);
        chain_.clear_and_present(red, green, geometry.empty() ? 0.12F : 0.20F, 1.0F,
                                 !capture_path_.empty());
        maybe_capture();
    }

private:
    void maybe_capture()
    {
        if (captured_ || capture_path_.empty()) return;
        const auto pixels = chain_.captured_pixels();
        if (pixels.empty()) return;
        std::ofstream output(capture_path_, std::ios::binary);
        output << "P6\n" << chain_.width() << ' ' << chain_.height() << "\n255\n";
        for (std::size_t offset = 0; offset + 3 <= pixels.size(); offset += 4)
            output.write(reinterpret_cast<const char*>(pixels.data() + offset), 3);
        captured_ = output.good();
    }
    melee::native::NativeWin32SwapChain& chain_;
    std::filesystem::path capture_path_;
    bool captured_ = false;
};
} // namespace

int main(int argc, char** argv)
{
    melee::native::NativeGameMemory memory(1024 * 1024);
    melee::native::NativeDemoGame demo_game(memory);
    melee::native::NativeTrainingGame training_game;
    melee::native::NativeGame* game = &demo_game;
    XInputSource input;
    melee::native::NativeWin32SwapChain chain;
    const bool gpu = chain.initialize();
    if (gpu) {
        chain.show();
    }
    ConsoleRenderer console;
    D3D12Renderer native_renderer(chain);
    std::uint64_t max_frames = 0;
    bool training = false;
    bool preview = false;
    std::filesystem::path preview_path;
    std::size_t preview_offset = 0;
    std::filesystem::path capture_path;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--training") {
            training = true;
            continue;
        }
        if (argument == "--dat-preview" && i + 2 < argc) {
            preview = true;
            preview_path = argv[++i];
            const auto* first = argv[++i];
            const auto* last = first + std::char_traits<char>::length(first);
            const auto parsed = std::from_chars(first, last, preview_offset);
            if (parsed.ec != std::errc{} || parsed.ptr != last) {
                std::cerr << "usage: melee_native_shell [--training] [--dat-preview PATH OFFSET] [--frames N]\n";
                return 2;
            }
            continue;
        }
        if (argument == "--capture" && i + 1 < argc) {
            capture_path = argv[++i];
            continue;
        }
        if (argument == "--frames" && i + 1 < argc) {
            const auto* first = argv[++i];
            const auto* last = first + std::char_traits<char>::length(first);
            const auto parsed = std::from_chars(first, last, max_frames);
            if (parsed.ec != std::errc{} || parsed.ptr != last) {
                std::cerr << "usage: melee_native_shell [--training] [--dat-preview PATH OFFSET] [--frames N]\n";
                return 2;
            }
            continue;
        }
        std::cerr << "usage: melee_native_shell [--training] [--dat-preview PATH OFFSET] [--frames N]\n";
        return 2;
    }
    if (training) game = &training_game;
    native_renderer.set_capture_path(capture_path);
    std::unique_ptr<melee::native::NativeAssetPreviewGame> preview_game;
    if (preview) {
        try {
            preview_game = std::make_unique<melee::native::NativeAssetPreviewGame>(
                preview_path, preview_offset);
            game = preview_game.get();
        } catch (const std::exception& error) {
            std::cerr << "DAT preview failed: " << error.what() << "\n";
            chain.shutdown();
            return 3;
        }
    }
    const int result = melee::native::run_native_loop(*game, input,
        gpu ? static_cast<melee::native::NativeRenderer&>(native_renderer)
            : static_cast<melee::native::NativeRenderer&>(console), max_frames);
    chain.shutdown();
    return result;
}
