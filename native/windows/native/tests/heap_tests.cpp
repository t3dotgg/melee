#include "native_heap.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
struct Tracked {
    static inline std::vector<int> destroyed;
    int id;
    explicit Tracked(int value) : id(value) {}
    ~Tracked() { destroyed.push_back(id); }
};
}

int main()
{
    using namespace melee::native;
    static_assert(std::is_trivially_copyable_v<NativeHandle>);
    static_assert(!std::is_pointer_v<NativeHandle>);

    NativeArena arena(NativeArenaKind::Scene, 128, "test");
    const NativeHandle bytes = arena.allocate(17, 8);
    assert(bytes.valid());
    assert(arena.resolve(bytes) != nullptr);
    auto span = arena.resolve_bytes(bytes);
    assert(span.size() == 17);
    span[0] = std::byte{0x5a};
    assert(std::to_integer<int>(arena.resolve_bytes(bytes)[0]) == 0x5a);
    assert(arena.stats().used >= 17);

    const NativeHandle foreign{bytes.slot, bytes.generation, NativeArenaKind::Audio};
    assert(arena.resolve(foreign) == nullptr);
    bool threw = false;
    try {
        (void)arena.resolve_bytes(foreign);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    Tracked::destroyed.clear();
    NativeHandle first = arena.make<Tracked>(1);
    NativeHandle second = arena.make<Tracked>(2);
    assert(arena.resolve(first) != nullptr);
    arena.reset();
    assert((Tracked::destroyed == std::vector<int>{2, 1}));
    assert(arena.resolve(first) == nullptr);
    assert(arena.resolve(second) == nullptr);
    const NativeHandle reused = arena.make<Tracked>(3);
    assert(reused.generation != first.generation);
    arena.reset();

    bool bad_alignment = false;
    try {
        (void)arena.allocate(1, 3);
    } catch (const std::invalid_argument&) {
        bad_alignment = true;
    }
    assert(bad_alignment);

    NativeArena tiny(NativeArenaKind::Frame, 8);
    (void)tiny.allocate(8);
    bool exhausted = false;
    try {
        (void)tiny.allocate(1);
    } catch (const std::bad_alloc&) {
        exhausted = true;
    }
    assert(exhausted);

    NativeHeap heap(64, 64, 64, 64);
    assert(&heap.arena(NativeArenaKind::Persistent) != &heap.arena(NativeArenaKind::Scene));
    const NativeHandle persistent = heap.arena(NativeArenaKind::Persistent).allocate(8);
    const NativeHandle scene = heap.arena(NativeArenaKind::Scene).allocate(8);
    assert(heap.arena(NativeArenaKind::Persistent).resolve(persistent) != nullptr);
    assert(heap.arena(NativeArenaKind::Scene).resolve(scene) != nullptr);
    heap.reset_scene();
    assert(heap.arena(NativeArenaKind::Scene).resolve(scene) == nullptr);
    assert(heap.arena(NativeArenaKind::Persistent).resolve(persistent) != nullptr);

    std::cout << "native heap tests passed\n";
}
