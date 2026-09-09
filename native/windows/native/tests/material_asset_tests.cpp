#include "native_asset_store.h"
#include "native_material_asset.h"

#include <cassert>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void put16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value >> 8);
    bytes[offset + 1] = static_cast<std::byte>(value);
}

void put32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value)
{
    bytes[offset] = static_cast<std::byte>(value >> 24);
    bytes[offset + 1] = static_cast<std::byte>(value >> 16);
    bytes[offset + 2] = static_cast<std::byte>(value >> 8);
    bytes[offset + 3] = static_cast<std::byte>(value);
}

std::vector<std::byte> material_record(std::uint32_t alpha_bits = 0x3f000000,
                                       std::uint32_t shininess_bits = 0x41200000)
{
    std::vector<std::byte> record(melee::native::kHsdMaterialRecordSize);
    record[0] = std::byte{1}; record[1] = std::byte{2}; record[2] = std::byte{3}; record[3] = std::byte{4};
    record[4] = std::byte{5}; record[5] = std::byte{6}; record[6] = std::byte{7}; record[7] = std::byte{8};
    record[8] = std::byte{9}; record[9] = std::byte{10}; record[10] = std::byte{11}; record[11] = std::byte{12};
    put32(record, 12, alpha_bits);
    put32(record, 16, shininess_bits);
    return record;
}

std::vector<std::byte> archive_with_material()
{
    constexpr std::size_t data_offset = 48;
    constexpr std::size_t name_offset = 40;
    std::vector<std::byte> archive(data_offset + melee::native::kHsdMaterialRecordSize);
    archive[0] = std::byte{'M'}; archive[1] = std::byte{'A'};
    archive[2] = std::byte{'R'}; archive[3] = std::byte{'C'};
    put16(archive, 4, 1);
    put16(archive, 6, 1);
    put32(archive, 8, data_offset);
    put32(archive, 12, melee::native::kHsdMaterialRecordSize);
    put32(archive, 16, name_offset);
    put16(archive, 20, 7);
    const char name[] = "fighter";
    for (std::size_t i = 0; i != 7; ++i) archive[name_offset + i] = static_cast<std::byte>(name[i]);
    const auto record = material_record();
    std::copy(record.begin(), record.end(), archive.begin() + data_offset);
    return archive;
}

} // namespace

int main()
{
    using namespace melee::native;
    const auto bytes = archive_with_material();
    const auto archive = NativeArchive::parse(bytes);
    const auto material = load_material_asset(archive, "fighter", 0xA5000000U);
    assert((material.ambient == NativeColor{1, 2, 3, 4}));
    assert((material.diffuse == NativeColor{5, 6, 7, 8}));
    assert((material.specular == NativeColor{9, 10, 11, 12}));
    assert(material.alpha == 0.5F && material.shininess == 10.0F);
    assert(material.render_mode == 0xA5000000U);
    assert(material.tev_stages.empty());

    bool rejected = false;
    try { (void)decode_hsd_material(std::span<const std::byte>(bytes.data() + 48, 19)); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { (void)decode_hsd_material(material_record(0x7fc00000)); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { (void)load_material_asset(archive, "missing"); }
    catch (const std::out_of_range&) { rejected = true; }
    assert(rejected);

    const auto root = std::filesystem::temp_directory_path() / "melee-native-material-asset-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream output(root / "materials.marc", std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    NativeAssetStore assets{NativeFileSystem(root)};
    const auto from_store = read_material_asset(assets, "materials.marc", "fighter", 7);
    assert(from_store.render_mode == 7);
    std::filesystem::remove_all(root);

    std::cout << "native material asset tests passed\n";
}
