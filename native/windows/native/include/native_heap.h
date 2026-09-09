#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <new>

namespace melee::native {

// Lifetime domains used by the native runtime.  Allocations in Scene, Frame,
// and Audio arenas can be reclaimed in bulk without touching persistent data.
enum class NativeArenaKind : std::uint8_t {
    Persistent = 0,
    Scene = 1,
    Frame = 2,
    Audio = 3,
};

// Opaque, process-local allocation identity.  It contains an arena, slot, and
// generation only; it never stores a host pointer or a 32-bit guest address.
struct NativeHandle {
    std::uint32_t slot = UINT32_MAX;
    std::uint64_t generation = 0;
    NativeArenaKind arena = NativeArenaKind::Persistent;

    constexpr bool valid() const noexcept
    {
        return slot != UINT32_MAX && generation != 0;
    }
    friend constexpr bool operator==(const NativeHandle&, const NativeHandle&) = default;
};
static_assert(!std::is_pointer_v<decltype(NativeHandle::generation)>);

struct NativeArenaStats {
    std::size_t capacity = 0;
    std::size_t used = 0;
    std::size_t allocations = 0;
    std::uint64_t epoch = 1;
};

class NativeArena {
public:
    NativeArena(NativeArenaKind kind, std::size_t capacity, std::string name = {});
    ~NativeArena();

    NativeArena(const NativeArena&) = delete;
    NativeArena& operator=(const NativeArena&) = delete;
    NativeArena(NativeArena&&) = delete;
    NativeArena& operator=(NativeArena&&) = delete;

    NativeArenaKind kind() const noexcept { return kind_; }
    const std::string& name() const noexcept { return name_; }
    NativeArenaStats stats() const noexcept;

    NativeHandle allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));

    template <typename T, typename... Args>
    NativeHandle make(Args&&... args)
    {
        const NativeHandle handle = allocate(sizeof(T), alignof(T));
        try {
            void* address = resolve(handle);
            T* object = new (address) T(std::forward<Args>(args)...);
            set_destructor(handle, [](void* ptr) noexcept { static_cast<T*>(ptr)->~T(); });
            (void)object;
            return handle;
        } catch (...) {
            // A failed constructor leaves the slot inactive while retaining the
            // generation, so it cannot be accidentally resolved by a stale ID.
            abandon(handle);
            throw;
        }
    }

    void reset() noexcept;

    // Returns nullptr for an invalid, stale, or foreign handle.
    void* resolve(NativeHandle handle) noexcept;
    const void* resolve(NativeHandle handle) const noexcept;
    std::span<std::byte> resolve_bytes(NativeHandle handle);
    std::span<const std::byte> resolve_bytes(NativeHandle handle) const;

private:
    struct Allocation {
        std::size_t offset = 0;
        std::size_t size = 0;
        std::uint64_t generation = 0;
        void (*destroy)(void*) noexcept = nullptr;
        bool active = false;
    };

    void set_destructor(NativeHandle handle, void (*destroy)(void*) noexcept);
    void abandon(NativeHandle handle) noexcept;
    std::size_t checked_offset(std::size_t size, std::size_t alignment) const;

    NativeArenaKind kind_;
    std::string name_;
    std::vector<std::byte> storage_;
    std::vector<Allocation> allocations_;
    std::size_t cursor_ = 0;
    std::uint64_t epoch_ = 1;
};

class NativeHeap {
public:
    // Capacities are bytes and may be tuned independently for each lifetime
    // domain. Defaults are deliberately modest for tools/tests; game builds
    // can select larger arenas at startup.
    explicit NativeHeap(std::size_t persistent_capacity = 16 * 1024 * 1024,
                        std::size_t scene_capacity = 8 * 1024 * 1024,
                        std::size_t frame_capacity = 4 * 1024 * 1024,
                        std::size_t audio_capacity = 4 * 1024 * 1024);

    NativeArena& arena(NativeArenaKind kind) noexcept;
    const NativeArena& arena(NativeArenaKind kind) const noexcept;
    void reset_scene() noexcept { scene_.reset(); }
    void reset_frame() noexcept { frame_.reset(); }
    void reset_audio() noexcept { audio_.reset(); }

private:
    NativeArena persistent_;
    NativeArena scene_;
    NativeArena frame_;
    NativeArena audio_;
};

} // namespace melee::native
