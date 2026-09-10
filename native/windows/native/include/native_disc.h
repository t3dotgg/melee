#pragma once

#include "native_asset_store.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace melee::native {

struct DiscEntry {
    std::uint32_t id = 0;
    std::string path;
};

struct DiscReadResult {
    std::uint64_t request_id = 0;
    std::uint32_t entry_id = 0;
    AssetStatus status = AssetStatus::Failed;
    std::vector<std::byte> data;
    std::string error;
};

// Native replacement for DVD entry-number reads. Entry IDs are explicit and
// validated at mount time; I/O is delegated to NativeAssetStore so completion
// ordering and cancellation remain deterministic on the simulation thread.
class NativeDisc final {
public:
    using RequestId = std::uint64_t;
    using Completion = std::function<void(DiscReadResult)>;

    NativeDisc(NativeAssetStore& assets, std::vector<DiscEntry> entries);
    std::vector<std::byte> read(std::uint32_t entry_id) const;
    RequestId request(std::uint32_t entry_id, Completion completion);
    bool cancel(RequestId request_id) { return assets_.cancel(request_id); }
    std::size_t poll() { return assets_.poll(); }
    void wait() { assets_.wait(); }

private:
    NativeAssetStore& assets_;
    std::unordered_map<std::uint32_t, std::string> entries_;
};

} // namespace melee::native
