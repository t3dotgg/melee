#include "native_swapchain.h"

#include <cassert>
#include <iostream>
#include <fstream>

int main(int argc, char** argv)
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
        assert(chain.clear_and_present(0.0F, 0.0F, 0.0F, 1.0F, true));
        assert(chain.geometry_uploads() == uploads_before + 1);
        assert(chain.prepared_draws()[0].object_id == 42);
        assert(chain.draw_calls() >= 1);
        const auto pixels = chain.captured_pixels();
        assert(pixels.size() == 320U * 240U * 4U);
        const auto center = (120U * 320U + 160U) * 4U;
        assert(pixels[center] == 255 && pixels[center + 1] == 0 && pixels[center + 2] == 0);
        assert(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0);
        if (argc == 2) {
            std::ofstream image(argv[1], std::ios::binary);
            image << "P6\n320 240\n255\n";
            for (std::size_t p = 0; p < pixels.size(); p += 4)
                image.write(reinterpret_cast<const char*>(pixels.data() + p), 3);
            assert(image.good());
        }
        for (auto& vertex : geometry.vertices) vertex.color = 0xffff0000U;
        assert(chain.prepare_geometry(geometry));
        assert(chain.clear_and_present(0.0F, 0.0F, 0.0F, 1.0F, true));
        assert(chain.captured_pixels()[center] == 0 && chain.captured_pixels()[center + 2] == 255);
        assert(chain.geometry_uploads() == uploads_before + 2);
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
