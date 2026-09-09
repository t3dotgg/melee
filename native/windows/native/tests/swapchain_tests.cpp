#include "native_swapchain.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace melee::native;
    NativeWindow window(320, 240, false);
    NativeD3D12Device device;
    NativeSwapChain swap;
    if (device.initialize() && swap.initialize(window, device)) {
        assert(swap.available());
        assert(swap.present());
        assert(swap.present_count() == 1);
    }
    swap.shutdown(); device.shutdown();
    assert(!swap.available());
    std::cout << "native swap-chain tests passed\n";
}
