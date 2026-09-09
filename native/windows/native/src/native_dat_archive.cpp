#include "native_dat_archive.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace melee::native {
namespace {
constexpr std::size_t kHeaderSize = 0x20;
constexpr std::uint32_t kChainEnd = 0xffffffffU;

void require_range(std::size_t blob_size, std::size_t offset, std::size_t size,
                   const char* what)
{
    if (offset > blob_size || size > blob_size - offset) {
        throw std::invalid_argument(std::string("DAT ") + what +
                                    " extends outside file");
    }
}

std::uint8_t byte_at(std::span<const std::byte> blob, std::size_t offset)
{
    return std::to_integer<std::uint8_t>(blob[offset]);
}

std::uint32_t be32(std::span<const std::byte> blob, std::size_t offset)
{
    require_range(blob.size(), offset, sizeof(std::uint32_t), "word");
    return (static_cast<std::uint32_t>(byte_at(blob, offset)) << 24) |
           (static_cast<std::uint32_t>(byte_at(blob, offset + 1)) << 16) |
           (static_cast<std::uint32_t>(byte_at(blob, offset + 2)) << 8) |
           static_cast<std::uint32_t>(byte_at(blob, offset + 3));
}

std::string read_symbol(std::span<const std::byte> blob, std::size_t symbols_offset,
                        std::size_t symbols_size, std::uint32_t symbol_offset)
{
    if (symbol_offset >= symbols_size) {
        throw std::invalid_argument("DAT symbol offset is outside string table");
    }
    const auto begin = blob.begin() + static_cast<std::ptrdiff_t>(symbols_offset + symbol_offset);
    const auto end = blob.begin() + static_cast<std::ptrdiff_t>(symbols_offset + symbols_size);
    const auto nul = std::find(begin, end, std::byte{0});
    if (nul == end) {
        throw std::invalid_argument("DAT symbol is not NUL terminated");
    }
    if (nul == begin) {
        throw std::invalid_argument("DAT symbol name is empty");
    }
    return {reinterpret_cast<const char*>(&*begin),
            static_cast<std::size_t>(nul - begin)};
}

void validate_data_offset(std::uint32_t offset, std::size_t data_size,
                          bool allow_end, const char* what)
{
    if (!allow_end && data_size == 0) {
        throw std::invalid_argument(std::string("DAT ") + what +
                                    " is outside data block");
    }
    const std::size_t limit = allow_end ? data_size : data_size - 1;
    if (static_cast<std::size_t>(offset) > limit) {
        throw std::invalid_argument(std::string("DAT ") + what +
                                    " is outside data block");
    }
}
} // namespace

