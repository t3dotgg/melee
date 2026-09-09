#include "native_dat_archive.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void put32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value)
{
    bytes[offset] = static_cast<std::byte>(value >> 24);
    bytes[offset + 1] = static_cast<std::byte>(value >> 16);
    bytes[offset + 2] = static_cast<std::byte>(value >> 8);
    bytes[offset + 3] = static_cast<std::byte>(value);
}

std::vector<std::byte> fixture()
{
    // Header + 12-byte data + one relocation + one public + one external +
    // two NUL-terminated names.
    std::vector<std::byte> bytes(72, std::byte{0});
    put32(bytes, 0, 72);
    put32(bytes, 4, 12);
    put32(bytes, 8, 1);
    put32(bytes, 12, 1);
    put32(bytes, 16, 1);
    put32(bytes, 32, 8); // relocation slot points at data offset 8.
    put32(bytes, 36, 0xffffffffU); // external chain terminator.
    put32(bytes, 44, 0); // relocation table: slot at data offset 0.
    put32(bytes, 48, 8); // public table: object at data offset 8.
    put32(bytes, 52, 0); // symbol "pub".
    put32(bytes, 56, 4); // external table: chain starts at data offset 4.
    put32(bytes, 60, 4); // symbol "ext".
    bytes[64] = std::byte{'p'}; bytes[65] = std::byte{'u'};
    bytes[66] = std::byte{'b'}; bytes[67] = std::byte{0};
    bytes[68] = std::byte{'e'}; bytes[69] = std::byte{'x'};
    bytes[70] = std::byte{'t'}; bytes[71] = std::byte{0};
    return bytes;
}

template <typename Function>
void expect_invalid(Function&& function)
{
    bool rejected = false;
    try { function(); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}
} // namespace

int main()
{
    using namespace melee::native;
    auto bytes = fixture();
    const auto original = bytes;
    const auto archive = NativeDatArchive::parse(bytes);
    assert(archive.header().file_size == bytes.size());
    assert(archive.header().data_size == 12);
    assert(archive.data().size() == 12);
    assert(archive.relocation_offsets().size() == 1);
    assert(archive.relocation_offsets()[0] == 0);
    assert(archive.relocation_target(0).value() == 8);
    assert(!archive.relocation_target(1).has_value());
    assert(archive.find_public("pub") != nullptr);
    assert(archive.find_public("pub")->data_offset == 8);
    assert(archive.find_external("ext") != nullptr);
    const auto references = archive.external_reference_offsets("ext");
    assert(references.size() == 1 && references[0] == 4);
    assert(archive.external_reference_offsets("missing").empty());
    assert(archive.data_at(8, 4).size() == 4);
    // The source remains untouched and the parsed archive owns its copy.
    assert(bytes == original);
    bytes[32] = std::byte{0xff};
    assert(std::to_integer<unsigned>(archive.data()[0]) == 0);

    auto bad = fixture();
    put32(bad, 0, 71);
    expect_invalid([&] { (void)NativeDatArchive::parse(bad); });
    bad = fixture();
    put32(bad, 44, 12); // relocation slot starts at the end of data.
    expect_invalid([&] { (void)NativeDatArchive::parse(bad); });
    bad = fixture();
    put32(bad, 48, 12); // public object must start inside data.
    expect_invalid([&] { (void)NativeDatArchive::parse(bad); });
    bad = fixture();
    put32(bad, 52, 99); // symbol offset outside the string table.
    expect_invalid([&] { (void)NativeDatArchive::parse(bad); });
    bad = fixture();
    bad[67] = std::byte{'x'}; // remove the public name terminator.
    expect_invalid([&] { (void)NativeDatArchive::parse(bad); });
    bad = fixture();
    put32(bad, 36, 4); // external chain loops back to itself.
    expect_invalid([&] { (void)NativeDatArchive::parse(bad); });
    bad = fixture();
    put32(bad, 36, 12); // external chain points past the data block.
    expect_invalid([&] { (void)NativeDatArchive::parse(bad); });

    if (const char* fixture_path = std::getenv("MELEE_DAT_FIXTURE");
        fixture_path != nullptr && fixture_path[0] != '\0') {
        std::cout << "native DAT fixture validation is provided by the parser; "
                  << "set MELEE_DAT_FIXTURE=" << fixture_path << " in a caller\n";
    }
    std::cout << "native DAT archive tests passed\n";
}
