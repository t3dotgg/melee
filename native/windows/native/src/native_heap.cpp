#include "native_heap.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace melee::native {
namespace {
constexpr std::size_t align_up(std::size_t value, std::size_t alignment)
{
    return (value + (alignment - 1)) & ~(alignment - 1);
}
}

NativeArena::NativeArena(NativeArenaKind kind, std::size_t capacity, std::string name)
    : kind_(kind), name_(std::move(name)), storage_(capacity)
{
    if (capacity == 0) {
        throw std::invalid_argument("NativeArena capacity must be non-zero");
    }
}

NativeArena::~NativeArena()
{
    reset();
}

NativeArenaStats NativeArena::stats() const noexcept
{
    return NativeArenaStats{storage_.size(), cursor_, allocations_.size(), epoch_};
}

std::size_t NativeArena::checked_offset(std::size_t size, std::size_t alignment) const
{
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        alignment > alignof(std::max_align_t)) {
        throw std::invalid_argument("NativeArena alignment must be a power of two <= max_align_t");
    }
    if (size > storage_.size()) {
        throw std::bad_alloc();
    }
    const std::size_t aligned = align_up(cursor_, alignment);
    if (aligned < cursor_ || size > storage_.size() - aligned) {
        throw std::bad_alloc();
    }
    return aligned;
}

NativeHandle NativeArena::allocate(std::size_t size, std::size_t alignment)
{
    if (size == 0) {
        throw std::invalid_argument("NativeArena cannot allocate zero bytes");
    }
    const std::size_t offset = checked_offset(size, alignment);
    if (allocations_.size() >= UINT32_MAX) {
        throw std::bad_alloc();
    }
    const std::uint32_t slot = static_cast<std::uint32_t>(allocations_.size());
    // Mix the arena epoch and slot into a monotonically changing identity. A
    // zero value is reserved for invalid handles.
    const std::uint64_t generation = (epoch_ << 32) ^ (static_cast<std::uint64_t>(slot) + 1);
    allocations_.push_back(Allocation{offset, size, generation, nullptr, true});
    cursor_ = offset + size;
    return NativeHandle{slot, generation, kind_};
}

void NativeArena::set_destructor(NativeHandle handle, void (*destroy)(void*) noexcept)
{
    if (handle.arena != kind_ || handle.slot >= allocations_.size()) {
        throw std::invalid_argument("NativeArena destructor handle is foreign");
    }
    Allocation& allocation = allocations_[handle.slot];
    if (!allocation.active || allocation.generation != handle.generation) {
        throw std::invalid_argument("NativeArena destructor handle is stale");
    }
    allocation.destroy = destroy;
}

void NativeArena::abandon(NativeHandle handle) noexcept
{
    if (handle.arena != kind_ || handle.slot >= allocations_.size()) {
        return;
    }
    Allocation& allocation = allocations_[handle.slot];
    if (allocation.active && allocation.generation == handle.generation) {
        allocation.active = false;
        allocation.destroy = nullptr;
    }
}

void NativeArena::reset() noexcept
{
    for (auto it = allocations_.rbegin(); it != allocations_.rend(); ++it) {
        if (it->active && it->destroy != nullptr) {
            it->destroy(storage_.data() + it->offset);
        }
        it->active = false;
        it->destroy = nullptr;
    }
    allocations_.clear();
    cursor_ = 0;
    ++epoch_;
    if (epoch_ == 0) {
        epoch_ = 1;
    }
}

void* NativeArena::resolve(NativeHandle handle) noexcept
{
    if (handle.arena != kind_ || !handle.valid() || handle.slot >= allocations_.size()) {
        return nullptr;
    }
    Allocation& allocation = allocations_[handle.slot];
    if (!allocation.active || allocation.generation != handle.generation) {
        return nullptr;
    }
    return storage_.data() + allocation.offset;
}

const void* NativeArena::resolve(NativeHandle handle) const noexcept
{
    if (handle.arena != kind_ || !handle.valid() || handle.slot >= allocations_.size()) {
        return nullptr;
    }
    const Allocation& allocation = allocations_[handle.slot];
    if (!allocation.active || allocation.generation != handle.generation) {
        return nullptr;
    }
    return storage_.data() + allocation.offset;
}

std::span<std::byte> NativeArena::resolve_bytes(NativeHandle handle)
{
    if (handle.arena != kind_ || !handle.valid() || handle.slot >= allocations_.size()) {
        throw std::invalid_argument("NativeArena byte handle is invalid");
    }
    Allocation& allocation = allocations_[handle.slot];
    if (!allocation.active || allocation.generation != handle.generation) {
        throw std::invalid_argument("NativeArena byte handle is stale");
    }
    return std::span<std::byte>(storage_.data() + allocation.offset, allocation.size);
}

std::span<const std::byte> NativeArena::resolve_bytes(NativeHandle handle) const
{
    if (handle.arena != kind_ || !handle.valid() || handle.slot >= allocations_.size()) {
        throw std::invalid_argument("NativeArena byte handle is invalid");
    }
    const Allocation& allocation = allocations_[handle.slot];
    if (!allocation.active || allocation.generation != handle.generation) {
        throw std::invalid_argument("NativeArena byte handle is stale");
    }
    return std::span<const std::byte>(storage_.data() + allocation.offset, allocation.size);
}

NativeHeap::NativeHeap(std::size_t persistent_capacity, std::size_t scene_capacity,
                       std::size_t frame_capacity, std::size_t audio_capacity)
    : persistent_(NativeArenaKind::Persistent, persistent_capacity, "persistent"),
      scene_(NativeArenaKind::Scene, scene_capacity, "scene"),
      frame_(NativeArenaKind::Frame, frame_capacity, "frame"),
      audio_(NativeArenaKind::Audio, audio_capacity, "audio")
{
}

NativeArena& NativeHeap::arena(NativeArenaKind kind) noexcept
{
    switch (kind) {
    case NativeArenaKind::Persistent: return persistent_;
    case NativeArenaKind::Scene: return scene_;
    case NativeArenaKind::Frame: return frame_;
    case NativeArenaKind::Audio: return audio_;
    }
    return persistent_;
}

const NativeArena& NativeHeap::arena(NativeArenaKind kind) const noexcept
{
    switch (kind) {
    case NativeArenaKind::Persistent: return persistent_;
    case NativeArenaKind::Scene: return scene_;
    case NativeArenaKind::Frame: return frame_;
    case NativeArenaKind::Audio: return audio_;
    }
    return persistent_;
}

} // namespace melee::native
