#include "native_d3d12.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace melee::native;
    NativeD3D12Device device;
    const bool initialized = device.initialize();
    if (initialized) {
        assert(device.available());
        assert(device.feature_level() >= 0xb000);
    }
    device.shutdown();
    assert(!device.available() && device.feature_level() == 0);
    std::cout << "native D3D12 device test passed (available="
              << (initialized ? "true" : "false") << ")\n";
}
