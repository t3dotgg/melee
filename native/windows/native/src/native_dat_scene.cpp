#include "native_dat_scene.h"

#include "native_material_asset.h"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <unordered_set>

namespace melee::native {
namespace {
std::uint32_t be32(std::span<const std::byte> bytes, std::size_t offset)
{
    return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 24) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 16) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 8) |
           static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3]));
}

std::optional<std::size_t> pointer(const NativeDatArchive& archive, std::size_t base,
                                   std::size_t field)
{
    return archive.pointer_target_at(base + field);
}
}

std::vector<NativeDatMaterialRef> collect_hsd_joint_materials(
    const NativeDatArchive& archive, std::size_t joint_offset, std::size_t max_nodes)
{
    if (max_nodes == 0) throw std::invalid_argument("DAT scene node limit must be nonzero");
    archive.data_at(joint_offset, 0x40);
    std::vector<std::size_t> joints{joint_offset};
    std::unordered_set<std::size_t> seen_joints;
    std::vector<NativeDatMaterialRef> result;
    while (!joints.empty()) {
        const auto joint = joints.back();
        joints.pop_back();
        if (!seen_joints.insert(joint).second) continue;
        if (seen_joints.size() > max_nodes) throw std::invalid_argument("DAT scene graph is too large");
        if (const auto child = pointer(archive, joint, 8); child) joints.push_back(*child);
        if (const auto next = pointer(archive, joint, 12); next) joints.push_back(*next);
        const auto dobj = pointer(archive, joint, 16);
        if (!dobj) continue;
        std::unordered_set<std::size_t> seen_dobj;
        for (auto current = dobj; current; ) {
            if (!seen_dobj.insert(*current).second) break;
            const auto bytes = archive.data_at(*current, 16);
            const auto mobj = pointer(archive, *current, 8);
            current = pointer(archive, *current, 4);
            if (!mobj) continue;
            const auto mobj_bytes = archive.data_at(*mobj, 16);
            const auto material = pointer(archive, *mobj, 12);
            if (!material) continue;
            const auto mode = be32(mobj_bytes, 4);
            result.push_back({*mobj, *material, mode,
                              decode_hsd_material(archive, *material, mode)});
            (void)bytes;
        }
    }
    return result;
}
} // namespace melee::native
