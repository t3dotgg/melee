#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace melee::native {

struct NativeDatHeader {
    std::uint32_t file_size = 0;
    std::uint32_t data_size = 0;
    std::uint32_t relocation_count = 0;
    std::uint32_t public_count = 0;
    std::uint32_t external_count = 0;
    std::uint8_t version[4]{};
};

struct NativeDatPublic {
    std::uint32_t data_offset = 0;
    std::string name;
};

struct NativeDatExternal {
    std::uint32_t first_reference_offset = 0xffffffffU;
    std::string name;
};

// Reader for the big-endian HSD archive (DAT) format used by Melee.  The
// original loader writes 32-bit GameCube pointers into the file buffer.  This
// reader keeps the source bytes immutable and resolves every pointer as a
// checked offset into an owned data block instead.
class NativeDatArchive final {
public:
    static NativeDatArchive parse(std::span<const std::byte> blob);

    const NativeDatHeader& header() const noexcept { return header_; }
    std::span<const std::byte> blob() const noexcept { return blob_; }
    std::span<const std::byte> data() const noexcept
    {
        return blob_.empty() ? std::span<const std::byte>{}
                             : std::span<const std::byte>(blob_).subspan(
                                   0x20, header_.data_size);
    }
    const std::vector<std::uint32_t>& relocation_offsets() const noexcept
    {
        return relocation_offsets_;
    }
    const std::vector<NativeDatPublic>& public_entries() const noexcept
    {
        return public_entries_;
    }
    const std::vector<NativeDatExternal>& external_entries() const noexcept
    {
        return external_entries_;
    }

    const NativeDatPublic* find_public(std::string_view name) const noexcept;
    const NativeDatExternal* find_external(std::string_view name) const noexcept;

    // Return the encoded target at a relocation slot as a data-relative
    // offset.  The returned offset may equal data().size() (one-past-end), as
    // observed in valid SdIntro archives.
    std::optional<std::size_t> relocation_target(std::size_t index) const noexcept;

    // Return all data-relative slots in an external reference chain.  The
    // chain is read-only; unlike HSD_ArchiveLocateExtern, no bytes are patched.
    std::vector<std::size_t> external_reference_offsets(
        std::string_view name) const;

    // Return a checked view into the owned data block.  This is the native
    // replacement for adding archive->data to a GameCube offset.
    std::span<const std::byte> data_at(std::size_t offset,
                                       std::size_t size = 1) const;

private:
    NativeDatHeader header_{};
    std::vector<std::byte> blob_;
    std::vector<std::uint32_t> relocation_offsets_;
    std::vector<NativeDatPublic> public_entries_;
    std::vector<NativeDatExternal> external_entries_;
};

} // namespace melee::native
