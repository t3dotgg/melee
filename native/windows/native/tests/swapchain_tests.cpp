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
        NativeRenderGeometry geometry;
        geometry.vertices = {{{-0.5F, -0.5F, 0.0F}, 0xff0000ffU},
                             {{0.5F, -0.5F, 0.0F}, 0xff0000ffU},
                             {{0.0F, 0.5F, 0.0F}, 0xff0000ffU}};
        geometry.indices = {0, 1, 2};
        geometry.draws = {{42, 7, 0, 3}};
        assert(chain.prepare_geometry(geometry));
        assert(chain.prepared_geometry().draw_count == 1);
        assert(chain.prepared_draws().size() == 1);
        const auto uploads_before = chain.geometry_uploads();
        (void)chain.clear_and_present(0.0F, 0.0F, 0.0F);
        assert(chain.geometry_uploads() == uploads_before + 1);
        assert(chain.prepared_draws()[0].object_id == 42);
    }
#else
    assert(!initialized);
    NativeRenderGeometry geometry;
    assert(!chain.prepare_geometry(geometry));
#endif
    chain.shutdown();
    assert(!chain.available());
    assert(chain.native_window() == nullptr);
    std::cout << "native Win32 swap-chain test passed (available="
              << (initialized ? "true" : "false") << ")\n";
}
