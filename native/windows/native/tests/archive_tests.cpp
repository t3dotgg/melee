#include "native_archive.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void put16(std::vector<std::byte>& b, std::size_t p, std::uint16_t v)
{
    b[p] = static_cast<std::byte>(v >> 8);
    b[p + 1] = static_cast<std::byte>(v);
}
void put32(std::vector<std::byte>& b, std::size_t p, std::uint32_t v)
{
    b[p] = static_cast<std::byte>(v >> 24);
    b[p + 1] = static_cast<std::byte>(v >> 16);
    b[p + 2] = static_cast<std::byte>(v >> 8);
    b[p + 3] = static_cast<std::byte>(v);
}
} // namespace

int main()
{
    using namespace melee::native;
    std::vector<std::byte> blob(64);
    blob[0] = std::byte{'M'}; blob[1] = std::byte{'A'};
    blob[2] = std::byte{'R'}; blob[3] = std::byte{'C'};
    put16(blob, 4, 1); put16(blob, 6, 2);
    // Two records at 8 and 22. Names begin at 40; payloads at 48.
    put32(blob, 8, 48); put32(blob, 12, 3); put32(blob, 16, 40); put16(blob, 20, 4);
    put32(blob, 22, 51); put32(blob, 26, 2); put32(blob, 30, 44); put16(blob, 34, 4);
    const char names[] = "bootmenu";
    for (std::size_t i = 0; i < sizeof(names) - 1; ++i) blob[40 + i] = static_cast<std::byte>(names[i]);
    blob[48] = std::byte{1}; blob[49] = std::byte{2}; blob[50] = std::byte{3};
    blob[51] = std::byte{9}; blob[52] = std::byte{8};

    const auto archive = NativeArchive::parse(blob);
    assert(archive.entries().size() == 2);
    const auto* boot = archive.find("boot");
    assert(boot != nullptr && archive.data(*boot).size() == 3);
    assert(std::to_integer<unsigned>(archive.data(*boot)[1]) == 2);
    assert(archive.find("missing") == nullptr);

    bool rejected = false;
    try { (void)NativeArchive::parse(std::span<const std::byte>(blob.data(), 7)); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    blob[0] = std::byte{'X'};
    rejected = false;
    try { (void)NativeArchive::parse(blob); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::cout << "native archive tests passed\n";
}
