#include "native_render_geometry.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace melee::native;
    RenderSnapshot snapshot;
    snapshot.objects = {{17, 2, {3.0F, 4.0F, 0.25F}}, {23, 9, {-1.0F, 0.0F, -2.0F}}};
    const NativeRenderGeometry geometry = build_proxy_geometry(snapshot, 0.5F, 1.0F);
    assert(!geometry.empty());
    assert(geometry.vertices.size() == 8);
    assert(geometry.indices.size() == 12);
    assert(geometry.draws.size() == 2);
    assert(geometry.draws[0].object_id == 17 && geometry.draws[0].material == 2);
    assert(geometry.draws[0].first_index == 0 && geometry.draws[0].index_count == 6);
    assert(geometry.draws[1].first_index == 6 && geometry.draws[1].index_count == 6);
    assert(std::abs(geometry.vertices[0].position.x - 2.5F) < 1e-6F);
    assert(std::abs(geometry.vertices[2].position.y - 5.0F) < 1e-6F);
    assert(geometry.indices[0] == 0 && geometry.indices[5] == 3);
    assert(geometry.vertices[0].color != geometry.vertices[4].color);

    bool rejected = false;
    try {
        (void)build_proxy_geometry(snapshot, 0.0F, 1.0F);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    rejected = false;
    snapshot.objects[0].transform.x = NAN;
    try {
        (void)build_proxy_geometry(snapshot);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    std::cout << "native render geometry tests passed\n";
}
