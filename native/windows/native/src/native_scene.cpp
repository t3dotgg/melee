#include "native_scene.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace melee::native {
NativeObjectId NativeScene::create_object(std::int32_t priority, RenderObject render,
                                          NativeObjectCallback callback)
{
    if (next_id_ == 0)
        throw std::overflow_error("native scene object id space exhausted");
    const NativeObjectId id = next_id_++;
    render.id = static_cast<std::uint32_t>(id & std::numeric_limits<std::uint32_t>::max());
    entries_.push_back(Entry{{id, priority, render}, std::move(callback),
                              next_insertion_order_++, true});
    // Sorting here is cheap for the small object sets used by gameplay and
    // means snapshots are ordered even before the first simulation update.
    std::stable_sort(entries_.begin(), entries_.end(), entry_before);
    return id;
}

bool NativeScene::destroy_object(NativeObjectId id) noexcept
{
    Entry* entry = find_entry(id);
    if (entry == nullptr || !entry->alive)
        return false;
    entry->alive = false;
    if (!dispatching_)
        compact_dead();
    return true;
}

bool NativeScene::contains(NativeObjectId id) const noexcept
{
    const Entry* entry = find_entry(id);
    return entry != nullptr && entry->alive;
}

void NativeScene::update(double dt_seconds, std::uint64_t simulation_frame)
{
    if (!(dt_seconds >= 0.0))
        throw std::invalid_argument("native scene dt must be non-negative");

    // Capture handles before invoking user code.  A callback may mutate the
    // scene, but only handles present at pass start are considered this pass.
    std::vector<NativeObjectId> dispatch_ids;
    dispatch_ids.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        if (entry.alive)
            dispatch_ids.push_back(entry.object.id);
    }

    dispatching_ = true;
    try {
        for (const NativeObjectId id : dispatch_ids) {
            Entry* entry = find_entry(id);
            if (entry == nullptr || !entry->alive || !entry->callback)
                continue;
            entry->callback(*this, id, dt_seconds);
        }
    } catch (...) {
        dispatching_ = false;
        compact_dead();
        std::stable_sort(entries_.begin(), entries_.end(), entry_before);
        throw;
    }
    dispatching_ = false;
    ++update_count_;
    compact_dead();

    // A callback can alter priority indirectly by replacing its render data,
    // but object priority itself is immutable.  Keep this explicit sort to
    // document and enforce the ordering invariant after mutation.
    std::stable_sort(entries_.begin(), entries_.end(), entry_before);
    (void)simulation_frame;
}

bool NativeScene::set_render_object(NativeObjectId id, RenderObject render) noexcept
{
    Entry* entry = find_entry(id);
    if (entry == nullptr || !entry->alive)
        return false;
    render.id = static_cast<std::uint32_t>(id & std::numeric_limits<std::uint32_t>::max());
    entry->object.render = render;
    return true;
}

bool NativeScene::set_callback(NativeObjectId id, NativeObjectCallback callback)
{
    Entry* entry = find_entry(id);
    if (entry == nullptr || !entry->alive)
        return false;
    entry->callback = std::move(callback);
    return true;
}

RenderSnapshot NativeScene::extract_snapshot(std::uint64_t simulation_frame) const
{
    RenderSnapshot snapshot;
    snapshot.simulation_frame = simulation_frame;
    snapshot.objects.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        if (entry.alive)
            snapshot.objects.push_back(entry.object.render);
    }
    return snapshot;
}

std::size_t NativeScene::size() const noexcept
{
    std::size_t count = 0;
    for (const Entry& entry : entries_) {
        if (entry.alive)
            ++count;
    }
    return count;
}

NativeScene::Entry* NativeScene::find_entry(NativeObjectId id) noexcept
{
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [id](const Entry& entry) { return entry.object.id == id; });
    return it == entries_.end() ? nullptr : &*it;
}

const NativeScene::Entry* NativeScene::find_entry(NativeObjectId id) const noexcept
{
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [id](const Entry& entry) { return entry.object.id == id; });
    return it == entries_.end() ? nullptr : &*it;
}

bool NativeScene::entry_before(const Entry& lhs, const Entry& rhs) noexcept
{
    if (lhs.object.priority != rhs.object.priority)
        return lhs.object.priority < rhs.object.priority;
    return lhs.insertion_order < rhs.insertion_order;
}

void NativeScene::compact_dead()
{
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [](const Entry& entry) { return !entry.alive; }),
                   entries_.end());
}

} // namespace melee::native
