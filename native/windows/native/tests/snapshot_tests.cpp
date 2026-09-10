#include "native_snapshot.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
bool rejects(std::span<const std::byte> bytes)
{
    try {
        (void)melee::native::deserialize_snapshot(bytes);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}
} // namespace

int main()
{
    using namespace melee::native;
    NativeSnapshot original;
    original.flags = 3;
    original.simulation_frame = 123456789;
    original.rng_state = 0x0123456789ABCDEFULL;
    original.payload = {std::byte{0}, std::byte{0x7f}, std::byte{0xa5}, std::byte{0xff}};

    const auto encoded = original.serialize();
    assert(encoded.size() == 36 + original.payload.size());
    const auto decoded = deserialize_snapshot(encoded);
    assert(decoded.version == original.version);
    assert(decoded.payload_type == original.payload_type);
    assert(decoded.flags == original.flags);
    assert(decoded.simulation_frame == original.simulation_frame);
    assert(decoded.rng_state == original.rng_state);
    assert(decoded.payload == original.payload);
    assert(serialize_snapshot(decoded) == encoded);

    auto corrupted = encoded;
    corrupted.back() ^= std::byte{1};
    assert(rejects(corrupted));
    corrupted = encoded;
    corrupted[4] = std::byte{2};
    assert(rejects(corrupted));
    corrupted = encoded;
    corrupted[28] = std::byte{0};
    corrupted[29] = std::byte{0};
    corrupted[30] = std::byte{0};
    corrupted[31] = std::byte{0};
    assert(rejects(corrupted));
    assert(rejects(std::span<const std::byte>(encoded.data(), encoded.size() - 1)));

    NativeSnapshot unsupported = original;
    unsupported.version = 2;
    bool threw = false;
    try {
        (void)unsupported.serialize();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    assert(snapshot_crc32(std::span<const std::byte>{}) == 0);
    std::cout << "native snapshot tests passed\n";
}