NativeDatArchive NativeDatArchive::parse(std::span<const std::byte> input)
{
    require_range(input.size(), 0, kHeaderSize, "header");
    const auto file_size = be32(input, 0);
    if (file_size != input.size()) {
        throw std::invalid_argument("DAT file_size does not match supplied bytes");
    }

    NativeDatArchive archive;
    archive.header_.file_size = file_size;
    archive.header_.data_size = be32(input, 4);
    archive.header_.relocation_count = be32(input, 8);
    archive.header_.public_count = be32(input, 12);
    archive.header_.external_count = be32(input, 16);
    for (std::size_t i = 0; i < 4; ++i) {
        archive.header_.version[i] = byte_at(input, 20 + i);
    }

    std::size_t offset = kHeaderSize;
    require_range(input.size(), offset, archive.header_.data_size, "data block");
    offset += archive.header_.data_size;
    const auto checked_table_bytes = [&](std::uint32_t count, std::size_t entry_size,
                                         const char* what) {
        if (static_cast<std::size_t>(count) >
            (std::numeric_limits<std::size_t>::max() - offset) / entry_size) {
            throw std::invalid_argument(std::string("DAT ") + what + " table overflows");
        }
        const auto bytes = static_cast<std::size_t>(count) * entry_size;
        require_range(input.size(), offset, bytes, what);
        offset += bytes;
    };

    const std::size_t relocation_table_offset = offset;
    checked_table_bytes(archive.header_.relocation_count, 4, "relocation");
    const std::size_t public_table_offset = offset;
    checked_table_bytes(archive.header_.public_count, 8, "public");
    const std::size_t external_table_offset = offset;
    checked_table_bytes(archive.header_.external_count, 8, "external");
    const std::size_t symbols_offset = offset;
    const std::size_t symbols_size = input.size() - symbols_offset;

    archive.blob_.assign(input.begin(), input.end());

    archive.relocation_offsets_.reserve(archive.header_.relocation_count);
    for (std::uint32_t i = 0; i < archive.header_.relocation_count; ++i) {
        const auto slot = be32(input, relocation_table_offset + static_cast<std::size_t>(i) * 4);
        // Unaligned slots occur in valid TyMnInfo.dat, so range validation is
        // deliberately byte-based rather than imposing host alignment.
        require_range(archive.header_.data_size, slot, 4, "relocation slot");
        validate_data_offset(be32(archive.data(), slot), archive.header_.data_size,
                             true, "relocation target");
        archive.relocation_offsets_.push_back(slot);
    }

    archive.public_entries_.reserve(archive.header_.public_count);
    for (std::uint32_t i = 0; i < archive.header_.public_count; ++i) {
        const auto p = public_table_offset + static_cast<std::size_t>(i) * 8;
        const auto data_target = be32(input, p);
        validate_data_offset(data_target, archive.header_.data_size, false,
                             "public offset");
        auto name = read_symbol(input, symbols_offset, symbols_size, be32(input, p + 4));
        if (archive.find_public(name) != nullptr) {
            throw std::invalid_argument("DAT public symbol is duplicated");
        }
        archive.public_entries_.push_back({data_target, std::move(name)});
    }

    archive.external_entries_.reserve(archive.header_.external_count);
    for (std::uint32_t i = 0; i < archive.header_.external_count; ++i) {
        const auto p = external_table_offset + static_cast<std::size_t>(i) * 8;
        const auto head = be32(input, p);
        if (head != kChainEnd) {
            require_range(archive.header_.data_size, head, 4, "external head");
        }
        auto name = read_symbol(input, symbols_offset, symbols_size, be32(input, p + 4));
        if (archive.find_external(name) != nullptr) {
            throw std::invalid_argument("DAT external symbol is duplicated");
        }
        archive.external_entries_.push_back({head, std::move(name)});
    }

    // Walk every chain once during parsing. This validates each link and
    // rejects cycles before callers can accidentally loop forever.
    for (const auto& external : archive.external_entries_) {
        std::vector<std::size_t> seen;
        auto current = external.first_reference_offset;
        while (current != kChainEnd) {
            require_range(archive.header_.data_size, current, 4, "external reference");
            if (std::find(seen.begin(), seen.end(), current) != seen.end()) {
                throw std::invalid_argument("DAT external reference chain contains a cycle");
            }
            seen.push_back(current);
            current = be32(archive.data(), current);
        }
    }

    return archive;
}

const NativeDatPublic* NativeDatArchive::find_public(std::string_view name) const noexcept
{
    const auto it = std::find_if(public_entries_.begin(), public_entries_.end(),
        [name](const NativeDatPublic& entry) { return entry.name == name; });
    return it == public_entries_.end() ? nullptr : &*it;
}

const NativeDatExternal* NativeDatArchive::find_external(std::string_view name) const noexcept
{
    const auto it = std::find_if(external_entries_.begin(), external_entries_.end(),
        [name](const NativeDatExternal& entry) { return entry.name == name; });
    return it == external_entries_.end() ? nullptr : &*it;
}

std::optional<std::size_t> NativeDatArchive::relocation_target(std::size_t index) const noexcept
{
    if (index >= relocation_offsets_.size()) return std::nullopt;
    const auto target = static_cast<std::size_t>(be32(data(), relocation_offsets_[index]));
    return target <= data().size() ? std::optional<std::size_t>(target) : std::nullopt;
}

std::vector<std::size_t> NativeDatArchive::external_reference_offsets(
    std::string_view name) const
{
    const auto* external = find_external(name);
    if (external == nullptr) return {};
    std::vector<std::size_t> references;
    auto current = external->first_reference_offset;
    while (current != kChainEnd) {
        references.push_back(current);
        current = be32(data(), current);
    }
    return references;
}

std::span<const std::byte> NativeDatArchive::data_at(std::size_t offset,
                                                      std::size_t size) const
{
    const auto block = data();
    if (offset > block.size() || size > block.size() - offset) {
        throw std::out_of_range("DAT data range is outside data block");
    }
    return block.subspan(offset, size);
}

} // namespace melee::native
