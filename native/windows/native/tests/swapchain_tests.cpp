#include "native_swapchain.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace melee::native;
    NativeSwapChainDesc desc;
    desc.width = 320;
    desc.height = 240;
    desc.buffer_count = 2;
    NativeWin32SwapChain chain;
    const bool initialized = chain.initialize(desc);
#ifdef _WIN32
    if (initialized) {
        assert(chain.available());
        assert(chain.native_window() != nullptr);
        assert(chain.width() == 320 && chain.height() == 240);
        chain.pump_messages();
        // A hidden flip-model chain can present without a render target; this
        // validates queue/swap-chain ownership and caller-driven cadence.
        const bool presented = chain.present();
        (void)presented;
        assert(chain.presented_frames() <= 1);
    }
#else
    assert(!initialized);
#endif
    chain.shutdown();
    assert(!chain.available());
    assert(chain.native_window() == nullptr);
    std::cout << "native Win32 swap-chain test passed (available="
              << (initialized ? "true" : "false") << ")\n";
}
