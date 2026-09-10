#pragma once

#include "native_render.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace melee::native {

// Host-native object handles are opaque, stable identifiers.  They are never
// pointers and are not tied to a GameCube address or allocator layout.
using NativeObjectId = std::uint64_t;

class NativeScene;

// A callback runs on the simulation thread.  It may destroy objects (including
// itself), update render data, or create new objects.  Objects created from a
// callback are visible on the next update pass, which keeps dispatch
// deterministic and prevents iterator invalidation.
using NativeObjectCallback = std::function<void(NativeScene&, NativeObjectId, double)>;

struct NativeSceneObject {
    NativeObjectId id = 0;
    std::int32_t priority = 0;
    RenderObject render;
};

class NativeScene final {
public:
    NativeScene() = default;
    NativeScene(const NativeScene&) = delete;
    NativeScene& operator=(const NativeScene&) = delete;

    NativeObjectId create_object(std::int32_t priority, RenderObject render,
                                 NativeObjectCallback callback = {});
    bool destroy_object(NativeObjectId id) noexcept;
    bool contains(NativeObjectId id) const noexcept;

    // Changes are applied in priority order (lower values first).  Equal
    // priorities preserve creation order.  Removal during dispatch is safe and
    // prevents a removed callback from running later in the same pass.
    void update(double dt_seconds, std::uint64_t simulation_frame);

    bool set_render_object(NativeObjectId id, RenderObject render) noexcept;
    bool set_callback(NativeObjectId id, NativeObjectCallback callback);

    // Extract an owned, pointer-free snapshot in the same deterministic order
    // used by callbacks.  Dead objects are omitted.
    RenderSnapshot extract_snapshot(std::uint64_t simulation_frame) const;

    std::size_t size() const noexcept;
    std::uint64_t update_count() const noexcept { return update_count_; }

private:
    struct Entry {
        NativeSceneObject object;
        NativeObjectCallback callback;
        std::uint64_t insertion_order = 0;
        bool alive = true;
    };

    Entry* find_entry(NativeObjectId id) noexcept;
    const Entry* find_entry(NativeObjectId id) const noexcept;
    static bool entry_before(const Entry& lhs, const Entry& rhs) noexcept;
    void compact_dead();

    std::vector<Entry> entries_;
    NativeObjectId next_id_ = 1;
    std::uint64_t next_insertion_order_ = 0;
    std::uint64_t update_count_ = 0;
    bool dispatching_ = false;
};

} // namespace melee::native
