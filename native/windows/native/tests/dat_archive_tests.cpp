#include "native_dat_archive.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
void expect_invalid(const char* label, Function&& function)
{
    bool rejected = false;
    try { function(); }
    catch (const std::invalid_argument&) { rejected = true; }
    if (!rejected) std::cerr << "not rejected: " << label << "\n";
    assert(rejected);
}
} // namespace

int main(int argc, char** argv)
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
    const auto copied = archive;
    assert(copied.data().data() == copied.blob().data() + 0x20);
    assert(copied.data().data() != archive.data().data());
    assert(copied.relocation_target(0).value() == 8);

    auto bad = fixture();
    put32(bad, 0, 71);
    expect_invalid("file size", [&] { (void)NativeDatArchive::parse(bad); });
    bad = fixture();
    put32(bad, 44, 12); // relocation slot starts at the end of data.
    expect_invalid("relocation slot", [&] { (void)NativeDatArchive::parse(bad); });
    bad = fixture();
    put32(bad, 48, 12); // public object must start inside data.
    expect_invalid("public offset", [&] { (void)NativeDatArchive::parse(bad); });
    bad = fixture();
    put32(bad, 52, 99); // symbol offset outside the string table.
    expect_invalid("symbol offset", [&] { (void)NativeDatArchive::parse(bad); });
    bad = fixture();
    bad[67] = std::byte{'x'};
    bad[71] = std::byte{'x'}; // remove all possible symbol terminators.
    expect_invalid("symbol terminator", [&] { (void)NativeDatArchive::parse(bad); });
    bad = fixture();
    put32(bad, 36, 4); // external chain loops back to itself.
    expect_invalid("chain cycle", [&] { (void)NativeDatArchive::parse(bad); });
    bad = fixture();
    put32(bad, 36, 12); // external chain points past the data block.
    expect_invalid("chain range", [&] { (void)NativeDatArchive::parse(bad); });

    bad = fixture();
    put32(bad, 32, 13);
    expect_invalid("relocation target", [&] { (void)NativeDatArchive::parse(bad); });
    bad = fixture();
    put32(bad, 32, 12); // One-past-end is a valid empty-range target.
    const auto one_past = NativeDatArchive::parse(bad);
    assert(one_past.relocation_target(0).value() == 12);
    assert(one_past.data_at(12, 0).empty());
    bad = fixture();
    put32(bad, 8, 0xffffffffU); // table count overrun.
    expect_invalid("table count", [&] { (void)NativeDatArchive::parse(bad); });

    // Optional local compatibility check; no original game data is checked in.
    for (int i = 1; i < argc; ++i) {
        std::ifstream file(argv[i], std::ios::binary);
        if (!file) throw std::runtime_error("cannot open local DAT fixture");
        const std::vector<char> chars{std::istreambuf_iterator<char>(file), {}};
        std::vector<std::byte> raw(chars.size());
        std::memcpy(raw.data(), chars.data(), chars.size());
        const auto local = NativeDatArchive::parse(raw);
        std::size_t external_references = 0;
        for (const auto& entry : local.external_entries()) {
            external_references += local.external_reference_offsets(entry.name).size();
        }
        std::cout << std::filesystem::path(argv[i]).filename().string()
                  << ": data=" << local.data().size()
                  << " relocations=" << local.relocation_offsets().size()
                  << " public=" << local.public_entries().size()
                  << " external=" << local.external_entries().size()
                  << " external_references=" << external_references << '\n';
    }

    if (const char* fixture_path = std::getenv("MELEE_DAT_FIXTURE");
        fixture_path != nullptr && fixture_path[0] != '\0') {
        std::cout << "native DAT fixture validation is provided by the parser; "
                  << "set MELEE_DAT_FIXTURE=" << fixture_path << " in a caller\n";
    }
    std::cout << "native DAT archive tests passed\n";
}
