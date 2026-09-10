#include "native_windows_input.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace melee::native;
    WindowsXInputDevice device;
    XInputState sample{};
    // No controller is a valid runtime condition; the adapter must fail
    // cleanly instead of exposing an uninitialized GameCube PAD packet.
    assert(!device.read(4, sample));
    std::cout << "native Windows input adapter tests passed (available="
              << (device.available() ? "true" : "false") << ")\n";
}
