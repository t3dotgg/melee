#include "native_scene.h"

#include <cassert>
#include <iostream>
#include <vector>

int main()
{
    using namespace melee::native;
    NativeScene scene;
    std::vector<NativeObjectId> calls;

    const auto low = scene.create_object(10, {0, 1, {1, 0, 0}},
        [&calls](NativeScene&, NativeObjectId id, double) { calls.push_back(id); });
    const auto first_equal = scene.create_object(20, {0, 2, {2, 0, 0}},
        [&calls](NativeScene&, NativeObjectId id, double) { calls.push_back(id); });
    const auto high = scene.create_object(30, {0, 3, {3, 0, 0}},
        [&calls](NativeScene&, NativeObjectId id, double) { calls.push_back(id); });
    const auto second_equal = scene.create_object(20, {0, 4, {4, 0, 0}},
        [&calls](NativeScene&, NativeObjectId id, double) { calls.push_back(id); });

    scene.update(1.0 / 60.0, 1);
    assert((calls == std::vector<NativeObjectId>{low, first_equal, second_equal, high}));

    // Removing an object from an earlier callback must prevent its callback
    // from running later in the same pass; self-removal is also safe.
    calls.clear();
    scene.set_callback(low, [&calls, high, second_equal](NativeScene& current,
                                                          NativeObjectId id, double) {
        calls.push_back(id);
        current.destroy_object(second_equal);
        current.destroy_object(id);
        current.destroy_object(high);
    });
    scene.update(1.0 / 60.0, 2);
    assert((calls == std::vector<NativeObjectId>{low, first_equal}));
    assert(!scene.contains(low) && !scene.contains(second_equal) && !scene.contains(high));
    assert(scene.size() == 1);

    // New objects are deferred from the current dispatch pass and are ordered
    // by priority, preserving insertion order for ties.
    calls.clear();
    const auto spawned = scene.create_object(5, {0, 5, {5, 0, 0}},
        [&calls](NativeScene&, NativeObjectId id, double) { calls.push_back(id); });
    scene.update(1.0 / 60.0, 3);
    assert((calls == std::vector<NativeObjectId>{spawned, first_equal}));

    const auto snapshot = scene.extract_snapshot(3);
    assert(snapshot.simulation_frame == 3);
    assert(snapshot.objects.size() == 2);
    assert(snapshot.objects[0].id == static_cast<std::uint32_t>(spawned));
    assert(snapshot.objects[1].id == static_cast<std::uint32_t>(first_equal));
    assert(snapshot.objects[0].material == 5 && snapshot.objects[1].material == 2);

    assert(!scene.destroy_object(999999));
    std::cout << "native scene tests passed\n";
}
