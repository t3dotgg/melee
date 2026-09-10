#include "native_archive.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace melee::native {
namespace {
std::uint8_t byte_at(std::span<const std::byte> b, std::size_t p)
{
    return std::to_integer<std::uint8_t>(b[p]);
}

std::uint16_t be16(std::span<const std::byte> b, std::size_t p)
{
    return static_cast<std::uint16_t>((byte_at(b, p) << 8) | byte_at(b, p + 1));
}

std::uint32_t be32(std::span<const std::byte> b, std::size_t p)
{
    return (static_cast<std::uint32_t>(byte_at(b, p)) << 24) |
           (static_cast<std::uint32_t>(byte_at(b, p + 1)) << 16) |
           (static_cast<std::uint32_t>(byte_at(b, p + 2)) << 8) |
           byte_at(b, p + 3);
}

void require_range(std::span<const std::byte> b, std::size_t p, std::size_t n)
{
    if (p > b.size() || n > b.size() - p) {
        throw std::invalid_argument("archive range outside blob");
    }
}
} // namespace

NativeArchive NativeArchive::parse(std::span<const std::byte> blob)
{
    require_range(blob, 0, 8);
    if (byte_at(blob, 0) != 'M' || byte_at(blob, 1) != 'A' ||
        byte_at(blob, 2) != 'R' || byte_at(blob, 3) != 'C') {
        throw std::invalid_argument("archive magic is not MARC");
    }
    if (be16(blob, 4) != 1) {
        throw std::invalid_argument("unsupported archive version");
    }
    const std::size_t count = be16(blob, 6);
    constexpr std::size_t record_size = 14;
    if (count > (std::numeric_limits<std::size_t>::max() - 8) / record_size) {
        throw std::invalid_argument("archive entry count overflows");
    }
    require_range(blob, 8, count * record_size);

    NativeArchive archive;
    archive.blob_ = blob;
    archive.entries_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t p = 8 + i * record_size;
        const std::size_t data_offset = be32(blob, p);
        const std::size_t data_size = be32(blob, p + 4);
        const std::size_t name_offset = be32(blob, p + 8);
        const std::size_t name_size = be16(blob, p + 12);
        require_range(blob, data_offset, data_size);
        require_range(blob, name_offset, name_size);
        std::string name;
        name.reserve(name_size);
        for (std::size_t n = 0; n < name_size; ++n) {
            const auto c = byte_at(blob, name_offset + n);
            if (c == 0) {
                throw std::invalid_argument("archive name contains NUL");
            }
            name.push_back(static_cast<char>(c));
        }
        if (name.empty() || archive.find(name) != nullptr) {
            throw std::invalid_argument("archive contains empty or duplicate name");
        }
        archive.entries_.push_back({std::move(name), data_offset, data_size});
    }
    return archive;
}

const ArchiveEntry* NativeArchive::find(std::string_view name) const noexcept
{
    const auto it = std::find_if(entries_.begin(), entries_.end(),
        [name](const ArchiveEntry& entry) { return entry.name == name; });
    return it == entries_.end() ? nullptr : &*it;
}

std::span<const std::byte> NativeArchive::data(const ArchiveEntry& entry) const
{
    if (entry.offset > blob_.size() || entry.size > blob_.size() - entry.offset) {
        throw std::invalid_argument("archive entry does not belong to blob");
    }
    return blob_.subspan(entry.offset, entry.size);
}
} // namespace melee::native
