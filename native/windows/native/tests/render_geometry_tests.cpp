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

    NativeGeometryUploadPlan upload_plan;
    assert(plan_geometry_upload(geometry, upload_plan));
    assert(upload_plan.vertex_bytes == 8U * sizeof(NativeRenderVertex));
    assert(upload_plan.index_bytes == 12U * sizeof(std::uint32_t));
    assert(upload_plan.index_offset % 16U == 0);
    assert(upload_plan.total_bytes >= upload_plan.index_offset + upload_plan.index_bytes);
    assert(upload_plan.draw_count == 2);
    const auto complete_bytes = upload_plan.total_bytes;
    NativeRenderGeometry malformed = geometry;
    malformed.indices[0] = 99;
    assert(!plan_geometry_upload(malformed, upload_plan));
    assert(upload_plan.total_bytes == 0);
    malformed = geometry;
    malformed.draws[0].index_count = 5;
    assert(!plan_geometry_upload(malformed, upload_plan));
    malformed = geometry;
    malformed.vertices[0].position.z = INFINITY;
    assert(!plan_geometry_upload(malformed, upload_plan));
    assert(!plan_geometry_upload(geometry, upload_plan, upload_plan.vertex_bytes));
    assert(!plan_geometry_upload(geometry, upload_plan, complete_bytes - 1));
    assert(plan_geometry_upload(geometry, upload_plan, complete_bytes));
    malformed = geometry;
    malformed.draws[0].first_index = UINT32_MAX;
    assert(!plan_geometry_upload(malformed, upload_plan));
    malformed = geometry;
    malformed.draws.clear();
    assert(!plan_geometry_upload(malformed, upload_plan));
    assert(plan_geometry_upload({}, upload_plan, 0));
    assert(upload_plan.total_bytes == 0 && upload_plan.draw_count == 0);

    RenderCommandBuffer commands;
    commands.append(snapshot.objects[0]);
    const NativeRenderGeometry command_geometry = commands.build_proxy_geometry();
    assert(command_geometry.draws.size() == 1);
    assert(command_geometry.draws[0].object_id == snapshot.objects[0].id);

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
