#include "native_material_asset.h"

#include "native_asset_store.h"

#include <bit>
#include <cstring>
#include <stdexcept>

namespace melee::native {
namespace {

std::uint8_t byte_at(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint8_t>(bytes[offset]);
}

float be_float(std::span<const std::byte> bytes, std::size_t offset)
{
    const std::uint32_t bits = (static_cast<std::uint32_t>(byte_at(bytes, offset)) << 24) |
                               (static_cast<std::uint32_t>(byte_at(bytes, offset + 1)) << 16) |
                               (static_cast<std::uint32_t>(byte_at(bytes, offset + 2)) << 8) |
                               static_cast<std::uint32_t>(byte_at(bytes, offset + 3));
    return std::bit_cast<float>(bits);
}

NativeColor color_at(std::span<const std::byte> bytes, std::size_t offset)
{
    return {byte_at(bytes, offset), byte_at(bytes, offset + 1), byte_at(bytes, offset + 2),
            byte_at(bytes, offset + 3)};
}

} // namespace

NativeMaterial decode_hsd_material(std::span<const std::byte> record,
                                   std::uint32_t render_mode)
{
    if (record.size() != kHsdMaterialRecordSize) {
        throw std::invalid_argument("HSD material record must contain exactly 20 bytes");
    }

    NativeMaterial material;
    material.ambient = color_at(record, 0);
    material.diffuse = color_at(record, 4);
    material.specular = color_at(record, 8);
    material.alpha = be_float(record, 12);
    material.shininess = be_float(record, 16);
    material.render_mode = render_mode;

    std::string error;
    if (!validate_material(material, &error)) {
        throw std::invalid_argument("invalid HSD material: " + error);
    }
    return material;
}

NativeMaterial load_material_asset(const NativeArchive& archive,
                                   std::string_view entry_name,
                                   std::uint32_t render_mode)
{
    const ArchiveEntry* entry = archive.find(entry_name);
    if (entry == nullptr) {
        throw std::out_of_range("material archive entry was not found");
    }
    return decode_hsd_material(archive.data(*entry), render_mode);
}

NativeMaterial read_material_asset(const NativeAssetStore& assets,
                                   std::string_view archive_path,
                                   std::string_view entry_name,
                                   std::uint32_t render_mode)
{
    const auto archive_bytes = assets.read_file(archive_path);
    const auto archive = NativeArchive::parse(archive_bytes);
    return load_material_asset(archive, entry_name, render_mode);
}

} // namespace melee::native
