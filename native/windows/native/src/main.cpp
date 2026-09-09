#include "native_game.h"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {
class KeyboardInput final : public melee::native::NativeInputSource {
public:
    melee::native::NativeInput poll() override { return {}; }
};

class ConsoleRenderer final : public melee::native::NativeRenderer {
public:
    void render(const melee::native::NativeFrameState& state) override
    {
        if (state.frame == 1) {
            std::cout << "Melee native Windows shell (experimental)\n";
        }
    }
};
} // namespace

int main(int argc, char** argv)
{
    melee::native::NativeGameMemory memory(1024 * 1024);
    melee::native::NativeDemoGame game(memory);
    KeyboardInput input;
    ConsoleRenderer renderer;
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
    return melee::native::run_native_loop(game, input, renderer, max_frames);
}
