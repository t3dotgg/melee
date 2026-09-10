#include "native_disc.h"

#include <stdexcept>

namespace melee::native {

NativeDisc::NativeDisc(NativeAssetStore& assets, std::vector<DiscEntry> entries)
    : assets_(assets)
{
    for (auto& entry : entries) {
        if (entry.id == 0 || entry.path.empty() || !entries_.emplace(entry.id, std::move(entry.path)).second)
            throw std::invalid_argument("disc entries must have unique nonzero IDs and paths");
    }
}

std::vector<std::byte> NativeDisc::read(std::uint32_t entry_id) const
{
    const auto it = entries_.find(entry_id);
    if (it == entries_.end()) throw std::out_of_range("disc entry was not found");
    return assets_.read_file(it->second);
}

NativeDisc::RequestId NativeDisc::request(std::uint32_t entry_id, Completion completion)
{
    const auto it = entries_.find(entry_id);
    if (it == entries_.end()) throw std::out_of_range("disc entry was not found");
    return assets_.request(it->second, [entry_id, completion = std::move(completion)](AssetReadResult result) mutable {
        completion({result.request_id, entry_id, result.status, std::move(result.data), std::move(result.error)});
    });
}

} // namespace melee::native
