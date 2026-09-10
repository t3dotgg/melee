#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace melee::native {

struct ArchiveEntry {
    std::string name;
    std::size_t offset = 0;
    std::size_t size = 0;
};

// A deliberately small container used by the source port. The wire format is
// big endian so existing DAT fixtures can be imported without exposing guest
// pointers to native code:
//   header: 'MARC', u16 version (1), u16 entry count
//   entry:  u32 data offset, u32 data size, u32 name offset, u16 name length
// Names and payloads are byte ranges in the same immutable blob.
class NativeArchive final {
public:
    static NativeArchive parse(std::span<const std::byte> blob);

    const ArchiveEntry* find(std::string_view name) const noexcept;
    std::span<const std::byte> data(const ArchiveEntry& entry) const;
    std::span<const std::byte> blob() const noexcept { return blob_; }
    const std::vector<ArchiveEntry>& entries() const noexcept { return entries_; }

private:
    std::span<const std::byte> blob_{};
    std::vector<ArchiveEntry> entries_;
};

} // namespace melee::native
