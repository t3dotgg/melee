#include "native_game.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace melee::native;
    NativeGameMemory memory(8);
    memory.write_be_u16(0, 0x1234);
    memory.write_be_u32(2, 0x89ABCDEF);
    assert(memory.read_be_u16(0) == 0x1234);
    assert(memory.read_be_u32(2) == 0x89ABCDEF);
    assert(std::to_integer<std::uint8_t>(memory.bytes()[2]) == 0x89);

    bool threw = false;
    try {
        (void)memory.read_be_u32(5);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);
    threw = false;
    try {
        memory.write_u8(8, 1);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);

    NativeDemoGame game(memory);
    NativeInput input;
    input.stick_x = 1.0F;
    game.update(input, 1.0 / 60.0);
    assert(game.state().frame == 1);
    assert(memory.read_be_u32(0) == 1);
    std::cout << "native memory and game tests passed\n";
}
