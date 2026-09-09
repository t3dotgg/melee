#pragma once

#include "native_archive.h"
#include "native_material.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace melee::native {

class NativeAssetStore;

// HSD_Material is the pointer-free part of an HSD_MObjDesc. Its wire layout is
// five big-endian fields: three GXColor values followed by alpha and
// shininess. The descriptor's pointer fields and TEV expression graph are not
// present in this record and are intentionally not inferred here.
constexpr std::size_t kHsdMaterialRecordSize = 20;

// Decode one serialized HSD_Material record. The render mode belongs to the
// containing HSD_MObjDesc, so callers provide it explicitly rather than
// guessing a value from adjacent bytes.
NativeMaterial decode_hsd_material(std::span<const std::byte> record,
                                   std::uint32_t render_mode = 0);

// Resolve a named MARC entry and decode it as an HSD_Material record. The
// returned values own no archive memory and are safe after the archive is
// released.
NativeMaterial load_material_asset(const NativeArchive& archive,
                                   std::string_view entry_name,
                                   std::uint32_t render_mode = 0);

// Read a MARC archive through the rooted native asset service, then decode the
// selected material entry. No guest pointer or GameCube address is exposed.
NativeMaterial read_material_asset(const NativeAssetStore& assets,
                                   std::string_view archive_path,
                                   std::string_view entry_name,
                                   std::uint32_t render_mode = 0);

} // namespace melee::native
