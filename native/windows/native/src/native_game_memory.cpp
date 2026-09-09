#include "native_game_memory.h"

#include <stdexcept>

namespace melee::native {

NativeGameMemory::NativeGameMemory(std::size_t size) : bytes_(size) {}

void NativeGameMemory::check_range(std::size_t offset, std::size_t width) const
{
    if (offset > bytes_.size() || width > bytes_.size() - offset) {
        throw std::out_of_range("NativeGameMemory access outside allocation");
    }
}

std::uint8_t NativeGameMemory::read_u8(std::size_t offset) const
{
    check_range(offset, 1);
    return std::to_integer<std::uint8_t>(bytes_[offset]);
}

std::uint16_t NativeGameMemory::read_be_u16(std::size_t offset) const
{
    check_range(offset, 2);
    return (static_cast<std::uint16_t>(read_u8(offset)) << 8) |
           read_u8(offset + 1);
}

std::uint32_t NativeGameMemory::read_be_u32(std::size_t offset) const
{
    check_range(offset, 4);
    return (static_cast<std::uint32_t>(read_u8(offset)) << 24) |
           (static_cast<std::uint32_t>(read_u8(offset + 1)) << 16) |
           (static_cast<std::uint32_t>(read_u8(offset + 2)) << 8) |
           read_u8(offset + 3);
}

void NativeGameMemory::write_u8(std::size_t offset, std::uint8_t value)
{
    check_range(offset, 1);
    bytes_[offset] = static_cast<std::byte>(value);
}

void NativeGameMemory::write_be_u16(std::size_t offset, std::uint16_t value)
{
    check_range(offset, 2);
    write_u8(offset, static_cast<std::uint8_t>(value >> 8));
    write_u8(offset + 1, static_cast<std::uint8_t>(value));
}

void NativeGameMemory::write_be_u32(std::size_t offset, std::uint32_t value)
{
    check_range(offset, 4);
    write_u8(offset, static_cast<std::uint8_t>(value >> 24));
    write_u8(offset + 1, static_cast<std::uint8_t>(value >> 16));
    write_u8(offset + 2, static_cast<std::uint8_t>(value >> 8));
    write_u8(offset + 3, static_cast<std::uint8_t>(value));
}

} // namespace melee::native
