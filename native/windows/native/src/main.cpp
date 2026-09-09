#include "native_game.h"

#include <iostream>

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

int main()
{
    melee::native::NativeGameMemory memory(1024 * 1024);
    melee::native::NativeDemoGame game(memory);
    KeyboardInput input;
    ConsoleRenderer renderer;
    return melee::native::run_native_loop(game, input, renderer, 0);
}
