#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace melee::native {

// Host-native storage for game state. Values are exposed as typed fields; no
// PowerPC registers, guest addresses, or generated dispatch are involved.
class NativeGameMemory {
public:
    explicit NativeGameMemory(std::size_t size);

    std::size_t size() const noexcept { return bytes_.size(); }
    std::span<std::byte> bytes() noexcept { return bytes_; }
    std::span<const std::byte> bytes() const noexcept { return bytes_; }

    std::uint8_t read_u8(std::size_t offset) const;
    std::uint16_t read_be_u16(std::size_t offset) const;
    std::uint32_t read_be_u32(std::size_t offset) const;
    void write_u8(std::size_t offset, std::uint8_t value);
    void write_be_u16(std::size_t offset, std::uint16_t value);
    void write_be_u32(std::size_t offset, std::uint32_t value);

private:
    void check_range(std::size_t offset, std::size_t width) const;
    std::vector<std::byte> bytes_;
};

} // namespace melee::native
