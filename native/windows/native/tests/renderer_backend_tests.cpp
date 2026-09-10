#include "native_renderer_backend.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace melee::native;
    NativeNullRenderer renderer;
    assert(renderer.initialize());
    RenderSnapshot snapshot;
    snapshot.simulation_frame = 4;
    snapshot.objects.push_back({1, 2, {1.0F, 2.0F, 0.0F}});
    renderer.submit(snapshot);
    renderer.present();
    renderer.present();
    assert(renderer.presented_frames() == 2);
    assert(renderer.last_snapshot().simulation_frame == 4);
    NativeD3D12Probe probe;
    std::cout << "native renderer backend tests passed (d3d12="
              << (probe.available() ? "true" : "false") << ")\n";
}
