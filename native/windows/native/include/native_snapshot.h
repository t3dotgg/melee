#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace melee::native {

// A host-native save state. All values are serialized explicitly; the payload
// is an owned byte vector and never contains process pointers or guest offsets.
enum class SnapshotPayloadType : std::uint16_t {
    GameState = 1,
};

struct NativeSnapshot {
    static constexpr std::uint16_t kCurrentVersion = 1;

    std::uint16_t version = kCurrentVersion;
    SnapshotPayloadType payload_type = SnapshotPayloadType::GameState;
    std::uint16_t flags = 0;
    std::uint64_t simulation_frame = 0;
    std::uint64_t rng_state = 0;
    std::vector<std::byte> payload;

    std::vector<std::byte> serialize() const;
    static NativeSnapshot deserialize(std::span<const std::byte> bytes);
};

// Standard CRC-32 (IEEE 802.3), exposed for diagnostics and fixture tests.
std::uint32_t snapshot_crc32(std::span<const std::byte> bytes) noexcept;

// Free-function spellings are convenient for systems that keep snapshots in
// generic containers.
std::vector<std::byte> serialize_snapshot(const NativeSnapshot& snapshot);
NativeSnapshot deserialize_snapshot(std::span<const std::byte> bytes);

} // namespace melee::native
