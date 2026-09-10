#include "native_snapshot.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace melee::native {
namespace {
constexpr std::size_t kHeaderSize = 36;
constexpr std::size_t kCrcOffset = 32;
constexpr std::uint32_t kMaxPayloadSize = 64u * 1024u * 1024u;
constexpr std::byte kMagic[] = {std::byte{'M'}, std::byte{'S'}, std::byte{'N'}, std::byte{'P'}};

std::uint8_t byte_at(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint8_t>(bytes[offset]);
}

std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(byte_at(bytes, offset) |
                                      (static_cast<std::uint16_t>(byte_at(bytes, offset + 1)) << 8));
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(byte_at(bytes, offset)) |
           (static_cast<std::uint32_t>(byte_at(bytes, offset + 1)) << 8) |
           (static_cast<std::uint32_t>(byte_at(bytes, offset + 2)) << 16) |
           (static_cast<std::uint32_t>(byte_at(bytes, offset + 3)) << 24);
}

std::uint64_t read_u64(std::span<const std::byte> bytes, std::size_t offset)
{
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(byte_at(bytes, offset + i)) << (i * 8);
    }
    return value;
}

void write_u16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8);
}

void write_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<std::byte>(value >> (i * 8));
    }
}

void write_u64(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) {
        bytes[offset + i] = static_cast<std::byte>(value >> (i * 8));
    }
}

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::invalid_argument(message);
    }
}
} // namespace

std::uint32_t snapshot_crc32(std::span<const std::byte> bytes) noexcept
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const std::byte value : bytes) {
        crc ^= std::to_integer<std::uint8_t>(value);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

std::vector<std::byte> NativeSnapshot::serialize() const
{
    require(version == kCurrentVersion, "unsupported snapshot version");
    require(static_cast<std::uint16_t>(payload_type) != 0, "snapshot payload type is invalid");
    require(payload.size() <= kMaxPayloadSize, "snapshot payload is too large");
    require(payload.size() <= std::numeric_limits<std::uint32_t>::max(),
            "snapshot payload size overflows wire format");

    const auto payload_size = static_cast<std::uint32_t>(payload.size());
    std::vector<std::byte> bytes(kHeaderSize + payload.size());
    for (std::size_t i = 0; i < sizeof(kMagic); ++i) {
        bytes[i] = kMagic[i];
    }
    write_u16(bytes, 4, version);
    write_u16(bytes, 6, static_cast<std::uint16_t>(kHeaderSize));
    write_u16(bytes, 8, static_cast<std::uint16_t>(payload_type));
    write_u16(bytes, 10, flags);
    write_u64(bytes, 12, simulation_frame);
    write_u64(bytes, 20, rng_state);
    write_u32(bytes, 28, payload_size);
    write_u32(bytes, kCrcOffset, 0);
    std::copy(payload.begin(), payload.end(), bytes.begin() + kHeaderSize);
    write_u32(bytes, kCrcOffset, snapshot_crc32(bytes));
    return bytes;
}

NativeSnapshot NativeSnapshot::deserialize(std::span<const std::byte> bytes)
{
    require(bytes.size() >= kHeaderSize, "snapshot is truncated");
    for (std::size_t i = 0; i < sizeof(kMagic); ++i) {
        require(bytes[i] == kMagic[i], "snapshot magic is invalid");
    }
    const auto version = read_u16(bytes, 4);
    require(version == kCurrentVersion, "unsupported snapshot version");
    const auto header_size = read_u16(bytes, 6);
    require(header_size == kHeaderSize, "snapshot header size is invalid");
    const auto payload_type = read_u16(bytes, 8);
    require(payload_type != 0, "snapshot payload type is invalid");
    const auto payload_size = read_u32(bytes, 28);
    require(payload_size <= kMaxPayloadSize, "snapshot payload is too large");
    require(static_cast<std::size_t>(payload_size) == bytes.size() - kHeaderSize,
            "snapshot payload size does not match input");

    const auto expected_crc = read_u32(bytes, kCrcOffset);
    std::vector<std::byte> crc_input(bytes.begin(), bytes.end());
    write_u32(crc_input, kCrcOffset, 0);
    require(snapshot_crc32(crc_input) == expected_crc, "snapshot CRC mismatch");

    NativeSnapshot snapshot;
    snapshot.version = version;
    snapshot.payload_type = static_cast<SnapshotPayloadType>(payload_type);
    snapshot.flags = read_u16(bytes, 10);
    snapshot.simulation_frame = read_u64(bytes, 12);
    snapshot.rng_state = read_u64(bytes, 20);
    snapshot.payload.assign(bytes.begin() + kHeaderSize, bytes.end());
    return snapshot;
}

std::vector<std::byte> serialize_snapshot(const NativeSnapshot& snapshot)
{
    return snapshot.serialize();
}

NativeSnapshot deserialize_snapshot(std::span<const std::byte> bytes)
{
    return NativeSnapshot::deserialize(bytes);
}

} // namespace melee::native


