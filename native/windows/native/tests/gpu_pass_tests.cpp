#include "native_swapchain.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace melee::native;
    NativeSwapChainDesc desc;
    desc.width = 64;
    desc.height = 64;
    desc.buffer_count = 2;
    NativeWin32SwapChain chain;
    const bool initialized = chain.initialize(desc);
#ifdef _WIN32
    if (initialized) {
        assert(chain.available());
        // This exercises command allocator/list recording, PRESENT -> RTV
        // transitions, queue submission, fence completion, and Present.
        assert(chain.clear_and_present(0.08f, 0.16f, 0.32f, 1.0f));
        assert(chain.presented_frames() == 1);
    }
#else
    assert(!initialized);
    assert(!chain.clear_and_present(0.0f, 0.0f, 0.0f));
#endif
    chain.shutdown();
    assert(!chain.available());
    std::cout << "native D3D12 clear pass test passed (available="
              << (initialized ? "true" : "false") << ")\n";
}
