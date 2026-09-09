#pragma once

#include "native_dat_archive.h"
#include "native_material.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace melee::native {

struct NativeDatMaterialRef {
    std::size_t descriptor_offset = 0;
    std::size_t material_offset = 0;
    std::uint32_t render_mode = 0;
    NativeMaterial material;
};

// Walk the documented HSD_Joint -> HSD_DObjDesc -> HSD_MObjDesc graph using
// only DAT relocation slots. No encoded word is treated as a host pointer.
// The node limit bounds malformed/cyclic asset graphs.
std::vector<NativeDatMaterialRef> collect_hsd_joint_materials(
    const NativeDatArchive& archive, std::size_t joint_offset,
    std::size_t max_nodes = 65536);

} // namespace melee::native
